// =============================================================================
//  qmlmenulibrary.cpp - QML MenuBar Library Implementation
// =============================================================================

#include "qmlmenulibrary.h"

MenuController::MenuController(QObject *parent) : QObject(parent)
{
    // 初始化完成
}

QVariantList MenuController::fileItems() const
{
    QVariantList list;
    
    MenuItemData items[] = {
        {"打开文件...", "Ctrl+O", "openFileRequested"},
        {"打开工程...", "Ctrl+Shift+O", "openProjectRequested"},
        {"保存工程", "Ctrl+Shift+S", "saveProjectRequested"},
        {"导入日志文件...", "Ctrl+I", "importLogFileRequested"},
        {"退出", "Alt+F4", "quitApplication"}
    };
    
    for (const auto &item : items) {
        list.append(item.toMap());
    }
    
    return list;
}

QVariantList MenuController::viewItems() const
{
    QVariantList list;
    
    MenuItemData items[] = {
        {"左侧栏", "", "toggleLeftDock", true, true},
        {"底部栏", "", "toggleBottomDock", true, false},
        {"右侧栏", "", "toggleRightDock", true, false},
        {"重置布局", "", "resetLayoutRequested"}
    };
    
    for (const auto &item : items) {
        list.append(item.toMap());
    }
    
    return list;
}

QVariantList MenuController::toolsItems() const
{
    QVariantList list;
    
    MenuItemData items[] = {
        {"Data Window", "Ctrl+Shift+D", "dataWindowRequested"},
        {"I/O Graph", "Ctrl+Shift+G", "ioGraphRequested"},
        {"Watcher 观测", "Ctrl+Shift+W", "watcherRequested"},
        {"着色规则编辑器...", "", "colorRuleEditorRequested"}
    };
    
    for (const auto &item : items) {
        list.append(item.toMap());
    }
    
    return list;
}

QVariantList MenuController::helpItems() const
{
    QVariantList list;
    
    MenuItemData items[] = {
        {"关于 openbus", "", "aboutRequested"},
        {"文档", "", "docsRequested"},
        {"快捷键", "", "shortcutsRequested"},
        {"许可证", "", "licenseRequested"}
    };
    
    for (const auto &item : items) {
        list.append(item.toMap());
    }
    
    return list;
}

// 额外信号处理
void MenuController::quitApplication() {
    QMetaObject::invokeMethod(qApp, "quit");
}
