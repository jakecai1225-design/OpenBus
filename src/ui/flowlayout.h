#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

#include <QLayout>
#include <QList>

/**
 * @brief 流式布局 — 子项按行排列，宽度不足自动换行（Qt 官方 FlowLayout 示例移植）
 *
 * 市场首页卡片网格使用（VS Code marketplace 网页版多列自适应换行版式，
 * doc/驱动系统方案.md §13.5）：卡片等宽固定尺寸，随视口宽度 1..N 列换行，
 * 无需按分辨率手工分栏。
 */
class FlowLayout : public QLayout
{
    Q_OBJECT
public:
    explicit FlowLayout(QWidget *parent, int margin = -1,
                        int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> m_itemList;
    int m_hSpace;
    int m_vSpace;
};

#endif // FLOWLAYOUT_H
