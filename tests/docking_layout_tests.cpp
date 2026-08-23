#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <cstdio>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

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

    QMainWindow window;
    window.resize(900, 600);

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

    bool ok = true;
    ok &= require(window.minimumSizeHint().height() < 1200,
                  "scroll-wrapped dock content must not force the top-level window to its 2400px minimum height");

    const QByteArray initialState = manager->saveState(1);
    ok &= require(!initialState.isEmpty(), "ADS layout state must be serializable");

    manager->addDockWidgetFloating(panelDock);
    app.processEvents();
    ok &= require(panelDock->isFloating(), "a panel must be detachable into a floating container");

    ok &= require(manager->restoreState(initialState, 1), "saved ADS layout must restore successfully");
    app.processEvents();
    ok &= require(!panelDock->isFloating() && !panelDock->isClosed(),
                  "restoring the layout must redock and reopen the panel");

    window.close();
    app.processEvents();
    delete manager;
    return ok ? 0 : 1;
}
