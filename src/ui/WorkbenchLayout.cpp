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
constexpr int kWorkbenchStateVersion = 2;
constexpr int kDefaultEditorWidth = 700;
constexpr int kDefaultEditorHeight = 600;

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
      m_previewArea(new PaneContainer(QStringLiteral("previewArea"), this)),
      m_auxiliarySidebar(new PaneContainer(QStringLiteral("auxiliarySidebar"), this)),
      m_bottomPanel(new PaneContainer(QStringLiteral("bottomPanel"), this)),
      m_horizontalSplitter(new QSplitter(Qt::Horizontal, this)),
      m_verticalSplitter(new QSplitter(Qt::Vertical, this))
{
    setObjectName(QStringLiteral("workbenchLayout"));
    m_editorHost->setObjectName(QStringLiteral("workbench.editor"));
    m_primarySidebar->setObjectName(QStringLiteral("workbench.primarySidebar"));
    m_previewArea->setObjectName(QStringLiteral("workbench.previewArea"));
    m_auxiliarySidebar->setObjectName(QStringLiteral("workbench.auxiliarySidebar"));
    m_bottomPanel->setObjectName(QStringLiteral("workbench.bottomPanel"));
    m_horizontalSplitter->setObjectName(QStringLiteral("workbench.horizontalSplitter"));
    m_verticalSplitter->setObjectName(QStringLiteral("workbench.verticalSplitter"));

    auto *editorLayout = new QVBoxLayout(m_editorHost);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    m_horizontalSplitter->setChildrenCollapsible(false);
    m_horizontalSplitter->addWidget(m_primarySidebar);
    m_horizontalSplitter->addWidget(m_previewArea);
    m_horizontalSplitter->addWidget(m_editorHost);
    m_horizontalSplitter->addWidget(m_auxiliarySidebar);
    m_horizontalSplitter->setStretchFactor(0, 0);
    m_horizontalSplitter->setStretchFactor(1, 0);
    m_horizontalSplitter->setStretchFactor(2, 1);
    m_horizontalSplitter->setStretchFactor(3, 0);

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

    connect(m_primarySidebar, &PaneContainer::stateChanged,
            this, &WorkbenchLayout::updatePartVisibility);
    connect(m_previewArea, &PaneContainer::stateChanged,
            this, &WorkbenchLayout::updatePartVisibility);
    connect(m_auxiliarySidebar, &PaneContainer::stateChanged,
            this, &WorkbenchLayout::updatePartVisibility);
    connect(m_bottomPanel, &PaneContainer::stateChanged,
            this, &WorkbenchLayout::updatePartVisibility);
    connect(m_horizontalSplitter, &QSplitter::splitterMoved,
            this, [this](int, int) { capturePartSizes(); });
    connect(m_verticalSplitter, &QSplitter::splitterMoved,
            this, [this](int, int) { capturePartSizes(); });
    updatePartVisibility();
}

void WorkbenchLayout::capturePartSizes() const
{
    const QList<int> horizontalSizes = m_horizontalSplitter->sizes();
    if (horizontalSizes.size() == 4)
    {
        if (m_primaryPartVisible && horizontalSizes.at(0) > 0)
            m_primarySidebarSize = horizontalSizes.at(0);
        if (m_previewPartVisible && horizontalSizes.at(1) > 0)
            m_previewAreaSize = horizontalSizes.at(1);
        if (m_auxiliaryPartVisible && horizontalSizes.at(3) > 0)
            m_auxiliarySidebarSize = horizontalSizes.at(3);
    }

    const QList<int> verticalSizes = m_verticalSplitter->sizes();
    if (verticalSizes.size() == 2 && m_bottomPartVisible && verticalSizes.at(1) > 0)
        m_bottomPanelSize = verticalSizes.at(1);
}

void WorkbenchLayout::updatePartVisibility()
{
    capturePartSizes();

    QList<int> horizontalSizes = m_horizontalSplitter->sizes();
    QList<int> verticalSizes = m_verticalSplitter->sizes();
    const bool showPrimary = m_primarySidebar->hasVisiblePanes();
    const bool showPreview = m_previewArea->hasVisiblePanes();
    const bool showAuxiliary = m_auxiliarySidebar->hasVisiblePanes();
    const bool showBottom = m_bottomPanel->hasVisiblePanes();
    const bool restorePrimarySize = showPrimary && !m_primaryPartVisible;
    const bool restorePreviewSize = showPreview && !m_previewPartVisible;
    const bool restoreAuxiliarySize = showAuxiliary && !m_auxiliaryPartVisible;
    const bool restoreBottomSize = showBottom && !m_bottomPartVisible;

    m_primarySidebar->setVisible(showPrimary);
    m_previewArea->setVisible(showPreview);
    m_auxiliarySidebar->setVisible(showAuxiliary);
    m_bottomPanel->setVisible(showBottom);
    m_primaryPartVisible = showPrimary;
    m_previewPartVisible = showPreview;
    m_auxiliaryPartVisible = showAuxiliary;
    m_bottomPartVisible = showBottom;

    if (horizontalSizes.size() == 4)
    {
        const int currentTotal = horizontalSizes.at(0)
            + horizontalSizes.at(1) + horizontalSizes.at(2)
            + horizontalSizes.at(3);
        const int primarySize = showPrimary
            ? (restorePrimarySize || horizontalSizes.at(0) <= 0
                   ? m_primarySidebarSize
                   : horizontalSizes.at(0))
            : 0;
        const int previewSize = showPreview
            ? (restorePreviewSize || horizontalSizes.at(1) <= 0
                   ? m_previewAreaSize
                   : horizontalSizes.at(1))
            : 0;
        const int auxiliarySize = showAuxiliary
            ? (restoreAuxiliarySize || horizontalSizes.at(3) <= 0
                   ? m_auxiliarySidebarSize
                   : horizontalSizes.at(3))
            : 0;
        const int total = currentTotal > 0
            ? currentTotal
            : primarySize + previewSize + kDefaultEditorWidth + auxiliarySize;
        const int editorSize = qMax(1, total - primarySize - previewSize - auxiliarySize);
        m_horizontalSplitter->setSizes({primarySize, previewSize, editorSize, auxiliarySize});
    }

    if (verticalSizes.size() == 2)
    {
        const int currentTotal = verticalSizes.at(0) + verticalSizes.at(1);
        const int bottomSize = showBottom
            ? (restoreBottomSize || verticalSizes.at(1) <= 0
                   ? m_bottomPanelSize
                   : verticalSizes.at(1))
            : 0;
        const int total = currentTotal > 0
            ? currentTotal
            : kDefaultEditorHeight + bottomSize;
        const int editorSize = qMax(1, total - bottomSize);
        m_verticalSplitter->setSizes({editorSize, bottomSize});
    }
}

PaneContainer *WorkbenchLayout::paneContainer(Part part) const
{
    switch (part)
    {
    case Part::PrimarySidebar:
        return m_primarySidebar;
    case Part::PreviewArea:
        return m_previewArea;
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
        {Part::PreviewArea, m_previewArea},
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
    capturePartSizes();
    QJsonObject root;
    root.insert(QStringLiteral("version"), kWorkbenchStateVersion);
    root.insert(QStringLiteral("primary_sidebar_size"), m_primarySidebarSize);
    root.insert(QStringLiteral("preview_area_size"), m_previewAreaSize);
    root.insert(QStringLiteral("auxiliary_sidebar_size"), m_auxiliarySidebarSize);
    root.insert(QStringLiteral("bottom_panel_size"), m_bottomPanelSize);

    QJsonArray horizontalSizes;
    for (const int size : m_horizontalSplitter->sizes())
        horizontalSizes.append(size);
    root.insert(QStringLiteral("horizontal_sizes"), horizontalSizes);

    QJsonArray verticalSizes;
    for (const int size : m_verticalSplitter->sizes())
        verticalSizes.append(size);
    root.insert(QStringLiteral("vertical_sizes"), verticalSizes);
    root.insert(QStringLiteral("primary_sidebar"), stateObject(m_primarySidebar->saveState()));
    root.insert(QStringLiteral("preview_area"), stateObject(m_previewArea->saveState()));
    root.insert(QStringLiteral("auxiliary_sidebar"), stateObject(m_auxiliarySidebar->saveState()));
    root.insert(QStringLiteral("bottom_panel"), stateObject(m_bottomPanel->saveState()));
    return stateBytes(root);
}

bool WorkbenchLayout::restoreState(const QByteArray &state)
{
    const QJsonObject root = stateObject(state);
    if (root.value(QStringLiteral("version")).toInt() != kWorkbenchStateVersion)
        return false;

    m_primarySidebarSize = qMax(1, root.value(QStringLiteral("primary_sidebar_size"))
                                      .toInt(m_primarySidebarSize));
    m_previewAreaSize = qMax(1, root.value(QStringLiteral("preview_area_size"))
                                   .toInt(m_previewAreaSize));
    m_auxiliarySidebarSize = qMax(1, root.value(QStringLiteral("auxiliary_sidebar_size"))
                                        .toInt(m_auxiliarySidebarSize));
    m_bottomPanelSize = qMax(1, root.value(QStringLiteral("bottom_panel_size"))
                                   .toInt(m_bottomPanelSize));

    const QList<QPair<Part, QString>> savedParts = {
        {Part::PrimarySidebar, QStringLiteral("primary_sidebar")},
        {Part::PreviewArea, QStringLiteral("preview_area")},
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
    const bool previewOk = m_previewArea->restoreState(
        stateBytes(root.value(QStringLiteral("preview_area")).toObject()));
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
    updatePartVisibility();
    return primaryOk && previewOk && auxiliaryOk && bottomOk;
}

void WorkbenchLayout::resetState()
{
    for (auto it = m_defaultPaneParts.constBegin(); it != m_defaultPaneParts.constEnd(); ++it)
        resetPaneLocation(it.key());
    m_primarySidebar->resetState();
    m_previewArea->resetState();
    m_auxiliarySidebar->resetState();
    m_bottomPanel->resetState();
    m_primarySidebarSize = 150;
    m_previewAreaSize = 200;
    m_auxiliarySidebarSize = 300;
    m_bottomPanelSize = 160;
    updatePartVisibility();
    m_horizontalSplitter->setSizes({m_primaryPartVisible ? m_primarySidebarSize : 0,
                                    m_previewPartVisible ? m_previewAreaSize : 0,
                                    kDefaultEditorWidth,
                                    m_auxiliaryPartVisible ? m_auxiliarySidebarSize : 0});
    m_verticalSplitter->setSizes({kDefaultEditorHeight,
                                  m_bottomPartVisible ? m_bottomPanelSize : 0});
}
