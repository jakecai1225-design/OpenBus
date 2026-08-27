import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/*!
 * \brief MenuBarMain 主菜单栏组件
 * 
 * 使用 QML 实现 VS Code 风格的菜单栏
 * 支持热重载和主题切换
 */
Menu {
    id: menuBarMain
    
    // 主窗口引用 (通过 C++ 传递)
    property MainWindow mainWindow: null
    
    // 从 C++ 获取菜单数据
    property var fileItems: MenuController.fileItems
    property var viewItems: MenuController.viewItems
    property var toolsItems: MenuController.toolsItems
    property var helpItems: MenuController.helpItems
    
    Component.onCompleted: {
        console.log("QML MenuBar initialized")
    }
    
    // File Menu
    Menu {
        title: "文件 (&F)"
        
        Repeater {
            model: fileItems
            
            MenuItem {
                text: modelData.text
                shortcut: modelData.shortcut
                checkable: modelData.checkable || false
                
                onTriggered: handleMenuItem(modelData.signal, true)
            }
            
            MenuSeparator {}
        }
    }
    
    // View Menu
    Menu {
        title: "视图 (&V)"
        
        Repeater {
            model: viewItems
            
            MenuItem {
                text: modelData.text
                shortcut: modelData.shortcut
                checkable: modelData.checkable || false
                checked: modelData.checked || false
                
                onToggled: (checked) => handleMenuItem(modelData.signal, checked)
            }
            
            MenuSeparator {}
        }
    }
    
    // Tools Menu
    Menu {
        title: "工具 (&T)"
        
        Repeater {
            model: toolsItems
            
            MenuItem {
                text: modelData.text
                shortcut: modelData.shortcut
                
                onTriggered: handleMenuItem(modelData.signal, false)
            }
            
            MenuSeparator {}
        }
    }
    
    // Help Menu
    Menu {
        title: "帮助 (&H)"
        
        Repeater {
            model: helpItems
            
            MenuItem {
                text: modelData.text
                shortcut: modelData.shortcut
                
                onTriggered: handleMenuItem(modelData.signal, false)
            }
            
            MenuSeparator {}
        }
    }
    
    // 统一处理函数 - 转发信号给 C++
    function handleMenuItem(signalName, state) {
        switch(signalName) {
            case "openFileRequested":
                if (mainWindow) mainWindow.openFile()
                break
            case "openProjectRequested":
                if (mainWindow) mainWindow.openProject()
                break
            case "saveProjectRequested":
                if (mainWindow) mainWindow.saveProject()
                break
            case "importLogFileRequested":
                if (mainWindow) mainWindow.importLogFile()
                break
            case "quitApplication":
                Qt.quit()
                break
            case "toggleLeftDock":
                if (mainWindow) mainWindow.toggleLeftDock(state)
                break
            case "toggleBottomDock":
                if (mainWindow) mainWindow.toggleBottomDock(state)
                break
            case "toggleRightDock":
                if (mainWindow) mainWindow.toggleRightDock(state)
                break
            case "resetLayoutRequested":
                if (mainWindow) mainWindow.resetLayout()
                break
            case "dataWindowRequested":
                if (mainWindow) mainWindow.dataWindow()
                break
            case "ioGraphRequested":
                if (mainWindow) mainWindow.ioGraph()
                break
            case "watcherRequested":
                if (mainWindow) mainWindow.watcher()
                break
            case "aboutRequested":
                if (mainWindow) mainWindow.about()
                break
            case "docsRequested":
                if (mainWindow) mainWindow.docs()
                break
            case "shortcutsRequested":
                if (mainWindow) mainWindow.shortcuts()
                break
            case "licenseRequested":
                if (mainWindow) mainWindow.license()
                break
            default:
                console.log("Unknown signal:", signalName)
        }
    }
}
