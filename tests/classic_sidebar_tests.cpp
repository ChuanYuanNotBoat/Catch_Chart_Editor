#include "app/MainWindow.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/CommandRouter.h"
#include "controller/PlaybackController.h"
#include "controller/SelectionController.h"
#include "model/Skin.h"
#include "review/ReviewWorkspace.h"
#include "ui/WorkbenchLayout.h"
#include "ui/PaneContainer.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <memory>

class ClassicSidebarTests : public QObject
{
    Q_OBJECT
private slots:
    void embeddedPagesSurviveModeSwitchesAndRestart()
    {
        Settings::instance().setNoteSoundVolume(0);
        Settings::instance().setFloatingToolWindowsEnabled(false);
        Settings::instance().clearClassicLayoutState();
        ChartController controller;
        SelectionController selection;
        AudioPlayer audio;
        PlaybackController playback(&audio);
        auto window = std::make_unique<MainWindow>(&controller, &selection, &playback, new Skin);
        window->resize(1440, 900);
        window->show();
        QTest::qWait(100);
        Chart chart;
        chart.meta().title = QStringLiteral("Starlight Garden");
        chart.meta().artist = QStringLiteral("Demo artist");
        chart.meta().difficulty = QStringLiteral("Hard");
        chart.addBpm(BpmEntry(0, 0, 1, 150));
        for (int i = 0; i < 24; ++i)
            chart.addNote(Note(i / 2, i % 2, 2, 64 + (i * 53) % 400));
        controller.loadChartFromData({}, chart);

        auto *workbench = window->findChild<WorkbenchLayout *>();
        auto *review = window->findChild<CceReview::ReviewWorkspace *>();
        auto *router = window->findChild<CommandRouter *>();
        auto *dialog = window->findChild<QDialog *>("reviewWorkspaceWindow");
        auto *layers = window->findChild<QTreeWidget *>("reviewLayers");
        auto *comments = window->findChild<QTreeWidget *>("reviewComments");
        QVERIFY(workbench && review && router && dialog && layers && comments);
        QVERIFY(workbench->primarySidebar()->paneVisible("navigation"));
        QVERIFY(!workbench->primarySidebar()->paneVisible("layers"));
        QVERIFY(!dialog->isWindow());
        QVERIFY(workbench->primarySidebar()->isAncestorOf(layers));
        QVERIFY(workbench->auxiliarySidebar()->isAncestorOf(comments));
        auto *toolbarTabs = window->findChild<QTabBar *>("reviewToolbarTabs");
        QVERIFY(toolbarTabs && toolbarTabs->isVisible());
        QCOMPARE(window->findChildren<QTabBar *>("reviewToolbarTabs").size(), 1);
        QCOMPARE(toolbarTabs->count(), 5);
        auto *menu = window->findChild<QMenu *>("localReviewMenu");
        QVERIFY(menu);
        QCOMPARE(menu->actions().size(), 3);
        QCOMPARE(menu->actions()[0]->objectName(), QStringLiteral("review.layers"));
        QCOMPARE(menu->actions()[1]->objectName(), QStringLiteral("review.comments"));
        QCOMPARE(menu->actions()[2]->objectName(), QStringLiteral("review.workspace"));
        for (const auto &id : {"review.addComment", "review.newLayer", "review.layerVisible", "review.layerLocked",
                              "review.accept", "review.reject", "review.applyAccepted", "review.exportChart",
                              "review.exportLayer", "review.start", "review.import", "review.export",
                              "review.identities", "review.recover", "review.peekBase", "review.previousConflict"})
        {
            QVERIFY(router->action(id));
            QVERIFY(!menu->actions().contains(router->action(id)));
        }
        for (const auto &id : {"review.exportChartButton", "review.exportLayerButton",
                              "review.identitiesButton", "review.recoverButton"})
        {
            auto *button = window->findChild<QPushButton *>(id);
            QVERIFY(button && workbench->primarySidebar()->isAncestorOf(button));
        }
        auto *previousConflict = window->findChild<QPushButton *>("review.previousConflictButton");
        QVERIFY(previousConflict && workbench->auxiliarySidebar()->isAncestorOf(previousConflict));
        QCOMPARE(window->findChild<QToolButton *>("classic.file.merge_charts")->defaultAction(),
                 router->action("file.merge_charts"));
        QCOMPARE(window->findChild<QToolButton *>("classic.review.saveProject")->defaultAction(),
                 router->action("review.saveProject"));

        const auto selectPage = [&](const QString &id) {
            auto *button = window->findChild<QToolButton *>("classic.page." + id);
            QVERIFY(button && button->isVisible());
            button->click();
            QVERIFY(button->isChecked());
            auto *host = workbench->paneContainerForPane(id);
            QVERIFY(host && host->paneVisible(id));
        };
        const auto screenshot = [&](const QString &name) {
            const QString directory = qEnvironmentVariable("CCE_CLASSIC_SCREENSHOT_DIR");
            if (directory.isEmpty())
                return;
            QDir().mkpath(directory);
            QTest::qWait(80);
            QVERIFY(window->grab().save(directory + "/" + name + ".png"));
        };
        screenshot("classic-editor");
        selectPage("layers");
        QCOMPARE(layers->topLevelItemCount(), 1);
        auto *opacity = window->findChild<QDoubleSpinBox *>("classicLayerOpacity");
        QVERIFY(opacity && opacity->isVisible());
        opacity->setValue(65);
        QTRY_COMPARE(layers->currentItem()->text(4), QStringLiteral("65"));
        QTest::qWait(50);
        const auto project = review->project();
        QCOMPARE(project.difficulties.value(project.activeDifficultyId).bindings.first().opacity, .65);
        QString error;
        QVERIFY2(review->identities()->createIdentity(QStringLiteral("Chart author"), &error), qPrintable(error));
        QVERIFY2(review->addCommentAtSelection(QStringLiteral("Check the spacing before the next phrase."),
                                               QStringLiteral("TimePoint"), &error), qPrintable(error));
        selectPage("comments");
        selectPage("comments");
        QCOMPARE(comments->topLevelItemCount(), 1);
        comments->setCurrentItem(comments->topLevelItem(0));
        screenshot("classic-layers-comments");
        toolbarTabs->setCurrentIndex(0);
        QVERIFY(workbench->auxiliarySidebar()->paneVisible("note"));
        QVERIFY(!workbench->auxiliarySidebar()->paneVisible("comments"));
        toolbarTabs->setCurrentIndex(4);
        QVERIFY(workbench->auxiliarySidebar()->paneVisible("review"));
        QVERIFY(dialog->isVisible());
        menu->actions()[1]->trigger();
        QVERIFY(workbench->auxiliarySidebar()->paneVisible("comments"));
        for (const auto &id : {"bpm", "meta", "note", "review", "comments"})
        {
            selectPage(id);
            QCOMPARE(window->size(), QSize(1440, 900));
        }
        router->action("review.workspace")->trigger();
        QVERIFY(workbench->auxiliarySidebar()->paneVisible("review"));
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->isWindow());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QVERIFY(dialog->isVisible());
        screenshot("classic-review");
        const QString directory = qEnvironmentVariable("CCE_CLASSIC_SCREENSHOT_DIR");
        if (!directory.isEmpty())
            QVERIFY(menu->grab().save(directory + "/project-review-menu.png"));
        selectPage("comments");

        auto *floating = window->findChild<QAction *>("view.floatingWindows");
        QVERIFY(floating);
        for (int i = 0; i < 2; ++i)
        {
            floating->setChecked(true);
            QCOMPARE(Settings::instance().classicRightPanelId(), QStringLiteral("comments"));
            QVERIFY(dialog->isWindow());
            QVERIFY(dialog->isAncestorOf(layers));
            QCOMPARE(dialog->findChild<QTabWidget *>()->count(), 5);
            floating->setChecked(false);
            workbench = window->findChild<WorkbenchLayout *>();
            QVERIFY(workbench && !dialog->isWindow());
            QVERIFY(workbench->primarySidebar()->isAncestorOf(layers));
            QVERIFY(workbench->auxiliarySidebar()->paneVisible("comments"));
            QVERIFY(workbench->primarySidebar()->paneVisible("layers"));
            QCOMPARE(window->findChildren<QTreeWidget *>("reviewLayers").size(), 1);
            QCOMPARE(comments->topLevelItemCount(), 1);
        }
        window->resize(1200, 720);
        QTest::qWait(50);
        QCOMPARE(window->size(), QSize(1200, 720));
        screenshot("classic-compact");
        window.reset();
        QCOMPARE(Settings::instance().classicRightPanelId(), QStringLiteral("comments"));
        window = std::make_unique<MainWindow>(&controller, &selection, &playback, new Skin);
        workbench = window->findChild<WorkbenchLayout *>();
        QVERIFY(workbench->auxiliarySidebar()->paneVisible("comments"));
        QVERIFY(workbench->primarySidebar()->paneVisible("layers"));
        QEvent languageChange(QEvent::LanguageChange);
        QApplication::sendEvent(window.get(), &languageChange);
        QCOMPARE(window->findChildren<QToolButton *>("classic.page.comments").size(), 1);
        QVERIFY(window->findChild<QToolButton *>("classic.page.comments")->isChecked());
        window->show();
        toolbarTabs = window->findChild<QTabBar *>("reviewToolbarTabs");
        QCOMPARE(window->findChildren<QTabBar *>("reviewToolbarTabs").size(), 1);
        QVERIFY(toolbarTabs->isVisible());
        QCOMPARE(window->findChild<QMenu *>("localReviewMenu")->actions().size(), 3);
        router = window->findChild<CommandRouter *>();
        router->action("review.workspace")->trigger();
        QSignalSpy navigation(router->action("review.previousConflict"), &QAction::triggered);
        window->findChild<QPushButton *>("review.previousConflictButton")->click();
        QCOMPARE(navigation.count(), 1);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("ClassicSidebarRegression");
    app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QTranslator translator;
    if (translator.load(qEnvironmentVariable("CCE_TEST_TRANSLATION")))
        app.installTranslator(&translator);
    ClassicSidebarTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "classic_sidebar_tests.moc"
