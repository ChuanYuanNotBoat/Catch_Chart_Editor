#include "MainWindow.h"
#include "MainWindowPrivate.h"

#include "ui/NoteEditPanel.h"
#include "ui/BPMTimePanel.h"
#include "ui/LeftPanel.h"
#include "ui/MetaEditPanel.h"
#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <DockManager.h>
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
}

void MainWindow::setFloatingToolWindowsEnabled(bool enabled)
{
    const bool wasInitialized = d->floatingToolWindowsInitialized;
    const bool wasEnabled = d->floatingToolWindowsEnabled;
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
        // Preserve the user's real ADS layout before temporarily emptying the
        // tool docks. Layout saves are paused in legacy mode so this snapshot
        // remains available after a restart.
        if (d->dockManager)
            Settings::instance().setDockLayoutState(
                d->dockManager->saveState(kDockLayoutVersion));
        closePluginPanels();

        const auto isOpen = [](ads::CDockWidget *dock, bool fallback)
        {
            return dock ? !dock->isClosed() : fallback;
        };
        d->timingToolsWereVisible = isOpen(d->timingToolsDock, true);
        d->rangeToolsWereVisible = isOpen(d->rangeToolsDock, true);
        d->mirrorToolsWereVisible = isOpen(d->mirrorToolsDock, true);
        d->pluginToolsWereVisible = isOpen(d->pluginToolsDock, false);
        d->leftPanelWasVisible = isOpen(d->leftPanelDock, true);
        d->previewWasVisible = isOpen(d->previewDock, true);
        d->notePanelWasVisible = isOpen(d->notePanelDock, true);
        d->bpmPanelWasVisible = isOpen(d->bpmPanelDock, false);
        d->metaPanelWasVisible = isOpen(d->metaPanelDock, false);

        // Prefer the ADS panel that is actually visible when converting a tab
        // group into the old single-panel right sidebar.
        if (d->notePanelDock && d->notePanelDock->isVisible())
            d->currentRightPanel = d->notePanel;
        if (d->bpmPanelDock && d->bpmPanelDock->isVisible())
            d->currentRightPanel = d->bpmPanel;
        if (d->metaPanelDock && d->metaPanelDock->isVisible())
            d->currentRightPanel = d->metaPanel;

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
        QWidget *rangeTools = takeDockContent(d->rangeToolsDock);
        QWidget *mirrorTools = takeDockContent(d->mirrorToolsDock);
        QWidget *curveTools = takeDockContent(d->curveToolsDock);
        QWidget *pluginTools = takeDockContent(d->pluginToolsDock);

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
                timingTools, rangeTools, mirrorTools, curveTools, pluginTools,
                d->pluginToolsWereVisible);
            d->notePanel->setNoteChainControlsVisible(curveVisible);
        }

        if (!d->legacySplitter)
        {
            d->legacySplitter = new QSplitter(Qt::Horizontal, this);
            d->legacyRightPanelContainer = new QWidget;
            d->legacyRightPanelContainer->setObjectName(QStringLiteral("rightPanelRoot"));
            d->legacyRightPanelContainer->setAttribute(Qt::WA_StyledBackground, true);
            d->legacyRightPanelLayout = new QVBoxLayout(d->legacyRightPanelContainer);
            d->legacyRightPanelLayout->setContentsMargins(0, 0, 0, 0);

            d->legacyRightScrollArea = new QScrollArea(d->legacySplitter);
            d->legacyRightScrollArea->setObjectName(QStringLiteral("legacyRightPanelScrollArea"));
            d->legacyRightScrollArea->setWidgetResizable(true);
            d->legacyRightScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
            d->legacyRightScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            d->legacyRightScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            d->legacyRightScrollArea->setFrameShape(QFrame::NoFrame);
            d->legacyRightScrollArea->setWidget(d->legacyRightPanelContainer);
            d->legacySplitter->addWidget(d->legacyRightScrollArea);
        }

        if (leftPanel)
            d->legacySplitter->insertWidget(0, leftPanel);
        if (preview)
            d->legacySplitter->insertWidget(1, preview);
        if (workspace)
            d->legacySplitter->insertWidget(2, workspace);
        if (notePanel)
            d->legacyRightPanelLayout->addWidget(notePanel);
        if (bpmPanel)
            d->legacyRightPanelLayout->addWidget(bpmPanel);
        if (metaPanel)
            d->legacyRightPanelLayout->addWidget(metaPanel);

        QWidget *currentPanel = d->currentRightPanel ? d->currentRightPanel : d->notePanel;
        d->notePanel->setVisible(currentPanel == d->notePanel);
        d->bpmPanel->setVisible(currentPanel == d->bpmPanel);
        d->metaPanel->setVisible(currentPanel == d->metaPanel);

        QWidget *oldCentral = takeCentralWidget();
        if (oldCentral)
            oldCentral->hide();
        setCentralWidget(d->legacySplitter);
        d->legacySplitter->setSizes({150, 200, 700, 300});
        d->legacySplitter->show();
    }
    else
    {
        QWidget *timingTools = d->notePanel ? d->notePanel->takeTimingToolsWidget() : nullptr;
        QWidget *rangeTools = d->notePanel ? d->notePanel->takeRangeToolsWidget() : nullptr;
        QWidget *mirrorTools = d->notePanel ? d->notePanel->takeMirrorToolsWidget() : nullptr;
        QWidget *curveTools = d->notePanel ? d->notePanel->takeCurveToolsWidget() : nullptr;
        QWidget *pluginTools = d->notePanel ? d->notePanel->takeEmbeddedPluginToolsWidget() : nullptr;

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
        setCentralWidget(d->dockManager);
        d->dockManager->show();

        const auto restoreDockContent = [](ads::CDockWidget *dock,
                                           QWidget *content,
                                           bool visible,
                                           ads::CDockWidget::eInsertMode insertMode = ads::CDockWidget::ForceScrollArea)
        {
            if (!dock || !content)
                return;
            content->show();
            dock->setWidget(content, insertMode);
            dock->toggleView(visible);
        };
        restoreDockContent(d->workspaceDock, d->workspaceContainer, true,
                           ads::CDockWidget::ForceNoScrollArea);
        restoreDockContent(d->leftPanelDock, d->leftPanel, d->leftPanelWasVisible);
        restoreDockContent(d->previewDock, d->previewWidget, d->previewWasVisible,
                           ads::CDockWidget::ForceNoScrollArea);
        restoreDockContent(d->notePanelDock, d->notePanel, d->notePanelWasVisible);
        restoreDockContent(d->bpmPanelDock, d->bpmPanel, d->bpmPanelWasVisible);
        restoreDockContent(d->metaPanelDock, d->metaPanel, d->metaPanelWasVisible);
        restoreDockContent(d->timingToolsDock, timingTools, d->timingToolsWereVisible);
        restoreDockContent(d->rangeToolsDock, rangeTools, d->rangeToolsWereVisible);
        restoreDockContent(d->mirrorToolsDock, mirrorTools, d->mirrorToolsWereVisible);
        restoreDockContent(d->curveToolsDock, curveTools, curveVisible);
        restoreDockContent(d->pluginToolsDock, pluginTools, d->pluginToolsWereVisible);

        if (d->notePanel)
            d->notePanel->setNoteChainControlsVisible(curveVisible);
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
        d->timingToolsDock, d->rangeToolsDock, d->mirrorToolsDock,
        d->curveToolsDock, d->pluginToolsDock, d->bpmPanelDock,
        d->metaPanelDock};
    for (ads::CDockWidget *dock : docks)
    {
        if (dock && dock->toggleViewAction())
            dock->toggleViewAction()->setVisible(d->floatingToolWindowsEnabled);
    }
    if (d->panelsMenu)
        d->panelsMenu->menuAction()->setVisible(d->floatingToolWindowsEnabled);
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
