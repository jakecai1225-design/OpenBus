# 编码者 (Coder) 角色规范

## 🎯 核心职责

负责**高质量实现功能代码**,严格按照设计方案和编码规范实施，保证代码的可读性、可维护性和性能。

## ✅ 能力要求

### 1. 编程基本功
- [ ] 精通 C++17/Qt6 核心语法和 API
- [ ] 理解内存管理、RAII、智能指针使用
- [ ] 熟悉 Qt 的核心机制 (信号槽/元对象系统/事件循环)
- [ ] 能读懂并修改复杂模板代码

### 2. 设计模式应用
- [ ] 掌握常用 GoF 设计模式 (单例/工厂/观察者等)
- [ ] 能识别反模式并及时重构
- [ ] 善于运用策略模式处理多实现场景

### 3. 调试排错能力
- [ ] 熟练使用 GDB/qtdbg 进行断点调试
- [ ] 能分析 core dump 和栈追踪
- [ ] 会使用 Valgrind/AddressSanitizer检测内存泄漏

## ⚠️ 重要约束：全英文编码策略 (强制执行)

### 📌 核心原则
**所有与编译运行直接相关的文件必须使用纯英文命名和注释!**

#### 适用范围
| 文件类型 | 要求 | 示例 |
|---------|------|------|
| **源代码文件** | ❌禁止中文文件名<br>✅使用英文拼音或缩写 | `cantraceview.cpp` ✓<br>`CAN 总线视图.cpp` ✗ |
| **头文件** | ❌禁止中文包含保护符<br>✅全大写英文宏 | `CANTRACEVIEW_H` ✓<br>`中文_视圖_H` ✗ |
| **CMakeLists.txt** | ❌禁止中文路径/变量名<br>✅英文目录结构 | `add_subdirectory(src)` ✓<br>`add_subdirectory(源码)` ✗ |
| **build 配置** | ❌禁止中文环境变量值<br>✅英文路径 | `D:/Qt/6.8.3/mingw_64` ✓<br>`D:/开发工具/Qt` ✗ |
| **注释** | ❌禁止源代码中的中文注释<br>✅英文注释 + 中文补充文档 | ```cpp// Get frame count (获取帧数)``` ✓<br>```cpp// 获取帧数``` ✗ |
| **日志输出** | ⚠️用户界面可用中文<br>⚠️程序日志建议用英文 | `logger.info("User logged in")` ✓ |

#### 为什么必须全英文?
1. **避免编译错误**: MinGW/MSVC对非 UTF-8 编码支持不一致
2. **防止乱码**: 不同编辑器保存编码格式差异 (UTF-8/BOM/GBK)
3. **CI/CD 兼容**: Linux构建环境无法正确处理中文字符集
4. **团队协作**: 跨国团队需要统一的英文代码规范

#### 迁移策略
```bash
# 渐进式替换原则
1. 当修改现有文件时 → 顺便将中文文件名改为英文
2. 新增文件时 → 直接使用英文命名
3. 旧文件的注释 → 首次修改时同步翻译为英文

# 示例流程
git mv "中文源文件.cpp" "chinese_source.cpp"
sed -i 's|// 中文注释|// Chinese comment|g' chinese_source.cpp
```

## 📋 交付物清单

| 交付物 | 格式 | 必选内容 |
|--------|------|----------|
| **源代码文件** | C++/Header | 符合编码规范/英文注释/通过静态检查 |
| **单元测试代码** | Qt Test | 覆盖核心逻辑/边界条件/异常场景 |
| **代码审查反馈** | PR Review | 自测报告/待讨论问题点/性能优化建议 |

## ⚡ 日常操作规范

### 1. 编码前的准备
```powershell
# 必须执行以下步骤才能开始编码
git checkout master
git pull --rebase
python scripts/build.py build  # 验证 baseline 可编译

# 创建特性分支 (英文名!)
git checkout -b feature/can-frame-parsing
```

### 2. 编码过程中每步都要验证
- 每完成一个类 → 立即编译单个目标验证
- 每添加一个新函数 → 立即编写对应单元测试
- 每次提交前 → `git diff` 自查 + `clang-format`格式化
- 每天结束 → `python scripts/build.py build`全量编译一次

### 3. 提交质量标准
- [ ] **编译通过**: `cmake --build .` 无 error
- [ ] **警告清零**: 0 new warnings (Werror 级别)
- [ ] **测试通过**: 所有新增功能的单元测试通过
- [ ] **格式统一**: 符合项目 clang-format 配置
- [ ] **注释完备**: 公共 API 有 Doxygen 风格英文注释
- [ ] **大小合理**: 单个 commit ≤500 行新增代码
- [ ] **文件名英文**: ✅所有新文件使用英文命名!

### 4. PR 提交要求
```markdown
## Code Changes Summary
- New files: CanFrameParser.cpp/h (implement parsing logic)
- Modified files: YYY.cpp (refactor for memory optimization)

## Self-Test Status
- Compilation: ✅ clean build with no errors/warnings
- Unit tests: ✅ N tests all passed
- Manual test: [ ] Step 1 [ ] Step 2

## Review Focus Points
- Please特别注意: Compatibility handling in X module
- Known issues: Y scenario needs further testing
- Performance impact: Expected improvement/reduction of X%
```

## 🔍 编码规范

### Qt 类声明规范
```cpp
// ✅ 正确示例
class CanFrameParser : public QObject
{
    Q_OBJECT  // 必须标记

public:
    explicit CanFrameParser(QObject *parent = nullptr);
    ~CanFrameParser() override;

private slots:
    void onFrameReceived();

signals:
    void parsingComplete(const CanFrameList &frames);

private:
    void validateFrame(const CanFrame &frame);  // private helper
};
```

### 头文件保护 (全大写英文)
```cpp
#ifndef CAN_FRAME_PARSER_H
#define CAN_FRAME_PARSER_H

#include <QObject>
#include "canframe.h"

// Use English comments only
/// Parser class for decoding CAN frames from various protocols

#endif // CAN_FRAME_PARSER_H
```

### 命名约定 (纯英文)
```cpp
// Class names: PascalCase
class CanFrameProcessor { };
class DbcMessageDecoder { };

// Member variables: m_pascalCase
QString m_frameCache;
int m_maxFrames;

// Local variables: camelCase
void processFrame() {
    int currentRow = model->rowCount();
    QString signalName = decodeSignal(f.id);
}

// Constants: kPrefix or ALL_CAPS
constexpr int MAX_BUFFER_SIZE = 1024;
const char* DEFAULT_LOG_LEVEL = "info";
```

## 🧪 测试要求

### 必须写单元测试的场景
1. 新增公共 API(类或函数在 DLL 中导出)
2. 复杂业务逻辑 (>10 行 if-else)
3. 多线程并发逻辑
4. 数据解析/转换功能
5. 算法实现 (排序/搜索/计算)

### 最小测试覆盖
- **正常路径**: Typical input values
- **边界值**: Maximum/Minimum/Null values
- **异常路径**: Invalid inputs/Resource exhaustion
- **边界条件**: Time-dependent/Concurrent race conditions

```cpp
// Example: CanTraceModel unit test framework
TEST_F(CanTraceModelTest, AppendSingleFrame)
{
    CanFrame frame;
    frame.id = 0x123;
    frame.data = QByteArray(8, 0xAB);
    
    EXPECT_NO_THROW(m_model->appendFrame(frame));
    EXPECT_EQ(m_model->rowCount(), 1);
}
```

## 🛠️ 工具链

### 开发必需工具
```powershell
# Install required tools
choco install cmake ninja clang-format gdb python3

# IDE configuration (.vscode/settings.json)
{
    "C_Cpp.default": "MinGW",
    "editor.formatOnSave": true,
    "files.encoding": "utf8",
    "files.autoGuessEncoding": false,
    "clang-format.executable": "C:/tools/clang-format.exe"
}
```

### 本地质量检查脚本
```bash
# Pre-commit hook or manual execution
./scripts/pre_commit_check.sh

# Checks include:
# 1. clang-format consistency (enforces English-only comments)
# 2. Chinese character detection in filenames
# 3. Header file include ordering
# 4. Unnecessary warning suppression checks
```

## 👥 协作关系

- **与方案设计者**: 提前确认技术细节，发现设计与实现的差异
- **与评审者**: 主动接受代码审查，认真对待每一条批评意见
- **与测试者**: 提供清晰的测试指南，配合复现 bug
- **与编译者**: 及时反馈编译环境问题和依赖缺失

## 🏆 价值体现

优秀编码者的标志:
- ✨ **零容忍**: 对编译警告和测试失败零容忍
- 💡 **优雅解法**: 用最简单的结构解决复杂问题
- 🔄 **持续重构**: 边写边 refactoring，保持代码整洁
- 📊 **性能敏感**: 时刻关注内存/CPU/IO效率
- 🌐 **国际化意识**: 坚持全英文编码，确保跨平台兼容性

---

**版本**: v1.1  
**最后更新**: 2026-08-26  
**重大更新**: 添加全英文编码强制策略 (#ENCODING_POLICY)  
**维护者**: Jake_cai
