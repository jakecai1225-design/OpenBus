#!/usr/bin/env python3
"""
Generate Qt6QuickWidgets import library for MinGW Qt6.8.3
This script creates a .def file and uses dlltool to generate libQt6QuickWidgets.a
"""

import os
import subprocess
import sys

QT_BIN = r"D:\Qt\6.8.3\mingw_64\bin"
DLLTOOL = r"D:\Qt\Tools\mingw1310_64\bin\dlltool.exe"

# DEF file content
DEF_CONTENT = """LIBRARY Qt6QuickWidgets.dll
EXPORTS
    @1 QQuickWidget::QQuickWidget@8 
    @2 QQuickWidget::QQuickWidget@12
    @3 QQuickWidget::~QQuickWidget@4  
    @4 QQuickWidget::setResizeMode@4
    @5 QQuickWidget::resizeMode@4
    @6 QQuickWidget::setSource@4
    @7 QQuickWidget::source@4
    @8 QQuickWidget::engine@4
    @9 QQuickWidget::rootObject@4
"""

def main():
    print("\n🛠️ Generating Qt6QuickWidgets import library")
    print("=" * 50)
    
    # Create DEF file
    def_path = os.path.join(QT_BIN, "qtquickwidgets.def")
    print(f"\n[1/2] Creating DEF file: {def_path}")
    
    try:
        with open(def_path, 'w', encoding='ascii') as f:
            f.write(DEF_CONTENT)
        print("✅ DEF file created successfully")
    except PermissionError:
        print(f"❌ ERROR: Cannot write to {QT_BIN}. Try running as Administrator!")
        return False
    except Exception as e:
        print(f"❌ ERROR: Failed to create DEF file: {e}")
        return False
    
    # Generate import library
    print(f"\n[2/2] Running dlltool...")
    cmd = [DLLTOOL, "-d", "qtquickwidgets.def", "-l", "libQt6QuickWidgets.a", "-D", "Qt6QuickWidgets.dll"]
    
    try:
        result = subprocess.run(cmd, cwd=QT_BIN, capture_output=True, text=True, timeout=30)
        
        if result.returncode == 0:
            lib_path = os.path.join(QT_BIN, "libQt6QuickWidgets.a")
            if os.path.exists(lib_path):
                size = os.path.getsize(lib_path) / 1024
                print(f"✅ SUCCESS! Import library generated:")
                print(f"   Location: {lib_path}")
                print(f"   Size: {size:.1f} KB")
                print(f"\n🎯 Now you can compile! Run:")
                print(f"   Remove-Item build -Recurse -Force")
                print(f"   python scripts/build.py configure")
                print(f"   python scripts/build.py build -j8")
                return True
            else:
                print("❌ FAILED: dlltool exited successfully but lib not found")
                return False
        else:
            print(f"❌ FAILED: dlltool returned exit code {result.returncode}")
            print(f"STDOUT: {result.stdout}")
            print(f"STDERR: {result.stderr}")
            return False
            
    except FileNotFoundError:
        print(f"❌ ERROR: dlltool not found at {DLLTOOL}")
        return False
    except subprocess.TimeoutExpired:
        print("❌ ERROR: dlltool timed out")
        return False
    except Exception as e:
        print(f"❌ ERROR: Unexpected error: {e}")
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
