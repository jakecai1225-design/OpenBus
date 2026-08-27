import QtQuick
import QtQuick.Controls

/*!
 * \brief ToolsMenu 工具子菜单
 * 
 * 包含各种开发工具：Data Window、I/O Graph、Watcher 等
 */
Menu {
    id: toolsMenuComponent
    
    title: "工具 (&T)"
    
    property var menuItems: []
    
    Repeater {
        model: menuItems
        
        MenuItem {
            text: modelData.text
            shortcut: modelData.shortcut || ""
            
            onTriggered: {
                if (modelData.onTriggered) {
                    modelData.onTriggered()
                }
            }
            
            MenuSeparator {}
        }
    }
}
