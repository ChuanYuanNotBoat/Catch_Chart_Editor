#include "ReviewWorkspace.h"
#include "ui/WorkbenchLayout.h"
#include "ui/PaneContainer.h"

#include <QDialog>
#include <QHeaderView>
#include <QMainWindow>
#include <QTabWidget>
#include <QTreeWidget>

namespace CceReview
{
void ReviewWorkspace::setClassicWorkbench(WorkbenchLayout *workbench)
{
    if (m_classicWorkbench == workbench)
        return;
    buildUi();
    m_panel->hide();
    if (m_classicWorkbench)
    {
        for (const auto &id : {QStringLiteral("layers"), QStringLiteral("comments"), QStringLiteral("review")})
        {
            WorkbenchLayout::Part part;
            if (m_classicWorkbench->panePart(id, &part))
                m_classicWorkbench->takePane(part, id);
        }
        m_panel->setParent(m_window, Qt::Window);
        m_panelTabs->insertTab(0, m_layersPage, tr("Layers & Difficulties"));
        m_panelTabs->insertTab(2, m_commentsPage, tr("Comments"));
        m_panelTabs->setTabText(1, tr("Diff & Conflict"));
        m_panelTabs->setTabText(3, tr("Export Metadata"));
        m_panelTabs->setTabText(4, tr("Event Log"));
        m_panel->resize(1100, 720);
    }
    m_classicWorkbench = workbench;
    if (workbench)
    {
        m_panelTabs->removeTab(m_panelTabs->indexOf(m_layersPage));
        m_panelTabs->removeTab(m_panelTabs->indexOf(m_commentsPage));
        m_panelTabs->setTabText(0, tr("Changes"));
        m_panelTabs->setTabText(1, tr("Export"));
        m_panelTabs->setTabText(2, tr("History"));
        m_panel->setParent(workbench, Qt::Widget);
        workbench->addPane(WorkbenchLayout::Part::PrimarySidebar, QStringLiteral("layers"), m_layersPage, false);
        workbench->addPane(WorkbenchLayout::Part::AuxiliarySidebar, QStringLiteral("comments"), m_commentsPage, false);
        workbench->addPane(WorkbenchLayout::Part::AuxiliarySidebar, QStringLiteral("review"), m_panel, false);
        workbench->setExclusivePaneGroup({QStringLiteral("navigation"), QStringLiteral("layers")});
        workbench->setExclusivePaneGroup({QStringLiteral("note"), QStringLiteral("bpm"), QStringLiteral("meta"),
                                         QStringLiteral("comments"), QStringLiteral("review")});
    }
    const bool compact = workbench != nullptr;
    if (auto *projectTools = m_layersPage->findChild<QWidget *>("standaloneProjectTools"))
        projectTools->setVisible(!compact);
    m_layersTree->header()->setStretchLastSection(false);
    m_layersTree->setColumnHidden(4, compact);
    m_layersTree->setColumnHidden(5, compact);
    for (int column = 1; column <= 3; ++column)
        m_layersTree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_commentsTree->header()->setStretchLastSection(false);
    m_commentsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 2; column < 5; ++column)
        m_commentsTree->setColumnHidden(column, compact);
    m_changesTree->header()->setStretchLastSection(false);
    m_changesTree->header()->setSectionResizeMode(0, compact ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    for (int column : {2, 3, 5})
        m_changesTree->setColumnHidden(column, compact);
    for (auto *page : {m_layersPage, m_commentsPage, static_cast<QWidget *>(m_panel.data())})
        for (auto *detail : page->findChildren<QWidget *>("classicSelectionDetails"))
            detail->setVisible(compact);
    refreshUi();
}
} // namespace CceReview
