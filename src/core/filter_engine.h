#ifndef FILTER_ENGINE_H
#define FILTER_ENGINE_H

#include <QString>
#include <memory>

struct CanFrame;

/**
 * @brief 轻量级 CAN 帧过滤引擎
 *
 * 使用自写递归下降解析器（零外部依赖），通过 pimpl 模式封装。
 *
 * 支持的表达式语法：
 *   - 变量: id, dlc, ch, time, fd, ext, rx, tx, std
 *   - 逻辑: and/&&, or/||, not/!
 *   - 比较: ==, !=, >, <, >=, <=
 *   - 裸十六进制: 0x123 自动解释为 id==0x123
 *   - id in 0x100,0x200 → (id==0x100 || id==0x200)
 *   - data contains 01 02 → 数据包含字节序列（单独求值）
 *
 * 示例:
 *   id == 0x123 and fd
 *   dlc > 8 and ext
 *   0x100 or 0x200
 *   not (id == 0x100 or id == 0x200)
 *   data contains 01 02 and ch == 1
 */
class FilterEngine
{
public:
    FilterEngine();
    ~FilterEngine();

    FilterEngine(const FilterEngine &) = delete;
    FilterEngine &operator=(const FilterEngine &) = delete;

    /// 编译过滤表达式，返回是否合法
    bool compile(const QString &expr);

    /// 对单帧求值（编译失败或空表达式时返回 true）
    bool evaluate(const CanFrame &frame) const;

    /// 表达式是否编译成功
    bool isValid() const;

    /// 表达式是否为空（无过滤）
    bool isEmpty() const;

    /// 返回编译错误信息
    QString errorString() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // FILTER_ENGINE_H
