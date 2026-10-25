#pragma once

#include <QLayout>
#include <QWidget>

// Compact action rows wrap at the available width, including inside a scroll
// area. Their minimum width is one control, rather than the sum of the row.
class FlowLayout final : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr) : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(6);
    }
    ~FlowLayout() override
    {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (auto *item : m_items)
            if (!item->isEmpty())
                size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    int arrange(const QRect &rect, bool apply) const
    {
        const auto margins = contentsMargins();
        const QRect area = rect.marginsRemoved(margins);
        int x = area.x(), y = area.y(), rowHeight = 0;
        for (auto *item : m_items)
        {
            if (item->isEmpty())
                continue;
            QSize size = item->sizeHint();
            size.setWidth(qMin(size.width(), qMax(0, area.width())));
            if (x > area.x() && x + size.width() > area.right() + 1)
            {
                x = area.x();
                y += rowHeight + spacing();
                rowHeight = 0;
            }
            if (apply)
                item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing();
            rowHeight = qMax(rowHeight, size.height());
        }
        return y + rowHeight - rect.y() + margins.bottom();
    }
    QList<QLayoutItem *> m_items;
};
