#include "app/MainWindow.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/CommandRouter.h"
#include "controller/PlaybackController.h"
#include "controller/SelectionController.h"
#include "model/Skin.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/NoteEditPanel.h"
#include "ui/LongRangeSelector.h"
#include "ui/dialogs/BpmMeasureDialog.h"
#include "utils/Settings.h"

#include <DockManager.h>
#include <DockWidget.h>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <memory>
#include <limits>

class ShortcutTests : public QObject
{
    Q_OBJECT
private:
    ChartController m_chart;
    SelectionController m_selection;
    AudioPlayer m_audio;
    PlaybackController m_playback{&m_audio};
    std::unique_ptr<MainWindow> m_window;
    CommandRouter *m_router = nullptr;
    ChartCanvas *m_canvas = nullptr;

    void focus(QWidget *widget)
    {
        widget->window()->show();
        widget->window()->activateWindow();
        widget->setFocus();
        QTest::qWait(30);
    }

    void resetChart()
    {
        Chart chart;
        chart.addBpm(BpmEntry(0, 0, 1, 120));
        chart.addNotes({Note(2, 0, 1, 100), Note(3, 0, 1, 200), Note(4, 0, 1, 300)});
        m_chart.loadChartFromData({}, chart);
        m_selection.clearSelection();
    }

    QHash<QString, QKeySequence> defaults() const
    {
        QHash<QString, QKeySequence> result;
        for (const auto &command : m_router->commands())
            result.insert(command.id, command.defaultBinding);
        return result;
    }

private slots:
    void initTestCase()
    {
        Settings::instance().setNoteSoundVolume(0);
        Settings::instance().setFloatingToolWindowsEnabled(true);
        resetChart();
        m_window = std::make_unique<MainWindow>(&m_chart, &m_selection, &m_playback, new Skin);
        m_router = m_window->findChild<CommandRouter *>();
        m_canvas = m_window->findChild<ChartCanvas *>();
        QVERIFY(m_router);
        QVERIFY(m_canvas);
        m_window->show();
        focus(m_canvas);
    }

    void init()
    {
        QVERIFY(m_router->applyBindings(defaults()));
        m_canvas->setNoteChainModeActive(false);
        m_canvas->setPluginToolMode(false);
        resetChart();
        focus(m_canvas);
    }

    void remappedDeleteUndoRedoAndRemovedF8()
    {
        QVERIFY(!m_router->action("playback.mark_manual_jerk"));
        QVERIFY(m_router->applyBindings({{"edit.delete", QKeySequence("Ctrl+D")},
                                        {"edit.undo", QKeySequence("Ctrl+U")},
                                        {"edit.redo", QKeySequence("Ctrl+R")}}));
        m_selection.select(0);
        QTest::keyClick(m_canvas, Qt::Key_Delete);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        QTest::keyClick(m_canvas, Qt::Key_F8);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        QTest::keyClick(m_canvas, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 2);
        QTest::keyClick(m_canvas, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 2);
        QTest::keyClick(m_canvas, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        QTest::keyClick(m_canvas, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        QTest::keyClick(m_canvas, Qt::Key_R, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 2);
        // F8 is free for a user binding after removal of the diagnostic command.
        QVERIFY(m_router->applyBindings({{"edit.delete", QKeySequence(Qt::Key_F8)}}));
        m_selection.select(0);
        QTest::keyClick(m_canvas, Qt::Key_F8);
        QCOMPARE(m_chart.chart()->notes().size(), 1);
    }

    void disablePersistsAcrossRegistryRecreationAndReset()
    {
        QVERIFY(m_router->applyBindings({{"edit.delete", QKeySequence()}}));
        m_selection.select(0);
        QTest::keyClick(m_canvas, Qt::Key_Delete);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        QVERIFY(Settings::instance().hasShortcut("edit.delete"));
        QVERIFY(Settings::instance().shortcut("edit.delete").isEmpty());
        QWidget otherWindow;
        CommandRouter restarted(&otherWindow);
        QAction deleteAction(&otherWindow);
        int activations = 0;
        connect(&deleteAction, &QAction::triggered, this, [&]() { ++activations; });
        restarted.registerAction(&deleteAction, "edit.delete", QKeySequence(Qt::Key_Delete));
        QVERIFY(deleteAction.shortcut().isEmpty());
        otherWindow.show();
        focus(&otherWindow);
        QTest::keyClick(&otherWindow, Qt::Key_Delete);
        QCOMPARE(activations, 0);
        QVERIFY(restarted.applyBindings({{"edit.delete", QKeySequence(Qt::Key_Delete)}}));
        QTest::keyClick(&otherWindow, Qt::Key_Delete);
        QCOMPARE(activations, 1);
    }

    void multiStrokeConflictAndExclusiveScopes()
    {
        const QKeySequence oldUndo = m_router->action("edit.undo")->shortcut();
        QString error;
        QVERIFY(!m_router->applyBindings({{"edit.undo", QKeySequence("Ctrl+K")},
                                         {"edit.copy", QKeySequence("Ctrl+K, Ctrl+C")}}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(m_router->action("edit.undo")->shortcut(), oldUndo);
        QVERIFY(m_router->applyBindings({{"edit.undo", QKeySequence("Ctrl+K, Ctrl+U")}}));
        m_chart.addNote(Note(6, 0, 1, 200));
        QTest::keyClick(m_canvas, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 4);
        QTest::keyClick(m_canvas, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        // Curve and process-tool commit share Enter in mutually exclusive modes.
        QVERIFY(m_router->validateBindings({{"curve.commit", QKeySequence(Qt::Key_Return)},
                                            {"plugin.commit", QKeySequence(Qt::Key_Return)}}));
        QSignalSpy curveCommit(m_router->action("curve.commit"), &QAction::triggered);
        QSignalSpy pluginCommit(m_router->action("plugin.commit"), &QAction::triggered);
        QTest::keyClick(m_canvas, Qt::Key_Return);
        QCOMPARE(curveCommit.count(), 0);
        QCOMPARE(pluginCommit.count(), 0);
        m_canvas->setPluginToolMode(true);
        QTest::keyClick(m_canvas, Qt::Key_Enter); // keypad alias
        QCOMPARE(pluginCommit.count(), 1);
        QCOMPARE(curveCommit.count(), 0);
        m_canvas->setNoteChainModeActive(true);
        QTest::keyClick(m_canvas, Qt::Key_Return);
        QCOMPARE(curveCommit.count(), 1);
        QCOMPARE(pluginCommit.count(), 1);
    }

    void arrowsAndSelectionHonorBindingsWithoutSeeking()
    {
        m_selection.select(0);
        QSignalSpy seek(&m_playback, &PlaybackController::positionChanged);
        QTest::keyClick(m_canvas, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(m_selection.selectedIndices(), (QSet<int>{0, 1}));
        QCOMPARE(seek.count(), 0);
        QVERIFY(m_router->applyBindings({{"canvas.extend_next", QKeySequence("Ctrl+Shift+L")}}));
        QTest::keyClick(m_canvas, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(m_selection.selectedIndices(), (QSet<int>{0, 1}));
        QTest::keyClick(m_canvas, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(m_selection.selectedIndices(), (QSet<int>{0, 1, 2}));
        QCOMPARE(seek.count(), 0);
        const double before = m_canvas->scrollBeat();
        auto *notePanel = m_window->findChild<NoteEditPanel *>();
        QVERIFY(notePanel);
        const int mode = notePanel->currentMode();
        QTest::keyClick(m_canvas, Qt::Key_Down, Qt::AltModifier);
        QVERIFY(notePanel->currentMode() != mode);
        QCOMPARE(m_canvas->scrollBeat(), before);
        QVERIFY(m_router->applyBindings({{"canvas.scroll_forward", QKeySequence("Ctrl+J")}}));
        QTest::keyClick(m_canvas, Qt::Key_Up);
        QCOMPARE(m_canvas->scrollBeat(), before);
        QTest::keyClick(m_canvas, Qt::Key_J, Qt::ControlModifier);
        QVERIFY(m_canvas->scrollBeat() > before);
    }

    void longRangeSelectorPreservesExactInput()
    {
        const int limit = std::numeric_limits<int>::max();
        Chart chart;
        chart.addBpm(BpmEntry(0, 0, 1, 120));
        chart.setNotes({Note(0, limit - 2, limit - 1, 400),
                        Note(0, limit - 1, limit, 10),
                        Note(0, limit - 2, limit - 1, 0, limit - 1, limit, 256)});
        QVERIFY(m_chart.loadChartFromData({}, chart));
        LongRangeSelector selector;
        selector.setChartController(&m_chart);
        selector.setSelectionController(&m_selection);
        const auto inputs = selector.findChildren<QLineEdit *>();
        QCOMPARE(inputs.size(), 2);
        const QString earlier = QString("0 %1/%2").arg(limit - 2).arg(limit - 1);
        const QString later = QString("0 %1/%2").arg(limit - 1).arg(limit);
        inputs[0]->setText(earlier);
        inputs[1]->setText(earlier);
        QVERIFY(QMetaObject::invokeMethod(&selector, "onSelectClicked", Qt::DirectConnection));
        QCOMPARE(m_selection.selectedIndices(), QSet<int>({1}));
        // Reversed inputs must swap even when both project to the same double.
        inputs[0]->setText(later);
        inputs[1]->setText(earlier);
        QVERIFY(QMetaObject::invokeMethod(&selector, "onSelectClicked", Qt::DirectConnection));
        QCOMPARE(inputs[0]->text(), earlier);
        QCOMPARE(inputs[1]->text(), later);
        QCOMPARE(m_selection.selectedIndices(), QSet<int>({0, 1, 2}));
    }

    void textInputsDialogsAndPopupsKeepTheirKeys()
    {
        QLineEdit input(m_window.get());
        input.setText("abc");
        input.show();
        focus(&input);
        m_selection.select(0);
        QSignalSpy deleted(m_router->action("edit.delete"), &QAction::triggered);
        QSignalSpy scroll(m_router->action("canvas.scroll_forward"), &QAction::triggered);
        QTest::keyClick(&input, Qt::Key_Left);
        QTest::keyClick(&input, Qt::Key_Delete);
        QCOMPARE(input.text(), QString("ab"));
        QTest::keyClick(&input, Qt::Key_Up);
        QCOMPARE(deleted.count(), 0);
        QCOMPARE(scroll.count(), 0);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
        QVERIFY(m_router->applyBindings({{"edit.delete", QKeySequence("Ctrl+D")}}));
        QTest::keyClick(&input, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(deleted.count(), 0);
        QDialog dialog(m_window.get());
        QPushButton button("Dialog button", &dialog);
        dialog.show();
        focus(&button);
        QTest::keyClick(&button, Qt::Key_D, Qt::ControlModifier);
        QTest::keyClick(&button, Qt::Key_Up);
        QCOMPARE(deleted.count(), 0);
        QCOMPARE(scroll.count(), 0);
        dialog.hide();
        focus(m_canvas);
        QMenu popup(m_window.get());
        popup.addAction("Example");
        popup.popup(m_window->mapToGlobal(QPoint(50, 50)));
        QTest::qWait(30);
        QTest::keyClick(&popup, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(deleted.count(), 0);
        popup.close();
    }

    void nativeCurveDeleteAndUndoUseSharedCommands()
    {
        m_canvas->setNoteChainModeActive(true);
        auto *editor = m_canvas->noteChainEditor();
        QVERIFY(editor);
        NoteChain::CanvasProjection projection;
        const QPointF point = projection.chartToCanvas({100, 2});
        QVERIFY(editor->handleMousePress(point, projection, NoteChain::Const::kLeftButton, false, false));
        QVERIFY(editor->handleMouseRelease(point, projection, NoteChain::Const::kLeftButton));
        editor->state().selectAnchor(editor->state().anchorAt(0).id);
        QVERIFY(editor->hasSelectedItems());
        QVERIFY(m_router->applyBindings({{"edit.delete", QKeySequence("Ctrl+D")},
                                        {"edit.undo", QKeySequence("Ctrl+U")},
                                        {"curve.delete", QKeySequence()}}));
        QTest::keyClick(m_canvas, Qt::Key_Delete);
        QTest::keyClick(m_canvas, Qt::Key_Backspace);
        QCOMPARE(editor->state().anchors().size(), 1);
        QTest::keyClick(m_canvas, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(editor->state().anchors().size(), 0);
        QTest::keyClick(m_canvas, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->state().anchors().size(), 0);
        QTest::keyClick(m_canvas, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(editor->state().anchors().size(), 1);
        QVERIFY(m_router->applyBindings({{"curve.toggle_anchor", QKeySequence("Ctrl+A")}}));
        const bool placement = editor->state().anchorPlacementEnabled();
        QTest::keyClick(m_canvas, Qt::Key_A);
        QCOMPARE(editor->state().anchorPlacementEnabled(), placement);
        QTest::keyClick(m_canvas, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(editor->state().anchorPlacementEnabled(), !placement);
    }

    void processToolUndoHonorsOverrides()
    {
        m_canvas->setPluginToolMode(true);
        QVERIFY(m_router->applyBindings({{"edit.undo", QKeySequence("Ctrl+U")}}));
        m_chart.addNote(Note(6, 0, 1, 200));
        QTest::keyClick(m_canvas, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 4);
        QTest::keyClick(m_canvas, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 3);
    }

    void bpmDialogUsesReboundLocalUndo()
    {
        QVERIFY(m_router->applyBindings({{"edit.undo", QKeySequence("Ctrl+U")}}));
        BpmMeasureDialog dialog(m_window.get());
        dialog.setMeasuredBpm(120);
        dialog.setMeasuredBpm(180);
        auto *spin = dialog.findChild<QDoubleSpinBox *>("bpmToAddSpin");
        QVERIFY(spin);
        dialog.show();
        focus(spin);
        QSignalSpy hostUndo(m_router->action("edit.undo"), &QAction::triggered);
        QTest::keyClick(spin, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(spin->value(), 180.0);
        QTest::keyClick(spin, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(spin->value(), 120.0);
        QCOMPARE(hostUndo.count(), 0);
    }

    void altReleaseClearsWheelGestureFromAnotherPanel()
    {
        auto *panel = m_window->findChild<NoteEditPanel *>();
        QVERIFY(panel);
        auto *button = panel->findChild<QPushButton *>();
        QVERIFY(button);
        focus(button);
        QSignalSpy cycles(m_canvas, &ChartCanvas::modeCycleRequested);
        const auto halfDetent = [this]() {
            QWheelEvent event(QPointF(10, 10), m_canvas->mapToGlobal(QPoint(10, 10)),
                              QPoint(), QPoint(0, 60), Qt::NoButton, Qt::AltModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(m_canvas, &event);
        };
        halfDetent();
        QCOMPARE(cycles.count(), 0);
        QTest::keyRelease(button, Qt::Key_Alt);
        halfDetent();
        QCOMPARE(cycles.count(), 0);
        halfDetent();
        QCOMPARE(cycles.count(), 1);
    }

    void menuRecreationKeepsBindingsAndOneCommandPerId()
    {
        QVERIFY(m_router->applyBindings({{"edit.copy", QKeySequence("Ctrl+Shift+C")}}));
        const int count = m_router->commands().size();
        QEvent languageChange(QEvent::LanguageChange);
        QApplication::sendEvent(m_window.get(), &languageChange);
        QCOMPARE(m_router->commands().size(), count);
        QSet<QString> ids;
        for (const auto &command : m_router->commands())
        {
            QVERIFY(!ids.contains(command.id));
            ids.insert(command.id);
        }
        QCOMPARE(m_router->action("edit.copy")->shortcut(), QKeySequence("Ctrl+Shift+C"));
        QSignalSpy copy(m_router->action("edit.copy"), &QAction::triggered);
        m_selection.select(0);
        QTest::keyClick(m_canvas, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(copy.count(), 0);
        QTest::keyClick(m_canvas, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(copy.count(), 1);
        QCOMPARE(m_selection.getClipboard().size(), 1);
    }

    void actualFloatingDockSharesCommands()
    {
        auto *dock = m_window->findChild<ads::CDockWidget *>("dock.note");
        QVERIFY(dock);
        dock->setFloating();
        QTest::qWait(80);
        auto *button = dock->widget()->findChild<QPushButton *>();
        QVERIFY(button);
        QVERIFY(button->window() != m_window.get());
        focus(button);
        m_selection.select(0);
        QVERIFY(m_router->applyBindings({{"edit.delete", QKeySequence("Ctrl+D")}}));
        QTest::keyClick(button, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 2);
        auto *panel = m_window->findChild<NoteEditPanel *>();
        QVERIFY(panel);
        const int mode = panel->currentMode();
        QTest::keyClick(button, Qt::Key_Down, Qt::AltModifier);
        QVERIFY(panel->currentMode() != mode);
    }

    void shortcutDialogKeepsEditsAfterPrefixConflict()
    {
        bool visited = false;
        bool retained = false;
        QTimer::singleShot(0, this, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            visited = true;
            auto *undo = dialog->findChild<QLineEdit *>("shortcut.edit.undo");
            auto *copy = dialog->findChild<QLineEdit *>("shortcut.edit.copy");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!undo || !copy || !buttons)
            {
                dialog->reject();
                return;
            }
            undo->setFocus();
            QTest::keyClick(undo, Qt::Key_K, Qt::ControlModifier);
            copy->setFocus();
            QTest::keyClick(copy, Qt::Key_K, Qt::ControlModifier);
            QTest::keyClick(copy, Qt::Key_C, Qt::ControlModifier);
            QTimer::singleShot(0, this, []() {
                if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    warning->accept();
            });
            buttons->button(QDialogButtonBox::Ok)->click();
            retained = dialog->isVisible() && undo->text() == "Ctrl+K" && copy->text() == "Ctrl+K, Ctrl+C";
            dialog->reject();
        });
        QVERIFY(QMetaObject::invokeMethod(m_window.get(), "configureShortcuts", Qt::DirectConnection));
        QVERIFY(visited);
        QVERIFY(retained);
        QCOMPARE(m_router->action("edit.undo")->shortcut(), QKeySequence(QKeySequence::Undo));
        QVERIFY(m_router->referenceMarkdown().contains("Select Next Note"));
        QVERIFY(!m_router->referenceMarkdown().contains("Playback Jerk"));
    }

    void cleanupTestCase()
    {
        m_window.reset();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("ShortcutRuntimeRegression");
    app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    ShortcutTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "shortcut_tests.moc"
