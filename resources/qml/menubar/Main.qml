// =============================================================================
//  Main.qml - QML MenuBar 界面
// =============================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15

// ✅ 关键修复：使用 ApplicationWindow 内联模式!
Item {
    id: root
    
    // ✅ 不依赖 parent.width，直接指定默认宽度
    width: 1024
    height: 30
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        MenuBar {
            Layout.fillWidth: true
            id: menuBar
            
            // ---- File 菜单 ----
            Menu {
                title: "文件 (&F)"
                
                MenuItem {
                    text: "打开文件... (Ctrl+O)"
                    shortcut: "Ctrl+O"
                    onTriggered: menuController.openFileRequested()
                }
                
                MenuItem {
                    text: "打开工程... (Ctrl+Shift+O)"
                    shortcut: "Ctrl+Shift+O"
                    onTriggered: menuController.openProjectRequested()
                }
                
                MenuItem {
                    text: "保存工程 (Ctrl+Shift+S)"
                    shortcut: "Ctrl+Shift+S"
                    onTriggered: menuController.saveProjectRequested()
                }
                
                MenuItem {
                    text: "导入日志文件... (Ctrl+I)"
                    shortcut: "Ctrl+I"
                    onTriggered: menuController.importLogFileRequested()
                }
                
                Separator {}
                
                MenuItem {
                    text: "退出 (Alt+F4)"
                    shortcut: "Alt+F4"
                    onTriggered: menuController.quitApplication()
                }
            }
            
            // ---- View 菜单 ----
            Menu {
                title: "视图 (&V)"
                
                MenuCheckableAction {
                    text: "左侧栏"
                    checked: true
                    onTriggered: menuController.toggleLeftDock(true)
                }
                
                MenuCheckableAction {
                    text: "底部栏"
                    checked: false
                    onTriggered: menuController.toggleBottomDock(false)
                }
                
                MenuCheckableAction {
                    text: "右侧栏"
                    checked: false
                    onTriggered: menuController.toggleRightDock(false)
                }
                
                Separator {}
                
                MenuItem {
                    text: "重置布局"
                    onTriggered: menuController.resetLayoutRequested()
                }
            }
            
            // ---- Tools 菜单 ----
            Menu {
                title: "工具 (&T)"
                
                MenuItem {
                    text: "Data Window (Ctrl+Shift+D)"
                    shortcut: "Ctrl+Shift+D"
                    onTriggered: menuController.dataWindowRequested()
                }
                
                MenuItem {
                    text: "I/O Graph (Ctrl+Shift+G)"
                    shortcut: "Ctrl+Shift+G"
                    onTriggered: menuController.ioGraphRequested()
                }
                
                MenuItem {
                    text: "Watcher 观测 (Ctrl+Shift+W)"
                    shortcut: "Ctrl+Shift+W"
                    onTriggered: menuController.watcherRequested()
                }
                
                MenuItem {
                    text: "着色规则编辑器..."
                    onTriggered: menuController.colorRuleEditorRequested()
                }
            }
            
            // ---- Help 菜单 ----
            Menu {
                title: "帮助 (&H)"
                
                MenuItem {
                    text: "关于 openbus"
                    onTriggered: menuController.aboutRequested()
                }
                
                MenuItem {
                    text: "文档"
                    onTriggered: menuController.docsRequested()
                }
                
                MenuItem {
                    text: "快捷键"
                    onTriggered: menuController.shortcutsRequested()
                }
                
                MenuItem {
                    text: "许可证"
                    onTriggered: menuController.licenseRequested()
                }
            }
        }
    }
}
