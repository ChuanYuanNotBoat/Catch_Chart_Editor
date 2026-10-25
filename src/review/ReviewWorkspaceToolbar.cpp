#include "ReviewWorkspace.h"
#include "controller/ChartController.h"
#include "controller/CommandRouter.h"
#include "controller/SelectionController.h"
#include "ui/WorkbenchLayout.h"
#include <QAction>
#include <QDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolBar>
#include <QTreeWidget>

namespace CceReview
{
namespace
{
class ToolPageTabs final : public QTabBar
{
public:
    using QTabBar::QTabBar;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const int clicked = tabAt(event->position().toPoint());
        const bool alreadySelected = clicked >= 0 && clicked == currentIndex();
        QTabBar::mousePressEvent(event);
        // Clicking the active page also reopens a panel the user has closed.
        if (alreadySelected && event->button() == Qt::LeftButton)
            emit currentChanged(clicked);
    }
};
} // namespace

void ReviewWorkspace::installToolbar(QToolBar *toolbar, QAction *review)
{
    if (m_editorToolbarActions.isEmpty())
        for (auto *action : toolbar->actions())
            if (action->property("toolPage").toBool())
                m_editorToolbarActions.append(action);
    for (const auto &action : m_toolbarOwnedActions)
        if (action)
            delete action;
    m_toolbarOwnedActions.clear();
    for (auto *action : toolbar->actions())
        toolbar->removeAction(action);
    auto *tabs = new ToolPageTabs(toolbar);
    tabs->setObjectName("reviewToolbarTabs");
    tabs->setExpanding(false);
    tabs->setUsesScrollButtons(false);
    for (const auto &action : m_editorToolbarActions)
    {
        const int index = tabs->addTab(action->text());
        connect(action, &QAction::changed, tabs, [tabs, action, index] {
            if (action)
                tabs->setTabText(index, action->text());
        });
        connect(action, &QAction::triggered, tabs, [tabs, index] {
            const QSignalBlocker block(tabs);
            tabs->setCurrentIndex(index);
        });
    }
    const int reviewIndex = tabs->addTab(QString(tr("Review & Layers (Experimental)")).replace('&', "&&"));
    auto *tabAction = toolbar->addWidget(tabs);
    m_toolbarOwnedActions.append(tabAction);
    connect(tabs, &QTabBar::currentChanged, this, [this, review, reviewIndex, tabs](int index) {
        if (index == reviewIndex)
            review->trigger();
        else if (index >= 0 && index < m_editorToolbarActions.size())
        {
            if (m_panel && !m_classicWorkbench)
                m_panel->hide();
            auto *action = m_editorToolbarActions[index].data();
            if (action && (!action->isCheckable() || !action->isChecked()))
                action->trigger();
            else if (action && m_classicWorkbench)
                m_classicWorkbench->setPaneVisible(QStringLiteral("note"), true);
        }
        const QSignalBlocker block(tabs);
        tabs->setCurrentIndex(index);
    });
    connect(review, &QAction::triggered, tabs, [tabs, reviewIndex] {
        const QSignalBlocker block(tabs);
        tabs->setCurrentIndex(reviewIndex);
    });
    // One page selector is shared by classic and floating layouts.
}

void ReviewWorkspace::installPanelCommands(CommandRouter *router, QMenu *menu)
{
    const auto add = [&](const QString &text, const QString &id, auto callback) {
        auto *action = new QAction(text, menu);
        action->setObjectName(id);
        connect(action, &QAction::triggered, this, callback);
        router->registerAction(action, id, {}, CommandRouter::Scope::Window);
        return action;
    };
    add(tr("Layers & Difficulties"), "review.layers", [this] {
        showPanelPage(0);
    });
    add(tr("New Layer"), "review.newLayer", [this] {
        changeLayer(0);
    });
    const auto toggle = [this](bool locked) {
        auto after = m_bundle;
        auto &bindings = after.project.difficulties[after.project.activeDifficultyId].bindings;
        for (auto &binding : bindings)
            if (binding.layerId == m_activeLayerId)
            {
                if (locked)
                    binding.locked = !binding.locked;
                else
                    binding.visible = !binding.visible;
            }
        QString error;
        if (!applyBundle(tr("Update layer binding"), after, &error))
        {
            report(error);
            refreshToolbar();
        }
    };
    m_layerVisibleAction = add(tr("Visible"), "review.layerVisible", [toggle] {
        toggle(false);
    });
    m_layerVisibleAction->setCheckable(true);
    m_layerLockedAction = add(tr("Locked"), "review.layerLocked", [toggle] {
        toggle(true);
    });
    m_layerLockedAction->setCheckable(true);
    add(tr("Accept"), "review.accept", [this] {
        if (selectToolbarChanges())
            setDecision("Accepted");
    });
    add(tr("Reject"), "review.reject", [this] {
        if (selectToolbarChanges())
            setDecision("Rejected");
    });
    add(tr("Apply Accepted"), "review.applyAccepted", [this] {
        const auto ids = acceptedChangeIds();
        QString error;
        if (!applyChanges(ids, &error))
            report(error);
    });
    refreshToolbar();
}

void ReviewWorkspace::refreshToolbar()
{
    const auto difficulty = m_bundle.project.difficulties.value(m_bundle.project.activeDifficultyId);
    for (const auto &binding : difficulty.bindings)
        if (binding.layerId == m_activeLayerId)
        {
            if (m_layerVisibleAction)
            {
                QSignalBlocker blocked(m_layerVisibleAction);
                m_layerVisibleAction->setChecked(binding.visible);
                m_layerVisibleAction->setEnabled(!m_controller->isReadOnly());
            }
            if (m_layerLockedAction)
            {
                QSignalBlocker blocked(m_layerLockedAction);
                m_layerLockedAction->setChecked(binding.locked);
                m_layerLockedAction->setEnabled(!m_controller->isReadOnly());
            }
        }
}

bool ReviewWorkspace::selectToolbarChanges()
{
    buildUi();
    if (!m_changesTree->selectedItems().isEmpty())
        return true;
    QSet<QByteArray> selected;
    const auto &chart = m_controller->isReadOnly() ? m_presentationChart : *m_controller->chart();
    for (int index : m_selection->selectedIndices())
        if (index >= 0 && index < chart.notes().size())
            selected.insert(canonicalHash(CceDocument::encodeNote(portableNote(chart.notes()[index]), true)));
    QSet<QString> ids;
    for (const auto &change : m_changes)
        if (change.domain == "Note"
            && (selected.contains(canonicalHash(change.before.toObject()))
                || selected.contains(canonicalHash(change.after.toObject()))))
            ids.insert(change.changeId);
    for (int i = 0; i < m_changesTree->topLevelItemCount(); ++i)
    {
        auto *item = m_changesTree->topLevelItem(i);
        if (ids.contains(item->data(0, Qt::UserRole).toString()))
            item->setSelected(true);
    }
    if (!ids.isEmpty())
        return true;
    showPanelPage(1);
    report(tr("Select a change to review."));
    return false;
}
} // namespace CceReview
