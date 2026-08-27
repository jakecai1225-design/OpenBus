import QtQuick
import QtQuick.Controls

/*!
 * \brief MenuItem 菜单项组件
 * 
 * 可复用的标准菜单项，支持文本、快捷键、图标等属性
 */
MenuItem {
    id: menuItemInstance
    
    // 扩展样式
    background: Rectangle {
        color: menuBarItem.hovered ? "#3d3d3d" : "transparent"
        border.color: "#3c3c3c"
        border.width: 1
        radius: 4
    }
    
    contentItem: Text {
        text: menuBarItem.text
        font: menuBarItem.font
        color: "#e0e0e0"
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        padding: 8
        
        Text {
            anchors.right: parent.right
            text: menuBarItem.shortcut
            color: "#888888"
            font.pixelSize: 10
            visible: menuBarItem.shortcut !== ""
        }
    }
}
