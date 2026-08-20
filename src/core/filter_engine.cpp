#include "filter_engine.h"
#include "canframe.h"
#include "logging.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QByteArray>

#include <array>
#include <string>
#include <vector>
#include <memory>
#include <cmath>

namespace {

// ============================================================
//  AST 节点 — 轻量递归下降解析器
// ============================================================

enum class NodeKind {
    Number,         // 数值字面量
    Variable,       // 变量: id, dlc, ch, time, fd, ext, rx, tx, std
    Compare,        // 比较: ==, !=, >, <, >=, <=
    Logical,        // 逻辑: &&, ||
    Not,            // 逻辑非
    DataContains,   // data contains <hex bytes>
    IdIn,           // id in {v1, v2, ...}
};

struct ASTNode {
    NodeKind kind;
    double number = 0;
    int varIdx = -1;              // 变量索引 (0-8)
    int op = 0;                   // 运算符
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    QByteArray dataPattern;

    static std::unique_ptr<ASTNode> makeNum(double v) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::Number; n->number = v; return n;
    }
    static std::unique_ptr<ASTNode> makeVar(int idx) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::Variable; n->varIdx = idx; return n;
    }
    static std::unique_ptr<ASTNode> makeCmp(int op,
            std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::Compare; n->op = op;
        n->left = std::move(l); n->right = std::move(r); return n;
    }
    static std::unique_ptr<ASTNode> makeLog(int op,
            std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::Logical; n->op = op;
        n->left = std::move(l); n->right = std::move(r); return n;
    }
    static std::unique_ptr<ASTNode> makeNot(std::unique_ptr<ASTNode> child) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::Not; n->left = std::move(child); return n;
    }
    static std::unique_ptr<ASTNode> makeDataContains(QByteArray pat) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::DataContains; n->dataPattern = pat; return n;
    }
    static std::unique_ptr<ASTNode> makeIdIn(std::vector<double> vals) {
        auto n = std::make_unique<ASTNode>();
        n->kind = NodeKind::IdIn; n->number = static_cast<double>(vals.size());
        // 将值列表存储在 left 链中（简单起见用 Number 节点链）
        if (!vals.empty()) {
            n->left = makeNum(vals[0]);
            auto *cur = n->left.get();
            for (size_t i = 1; i < vals.size(); ++i) {
                cur->right = makeNum(vals[i]);
                cur = cur->right.get();
            }
        }
        return n;
    }
};

// ============================================================
//  预处理器 — 语法糖转换（与原逻辑兼容）
// ============================================================

constexpr size_t kMaxDataMatches = 16;

class Preprocessor
{
public:
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

    // 1. 提取 "data contains <hex bytes>" → __data_N__
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

    // 2. "id in 0x100,0x200" → "(id==0x100||id==0x200)"
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

    // 3. and→&&, or→||, not→!
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

    // 4. 裸十六进制/十进制 → (id==NUMBER)
    void wrapBareHex()
    {
        QString result;
        result.reserve(m_output.size() + 32);
        int i = 0;
        int len = m_output.size();
        while (i < len) {
            bool isHex = (i + 1 < len && m_output[i] == '0' &&
                          (m_output[i + 1] == 'x' || m_output[i + 1] == 'X'));
            bool isDec = m_output[i].isDigit() && !isHex;
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
                if (i < len && (m_output[i].isLetter() || m_output[i] == '_')) {
                    result += m_output.mid(start, i - start);
                    continue;
                }

                QString num = m_output.mid(start, i - start);
                int j = start - 1;
                while (j >= 0 && m_output[j].isSpace()) --j;

                bool isBare;
                if (j < 0)
                    isBare = true;
                else if (m_output[j] == '!' && j > 0 && m_output[j - 1] == '=')
                    isBare = false;
                else if (m_output[j] == '!')
                    isBare = true;
                else if (m_output[j] == '(' || m_output[j] == '&' || m_output[j] == '|')
                    isBare = true;
                else
                    isBare = false;

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

    // 5. 0xNNN → 十进制
    void convertHexToDecimal()
    {
        QRegularExpression rx("0[xX][0-9a-fA-F]+");
        auto it = rx.globalMatch(m_output);
        QString result;
        int lastEnd = 0;
        while (it.hasNext()) {
            auto m = it.next();
            result += m_output.mid(lastEnd, m.capturedStart() - lastEnd);
            QString hexStr = m.captured(0).mid(2);
            bool ok = false;
            quint32 val = hexStr.toUInt(&ok, 16);
            result += ok ? QString::number(val) : m.captured(0);
            lastEnd = m.capturedEnd();
        }
        result += m_output.mid(lastEnd);
        m_output = result;
    }
};

// ============================================================
//  Tokenizer
// ============================================================

enum TokenType {
    TOK_NUMBER, TOK_IDENT,
    TOK_EQ, TOK_NE, TOK_GT, TOK_LT, TOK_GE, TOK_LE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA,
    TOK_EOF, TOK_ERROR
};

struct Token {
    TokenType type = TOK_EOF;
    double numVal = 0;
    QString strVal;
};

class Tokenizer {
public:
    explicit Tokenizer(const QString &input) : m_input(input), m_pos(0) {}

    Token next()
    {
        skipSpaces();
        if (m_pos >= m_input.size())
            return {TOK_EOF, 0, {}};

        QChar c = m_input[m_pos];

        // 双字符运算符
        if (m_pos + 1 < m_input.size()) {
            QString two = m_input.mid(m_pos, 2);
            if (two == "==") { m_pos += 2; return {TOK_EQ, 0, {}}; }
            if (two == "!=") { m_pos += 2; return {TOK_NE, 0, {}}; }
            if (two == ">=") { m_pos += 2; return {TOK_GE, 0, {}}; }
            if (two == "<=") { m_pos += 2; return {TOK_LE, 0, {}}; }
            if (two == "&&") { m_pos += 2; return {TOK_AND, 0, {}}; }
            if (two == "||") { m_pos += 2; return {TOK_OR,  0, {}}; }
        }

        // 单字符运算符
        if (c == '>') { ++m_pos; return {TOK_GT, 0, {}}; }
        if (c == '<') { ++m_pos; return {TOK_LT, 0, {}}; }
        if (c == '!') { ++m_pos; return {TOK_NOT, 0, {}}; }
        if (c == '(') { ++m_pos; return {TOK_LPAREN, 0, {}}; }
        if (c == ')') { ++m_pos; return {TOK_RPAREN, 0, {}}; }
        if (c == ',') { ++m_pos; return {TOK_COMMA, 0, {}}; }

        // 数字（十进制 / 十六进制）
        if (c.isDigit() || (c == '0' && m_pos + 1 < m_input.size() &&
                            (m_input[m_pos + 1] == 'x' || m_input[m_pos + 1] == 'X'))) {
            return readNumber();
        }

        // 标识符 / 关键字
        if (c.isLetter() || c == '_') {
            return readIdent();
        }

        return {TOK_ERROR, 0, QString("意外字符: '%1'").arg(c)};
    }

    Token peek()
    {
        int saved = m_pos;
        auto tok = next();
        m_pos = saved;
        return tok;
    }

private:
    QString m_input;
    int m_pos;

    void skipSpaces()
    {
        while (m_pos < m_input.size() && m_input[m_pos].isSpace())
            ++m_pos;
    }

    Token readNumber()
    {
        int start = m_pos;
        if (m_input[m_pos] == '0' && m_pos + 1 < m_input.size() &&
            (m_input[m_pos + 1] == 'x' || m_input[m_pos + 1] == 'X')) {
            m_pos += 2;
            while (m_pos < m_input.size() &&
                   (m_input[m_pos].isDigit() ||
                    (m_input[m_pos] >= 'a' && m_input[m_pos] <= 'f') ||
                    (m_input[m_pos] >= 'A' && m_input[m_pos] <= 'F')))
                ++m_pos;
            bool ok = false;
            double val = m_input.mid(start + 2, m_pos - start - 2)
                             .toUInt(&ok, 16);
            return {TOK_NUMBER, ok ? val : 0, {}};
        }
        while (m_pos < m_input.size() &&
               (m_input[m_pos].isDigit() || m_input[m_pos] == '.'))
            ++m_pos;
        return {TOK_NUMBER, m_input.mid(start, m_pos - start).toDouble(), {}};
    }

    Token readIdent()
    {
        int start = m_pos;
        while (m_pos < m_input.size() &&
               (m_input[m_pos].isLetterOrNumber() || m_input[m_pos] == '_'))
            ++m_pos;
        QString word = m_input.mid(start, m_pos - start);
        QString lower = word.toLower();
        if (lower == "and") return {TOK_AND,  0, {}};
        if (lower == "or")  return {TOK_OR,   0, {}};
        if (lower == "not") return {TOK_NOT,  0, {}};
        return {TOK_IDENT, 0, word};
    }
};

// ============================================================
//  Parser — 递归下降
//
//  文法:
//    expr     → orExpr
//    orExpr   → andExpr ('||' andExpr)*
//    andExpr  → notExpr ('&&' notExpr)*
//    notExpr  → '!' notExpr | comparison
//    comparison → primary [compOp primary]
//    primary  → '(' expr ')'
//             | 'data' 'contains' hexByte+     (仅当 __data_N__ 未匹配时)
//             | 'id' 'in' number (',' number)*
//             | IDENT                          (变量，truthy 求值)
//             | NUMBER                         (裸数字 → id == N)
// ============================================================

class Parser {
public:
    explicit Parser(const QString &input, size_t numData = 0,
                const std::array<QByteArray, kMaxDataMatches> *patterns = nullptr)
        : m_tok(input), m_numData(numData), m_patterns(patterns) {}

    std::unique_ptr<ASTNode> parse()
    {
        advance();
        auto ast = parseOr();
        if (!m_error.isEmpty())
            return nullptr;
        if (m_cur.type != TOK_EOF) {
            m_error = QString("意外的标记: '%1'").arg(
                m_cur.type == TOK_IDENT ? m_cur.strVal : tokenStr());
            return nullptr;
        }
        return ast;
    }

    QString error() const { return m_error; }

private:
    Tokenizer m_tok;
    Token m_cur;
    QString m_error;
    size_t m_numData;
    const std::array<QByteArray, kMaxDataMatches> *m_patterns = nullptr;

    void advance() { m_cur = m_tok.next(); }
    Token peek()   { return m_tok.peek(); }

    QString tokenStr() const
    {
        switch (m_cur.type) {
        case TOK_NUMBER: return QString::number(m_cur.numVal);
        case TOK_IDENT:  return m_cur.strVal;
        case TOK_EQ:     return "==";
        case TOK_NE:     return "!=";
        case TOK_GT:     return ">";
        case TOK_LT:     return "<";
        case TOK_GE:     return ">=";
        case TOK_LE:     return "<=";
        case TOK_AND:    return "&&";
        case TOK_OR:     return "||";
        case TOK_NOT:    return "!";
        case TOK_LPAREN: return "(";
        case TOK_RPAREN: return ")";
        case TOK_COMMA:  return ",";
        default:         return "?";
        }
    }

    int varIndex(const QString &name)
    {
        QString n = name.toLower();
        if (n == "id")   return 0;
        if (n == "dlc")  return 1;
        if (n == "ch")   return 2;
        if (n == "time") return 3;
        if (n == "fd")   return 4;
        if (n == "ext")  return 5;
        if (n == "rx")   return 6;
        if (n == "tx")   return 7;
        if (n == "std")  return 8;
        if (n == "error") return 9;
        return -1;
    }

    bool isCompOp() const
    {
        return m_cur.type == TOK_EQ || m_cur.type == TOK_NE ||
               m_cur.type == TOK_GT || m_cur.type == TOK_LT ||
               m_cur.type == TOK_GE || m_cur.type == TOK_LE;
    }

    // ---- 文法规则 ----

    std::unique_ptr<ASTNode> parseOr()
    {
        auto left = parseAnd();
        if (!left || m_error.size()) return left;
        while (m_cur.type == TOK_OR) {
            advance();
            auto right = parseAnd();
            if (!right) return nullptr;
            left = ASTNode::makeLog('|', std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<ASTNode> parseAnd()
    {
        auto left = parseNot();
        if (!left || m_error.size()) return left;
        while (m_cur.type == TOK_AND) {
            advance();
            auto right = parseNot();
            if (!right) return nullptr;
            left = ASTNode::makeLog('&', std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<ASTNode> parseNot()
    {
        if (m_cur.type == TOK_NOT) {
            advance();
            auto operand = parseNot();
            if (!operand) return nullptr;
            return ASTNode::makeNot(std::move(operand));
        }
        return parseComparison();
    }

    std::unique_ptr<ASTNode> parseComparison()
    {
        // ---- 括号子表达式 ----
        if (m_cur.type == TOK_LPAREN) {
            advance();
            auto expr = parseOr();
            if (!expr) return nullptr;
            if (m_cur.type != TOK_RPAREN) {
                m_error = "缺少右括号 ')'"; return nullptr;
            }
            advance();
            return expr;  // 已在 or 层处理，无需再包装
        }

        // ---- data contains <hex bytes> ----
        if (m_cur.type == TOK_IDENT && m_cur.strVal.toLower() == "data") {
            auto next = peek();
            if (next.type == TOK_IDENT &&
                next.strVal.toLower() == "contains") {
                advance(); // skip 'data'
                advance(); // skip 'contains'
                QByteArray pattern;
                while (m_cur.type == TOK_NUMBER) {
                    pattern.append(static_cast<char>(
                        static_cast<int>(m_cur.numVal) & 0xFF));
                    advance();
                }
                if (pattern.isEmpty()) {
                    m_error = "'data contains' 后需要至少一个字节值";
                    return nullptr;
                }
                return ASTNode::makeDataContains(pattern);
            }
            // 不是 "data contains"，作为普通变量处理
        }

        // ---- 标识符（变量 / id in / 比较左侧） ----
        if (m_cur.type == TOK_IDENT) {
            QString name = m_cur.strVal;
            int vidx = varIndex(name);
            if (vidx < 0) {
                // 预处理器提取的 data contains 模式 → 生成 DataContains 节点。
                // （曾为 makeVar(9) 占位，求值落到 error 变量：普通帧恒 false、
                // 错误帧误匹配 — B2 套件 dataContains 用例攻破，2026-08-20 修复）
                if (name.startsWith("__data_") && name.endsWith("__")) {
                    const int idx = QStringView(name)
                                        .mid(7, name.size() - 9)
                                        .toInt();
                    if (m_patterns && idx >= 0
                        && idx < static_cast<int>(m_numData)) {
                        advance();
                        return ASTNode::makeDataContains(
                            (*m_patterns)[static_cast<size_t>(idx)]);
                    }
                    m_error = QString("内部错误: 无效 data 模式 '%1'").arg(name);
                    return nullptr;
                }
                m_error = QString("未知变量: '%1'").arg(name);
                return nullptr;
            }
            advance();

            // id in v1, v2, ...
            if (vidx == 0 && m_cur.type == TOK_IDENT &&
                m_cur.strVal.toLower() == "in") {
                advance(); // skip 'in'
                std::vector<double> vals;
                while (true) {
                    if (m_cur.type != TOK_NUMBER) {
                        m_error = "'in' 后需要数值"; return nullptr;
                    }
                    vals.push_back(m_cur.numVal);
                    advance();
                    if (m_cur.type != TOK_COMMA) break;
                    advance();
                }
                return ASTNode::makeIdIn(vals);
            }

            // 比较运算: var op value
            if (isCompOp()) {
                int op = m_cur.type;
                advance();
                std::unique_ptr<ASTNode> rhs;
                if (m_cur.type == TOK_IDENT) {
                    int rvidx = varIndex(m_cur.strVal);
                    if (rvidx < 0) {
                        m_error = QString("未知变量: '%1'").arg(m_cur.strVal);
                        return nullptr;
                    }
                    advance();
                    rhs = ASTNode::makeVar(rvidx);
                } else if (m_cur.type == TOK_NUMBER) {
                    rhs = ASTNode::makeNum(m_cur.numVal);
                    advance();
                } else {
                    m_error = "比较运算符后需要变量或数值";
                    return nullptr;
                }
                return ASTNode::makeCmp(op, ASTNode::makeVar(vidx),
                                        std::move(rhs));
            }

            // 单独变量 → truthy 求值 (var != 0)
            return ASTNode::makeCmp(TOK_NE, ASTNode::makeVar(vidx),
                                    ASTNode::makeNum(0));
        }

        // ---- 裸数字 → id == N ----
        if (m_cur.type == TOK_NUMBER) {
            double val = m_cur.numVal;
            advance();
            return ASTNode::makeCmp(TOK_EQ, ASTNode::makeVar(0),
                                    ASTNode::makeNum(val));
        }

        // ---- NOT 结果（!expr 作为 truthy 求值） ----
        // 此处不应到达（parseNot 已处理），但保险起见
        m_error = QString("意外的标记: '%1'").arg(tokenStr());
        return nullptr;
    }
};

// ============================================================
//  AST 求值器
// ============================================================

struct EvalContext {
    double vars[10] = {};  // 0:id 1:dlc 2:ch 3:time 4:fd 5:ext 6:rx 7:tx 8:std 9:error
    const QByteArray *data = nullptr;
    const std::array<QByteArray, kMaxDataMatches> *dataPatterns = nullptr;
    size_t numData = 0;
};

double evalAST(const ASTNode *node, const EvalContext &ctx)
{
    if (!node) return 0;

    switch (node->kind) {
    case NodeKind::Number:
        return node->number;

    case NodeKind::Variable:
        if (node->varIdx >= 0 && node->varIdx < 10)
            return ctx.vars[node->varIdx];
        return 0;

    case NodeKind::Compare: {
        double l = evalAST(node->left.get(), ctx);
        double r = evalAST(node->right.get(), ctx);
        switch (node->op) {
        case TOK_EQ: return (l == r) ? 1.0 : 0.0;
        case TOK_NE: return (l != r) ? 1.0 : 0.0;
        case TOK_GT: return (l >  r) ? 1.0 : 0.0;
        case TOK_LT: return (l <  r) ? 1.0 : 0.0;
        case TOK_GE: return (l >= r) ? 1.0 : 0.0;
        case TOK_LE: return (l <= r) ? 1.0 : 0.0;
        default: return 0;
        }
    }

    case NodeKind::Logical: {
        double l = evalAST(node->left.get(), ctx);
        if (node->op == '&')
            return (l != 0.0) ?
                   ((evalAST(node->right.get(), ctx) != 0.0) ? 1.0 : 0.0)
                   : 0.0;  // 短路
        else
            return (l != 0.0) ? 1.0 :
                   ((evalAST(node->right.get(), ctx) != 0.0) ? 1.0 : 0.0);
    }

    case NodeKind::Not:
        return (evalAST(node->left.get(), ctx) == 0.0) ? 1.0 : 0.0;

    case NodeKind::DataContains: {
        if (!ctx.data) return 0;
        return ctx.data->contains(node->dataPattern) ? 1.0 : 0.0;
    }

    case NodeKind::IdIn: {
        double idVal = ctx.vars[0]; // id
        auto *cur = node->left.get();
        while (cur) {
            if (idVal == cur->number) return 1.0;
            cur = cur->right.get();
        }
        return 0.0;
    }
    }
    return 0;
}

} // anonymous namespace

// ============================================================
//  FilterEngine::Impl
// ============================================================

struct FilterEngine::Impl
{
    std::unique_ptr<ASTNode> ast;
    std::array<QByteArray, kMaxDataMatches> dataPatterns;
    size_t numData = 0;
    bool compiled = false;
    // 未设置表达式 = 无过滤（与 evaluate 对未编译透传一致；
    // 曾默认 false，新构造实例 isEmpty() 误报非空 — B2 emptyExpr 用例攻破）
    bool empty = true;
    QString errorMsg;
};

// ============================================================
//  FilterEngine 公共接口
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
    m_impl->ast.reset();

    if (expr.trimmed().isEmpty()) {
        m_impl->empty = true;
        m_impl->compiled = true;
        return true;
    }
    m_impl->empty = false;

    // 预处理（语法糖转换）
    Preprocessor pp;
    QString processed = pp.run(expr);
    m_impl->numData = pp.numData();
    for (size_t i = 0; i < pp.numData(); ++i)
        m_impl->dataPatterns[i] = pp.dataPattern(i);

    // 解析为 AST（dataPatterns 已拷入 m_impl，Parser 据此生成 DataContains 节点）
    Parser parser(processed, pp.numData(), &m_impl->dataPatterns);
    m_impl->ast = parser.parse();

    if (!m_impl->ast) {
        m_impl->errorMsg = parser.error();
        OPENBUS_LOG_WARN("FilterEngine", "compile failed: '{}' -> '{}', error: {}",
                     expr.toStdString(), processed.toStdString(),
                     parser.error().toStdString());
        return false;
    }

    m_impl->compiled = true;
    OPENBUS_LOG_DEBUG("FilterEngine", "compiled: {}", processed.toStdString());
    return true;
}

bool FilterEngine::evaluate(const CanFrame &frame) const
{
    if (m_impl->empty)     return true;
    if (!m_impl->compiled) return true;

    EvalContext ctx;
    ctx.vars[0] = static_cast<double>(frame.id & 0x1FFFFFFF);  // id
    ctx.vars[1] = static_cast<double>(frame.dlc);               // dlc
    ctx.vars[2] = static_cast<double>(frame.channel);            // ch
    ctx.vars[3] = frame.timestamp;                               // time
    ctx.vars[4] = frame.fd ? 1.0 : 0.0;                         // fd
    ctx.vars[5] = frame.extended ? 1.0 : 0.0;                   // ext
    ctx.vars[6] = (frame.direction == CanFrame::Rx) ? 1.0 : 0.0; // rx
    ctx.vars[7] = (frame.direction == CanFrame::Tx) ? 1.0 : 0.0; // tx
    ctx.vars[8] = frame.extended ? 0.0 : 1.0;                    // std
    ctx.vars[9] = frame.isErrorFrame() ? 1.0 : 0.0;              // error
    ctx.data = &frame.data;
    ctx.dataPatterns = &m_impl->dataPatterns;
    ctx.numData = m_impl->numData;

    return evalAST(m_impl->ast.get(), ctx) != 0.0;
}

bool FilterEngine::isValid() const  { return m_impl->compiled; }
bool FilterEngine::isEmpty() const  { return m_impl->empty; }

QString FilterEngine::errorString() const
{
    return m_impl->errorMsg;
}
