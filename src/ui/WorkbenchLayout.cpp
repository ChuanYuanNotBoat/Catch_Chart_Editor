#include "WorkbenchLayout.h"

#include "PaneContainer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QSplitter>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
constexpr int kWorkbenchStateVersion = 1;

QJsonObject stateObject(const QByteArray &state)
{
    const QJsonDocument document = QJsonDocument::fromJson(state);
    return document.isObject() ? document.object() : QJsonObject();
}

QByteArray stateBytes(const QJsonObject &state)
{
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}
}

WorkbenchLayout::WorkbenchLayout(QWidget *parent)
    : QWidget(parent),
      m_editorHost(new QWidget(this)),
      m_primarySidebar(new PaneContainer(QStringLiteral("primarySidebar"), this)),
      m_auxiliarySidebar(new PaneContainer(QStringLiteral("auxiliarySidebar"), this)),
      m_bottomPanel(new PaneContainer(QStringLiteral("bottomPanel"), this)),
      m_horizontalSplitter(new QSplitter(Qt::Horizontal, this)),
      m_verticalSplitter(new QSplitter(Qt::Vertical, this))
{
    setObjectName(QStringLiteral("workbenchLayout"));
    m_editorHost->setObjectName(QStringLiteral("workbench.editor"));
    m_primarySidebar->setObjectName(QStringLiteral("workbench.primarySidebar"));
    m_auxiliarySidebar->setObjectName(QStringLiteral("workbench.auxiliarySidebar"));
    m_bottomPanel->setObjectName(QStringLiteral("workbench.bottomPanel"));
    m_horizontalSplitter->setObjectName(QStringLiteral("workbench.horizontalSplitter"));
    m_verticalSplitter->setObjectName(QStringLiteral("workbench.verticalSplitter"));

    auto *editorLayout = new QVBoxLayout(m_editorHost);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    m_horizontalSplitter->setChildrenCollapsible(false);
    m_horizontalSplitter->addWidget(m_primarySidebar);
    m_horizontalSplitter->addWidget(m_editorHost);
    m_horizontalSplitter->addWidget(m_auxiliarySidebar);
    m_horizontalSplitter->setStretchFactor(0, 0);
    m_horizontalSplitter->setStretchFactor(1, 1);
    m_horizontalSplitter->setStretchFactor(2, 0);

    m_bottomPanel->setMinimumHeight(0);
    m_verticalSplitter->setChildrenCollapsible(false);
    m_verticalSplitter->addWidget(m_horizontalSplitter);
    m_verticalSplitter->addWidget(m_bottomPanel);
    m_verticalSplitter->setStretchFactor(0, 1);
    m_verticalSplitter->setStretchFactor(1, 0);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_verticalSplitter);
}

PaneContainer *WorkbenchLayout::paneContainer(Part part) const
{
    switch (part)
    {
    case Part::PrimarySidebar:
        return m_primarySidebar;
    case Part::AuxiliarySidebar:
        return m_auxiliarySidebar;
    case Part::BottomPanel:
        return m_bottomPanel;
    case Part::Editor:
        return nullptr;
    }
    return nullptr;
}

PaneContainer *WorkbenchLayout::paneContainerForPane(const QString &paneId,
                                                     Part *part) const
{
    const QString normalized = paneId.trimmed();
    const QList<QPair<Part, PaneContainer *>> containers = {
        {Part::PrimarySidebar, m_primarySidebar},
        {Part::AuxiliarySidebar, m_auxiliarySidebar},
        {Part::BottomPanel, m_bottomPanel}};
    for (const auto &entry : containers)
    {
        if (entry.second && entry.second->containsPane(normalized))
        {
            if (part)
                *part = entry.first;
            return entry.second;
        }
    }
    if (part)
        *part = Part::Editor;
    return nullptr;
}

bool WorkbenchLayout::setEditorWidget(QWidget *widget)
{
    if (!widget || widget == m_editorWidget)
        return widget == m_editorWidget;

    if (m_editorWidget)
    {
        m_editorWidget->setParent(nullptr);
        m_editorWidget = nullptr;
    }
    m_editorWidget = widget;
    m_editorWidget->setProperty("workbenchPartId", QStringLiteral("editor"));
    m_editorWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_editorHost->layout()->addWidget(m_editorWidget);
    return true;
}

QWidget *WorkbenchLayout::takeEditorWidget()
{
    if (!m_editorWidget)
        return nullptr;

    QWidget *widget = m_editorWidget;
    m_editorWidget = nullptr;
    widget->setParent(nullptr);
    return widget;
}

bool WorkbenchLayout::addPane(Part part,
                              const QString &paneId,
                              QWidget *content,
                              bool visible,
                              bool scrollable)
{
    PaneContainer *container = paneContainer(part);
    const QString normalized = paneId.trimmed();
    if (!container || normalized.isEmpty() || paneContainerForPane(normalized))
        return false;
    if (!container->addPane(normalized, content, visible, scrollable))
        return false;
    m_defaultPaneParts.insert(normalized, part);
    return true;
}

QWidget *WorkbenchLayout::takePane(Part part, const QString &paneId)
{
    PaneContainer *container = paneContainer(part);
    return container ? container->takePane(paneId) : nullptr;
}

bool WorkbenchLayout::panePart(const QString &paneId, Part *part) const
{
    return paneContainerForPane(paneId, part) != nullptr;
}

bool WorkbenchLayout::movePane(const QString &paneId, Part targetPart, int targetIndex)
{
    PaneContainer *target = paneContainer(targetPart);
    Part sourcePart = Part::Editor;
    PaneContainer *source = paneContainerForPane(paneId, &sourcePart);
    if (!source || !target || targetPart == Part::Editor)
        return false;

    const QString normalized = paneId.trimmed();
    if (source == target)
    {
        if (targetIndex < 0)
            return true;
        return target->movePane(normalized, targetIndex);
    }

    const bool visible = source->paneVisible(normalized);
    const bool expanded = source->paneExpanded(normalized);
    const int size = source->paneSize(normalized);
    const bool scrollable = source->paneScrollable(normalized);
    QWidget *content = source->takePane(normalized, true);
    if (!content)
        return false;

    if (!target->addPane(normalized, content, visible, scrollable, false))
    {
        source->addPane(normalized, content, visible, scrollable, false);
        source->setPaneExpanded(normalized, expanded);
        if (size > 0)
            source->setPaneSize(normalized, size);
        return false;
    }

    target->setPaneExpanded(normalized, expanded);
    if (size > 0)
        target->setPaneSize(normalized, size);
    if (targetIndex >= 0)
    {
        const int clampedIndex = qMin(targetIndex, target->paneOrder().size() - 1);
        target->movePane(normalized, clampedIndex);
    }
    return true;
}

bool WorkbenchLayout::resetPaneLocation(const QString &paneId)
{
    const QString normalized = paneId.trimmed();
    const auto it = m_defaultPaneParts.constFind(normalized);
    return it != m_defaultPaneParts.constEnd() && movePane(normalized, it.value());
}

bool WorkbenchLayout::setPaneVisible(const QString &paneId, bool visible)
{
    PaneContainer *container = paneContainerForPane(paneId);
    return container && container->setPaneVisible(paneId, visible);
}

QByteArray WorkbenchLayout::saveState() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kWorkbenchStateVersion);

    QJsonArray horizontalSizes;
    for (const int size : m_horizontalSplitter->sizes())
        horizontalSizes.append(size);
    root.insert(QStringLiteral("horizontal_sizes"), horizontalSizes);

    QJsonArray verticalSizes;
    for (const int size : m_verticalSplitter->sizes())
        verticalSizes.append(size);
    root.insert(QStringLiteral("vertical_sizes"), verticalSizes);
    root.insert(QStringLiteral("primary_sidebar"), stateObject(m_primarySidebar->saveState()));
    root.insert(QStringLiteral("auxiliary_sidebar"), stateObject(m_auxiliarySidebar->saveState()));
    root.insert(QStringLiteral("bottom_panel"), stateObject(m_bottomPanel->saveState()));
    return stateBytes(root);
}

bool WorkbenchLayout::restoreState(const QByteArray &state)
{
    const QJsonObject root = stateObject(state);
    if (root.value(QStringLiteral("version")).toInt() != kWorkbenchStateVersion)
        return false;

    const QList<QPair<Part, QString>> savedParts = {
        {Part::PrimarySidebar, QStringLiteral("primary_sidebar")},
        {Part::AuxiliarySidebar, QStringLiteral("auxiliary_sidebar")},
        {Part::BottomPanel, QStringLiteral("bottom_panel")}};
    QSet<QString> seen;
    for (const auto &savedPart : savedParts)
    {
        const QJsonObject partState = root.value(savedPart.second).toObject();
        for (const QJsonValue &value : partState.value(QStringLiteral("panes")).toArray())
        {
            const QString paneId = value.toObject().value(QStringLiteral("id")).toString();
            if (paneId.isEmpty() || seen.contains(paneId))
                continue;
            seen.insert(paneId);
            Part currentPart = Part::Editor;
            if (panePart(paneId, &currentPart) && currentPart != savedPart.first)
                movePane(paneId, savedPart.first);
        }
    }

    const bool primaryOk = m_primarySidebar->restoreState(
        stateBytes(root.value(QStringLiteral("primary_sidebar")).toObject()));
    const bool auxiliaryOk = m_auxiliarySidebar->restoreState(
        stateBytes(root.value(QStringLiteral("auxiliary_sidebar")).toObject()));
    const bool bottomOk = m_bottomPanel->restoreState(
        stateBytes(root.value(QStringLiteral("bottom_panel")).toObject()));

    const QJsonArray horizontalSizes = root.value(QStringLiteral("horizontal_sizes")).toArray();
    if (horizontalSizes.size() == m_horizontalSplitter->count())
    {
        QList<int> sizes;
        for (const QJsonValue &value : horizontalSizes)
            sizes.append(qMax(0, value.toInt()));
        m_horizontalSplitter->setSizes(sizes);
    }

    const QJsonArray verticalSizes = root.value(QStringLiteral("vertical_sizes")).toArray();
    if (verticalSizes.size() == m_verticalSplitter->count())
    {
        QList<int> sizes;
        for (const QJsonValue &value : verticalSizes)
            sizes.append(qMax(0, value.toInt()));
        m_verticalSplitter->setSizes(sizes);
    }
    return primaryOk && auxiliaryOk && bottomOk;
}

void WorkbenchLayout::resetState()
{
    for (auto it = m_defaultPaneParts.constBegin(); it != m_defaultPaneParts.constEnd(); ++it)
        resetPaneLocation(it.key());
    m_primarySidebar->resetState();
    m_auxiliarySidebar->resetState();
    m_bottomPanel->resetState();
    m_horizontalSplitter->setSizes({220, 700, 260});
    m_verticalSplitter->setSizes({600, 160});
}
