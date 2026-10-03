#include "app/MainWindow.h"
#include "ui/analysis/AnalysisEditor.h"
#include "ui/analysis/AnalysisCanvas.h"
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
    AnalysisCanvas *canvas() const { return m_editor->canvas(); }
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
        m_editor = std::make_unique<AnalysisEditor>(&m_chart, &m_selection, &m_playback, m_mainCanvas,
                                                    m_range, m_main.get());
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
        const auto raw =
            QJsonDocument::fromJson(
                m_editor->findChild<QPlainTextEdit *>("analysis.rawDiagnostics")->toPlainText().toUtf8())
                .object();
        const auto row = raw.value("tempoCandidates").toArray().first().toObject();
        QCOMPARE(row.value("pulseTimeSeconds").toDouble(), 12.345);
        QCOMPARE(row.value("legacyOffsetMilliseconds").toDouble(), -41.);
        QVERIFY(raw.value("legacyBpm").isNull());
        QVERIFY(qAbs(raw.value("derivedAnchorFit")
                         .toArray()
                         .first()
                         .toObject()
                         .value("residualMilliseconds")
                         .toDouble() +
                     100) < 1e-6);
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
        auto *tabs = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
        m_editor->findChild<QDoubleSpinBox *>("analysis.timingDuration")->setValue(12);
        auto *run = m_editor->findChild<QPushButton *>("analysis.runTiming");
        run->click();
        QVERIFY(!run->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 45000);
        const auto raw =
            QJsonDocument::fromJson(
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
    }
    void realPlaybackAndLoop()
    {
        loadChart(true);
        QVERIFY(m_audio.load(m_wav));
        QTRY_VERIFY_WITH_TIMEOUT(m_audio.canPlay(), 10000);
        m_playback.setLoopRange(400, 800, true);
        QSignalSpy ticks(&m_playback, &PlaybackController::playbackFrameTick);
        const auto ack = connect(&m_playback, &PlaybackController::playbackFrameTick, &m_playback,
                                 [this](double, qint64 seq) { m_playback.acknowledgeFramePainted(seq); });
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
