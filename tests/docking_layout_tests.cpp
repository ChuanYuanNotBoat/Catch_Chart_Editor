#include <QApplication>
#include <QAbstractScrollArea>
#include <QColor>
#include <QGuiApplication>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QWidget>
#include <cstdio>

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
    manager->addDockWidget(ads::RightDockWidgetArea, panelDock, workspaceArea);

    window.show();
    app.processEvents();

    ok &= require(window.minimumSizeHint().height() < 1200,
                  "scroll-wrapped dock content must not force the top-level window to its 2400px minimum height");

    const QByteArray initialState = manager->saveState(1);
    ok &= require(!initialState.isEmpty(), "ADS layout state must be serializable");

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

    ok &= require(manager->restoreState(initialState, 1), "saved ADS layout must restore successfully");
    app.processEvents();
    ok &= require(!panelDock->isFloating() && !panelDock->isClosed(),
                  "restoring the layout must redock and reopen the panel");

    window.close();
    app.processEvents();
    delete manager;
    return ok ? 0 : 1;
}
