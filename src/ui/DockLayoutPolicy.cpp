#include "DockLayoutPolicy.h"

#include <DockAreaWidget.h>
#include <DockWidget.h>
#include <QList>
#include <QSizePolicy>
#include <QSplitter>
#include <QWidget>

namespace
{
constexpr char kCompactToolDockProperty[] = "cceCompactToolDock";

bool isCompactOnlySubtree(QWidget *widget)
{
    if (auto *area = qobject_cast<ads::CDockAreaWidget *>(widget))
    {
        const QList<ads::CDockWidget *> docks = area->dockWidgets();
        if (docks.isEmpty())
            return false;
        for (ads::CDockWidget *dock : docks)
        {
            if (!dock || !dock->property(kCompactToolDockProperty).toBool())
                return false;
        }
        return true;
    }

    if (auto *splitter = qobject_cast<QSplitter *>(widget))
    {
        bool hasChild = false;
        for (int index = 0; index < splitter->count(); ++index)
        {
            QWidget *child = splitter->widget(index);
            if (!child)
                continue;
            hasChild = true;
            if (!isCompactOnlySubtree(child))
                return false;
        }
        return hasChild;
    }

    return false;
}

void rebalanceVerticalAncestors(QWidget *widget)
{
    QWidget *current = widget;
    while (current)
    {
        auto *splitter = qobject_cast<QSplitter *>(current->parentWidget());
        if (!splitter)
            break;

        if (splitter->orientation() == Qt::Vertical)
        {
            for (int index = 0; index < splitter->count(); ++index)
            {
                QWidget *child = splitter->widget(index);
                splitter->setStretchFactor(
                    index, isCompactOnlySubtree(child) ? 0 : 1);
            }
        }
        current = splitter;
    }
}
}

namespace DockLayoutPolicy
{
void applyCompactToolDockPolicy(ads::CDockWidget *dock)
{
    if (!dock)
        return;

    dock->setProperty(kCompactToolDockProperty, true);
    if (QWidget *content = dock->widget())
    {
        QSizePolicy policy = content->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Maximum);
        policy.setVerticalStretch(0);
        content->setSizePolicy(policy);
    }

    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
        rebalanceVerticalAncestors(area);
}
}
