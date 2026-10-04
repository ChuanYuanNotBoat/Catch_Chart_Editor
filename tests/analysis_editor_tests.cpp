#include "app/MainWindow.h"
#include "ui/analysis/AnalysisEditor.h"
#include "ui/analysis/AnalysisCanvas.h"
#include "ui/analysis/TransientPanel.h"
#include "ui/analysis/TimingToolsPanel.h"
#include "ui/analysis/TimingInterpolationPanel.h"
#include "analysis/AutoTimingDiagnostics.h"
#include "audio/SpectrumService.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/NoteEditPanel.h"
#include "ui/LongRangeSelector.h"
#include "model/Skin.h"
#include "utils/Settings.h"
#include <QTest>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTabBar>
#include <QSplitter>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonArray>
#include <QAction>
#include <QMenu>
#include <QTabWidget>
#include <QLabel>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QTimer>
#include <QMessageBox>
#include <QClipboard>
#include <QAbstractButton>
#include <QFileInfo>
#include <QSpinBox>
#include <QComboBox>
#include <QBuffer>
#include <QSaveFile>
#include <memory>
#include <cmath>
class AnalysisEditorTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp;
    ChartController m_chart;
    SelectionController m_selection;
    AudioPlayer m_audio;
    PlaybackController m_playback{&m_audio};
    std::unique_ptr<MainWindow> m_main;
    std::unique_ptr<AnalysisEditor> m_editor;
    ChartCanvas *m_mainCanvas = nullptr;
    LongRangeSelector *m_range = nullptr;
    QString m_wav;
    AnalysisCanvas *canvas() const
    {
        return m_editor->canvas();
    }
    QPoint atBeat(double beat) const
    {
        return QPoint((canvas()->spectrumWidth() + canvas()->width()) / 2,
                      qRound(canvas()->yAtTime(canvas()->timeAtBeat(beat))));
    }
    void writeWav()
    {
        m_wav = m_temp.filePath("stereo-pulses.wav");
        QFile file(m_wav);
        QVERIFY(file.open(QIODevice::WriteOnly));
        constexpr int rate = 44100, seconds = 12, bytes = rate * seconds * 4;
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + bytes);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4)
            << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(bytes);
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < rate * seconds; ++i)
        {
            const double t = double(i) / rate, env = .7 * std::exp(-40 * std::fmod(t, .5));
            out << qint16(32767 * env * std::sin(2 * pi * 220 * t))
                << qint16(32767 * env * std::sin(2 * pi * 1760 * t));
        }
    }
    void loadChart(bool audio = false)
    {
        Chart chart;
        chart.bpmList().clear();
        chart.addBpm(BpmEntry(0, 0, 1, 120));
        chart.addBpm(BpmEntry(8, 0, 1, 180));
        chart.meta().offset = 125;
        if (audio)
            chart.meta().audioFile = m_wav;
        QVERIFY(m_chart.loadChartFromData(m_temp.filePath("test.mc"), chart));
        m_selection.clearSelection();
    }
    void createEditor()
    {
        m_editor =
            std::make_unique<AnalysisEditor>(&m_chart, &m_selection, &m_playback, m_mainCanvas, m_range, m_main.get());
        m_editor->show();
        m_editor->activateWindow();
        canvas()->setFocus();
        QTest::qWait(30);
    }
private slots:
    void initTestCase()
    {
        QVERIFY(m_temp.isValid());
        Settings::instance().setNoteSoundVolume(0);
        Settings::instance().setVerticalFlip(false);
        writeWav();
        loadChart();
        m_main = std::make_unique<MainWindow>(&m_chart, &m_selection, &m_playback, new Skin);
        m_mainCanvas = m_main->findChild<ChartCanvas *>();
        QVERIFY(m_mainCanvas);
        m_range = m_main->findChild<NoteEditPanel *>()->longRangeSelector();
        QVERIFY(m_range);
        m_main->show();
    }
    void init()
    {
        m_playback.pause();
        m_playback.setLoopRange(0, 0, false);
        loadChart();
        QSettings s("CatchEditor", "CatchChartEditor");
        s.remove("analysisEditor");
        Settings::instance().setShortcut("edit.undo", QKeySequence::Undo);
        Settings::instance().setShortcut("edit.redo", QKeySequence::Redo);
        m_mainCanvas->setVerticalFlip(false);
        m_mainCanvas->setTimeDivision(4);
        m_mainCanvas->setTimeScale(1);
        m_mainCanvas->setScrollPos(2000);
        m_range->setStartBeat(0);
        m_range->setEndBeat(0);
        m_range->setRangeVisible(false);
        createEditor();
    }
    void cleanup()
    {
        m_editor.reset();
        m_playback.setLoopRange(0, 0, false);
    }
    void compactAndPersistentLayout()
    {
        QVERIFY(canvas()->height() > m_editor->height() * .75);
        QVERIFY(canvas()->width() <= m_editor->width() * .67);
        QVERIFY(canvas()->width() >= m_editor->width() * .35);
        QVERIFY(canvas()->width() - canvas()->spectrumWidth() <= 56);
        QVERIFY(m_editor->findChild<QWidget *>("analysis.bottom")->height() <= 38);
        auto *tabs = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        const int initial = canvas()->width();
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
        QTest::qWait(10);
        QVERIFY(m_editor->findChild<QPushButton *>("analysis.runTiming")->isVisible());
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
        QTest::qWait(10);
        QVERIFY(!m_editor->findChild<QPushButton *>("analysis.runTiming")->isVisible());
        QVERIFY(canvas()->width() >= initial - 10);
        const QPoint divider(canvas()->spectrumWidth(), 200);
        QTest::mousePress(canvas(), Qt::LeftButton, {}, divider);
        QTest::mouseMove(canvas(), divider - QPoint(30, 0));
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, divider - QPoint(30, 0));
        QVERIFY(canvas()->spectrumWidth() < divider.x() - 20);
        canvas()->setNoteLaneWidth(84);
        canvas()->setMillisecondsPerPixel(2.5);
        auto *work = m_editor->findChild<QSplitter *>("analysis.work");
        work->setSizes({420, 720});
        const int width = canvas()->width();
        m_editor->close();
        QVERIFY(!m_editor->isVisible());
        m_editor.reset();
        createEditor();
        QCOMPARE(canvas()->noteLaneWidth(), 84);
        QCOMPARE(canvas()->millisecondsPerPixel(), 2.5);
        QVERIFY(qAbs(canvas()->width() - width) <= 3);
    }
    void manualMeasurementGesturesAndUndo()
    {
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(0).center());
        m_editor->findChild<QTabWidget *>("analysis.timingModes")->setCurrentIndex(0);
        auto *panel = m_editor->findChild<TimingToolsPanel *>();
        auto *pick = m_editor->findChild<QCheckBox *>("analysis.measurePick");
        auto *span = m_editor->findChild<QLineEdit *>("analysis.measureSpan");
        auto *apply = m_editor->findChild<QPushButton *>("analysis.measureApply");
        QVERIFY(panel && pick && span && apply);
        QCOMPARE(span->text(), QString("1"));
        canvas()->setMillisecondsPerPixel(2);
        pick->setChecked(true);
        const auto revision = m_chart.revision();
        auto at = [&](double ms) { return QPoint(80, qRound(canvas()->yAtTime(ms))); };
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, at(canvas()->timeAtBeat(4.07)));
        QCOMPARE(panel->diagnostics().value("startBeat").toArray(), QJsonArray({4, 0, 4}));
        const QPoint end = at(canvas()->timeAtBeat(4) + 310.25);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, end);
        double endMs = canvas()->timeAtY(end.y());
        QCOMPARE(panel->diagnostics().value("endAudioMilliseconds").toDouble(), endMs);
        QVERIFY(apply->isEnabled());
        QVERIFY(qAbs(panel->diagnostics().value("bpm").toDouble() - 60000 / (endMs - 1875)) < 1e-9);
        QCOMPARE(m_chart.revision(), revision);
        QVERIFY(!m_chart.canUndo());
        // End remains free even when dragged between reference grid lines.
        QTest::mousePress(canvas(), Qt::LeftButton, {}, end);
        QTest::mouseMove(canvas(), end + QPoint(0, 13));
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, end + QPoint(0, 13));
        endMs = canvas()->timeAtY(end.y() + 13);
        QCOMPARE(panel->diagnostics().value("endAudioMilliseconds").toDouble(), endMs);
        const QPoint start = at(canvas()->timeAtBeat(4));
        QTest::mousePress(canvas(), Qt::LeftButton, {}, start);
        const QPoint newStart = at(canvas()->timeAtBeat(4.52));
        QTest::mouseMove(canvas(), newStart);
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, newStart);
        QCOMPARE(panel->diagnostics().value("startBeat").toArray(), QJsonArray({4, 2, 4}));
        QTest::keyClick(canvas(), Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), span);
        span->setText("1/3");
        QVERIFY(panel->diagnostics().value("valid").toBool());
        const double fractionalBpm = 20000 / (endMs - 2125);
        QVERIFY(qAbs(panel->diagnostics().value("bpm").toDouble() - fractionalBpm) < 1e-8);
        m_editor->findChild<QPushButton *>("analysis.measureCopy")->click();
        QVERIFY(qAbs(QApplication::clipboard()->text().toDouble() - fractionalBpm) < 1e-8);
        span->setText("1/0");
        QVERIFY(!apply->isEnabled());
        span->setText("1");
        const double bpm = panel->diagnostics().value("bpm").toDouble();
        QVERIFY(apply->isEnabled());
        apply->click();
        QCOMPARE(m_chart.chart()->bpmList().size(), 3);
        const auto inserted = m_chart.chart()->bpmList()[1];
        QCOMPARE(inserted.beatNum, 4);
        QCOMPARE(inserted.numerator, 2);
        QCOMPARE(inserted.denominator, 4);
        QCOMPARE(inserted.bpm, bpm);
        QVERIFY(!panel->diagnostics().contains("startBeat"));
        QVERIFY(!canvas()->measurementPicking());
        QCOMPARE(m_chart.chart()->meta().offset, 125);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->bpmList().size(), 2);
        m_chart.redo();
        QCOMPARE(m_chart.chart()->bpmList().size(), 3);
        QCOMPARE(m_chart.chart()->bpmList()[1].bpm, bpm);
        pick->setChecked(true);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, at(2500));
        QTest::keyClick(canvas(), Qt::Key_Escape);
        QVERIFY(!canvas()->measurementPicking());
        QVERIFY(!panel->diagnostics().contains("startBeat"));
    }
    void manualMeasurementPreservesExistingTripletAndConfirmsNotes()
    {
        Chart chart;
        chart.bpmList().clear();
        chart.addBpm(BpmEntry(0, 0, 1, 120));
        chart.addBpm(BpmEntry(2, 2, 6, 120));
        chart.addBpm(BpmEntry(8, 0, 1, 180));
        chart.addNote(Note(5, 1, 3, 100));
        chart.meta().offset = 125;
        QVERIFY(m_chart.loadChartFromData(m_temp.filePath("exact.mc"), chart));
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(0).center());
        m_editor->findChild<QTabWidget *>("analysis.timingModes")->setCurrentIndex(0);
        m_mainCanvas->setScrollPos(1800);
        canvas()->setMillisecondsPerPixel(2);
        auto *panel = m_editor->findChild<TimingToolsPanel *>();
        auto *pick = m_editor->findChild<QCheckBox *>("analysis.measurePick");
        auto *span = m_editor->findChild<QLineEdit *>("analysis.measureSpan");
        auto *apply = m_editor->findChild<QPushButton *>("analysis.measureApply");
        pick->setChecked(true);
        auto at = [&](double ms) { return QPoint(80, qRound(canvas()->yAtTime(ms))); };
        const double startMs = canvas()->timeAtBeat(BeatPosition(2, 2, 6).toDouble());
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, at(startMs));
        QCOMPARE(panel->diagnostics().value("startBeat").toArray(), QJsonArray({2, 2, 6}));
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, at(startMs + 400.5));
        const double bpm = panel->diagnostics().value("bpm").toDouble();
        span->setText("8"); // cannot replace an interval containing another timing point
        QVERIFY(!apply->isEnabled());
        span->setText("1");
        auto answer = [&](QMessageBox::StandardButton button) {
            QTimer::singleShot(30, this, [button] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (dialog)
                    dialog->button(button)->click();
            });
            apply->click();
        };
        answer(QMessageBox::No);
        QCOMPARE(m_chart.chart()->bpmList()[1].bpm, 120.);
        QVERIFY(!m_chart.canUndo());
        answer(QMessageBox::Yes);
        const auto updated = m_chart.chart()->bpmList()[1];
        QCOMPARE(updated.beatNum, 2);
        QCOMPARE(updated.numerator, 2);
        QCOMPARE(updated.denominator, 6);
        QCOMPARE(updated.bpm, bpm);
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        QCOMPARE(m_chart.chart()->notes()[0], chart.notes()[0]);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->bpmList()[1].bpm, 120.);
        m_chart.redo();
        QCOMPARE(m_chart.chart()->bpmList()[1].bpm, bpm);
        // Timing edits, source replacement, vertical flip and hiding invalidate/stop picking.
        pick->setChecked(true);
        m_mainCanvas->setVerticalFlip(true);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, at(startMs));
        QCOMPARE(panel->diagnostics().value("startBeat").toArray(), QJsonArray({2, 2, 6}));
        m_chart.setMetaData(chart.meta());
        QVERIFY(!panel->diagnostics().contains("startBeat"));
        pick->setChecked(true);
        m_editor->hide();
        QVERIFY(!canvas()->measurementPicking());
    }
    void coordinatesZoomAndSharedTime()
    {
        for (double time : {1000., 2000., 3000., 5500.})
            QVERIFY(qAbs(canvas()->timeAtY(canvas()->yAtTime(time)) - time) < 1e-6);
        const double scale = m_mainCanvas->timeScale();
        canvas()->setMillisecondsPerPixel(1.5);
        QCOMPARE(m_mainCanvas->timeScale(), scale);
        m_mainCanvas->setScrollPos(2300);
        QTRY_VERIFY(qAbs(canvas()->currentTime() - 2300) < .01);
        m_mainCanvas->scrollByDivision(1, false);
        QTRY_VERIFY(qAbs(canvas()->currentTime() - m_mainCanvas->currentPlayTime()) < .01);
        m_mainCanvas->setScrollPos(2300); // bring the click target into the independent viewport
        const QPoint spectrumPoint(40, qRound(canvas()->yAtTime(1900)));
        QVERIFY(canvas()->rect().contains(spectrumPoint));
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, spectrumPoint);
        QVERIFY(qAbs(m_mainCanvas->currentPlayTime() - 1900) < 2);
        QVERIFY(qAbs(canvas()->currentTime() - m_mainCanvas->currentPlayTime()) < .1);
        canvas()->setViewSynchronized(true);
        for (double y : {80., 300., 600.})
        {
            const double mainBeat =
                m_mainCanvas->chartYToBeat((y - 28) / (canvas()->height() - 28) * m_mainCanvas->height());
            QVERIFY(qAbs(canvas()->beatAtTime(canvas()->timeAtY(y)) - mainBeat) < 1e-6);
        }
        m_mainCanvas->setVerticalFlip(true);
        QVERIFY(canvas()->timeAtY(80) > canvas()->timeAtY(500));
        canvas()->setViewSynchronized(false);
        QVERIFY(canvas()->timeAtY(80) > canvas()->timeAtY(500));
    }
    void noteEditSharedUndoAndShortcutIsolation()
    {
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atBeat(3.5));
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        const auto note = m_chart.chart()->notes().first();
        QVERIFY(qAbs(note.getStartBeat() - 3.5) < .001);
        QCOMPARE(note.x, 256);
        QTest::keyClick(canvas(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        m_chart.redo();
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        auto shifted = note;
        shifted.x = 7;
        m_chart.moveNote(note, shifted);
        QTest::mouseClick(canvas(), Qt::RightButton, {}, atBeat(3.5));
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->notes().first().x, 7);
        m_selection.select(0);
        QTest::keyClick(canvas(), Qt::Key_Delete);
        QCOMPARE(m_chart.chart()->notes().size(),
                 1); // main spatial editing command must not leak into this workspace
    }
    void normalNoteInsideRain()
    {
        m_chart.addNote(Note(1, 0, 1, 7, 0, 1, 10));
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atBeat(3.5));
        QCOMPARE(m_chart.chart()->notes().size(), 2);
        QTest::mouseClick(canvas(), Qt::RightButton, {}, atBeat(3.5));
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        QVERIFY(m_chart.chart()->notes().first().isRainNote());
    }
    void customUndoBinding()
    {
        Settings::instance().setShortcut("edit.undo", QKeySequence("Ctrl+U"));
        m_editor->hide();
        m_editor->show();
        m_editor->activateWindow();
        canvas()->setFocus();
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atBeat(3.5));
        QTest::keyClick(canvas(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        QTest::keyClick(canvas(), Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(m_chart.chart()->notes().size(), 0);
    }
    void rainPlacementTailAndTimeRange()
    {
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, QPoint(atBeat(3).x(), 14));
        auto *tools = canvas()->findChild<QMenu *>("analysis.noteTools");
        QVERIFY(tools && tools->isVisible());
        tools->actions().at(1)->trigger();
        tools->close();
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atBeat(2));
        QTest::mouseClick(canvas(), Qt::RightButton, {}, atBeat(3));
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atBeat(3));
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atBeat(5));
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        QVERIFY(m_chart.chart()->notes().first().isRainNote());
        QTest::mousePress(canvas(), Qt::LeftButton, {}, atBeat(5));
        QTest::mouseMove(canvas(), atBeat(6));
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, atBeat(6));
        QVERIFY(qAbs(m_chart.chart()->notes().first().getEndBeat() - 6) < .001);
        m_chart.undo();
        QVERIFY(qAbs(m_chart.chart()->notes().first().getEndBeat() - 5) < .001);
        QTest::mousePress(canvas(), Qt::LeftButton, Qt::ShiftModifier, atBeat(2));
        QTest::mouseMove(canvas(), atBeat(4));
        QTest::mouseRelease(canvas(), Qt::LeftButton, Qt::ShiftModifier, atBeat(4));
        QTRY_VERIFY(qAbs(m_range->currentStartBeat() - 2) < .01);
        QCOMPARE(m_range->currentEndBeat(), 4.);
        QVERIFY(m_range->isRangeVisible());
        auto *loop = m_editor->findChild<QCheckBox *>("analysis.loop");
        loop->setChecked(true);
        QVERIFY(m_playback.loopEnabled());
        QCOMPARE(m_playback.loopStartMs(), canvas()->timeAtBeat(2));
        QCOMPARE(m_playback.loopEndMs(), canvas()->timeAtBeat(4));
        m_playback.setLoopRange(100, 105, true);
        QVERIFY(!m_playback.loopEnabled());
        QVERIFY(!loop->isChecked());
        m_playback.setLoopRange(1000, 2000, true);
        QVERIFY(loop->isChecked());
        loadChart();
        QVERIFY(!m_playback.loopEnabled());
    }
    void diagnosticSemanticsAndFit()
    {
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        result.analysis.valid = true;
        AutoTiming2Candidate c;
        c.bpm = 120;
        c.hasPulseTime = true;
        c.pulseTimeSeconds = 12.345;
        c.legacyOffsetMilliseconds = -41;
        result.analysis.tempoCandidates.append(c);
        AutoTiming2TempoCurveSegment segment;
        segment.startSeconds = 10;
        segment.endSeconds = 12;
        segment.startBeat = 0;
        segment.endBeat = 4;
        segment.phaseLinear = 4;
        result.analysis.tempoMap.segments.append(segment);
        AutoTiming2TempoMapAnchor anchor;
        anchor.timeSeconds = 11.1;
        anchor.phaseBeat = 2;
        result.analysis.tempoMap.anchors.append(anchor);
        m_editor->showTimingResult(result);
        const auto raw = QJsonDocument::fromJson(
                             m_editor->findChild<QPlainTextEdit *>("analysis.rawDiagnostics")->toPlainText().toUtf8())
                             .object();
        const auto row = raw.value("tempoCandidates").toArray().first().toObject();
        QCOMPARE(row.value("pulseTimeSeconds").toDouble(), 12.345);
        QCOMPARE(row.value("legacyOffsetMilliseconds").toDouble(), -41.);
        QVERIFY(raw.value("legacyBpm").isNull());
        QVERIFY(qAbs(raw.value("derivedAnchorFit").toArray().first().toObject().value("residualMilliseconds").toDouble()
                     + 100)
                < 1e-6);
        result.analysis.tempoCandidates[0].hasPulseTime = false;
        QVERIFY(analysis::timingDiagnostics(result)
                    .value("tempoCandidates")
                    .toArray()
                    .first()
                    .toObject()
                    .value("pulseTimeSeconds")
                    .isNull());
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QVERIFY(!m_chart.canUndo());
    }
    void diagnosticWindowPreviewAndNavigation()
    {
        const auto originalBpms = m_chart.chart()->bpmList();
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        result.analysis.valid = true;
        result.analysis.durationSeconds = 4;
        result.analysisStartMs = 10000;
        AutoTiming2Candidate global;
        global.bpm = 120;
        global.legacyOffsetMilliseconds = -41;
        result.analysis.tempoCandidates.append(global);
        AutoTiming2Window window;
        window.id = 7;
        window.startSeconds = 10;
        window.endSeconds = 14;
        auto local = global;
        local.hasPulseTime = true;
        local.pulseTimeSeconds = 10.125;
        local.phaseConfidence = .8;
        window.tempoCandidates.append(local);
        result.analysis.windows.append(window);
        auto empty = window;
        empty.id = 8;
        empty.tempoCandidates.clear();
        result.analysis.windows.append(empty);
        m_editor->showTimingResult(result);
        auto *preview = m_editor->findChild<QCheckBox *>("analysis.candidateGrid");
        auto *go = m_editor->findChild<QPushButton *>("analysis.candidateSeek");
        auto *windows = m_editor->findChild<QTableWidget *>("analysis.table.windows");
        auto *locals = m_editor->findChild<QTableWidget *>("analysis.table.windowCandidates");
        QVERIFY(!preview->isEnabled());
        QVERIFY(!go->isEnabled());
        QVERIFY(!canvas()->timingPreviewVisible());
        windows->setCurrentCell(0, 0);
        QCOMPARE(locals->rowCount(), 1);
        QVERIFY(preview->isEnabled());
        QVERIFY(!preview->isChecked());
        go->click();
        QVERIFY(qAbs(m_mainCanvas->currentPlayTime() - 10125) < .01);
        QVERIFY(qAbs(canvas()->currentTime() - 10125) < .01);
        m_mainCanvas->setScrollPos(12000);
        m_editor->findChild<QDoubleSpinBox *>("analysis.zoom")->setValue(10);
        const double probeTime = 10625;
        const auto originalSnap = canvas()->snappedNoteAtY(canvas()->yAtTime(probeTime)).getStartBeat();
        preview->setChecked(true);
        QVERIFY(canvas()->timingPreviewVisible());
        QCOMPARE(canvas()->snappedNoteAtY(canvas()->yAtTime(probeTime)).getStartBeat(), originalSnap);
        const QImage image = canvas()->grab().toImage();
        auto hasCyanLine = [&](double time) {
            const int row = qRound(canvas()->yAtTime(time));
            for (int y = qMax(28, row - 1); y <= qMin(image.height() - 1, row + 1); ++y)
                for (int x = 50; x < qMin(200, canvas()->spectrumWidth()); ++x)
                {
                    const auto color = image.pixelColor(x, y);
                    if (color.red() < 110 && color.green() > 150 && color.blue() > 170)
                        return true;
                }
            return false;
        };
        QVERIFY(hasCyanLine(probeTime));
        QVERIFY(!hasCyanLine(9625));
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(0).center());
        auto *tables = m_editor->findChild<QTabWidget *>("analysis.timingTables");
        tables->setCurrentWidget(windows);
        QTest::mouseClick(windows->viewport(), Qt::LeftButton, {},
                          windows->visualItemRect(windows->item(0, 0)).center());
        QTest::mouseDClick(windows->viewport(), Qt::LeftButton, {},
                           windows->visualItemRect(windows->item(0, 0)).center());
        QCOMPARE(tables->currentWidget(), locals);
        QVERIFY(qAbs(m_mainCanvas->currentPlayTime() - 10000) < .01);
        auto *globalTable = m_editor->findChild<QTableWidget *>("analysis.table.tempoCandidates");
        tables->setCurrentWidget(globalTable);
        QTest::mouseClick(globalTable->viewport(), Qt::LeftButton, {},
                          globalTable->visualItemRect(globalTable->item(0, 0)).center());
        QVERIFY(!preview->isEnabled());
        QVERIFY(!canvas()->timingPreviewVisible());
        tables->setCurrentWidget(windows);
        QTest::mouseClick(windows->viewport(), Qt::LeftButton, {},
                          windows->visualItemRect(windows->item(0, 0)).center());
        QVERIFY(preview->isEnabled());
        preview->setChecked(true);
        windows->setCurrentCell(1, 0);
        QCOMPARE(locals->rowCount(), 0);
        QVERIFY(!preview->isEnabled());
        QVERIFY(!preview->isChecked());
        QVERIFY(!canvas()->timingPreviewVisible());
        QVERIFY(m_editor->findChild<QLabel *>("analysis.candidateDetails")->text().contains("#8"));
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QVERIFY(!m_chart.canUndo());
        QCOMPARE(m_chart.chart()->meta().offset, 125.);
        const auto &bpms = m_chart.chart()->bpmList();
        QCOMPARE(bpms.size(), originalBpms.size());
        for (int i = 0; i < bpms.size(); ++i)
        {
            QCOMPARE(bpms[i].beatNum, originalBpms[i].beatNum);
            QCOMPARE(bpms[i].numerator, originalBpms[i].numerator);
            QCOMPARE(bpms[i].denominator, originalBpms[i].denominator);
            QCOMPARE(bpms[i].bpm, originalBpms[i].bpm);
        }
    }
    void realAudioAndTimingPipeline()
    {
        auto result = SpectrumService::analyzeFileRange(m_wav, 2000, 2500, {});
        QVERIFY2(result.valid(), result.error.c_str());
        QCOMPARE(result.sourceChannels, 2);
        QVERIFY(qAbs(result.startSeconds - 2) < .001);
        QVERIFY(result.leftDb != result.rightDb);
        QVERIFY(qAbs(result.durationSeconds - 2.5) < .001);
        loadChart(true);
        QTRY_VERIFY_WITH_TIMEOUT(canvas()->spectrum().valid(), 15000);
        auto *transient = m_editor->findChild<TransientPanel *>();
        QVERIFY(transient);
        QTRY_VERIFY_WITH_TIMEOUT(transient->result().valid(), 15000);
        QVERIFY(!transient->result().peaks.empty());
        auto *tabs = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
        m_editor->findChild<QDoubleSpinBox *>("analysis.timingDuration")->setValue(12);
        auto *run = m_editor->findChild<QPushButton *>("analysis.runTiming");
        run->click();
        QVERIFY(!run->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 45000);
        const auto raw = QJsonDocument::fromJson(
                             m_editor->findChild<QPlainTextEdit *>("analysis.rawDiagnostics")->toPlainText().toUtf8())
                             .object();
        QCOMPARE(raw.value("analysisStatus").toString(), QString("Succeeded"));
        QVERIFY(raw.value("windows").toArray().size() > 0);
        QCOMPARE(raw.value("timeline").toString(), QString("whole-file audio seconds"));
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QVERIFY(!m_chart.canUndo());
        const QString screenshots = qEnvironmentVariable("CCE_ANALYSIS_SCREENSHOT_DIR");
        auto *windows = m_editor->findChild<QTableWidget *>("analysis.table.windows");
        windows->setCurrentCell(0, 0);
        auto *locals = m_editor->findChild<QTableWidget *>("analysis.table.windowCandidates");
        QVERIFY(locals->rowCount() > 0);
        auto *preview = m_editor->findChild<QCheckBox *>("analysis.candidateGrid");
        QVERIFY(preview->isEnabled());
        preview->setChecked(true);
        QVERIFY(canvas()->timingPreviewVisible());
        if (!screenshots.isEmpty())
        {
            QDir().mkpath(screenshots);
            preview->setChecked(false);
            m_chart.addNotes({Note(2, 0, 1, 32), Note(3, 1, 2, 470), Note(4, 1, 2, 128), Note(4, 0, 1, 5, 1, 2, 320)});
            m_selection.select(2);
            QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/01-default.png"));
            canvas()->setMillisecondsPerPixel(2);
            m_mainCanvas->setScrollPos(2800);
            m_range->setStartBeat(4);
            m_range->setEndBeat(5.5);
            m_range->setRangeVisible(true);
            m_playback.setLoopRange(canvas()->timeAtBeat(4), canvas()->timeAtBeat(5.5), true);
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/02-closeup.png"));
            m_playback.setLoopRange(0, 0, false);
            m_range->setRangeVisible(false);
            canvas()->setMillisecondsPerPixel(6);
            m_mainCanvas->setScrollPos(2000);
            QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
            auto *tables = m_editor->findChild<QTabWidget *>("analysis.timingTables");
            auto *global = m_editor->findChild<QTableWidget *>("analysis.table.tempoCandidates");
            tables->setCurrentWidget(global);
            QTest::mouseClick(global->viewport(), Qt::LeftButton, {},
                              global->visualItemRect(global->item(0, 0)).center());
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/03-timing.png"));
            tables->setCurrentWidget(windows);
            QTest::mouseClick(windows->viewport(), Qt::LeftButton, {},
                              windows->visualItemRect(windows->item(0, 0)).center());
            QTest::mouseDClick(windows->viewport(), Qt::LeftButton, {},
                               windows->visualItemRect(windows->item(0, 0)).center());
            preview->setChecked(true);
            QVERIFY(canvas()->timingPreviewVisible());
            canvas()->setMillisecondsPerPixel(5);
            m_mainCanvas->setScrollPos(2000);
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/04-window-preview.png"));
            preview->setChecked(false);
            QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
            canvas()->setMillisecondsPerPixel(6);
            m_editor->resize(1460, 860);
            auto *right = m_editor->findChild<QTabBar *>("analysis.rightTabs");
            QTest::mouseClick(right, Qt::LeftButton, {}, right->tabRect(2).center());
            m_editor->findChild<QSplitter *>("analysis.panels")->setSizes({0, 800, 600});
            m_editor->findChild<QSplitter *>("analysis.work")->setSizes({620, 180});
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/05-diagnostics.png"));
            QTest::mouseClick(right, Qt::LeftButton, {}, right->tabRect(2).center());
            QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(1).center());
            m_editor->findChild<QSplitter *>("analysis.panels")->setSizes({450, 950, 0});
            m_editor->findChild<QSplitter *>("analysis.work")->setSizes({580, 370});
            m_mainCanvas->setScrollPos(2000);
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/06-transient-curves.png"));
            auto *transientTabs = m_editor->findChild<QTabWidget *>("analysis.transientTabs");
            transientTabs->setCurrentIndex(1);
            QTest::qWait(60);
            QVERIFY(m_editor->grab().save(screenshots + "/07-transient-peaks.png"));
            while (m_chart.canUndo())
                m_chart.undo();
            QCOMPARE(m_chart.chart()->notes().size(), 0);
        }
        run->click();
        QVERIFY(!run->isEnabled());
        loadChart();
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 45000);
        QVERIFY(m_editor->findChild<QPlainTextEdit *>("analysis.rawDiagnostics")->toPlainText().isEmpty());
        QVERIFY(!canvas()->spectrum().valid());
        QVERIFY(!canvas()->timingPreviewVisible());
        QVERIFY(!preview->isChecked());
        QCOMPARE(locals->rowCount(), 0);
        QVERIFY(!transient->result().valid());
    }
    void transientParametersNavigationAndStaleResults()
    {
        auto *panel = m_editor->findChild<TransientPanel *>();
        auto *peaks = m_editor->findChild<QTableWidget *>("analysis.transientPeaks");
        auto *threshold = m_editor->findChild<QDoubleSpinBox *>("analysis.transientThreshold");
        auto *interval = m_editor->findChild<QDoubleSpinBox *>("analysis.transientInterval");
        auto *rejected = m_editor->findChild<QCheckBox *>("analysis.transientRejected");
        QVERIFY(panel && peaks && threshold && interval && rejected);
        loadChart(true);
        QTRY_VERIFY_WITH_TIMEOUT(panel->result().valid(), 15000);
        const int all = peaks->rowCount();
        QVERIFY(all > 5);
        auto detectedCount = [&] {
            return std::count_if(panel->result().peaks.begin(), panel->result().peaks.end(), [](const auto &p) {
                return p.disposition == analysis::PeakDisposition::Detected;
            });
        };
        const auto defaultCount = detectedCount();
        QVERIFY(defaultCount >= 5);
        const auto json = panel->diagnostics();
        QCOMPARE(json.value("timeline").toString(), QString("whole-file audio seconds"));
        QCOMPARE(json.value("peaks").toArray().size(), int(panel->result().peaks.size()));
        const auto firstPeak = json.value("peaks").toArray().first().toObject();
        QCOMPARE(firstPeak.value("timeSeconds").toDouble(), panel->result().peaks.front().timeSeconds);
        QVERIFY(firstPeak.contains("disposition") && firstPeak.contains("widthSeconds"));
        rejected->setChecked(false);
        QCOMPARE(peaks->rowCount(), int(defaultCount));
        rejected->setChecked(true);
        QCOMPARE(peaks->rowCount(), all);
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(1).center());
        auto *tabs = m_editor->findChild<QTabWidget *>("analysis.transientTabs");
        tabs->setCurrentIndex(1);
        peaks->setCurrentCell(2, 0);
        const double time = peaks->item(2, 0)->data(Qt::UserRole).toDouble();
        QTest::mouseClick(peaks->viewport(), Qt::LeftButton, {}, peaks->visualItemRect(peaks->item(2, 0)).center());
        QTest::mouseDClick(peaks->viewport(), Qt::LeftButton, {}, peaks->visualItemRect(peaks->item(2, 0)).center());
        QVERIFY(qAbs(canvas()->currentTime() - time * 1000) < .01);
        QVERIFY(qAbs(m_mainCanvas->currentPlayTime() - time * 1000) < .01);
        QVERIFY(!canvas()->viewSynchronized());
        QCOMPARE(canvas()->millisecondsPerPixel(), 1.);
        tabs->setCurrentIndex(0);
        auto *plot = m_editor->findChild<QWidget *>("analysis.transientPlot");
        const double expected = canvas()->timeAtY(28 + .5 * (canvas()->height() - 28));
        QTest::mouseClick(plot, Qt::LeftButton, {}, QPoint(plot->width() / 2, qRound(24 + .5 * (plot->height() - 42))));
        QVERIFY(qAbs(canvas()->currentTime() - expected) < 3);
        // Rapid parameter edits must finish with only the latest options visible.
        threshold->setValue(.95);
        interval->setValue(2000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->result().valid() && !panel->busy(), 10000);
        QVERIFY(detectedCount() < defaultCount);
        QCOMPARE(panel->diagnostics().value("minimumIntervalSeconds").toDouble(), 2.);
        threshold->setValue(.12);
        interval->setValue(60);
        QTRY_VERIFY_WITH_TIMEOUT(panel->result().valid() && !panel->busy(), 10000);
        QCOMPARE(detectedCount(), defaultCount);
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QVERIFY(!m_chart.canUndo());
        QCOMPARE(m_chart.chart()->meta().offset, 125.);
        // Supersede a pending job with a different source, without accepting its old results.
        auto large = canvas()->spectrum();
        const auto left = large.leftDb, right = large.rightDb;
        large.frames.resize(12000);
        large.durationSeconds = large.hopSeconds * 12000;
        large.leftDb.clear();
        large.rightDb.clear();
        for (int i = 0; i < 10; ++i)
        {
            large.leftDb.insert(large.leftDb.end(), left.begin(), left.end());
            large.rightDb.insert(large.rightDb.end(), right.begin(), right.end());
        }
        panel->setSpectrum(large);
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     const auto watchers = panel->findChildren<QFutureWatcherBase *>();
                                     return std::any_of(watchers.begin(), watchers.end(), [](auto *w) {
                                         return w->isRunning();
                                     });
                                 })(),
                                 10000);
        loadChart();
        QTRY_VERIFY_WITH_TIMEOUT(!panel->busy(), 10000);
        QVERIFY(!panel->result().valid());
        QVERIFY(panel->diagnostics().isEmpty());
        QCOMPARE(peaks->rowCount(), 0);
    }
    void realPlaybackAndLoop()
    {
        loadChart(true);
        QVERIFY(m_audio.load(m_wav));
        QTRY_VERIFY_WITH_TIMEOUT(m_audio.canPlay(), 10000);
        m_playback.setLoopRange(400, 800, true);
        QSignalSpy ticks(&m_playback, &PlaybackController::playbackFrameTick);
        const auto ack =
            connect(&m_playback, &PlaybackController::playbackFrameTick, &m_playback, [this](double, qint64 seq) {
                m_playback.acknowledgeFramePainted(seq);
            });
        m_playback.playFromTime(400);
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() >= 10, 5000);
        QTest::qWait(950);
        m_playback.pause();
        disconnect(ack);
        bool wrapped = false;
        double previous = -1;
        for (const auto &tick : ticks)
        {
            const double time = tick[0].toDouble();
            if (previous > 600 && time < 550)
                wrapped = true;
            previous = time;
        }
        QVERIFY(wrapped);
        QVERIFY(qAbs(canvas()->currentTime() - m_mainCanvas->currentPlayTime()) < 25);
    }
    void interpolationPreservesTripletsAndUsesOneTimingUndo()
    {
        Chart fixture;
        fixture.bpmList().clear();
        fixture.addBpm(BpmEntry(0, 0, 1, 120));
        fixture.addBpm(BpmEntry(2, 2, 6, 120));
        fixture.addBpm(BpmEntry(10, 2, 6, 155));
        fixture.addBpm(BpmEntry(12, 0, 1, 220));
        fixture.meta().offset = 125;
        fixture.addNote(Note(3, 1, 7, 200));
        QVERIFY(m_chart.loadChartFromData(m_temp.filePath("interpolation.mc"), fixture));
        const auto before = m_chart.chart()->bpmList();
        const auto notes = m_chart.chart()->notes();
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(0).center());
        auto *modes = m_editor->findChild<QTabWidget *>("analysis.timingModes");
        modes->setCurrentIndex(2);
        auto *panel = m_editor->findChild<TimingInterpolationPanel *>();
        auto *apply = panel->findChild<QPushButton *>("analysis.interpolationApply");
        auto *pick = panel->findChild<QCheckBox *>("analysis.interpolationPick");
        QVERIFY(panel && apply && pick);
        canvas()->setMillisecondsPerPixel(4);
        m_mainCanvas->setScrollPos(1400);
        pick->setChecked(true);
        const double startMs = canvas()->timeAtBeat(2. + 1. / 3);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, QPoint(80, qRound(canvas()->yAtTime(startMs))));
        QVERIFY(!pick->isChecked());
        QCOMPARE(panel->findChild<QSpinBox *>("analysis.interpolationStartWhole")->value(), 2);
        QCOMPARE(panel->findChild<QSpinBox *>("analysis.interpolationStartNumerator")->value(), 2);
        QCOMPARE(panel->findChild<QSpinBox *>("analysis.interpolationStartDenominator")->value(), 6);
        panel->findChild<QComboBox *>("analysis.interpolationRangeMode")->setCurrentIndex(1);
        panel->findChild<QSpinBox *>("analysis.interpolationEndWhole")->setValue(10);
        panel->findChild<QSpinBox *>("analysis.interpolationEndNumerator")->setValue(2);
        panel->findChild<QSpinBox *>("analysis.interpolationEndDenominator")->setValue(6);
        panel->findChild<QDoubleSpinBox *>("analysis.interpolationStartBpm")->setValue(120);
        panel->findChild<QDoubleSpinBox *>("analysis.interpolationEndBpm")->setValue(240);
        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(canvas()->interpolationPreviewVisible());
        const auto diagnostic = panel->diagnostics();
        const auto nodes = diagnostic.value("nodes").toArray();
        QCOMPARE(nodes.first().toObject().value("beat").toArray(), QJsonArray({2, 2, 6}));
        QCOMPARE(nodes.last().toObject().value("beat").toArray(), QJsonArray({10, 2, 6}));
        QCOMPARE(nodes.last().toObject().value("storedBpm").toDouble(), 155.);
        QVERIFY(diagnostic.value("maximumInteriorErrorMilliseconds").toDouble() < 10);
        QCOMPARE(diagnostic.value("replacedTimingPoints").toInt(), 2);
        const auto revision = m_chart.revision();
        auto answer = [&](QMessageBox::StandardButton choice) {
            QTimer::singleShot(30, [choice] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QCOMPARE(dialog->defaultButton(), qobject_cast<QPushButton *>(dialog->button(QMessageBox::No)));
                dialog->button(choice)->click();
            });
            apply->click();
        };
        answer(QMessageBox::No);
        QCOMPARE(m_chart.revision(), revision);
        QVERIFY(!m_chart.canUndo());
        QSignalSpy changes(&m_chart, &ChartController::chartChangeCommitted);
        QSignalSpy noteChanges(&m_chart, &ChartController::notesChanged);
        answer(QMessageBox::Yes);
        QCOMPARE(changes.size(), 1);
        const auto change = qvariant_cast<ChartChange>(changes.first().first());
        QCOMPARE(change.types, ChartChangeSet(ChartChangeType::Timing));
        QCOMPARE(noteChanges.size(), 0);
        QCOMPARE(m_chart.chart()->bpmList().size(), nodes.size() + 2);
        const auto &after = m_chart.chart()->bpmList();
        QCOMPARE(after.first().bpm, 120.);
        QCOMPARE(after.last().position(), before.last().position());
        QCOMPARE(after.last().bpm, 220.);
        QCOMPARE(after[1].beatNum, 2);
        QCOMPARE(after[1].numerator, 2);
        QCOMPARE(after[1].denominator, 6);
        QCOMPARE(m_chart.chart()->notes(), notes);
        QCOMPARE(m_chart.chart()->meta().offset, 125.);
        QVERIFY(qAbs(canvas()->timeAtBeat(10. + 1. / 3) - startMs
                     - diagnostic.value("durationMilliseconds").toDouble()) < 1e-6);
        QVERIFY(!apply->isEnabled());
        QVERIFY(!canvas()->interpolationPreviewVisible());
        m_chart.undo();
        QVERIFY(!m_chart.canUndo());
        QCOMPARE(m_chart.chart()->bpmList().size(), before.size());
        for (int i = 0; i < before.size(); ++i)
        {
            const auto &point = m_chart.chart()->bpmList()[i];
            QCOMPARE(point.beatNum, before[i].beatNum);
            QCOMPARE(point.numerator, before[i].numerator);
            QCOMPARE(point.denominator, before[i].denominator);
            QCOMPARE(point.bpm, before[i].bpm);
        }
        m_chart.redo();
        QCOMPARE(m_chart.chart()->bpmList().size(), nodes.size() + 2);
        QCOMPARE(m_chart.chart()->notes(), notes);
    }
    void interpolationRangesAndStaleProposal()
    {
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(0).center());
        m_editor->findChild<QTabWidget *>("analysis.timingModes")->setCurrentIndex(2);
        auto *panel = m_editor->findChild<TimingInterpolationPanel *>();
        auto *apply = panel->findChild<QPushButton *>("analysis.interpolationApply");
        auto *mode = panel->findChild<QComboBox *>("analysis.interpolationRangeMode");
        auto *resume = panel->findChild<QCheckBox *>("analysis.interpolationResume");
        auto *gap = panel->findChild<QLineEdit *>("analysis.interpolationGap");
        auto *length = panel->findChild<QLineEdit *>("analysis.interpolationLength");
        panel->findChild<QDoubleSpinBox *>("analysis.interpolationStartBpm")->setValue(120);
        panel->findChild<QDoubleSpinBox *>("analysis.interpolationEndBpm")->setValue(240);
        length->setText("8");
        QTRY_VERIFY(apply->isEnabled());
        const double duration = panel->diagnostics().value("durationMilliseconds").toDouble();
        mode->setCurrentIndex(2);
        panel->findChild<QDoubleSpinBox *>("analysis.interpolationDuration")->setValue(duration);
        resume->setChecked(false);
        QTRY_VERIFY(apply->isEnabled());
        const auto diagnostic = panel->diagnostics();
        QVERIFY(qAbs(diagnostic.value("durationResidualMilliseconds").toDouble()) < .001);
        QCOMPARE(diagnostic.value("nodes").toArray().last().toObject().value("storedBpm").toDouble(), 240.);
        gap->setText("0");
        QVERIFY(!apply->isEnabled());
        QTRY_VERIFY(!panel->diagnostics().value("failure").toString().isEmpty());
        QVERIFY(!canvas()->interpolationPreviewVisible());
        gap->setText("1");
        QTRY_VERIFY(apply->isEnabled());
        // A note edit while the modal confirmation is open must supersede the
        // pending proposal. Only the note edit enters the shared undo stack.
        const auto before = m_chart.chart()->bpmList();
        QTimer::singleShot(30, [this] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            m_chart.addNote(Note(4, 1, 5, 300));
            dialog->button(QMessageBox::Yes)->click();
        });
        apply->click();
        QCOMPARE(m_chart.chart()->bpmList().size(), before.size());
        QCOMPARE(m_chart.chart()->bpmList().first().bpm, before.first().bpm);
        QCOMPARE(m_chart.chart()->notes().size(), 1);
        QVERIFY(!apply->isEnabled());
        QVERIFY(!canvas()->interpolationPreviewVisible());
        m_chart.undo();
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QVERIFY(!m_chart.canUndo());
        // Duplicate source points inside the replacement range still require
        // repair; preview must not silently guess their active BPM.
        Chart duplicate;
        duplicate.bpmList() = {BpmEntry(0, 0, 1, 120), BpmEntry(2, 1, 3, 160), BpmEntry(2, 2, 6, 180)};
        QVERIFY(m_chart.loadChartFromData(m_temp.filePath("duplicate-timing.mc"), duplicate));
        QTRY_VERIFY(panel->diagnostics().value("failure").toString().contains("duplicate"));
        QVERIFY(!apply->isEnabled());
        QVERIFY(!canvas()->interpolationPreviewVisible());
        QVERIFY(!m_chart.canUndo());
    }
    void timingReplacementRejectsInvalidListsAndKeepsNoteHistory()
    {
        const auto revision = m_chart.revision();
        const auto before = m_chart.chart()->bpmList();
        QVERIFY(!m_chart.replaceBpmList("Invalid", {}));
        QVERIFY(!m_chart.replaceBpmList("No-op", before));
        QVERIFY(!m_chart.replaceBpmList("Duplicate", {BpmEntry(0, 0, 1, 120), BpmEntry(0, 0, 2, 180)}));
        QVERIFY(!m_chart.replaceBpmList("Negative", {BpmEntry(-1, 0, 1, 120)}));
        QVERIFY(!m_chart.replaceBpmList("NaN", {BpmEntry(0, 0, 1, std::nan(""))}));
        QCOMPARE(m_chart.revision(), revision);
        QVERIFY(!m_chart.canUndo());
        const QVector<BpmEntry> proposed{BpmEntry(0, 0, 1, 130), BpmEntry(4, 2, 6, 200)};
        QVERIFY(m_chart.replaceBpmList("Interpolate BPM", proposed));
        const Note note(4, 2, 6, 240);
        m_chart.addNote(note);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QCOMPARE(m_chart.chart()->bpmList().first().bpm, 130.);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->bpmList().first().bpm, 120.);
        QVERIFY(!m_chart.canUndo());
        m_chart.redo();
        m_chart.redo();
        QCOMPARE(m_chart.chart()->notes().first(), note);
        QCOMPARE(m_chart.chart()->bpmList().last().denominator, 6);
        QCOMPARE(m_chart.chart()->bpmList().last().bpm, 200.);
    }
    // Optional integration case: no copyrighted audio is included in the repository.
    // Set CCE_ANALYSIS_AUDIO_FILE to inspect an actual audio file through production Qt APIs.
    void externalAudioInspection()
    {
        const QString path = qEnvironmentVariable("CCE_ANALYSIS_AUDIO_FILE");
        if (path.isEmpty())
            QSKIP("Set CCE_ANALYSIS_AUDIO_FILE to inspect user-supplied audio");
        QVERIFY(QFileInfo::exists(path));
        const QString output = qEnvironmentVariable("CCE_ANALYSIS_SCREENSHOT_DIR");
        if (!output.isEmpty())
            QVERIFY(QDir().mkpath(output));
        Chart chart;
        chart.bpmList().clear();
        chart.addBpm(BpmEntry(0, 0, 1, 120)); // blank test chart, not inferred song timing
        chart.meta().offset = 0;
        chart.meta().audioFile = path;
        chart.meta().title = QFileInfo(path).completeBaseName();
        QVERIFY(m_chart.loadChartFromData(m_temp.filePath("external.mc"), chart));
        QTRY_VERIFY_WITH_TIMEOUT(m_audio.duration() > 1000 && m_audio.canPlay(), 15000);
        const double duration = m_audio.duration() / 1000.;
        auto *panel = m_editor->findChild<TransientPanel *>();
        QVERIFY(panel);
        QJsonObject report{{"fileName", QFileInfo(path).fileName()},
                           {"audioDurationSeconds", duration},
                           {"chartGrid", "blank test chart at 120 BPM; not song ground truth"}};
        QJsonArray inspected;
        // Early, middle, late and EOF ranges check seeking/cropping in a long FLAC.
        for (double start : {0., duration * .4, std::max(0., duration - 12), std::max(0., duration - 2)})
        {
            QElapsedTimer elapsed;
            elapsed.start();
            const auto spectrum = SpectrumService::analyzeFileRange(path, start * 1000, 8000, {});
            QVERIFY2(spectrum.valid(), spectrum.error.c_str());
            QVERIFY(spectrum.sourceChannels > 0);
            QVERIFY(spectrum.sampleRate >= 1000 && spectrum.sampleRate <= 384000);
            QVERIFY(qAbs(spectrum.startSeconds - start) < .001);
            QVERIFY(qAbs(spectrum.durationSeconds - std::min(8., duration - start)) < .02);
            for (float value : spectrum.leftDb)
                QVERIFY(std::isfinite(value));
            for (float value : spectrum.rightDb)
                QVERIFY(std::isfinite(value));
            const auto result = analysis::analyzeTransients(spectrum);
            QVERIFY2(result.valid(), result.error.c_str());
            int detected = 0;
            for (const auto &peak : result.peaks)
            {
                QVERIFY(peak.timeSeconds >= spectrum.startSeconds);
                QVERIFY(peak.timeSeconds < spectrum.startSeconds + spectrum.durationSeconds);
                QVERIFY(std::isfinite(peak.strength) && std::isfinite(peak.widthSeconds));
                if (peak.disposition == analysis::PeakDisposition::Detected)
                    ++detected;
            }
            inspected.append(QJsonObject{{"requestedStartSeconds", start},
                                         {"decodedStartSeconds", spectrum.startSeconds},
                                         {"decodedDurationSeconds", spectrum.durationSeconds},
                                         {"hopMilliseconds", spectrum.hopSeconds * 1000},
                                         {"sampleRate", spectrum.sampleRate},
                                         {"channels", spectrum.sourceChannels},
                                         {"stereoBandsDiffer", spectrum.leftDb != spectrum.rightDb},
                                         {"frames", int(spectrum.frames.size())},
                                         {"detected", detected},
                                         {"rejected", int(result.peaks.size()) - detected},
                                         {"decodeAndAnalysisMilliseconds", elapsed.elapsed()}});
        }
        report["inspectedRanges"] = inspected;
        const double center = std::floor(duration * .4);
        m_editor->resize(1460, 860);
        m_mainCanvas->setScrollPos(center * 1000);
        canvas()->setMillisecondsPerPixel(4);
        QTRY_VERIFY_WITH_TIMEOUT(canvas()->spectrum().valid() && canvas()->spectrum().startSeconds <= center
                                     && canvas()->spectrum().startSeconds + canvas()->spectrum().durationSeconds
                                            > center
                                     && panel->result().valid() && !panel->busy(),
                                 30000);
        QVERIFY(m_editor->findChild<QLabel *>("analysis.audioName")->text().contains(QFileInfo(path).fileName()));
        auto saveJson = [&](const QString &name, const QJsonObject &object) {
            if (output.isEmpty())
                return;
            QSaveFile file(output + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto data = QJsonDocument(object).toJson();
            QCOMPARE(file.write(data), qint64(data.size()));
            QVERIFY(file.commit());
        };
        auto savePixmap = [&](const QPixmap &pixmap, const QString &name) {
            if (output.isEmpty())
                return true;
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (!buffer.open(QIODevice::WriteOnly) || !pixmap.save(&buffer, "PNG")
                || !bytes.endsWith(QByteArray::fromHex("0000000049454e44ae426082")))
                return false;
            QSaveFile file(output + '/' + name);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                return false;
            QFile persisted(output + '/' + name);
            return persisted.open(QIODevice::ReadOnly) && persisted.readAll() == bytes;
        };
        auto capture = [&](const QString &name) {
            if (output.isEmpty())
                return;
            QTest::qWait(80);
            QVERIFY(savePixmap(m_editor->grab(), name));
        };
        capture("01-spectrum.png");
        auto *side = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(1).center());
        m_editor->findChild<QSplitter *>("analysis.panels")->setSizes({470, 950, 0});
        m_editor->findChild<QSplitter *>("analysis.work")->setSizes({580, 370});
        capture("02-transient-curves.png");
        const auto transientJson = panel->diagnostics();
        const int all = int(panel->result().peaks.size());
        QVERIFY(all > 0);
        auto *threshold = m_editor->findChild<QDoubleSpinBox *>("analysis.transientThreshold");
        auto *interval = m_editor->findChild<QDoubleSpinBox *>("analysis.transientInterval");
        interval->setValue(120);
        threshold->setValue(.35);
        QTRY_VERIFY_WITH_TIMEOUT(panel->result().valid() && !panel->busy(), 10000);
        report["changedParameterTransient"] = panel->diagnostics();
        interval->setValue(60);
        threshold->setValue(.12);
        QTRY_VERIFY_WITH_TIMEOUT(panel->result().valid() && !panel->busy(), 10000);
        QCOMPARE(int(panel->result().peaks.size()), all);
        auto *transientTabs = m_editor->findChild<QTabWidget *>("analysis.transientTabs");
        transientTabs->setCurrentIndex(1);
        auto *peaks = m_editor->findChild<QTableWidget *>("analysis.transientPeaks");
        int near = 0;
        while (near + 1 < peaks->rowCount() && peaks->item(near, 0)->data(Qt::UserRole).toDouble() < center)
            ++near;
        peaks->setCurrentCell(near, 0);
        peaks->scrollToItem(peaks->item(near, 0), QAbstractItemView::PositionAtCenter);
        QTest::qWait(30);
        const double selected = peaks->item(near, 0)->data(Qt::UserRole).toDouble();
        const QPoint point = peaks->visualItemRect(peaks->item(near, 0)).center();
        QTest::mouseClick(peaks->viewport(), Qt::LeftButton, {}, point);
        QTest::mouseDClick(peaks->viewport(), Qt::LeftButton, {}, point);
        QVERIFY(qAbs(canvas()->currentTime() - selected * 1000) < .01);
        QVERIFY(qAbs(m_mainCanvas->currentPlayTime() - selected * 1000) < .01);
        capture("03-transient-peaks.png");
        transientTabs->setCurrentIndex(0);
        capture("04-transient-closeup.png");
        saveJson("transient-diagnostics.json", transientJson);
        // Run the same asynchronous timing pipeline used by the Timing button.
        QTest::mouseClick(side, Qt::LeftButton, {}, side->tabRect(0).center());
        m_editor->findChild<QDoubleSpinBox *>("analysis.timingStart")->setValue(center);
        m_editor->findChild<QDoubleSpinBox *>("analysis.timingDuration")->setValue(16);
        auto *run = m_editor->findChild<QPushButton *>("analysis.runTiming");
        QElapsedTimer timingElapsed;
        timingElapsed.start();
        run->click();
        QVERIFY(!run->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 90000);
        const auto timing =
            QJsonDocument::fromJson(
                m_editor->findChild<QPlainTextEdit *>("analysis.rawDiagnostics")->toPlainText().toUtf8())
                .object();
        QCOMPARE(timing.value("analysisStatus").toString(), QString("Succeeded"));
        QCOMPARE(timing.value("timeline").toString(), QString("whole-file audio seconds"));
        QVERIFY(!timing.value("windows").toArray().isEmpty());
        report["timingAnalysisMilliseconds"] = timingElapsed.elapsed();
        report["timingCandidates"] = timing.value("tempoCandidates");
        report["timingStatus"] = timing.value("analysisStatus");
        saveJson("timing-diagnostics.json", timing);
        canvas()->setMillisecondsPerPixel(4);
        m_mainCanvas->setScrollPos(center * 1000);
        auto *windows = m_editor->findChild<QTableWidget *>("analysis.table.windows");
        auto *locals = m_editor->findChild<QTableWidget *>("analysis.table.windowCandidates");
        auto *tables = m_editor->findChild<QTabWidget *>("analysis.timingTables");
        int bestWindow = -1, bestLocal = -1;
        double bestPhase = -1;
        for (int row = 0; row < windows->rowCount(); ++row)
        {
            windows->setCurrentCell(row, 0);
            for (int local = 0; local < locals->rowCount(); ++local)
            {
                locals->setCurrentCell(local, 0);
                const auto candidate = locals->item(local, 0)->data(Qt::UserRole).toJsonObject();
                const double phase = candidate.value("phaseConfidence").toDouble();
                if (!candidate.value("pulseTimeSeconds").isDouble() || phase <= bestPhase)
                    continue;
                bestWindow = row;
                bestLocal = local;
                bestPhase = phase;
            }
        }
        bool previewed = false;
        if (bestWindow >= 0)
        {
            windows->setCurrentCell(bestWindow, 0);
            // Selecting the same window again also restores its local candidates.
            QMetaObject::invokeMethod(windows, "cellClicked", Qt::DirectConnection, Q_ARG(int, bestWindow),
                                      Q_ARG(int, 0));
            locals->setCurrentCell(bestLocal, 0);
            auto *preview = m_editor->findChild<QCheckBox *>("analysis.candidateGrid");
            if (preview->isEnabled())
            {
                preview->setChecked(true);
                previewed = canvas()->timingPreviewVisible();
                tables->setCurrentWidget(locals);
                const auto selectedCandidate = locals->item(bestLocal, 0)->data(Qt::UserRole).toJsonObject();
                report["previewedWindowCandidate"] = selectedCandidate;
                m_editor->findChild<QPushButton *>("analysis.candidateSeek")->click();
                canvas()->setMillisecondsPerPixel(4);
            }
        }
        report["candidateGridAvailable"] = previewed;
        capture("05-timing.png");
        // Real FLAC playback seeks/loops over the inspected part; display clock
        // validation does not claim that hardware audio output was monitored.
        const double loopStart = center * 1000;
        m_playback.setLoopRange(loopStart, loopStart + 500, true);
        QSignalSpy ticks(&m_playback, &PlaybackController::playbackFrameTick);
        const auto ack =
            connect(&m_playback, &PlaybackController::playbackFrameTick, &m_playback, [this](double, qint64 seq) {
                m_playback.acknowledgeFramePainted(seq);
            });
        m_playback.playFromTime(loopStart);
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() >= 10, 10000);
        QTest::qWait(800);
        m_playback.pause();
        disconnect(ack);
        bool wrapped = false;
        double previous = loopStart;
        for (const auto &tick : ticks)
        {
            const double time = tick[0].toDouble();
            if (previous > loopStart + 300 && time < loopStart + 150)
                wrapped = true;
            QVERIFY(time >= loopStart - 5 && time <= loopStart + 550);
            previous = time;
        }
        QVERIFY(wrapped);
        QVERIFY(qAbs(canvas()->currentTime() - m_mainCanvas->currentPlayTime()) < 25);
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        QVERIFY(!m_chart.canUndo());
        report["playbackLoopWrapped"] = wrapped;
        report["chartUnchanged"] = true;
        // Manual edits use the actual decoded spectrum and production mouse
        // handlers. These chosen points test editing, not annotated song BPM.
        m_editor->findChild<QTabWidget *>("analysis.timingModes")->setCurrentIndex(0);
        auto *measure = m_editor->findChild<TimingToolsPanel *>();
        auto *pick = m_editor->findChild<QCheckBox *>("analysis.measurePick");
        auto *span = m_editor->findChild<QLineEdit *>("analysis.measureSpan");
        auto *apply = m_editor->findChild<QPushButton *>("analysis.measureApply");
        canvas()->setMillisecondsPerPixel(1);
        m_mainCanvas->setScrollPos((center + .45) * 1000);
        double chosenEnd = center + .35;
        bool chosenTransient = false;
        for (const auto &peak : panel->result().peaks)
            if (peak.disposition == analysis::PeakDisposition::Detected
                && peak.timeSeconds >= center + .2 && peak.timeSeconds <= center + .6)
            {
                chosenEnd = peak.timeSeconds;
                chosenTransient = true;
                break;
            }
        auto atSpectrum = [&](double ms) { return QPoint(80, qRound(canvas()->yAtTime(ms))); };
        pick->setChecked(true);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atSpectrum(center * 1000));
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atSpectrum(chosenEnd * 1000));
        auto measured = measure->diagnostics();
        QVERIFY(measured.value("valid").toBool());
        QVERIFY(apply->isEnabled());
        QCOMPARE(measured.value("startBeat").toArray(), QJsonArray({int(center * 2), 0, 4}));
        const auto revision = m_chart.revision();
        capture("06-manual-measure.png");
        const QPoint endMarker = atSpectrum(measured.value("endAudioMilliseconds").toDouble());
        QTest::mousePress(canvas(), Qt::LeftButton, {}, endMarker);
        QTest::mouseMove(canvas(), endMarker + QPoint(0, 3));
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, endMarker + QPoint(0, 3));
        measured = measure->diagnostics();
        span->setText("1/3");
        const auto fractional = measure->diagnostics();
        QVERIFY(fractional.value("valid").toBool());
        QVERIFY(qAbs(fractional.value("bpm").toDouble() * 3 - measured.value("bpm").toDouble()) < 1e-8);
        capture("07-manual-fraction.png");
        span->setText("1");
        QCOMPARE(m_chart.revision(), revision);
        measured = measure->diagnostics();
        const double measuredBpm = measured.value("bpm").toDouble();
        apply->click();
        QCOMPARE(m_chart.chart()->bpmList().size(), 2);
        const auto applied = m_chart.chart()->bpmList().last();
        QCOMPARE(applied.position(), BeatPosition(int(center * 2), 0, 4));
        QCOMPARE(applied.denominator, 4);
        QCOMPARE(applied.bpm, measuredBpm);
        QCOMPARE(m_chart.chart()->meta().offset, 0);
        QCOMPARE(m_chart.chart()->notes().size(), 0);
        capture("08-manual-applied.png");
        m_chart.undo();
        QCOMPARE(m_chart.chart()->bpmList().size(), 1);
        QCOMPARE(m_chart.chart()->bpmList().first().bpm, 120.);
        QVERIFY(!m_chart.canUndo());
        capture("09-manual-undone.png");
        m_chart.redo();
        QCOMPARE(m_chart.chart()->bpmList().last().bpm, measuredBpm);
        m_chart.undo();
        report["manualMeasurement"] = QJsonObject{
            {"selection", "Start on the 120 BPM reference grid; End from a detected transient if available"},
            {"endChosenFromTransient", chosenTransient},
            {"accuracyClaim", "interaction validation only; no manually annotated song ground truth"},
            {"oneBeat", measured}, {"fractionalBeat", fractional},
            {"appliedBpm", measuredBpm}, {"undoRedoVerified", true}, {"chartRestored", true}};
        saveJson("manual-measurement.json", report.value("manualMeasurement").toObject());
        // Interpolation runs over the same real spectrum. The curve parameters
        // are explicit editing fixtures, not estimates of the song's tempo.
        m_editor->resize(1460, 1020);
        m_editor->findChild<QSplitter *>("analysis.panels")->setSizes({545, 900, 0});
        m_editor->findChild<QTabWidget *>("analysis.timingModes")->setCurrentIndex(2);
        auto *interpolation = m_editor->findChild<TimingInterpolationPanel *>();
        auto *interpolationApply = interpolation->findChild<QPushButton *>("analysis.interpolationApply");
        auto *interpolationPick = interpolation->findChild<QCheckBox *>("analysis.interpolationPick");
        auto *interpolationStartBpm = interpolation->findChild<QDoubleSpinBox *>("analysis.interpolationStartBpm");
        auto *interpolationEndBpm = interpolation->findChild<QDoubleSpinBox *>("analysis.interpolationEndBpm");
        auto *interpolationLength = interpolation->findChild<QLineEdit *>("analysis.interpolationLength");
        interpolationStartBpm->setValue(120);
        interpolationEndBpm->setValue(240);
        interpolationLength->setText("8");
        canvas()->setMillisecondsPerPixel(4);
        m_mainCanvas->setScrollPos((center + 1.4) * 1000);
        interpolationPick->setChecked(true);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, atSpectrum(center * 1000));
        QTRY_VERIFY(interpolationApply->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(canvas()->spectrum().valid()
                                    && canvas()->spectrum().startSeconds <= center
                                    && canvas()->spectrum().startSeconds + canvas()->spectrum().durationSeconds > center + 2.8,
                                30000);
        const auto interpolationJson = interpolation->diagnostics();
        m_mainCanvas->setScrollPos(center * 1000 + interpolationJson.value("durationMilliseconds").toDouble() * .8);
        QCOMPARE(interpolationJson.value("startBeat").toArray(), QJsonArray({int(center * 2), 0, 4}));
        QVERIFY(canvas()->interpolationPreviewVisible());
        QVERIFY(interpolationJson.value("maximumInteriorErrorMilliseconds").toDouble() < 10);
        capture("10-interpolation-preview.png");
        const auto interpolationRevision = m_chart.revision();
        QTimer::singleShot(30, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const bool saved = savePixmap(dialog->grab(), "11-interpolation-confirmation.png");
            dialog->button(QMessageBox::Yes)->click();
            QVERIFY(saved);
        });
        interpolationApply->click();
        QCOMPARE(m_chart.revision(), interpolationRevision + 1);
        const int pointCount = interpolationJson.value("nodes").toArray().size();
        QCOMPARE(m_chart.chart()->bpmList().size(), pointCount + 1);
        QCOMPARE(m_chart.chart()->bpmList()[1].denominator, 4);
        QCOMPARE(m_chart.chart()->bpmList().last().bpm, 120.);
        QVERIFY(!canvas()->interpolationPreviewVisible());
        const double interpolationDuration = interpolationJson.value("durationMilliseconds").toDouble();
        QVERIFY(qAbs(canvas()->timeAtBeat(center * 2 + 8) - center * 1000 - interpolationDuration) < 1e-6);
        capture("12-interpolation-applied.png");
        m_chart.undo();
        QCOMPARE(m_chart.chart()->bpmList().size(), 1);
        QVERIFY(!m_chart.canUndo());
        capture("13-interpolation-undone.png");
        m_chart.redo();
        QCOMPARE(m_chart.chart()->bpmList().size(), pointCount + 1);
        m_chart.undo();
        interpolationStartBpm->setValue(12);
        interpolationEndBpm->setValue(480);
        interpolationLength->setText("16");
        QTRY_VERIFY(interpolationApply->isEnabled());
        const auto adaptiveJson = interpolation->diagnostics();
        QVERIFY(adaptiveJson.value("nodes").toArray().size() > 17);
        QVERIFY(adaptiveJson.value("maximumInteriorErrorMilliseconds").toDouble() < 10);
        const double adaptiveDuration = adaptiveJson.value("durationMilliseconds").toDouble();
        canvas()->setMillisecondsPerPixel(12);
        m_mainCanvas->setScrollPos(center * 1000 + adaptiveDuration * .8);
        QTRY_VERIFY_WITH_TIMEOUT(canvas()->spectrum().valid()
                                    && canvas()->spectrum().startSeconds <= center
                                    && canvas()->spectrum().startSeconds + canvas()->spectrum().durationSeconds
                                        > center + adaptiveDuration / 1000,
                                30000);
        capture("14-interpolation-adaptive.png");
        report["bpmInterpolation"] = QJsonObject{
            {"accuracyClaim", "editing validation only; explicit BPM curve, not annotated song tempo"},
            {"beatLinearRamp", interpolationJson}, {"steepAdaptiveRamp", adaptiveJson},
            {"undoRedoVerified", true}, {"chartRestored", true}, {"notesUnchanged", true}};
        saveJson("bpm-interpolation.json", report.value("bpmInterpolation").toObject());
        saveJson("inspection-report.json", report);
        loadChart();
        QTRY_VERIFY_WITH_TIMEOUT(!panel->busy(), 10000);
        QVERIFY(!panel->result().valid());
        QVERIFY(!canvas()->spectrum().valid());
        QVERIFY(!canvas()->timingPreviewVisible());
        QVERIFY(!canvas()->interpolationPreviewVisible());
    }
};
int main(int argc, char **argv)
{
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QApplication app(argc, argv);
    AnalysisEditorTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "analysis_editor_tests.moc"
