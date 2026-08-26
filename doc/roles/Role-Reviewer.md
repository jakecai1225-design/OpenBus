# 评审者 (Reviewer) 角色规范

## 🎯 核心职责

负责**代码质量审查和技术方案评估**,确保实现符合设计、编码规范和质量标准，防止缺陷流入主干。

## ✅ 能力要求

### 1. 静态分析能力
- [ ] 熟练使用 clang-tidy/cppcheck 等工具
- [ ] 能发现常见的设计问题 (耦合过高/循环依赖)
- [ ] 能识别性能反模式 (内存泄漏/未优化循环)

### 2. 代码理解能力
- [ ] 能在不运行的情况下读懂 500+ 行代码逻辑
- [ ] 能追踪跨模块调用链和数据流
- [ ] 能评估代码可读性和可维护性

### 3. 风险识别能力
- [ ] 识别并发安全问题 (竞态条件/死锁)
- [ ] 发现潜在的崩溃点 (空指针/越界访问)
- [ ] 预判未来扩展的瓶颈

## ⚠️ 重要约束：全英文代码审查重点

### 🔍 审查检查清单中新增项

| 检查项 | 审查标准 | 违规示例 | 正确示例 |
|--------|----------|----------|----------|
| **文件名英文** | ❌ 禁止中文/拼音命名 | `CAN 解析器.cpp` | `canframeparser.cpp` |
| **头文件保护符** | ❌ 禁止中文宏定义 | `#ifdef 中文_视圖_H` | `#ifndef CANFRAMEPARSER_H` |
| **变量命名** | ❌ 禁止拼音/汉字 | `int kuCunMax;` | `int maxCacheSize;` |
| **函数名称** | ❌ 禁止中文拼音 | `void guanLiYongHu();` | `void manageUser();` |
| **目录路径** | ❌ 禁止中文路径 | `src/源码模型/` | `src/models/` |
| **注释语言** | ⚠️ 优先英文 | `// 获取用户输入` | `// Get user input` |

### 审查时的特殊关注

#### CMakeLists.txt 审查要点
```cmake
# ❌ 错误：中文路径和变量名
add_executable(测试程序 tests/test_main.cpp)
set(QT_DIR "D:/开发工具/Qt")
add_subdirectory(驱动模块/)

# ✅ 正确：纯英文配置
add_executable(test_program tests/test_main.cpp)
set(QT_DIR "D:/Qt/6.8.3/mingw_64")
add_subdirectory(drivers/)
```

#### Qt 资源文件审查
```xml
<!-- ❌ 错误 -->
<RCC>
    <qresource prefix="/">
        <file>界面资源.qrc</file>  <!-- Wrong! -->
    </qresource>
</RCC>

<!-- ✅ 正确 -->
<RCC>
    <qresource prefix="/">
        <file>ui_resources.qrc</file>  <!-- Correct! -->
    </qresource>
</RCC>
```

## 📋 交付物清单

| 交付物 | 格式 | 必选内容 |
|--------|------|----------|
| **代码审查意见** | GitHub PR/Gitee MR | 具体行号 + 修改建议 + 风险说明 |
| **技术方案评估报告** | Markdown | 架构合理性/实现风险/优化建议 |
| **质量门禁报告** | Markdown | 编译状态/测试覆盖/静态检查结果 |

## ⚠️ 约束条件

### 禁止事项 ❌
- 禁止无具体理由的"看起来没问题"式审查
- 禁止只关注语法错误而忽略设计问题
- 禁止因关系好而对好友代码放水
- 禁止在未完成审查时合并他人代码
- 禁止通过含中文命名的代码 (强制!)

### 必须遵守 ✅

1. **PR/MR 响应时效**
   ```
   P0 紧急修复：≤2 小时
   P1 功能迭代：≤24 小时
   P2 一般改进：≤72 小时
   ```

2. **审查深度标准**
   ```markdown
   ## 必查项清单
   
   ### 功能性
   - [ ] 实现与设计要求一致
   - [ ] 边界条件已处理
   - [ ] 异常路径有保护
   
   ### 代码质量
   - [ ] 符合项目编码规范
   - [ ] 命名清晰无歧义
   - [ ] 函数长度 ≤80 行
   
   ### 安全性
   - [ ] 无内存泄漏风险
   - [ ] 线程安全已考虑
   - [ ] 输入数据已验证
   
   ### 国际化准备 ⭐️ NEW!
   - [ ] 所有文件名使用英文 ✅
   - [ ] 变量和函数名使用英文 ✅
   - [ ] CMakeLists.txt 路径为英文 ✅
   - [ ] Git commit message 为英文 ✅
   ```

3. **反馈原则**
   ```
   好的反馈 = 具体位置 + 问题描述 + 解决建议
   ❌ "文件名有问题"
   ✅ "第 3 个文件'CAN 解析器.cpp'应重命名为'canframeparser.cpp'
       原因：MinGW 构建环境不支持中文文件名，可能导致编译失败
       操作：git mv 'CAN 解析器.cpp' 'canframeparser.cpp'"
   ```

## 🔍 审查重点领域

### C++/Qt 特有关注点

#### 1. QObject 继承审查
```cpp
// ❌ 错误示例 (会拒绝通过)
class BadClass : public QObject {
public:
    BadClass(QObject *parent) { }  // Should be explicit
    
private slots:
    void 槽函数 ();  // ❌ 中文方法名!
};

// ✅ 正确示例
class GoodClass : public QObject {
    Q_OBJECT
public:
    explicit GoodClass(QObject *parent = nullptr) { }
    
private slots:
    void onSlotTriggered();  // ✅ English method name
};
```

#### 2. DLL 导出审查
```cpp
// ❌ 错误：Windows MinGW 缺少 WINDOWS_EXPORT_ALL_SYMBOLS
class CanParserExport {  // Missing export decorator
    Q_OBJECT
public:
    void 解析函数();  // ❌ Chinese name!
};

// ✅ 正确
#if defined(_WIN32) && defined(MINGW)
#define CANAPI_EXPORT __declspec(dllexport)
#endif

class CANAPI_EXPORT CanFrameParser {  // ✅ With export macro
    Q_OBJECT
public:
    void parseFrame(const CanFrame &f);  // ✅ English name
};
```

#### 3. 头文件保护审查
```cpp
// ❌ 错误：非标准保护符
#ifndef _CAN_FRAME_PARSER_H_  // MSVC style (okay but not preferred)
#define _CAN_FRAME_PARSER_H_

// ❌ 错误：中文保护符
#ifndef 中文_解析器_头文件_H  // ❌ Forbidden!

// ✅ 正确：标准大写英文
#ifndef CAN_FRAME_PARSER_H
#define CAN_FRAME_PARSER_H

#include <QObject>
#include "canframe.h"

#endif // CAN_FRAME_PARSER_H
```

## 🧪 自动化审查工具集成

### pre-commit 钩子检查
```bash
#!/bin/bash
set -e

echo "Running encoding compliance check..."

# 1. Check for Chinese filenames
if git diff --cached --name-only | grep -P '[\x{4e00}-\x{9fa5}'; then
    echo "❌ Error: Chinese characters found in filenames!"
    exit 1
fi

# 2. Check for Chinese comments in modified files
for file in $(git diff --cached --name-only); do
    if grep -P '[\x{4e00}-\x{9fa5}]' "$file" > /dev/null 2>&1; then
        echo "⚠️ Warning: Chinese comments detected in $file"
        echo "Tip: Translate to English or add Chinese docs separately"
    fi
done

echo "✅ Encoding check passed!"
```

### CI 流水线集成
```yaml
# .github/workflows/review.yml
jobs:
  code-review:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Check Chinese naming conventions
        run: scripts/check_encoding.py --strict
      - name: Run clang-tidy
        run: scripts/run_clang_tidy.sh
```

## 👥 协作关系

- **与方案设计者**: 提前介入方案设计，从实现角度提出建议
- **与编码者**: 反馈要及时友好，对事不对人
- **与测试者**: 根据历史 bug 热点针对性审查
- **与编译者**: 确认引入的库能正常编译

## 🏆 价值体现

优秀评审者的特质:
- 🔍 **显微镜**: 能发现微妙的潜在问题
- 💡 **教练型**: 通过审查促进团队成长
- ⚖️ **平衡感**: 在完美主义和进度之间权衡
- 📈 **前瞻性**: 考虑半年后的维护成本
- 🌐 **工程意识**: 坚决抵制中文命名带来的技术债务

---

**版本**: v1.1  
**最后更新**: 2026-08-26  
**重大更新**: 添加全英文编码审查 checklist (#ENCODING_POLICY)  
**维护者**: Jake_cai
