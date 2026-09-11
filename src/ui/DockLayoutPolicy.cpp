#include "DockLayoutPolicy.h"

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <QList>
#include <QMap>
#include <QSizePolicy>
#include <QSplitter>
#include <QVariant>
#include <QWidget>

namespace
{
constexpr char kCompactToolDockProperty[] = "cceCompactToolDock";
constexpr char kPrimaryWorkspaceDockProperty[] = "ccePrimaryWorkspaceDock";
constexpr char kOriginalVerticalPolicyProperty[] = "cceOriginalVerticalPolicy";
constexpr char kOriginalMaximumHeightProperty[] = "cceOriginalMaximumHeight";
constexpr int kPrimaryWorkspaceMinimumWidth = 420;
constexpr int kPrimaryWorkspaceMinimumHeight = 300;

void setCompactVerticalPolicy(QWidget *widget, bool compact)
{
    if (!widget)
        return;

    QSizePolicy policy = widget->sizePolicy();
    if (compact)
    {
        if (!widget->property(kOriginalVerticalPolicyProperty).isValid())
        {
            widget->setProperty(kOriginalVerticalPolicyProperty,
                                int(policy.verticalPolicy()));
        }
        if (!widget->property(kOriginalMaximumHeightProperty).isValid())
        {
            widget->setProperty(kOriginalMaximumHeightProperty,
                                widget->maximumHeight());
        }
        policy.setVerticalPolicy(QSizePolicy::Maximum);
        policy.setVerticalStretch(0);

        const int originalMaximum =
            widget->property(kOriginalMaximumHeightProperty).toInt();
        const int naturalHeight = qMax(widget->sizeHint().height(),
                                       widget->minimumSizeHint().height());
        if (naturalHeight > 0)
        {
            widget->setMaximumHeight(
                qMax(widget->minimumHeight(),
                     qMin(originalMaximum, naturalHeight)));
        }
    }
    else
    {
        const QVariant originalPolicy = widget->property(kOriginalVerticalPolicyProperty);
        if (originalPolicy.isValid())
        {
            policy.setVerticalPolicy(
                static_cast<QSizePolicy::Policy>(originalPolicy.toInt()));
            widget->setProperty(kOriginalVerticalPolicyProperty, QVariant());
        }

        const QVariant originalMaximum = widget->property(kOriginalMaximumHeightProperty);
        if (originalMaximum.isValid())
        {
            widget->setMaximumHeight(originalMaximum.toInt());
            widget->setProperty(kOriginalMaximumHeightProperty, QVariant());
        }
    }
    widget->setSizePolicy(policy);
}

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

bool containsPrimaryWorkspace(QWidget *widget)
{
    if (!widget)
        return false;

    if (auto *area = qobject_cast<ads::CDockAreaWidget *>(widget))
    {
        const QList<ads::CDockWidget *> docks = area->dockWidgets();
        for (ads::CDockWidget *dock : docks)
        {
            if (dock && dock->property(kPrimaryWorkspaceDockProperty).toBool())
                return true;
        }
        return false;
    }

    if (auto *splitter = qobject_cast<QSplitter *>(widget))
    {
        for (int index = 0; index < splitter->count(); ++index)
        {
            if (containsPrimaryWorkspace(splitter->widget(index)))
                return true;
        }
    }

    return false;
}

void rebalanceVerticalAncestors(QWidget *widget, bool constrainCompactSubtrees)
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
                const bool compactOnly = constrainCompactSubtrees
                                         && isCompactOnlySubtree(child);
                setCompactVerticalPolicy(child, compactOnly);
                splitter->setStretchFactor(index, compactOnly ? 0 : 1);
            }
        }
        current = splitter;
    }
}

void protectWorkspaceAncestors(QWidget *widget)
{
    QWidget *current = widget;
    while (current)
    {
        auto *splitter = qobject_cast<QSplitter *>(current->parentWidget());
        if (!splitter)
            break;

        const int workspaceIndex = splitter->indexOf(current);
        if (workspaceIndex >= 0 && containsPrimaryWorkspace(current))
            splitter->setCollapsible(workspaceIndex, false);
        current = splitter;
    }
}
}

namespace DockLayoutPolicy
{
QSize primaryWorkspaceMinimumSize()
{
    return QSize(kPrimaryWorkspaceMinimumWidth, kPrimaryWorkspaceMinimumHeight);
}

void applyPrimaryWorkspaceDockPolicy(ads::CDockWidget *dock)
{
    if (!dock)
        return;

    dock->setProperty(kPrimaryWorkspaceDockProperty, true);
    dock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    dock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    dock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    dock->setMinimumSizeHintMode(
        ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);

    if (QWidget *content = dock->widget())
    {
        const QSize required = primaryWorkspaceMinimumSize();
        content->setMinimumSize(qMax(content->minimumWidth(), required.width()),
                                qMax(content->minimumHeight(), required.height()));
        QSizePolicy policy = content->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Expanding);
        policy.setVerticalPolicy(QSizePolicy::Expanding);
        policy.setHorizontalStretch(1);
        policy.setVerticalStretch(1);
        content->setSizePolicy(policy);
    }

    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
    {
        // Side panels can be placed next to the editor, but a drop directly
        // above/below (or into a tab with it) would turn the chart canvas into
        // another ordinary splitter leaf.
        area->setAllowedAreas(ads::LeftDockWidgetArea
                              | ads::RightDockWidgetArea);
        protectWorkspaceAncestors(area);
    }
}

void applyCompactToolDockPolicy(ads::CDockWidget *dock)
{
    if (!dock)
        return;

    dock->setProperty(kCompactToolDockProperty, true);
    const bool constrainToNaturalHeight = !dock->isInFloatingContainer();
    setCompactVerticalPolicy(dock, constrainToNaturalHeight);
    if (QWidget *content = dock->widget())
        setCompactVerticalPolicy(content, constrainToNaturalHeight);

    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
    {
        setCompactVerticalPolicy(area, constrainToNaturalHeight
                                           && isCompactOnlySubtree(area));
        rebalanceVerticalAncestors(area, constrainToNaturalHeight);
    }
}

void refreshCompactToolDockPolicies(ads::CDockManager *manager)
{
    if (!manager)
        return;

    const QMap<QString, ads::CDockWidget *> docks = manager->dockWidgetsMap();
    for (ads::CDockWidget *dock : docks)
    {
        if (dock && dock->property(kCompactToolDockProperty).toBool())
            applyCompactToolDockPolicy(dock);
    }
}
}
