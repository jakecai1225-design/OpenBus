# ============================================================
# OpenBUS 构建环境配置（PowerShell Profile）
# ============================================================
# 首次运行前取消注释，并重启 PowerShell 窗口
# ============================================================

# # Qt 安装路径（实际位置：D:/Qt）
$env:SIN_QT_DIR = "D:/Qt/6.8.3/mingw_64"

# # MinGW 工具链路径
$env:SIN_MINGW_DIR = "D:/Qt/Tools/mingw1310_64"

# # CMake 路径
$env:SIN_CMAKE_DIR = "C:/Program Files/CMake"

# # PATH 优先级（确保 Qt 工具最先被找到）
$env:PATH = "$env:SIN_QT_DIR/bin;$env:SIN_MINGW_DIR/bin;$env:SIN_CMAKE_DIR/bin;$env:PATH"

# # 验证配置
Write-Host "`n✓ OpenBUS 构建环境配置已加载" -ForegroundColor Green
Write-Host "  Qt: $env:SIN_QT_DIR" -ForegroundColor Cyan
Write-Host "  MingW: $env:SIN_MINGW_DIR" -ForegroundColor Cyan
Write-Host "  CMake: $env:SIN_CMAKE_DIR" -ForegroundColor Cyan
