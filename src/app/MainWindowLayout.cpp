#include "MainWindow.h"
#include "MainWindowPrivate.h"

#include "ui/NoteEditPanel.h"
#include "ui/BPMTimePanel.h"
#include "ui/LeftPanel.h"
#include "ui/MetaEditPanel.h"
#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "ui/DockLayoutPolicy.h"
#include "ui/PaneContainer.h"
#include "ui/WorkbenchLayout.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <DockManager.h>
#include <DockAreaWidget.h>
#include <DockWidget.h>
#include <QAbstractScrollArea>
#include <QAction>
#include <QFrame>
#include <QList>
#include <QMenu>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
constexpr int kDockLayoutVersion = 3;

QList<int> classicDefaultSplitterSizes()
{
    return {150, 200, 700, 300};
}
}

void MainWindow::ensureWorkspaceDockVisible()
{
    if (!d->workspaceDock)
        return;

    // The chart workspace is the editor's fixed primary interaction surface.
    // Closing it must never leave the window in a state where chart editing can
    // no longer be recovered.
    DockLayoutPolicy::applyPrimaryWorkspaceDockPolicy(d->workspaceDock);
    if ((!d->floatingToolWindowsInitialized || d->floatingToolWindowsEnabled)
        && d->workspaceDock->isClosed())
    {
        d->workspaceDock->toggleView(true);
    }
}

void MainWindow::saveClassicLayoutState()
{
    Settings &settings = Settings::instance();
    if (d->workbenchLayout)
    {
        settings.setClassicLayoutState(d->workbenchLayout->saveState());
    }
    else if (d->legacySplitter)
    {
        settings.setClassicLayoutState(d->legacySplitter->saveState());
    }

    QString panelId = QStringLiteral("note");
    if (d->currentRightPanel == d->bpmPanel)
        panelId = QStringLiteral("bpm");
    else if (d->currentRightPanel == d->metaPanel)
        panelId = QStringLiteral("meta");
    settings.setClassicRightPanelId(panelId);

    if (d->notePanel)
        settings.setClassicPluginToolsVisible(d->notePanel->embeddedPluginToolsVisible());
}

void MainWindow::restoreClassicLayoutState()
{
    Settings &settings = Settings::instance();
    const QString panelId = settings.classicRightPanelId();
    QWidget *panel = d->notePanel;
    if (panelId == QLatin1String("bpm") && d->bpmPanel)
        panel = d->bpmPanel;
    else if (panelId == QLatin1String("meta") && d->metaPanel)
        panel = d->metaPanel;
    d->currentRightPanel = panel;

    if (d->workbenchLayout)
    {
        const QByteArray state = settings.classicLayoutState();
        const bool restored = !state.isEmpty() && d->workbenchLayout->restoreState(state);
        if (!restored)
        {
            d->workbenchLayout->resetState();
            d->workbenchLayout->primarySidebar()->setPaneVisible(
                QStringLiteral("navigation"), d->leftPanelWasVisible);
            d->workbenchLayout->primarySidebar()->setPaneVisible(
                QStringLiteral("preview"), d->previewWasVisible);
        }
        d->workbenchLayout->auxiliarySidebar()->setPaneVisible(
            QStringLiteral("note"), panel == d->notePanel);
        d->workbenchLayout->auxiliarySidebar()->setPaneVisible(
            QStringLiteral("bpm"), panel == d->bpmPanel);
        d->workbenchLayout->auxiliarySidebar()->setPaneVisible(
            QStringLiteral("meta"), panel == d->metaPanel);
    }
    else
    {
        if (d->notePanel)
            d->notePanel->setVisible(panel == d->notePanel);
        if (d->bpmPanel)
            d->bpmPanel->setVisible(panel == d->bpmPanel);
        if (d->metaPanel)
            d->metaPanel->setVisible(panel == d->metaPanel);
    }
    if (d->notePanel)
        d->notePanel->setEmbeddedPluginToolsVisible(settings.classicPluginToolsVisible());

    if (d->workbenchLayout)
        return;

    if (!d->legacySplitter)
        return;
    const QByteArray splitterState = settings.classicLayoutState();
    if (splitterState.isEmpty() || !d->legacySplitter->restoreState(splitterState))
        d->legacySplitter->setSizes(classicDefaultSplitterSizes());
}

void MainWindow::setFloatingToolWindowsEnabled(bool enabled)
{
    const bool wasInitialized = d->floatingToolWindowsInitialized;
    const bool wasEnabled = d->floatingToolWindowsEnabled;

    // Save the mode being left before any widgets are detached. Each layout is
    // persisted independently; switching modes must not project one layout's
    // active tabs, visibility, or splitter sizes onto the other.
    if (wasInitialized && wasEnabled && !enabled && d->dockManager)
    {
        Settings::instance().setDockLayoutState(
            d->dockManager->saveState(kDockLayoutVersion));
    }
    else if (wasInitialized && !wasEnabled && enabled)
    {
        saveClassicLayoutState();
    }

    d->floatingToolWindowsInitialized = true;
    d->floatingToolWindowsEnabled = enabled;
    Settings::instance().setFloatingToolWindowsEnabled(enabled);

    if (d->floatingToolWindowsAction)
    {
        const QSignalBlocker blocker(d->floatingToolWindowsAction);
        d->floatingToolWindowsAction->setChecked(enabled);
    }

    // The initial dockable mode already has every panel in ADS.
    if (enabled && !wasInitialized)
    {
        ensureWorkspaceDockVisible();
        configureNotePanelScrollArea();
        updateToolDockActionVisibility();
        updateDockTitles();
        return;
    }

    if (enabled == wasEnabled && wasInitialized)
    {
        configureNotePanelScrollArea();
        updateToolDockActionVisibility();
        return;
    }

    const bool curveVisible = d->curvePanelAction && d->curvePanelAction->isChecked();
    setUpdatesEnabled(false);
    if (!enabled)
    {
        // Hide every floating container in one step first. Without this they
        // would close one by one (each with a visible flash) as their docks
        // are emptied below. showDockWidget() shows them again when floating
        // mode is re-enabled.
        for (ads::CFloatingDockContainer *floatingWindow :
             d->dockManager->floatingWidgets())
        {
            if (floatingWindow)
                floatingWindow->hide();
        }
        closePluginPanels();

        const auto isOpen = [](ads::CDockWidget *dock, bool fallback)
        {
            return dock ? !dock->isClosed() : fallback;
        };
        d->timingToolsWereVisible = isOpen(d->timingToolsDock, true);
        d->playbackSpeedToolsWereVisible = isOpen(d->playbackSpeedToolsDock, true);
        d->rangeToolsWereVisible = isOpen(d->rangeToolsDock, true);
        d->mirrorToolsWereVisible = isOpen(d->mirrorToolsDock, true);
        d->pluginToolsWereVisible = isOpen(d->pluginToolsDock, false);
        d->statsToolsWereVisible = isOpen(d->statsToolsDock, true);
        d->leftPanelWasVisible = isOpen(d->leftPanelDock, true);
        d->previewWasVisible = isOpen(d->previewDock, true);
        d->notePanelWasVisible = isOpen(d->notePanelDock, true);
        d->bpmPanelWasVisible = isOpen(d->bpmPanelDock, false);
        d->metaPanelWasVisible = isOpen(d->metaPanelDock, false);

        const auto takeDockContent = [](ads::CDockWidget *dock) -> QWidget *
        {
            if (!dock)
                return nullptr;
            dock->toggleView(false);
            QWidget *content = dock->takeWidget();
            if (content)
                content->setStyleSheet(QString());
            return content;
        };

        QWidget *timingTools = takeDockContent(d->timingToolsDock);
        QWidget *playbackSpeedTools = takeDockContent(d->playbackSpeedToolsDock);
        QWidget *rangeTools = takeDockContent(d->rangeToolsDock);
        QWidget *mirrorTools = takeDockContent(d->mirrorToolsDock);
        QWidget *curveTools = takeDockContent(d->curveToolsDock);
        QWidget *pluginTools = takeDockContent(d->pluginToolsDock);
        QWidget *statsTools = takeDockContent(d->statsToolsDock);

        QWidget *leftPanel = takeDockContent(d->leftPanelDock);
        QWidget *preview = takeDockContent(d->previewDock);
        QWidget *notePanel = takeDockContent(d->notePanelDock);
        QWidget *bpmPanel = takeDockContent(d->bpmPanelDock);
        QWidget *metaPanel = takeDockContent(d->metaPanelDock);
        QWidget *workspace = d->workspaceDock ? d->workspaceDock->takeWidget() : nullptr;
        for (QWidget *panel : {leftPanel, notePanel, bpmPanel, metaPanel})
        {
            if (panel)
                panel->setStyleSheet(QString());
        }
        if (d->notePanel)
        {
            d->notePanel->attachLegacyToolSections(
                timingTools, playbackSpeedTools, rangeTools, mirrorTools,
                curveTools, pluginTools,
                Settings::instance().classicPluginToolsVisible());
            d->notePanel->setNoteChainControlsVisible(curveVisible);
        }
        if (d->leftPanel && statsTools)
            d->leftPanel->attachStatsSection(statsTools);

        d->workbenchLayout = new WorkbenchLayout(this);
        d->workbenchLayout->setObjectName(QStringLiteral("classicWorkbench"));
        d->workbenchLayout->setEditorWidget(workspace);
        if (leftPanel)
            d->workbenchLayout->addPane(WorkbenchLayout::Part::PrimarySidebar,
                                        QStringLiteral("navigation"),
                                        leftPanel,
                                        d->leftPanelWasVisible,
                                        true);
        if (preview)
            d->workbenchLayout->addPane(WorkbenchLayout::Part::PrimarySidebar,
                                        QStringLiteral("preview"),
                                        preview,
                                        d->previewWasVisible,
                                        false);
        if (notePanel)
            d->workbenchLayout->addPane(WorkbenchLayout::Part::AuxiliarySidebar,
                                        QStringLiteral("note"),
                                        notePanel,
                                        d->notePanelWasVisible,
                                        true);
        if (bpmPanel)
            d->workbenchLayout->addPane(WorkbenchLayout::Part::AuxiliarySidebar,
                                        QStringLiteral("bpm"),
                                        bpmPanel,
                                        d->bpmPanelWasVisible,
                                        true);
        if (metaPanel)
            d->workbenchLayout->addPane(WorkbenchLayout::Part::AuxiliarySidebar,
                                        QStringLiteral("meta"),
                                        metaPanel,
                                        d->metaPanelWasVisible,
                                        true);

        QWidget *oldCentral = takeCentralWidget();
        if (oldCentral)
            oldCentral->hide();
        setCentralWidget(d->workbenchLayout);
        d->legacySplitter = d->workbenchLayout->horizontalSplitter();
        d->legacyRightPanelContainer = d->workbenchLayout->auxiliarySidebar();
        d->legacyRightPanelContainer->setObjectName(QStringLiteral("rightPanelRoot"));
        d->legacyRightScrollArea = d->workbenchLayout->auxiliarySidebar()
                                       ->scrollAreaForPane(QStringLiteral("note"));
        d->legacyRightPanelLayout = nullptr;
        d->workbenchLayout->show();
        restoreClassicLayoutState();
    }
    else
    {
        if (d->workbenchLayout)
        {
            d->workbenchLayout->takeEditorWidget();
            for (const auto pane : {
                     qMakePair(WorkbenchLayout::Part::PrimarySidebar,
                               QStringLiteral("navigation")),
                     qMakePair(WorkbenchLayout::Part::PrimarySidebar,
                               QStringLiteral("preview")),
                     qMakePair(WorkbenchLayout::Part::AuxiliarySidebar,
                               QStringLiteral("note")),
                     qMakePair(WorkbenchLayout::Part::AuxiliarySidebar,
                               QStringLiteral("bpm")),
                     qMakePair(WorkbenchLayout::Part::AuxiliarySidebar,
                               QStringLiteral("meta"))})
            {
                d->workbenchLayout->takePane(pane.first, pane.second);
            }
        }

        QWidget *timingTools = d->notePanel ? d->notePanel->takeTimingToolsWidget() : nullptr;
        QWidget *playbackSpeedTools = d->notePanel
                                          ? d->notePanel->takePlaybackSpeedToolsWidget()
                                          : nullptr;
        QWidget *rangeTools = d->notePanel ? d->notePanel->takeRangeToolsWidget() : nullptr;
        QWidget *mirrorTools = d->notePanel ? d->notePanel->takeMirrorToolsWidget() : nullptr;
        QWidget *curveTools = d->notePanel ? d->notePanel->takeCurveToolsWidget() : nullptr;
        QWidget *pluginTools = d->notePanel ? d->notePanel->takeEmbeddedPluginToolsWidget() : nullptr;
        QWidget *statsTools = d->leftPanel ? d->leftPanel->takeStatsSection() : nullptr;

        const auto detachLegacyWidget = [](QWidget *widget)
        {
            if (widget)
                widget->setParent(nullptr);
        };
        detachLegacyWidget(d->leftPanel);
        detachLegacyWidget(d->previewWidget);
        detachLegacyWidget(d->workspaceContainer);
        detachLegacyWidget(d->notePanel);
        detachLegacyWidget(d->bpmPanel);
        detachLegacyWidget(d->metaPanel);

        QWidget *oldCentral = takeCentralWidget();
        if (oldCentral)
            oldCentral->hide();
        if (oldCentral == d->workbenchLayout)
        {
            delete d->workbenchLayout;
            d->workbenchLayout = nullptr;
        }
        d->legacySplitter = nullptr;
        d->legacyRightScrollArea = nullptr;
        d->legacyRightPanelContainer = nullptr;
        d->legacyRightPanelLayout = nullptr;
        setCentralWidget(d->dockManager);
        d->dockManager->show();

        const auto restoreDockContent = [](ads::CDockWidget *dock,
                                           QWidget *content,
                                           bool visible,
                                           ads::CDockWidget::eInsertMode insertMode = ads::CDockWidget::ForceScrollArea)
        {
            if (!dock || !content)
                return;
            // Reparent the content into the dock widget before showing it.
            // The content is parentless here, so showing it first would
            // briefly flash a transient top-level native window per panel.
            dock->setWidget(content, insertMode);
            content->show();
            dock->toggleView(visible);
        };
        restoreDockContent(d->workspaceDock, d->workspaceContainer, true,
                           ads::CDockWidget::ForceNoScrollArea);
        restoreDockContent(d->leftPanelDock, d->leftPanel, d->leftPanelWasVisible);
        restoreDockContent(d->statsToolsDock, statsTools, d->statsToolsWereVisible);
        restoreDockContent(d->previewDock, d->previewWidget, d->previewWasVisible,
                           ads::CDockWidget::ForceNoScrollArea);
        restoreDockContent(d->notePanelDock, d->notePanel, d->notePanelWasVisible);
        restoreDockContent(d->bpmPanelDock, d->bpmPanel, d->bpmPanelWasVisible);
        restoreDockContent(d->metaPanelDock, d->metaPanel, d->metaPanelWasVisible);
        restoreDockContent(d->timingToolsDock, timingTools, d->timingToolsWereVisible);
        restoreDockContent(d->playbackSpeedToolsDock, playbackSpeedTools,
                           d->playbackSpeedToolsWereVisible);
        restoreDockContent(d->rangeToolsDock, rangeTools, d->rangeToolsWereVisible);
        restoreDockContent(d->mirrorToolsDock, mirrorTools, d->mirrorToolsWereVisible);
        restoreDockContent(d->curveToolsDock, curveTools, curveVisible);
        restoreDockContent(d->pluginToolsDock, pluginTools, d->pluginToolsWereVisible);

        if (d->notePanel)
            d->notePanel->setNoteChainControlsVisible(curveVisible);

        // restoreDockLayout() reads only the multi-window snapshot. If this is
        // the first switch and no snapshot exists, the captured visibility
        // flags above remain the fallback layout.
        restoreDockLayout();
        ensurePlaybackSpeedDockAssigned();
        ensureStatsDockAssigned();
        ensureWorkspaceDockVisible();
    }

    updateDockTitles();
    updateToolDockActionVisibility();
    configureNotePanelScrollArea();
    setUpdatesEnabled(true);
    update();
    if (d->floatingToolWindowsAction)
        applySidebarTheme();

    Logger::info(QString("Floating tool windows: %1")
                     .arg(enabled ? QStringLiteral("enabled") : QStringLiteral("disabled")));
}

void MainWindow::updateToolDockActionVisibility()
{
    const QList<ads::CDockWidget *> docks = {
        d->leftPanelDock, d->previewDock, d->notePanelDock,
        d->timingToolsDock, d->playbackSpeedToolsDock,
        d->rangeToolsDock, d->mirrorToolsDock,
        d->curveToolsDock, d->pluginToolsDock, d->bpmPanelDock,
        d->metaPanelDock, d->statsToolsDock};
    for (ads::CDockWidget *dock : docks)
    {
        if (dock && dock->toggleViewAction())
            dock->toggleViewAction()->setVisible(d->floatingToolWindowsEnabled);
    }
    if (d->panelsMenu)
    {
        // Keep View -> Panels available in classic mode so its own layout can
        // be reset. Recovery/toggle actions remain multi-window-only because
        // their ADS widgets are temporarily empty in the classic workspace.
        d->panelsMenu->menuAction()->setVisible(true);
        for (QAction *action : d->panelsMenu->actions())
        {
            if (action && (action->objectName() == QLatin1String("action.show_main_editor")
                           || action->objectName() == QLatin1String("action.reopen_closed_panels")))
            {
                action->setVisible(d->floatingToolWindowsEnabled);
            }
        }
    }
    if (d->panelsToolbarAction)
        d->panelsToolbarAction->setVisible(d->floatingToolWindowsEnabled);
}

void MainWindow::ensurePlaybackSpeedDockAssigned()
{
    if (!d->dockManager || !d->playbackSpeedToolsDock
        || d->playbackSpeedToolsDock->dockAreaWidget())
    {
        return;
    }

    ads::CDockAreaWidget *area = nullptr;
    if (d->timingToolsDock && d->timingToolsDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::BottomDockWidgetArea, d->playbackSpeedToolsDock,
            d->timingToolsDock->dockAreaWidget());
    }
    else if (d->rangeToolsDock && d->rangeToolsDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::TopDockWidgetArea, d->playbackSpeedToolsDock,
            d->rangeToolsDock->dockAreaWidget());
    }
    else if (d->notePanelDock && d->notePanelDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::BottomDockWidgetArea, d->playbackSpeedToolsDock,
            d->notePanelDock->dockAreaWidget());
    }
    else if (d->workspaceDock && d->workspaceDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::RightDockWidgetArea, d->playbackSpeedToolsDock,
            d->workspaceDock->dockAreaWidget());
    }

    if (!area)
        return;

    area->setAllowedAreas(ads::OuterDockAreas);
    d->playbackSpeedToolsDock->toggleView(true);
    configureCompactToolDock(d->playbackSpeedToolsDock);
    Logger::info("Added Playback Speed panel to an existing ADS layout.");
}

void MainWindow::ensureStatsDockAssigned()
{
    if (!d->dockManager || !d->statsToolsDock
        || d->statsToolsDock->dockAreaWidget())
    {
        return;
    }

    ads::CDockAreaWidget *area = nullptr;
    if (d->leftPanelDock && d->leftPanelDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::BottomDockWidgetArea, d->statsToolsDock,
            d->leftPanelDock->dockAreaWidget());
    }
    else if (d->notePanelDock && d->notePanelDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::BottomDockWidgetArea, d->statsToolsDock,
            d->notePanelDock->dockAreaWidget());
    }
    else if (d->workspaceDock && d->workspaceDock->dockAreaWidget())
    {
        area = d->dockManager->addDockWidget(
            ads::RightDockWidgetArea, d->statsToolsDock,
            d->workspaceDock->dockAreaWidget());
    }

    if (!area)
        return;

    area->setAllowedAreas(ads::OuterDockAreas);
    d->statsToolsDock->toggleView(true);
    configureCompactToolDock(d->statsToolsDock);
    Logger::info("Added Chart Statistics panel to an existing ADS layout.");
}

void MainWindow::configureNotePanelScrollArea()
{
    if (!d->floatingToolWindowsEnabled)
    {
        if (!d->legacyRightScrollArea)
            return;
        d->legacyRightScrollArea->setWidgetResizable(true);
        d->legacyRightScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        d->legacyRightScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        d->legacyRightScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (d->legacyRightScrollArea->viewport())
            d->legacyRightScrollArea->viewport()->setMinimumWidth(0);
        return;
    }

    if (!d->notePanelDock)
        return;

    QScrollArea *scroll = d->notePanelDock->findChild<QScrollArea *>(
        QStringLiteral("dockWidgetScrollArea"));
    if (!scroll)
        return;

    scroll->setWidgetResizable(true);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    if (scroll->viewport())
        scroll->viewport()->setMinimumWidth(0);
    if (d->notePanel)
        d->notePanel->setMinimumWidth(0);
}
