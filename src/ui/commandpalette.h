#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QFrame>
#include <QVector>
#include <functional>

class QLineEdit;
class QListWidget;
class QLabel;

/**
 * @brief VS Code-style quick open / command palette popup.
 *
 * Prefixes (same idea as VS Code):
 *   >   commands only
 *   @   plugins
 *   #   settings
 *   (none) fuzzy match across files, commands, plugins, settings, views
 */
class CommandPalette : public QFrame
{
    Q_OBJECT

public:
    enum class Kind {
        Command,
        File,
        Plugin,
        Setting,
        View
    };

    struct Item {
        Kind kind = Kind::Command;
        QString label;
        QString detail;
        QString filterText; // lowercased haystack
        std::function<void()> run;
    };

    explicit CommandPalette(QWidget *parent = nullptr);

    void setItems(const QVector<Item> &items);
    void openCentered(QWidget *anchor = nullptr);
    void openBelow(const QRect &globalAnchorRect);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onQueryChanged(const QString &text);
    void onActivateCurrent();

private:
    void rebuildList();
    static int fuzzyScore(const QString &query, const QString &haystack);
    static QString kindLabel(Kind k);

    QLineEdit *m_input = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QVector<Item> m_all;
    QVector<int> m_filtered; // indices into m_all
};

#endif // COMMANDPALETTE_H
