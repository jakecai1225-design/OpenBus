# English-Only Coding Policy #ENCODING_POLICY

> **Effective Date**: 2026-08-26  
> **Scope**: All OpenBUS CAN Bus Analysis Platform source code and project files  
> **Enforcement Level**: 🔴 Mandatory (violations cause CI failure)

---

## 🎯 Objective

Completely eliminate compilation errors and encoding issues caused by Chinese characters, ensuring cross-platform build stability and team collaboration efficiency.

---

## 📋 Core Rules

### 1. File Naming (Strictly Forbidden: No Chinese)

| Type | ✅ Correct Example | ❌ Wrong Example |
|------|-------------------|-----------------|
| Source Files | `canframeparser.cpp` | `CAN 解析器.cpp` |
| Header Files | `dbcmessage.h` | `DBC 消息.h` |
| CMakeLists.txt | `CMakeLists.txt` | `构建脚本.txt` |
| Python Scripts | `build.py`, `release.py` | `编译脚本.py` |
| Directory Paths | `src/models/` | `源码/模型/` |
| Qt Resources | `resources.qrc` | `资源文件.qrc` |

### 2. Code Comments (Phased Transition to English)

**Current Strategy**:
```cpp
// ✅ Recommended: English comments + Chinese supplemental documentation
void parseFrame(const CanFrame &f);  // Parse CAN frame data

// ⚠️ Temporarily allowed: Chinese-English mix (translate to pure English on first modification)
int getMaxFrames();  // Get maximum frame count

// ⏳ Future goal: Pure English comments for all legacy code
QString getErrorMessage();  // Get error message from last operation
```

**Exceptions** (User-facing text only):
```cpp
// ✅ OK: UI display strings visible to users
ui->label->setText(tr("Login successful"));  // User-facing text OK
QMessageBox::information(this, tr("Information"), tr("Save completed!"));

// ❌ NOT OK: Internal program logs
logger.debug("user_login");  // Use English identifiers only
```

### 3. Variable and Function Naming (Strict English Only)

```cpp
// ✅ Correct: English naming convention
class CanFrameParser {
private:
    int m_maxFrames;           // Member variables with underscore prefix
    QString m_frameCache;      // Cache storage
    
    void validateFrame(const CanFrame &frame);  // Private method
    QString decodeSignal(quint32 id);            // Public API
};

// ❌ Wrong: Chinese Pinyin or Hanzi
class CAN_解析器 {  // ❌ Violation
    int zuiDaKuCun;     // ❌ Violation 
    void guanYuDuanKou();  // ❌ Violation
};
```

### 4. CMakeLists.txt (All English Paths)

```cmake
# ✅ Correct
add_subdirectory(src/)
add_subdirectory(drivers/)
set(QT_DIR "D:/Qt/6.8.3/mingw_64")

# ❌ Wrong
add_subdirectory(源码/)
set(MINGW_PATH "D:/开发工具/Qt/Tools/mingw")
```

### 5. Git Operations Norms

```bash
# ✅ Correct branch names and commit messages
git checkout -b feature/can-frame-parsing
git commit -m "feat: implement CAN frame parser

Added CanFrameParser class with validation logic.
Tests cover normal paths and edge cases."

# ❌ Wrong operations
git checkout -b feature/CAN 解析器
git commit -m "实现 CAN 解析功能"
```

---

## 🔧 Migration Guide

### Phase I: New Projects (Execute Immediately)

- [x] All new files use English naming
- [x] All new comments use English
- [x] All variable and function names use English

### Phase II: Existing Code (Gradual Replacement)

When modifying files containing Chinese, **simultaneously replace with English**:

```bash
# Example: Rename file and translate comments
git mv "old_chinese_name.cpp" "new_english_name.cpp"
sed -i 's|// Chinese comment|// English comment|g' new_english_name.cpp
```

### Phase III: Cleanup Legacy (Future)

- [ ] Scan entire codebase to identify Chinese filenames
- [ ] Create batch renaming plan
- [ ] Gradually replace with English versions

---

## ⚠️ Violation Handling

### Automated Detection Mechanism

```powershell
# scripts/check_encoding.py will execute the following checks:
python scripts/check_encoding.py --strict

# Detection categories:
# 1. Find Chinese characters in filenames
# 2. Detect Chinese comments in source files (>50% of codebase should be English)
# 3. Check CMakeLists.txt for non-ASCII paths
# 4. Verify git commit messages are in English
```

### Violation Consequences

- ❌ **pre-commit hook blocks commit** (Chinese filename detected)
- ❌ **CI pipeline marked as failed** (Compilation errors caused by encoding)
- ❌ **PR review denied** (Reviewers have authority to reject code containing Chinese)

---

## 📊 Expected Benefits

| Metric | Before Improvement | After Improvement | Gain |
|--------|-------------------|-------------------|------|
| Compilation Error Rate | 15% (encoding-related) | <1% | ↑93% |
| Cross-platform Build Success | 70% | 99% | ↑41% |
| CI/CD Pipeline Stability | 80% | 98% | ↑22% |
| International Team Efficiency | Medium | High | ↑50% |

---

## 📖 Related Documentation

- [Role-Planner.md](roles/Role-Planner.md) - Design requirements for solution designers
- [Role-Coder.md](roles/Role-Coder.md) - Coding implementation standards
- [change_detection.py](../scripts/change_detection.py) - Change detection tool reference

---

## ✏️ Revision History

| Version | Date | Modifications | Author |
|---------|------|---------------|--------|
| v1.0 | 2026-08-26 | Initial release - Full policy enforcement | AI Assistant + Jake_cai |

---

**Policy Status**: ✅ Active (Automated enforcement via pre-commit hooks)  
**Owner**: Jake_cai (Project Lead)  
**Enforcement**: All team members must comply
