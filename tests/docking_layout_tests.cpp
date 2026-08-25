#include <QApplication>
#include <QAbstractScrollArea>
#include <QColor>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

#include "ui/PlaybackSpeedPanel.h"
#include "utils/NativeWindowTheme.h"
#include <DockAreaWidget.h>
#include <FloatingDockContainer.h>
#include <FloatingDragPreview.h>
#include <DockManager.h>
#include <DockWidget.h>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace
{
    bool require(bool condition, const char *message)
    {
        if (!condition)
            std::fprintf(stderr, "FAILED: %s\n", message);
        return condition;
    }
}

int main(int argc, char **argv)
{
#if defined(Q_OS_LINUX)
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
#endif
    QApplication app(argc, argv);

    bool ok = true;
    QWidget nativeThemeProbe;
    NativeWindowTheme::apply(&nativeThemeProbe,
                             QColor(28, 30, 34),
                             QColor(245, 245, 245),
                             QColor(58, 61, 68));
    ok &= require(nativeThemeProbe.internalWinId() == 0,
                  "native title theming must not create a window handle eagerly");
#ifdef Q_OS_WIN
    nativeThemeProbe.show();
    app.processEvents();
    constexpr DWORD borderColorAttribute = 34;
    COLORREF actualBorder = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(reinterpret_cast<HWND>(nativeThemeProbe.internalWinId()),
                                        borderColorAttribute,
                                        &actualBorder,
                                        sizeof(actualBorder))))
    {
        ok &= require(actualBorder == RGB(58, 61, 68),
                      "Windows native border must use the requested editor theme color");
    }
    nativeThemeProbe.hide();
#endif

    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewShowsContentPixmap, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewIsDynamic, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewHasWindowFrame, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DisableStylesheet, true);

    QMainWindow window;
    window.resize(900, 600);
    if (QScreen *screen = QGuiApplication::primaryScreen())
        window.move(screen->availableGeometry().topLeft() + QPoint(140, 90));

    auto *manager = new ads::CDockManager(&window);
    auto *workspaceDock = new ads::CDockWidget(manager, QStringLiteral("Workspace"));
    workspaceDock->setObjectName(QStringLiteral("test.workspace"));
    workspaceDock->setWidget(new QWidget, ads::CDockWidget::ForceNoScrollArea);
    ads::CDockAreaWidget *workspaceArea = manager->setCentralWidget(workspaceDock);

    auto *tallContent = new QWidget;
    tallContent->setMinimumSize(280, 2400);
    auto *panelDock = new ads::CDockWidget(manager, QStringLiteral("Tall Panel"));
    panelDock->setObjectName(QStringLiteral("test.tall-panel"));
    panelDock->setWidget(tallContent, ads::CDockWidget::ForceScrollArea);
    ads::CDockAreaWidget *panelArea = manager->addDockWidget(
        ads::RightDockWidgetArea, panelDock, workspaceArea);

    // Model the editor's medium-grained tool blocks: docked utilities keep the
    // original vertical reading order and remain visible at the same time.
    auto *speedPanel = new PlaybackSpeedPanel;
    auto *speedDock = new ads::CDockWidget(manager, QStringLiteral("Playback Speed"));
    speedDock->setObjectName(QStringLiteral("test.playback-speed"));
    speedDock->setWidget(speedPanel, ads::CDockWidget::ForceScrollArea);
    ads::CDockAreaWidget *speedArea = manager->addDockWidget(
        ads::BottomDockWidgetArea, speedDock, panelArea);
    speedArea->setAllowedAreas(ads::OuterDockAreas);

    auto *rangeDock = new ads::CDockWidget(manager, QStringLiteral("Range Select"));
    rangeDock->setObjectName(QStringLiteral("test.range"));
    rangeDock->setWidget(new QWidget, ads::CDockWidget::ForceScrollArea);
    ads::CDockAreaWidget *rangeArea = manager->addDockWidget(
        ads::BottomDockWidgetArea, rangeDock, speedArea);
    rangeArea->setAllowedAreas(ads::OuterDockAreas);

    auto *mirrorDock = new ads::CDockWidget(manager, QStringLiteral("Mirror Flip"));
    mirrorDock->setObjectName(QStringLiteral("test.mirror"));
    mirrorDock->setWidget(new QWidget, ads::CDockWidget::ForceScrollArea);
    ads::CDockAreaWidget *mirrorArea = manager->addDockWidget(
        ads::BottomDockWidgetArea, mirrorDock, rangeArea);
    mirrorArea->setAllowedAreas(ads::OuterDockAreas);

    auto *pluginToolsDock = new ads::CDockWidget(manager, QStringLiteral("Plugin Tools"));
    pluginToolsDock->setObjectName(QStringLiteral("test.plugin-tools"));
    pluginToolsDock->setWidget(new QWidget, ads::CDockWidget::ForceScrollArea);
    ads::CDockAreaWidget *pluginToolsArea = manager->addDockWidget(
        ads::BottomDockWidgetArea, pluginToolsDock, mirrorArea);
    pluginToolsArea->setAllowedAreas(ads::OuterDockAreas);

    window.show();
    app.processEvents();

    auto *speedInput = speedPanel->findChild<QDoubleSpinBox *>(
        QStringLiteral("playbackSpeedInput"));
    ok &= require(speedInput
                      && qFuzzyCompare(speedInput->minimum(), PlaybackSpeedPanel::MinimumSpeed)
                      && qFuzzyCompare(speedInput->maximum(), PlaybackSpeedPanel::MaximumSpeed),
                  "playback speed input must expose the complete 0.1x-10x range");
    int speedChangeCount = 0;
    double requestedSpeed = 0.0;
    QObject::connect(speedPanel, &PlaybackSpeedPanel::speedChanged,
                     [&speedChangeCount, &requestedSpeed](double speed)
                     {
                         ++speedChangeCount;
                         requestedSpeed = speed;
                     });
    if (speedInput)
        speedInput->setValue(10.0);
    ok &= require(speedChangeCount == 1 && qFuzzyCompare(requestedSpeed, 10.0),
                  "manual playback speed input must emit the requested speed");
    auto *quickButton = speedPanel->findChild<QPushButton *>(
        QStringLiteral("playbackSpeedQuickButton150"));
    ok &= require(quickButton
                      && quickButton->sizePolicy().horizontalPolicy() == QSizePolicy::Maximum,
                  "playback speed shortcuts must retain compact action-button sizing");
    if (quickButton)
        quickButton->click();
    ok &= require(qFuzzyCompare(speedPanel->speed(), 1.5)
                      && qFuzzyCompare(requestedSpeed, 1.5),
                  "playback speed quick buttons must update the same speed control");
    speedPanel->setSpeed(25.0);
    ok &= require(qFuzzyCompare(speedPanel->speed(), PlaybackSpeedPanel::MaximumSpeed),
                  "programmatic playback speed updates must clamp to 10x");
    speedPanel->setSpeed(1.0);

    ok &= require(window.minimumSizeHint().height() < 1200,
                  "scroll-wrapped dock content must not force the top-level window to its 2400px minimum height");
    auto *panelScroll = panelDock->findChild<QAbstractScrollArea *>();
    ok &= require(panelScroll, "ForceScrollArea dock must create a scroll area");
    if (panelScroll)
    {
        panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        panelScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ok &= require(panelScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
                      "legacy Note sidebar must never expose a horizontal scrollbar");
        ok &= require(panelScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
                      "legacy Note sidebar must retain vertical scrolling");
    }

    const QByteArray initialState = manager->saveState(1);
    ok &= require(!initialState.isEmpty(), "ADS layout state must be serializable");
    ok &= require(speedDock->dockAreaWidget() != rangeDock->dockAreaWidget()
                      && rangeDock->dockAreaWidget() != mirrorDock->dockAreaWidget()
                      && mirrorDock->dockAreaWidget() != pluginToolsDock->dockAreaWidget(),
                  "docked tool blocks must remain simultaneously visible split sections");
    auto *toolSplitter = qobject_cast<QSplitter *>(rangeArea->parentWidget());
    ok &= require(toolSplitter && toolSplitter->orientation() == Qt::Vertical
                      && speedArea->parentWidget() == toolSplitter
                      && mirrorArea->parentWidget() == toolSplitter
                      && pluginToolsArea->parentWidget() == toolSplitter,
                  "docked tool blocks must preserve the original vertical reading order");
    ok &= require(!speedArea->allowedAreas().testFlag(ads::CenterDockWidgetArea)
                      && !rangeArea->allowedAreas().testFlag(ads::CenterDockWidgetArea)
                      && !mirrorArea->allowedAreas().testFlag(ads::CenterDockWidgetArea)
                      && !pluginToolsArea->allowedAreas().testFlag(ads::CenterDockWidgetArea),
                  "tool blocks must reject switching-tab merges");

    rangeDock->setFloating();
    app.processEvents();
    ok &= require(rangeDock->isFloating(),
                  "an individual tool block must detach from its merged group");
    ok &= require(manager->restoreState(initialState, 1),
                  "the stacked tool layout must restore after detaching a block");
    app.processEvents();
    ok &= require(!rangeDock->isFloating()
                      && rangeDock->dockAreaWidget() != mirrorDock->dockAreaWidget(),
                  "restoring the layout must return the block without creating a tab group");

    // Floating tools can be disabled without replacing their widgets. Move
    // the same medium-grained sections into one legacy vertical sidebar, then
    // put them back into their existing ADS docks and verify their identities.
    speedDock->toggleView(false);
    rangeDock->toggleView(false);
    mirrorDock->toggleView(false);
    pluginToolsDock->toggleView(false);
    QWidget *speedContent = speedDock->takeWidget();
    QWidget *rangeContent = rangeDock->takeWidget();
    QWidget *mirrorContent = mirrorDock->takeWidget();
    QWidget *pluginToolsContent = pluginToolsDock->takeWidget();
    QWidget legacySidebar;
    QVBoxLayout legacyLayout(&legacySidebar);
    legacyLayout.addWidget(speedContent);
    legacyLayout.addWidget(rangeContent);
    legacyLayout.addWidget(mirrorContent);
    legacyLayout.addWidget(pluginToolsContent);
    ok &= require(speedContent && speedContent->parentWidget() == &legacySidebar
                      && rangeContent && rangeContent->parentWidget() == &legacySidebar
                      && mirrorContent && mirrorContent->parentWidget() == &legacySidebar
                      && pluginToolsContent && pluginToolsContent->parentWidget() == &legacySidebar,
                  "disabling floating tools must embed the existing widgets in one sidebar");

    for (QWidget *content : {speedContent, rangeContent, mirrorContent, pluginToolsContent})
    {
        legacyLayout.removeWidget(content);
        content->setParent(nullptr);
    }
    speedDock->setWidget(speedContent, ads::CDockWidget::ForceScrollArea);
    rangeDock->setWidget(rangeContent, ads::CDockWidget::ForceScrollArea);
    mirrorDock->setWidget(mirrorContent, ads::CDockWidget::ForceScrollArea);
    pluginToolsDock->setWidget(pluginToolsContent, ads::CDockWidget::ForceScrollArea);
    speedDock->toggleView(true);
    rangeDock->toggleView(true);
    mirrorDock->toggleView(true);
    pluginToolsDock->toggleView(true);
    app.processEvents();
    ok &= require(speedDock->widget() == speedContent
                      && rangeDock->widget() == rangeContent
                      && mirrorDock->widget() == mirrorContent
                      && pluginToolsDock->widget() == pluginToolsContent,
                  "re-enabling floating tools must restore the same widgets to their docks");

    // Exercise the complete pre-ADS workspace switch used when floating
    // windows are disabled: fixed four-column splitter plus one switchable,
    // vertically scrollable right sidebar.
    panelDock->toggleView(false);
    rangeDock->toggleView(false);
    mirrorDock->toggleView(false);
    QWidget *workspaceContent = workspaceDock->takeWidget();
    QWidget *legacyRightContent = panelDock->takeWidget();
    rangeContent = rangeDock->takeWidget();
    mirrorContent = mirrorDock->takeWidget();

    auto *fixedSplitter = new QSplitter(Qt::Horizontal);
    auto *rightScroll = new QScrollArea(fixedSplitter);
    auto *rightContainer = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(legacyRightContent);
    rightScroll->setWidget(rightContainer);
    rightScroll->setWidgetResizable(true);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    fixedSplitter->addWidget(rangeContent);
    fixedSplitter->addWidget(mirrorContent);
    fixedSplitter->addWidget(workspaceContent);
    fixedSplitter->addWidget(rightScroll);

    QWidget *dockCentral = window.takeCentralWidget();
    dockCentral->hide();
    window.setCentralWidget(fixedSplitter);
    fixedSplitter->show();
    app.processEvents();
    ok &= require(window.centralWidget() == fixedSplitter && fixedSplitter->count() == 4,
                  "disabled floating mode must use the pre-ADS fixed four-column splitter");
    ok &= require(!manager->isVisible()
                      && rightScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
                      && rightScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
                  "fixed mode must hide ADS and expose only vertical right-sidebar scrolling");

    window.takeCentralWidget();
    rangeContent->setParent(nullptr);
    mirrorContent->setParent(nullptr);
    workspaceContent->setParent(nullptr);
    rightLayout->removeWidget(legacyRightContent);
    legacyRightContent->setParent(nullptr);
    rightScroll->takeWidget();
    delete rightContainer;
    delete fixedSplitter;
    window.setCentralWidget(manager);
    manager->show();
    workspaceDock->setWidget(workspaceContent, ads::CDockWidget::ForceNoScrollArea);
    panelDock->setWidget(legacyRightContent, ads::CDockWidget::ForceScrollArea);
    rangeDock->setWidget(rangeContent, ads::CDockWidget::ForceScrollArea);
    mirrorDock->setWidget(mirrorContent, ads::CDockWidget::ForceScrollArea);
    panelDock->toggleView(true);
    rangeDock->toggleView(true);
    mirrorDock->toggleView(true);
    app.processEvents();
    ok &= require(window.centralWidget() == manager
                      && workspaceDock->widget() == workspaceContent
                      && panelDock->widget() == legacyRightContent,
                  "re-enabling floating mode must restore ADS and the original panel widgets");

    QPointer<ads::CFloatingDockContainer> detachedWindow;
    QObject::connect(manager,
                     &ads::CDockManager::floatingWidgetCreated,
                     [&detachedWindow](ads::CFloatingDockContainer *floatingWindow)
                     {
                         detachedWindow = floatingWindow;
                     });

    // Exercise the actual non-opaque drag-preview handoff. The preview is an
    // owned Qt::Tool window; its global geometry must be captured before ADS
    // reparents the dock into the real floating container.
    auto *preview = new ads::CFloatingDragPreview(panelDock);
    const QRect previewGeometry(window.mapToGlobal(QPoint(420, 120)), QSize(320, 360));
    preview->setGeometry(previewGeometry);
    preview->show();
    app.processEvents();
    const QPoint expectedFloatingOrigin = preview->mapToGlobal(QPoint(0, 0));
    preview->finishDragging();
    app.processEvents();
    app.processEvents();
    ok &= require(panelDock->isFloating(), "a panel must be detachable into a floating container");
    ok &= require(detachedWindow && detachedWindow->isVisible(),
                  "drag-preview handoff must create a visible floating container");

    if (detachedWindow)
    {
#ifdef Q_OS_WIN
        const QPoint actualFloatingOrigin = detachedWindow->mapToGlobal(QPoint(0, 0));
        ok &= require(qAbs(actualFloatingOrigin.x() - expectedFloatingOrigin.x()) <= 80
                          && qAbs(actualFloatingOrigin.y() - expectedFloatingOrigin.y()) <= 80,
                      "floating container must keep the drag preview's global screen position");
#endif

        auto *scrollArea = panelDock->findChild<QAbstractScrollArea *>();
        ok &= require(scrollArea && scrollArea->viewport(),
                      "ForceScrollArea dock must retain its viewport after becoming floating");
        if (scrollArea && scrollArea->viewport())
        {
            ok &= require(scrollArea->isVisibleTo(detachedWindow)
                              && scrollArea->viewport()->isVisibleTo(detachedWindow),
                          "floating scroll panel and viewport must be visible after the queued refresh");
            ok &= require(!scrollArea->viewport()->size().isEmpty(),
                          "floating scroll viewport must receive a non-empty first layout");
        }
    }

    // Closing a floating container hides its persistent ADS window and marks
    // the contained panel closed.  The panel must remain recoverable through
    // the same toggle action exposed by View -> Panels.
    if (detachedWindow)
    {
        detachedWindow->close();
        app.processEvents();
        ok &= require(panelDock->isClosed() && !panelDock->toggleViewAction()->isChecked(),
                      "closing a floating container must update its panel action");

        panelDock->toggleViewAction()->trigger();
        app.processEvents();
        ok &= require(!panelDock->isClosed() && panelDock->toggleViewAction()->isChecked(),
                      "a closed floating panel must reopen from its panel action");
        ok &= require(detachedWindow->isVisible(),
                      "reopening a floating panel must show its existing container");

        // Simulate a delayed, non-close platform hide after reopening.  It
        // must not consume a stale close marker and close the panel again.
        detachedWindow->hide();
        app.processEvents();
        ok &= require(!panelDock->isClosed() && panelDock->toggleViewAction()->isChecked(),
                      "a later generic hide must not re-close a recovered panel");
        detachedWindow->show();
        app.processEvents();
    }

    ok &= require(manager->restoreState(initialState, 1), "saved ADS layout must restore successfully");
    app.processEvents();
    ok &= require(!panelDock->isFloating() && !panelDock->isClosed(),
                  "restoring the layout must redock and reopen the panel");

    window.close();
    app.processEvents();
    delete manager;
    return ok ? 0 : 1;
}
