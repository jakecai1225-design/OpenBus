// =============================================================================
//  qmlmenulibrary.h - QML MenuBar Library Header
// =============================================================================

#ifndef QMLMENU_LIBRARY_H
#define QMLMENU_LIBRARY_H

#include <QObject>
#include <QVariantList>
#include <QApplication>
#include <QtQmlIntegration/qqmlintegration.h>

/*!
 * \brief MenuController 菜单栏控制器（独立 DLL 版本）
 * 
 * 专为 QML 菜单栏设计的 C++ 后端控制器
 * 作为独立 DLL 加载，避免 openbus_data 依赖 Qt6::Qml
 */
class MenuController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    
public:
    explicit MenuController(QObject *parent = nullptr);
    
    // 菜单数据接口 (返回 QVariantMap 数组供 QML 解析)
    Q_PROPERTY(QVariantList fileItems READ fileItems CONSTANT)
    QVariantList fileItems() const;
    
    Q_PROPERTY(QVariantList viewItems READ viewItems CONSTANT)
    QVariantList viewItems() const;
    
    Q_PROPERTY(QVariantList toolsItems READ toolsItems CONSTANT)
    QVariantList toolsItems() const;
    
    Q_PROPERTY(QVariantList helpItems READ helpItems CONSTANT)
    QVariantList helpItems() const;

signals:
    // 文件菜单信号
    void openFileRequested();
    void openProjectRequested();
    void saveProjectRequested();
    void importLogFileRequested();
    
    // 视图菜单信号
    void toggleLeftDock(bool checked);
    void toggleBottomDock(bool checked);
    void toggleRightDock(bool checked);
    void resetLayoutRequested();
    
    // 工具菜单信号
    void dataWindowRequested();
    void ioGraphRequested();
    void watcherRequested();
    void colorRuleEditorRequested();
    
    // 帮助菜单信号
    void aboutRequested();
    void docsRequested();
    void shortcutsRequested();
    void licenseRequested();

private slots:
    void quitApplication();
    
private:
    struct MenuItemData {
        QString text;
        QString shortcut;
        QString signalName;
        bool checkable = false;
        bool checked = false;
        
        QVariant toMap() const {
            return QVariant::fromValue(QVariantMap{
                {"text", text},
                {"shortcut", shortcut},
                {"signal", signalName},
                {"checkable", checkable},
                {"checked", checked}
            });
        }
    };
};

#endif // QMLMENU_LIBRARY_H
