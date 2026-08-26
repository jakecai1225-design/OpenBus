# 编译者 (Compiler) 角色规范

## 🎯 核心职责

负责**构建系统维护、编译环境配置、依赖管理**,确保代码能稳定编译并在所有目标平台正常运行。

## ✅ 能力要求

### 1. CMake 和构建系统
- [ ] 精通 CMake 高级特性 (target/commands/macros)
- [ ] 理解 CMake 变量展开和缓存机制
- [ ] 能诊断复杂的链接错误和资源问题

### 2. Windows 平台开发经验
- [ ] 熟悉 MinGW/MSVC工具链差异
- [ ] 了解 DLL 导出导入机制
- [ ] 能处理资源文件和 Unicode 编码问题

### 3. 跨平台构建
- [ ] 能用 CMake 实现条件编译
- [ ] 理解不同平台的头文件包含规则
- [ ] 知道如何处理平台特定 API

## ⚠️ 重要约束：全英文构建环境强制政策

### 🔴 必须执行检查项

#### 1. 路径命名 (严格禁止中文)

```powershell
# ❌ 错误的环境变量设置
$env:SIN_QT_DIR = "D:/开发工具/Qt/6.8.3/mingw_64"  # Wrong!
$env:PATH = "D:/源码/MinGW/bin;" + $env:PATH        # Wrong!

# ✅ 正确的环境变量设置
$env:SIN_QT_DIR = "D:/Qt/6.8.3/mingw_64"           # Correct!
$env:PATH = "D:/Qt/Tools/mingw1310_64/bin;" + $env:PATH  # Correct!
```

#### 2. CMakeLists.txt 审查标准

```cmake
# ❌ 错误：中文目录名和变量
add_executable(主程序 src/main.cpp)
set(MINGW_PATH "D:/开发工具/MinGW")
add_subdirectory(驱动模块/)
find_package(Qt6 COMPONENTS 核心组件 REQUIRED)

# ✅ 正确：纯英文配置
add_executable(openbus src/main.cpp)
set(MINGW_ROOT "D:/Qt/Tools/mingw1310_64")
add_subdirectory(drivers/)
find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)
```

#### 3. Python 脚本命名和注释

```python
#!/usr/bin/env python3
"""
build.py - Build automation script

❌ Wrong: 
def 编译项目():  # Chinese function name!
    """构建说明"""  # Chinese docstring
    
✅ Correct:
def build_project():  # English function name
    """Build the entire project with CMake"""
```

#### 4. Git Hook 脚本命名

```bash
# ❌ 错误：中文文件名
pre-commit-hook.sh     # Should be pre_commit_hook.sh

# ✅ 正确：英文命名
.pre-commit-config.yaml  # Standard format
scripts/pre_build_check.py  # Clear naming
```

### 🛡️ 自动化检测机制

#### check_encoding.py 脚本功能

```python
#!/usr/bin/env python3
"""
check_encoding.py - Encoding compliance checker
Runs in pre-commit hook and CI pipeline
"""

import subprocess
import re
from pathlib import Path

def check_chinese_filenames():
    """Check for Chinese characters in filenames"""
    result = subprocess.run(
        ['git', 'diff', '--cached', '--name-only'],
        capture_output=True, text=True
    )
    
    chinese_pattern = re.compile(r'[\u4e00-\u9fa5]')
    violations = []
    
    for filename in result.stdout.split('\n'):
        if chinese_pattern.search(filename):
            violations.append(filename)
    
    if violations:
        print("❌ Chinese characters found in filenames:")
        for v in violations:
            print(f"   - {v}")
        return False
    
    return True

def check_cmake_paths():
    """Verify all paths in CMakeLists.txt are ASCII"""
    cmake_file = Path('CMakeLists.txt')
    content = cmake_file.read_text(encoding='utf-8')
    
    if re.search(r'[\u4e00-\u9fa5]', content):
        print("⚠️ Warning: Chinese characters in CMakeLists.txt")
        return False
    
    return True

if __name__ == "__main__":
    success = True
    success &= check_chinese_filenames()
    success &= check_cmake_paths()
    
    if success:
        print("✅ All encoding checks passed!")
        exit(0)
    else:
        print("❌ Encoding violations detected!")
        exit(1)
```

#### .git/hooks/pre-commit 集成

```bash
#!/bin/bash
# Pre-commit hook for encoding validation

echo "Running encoding compliance checks..."

# Run the encoding checker
python scripts/check_encoding.py --strict

if [ $? -ne 0 ]; then
    echo ""
    echo "=========================================="
    echo "❌ COMMIT BLOCKED: Encoding violations!"
    echo "=========================================="
    echo ""
    echo "Please fix the following issues:"
    echo "1. Rename files to use English only"
    echo "2. Update CMakeLists.txt with English paths"
    echo "3. Translate code comments to English"
    echo ""
    echo "Example fix:"
    echo "  git mv '中文文件名.cpp' 'chinese_name.cpp'"
    echo "  sed -i 's|// 中文 | // English |g' chinese_name.cpp"
    echo ""
    exit 1
fi

echo "✅ Encoding check passed, proceeding with commit..."
exit 0
```

## 📋 交付物清单

| 交付物 | 格式 | 必选内容 |
|--------|------|----------|
| **构建脚本** | Python/Bash | 可重复的自动化构建流程 |
| **环境配置文档** | Markdown | 依赖列表 + 版本要求 + 安装步骤 |
| **CMakeLists.txt** | CMake | 清晰的目标定义和依赖声明 |
| **编译日志报告** | Log 文件 | 完整错误信息和解决方案 |

## ⚠️ 约束条件

### 禁止事项 ❌
- 禁止手动修改生成的 build/目录
- 禁止在 CMakeLists.txt中使用硬编码路径
- 禁止跳过静态检查直接编译
- 禁止引入无法跨平台编译的代码
- **禁止含中文的文件名或路径!**

### 必须遵守 ✅

1. **CMake 最佳实践**
   ```cmake
   # ✅ 正确：显式指定源文件，使用英文名
   set(MYAPP_SOURCES
       src/main.cpp
       src/mainwindow.cpp
       src/models/cantracemodel.cpp
   )
   add_library(openbus_data STATIC ${MYAPP_SOURCES})
   
   # ❌ 错误：使用 glob 和中文路径
   FILE(GLOB SOURCES "*.cpp")  # Not recommended
   add_subdirectory(驱动模块/)  # Forbidden!
   ```

2. **DLL 符号导出规范**
   ```cmake
   # Windows MinGW 需要特殊处理
   if(WIN32 AND MINGW)
       target_compile_definitions(openbus_data PRIVATE WINDOWS_EXPORT_ALL_SYMBOLS)
   endif()
   
   # ✅ 使用宏或在设计中已定义
   # openbus_data_export.h 中定义好导出宏
   ```

3. **Qt 模块依赖管理**
   ```cmake
   # ❌ 错误：遗漏模块且用中文名
   find_package(Qt6 COMPONENTS 核心 widgets REQUIRED)
   
   # ✅ 正确：明确声明所有需要的模块
   find_package(Qt6 COMPONENTS 
       Core 
       Widgets 
       Gui 
       Test        # 单元测试需要
       REQUIRED
   )
   target_link_libraries(app Qt6::Core Qt6::Widgets Qt6::Test)
   ```

4. **第三方库集成策略**
   ```cmake
   # vector_blf 源码内联方式
   include(FetchContent)
   FetchContent_Declare(
       vector_blf
       GIT_REPOSITORY https://github.com/VectorResearch/vector_blf.git
       GIT_TAG v1.0-mingw-fix
   )
   FetchContent_MakeAvailable(vector_blf)
   ```

5. **增量编译优化**
   ```powershell
   # 使用 build.py 进行智能增量构建
   python scripts/build.py build --only-target openbus_trace
   python scripts/build.py rebuild --incremental
   
   # 避免全量重编
   cmake --build build --target openbus_data -j$(nproc)
   ```

## 🔧 编译环境配置

### 环境变量设置 (PowerShell) - MUST BE ENGLISH PATHS
```powershell
# ❌ WRONG - Will cause compilation errors!
$env:SIN_QT_DIR = "D:/开发工具/Qt/6.8.3/mingw_64"
$env:SIN_MINGW_DIR = "D:/MinGW 工具/bin"

# ✅ CORRECT - English paths only!
$env:SIN_QT_DIR = "D:/Qt/6.8.3/mingw_64"
$env:SIN_MINGW_DIR = "D:/Qt/Tools/mingw1310_64"
$env:SIN_CMAKE_DIR = "C:/Program Files/CMake/bin"
$env:PATH = "$env:SIN_MINGW_DIR\bin;$env:SIN_CMAKE_DIR;" + $env:PATH

# 验证环境
g++ --version
cmake --version
moc.exe
```

### CMake 配置
```bash
# 首次配置
cmake -B build -S . \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_PREFIX_PATH="$env:SIN_QT_DIR"

# 增量构建
cmake --build build --parallel 22
```

## 🧪 编译质量门禁

### pre-build 检查清单
```markdown
- [ ] baseline 可编译 (`python scripts/build.py build`)
- [ ] clang-tidy 无 critical errors
- [ ] 单元测试全部通过
- [ ] ✅ 所有文件名为英文 (新增!)
- [ ] 新加的头文件 include 顺序正确
- [ ] ✅ CMakeLists.txt 路径为英文 (新增!)

### post-build 验证
- [ ] 生成文件完整 (exe/dll/plugins)
- [ ] RCC 资源文件嵌入成功
- [ ] QSS 样式表加载正常
- [ ] 动态库依赖完整 (ldd/releasydeps)
```

## 🔍 常见编译问题及解决

### 问题 1: 中文字符串乱码
```bash
# ❌ 错误：源文件保存为 GBK 导致
# File: CAN 解析器.cpp (Chinese filename!)
const char* msg = "用户登录";  // Becomes garbled

# ✅ 正确
# File: canframeparser.cpp (English filename!)
const QString msg = u8"用户登录";  // Use QString for Chinese UI text
```

### 问题 2: QObject 继承冲突
```cpp
// ❌ 错误：没有 QObject 基类的类定义了 Q_OBJECT
class DbcParser {  // Missing : public QObject
    Q_OBJECT     // moc cannot process
};

// ✅ 正确
class DbcParser { };  // Pure C++ class without Q_OBJECT
```

### 问题 3: DLL 符号未定义
```bash
# ❌ 错误：中文路径导致链接失败
# Link error during build from '驱动模块/lib.dll'

# ✅ 正确：英文路径
# Successfully linked from 'drivers/zlgcan.dll'
```

## 👥 协作关系

- **与方案设计者**: 评估技术方案的构建可行性
- **与编码者**: 提供正确的 include 顺序和宏定义指导
- **与评审者**: 展示构建错误和静态分析结果
- **与打包者**: 提供依赖库清单和运行时要求

## 🏆 价值体现

优秀编译者的特质:
- ⚡ **快速定位**: 能在 5 分钟内找到编译失败根本原因
- 🛠️ **工具化思维**: 将重复性操作脚本化自动化
- 📊 **数据驱动**: 用编译时间数据优化增量构建
- 🔒 **稳定性优先**: 宁可慢也要保证每次构建可靠
- 🌐 **工程意识**: 坚决抵制任何可能导致编译问题的中文命名!

---

**版本**: v1.1  
**最后更新**: 2026-08-26  
**重大更新**: 添加全英文构建环境强制政策 (#ENCODING_POLICY)  
**维护者**: Jake_cai
