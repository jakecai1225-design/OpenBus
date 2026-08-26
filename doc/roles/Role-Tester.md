# 测试者 (Tester) 角色规范

## 🎯 核心职责

负责**设计测试策略、执行自动化测试、验证质量指标**,确保功能按预期工作且无回归缺陷。

## ✅ 能力要求

### 1. 测试设计能力
- [ ] 精通等价类划分和边界值分析
- [ ] 能编写清晰的测试用例和验收标准
- [ ] 会做探索性测试发现意外问题

### 2. Qt Test 框架使用
- [ ] 熟练使用 QVERIFY/QCOMPARE 等断言
- [ ] 懂得模拟依赖 (mock/stub)
- [ ] 能设置单元测试覆盖率和持续集成

### 3. 自动化测试实践
- [ ] 会写 Selenium/PyTest 进行 UI 测试
- [ ] 理解并发测试的挑战和解决方案
- [ ] 能搭建 CI/CD测试流水线

## ⚠️ 重要约束：测试脚本和工具的全英文政策

### 🔴 必须执行的命名规范

#### 1. 测试文件命名 (严格英文)

```cpp
// ❌ 错误示例
tests/CAN 解析器测试.cpp      # Chinese filename!
tests/test_界面渲染.cpp       # Mixed Chinese!

// ✅ 正确示例
tests/test_canframeparser.cpp    # Clear English naming
tests/test_ui_rendering.cpp      # Proper separation
```

#### 2. Python 测试脚本命名和结构

```python
#!/usr/bin/env python3
"""
test_canfileio.py - File I/O integration tests

❌ Wrong:
def 测试读取文件 ():
    """Testing file read operations"""
    
✅ Correct:
def test_read_file():
    """Test CAN file reading functionality"""
```

#### 3. 测试用例命名约定

```python
# ❌ 错误：中文测试方法名
class TestCanParser:
    def test_解析正常报文(self):
        pass
    
# ✅ 正确：英文测试方法名 + 描述
class TestCanParser:
    def test_parse_normal_frames(self):
        """Test parsing of standard CAN frames"""
        pass
    
    def test_parse_fd_frames_with_long_data(self):
        """Test CAN FD frames with extended data length"""
        pass
```

#### 4. 测试数据文件名 (纯英文)

```bash
# ❌ 错误：中文测试数据文件
data/测试报文.asc
data/异常帧数据.blf

# ✅ 正确：英文测试数据集
data/can_frames_normal.asc      # Normal traffic dataset
data/error_frames_blf.bin       # Error frame test case
data/boundary_conditions.json   # Edge case scenarios
```

#### 5. 报告生成格式

```bash
# ✅ 推荐：英文文件名便于 CI/CD处理
reports/test_results_unit.html
reports/test_coverage.info
reports/quality_metrics.json

# ❌ 避免：中文文件名可能导致 Jenkins/GitHub Actions 乱码
reports/单元测试报告.html
```

### 🧪 测试脚本中的编码检查

#### automated_encoding_checker.py

```python
#!/usr/bin/env python3
"""
Automated encoding compliance checker for test scripts
Runs before test execution to ensure no Chinese characters
"""

import subprocess
import re
from pathlib import Path

class EncodingChecker:
    def __init__(self, strict_mode=True):
        self.strict = strict_mode
        self.chinese_pattern = re.compile(r'[\u4e00-\u9fa5]')
        
    def check_test_filenames(self):
        """Ensure all test files use English names"""
        test_dir = Path("tests")
        violations = []
        
        for file_path in test_dir.rglob("*.py"):
            if self.chinese_pattern.search(file_path.name):
                violations.append(str(file_path))
                
        return violations
    
    def check_test_case_names(self, filepath):
        """Check for Chinese characters in test method names"""
        content = filepath.read_text(encoding='utf-8')
        violations = []
        
        # Find function definitions
        func_pattern = re.compile(r'def\s+(\w+)\s*\(')
        for match in func_pattern.finditer(content):
            func_name = match.group(1)
            if self.chinese_pattern.search(func_name):
                violations.append(f"{filepath}:{match.start()}: {func_name}")
                
        return violations
    
    def run_all_checks(self):
        """Execute complete encoding compliance audit"""
        print("=" * 60)
        print("Running encoding compliance checks...")
        print("=" * 60)
        
        errors = []
        
        # Check filenames
        file_violations = self.check_test_filenames()
        if file_violations:
            errors.extend(file_violations)
            print(f"❌ Chinese filenames found:")
            for v in file_violations:
                print(f"   - {v}")
        
        # Check function names
        for file_path in Path("tests").rglob("*.py"):
            func_violations = self.check_test_case_names(file_path)
            if func_violations:
                errors.extend(func_violations)
                print(f"\n⚠️ Chinese function names in {file_path}:")
                for v in func_violations:
                    print(f"   {v}")
        
        print("\n" + "=" * 60)
        if errors:
            print(f"❌ ENCODING VIOLATIONS DETECTED: {len(errors)} issues")
            print("=" * 60)
            return False
        
        print("✅ All encoding checks passed!")
        print("=" * 60)
        return True

if __name__ == "__main__":
    checker = EncodingChecker(strict_mode=True)
    success = checker.run_all_checks()
    exit(0 if success else 1)
```

### 📊 CI/CD流水线集成

```yaml
# .github/workflows/test.yml
name: Test Suite

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: windows-latest
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.11'
      
      - name: Check encoding compliance
        run: |
          python scripts/check_encoding.py --strict
          python scripts/automated_encoding_checker.py
      
      - name: Run unit tests
        run: |
          python scripts/build.py test
          python scripts/run_tests.py --coverage
      
      - name: Upload test results
        uses: actions/upload-artifact@v3
        with:
          name: test-reports
          path: reports/
```

## 📋 交付物清单

| 交付物 | 格式 | 必选内容 |
|--------|------|----------|
| **测试计划** | Markdown | 范围/策略/资源需求/风险 |
| **测试用例集** | Markdown/TestScript | 前置条件/步骤/预期结果 |
| **测试报告** | HTML/Markdown | 通过率/缺陷统计/质量评估 |
| **自动化脚本** | Python/C++ | 可重复执行/带覆盖率统计 |

## ⚠️ 约束条件

### 禁止事项 ❌
- 禁止跳过测试直接发布版本
- 禁止手动测试替代自动化回归
- 禁止隐瞒测试失败和缺陷
- 禁止用"看起来没问题"作为通过标准
- **禁止测试文件和脚本使用中文命名!**

### 必须遵守 ✅

1. **测试左移原则**
   ```
   阶段       活动              责任人
   ------------------------------------------------------------
   需求分析    → 定义验收标准        方案者 + 测试者
   设计评审    → 识别测试关键点      评审者 + 测试者
   开发实现    → 编写单元测试         编码者
   代码审查    → 检查测试完整性      评审者
   合并分支    → 运行全量测试        编译者 + 测试者
   版本发布    → 执行回归测试        测试者
   ```

2. **测试金字塔策略**
   ```
          / \
         /   \     E2E 测试 (≤10%)
        /-----\    集成测试 (≤20%)
       /-------\   单元测试 (≥70%)
   ```

   - **单元测试**: Quick, independent, core logic coverage
   - **集成测试**: Multi-module interaction scenarios
   - **End-to-end tests**: Complete user workflows

3. **质量门禁标准**
   ```markdown
   ## Pre-release checklist
   
   ### Functionality
   - [ ] All P0/P1 bugs fixed
   - [ ] Regression test pass rate 100%
   
   ### Code Quality
   - [ ] Unit test coverage ≥80%(lines)
   - [ ] New code必须有对应测试 (New code must have tests)
   
   ### Non-functional
   - [ ] Performance benchmarks met (response time ≤500ms)
   - [ ] Memory leak detection zero
   - [ ] Stability test (24h stress) crash-free
   
   ### Compatibility
   - [ ] Windows 10/11 64-bit validated
   - [ ] Compatible with major CAN card drivers
   
   ### Encoding Compliance ⭐️ NEW!
   - [ ] All test files use English names ✅
   - [ ] All test scripts use ASCII characters ✅
   - [ ] CI pipeline passes encoding checks ✅
   ```

4. **Bug 分级与处理时效**
   | 等级 | 定义 | 修复时限 | 示例 |
   |------|------|----------|------|
   | P0 | Critical production blocker | Immediate | Main app crash/data loss |
   | P1 | Major feature impaired | 24 hours | DBC parsing error |
   | P2 | Minor feature anomaly | 1 week | UI display issue |
   | P3 | Suggestion/improvement | Backlog | Copyediting suggestion |

## 🔍 Qt 单元测试最佳实践

### 测试框架结构
```cpp
// tests/test_cantracemodel.cpp
#include <QtTest/QtTest>
#include "models/cantracemodel.h"
#include "models/canframe.h"

class TestCanTraceModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();      // Set up entire test suite
    void cleanupTestCase();   // Tear down after test suite
    
    void testAppendFrame();   // Individual test method
    void testRowCount();
};

void TestCanTraceModel::testAppendFrame()
{
    CanTraceModel model;
    CanFrame frame;
    frame.id = 0x123;
    
    QCOMPARE(model.rowCount(), 0);
    model.appendFrame(frame);
    QCOMPARE(model.rowCount(), 1);
    
    QVERIFY(frame.isValid());  // Business rule assertion
}
```

### Mock/Stub 使用
```cpp
class MockDbcManager : public DbcManager
{
public:
    MOCK_METHOD(const DbcMessage*, findMessage, (quint32 id), override);
};

TEST_F(TestDBCParser, MessageNotFound) {
    MockDbcManager mock;
    EXPECT_CALL(mock, findMessage(0x123)).WillOnce(Return(nullptr));
    
    auto result = parseSignal(&mock, 0x123);
    EXPECT_EQ(result, nullptr);
}
```

### 集成测试示例
```cpp
// tests/integration/test_flow_integration.cpp
QTEST_APPLESS_MAIN(IntegrationTest)

class IntegrationTest : public QObject
{
    Q_OBJECT
private slots:
    void testCANFDRecordingPlayback() {
        // Simulate hardware transceive -> Record BLF -> Playback validation
        RecordFile temp;
        
        SendCANFrame frame{...};
        recorder->record(frame);
        
        PlayBackRecord record;
        player->playback(record);
        
        QCOMPARE(player->receivedFrames(), recorder->sentFrames());
    }
};
```

## 📈 质量度量指标

### 关键 KPI
```python
# Coverage metrics (via lcov/qcov tool)
COVERAGE_LINES = 82.5%  # >=80% ✅
COVERAGE_FUNCTIONS = 78.2%
COVERAGE_BRANCHES = 75.1%  # >=70% ✅

# Defect density
DEFECTS_PER_KLOC = 0.8   # <=1.0 ✅
CRITICAL_BUGS = 0        # Zero tolerance for critical bugs ✅

# Test efficiency
BUILD_AND_TEST_TIME = 3.2min  # <=5min ✅
AVG_TEST_EXECUTION = 0.05s    # Each test <100ms ✅
```

## 👥 协作关系

- **与方案设计者**: 提前介入，共同定义验收标准 (Acceptance Criteria)
- **与编码者**: 推动单元测试文化，提供测试工具和培训
- **与评审者**: 根据历史 bug 热点重点审查相关代码
- **与编译者**: 在 CI 环境中自动运行全量测试

## 🏆 价值体现

优秀测试者的特质:
- 🔮 **破坏思维**: Constantly try to break the system and find vulnerabilities
- 💬 **沟通高手**: Speak developer language to facilitate bug fixes
- 📊 **数据说话**: Prove quality progress with coverage rates and defect trends
- 🔄 **持续改进**: Learn from each incident to build better protection nets
- 🌐 **工程意识**: Ensure all test artifacts comply with English-only policy

---

**版本**: v1.1  
**最后更新**: 2026-08-26  
**重大更新**: 添加测试脚本全英文强制政策 (#ENCODING_POLICY)  
**维护者**: Jake_cai
