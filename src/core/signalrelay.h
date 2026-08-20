#ifndef SIGNALRELAY_H
#define SIGNALRELAY_H

#include <QObject>
#include <functional>

/**
 * @brief 字符串信号 → lambda/std::function 桥接（DEF-08）
 *
 * MinGW 未做 dllimport 声明的跨 DLL 场景，新式 PMF connect 的信号查找
 * （IndexOfMethod 成员函数地址比较）拿到的是本地跳转 thunk 地址，与
 * data.dll 内 moc 比较的真实地址不等，连接静默失败（"signal not found"）。
 * 字符串 SIGNAL/SLOT connect 走运行期签名匹配，不受影响——但字符串重载
 * 只接收 SLOT() 字符串槽，不能挂 lambda/PMF 槽。本类以少量常用参数签名
 * 的槽转发到 std::function，补齐「字符串信号 + 任意可调用对象」组合。
 *
 * 实现编入 openbus_data（vtable 单一副本，经导入库供壳/模块 DLL 使用）。
 *
 * 用法（parent 管理生命周期；槽参数少于信号参数合法，多余信号参数被忽略）：
 *   auto *relay = new SignalRelay(this);
 *   relay->fire0 = [this] { viewport()->update(); };
 *   connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
 *           relay, SLOT(fire()));
 *
 * 需要信号参数时选择对应槽并赋值对应 fn 成员：
 *   relay->fnString = [this](const QString &msg) { ... };
 *   connect(sender, SIGNAL(errorOccurred(QString)), relay, SLOT(fireQString(QString)));
 */
class SignalRelay : public QObject
{
    Q_OBJECT

public:
    explicit SignalRelay(QObject *parent = nullptr);

    // 与 public slots 一一对应的转发目标（按需赋值其一；与槽函数不同名避免遮蔽）
    std::function<void()> fire0;                                // ()
    std::function<void(bool, const QString &)> fnBoolString;    // (bool, QString)
    std::function<void(const QString &)> fnString;              // (QString)
    std::function<void(const QString &, int)> fnStringInt;      // (QString, int)
    std::function<void(int, int)> fnIntInt;                     // (int, int)

public slots:
    void fire();
    void fireBoolQString(bool a, const QString &b);
    void fireQString(const QString &a);
    void fireQStringInt(const QString &a, int b);
    void fireIntInt(int a, int b);
};

#endif // SIGNALRELAY_H
