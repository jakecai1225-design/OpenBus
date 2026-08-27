import QtQuick
import QtQuick.Controls

/*!
 * \brief HelpMenu 帮助子菜单
 * 
 * 包含关于、文档、快捷键、许可证等帮助信息
 */
Menu {
    id: helpMenuComponent
    
    title: "帮助 (&H)"
    
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
