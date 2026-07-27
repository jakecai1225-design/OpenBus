#include "canutils.h"
#include "core/canframe.h"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace CanUtils {

// ============================================================
//  格式化
// ============================================================

QString formatTime(double seconds)
{
    return QString::number(seconds, 'f', 6);
}

QString formatId(quint32 id, bool extended)
{
    if (extended)
        return QStringLiteral("0x%1").arg(id & 0x1FFFFFFF, 8, 16, QChar('0')).toUpper();
    return QStringLiteral("0x%1").arg(id & 0x7FF, 3, 16, QChar('0')).toUpper();
}

QString formatData(const QByteArray &data)
{
    QString result;
    result.reserve(data.size() * 3);
    for (int i = 0; i < data.size(); ++i) {
        if (i > 0) result += ' ';
        result += QStringLiteral("%1").arg(static_cast<unsigned char>(data[i]), 2, 16, QChar('0')).toUpper();
    }
    return result;
}

QString formatDlc(quint8 dlc, bool fd)
{
    if (!fd)
        return QString::number(dlc);
    return QStringLiteral("%1 (%2)")
        .arg(dlc)
        .arg(CanFrame::dlcToLength(dlc));
}

QString formatFlags(const CanFrame &frame)
{
    QStringList flags;
    if (frame.fd)            flags << "FD";
    if (frame.bitrateSwitch) flags << "BRS";
    if (frame.errorState)    flags << "ESI";
    if (frame.extended)      flags << "EXT";
    if (frame.isErrorFrame()) flags << "ERR";
    return flags.join(' ');
}

// ============================================================
//  十六进制解析
// ============================================================

quint32 parseHex(const QString &s)
{
    QString trimmed = s.trimmed();
    if (trimmed.startsWith("0x", Qt::CaseInsensitive))
        trimmed = trimmed.mid(2);
    bool ok = false;
    quint32 val = trimmed.toUInt(&ok, 16);
    return ok ? val : 0xFFFFFFFF;
}

// ============================================================
//  过滤器解析 — 递归下降
// ============================================================

namespace {

/// 单个 token
struct Token {
    enum Type {
        TokEnd, TokIdent, TokHex, TokDec, TokComma,
        TokEq, TokNe, TokGt, TokLt, TokLParen, TokRParen
    } type;
    QString text;
};

/// 词法分析器
class Lexer {
public:
    explicit Lexer(const QString &src) : m_src(src), m_pos(0) {}

    Token next()
    {
        skipSpace();
        if (m_pos >= m_src.size())
            return {Token::TokEnd, ""};

        QChar c = m_src[m_pos];

        if (c == '(') { m_pos++; return {Token::TokLParen, "("}; }
        if (c == ')') { m_pos++; return {Token::TokRParen, ")"}; }
        if (c == ',') { m_pos++; return {Token::TokComma, ","}; }
        if (c == '=') { m_pos++; if (peek() == '=') { m_pos++; } return {Token::TokEq, "=="}; }
        if (c == '!') {
            m_pos++;
            if (peek() == '=') { m_pos++; return {Token::TokNe, "!="}; }
            m_pos--; // 处理 !fd 等
        }
        if (c == '>') { m_pos++; return {Token::TokGt, ">"}; }
        if (c == '<') { m_pos++; return {Token::TokLt, "<"}; }

        // 十六进制数字
        if (c == '0' && (m_pos + 1 < m_src.size()) && (m_src[m_pos + 1] == 'x' || m_src[m_pos + 1] == 'X')) {
            m_pos += 2;
            return lexNumber(/*hex=*/true);
        }
        if (c.isDigit()) {
            return lexNumber(/*hex=*/false);
        }

        // 标识符
        if (c.isLetter()) {
            int start = m_pos;
            while (m_pos < m_src.size() && (m_src[m_pos].isLetterOrNumber() || m_src[m_pos] == '_'))
                m_pos++;
            return {Token::TokIdent, m_src.mid(start, m_pos - start).toLower()};
        }

        // 未知字符
        m_pos++;
        return {Token::TokEnd, ""};
    }

private:
    const QString &m_src;
    int m_pos;

    QChar peek() const { return m_pos < m_src.size() ? m_src[m_pos] : QChar(); }

    void skipSpace()
    {
        while (m_pos < m_src.size() && m_src[m_pos].isSpace())
            m_pos++;
    }

    Token lexNumber(bool hex)
    {
        int start = m_pos;
        while (m_pos < m_src.size()) {
            QChar c = m_src[m_pos];
            if (hex) {
                if (c.isDigit() || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                    m_pos++;
                else
                    break;
            } else {
                if (c.isDigit())
                    m_pos++;
                else
                    break;
            }
        }
        QString num = m_src.mid(start, m_pos - start);
        return {hex ? Token::TokHex : Token::TokDec, num};
    }
};

/// 递归下降解析器
class Parser {
public:
    explicit Parser(const QString &expr) : m_lex(expr), m_ok(true)
    {
        m_cur = m_lex.next();
    }

    CanUtils::FilterPredicate parse()
    {
        auto p = parseOr();
        if (m_cur.type != Token::TokEnd)
            m_ok = false;
        return m_ok ? p : CanUtils::FilterPredicate{};
    }

    bool ok() const { return m_ok; }

private:
    Lexer m_lex;
    Token m_cur;
    bool m_ok;

    void advance() { m_cur = m_lex.next(); }

    // or := and ('or' and)*
    CanUtils::FilterPredicate parseOr()
    {
        auto left = parseAnd();
        while (m_ok && m_cur.type == Token::TokIdent && m_cur.text == "or") {
            advance();
            auto right = parseAnd();
            auto l = std::move(left);
            left = [l, r = std::move(right)](const CanFrame &f) { return l(f) || r(f); };
        }
        return left;
    }

    // and := not ('and' not)*
    CanUtils::FilterPredicate parseAnd()
    {
        auto left = parseNot();
        while (m_ok && m_cur.type == Token::TokIdent && m_cur.text == "and") {
            advance();
            auto right = parseNot();
            auto l = std::move(left);
            left = [l, r = std::move(right)](const CanFrame &f) { return l(f) && r(f); };
        }
        return left;
    }

    // not := 'not' not | atom
    CanUtils::FilterPredicate parseNot()
    {
        if (m_ok && m_cur.type == Token::TokIdent && m_cur.text == "not") {
            advance();
            auto inner = parseNot();
            return [i = std::move(inner)](const CanFrame &f) { return !i(f); };
        }
        return parseAtom();
    }

    // atom := '(' or ')' | simple
    CanUtils::FilterPredicate parseAtom()
    {
        if (m_cur.type == Token::TokLParen) {
            advance();
            auto p = parseOr();
            if (m_cur.type != Token::TokRParen) { m_ok = false; return {}; }
            advance();
            return p;
        }
        return parseSimple();
    }

    // simple := ident op? value?
    CanUtils::FilterPredicate parseSimple()
    {
        if (m_cur.type == Token::TokHex) {
            // 裸 hex -> 按 ID 匹配
            quint32 id = parseHex("0x" + m_cur.text);
            advance();
            return [id](const CanFrame &f) { return (f.id & 0x1FFFFFFF) == id; };
        }
        if (m_cur.type != Token::TokIdent) { m_ok = false; return {}; }

        QString kw = m_cur.text;
        advance();

        // 关键字标志
        if (kw == "fd")
            return [](const CanFrame &f) { return f.fd; };
        if (kw == "ext")
            return [](const CanFrame &f) { return f.extended; };
        if (kw == "std")
            return [](const CanFrame &f) { return !f.extended; };
        if (kw == "rx")
            return [](const CanFrame &f) { return f.direction == CanFrame::Rx; };
        if (kw == "tx")
            return [](const CanFrame &f) { return f.direction == CanFrame::Tx; };

        // id / dlc / ch / data
        if (kw == "id") {
            if (m_cur.type == Token::TokEq) {
                advance();
                quint32 id = expectHex();
                return [id](const CanFrame &f) { return (f.id & 0x1FFFFFFF) == id; };
            }
            if (m_cur.type == Token::TokNe) {
                advance();
                quint32 id = expectHex();
                return [id](const CanFrame &f) { return (f.id & 0x1FFFFFFF) != id; };
            }
            if (m_cur.type == Token::TokIdent && m_cur.text == "in") {
                advance();
                QVector<quint32> ids;
                if (m_cur.type != Token::TokHex && m_cur.type != Token::TokDec) { m_ok = false; return {}; }
                ids << expectHex();
                while (m_cur.type == Token::TokComma) {
                    advance();
                    ids << expectHex();
                }
                return [ids](const CanFrame &f) {
                    return ids.contains(f.id & 0x1FFFFFFF);
                };
            }
            m_ok = false;
            return {};
        }

        if (kw == "dlc") {
            auto op = m_cur.type;
            advance();
            int val = expectDec();
            if (op == Token::TokEq) return [val](const CanFrame &f) { return f.dlc == val; };
            if (op == Token::TokNe) return [val](const CanFrame &f) { return f.dlc != val; };
            if (op == Token::TokGt) return [val](const CanFrame &f) { return f.dlc > val; };
            if (op == Token::TokLt) return [val](const CanFrame &f) { return f.dlc < val; };
            m_ok = false;
            return {};
        }

        if (kw == "ch") {
            if (m_cur.type == Token::TokEq) {
                advance();
                int val = expectDec();
                return [val](const CanFrame &f) { return f.channel == val; };
            }
            m_ok = false;
            return {};
        }

        if (kw == "data") {
            if (m_cur.type == Token::TokIdent && m_cur.text == "contains") {
                advance();
                // 收集十六进制字节
                QByteArray pattern;
                while (m_cur.type == Token::TokHex || m_cur.type == Token::TokDec) {
                    QString s = m_cur.text;
                    advance();
                    // 逐字节解析
                    if (s.length() % 2 != 0) s.prepend('0');
                    for (int i = 0; i < s.length(); i += 2)
                        pattern.append(static_cast<char>(s.mid(i, 2).toUInt(nullptr, 16)));
                }
                if (pattern.isEmpty()) { m_ok = false; return {}; }
                return [pattern](const CanFrame &f) { return f.data.contains(pattern); };
            }
            m_ok = false;
            return {};
        }

        // !fd 处理
        if (kw.startsWith("!")) {
            QString neg = kw.mid(1);
            if (neg == "fd")
                return [](const CanFrame &f) { return !f.fd; };
            if (neg == "ext")
                return [](const CanFrame &f) { return !f.extended; };
        }

        m_ok = false;
        return {};
    }

    quint32 expectHex()
    {
        if (m_cur.type == Token::TokHex) {
            quint32 v = parseHex("0x" + m_cur.text);
            advance();
            return v;
        }
        if (m_cur.type == Token::TokDec) {
            quint32 v = parseHex("0x" + m_cur.text);
            advance();
            return v;
        }
        m_ok = false;
        return 0;
    }

    int expectDec()
    {
        if (m_cur.type == Token::TokDec || m_cur.type == Token::TokHex) {
            int v = m_cur.text.toInt();
            advance();
            return v;
        }
        m_ok = false;
        return 0;
    }
};

} // anonymous namespace

// ============================================================
//  公共接口
// ============================================================

FilterPredicate parseFilter(const QString &expr)
{
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty())
        return [](const CanFrame &) { return true; }; // 无过滤

    Parser parser(trimmed);
    return parser.parse();
}

bool isFilterValid(const QString &expr)
{
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty())
        return true;
    Parser parser(trimmed);
    parser.parse();
    return parser.ok();
}

QString filterHelp()
{
    return QStringLiteral(
        "过滤器语法:\n"
        "  0x123            匹配 ID 为 0x123 的帧\n"
        "  id == 0x123      匹配指定 ID\n"
        "  id != 0x123      排除指定 ID\n"
        "  id in 0x100,0x200  匹配多个 ID\n"
        "  dlc > 8          DLC 大于 8 (CAN FD)\n"
        "  dlc == 8         DLC 等于 8\n"
        "  fd               仅 CAN FD 帧\n"
        "  ext              仅扩展帧\n"
        "  std              仅标准帧\n"
        "  rx               仅接收帧\n"
        "  tx               仅发送帧\n"
        "  ch == 1          匹配通道 1\n"
        "  data contains 01 02  数据包含字节序列\n"
        "  and / or / not   逻辑组合\n"
        "  示例: id == 0x123 and dlc > 8\n"
        "        fd and ext\n"
        "        not (id == 0x100 or id == 0x200)"
    );
}

} // namespace CanUtils
