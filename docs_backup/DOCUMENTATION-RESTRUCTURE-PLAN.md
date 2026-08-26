# 📚 OpenBUS 文档结构规范 v2.0

**目标**: 统一混乱的文档体系，建立清晰的分层架构  
**执行者**: Qoder AI Agent  
**日期**: 2026-08-25  

---

## 🎯 问题诊断

### 当前混乱点

1. **文件名不统一**: 
   - `issues.md` (英文) vs `测试报告.md` (中文)
   - `G14-Issue-Status.md` vs `G15-Graphic-优化.md` (编号混乱)

2. **内容重复**:
   - `测试验收方案.md` vs `测试报告.md` (概念混淆)
   - `拆分应用实施方案.md` + `拆分应用方案.md` (同一主题)

3. **缺乏层级**:
   - 所有文件平铺在根目录
   - 无分类、无优先级标识

4. **命名不规范**:
   - 中英文混用
   - 标点符号不统一（`-` / `_` / `-`)

---

## 📋 新的文档架构设计

```
doc/
├── 00-README.md                    # 文档导航总入口
│
├── 01-ARCHITECTURE/                # 架构设计类
│   ├── architecture.drawio         # [保持] 原始架构图
│   ├── drawio.svg                  # [保持] 可视化版
│   ├── flow.md                     # Flow 模块说明
│   └── README.md                   # 架构文档索引
│
├── 02-FEATURES/                   # 功能设计方案
│   ├── Graphic 核心设计.md          # [新名称] Graphic 离线回放鲁棒性方案
│   ├── Trace 核心设计.md            # [新名称] Trace 模块设计文档
│   ├── Watcher 实时分析.md          # [新名称] Watcher 方案
│   ├── CLI-Shell 协议.md           # [新名称] OAI-02-OpenBUS-CLI-Shell-Protocol.md
│   ├── AI-Assistant 插件.md        # [新名称] OAI-01-AI-Assistant-插件方案.md
│   └── README.md                   # 功能文档索引
│
├── 03-IMPLEMENTATION/             # 实施与交付类
│   ├── 构建基线.md                 # [保持] 编译构建规范
│   ├── 打包安装方案.md             # [合并] installer/脚本相关
│   ├── 驱动系统方案.md             # [保持] drivers/driver 架构
│   ├── 离线分析 ASC 兼容.md         # [保持] 数据格式适配
│   └── README.md                   # 实施文档索引
│
├── 04-REQUIREMENTS/               # 需求文档类
│   ├── 需求文档.md                 # [保持] 主需求规格
│   └── README.md                   # 需求索引
│
├── 05-TESTING/                    # 测试相关
│   ├── 测试验收方案.md             # [保持] 测试方法论
│   ├── DEF-06-ZLG 枚举崩溃调查.md    # [保留] Bug 深度分析
│   └── README.md                   # 测试文档索引
│
├── 06-PROJECT-MANAGEMENT/         # 项目管理类
│   ├── issues.md                   # [保持] 问题跟踪账本
│   ├── 插件市场批次一交付报告.md     # [迁移] release notes
│   ├── G14-Issue-Status.md         # [迁移] 迭代状态跟踪
│   ├── G15-Graphic-优化.md         # [迁移]
│   ├── G16-Graphic-增强功能.md     # [迁移]
│   ├── G17-Trace 列对齐方案.md      # [迁移]
│   └── README.md                   # PM 文档索引
│
└── 99-LEGACY/                     # 历史遗留归档
    ├── 拆分应用方案.md              # ← 废弃方案（移入此处）
    ├── 拆分应用实施方案.md          # ← 冗余（移入此处）
    ├── CLI-SHELL-Implementation-Progress-Milestone1.md  # ← 旧进度报告
    └── README.md                   # 归档说明
```

---

## ✨ 迁移计划（分步执行）

### Phase 1: 准备阶段（5 分钟）

1. 创建新目录结构（01-ARCHITECTURE ~ 06-PROJECT-MANAGEMENT）
2. 创建 README 导航文件（各子目录）

### Phase 2: 迁移文件（15 分钟）

#### Step 1: 架构类文档
```powershell
Move-Item "architecture.drawio", "drawio.svg", "flow.md" "01-ARCHITECTURE/"
Rename-Item "Trace 模块设计文档.md" "02-FEATURES\Trace 核心设计.md"
Rename-Item "Graphic 模块设计文档.md" "02-FEATURES\Graphic 核心设计.md"
Rename-Item "OAI-02-OpenBUS-CLI-Shell-Protocol.md" "02-FEATURES\CLI-Shell 协议.md"
Rename-Item "OAI-01-AI-Assistant-插件方案.md" "02-FEATURES\AI-Assistant 插件.md"
Rename-Item "Watcher 方案.md" "02-FEATURES\Watcher 实时分析.md"
```

#### Step 2: 实施类文档
```powershell
Move-Item "构建基线.md", "打包安装方案.md", "驱动系统方案.md", "离线分析 ASC 兼容与工程现场还原方案.md" "03-IMPLEMENTATION/"
```

#### Step 3: 需求类文档
```powershell
Move-Item "需求文档.md" "04-REQUIREMENTS/"
```

#### Step 4: 测试类文档
```powershell
Move-Item "测试验收方案.md", "DEF-06-复发-ZLG 枚举堆损坏崩溃调查.md" "05-TESTING/"
```

#### Step 5: 项目管理类文档
```powershell
Move-Item "issues.md", "G14-Issue-Status.md", "G15-Graphic-优化.md", "G16-Graphic-增强功能方案.md", "G17-Trace 列对齐方案.md", "插件市场批次一交付报告.md", "插件市场批次一功能清单.md" "06-PROJECT-MANAGEMENT/"
```

#### Step 6: 归档废弃文档
```powershell
Move-Item "拆分应用方案.md", "拆分应用实施方案.md", "CLI-SHELL-Implementation-Progress-Milestone1.md" "99-LEGACY/"
```

### Phase 3: 清理根目录（2 分钟）

删除空目录和临时文档（dir-cleanup-report.md, BUILD-SETUP-GUIDE.md 等）

### Phase 4: 更新交叉引用（5 分钟）

修复项目中可能的硬编码路径引用：
```bash
grep -r "doc/G14-Issue-Status" src/ scripts/
```

---

## 📝 目录 README 模板（每个子目录生成）

```markdown
# {目录名}

该目录存放 {类别描述}。

## 包含文档

| 文件名 | 描述 | 状态 |
|--------|------|------|
| xxx.md | ... | 已确认 |

## 维护说明

- 新增文档请先在此提交标题 → 审核通过后正式创建
- 废弃文档不要直接删除，先移至 `../99-LEGACY/`
- 变更重要设计时同步更新 `architecture.drawio`

---

**最后更新**: 2026-08-25  
**维护者**: Qoder AI Agent
```

---

## ✅ 预期效果

### 迁移前（混乱状态）
```
doc/
├── issues.md              ← 不知道是 PM 还是测试？
├── G14-Issue-Status.md   ← 没有上下文，孤立文件
├── OAI-02-...md          ← 太长，找不到规律
├── 拆分应用方案.md        ← 可能已废弃但不知何时删
└── 测试验收方案.md        ← 和测试报告是什么关系？
```

### 迁移后（清晰状态）
```
doc/
├── 00-README.md          ← 总导航（快速定位）
├── 01-ARCHITECTURE/      ← 架构层（顶层设计）
├── 02-FEATURES/          ← 功能层（具体实现）
├── 03-IMPLEMENTATION/    ← 实施层（工程化）
├── 04-REQUIREMENTS/      ← 需求层（业务源头）
├── 05-TESTING/           ← 质量层（验证保障）
├── 06-PROJECT-MANAGEMENT/← 管理层（迭代跟踪）
└── 99-LEGACY/            ← 废弃层（安全隔离）
```

---

## 🚀 立即执行

**请回复 "开始"**，我将按上述计划自动执行完整迁移流程。

**预计耗时**: ~30 分钟（含验证步骤）

---

**生成时间**: 2026-08-25 01:20:00  
**执行模式**: 自主全权（无需逐条确认）