#include "filter_engine.h"
#include "canframe.h"
#include "logging.h"

#include <exprtk.hpp>

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QByteArray>

#include <array>
#include <string>
#include <vector>

namespace {

/// 最多支持的 "data contains" 子句数
constexpr size_t kMaxDataMatches = 16;

/// 将用户友好的过滤表达式预处理为 exprtk 可编译的 C 风格表达式
class Preprocessor
{
public:
    /// 运行预处理，结果写入 m_output
    QString run(const QString &input)
    {
        m_output = input;
        m_numData = 0;
        extractDataContains();
        convertIdIn();
        replaceLogicalKeywords();
        wrapBareHex();
        convertHexToDecimal();
        return m_output;
    }

    size_t numData() const { return m_numData; }
    const QByteArray &dataPattern(size_t i) const { return m_dataPatterns[i]; }

private:
    QString m_output;
    size_t m_numData = 0;
    std::array<QByteArray, kMaxDataMatches> m_dataPatterns{};

    // --------------------------------------------------------
    //  1. 提取 "data contains <hex bytes>" 子句
    //    替换为 __data_N__ 变量，字节模式保存到 m_dataPatterns
    // --------------------------------------------------------
    void extractDataContains()
    {
        QRegularExpression rx(
            "data\\s+contains\\s+"
            "((?:0x[0-9a-fA-F]+|[0-9a-fA-F]{2,})"
            "(?:\\s+(?:0x[0-9a-fA-F]+|[0-9a-fA-F]{2,}))*)",
            QRegularExpression::CaseInsensitiveOption);

        QString result;
        int lastEnd = 0;
        auto it = rx.globalMatch(m_output);
        while (it.hasNext()) {
            auto m = it.next();
            result += m_output.mid(lastEnd, m.capturedStart() - lastEnd);

            // 解析十六进制字节序列
            QString bytesStr = m.captured(1);
            QStringList tokens = bytesStr.split(
                QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            QByteArray pattern;
            for (const QString &tok : tokens) {
                QString s = tok;
                if (s.startsWith("0x", Qt::CaseInsensitive))
                    s = s.mid(2);
                if (s.length() % 2 != 0)
                    s.prepend('0');
                for (int i = 0; i < s.length(); i += 2) {
                    pattern.append(static_cast<char>(
                        s.mid(i, 2).toUInt(nullptr, 16)));
                }
            }

            if (!pattern.isEmpty() && m_numData < kMaxDataMatches) {
                m_dataPatterns[m_numData] = pattern;
                result += "__data_" + QString::number(m_numData) + "__";
                ++m_numData;
            }
            lastEnd = m.capturedEnd();
        }
        result += m_output.mid(lastEnd);
        m_output = result;
    }

    // --------------------------------------------------------
    //  2. 转换 "id in 0x100,0x200,..." → "(id==0x100||id==0x200||...)"
    // --------------------------------------------------------
    void convertIdIn()
    {
        QRegularExpression rx(
            "\\bid\\s+in\\s+"
            "((?:0x[0-9a-fA-F]+|\\d+)"
            "(?:\\s*,\\s*(?:0x[0-9a-fA-F]+|\\d+))*)",
            QRegularExpression::CaseInsensitiveOption);

        QString result;
        int lastEnd = 0;
        auto it = rx.globalMatch(m_output);
        while (it.hasNext()) {
            auto m = it.next();
            result += m_output.mid(lastEnd, m.capturedStart() - lastEnd);

            QStringList ids = m.captured(1).split(',', Qt::SkipEmptyParts);
            QStringList orParts;
            for (const QString &id : ids)
                orParts << "id==" + id.trimmed();
            result += "(" + orParts.join("||") + ")";

            lastEnd = m.capturedEnd();
        }
        result += m_output.mid(lastEnd);
        m_output = result;
    }

    // --------------------------------------------------------
    //  3. 替换逻辑关键字: and→&&, or→||, not→!
    // --------------------------------------------------------
    void replaceLogicalKeywords()
    {
        m_output.replace(
            QRegularExpression("\\band\\b",
                               QRegularExpression::CaseInsensitiveOption),
            "&&");
        m_output.replace(
            QRegularExpression("\\bor\\b",
                               QRegularExpression::CaseInsensitiveOption),
            "||");
        m_output.replace(
            QRegularExpression("\\bnot\\b",
                               QRegularExpression::CaseInsensitiveOption),
            "!");
    }

    // --------------------------------------------------------
    //  4. 包装裸十六进制/十进制数字为 (id==NUMBER)
    //     仅处理不紧跟在比较运算符之后的数字
    // --------------------------------------------------------
    void wrapBareHex()
    {
        QString result;
        result.reserve(m_output.size() + 32);
        int i = 0;
        int len = m_output.size();
        while (i < len) {
            // 检测数字起始
            bool isHex = (i + 1 < len && m_output[i] == '0' &&
                          (m_output[i + 1] == 'x' || m_output[i + 1] == 'X'));
            bool isDec = m_output[i].isDigit() && !isHex;

            // 前一字符不能是字母/数字/下划线（否则是标识符的一部分，如 __data_0__）
            bool prevIsIdent = (i > 0 && (m_output[i - 1].isLetterOrNumber() ||
                                          m_output[i - 1] == '_'));

            if ((isHex || isDec) && !prevIsIdent) {
                int start = i;
                if (isHex) i += 2;
                while (i < len && (m_output[i].isDigit() ||
                                   (m_output[i] >= 'a' && m_output[i] <= 'f') ||
                                   (m_output[i] >= 'A' && m_output[i] <= 'F'))) {
                    ++i;
                }
                // 后一字符不能是字母/下划线（标识符）
                if (i < len && (m_output[i].isLetter() || m_output[i] == '_')) {
                    result += m_output.mid(start, i - start);
                    continue;
                }

                QString num = m_output.mid(start, i - start);

                // 向前跳过空白，检查前一非空白字符
                int j = start - 1;
                while (j >= 0 && m_output[j].isSpace()) --j;

                // 若前一字符是逻辑运算符或表达式起始，则为裸 ID，需包装
                // 注意: != 是"不等于"运算符，不是逻辑非
                bool isBare;
                if (j < 0)
                    isBare = true;
                else if (m_output[j] == '!' && j > 0 && m_output[j - 1] == '=')
                    isBare = false;  // != 运算符
                else if (m_output[j] == '!' )
                    isBare = true;   // 逻辑非
                else if (m_output[j] == '(' || m_output[j] == '&' || m_output[j] == '|')
                    isBare = true;
                else
                    isBare = false;  // =, >, < 等比较运算符

                if (isBare)
                    result += "(id==" + num + ")";
                else
                    result += num;
            } else {
                result += m_output[i];
                ++i;
            }
        }
        m_output = result;
    }

    // --------------------------------------------------------
    //  5. 将所有 0xNNN 十六进制字面量转换为十进制
    //     exprtk 不支持 0x 前缀，必须转为纯数字
    // --------------------------------------------------------
    void convertHexToDecimal()
    {
        QRegularExpression rx("0[xX][0-9a-fA-F]+");
        auto it = rx.globalMatch(m_output);
        QString result;
        int lastEnd = 0;
        while (it.hasNext()) {
            auto m = it.next();
            result += m_output.mid(lastEnd, m.capturedStart() - lastEnd);
            QString hexStr = m.captured(0).mid(2); // 去掉 0x
            bool ok = false;
            quint32 val = hexStr.toUInt(&ok, 16);
            result += ok ? QString::number(val) : m.captured(0);
            lastEnd = m.capturedEnd();
        }
        result += m_output.mid(lastEnd);
        m_output = result;
    }
};

} // anonymous namespace

// ============================================================
//  Impl
// ============================================================

struct FilterEngine::Impl
{
    exprtk::symbol_table<double> symbols;
    exprtk::expression<double> expression;
    exprtk::parser<double> parser;

    // 帧字段变量（evaluate 时更新）
    double var_id  = 0;
    double var_dlc = 0;
    double var_ch  = 0;
    double var_time = 0;
    double var_fd  = 0;
    double var_ext = 0;
    double var_rx  = 0;
    double var_tx  = 0;
    double var_std = 0;

    // data contains 求值结果变量
    std::array<double, kMaxDataMatches> dataVars{};
    std::array<QByteArray, kMaxDataMatches> dataPatterns;
    size_t numData = 0;

    bool compiled = false;
    bool empty = false;
    std::string errorMsg;

    Impl()
    {
        symbols.add_variable("id",   var_id);
        symbols.add_variable("dlc",  var_dlc);
        symbols.add_variable("ch",   var_ch);
        symbols.add_variable("time", var_time);
        symbols.add_variable("fd",   var_fd);
        symbols.add_variable("ext",  var_ext);
        symbols.add_variable("rx",   var_rx);
        symbols.add_variable("tx",   var_tx);
        symbols.add_variable("std",  var_std);
        for (size_t i = 0; i < kMaxDataMatches; ++i) {
            symbols.add_variable(
                "__data_" + std::to_string(i) + "__", dataVars[i]);
        }
        symbols.add_constants();

        expression.register_symbol_table(symbols);
    }
};

// ============================================================
//  FilterEngine
// ============================================================

FilterEngine::FilterEngine()
    : m_impl(std::make_unique<Impl>())
{
}

FilterEngine::~FilterEngine() = default;

bool FilterEngine::compile(const QString &expr)
{
    m_impl->compiled = false;
    m_impl->errorMsg.clear();
    m_impl->numData = 0;

    if (expr.trimmed().isEmpty()) {
        m_impl->empty = true;
        m_impl->compiled = true;
        return true;
    }
    m_impl->empty = false;

    Preprocessor pp;
    QString processed = pp.run(expr);
    m_impl->numData = pp.numData();
    for (size_t i = 0; i < pp.numData(); ++i)
        m_impl->dataPatterns[i] = pp.dataPattern(i);

    std::string exprStr = processed.toStdString();

    // 重置 expression（清除上次编译状态）
    m_impl->expression.release();

    if (m_impl->parser.compile(exprStr, m_impl->expression)) {
        m_impl->compiled = true;
        SIN_LOG_DEBUG("FilterEngine", "compiled: {}", processed.toStdString());
        return true;
    }

    m_impl->errorMsg = m_impl->parser.error();
    SIN_LOG_WARN("FilterEngine", "compile failed: '{}' → '{}', error: {}",
                 expr.toStdString(), processed.toStdString(), m_impl->errorMsg);
    return false;
}

bool FilterEngine::evaluate(const CanFrame &frame) const
{
    if (m_impl->empty)  return true;
    if (!m_impl->compiled) return true;

    // 更新变量
    m_impl->var_id   = static_cast<double>(frame.id & 0x1FFFFFFF);
    m_impl->var_dlc  = static_cast<double>(frame.dlc);
    m_impl->var_ch   = static_cast<double>(frame.channel);
    m_impl->var_time = frame.timestamp;
    m_impl->var_fd   = frame.fd ? 1.0 : 0.0;
    m_impl->var_ext  = frame.extended ? 1.0 : 0.0;
    m_impl->var_rx   = (frame.direction == CanFrame::Rx) ? 1.0 : 0.0;
    m_impl->var_tx   = (frame.direction == CanFrame::Tx) ? 1.0 : 0.0;
    m_impl->var_std  = frame.extended ? 0.0 : 1.0;

    // 求值 data contains 子句
    for (size_t i = 0; i < m_impl->numData; ++i) {
        m_impl->dataVars[i] =
            frame.data.contains(m_impl->dataPatterns[i]) ? 1.0 : 0.0;
    }

    double result = m_impl->expression.value();
    return result != 0.0;
}

bool FilterEngine::isValid() const { return m_impl->compiled; }
bool FilterEngine::isEmpty() const { return m_impl->empty; }

QString FilterEngine::errorString() const
{
    return QString::fromStdString(m_impl->errorMsg);
}
