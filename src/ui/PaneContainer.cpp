#include "PaneContainer.h"

#include <QFrame>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QSizePolicy>

#include <utility>

namespace
{
constexpr int kPaneStateVersion = 1;
constexpr int kDefaultPaneSize = 120;
}

PaneContainer::PaneContainer(const QString &containerId, QWidget *parent)
    : QWidget(parent),
      m_containerId(containerId.trimmed()),
      m_splitter(new QSplitter(Qt::Vertical, this))
{
    setObjectName(QStringLiteral("paneContainer.%1").arg(m_containerId));
    m_splitter->setObjectName(QStringLiteral("%1.splitter").arg(objectName()));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setOpaqueResize(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter);

    connect(m_splitter, &QSplitter::splitterMoved, this,
            [this](int, int) {
                captureSizes();
                emit stateChanged();
            });
}

int PaneContainer::indexOf(const QString &paneId) const
{
    const QString normalized = paneId.trimmed();
    for (int i = 0; i < m_panes.size(); ++i)
    {
        if (m_panes.at(i).id == normalized)
            return i;
    }
    return -1;
}

bool PaneContainer::addPane(const QString &paneId,
                            QWidget *content,
                            bool visible,
                            bool scrollable,
                            bool rememberDefault)
{
    const QString normalized = paneId.trimmed();
    if (normalized.isEmpty() || !content || indexOf(normalized) >= 0)
        return false;

    PaneEntry entry;
    entry.id = normalized;
    entry.content = content;
    entry.scrollable = scrollable;
    entry.visible = visible;
    entry.expanded = true;

    if (scrollable)
    {
        entry.scrollArea = new QScrollArea(m_splitter);
        entry.scrollArea->setObjectName(
            QStringLiteral("%1.%2.scrollArea").arg(objectName(), normalized));
        entry.scrollArea->setWidgetResizable(true);
        entry.scrollArea->setFrameShape(QFrame::NoFrame);
        entry.scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        entry.scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        entry.scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        entry.scrollArea->setWidget(content);
        entry.host = entry.scrollArea;
    }
    else
    {
        entry.host = content;
        content->setParent(m_splitter);
    }

    entry.content->setProperty("workbenchPaneId", normalized);
    entry.host->setProperty("workbenchPaneId", normalized);
    entry.host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    entry.host->setVisible(visible);
    m_splitter->addWidget(entry.host);
    m_panes.append(entry);
    if (rememberDefault)
        m_defaultOrder.append(normalized);
    applyPaneState();
    emit stateChanged();
    return true;
}

void PaneContainer::removeHost(PaneEntry &entry, bool deleteContent)
{
    if (!entry.host)
        return;

    QWidget *content = entry.content;
    QWidget *host = entry.host;

    if (entry.scrollArea)
    {
        entry.scrollArea->takeWidget();
    }
    if (content)
        content->setParent(nullptr);

    if (host != content)
    {
        host->setParent(nullptr);
        delete host;
    }
    if (deleteContent)
        delete content;
    entry.content = nullptr;
    entry.host = nullptr;
    entry.scrollArea = nullptr;
}

QWidget *PaneContainer::takePane(const QString &paneId,
                                 bool preserveDefaultOrder)
{
    const int index = indexOf(paneId);
    if (index < 0)
        return nullptr;

    PaneEntry entry = m_panes.takeAt(index);
    if (!preserveDefaultOrder)
        m_defaultOrder.removeAll(entry.id);
    QWidget *content = entry.content;
    if (entry.scrollArea)
        entry.scrollArea->takeWidget();
    if (content)
    {
        content->setParent(nullptr);
        content->setProperty("workbenchPaneId", QVariant());
    }
    // A non-scrollable pane is its own host. Only delete the wrapper created
    // for scrollable panes; the caller receives the content widget alive.
    if (entry.host && entry.host != content)
    {
        entry.host->setParent(nullptr);
        delete entry.host;
    }
    emit stateChanged();
    return content;
}

bool PaneContainer::removePane(const QString &paneId)
{
    QWidget *content = takePane(paneId);
    if (!content)
        return false;
    delete content;
    return true;
}

void PaneContainer::captureSizes() const
{
    if (!m_splitter)
        return;
    const QList<int> sizes = m_splitter->sizes();
    for (int i = 0; i < m_panes.size() && i < sizes.size(); ++i)
    {
        if (sizes.at(i) > 0)
            const_cast<PaneEntry &>(m_panes[i]).cachedSize = sizes.at(i);
    }
}

void PaneContainer::applyPaneState()
{
    if (!m_splitter)
        return;

    QList<int> sizes;
    sizes.reserve(m_panes.size());
    for (const PaneEntry &entry : m_panes)
    {
        if (entry.host)
        {
            entry.host->setVisible(entry.visible);
            entry.host->setMaximumHeight(
                entry.expanded ? QWIDGETSIZE_MAX : qMax(0, entry.cachedSize));
        }
        sizes.append(entry.visible && entry.expanded
                         ? qMax(kDefaultPaneSize, entry.cachedSize)
                         : 0);
    }
    if (!sizes.isEmpty())
        m_splitter->setSizes(sizes);
}

bool PaneContainer::setPaneVisible(const QString &paneId, bool visible)
{
    const int index = indexOf(paneId);
    if (index < 0 || m_panes[index].visible == visible)
        return index >= 0;

    captureSizes();
    m_panes[index].visible = visible;
    applyPaneState();
    emit stateChanged();
    return true;
}

bool PaneContainer::setPaneExpanded(const QString &paneId, bool expanded)
{
    const int index = indexOf(paneId);
    if (index < 0 || m_panes[index].expanded == expanded)
        return index >= 0;

    captureSizes();
    m_panes[index].expanded = expanded;
    applyPaneState();
    emit stateChanged();
    return true;
}

bool PaneContainer::movePane(const QString &paneId, int targetIndex)
{
    const int sourceIndex = indexOf(paneId);
    if (sourceIndex < 0 || targetIndex < 0 || targetIndex >= m_panes.size())
        return false;
    if (sourceIndex == targetIndex)
        return true;

    captureSizes();
    PaneEntry entry = m_panes.takeAt(sourceIndex);
    m_panes.insert(targetIndex, entry);
    m_splitter->insertWidget(targetIndex, entry.host);
    applyPaneState();
    emit stateChanged();
    return true;
}

bool PaneContainer::setPaneSize(const QString &paneId, int size)
{
    const int index = indexOf(paneId);
    if (index < 0 || size <= 0)
        return false;
    m_panes[index].cachedSize = size;
    applyPaneState();
    emit stateChanged();
    return true;
}

bool PaneContainer::containsPane(const QString &paneId) const
{
    return indexOf(paneId) >= 0;
}

QStringList PaneContainer::paneOrder() const
{
    QStringList result;
    for (const PaneEntry &entry : m_panes)
        result.append(entry.id);
    return result;
}

bool PaneContainer::paneVisible(const QString &paneId) const
{
    const int index = indexOf(paneId);
    return index >= 0 && m_panes.at(index).visible;
}

bool PaneContainer::paneExpanded(const QString &paneId) const
{
    const int index = indexOf(paneId);
    return index >= 0 && m_panes.at(index).expanded;
}

int PaneContainer::paneSize(const QString &paneId) const
{
    captureSizes();
    const int index = indexOf(paneId);
    return index >= 0 ? m_panes.at(index).cachedSize : 0;
}

bool PaneContainer::paneScrollable(const QString &paneId) const
{
    const int index = indexOf(paneId);
    return index >= 0 && m_panes.at(index).scrollable;
}

bool PaneContainer::hasVisiblePanes() const
{
    for (const PaneEntry &entry : m_panes)
    {
        if (entry.visible && entry.expanded)
            return true;
    }
    return false;
}

QScrollArea *PaneContainer::scrollAreaForPane(const QString &paneId) const
{
    const int index = indexOf(paneId);
    return index >= 0 ? m_panes.at(index).scrollArea : nullptr;
}

QByteArray PaneContainer::saveState() const
{
    captureSizes();
    QJsonObject root;
    root.insert(QStringLiteral("version"), kPaneStateVersion);
    root.insert(QStringLiteral("container_id"), m_containerId);

    QJsonArray panes;
    for (int i = 0; i < m_panes.size(); ++i)
    {
        const PaneEntry &entry = m_panes.at(i);
        QJsonObject pane;
        pane.insert(QStringLiteral("id"), entry.id);
        pane.insert(QStringLiteral("order"), i);
        pane.insert(QStringLiteral("visible"), entry.visible);
        pane.insert(QStringLiteral("expanded"), entry.expanded);
        pane.insert(QStringLiteral("size"), entry.cachedSize);
        panes.append(pane);
    }
    root.insert(QStringLiteral("panes"), panes);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool PaneContainer::restoreState(const QByteArray &state)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(state, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != kPaneStateVersion ||
        root.value(QStringLiteral("container_id")).toString() != m_containerId)
        return false;

    QHash<QString, QJsonObject> saved;
    QStringList savedOrder;
    for (const QJsonValue &value : root.value(QStringLiteral("panes")).toArray())
    {
        const QJsonObject pane = value.toObject();
        const QString id = pane.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || saved.contains(id))
            continue;
        saved.insert(id, pane);
        savedOrder.append(id);
    }

    captureSizes();
    QList<PaneEntry> reordered;
    for (const QString &id : savedOrder)
    {
        const int index = indexOf(id);
        if (index < 0)
            continue;
        reordered.append(m_panes.at(index));
    }
    for (const PaneEntry &entry : std::as_const(m_panes))
    {
        bool alreadyAdded = false;
        for (const PaneEntry &candidate : reordered)
        {
            if (candidate.id == entry.id)
            {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded)
            reordered.append(entry);
    }
    m_panes = std::move(reordered);

    for (PaneEntry &entry : m_panes)
    {
        const QJsonObject pane = saved.value(entry.id);
        if (pane.isEmpty())
            continue;
        entry.visible = pane.value(QStringLiteral("visible")).toBool(entry.visible);
        entry.expanded = pane.value(QStringLiteral("expanded")).toBool(entry.expanded);
        entry.cachedSize = qMax(0, pane.value(QStringLiteral("size")).toInt(entry.cachedSize));
    }
    for (int i = 0; i < m_panes.size(); ++i)
        m_splitter->insertWidget(i, m_panes.at(i).host);
    applyPaneState();
    emit stateChanged();
    return true;
}

void PaneContainer::resetState()
{
    captureSizes();
    QList<PaneEntry> reordered;
    for (const QString &id : std::as_const(m_defaultOrder))
    {
        const int index = indexOf(id);
        if (index >= 0)
            reordered.append(m_panes.at(index));
    }
    m_panes = std::move(reordered);
    for (PaneEntry &entry : m_panes)
    {
        entry.visible = true;
        entry.expanded = true;
        entry.cachedSize = 0;
    }
    for (int i = 0; i < m_panes.size(); ++i)
        m_splitter->insertWidget(i, m_panes.at(i).host);
    applyPaneState();
    emit stateChanged();
}

void PaneContainer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    captureSizes();
}
