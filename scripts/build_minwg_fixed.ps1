# OpenBus MinGW Build Script (Fixed Environment for PowerShell)
# Fixes: AutoMoc predefines issue, PATH/INCLUDE environment inheritance

$ErrorActionPreference = 'Stop'

Write-Host "`n=========================================" -ForegroundColor Cyan
Write-Host "OpenBuild MinGW - Fixed Environment" -ForegroundColor Cyan
Write-Host "=========================================`n" -ForegroundColor Cyan

# ============================================================
#  Set explicit paths (overriding inherited env vars)
# ============================================================
$QT_DIR = "D:\Qt\6.8.3\mingw_64"
$MINGW_DIR = "D:\Qt\Tools\mingw1310_64"
$CMAKE_EXE = "C:\Program Files\CMake\bin\cmake.exe"
$PROJECT_ROOT = $PSScriptRoot | Split-Path | Join-Path -ChildPath "." | Resolve-Path

Write-Host "[INFO] Project root: $PROJECT_ROOT" -ForegroundColor Gray
Write-Host "[INFO] Qt path: $QT_DIR" -ForegroundColor Gray
Write-Host "[INFO] MinGW path: $MINGW_DIR" -ForegroundColor Gray
Write-Host "[INFO] CMake exe: $CMAKE_EXE" -ForegroundColor Gray
Write-Host ""

# Update PATH and INCLUDE for this session
$env:PATH = "$MINGW_DIR\bin;C:\Program Files\CMake\bin;$QT_DIR\bin;" + $env:PATH
$env:INCLUDE = `
    "$MINGW_DIR\lib\gcc\x86_64-w64-mingw32\13.1.0\include;" +
    "$MINGW_DIR\x86_64-w64-mingw32\include;" +
    "$MINGW_DIR\lib\gcc\x86_64-w64-mingw32\13.1.0\include-fixed;" +
    $env:INCLUDE

Write-Host "[INFO] Environment configured." -ForegroundColor Green
Write-Host ""

# ============================================================
#  Clean build directory
# ============================================================
$buildDir = Join-Path $PROJECT_ROOT "build"

if (Test-Path $buildDir) {
    Write-Host "[INFO] Cleaning old build directory..." -ForegroundColor Yellow
    Remove-Item $buildDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Set-Location $buildDir

Write-Host "[INFO] Configuring with CMake..." -ForegroundColor Cyan
Write-Host ""

# ============================================================
#  Configure CMake (with AutoMoc fix from CMakeLists.txt)
# ============================================================
# Use simple direct call instead of splatting
$configureCmd = "cmake -G `"MinGW Makefiles`" ` -DCMAKE_PREFIX_PATH=$QT_DIR ` -DCMAKE_CXX_COMPILER=`"$MINGW_DIR/bin/g++.exe`" ` -DCMAKE_C_COMPILER=`"$MINGW_DIR/bin/gcc.exe`" ` -DCMAKE_MAKE_PROGRAM=`"$MINGW_DIR/bin/mingw32-make.exe`" `$PROJECT_ROOT"

try {
    Invoke-Expression $configureCmd
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERROR] CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "`n[SUCCESS] Configuration complete!" -ForegroundColor Green
    Write-Host ""
    
    # ============================================================
    #  Build
    # ============================================================
    Write-Host "[INFO] Building project... (parallel: 8 jobs)" -ForegroundColor Cyan
    Write-Host ""
    
    cmake --build . --config Debug -- -j8
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERROR] Build failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "`n[SUCCESS] Build completed successfully!" -ForegroundColor Green
    Write-Host "Output: $buildDir\bin\openbus.exe" -ForegroundColor White
    
} catch {
    Write-Host "`n[ERROR] $_" -ForegroundColor Red
    exit 1
} finally {
    Set-Location $PROJECT_ROOT
}
