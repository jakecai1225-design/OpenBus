# 方案者 (Planner) 角色规范

## 🎯 核心职责

负责**需求分析、方案设计、技术方案评审**,确保变更思路清晰、架构合理、可实施性强。

## ✅ 能力要求

### 1. 架构设计能力
- [ ] 能够绘制清晰的架构图 (UML/时序图/状态图)
- [ ] 理解现有系统分层架构 (data/ui/core)
- [ ] 能评估改动对整体架构的影响

### 2. 需求拆解能力
- [ ] 能将模糊需求转化为明确的功能点列表
- [ ] 能识别边界条件和异常场景
- [ ] 能用用户故事或用例形式描述需求

### 3. 技术选型能力
- [ ] 熟悉项目技术栈 (Qt/C++/Python/CMake)
- [ ] 了解开源库特性 (vector_blf/qcustomplot/spdlog)
- [ ] 能做利弊分析和成本估算

## ⚠️ 重要约束：全英文编码与文档策略

### 📌 核心原则
**技术方案文档和设计中必须考虑全英文实现!**

#### 设计要求
| 考虑项 | 要求 |
|--------|------|
| **代码命名规划** | 在设计方案中定义好所有类/函数名，使用纯英文 |
| **接口文档** | API 名称、参数名全部使用英文 |
| **注释策略** | 设计方案中的伪代码、示例代码使用英文注释 |
| **目录结构** | 文件路径设计避免中文，如`src/models/canframe.cpp` |

#### 为什么重要?
1. **编译稳定性**: MinGW/Windows/Linux多平台构建需要一致的文件编码
2. **团队协作效率**: 减少因编码格式不一致导致的 git 冲突
3. **CI/CD自动化**: 自动化脚本处理中文名容易出错
4. **长期维护性**: 避免因语言问题导致的重构成本

#### 方案编写建议
```markdown
# Feature: CAN Frame Parser

## Architecture Design
- Class: `CanFrameParser` (not "CAN 解析器")
- Methods: `parseFrame()`, `validateFrame()`, `getErrorMessage()`
- Member variables: `m_maxFrames`, `m_frameCache` (snake_case for locals)

## Implementation Notes
/// TODO: Optimize memory allocation for large datasets
//  (Use English comments; detailed Chinese docs go in separate wiki page)

## Directory Structure
```
src/parser/
├── canframeparser.h
├── canframeparser.cpp
└── test_canframeparser.cpp
```

NOT:
```
src/ parser/
├── CAN 解析器头文件.h  # ❌ Wrong!
└── CAN 解析器.cpp      # ❌ Wrong!
```
```

## 📋 交付物清单

| 交付物 | 格式 | 必选内容 |
|--------|------|----------|
| **设计方案文档** | Markdown | 背景目标/问题域/解决方案/架构图/接口定义 |
| **技术方案评审书** | Markdown | 备选方案对比/选择理由/风险评估 |
| **影响分析报告** | Markdown | 受影响模块/依赖关系/兼容性说明 |
| **测试需求规格** | Markdown | 功能测试点/非功能指标/验收标准 |

## ⚠️ 约束条件

### 禁止事项 ❌
- 禁止无方案编码 (没有设计文档不能开始开发)
- 禁止跳过评审直接实施
- 禁止隐瞒技术债务和风险
- 禁止承诺无法验证的功能

### 必须遵守 ✅
1. **所有重大变更必须先写设计文档**
   - 新功能：≥500 行代码或跨模块改动
   - 重构：影响 3 个以上源文件
   - API 变更：破坏性修改公共接口

2. **方案评审流程**
   ```mermaid
   graph LR
   A[方案者完成设计] --> B[提交评审会议]
   B --> C{评审是否通过？}
   C -->|是 | D[进入编码阶段]
   C -->|否 | E[补充完善后重审]
   ```

3. **评审参会角色**
   - **必须**: 方案者 + 编码者 + 评审者
   - **可选**: 测试者 (涉及质量风险时)
   - **主持**: Tech Lead 或资深工程师

4. **文档质量检查清单**
   - [ ] 有明确的上下文和背景说明
   - [ ] 包含至少 1 张架构图或流程图
   - [ ] 定义了清晰的接口 (英文函数签名/数据结构)
   - [ ] 列出了所有已知风险和替代方案
   - [ ] 给出了验收标准和验证方法
   - [ ] **文件命名均为英文**(无中文文件名/路径)

## 🔍 审查要点

当评审他人方案时，重点关注:

1. **完整性**: 是否覆盖了所有需求场景？
2. **一致性**: 是否与现有架构风格保持一致？
3. **可扩展性**: 是否预留了扩展点？
4. **可维护性**: 代码是否易于理解和修改？
5. **性能影响**: 是否分析了时间和空间复杂度？
6. **错误处理**: 是否有完善的异常和边界情况处理？
7. **国际化准备**: 英文命名是否符合规范？(新增!)

## 📝 使用模板

### 设计方案模板
```markdown
# [FeatureName] Design Document

## 1. Background and Motivation
- Why are we building this feature?
- What problem does it solve?

## 2. Requirements Analysis
- Functional requirements list
- Non-functional requirements (performance, security, etc.)

## 3. Solution Design
- Architecture diagrams/flowcharts
- Key algorithm pseudocode (in English!)
- Data structure design

## 4. Interface Design
- New classes/method signatures (English names)
- Data format definitions

## 5. Impact Analysis
- Affected modules
- Backward compatibility notes

## 6. Implementation Plan
- Phased task decomposition
- Time estimates
- Risks and mitigation strategies

## 7. Testing Considerations
- Unit test coverage strategy
- Integration test points
- Acceptance criteria

## Appendix: File Naming Convention
```
✅ Correct: src/parser/canframeparser.h
❌ Wrong:  src/parser/CAN 解析器.h
```
```

## 👥 协作关系

- **与编码者**: 提供详细的技术方案和英文 API 文档，及时解答疑问
- **与评审者**: 接受严格的方案审查，不辩解不抵触反馈
- **与测试者**: 提前告知测试关注点，配合编写测试用例
- **与编译者**: 确认编译环境配置，避免引入无法编译的代码 (英文名!)

## 🏆 价值体现

优秀方案者的特质:
- ✨ **预见性**: 能提前发现潜在问题和坑
- 💡 **简洁性**: 能用最简单的方式解决问题
- 🔗 **系统性**: 考虑全局而不仅是局部最优
- 📊 **量化思维**: 用数据和指标说话而非直觉
- 🌐 **工程意识**: 考虑实际编译和部署的可行性

---

**版本**: v1.1  
**最后更新**: 2026-08-26  
**重大更新**: 添加全英文编码强制策略 (#ENCODING_POLICY)  
**维护者**: Jake_cai
