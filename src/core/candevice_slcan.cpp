#include "candevice_slcan.h"
#include "logging.h"

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

/// SLCAN 标准波特率表（Lawicel S0..S8，方案 §14.5.2 保守公共子集）
constexpr int kSlcanBaudTable[] = {
    10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000
};

/// COM 名自然排序（COM2 < COM10，避免字典序 COM10 < COM2）
bool comPortLess(const QString &a, const QString &b)
{
    const auto num = [](const QString &s) {
        int i = 3; // 跳过 "COM"
        while (i < s.size() && s.at(i).isDigit()) ++i;
        bool ok = false;
        const int v = s.mid(3, i - 3).toInt(&ok);
        return ok ? v : -1;
    };
    return num(a) < num(b);
}

/// 解析一行 SLCAN 接收报文；t/T=数据帧、r/R=远程帧（映射为空数据帧，
/// 与 ZLG 后端"RTR 不单独建模"的既有约定一致）；F/z/杂散应答行忽略
bool parseSlcanLine(const QByteArray &line, std::vector<CanFrame> &out)
{
    if (line.isEmpty())
        return false;
    const char c = line.at(0);
    const bool ext = (c == 'T' || c == 'R');
    const bool rtr = (c == 'r' || c == 'R');
    if (!ext && !rtr && c != 't')
        return false; // F/z/\x07 等状态或应答行

    const int idLen = ext ? 8 : 3;
    if (line.size() < 1 + idLen)
        return false;
    bool ok = false;
    const quint32 id = line.mid(1, idLen).toUInt(&ok, 16);
    if (!ok)
        return false;

    CanFrame frame;
    frame.id = id & (ext ? 0x1FFFFFFFu : 0x7FFu);
    frame.extended = ext;
    frame.fd = false;
    frame.channel = 1;
    frame.direction = CanFrame::Rx;

    int pos = 1 + idLen;
    if (rtr) {
        // 长度位可选（r123 / r304）；无 RTR 标志位，仅保留 DLC
        if (line.size() > pos)
            frame.dlc = quint8(line.mid(pos, 1).toUInt(&ok, 16) & 0xF);
        out.push_back(frame);
        return true;
    }

    if (line.size() < pos + 1)
        return false;
    const int len = int(line.mid(pos, 1).toUInt(&ok, 16) & 0xF);
    ++pos;
    if (len > 8 || line.size() < pos + len * 2)
        return false;

    QByteArray data(len, Qt::Uninitialized);
    for (int i = 0; i < len; ++i) {
        const quint8 byte = quint8(line.mid(pos + i * 2, 2).toUInt(&ok, 16) & 0xFF);
        if (!ok)
            return false;
        data[i] = char(byte);
    }
    frame.dlc = quint8(len);
    frame.data = data;
    out.push_back(frame);
    return true;
}

} // namespace

// ============================================================
//  构造 / 析构
// ============================================================

CanDeviceSlcan::CanDeviceSlcan(int subType)
{
    // 目前各型号行为一致（115200-8N1 + 公共命令子集），型号仅用于市场页展示
    (void)subType;
}

CanDeviceSlcan::~CanDeviceSlcan()
{
    close();
}

// ============================================================
//  枚举（无厂商 SDK，串口即设备）
// ============================================================

QStringList CanDeviceSlcan::availablePorts()
{
    QStringList ports;
#ifdef Q_OS_WIN
    // QueryDosDeviceW 列出全部符号链接，过滤 "COM" 前缀即串口设备
    std::wstring buf(8192, L'\0');
    DWORD n = 0;
    for (;;) {
        n = QueryDosDeviceW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n > 0)
            break;
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            buf.resize(buf.size() * 2);
            continue;
        }
        return ports; // 无设备或查询失败
    }
    const wchar_t *p = buf.data();
    while (*p) {
        const int len = int(wcslen(p));
        const QString name = QString::fromWCharArray(p, len);
        if (name.startsWith(QStringLiteral("COM")))
            ports << name;
        p += len + 1;
    }
    std::sort(ports.begin(), ports.end(), comPortLess);
#else
    OPENBUS_LOG_WARN("SLCAN", "当前平台未实现串口枚举");
#endif
    return ports;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceSlcan::enumerate()
{
    // 枚举零副作用：不逐个打开串口探测（占用风险 + UI 线程耗时），
    // 列出全部 COM 口由用户选择；固件名在 open() 阶段经 V 命令获取
    std::vector<DeviceInfo> out;
    const QStringList ports = availablePorts();
    for (int i = 0; i < ports.size(); ++i) {
        DeviceInfo di;
        di.brand = Brand::SLCAN;
        di.name = QStringLiteral("SLCAN @ %1").arg(ports.at(i));
        di.deviceType = 0;
        di.deviceIndex = i;
        di.channels = 1;
        di.driverId = QStringLiteral("slcan");
        di.hasHwTimestamp = false; // 串口文本协议无硬件时间戳（方案 §14.6）
        out.push_back(di);
    }
    return out;
}

// ============================================================
//  打开 / 关闭
// ============================================================

bool CanDeviceSlcan::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    (void)dataBaud;
    if (m_opened)
        close();

    if (canFd) {
        OPENBUS_LOG_ERROR("SLCAN", "SLCAN 协议不支持 CAN FD（各固件 FD 方言互不兼容）");
        return false;
    }
    const int sIdx = baudToSlcanIndex(arbBaud);
    if (sIdx < 0) {
        OPENBUS_LOG_ERROR("SLCAN", "波特率 {} 不在 SLCAN 标准表 (10k/20k/50k/100k/125k/250k/500k/800k/1M)", arbBaud);
        return false;
    }
    if (channel != 0)
        OPENBUS_LOG_WARN("SLCAN", "SLCAN 为单通道设备，忽略通道号 {}", channel);

    // 串口解析：vendorCtrl 指定优先，否则按枚举序号取
    QString port = m_portName;
    if (port.isEmpty()) {
        const QStringList ports = availablePorts();
        if (devIndex < 0 || devIndex >= ports.size()) {
            OPENBUS_LOG_ERROR("SLCAN", "串口序号 {} 超出范围（共 {} 个）", devIndex, ports.size());
            return false;
        }
        port = ports.at(devIndex);
    }

    if (!openSerial(port, m_uartBaud))
        return false;

    // Lawicel 打开序列：C(复位会话) → V(版本) → S(波特率) → O(上线)。
    // 各固件对 C/S/O 的应答行为不一（应答 \r / BEL / 静默），一律不等待，
    // 仅 V 为查询命令、按 200ms 宽限读固件名；杂散应答字节由 recv 丢弃。
    writeLine("C");
    drainRxAged();
    m_deviceName = QStringLiteral("SLCAN @ %1").arg(port);
    if (writeLine("V")) {
        const QByteArray v = readLine(200);
        if (!v.isEmpty())
            m_deviceName += QStringLiteral(" (%1)").arg(QString::fromLatin1(v));
        else
            OPENBUS_LOG_WARN("SLCAN", "{} 无 V 应答：可能非 SLCAN 设备或串口波特率不符", port.toStdString());
    }

    if (!writeLine(QByteArray("S") + QByteArray::number(sIdx))
        || !writeLine("O")) {
        OPENBUS_LOG_ERROR("SLCAN", "{} 发送 S/O 命令失败", port.toStdString());
        close();
        return false;
    }
    drainRxAged();

    m_channel = 0;
    m_busOn = true;
    m_opened = true;
    OPENBUS_LOG_INFO("SLCAN", "已打开 {}（波特率 {}，串口 {}）", m_deviceName.toStdString(), arbBaud, m_uartBaud);
    return true;
}

void CanDeviceSlcan::close()
{
    if (m_handle) {
        if (m_busOn)
            writeLine("C"); // 优雅下线；失败也只能硬关
        QThread::msleep(20);
    }
    closeSerial();
    m_opened = false;
    m_busOn = false;
}

// ============================================================
//  收发
// ============================================================

int CanDeviceSlcan::send(const CanFrame &frame)
{
    if (!m_opened || !m_busOn)
        return 0;
    if (frame.fd) {
        OPENBUS_LOG_WARN("SLCAN", "SLCAN 不支持 CAN FD 帧发送");
        return 0;
    }

    QByteArray cmd;
    if (frame.extended) {
        cmd = 'T' + QByteArray::number(frame.id & 0x1FFFFFFFu, 16).rightJustified(8, '0');
    } else {
        cmd = 't' + QByteArray::number(frame.id & 0x7FFu, 16).rightJustified(3, '0');
    }
    const int len = qMin(frame.data.size(), 8);
    cmd += QByteArray::number(len, 16); // 长度占 1 个十六进制字符（0..8）
    for (int i = 0; i < len; ++i)
        cmd += QByteArray(1, frame.data.at(i)).toHex();

    return writeLine(cmd) ? 1 : 0;
}

int CanDeviceSlcan::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened)
        return 0;
    QElapsedTimer timer;
    timer.start();
    int got = 0;

    forever {
        const int slice = (timeoutMs < 0)
            ? 50
            : qBound(0, timeoutMs - int(timer.elapsed()), 50);
        const QByteArray line = readLine(slice);
        if (!line.isEmpty()) {
            if (parseSlcanLine(line, outFrames))
                ++got;
            continue;
        }
        // 无新行：阻塞模式已收齐一波即返回，否则继续等；
        // 限时模式等满超时窗口（与 recvLoop 的 10ms 轮询节奏对齐）
        if (timeoutMs < 0) {
            if (got > 0)
                break;
            continue;
        }
        if (int(timer.elapsed()) >= timeoutMs)
            break;
    }
    return got;
}

int CanDeviceSlcan::pendingCount() const
{
    // 近似值：缓冲中行终止符数量（用于上层判断 recv 是否会阻塞）
    int n = 0;
    for (const char ch : m_rxBuf)
        if (ch == '\r' || ch == '\n')
            ++n;
    return n;
}

QString CanDeviceSlcan::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("SLCAN") : m_deviceName;
}

// ============================================================
//  厂商扩展 / 硬件滤波
// ============================================================

bool CanDeviceSlcan::vendorCtrl(int cmd, void *param)
{
    switch (cmd) {
    case VendorCmdSetPort:
        if (!param)
            return false;
        m_portName = *static_cast<QString *>(param);
        return true;
    case VendorCmdSetUartBaud:
        if (!param)
            return false;
        m_uartBaud = *static_cast<int *>(param);
        return true;
    default:
        return false;
    }
}

bool CanDeviceSlcan::setAcceptanceFilter(quint32 code, quint32 mask, bool extended)
{
    (void)extended; // M/m 命令与标准/扩展帧格式无关
    if (!m_handle)
        return false;
    // Lawicel：M=验收码，m=掩码（1=比较，0=忽略），8 位十六进制
    const QByteArray m = QByteArray("M") + QByteArray::number(code, 16).rightJustified(8, '0').toUpper();
    const QByteArray k = QByteArray("m") + QByteArray::number(mask, 16).rightJustified(8, '0');
    return writeLine(m) && writeLine(k);
}

bool CanDeviceSlcan::clearAcceptanceFilter()
{
    if (!m_handle)
        return false;
    // 全部位不比较（掩码 0）= 接收所有帧
    return writeLine("M00000000") && writeLine("m00000000");
}

// ============================================================
//  串口底层（Win32）
// ============================================================

bool CanDeviceSlcan::openSerial(const QString &portName, int uartBaud)
{
    closeSerial();
#ifdef Q_OS_WIN
    // \\.\ 前缀：COM10 及以上必须走设备命名空间路径
    const QString path = QStringLiteral("\\\\.\\") + portName;
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),
                           GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        OPENBUS_LOG_ERROR("SLCAN", "打开串口 {} 失败 (GetLastError={})",
                          portName.toStdString(), int(GetLastError()));
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    bool ok = GetCommState(h, &dcb) != FALSE;
    if (ok) {
        dcb.BaudRate = DWORD(uartBaud);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        // DTR/RTS 均不置位：避免 ESP32 / Arduino 的 DTR-RTS 自动复位电路误触发
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fInX = FALSE;
        dcb.fOutX = FALSE;
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fAbortOnError = FALSE;
        ok = SetCommState(h, &dcb) != FALSE;
    }
    if (ok) {
        // 读：首字节后 5ms 无新数据即返回（低延迟聚合）；写：10ms/字节 + 200ms 兜底
        COMMTIMEOUTS to = {};
        to.ReadIntervalTimeout = 5;
        to.ReadTotalTimeoutMultiplier = 0;
        to.ReadTotalTimeoutConstant = 0;
        to.WriteTotalTimeoutMultiplier = 10;
        to.WriteTotalTimeoutConstant = 200;
        ok = SetCommTimeouts(h, &to) != FALSE;
    }
    if (!ok) {
        OPENBUS_LOG_ERROR("SLCAN", "配置串口 {} 失败 (GetLastError={})",
                          portName.toStdString(), int(GetLastError()));
        CloseHandle(h);
        return false;
    }

    PurgeComm(h, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    m_handle = h;
    return true;
#else
    (void)portName;
    (void)uartBaud;
    OPENBUS_LOG_ERROR("SLCAN", "当前平台未实现串口后端");
    return false;
#endif
}

void CanDeviceSlcan::closeSerial()
{
#ifdef Q_OS_WIN
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
#endif
    m_rxBuf.clear();
}

bool CanDeviceSlcan::writeLine(const QByteArray &cmd)
{
#ifdef Q_OS_WIN
    if (!m_handle)
        return false;
    const QByteArray pkt = cmd + '\r';
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_handle), pkt.constData(),
                   DWORD(pkt.size()), &written, nullptr)
        || written != DWORD(pkt.size())) {
        OPENBUS_LOG_WARN("SLCAN", "串口写入失败 (GetLastError={})", int(GetLastError()));
        return false;
    }
    return true;
#else
    (void)cmd;
    return false;
#endif
}

QByteArray CanDeviceSlcan::readLine(int timeoutMs)
{
    if (!m_handle)
        return QByteArray();
    QElapsedTimer timer;
    timer.start();
    forever {
        // 缓冲中已有完整行 → 取出（空行跳过）
        int idx = m_rxBuf.indexOf('\r');
        if (idx < 0)
            idx = m_rxBuf.indexOf('\n');
        if (idx >= 0) {
            const QByteArray line = m_rxBuf.left(idx);
            m_rxBuf.remove(0, idx + 1);
            if (!line.isEmpty())
                return line;
            continue;
        }

        const int remain = timeoutMs - int(timer.elapsed());
        if (remain <= 0)
            return QByteArray();

#ifdef Q_OS_WIN
        // 非重叠读：COMMTIMEOUTS 保证最多阻塞 remain（切片 20ms）ms
        char buf[256];
        DWORD got = 0;
        const DWORD wait = DWORD(qMin(remain, 20));
        if (!ReadFile(static_cast<HANDLE>(m_handle), buf, sizeof(buf), &got, nullptr))
            return QByteArray();
        if (got > 0)
            m_rxBuf.append(buf, int(got));
#endif
    }
}

void CanDeviceSlcan::drainRxAged()
{
    // 丢弃启动期杂散应答/回显（80ms 窗口内的存量字节）
    QElapsedTimer timer;
    timer.start();
    while (!timer.hasExpired(80)) {
        const QByteArray line = readLine(20);
        if (line.isEmpty() && m_rxBuf.isEmpty())
            break;
    }
}

int CanDeviceSlcan::baudToSlcanIndex(int baud)
{
    for (int i = 0; i < int(sizeof(kSlcanBaudTable) / sizeof(kSlcanBaudTable[0])); ++i)
        if (kSlcanBaudTable[i] == baud)
            return i;
    return -1;
}
