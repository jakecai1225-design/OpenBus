import QtQuick
import QtQuick.Controls

/*!
 * \brief ViewMenu 视图子菜单
 * 
 * 包含侧边栏切换、布局控制等视图相关操作
 */
Menu {
    id: viewMenuComponent
    
    title: "视图 (&V)"
    
    property var menuItems: []
    
    Repeater {
        model: menuItems
        
        MenuItem {
            text: modelData.text
            shortcut: modelData.shortcut || ""
            checkable: modelData.checkable || false
            checked: modelData.checked || false
            
            onToggled: (checked) => {
                if (modelData.onToggled) {
                    modelData.onToggled(checked)
                }
            }
            
            MenuSeparator {}
        }
    }
}
