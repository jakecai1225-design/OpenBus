import QtQuick
import QtQuick.Controls

/*!
 * \brief FileMenu 文件子菜单
 * 
 * 包含文件相关的操作：新建、打开、保存、导入、导出、退出等
 */
Menu {
    id: fileMenuComponent
    
    title: "文件 (&F)"
    
    property var menuItems: []
    
    Repeater {
        model: menuItems
        
        MenuItem {
            text: modelData.text
            shortcut: modelData.shortcut || ""
            checkable: modelData.checkable || false
            
            onTriggered: {
                if (modelData.onTriggered) {
                    modelData.onTriggered()
                }
            }
            
            MenuSeparator {}
        }
    }
}
