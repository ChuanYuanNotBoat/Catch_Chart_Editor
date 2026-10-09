#include "app/MainWindow.h"
#include "ui/analysis/AnalysisEditor.h"
#include "ui/analysis/AnalysisCanvas.h"
#include "ui/analysis/TransientPanel.h"
#include "ui/analysis/TimingToolsPanel.h"
#include "ui/analysis/TimingInterpolationPanel.h"
#include "analysis/AutoTimingDiagnostics.h"
#include "analysis/AnalysisConfig.h"
#include "analysis/AnalysisSession.h"
#include "analysis/MusicalTimeTransform.h"
#include "analysis/SpectrumPaging.h"
#include "utils/NativeWindowTheme.h"
#include "ui/analysis/AnalysisConfigPanel.h"
#include "ui/analysis/AnalysisWorkbench.h"
#include "ui/analysis/AudioOverview.h"
#include <QStandardPaths>
#include <QFontDatabase>
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
#include <QInputDialog>
#include <QScopeGuard>
#include <QtConcurrentRun>
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
    static SpectrumService::Page spectrumPage(double startMs, double durationMs, int frames = 120, int bins = 32)
    {
        analysis::StereoSpectrum s;
        s.startSeconds = startMs / 1000;
        s.durationSeconds = durationMs / 1000;
        s.hopSeconds = s.durationSeconds / frames;
        s.sourceChannels = 2;
        s.sampleRate = 44100;
        for (int i = 0; i < bins; ++i)
            s.frequencies.push_back(float(30 * std::pow(20000. / 30, double(i) / qMax(1, bins - 1))));
        s.frames.resize(frames, {.7f, .5f, .2f, .1f});
        s.leftDb.resize(size_t(frames) * bins, -30);
        s.rightDb.resize(size_t(frames) * bins, -60);
        auto raster = analysis::prepareSpectrumRaster(s);
        return {std::make_shared<const analysis::StereoSpectrum>(std::move(s)), std::move(raster)};
    }
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
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
    void spectrumPagingCoversEdgesAndBoundsWork()
    {
        const auto ordinary = analysis::spectrumPageRange(8000, 10000);
        QVERIFY(ordinary.valid);
        QVERIFY(ordinary.startMs < 8000 && ordinary.endMs > 10000);
        QVERIFY(ordinary.durationMs() >= 12000 && ordinary.durationMs() < 30000);
        const auto flipped = analysis::spectrumPageRange(10000, 8000);
        QCOMPARE(flipped.startMs, ordinary.startMs);
        QCOMPARE(flipped.endMs, ordinary.endMs);
        // Previously the 20 s alignment plus the 120 s cap repeatedly missed
        // the last edge of a 100 s viewport.
        for (double low : {0., 19999., 21999., 99999.})
        {
            const auto r = analysis::spectrumPageRange(low, low + 100000);
            QVERIFY(r.valid);
            QVERIFY(r.startMs <= low && r.endMs >= low + 100000);
            QVERIFY(r.durationMs() <= 120000);
        }
        const auto eof = analysis::spectrumPageRange(9500, 16000, 10000);
        QVERIFY(eof.valid);
        QCOMPARE(eof.requiredEndMs, 10000.);
        QCOMPARE(eof.endMs, 10000.);
        QVERIFY(!analysis::spectrumPageRange(10000, 16000, 10000).valid);
        QVERIFY(!analysis::spectrumPageRange(0, 100001).valid);
        QVERIFY(!analysis::spectrumPageRange(NAN, 1000).valid);
    }
    void spectrumPrefetchKeepsVisiblePageAndRejectsStaleCompletion()
    {
        m_editor.reset();
        loadChart(true);
        AudioPlayer localAudio;
        PlaybackController localPlayback(&localAudio);
        struct Pending
        {
            double start, duration;
            std::shared_ptr<std::atomic<bool>> cancel;
            SpectrumService::PageCallback callback;
        };
        QVector<Pending> pending;
        m_editor = std::make_unique<AnalysisEditor>(
            &m_chart, &m_selection, &localPlayback, m_mainCanvas, m_range, m_main.get(),
            [&](QObject *, const QString &, double start, double duration, std::shared_ptr<std::atomic<bool>> cancel,
                SpectrumService::PageCallback callback) {
                pending.append({start, duration, std::move(cancel), std::move(callback)});
            });
        // Destruction must precede the local playback controller on all exits.
        const auto destroyEditor = qScopeGuard([&] {
            m_editor.reset();
        });
        m_editor->show();
        canvas()->setViewSynchronized(false);
        canvas()->setMillisecondsPerPixel(2000. / qMax(1, canvas()->height() - 28));
        canvas()->setCurrentTime(0);
        QTRY_COMPARE_WITH_TIMEOUT(pending.size(), 1, 2000);
        QVERIFY(pending[0].duration < 30000);
        const auto first = spectrumPage(pending[0].start, pending[0].duration);
        pending[0].callback(first);
        QCOMPARE(&canvas()->spectrum(), first.spectrum.get());
        const double firstEnd = (first.spectrum->startSeconds + first.spectrum->durationSeconds) * 1000;
        // Both edges still fit; entering the guard zone starts the next page.
        canvas()->setCurrentTime(firstEnd - 1200);
        const double visibleEnd = qMax(canvas()->timeAtY(28), canvas()->timeAtY(canvas()->height()));
        QVERIFY(visibleEnd < firstEnd);
        QTRY_COMPARE_WITH_TIMEOUT(pending.size(), 2, 2000);
        QCOMPARE(&canvas()->spectrum(), first.spectrum.get());
        QVERIFY(pending[1].start < visibleEnd && pending[1].start + pending[1].duration > firstEnd);
        QTest::qWait(150);
        QCOMPARE(pending.size(), 2); // no duplicate workers while prefetching
        auto second = spectrumPage(pending[1].start, pending[1].duration);
        canvas()->setCurrentTime(0);
        pending[1].callback(second);
        QCOMPARE(&canvas()->spectrum(), first.spectrum.get()); // a late prefetch cannot open a hole
        canvas()->setCurrentTime(firstEnd - 1200);
        QTest::qWait(180);
        QCOMPARE(&canvas()->spectrum(), second.spectrum.get());
        canvas()->setCurrentTime(0);
        QTest::qWait(180);
        QCOMPARE(&canvas()->spectrum(), first.spectrum.get());
        QCOMPARE(pending.size(), 2); // reverse navigation reuses a neighbor
        canvas()->setCurrentTime(50000);
        QTRY_COMPARE_WITH_TIMEOUT(pending.size(), 3, 2000);
        canvas()->setCurrentTime(80000);
        QTRY_VERIFY_WITH_TIMEOUT(pending[2].cancel->load(), 2000);
        pending[2].callback(spectrumPage(pending[2].start, pending[2].duration));
        QCOMPARE(&canvas()->spectrum(), first.spectrum.get());
        QTRY_COMPARE_WITH_TIMEOUT(pending.size(), 4, 2000);
        auto last = spectrumPage(pending[3].start, pending[3].duration);
        pending[3].callback(last);
        QCOMPARE(&canvas()->spectrum(), last.spectrum.get());
        QTest::qWait(180);
        QCOMPARE(pending.size(), 4);
        const double eof = (last.spectrum->startSeconds + last.spectrum->durationSeconds) * 1000;
        canvas()->setCurrentTime(eof - 1200);
        QTRY_COMPARE_WITH_TIMEOUT(pending.size(), 5, 2000);
        QVERIFY(pending[4].start < eof);
        pending[4].callback(spectrumPage(pending[4].start, eof - pending[4].start));
        canvas()->setCurrentTime(eof + 5000);
        QTest::qWait(180);
        QCOMPARE(pending.size(), 5); // known EOF never decodes empty ranges
    }
    void spectrumViewportClipsCropAndCachesProjection()
    {
        auto page = spectrumPage(1000, 250, 3, 4);
        // A final padded hop must not paint past the actual audio endpoint.
        auto s = *page.spectrum;
        s.hopSeconds = .1;
        auto raster = page.raster;
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x)
            {
                raster.left.setPixel(x, y, qRgb(40 + y * 40, 0, 0));
                raster.right.setPixel(x, y, qRgb(0, 0, 40 + y * 40));
            }
        SpectrumViewport view;
        auto projection = [](double y) {
            return 900 + 10 * y;
        };
        const auto image = view.image(s, raster, 1, {200, 50}, 1, "audio", 900, 1400, projection);
        const auto rects = spectrumChannelRects(200, 50);
        const int x = rects[0].left() + 5, right = rects[1].left() + 5;
        QCOMPARE(qAlpha(image.pixel(x, 9)), 0);
        QCOMPARE(image.pixel(x, 10), qRgb(40, 0, 0));
        QCOMPARE(image.pixel(right, 20), qRgb(0, 0, 80));
        QCOMPARE(image.pixel(x, 34), qRgb(120, 0, 0));
        QCOMPARE(qAlpha(image.pixel(x, 35)), 0);
        const auto count = view.rebuildCount();
        view.image(s, raster, 1, {200, 50}, 1, "audio", 900, 1400, projection);
        QCOMPARE(view.rebuildCount(), count);
        auto flipped = view.image(s, raster, 1, {200, 50}, 2, "audio", 1400, 900, [](double y) {
            return 1400 - 10 * y;
        });
        QCOMPARE(flipped.size(), QSize(400, 100));
        QCOMPARE(flipped.devicePixelRatio(), 2.);
        QCOMPARE(qAlpha(flipped.pixel(x * 2, 20)), 0);
        QCOMPARE(flipped.pixel(x * 2, 31), qRgb(120, 0, 0));
        QCOMPARE(view.rebuildCount(), count + 1);
        view.image(s, raster, 1, {200, 50}, 2, "new timing map", 1400, 900, [](double y) {
            return 1400 - 10 * y;
        });
        QCOMPARE(view.rebuildCount(), count + 2);
    }
    void preparedSpectrumAttachmentAndOverlayPaintStayBounded()
    {
        // Maximum-size page: rasterization runs off the GUI thread, attachment
        // shares all large arrays, and unchanged-view paints reuse the raster.
        auto future = QtConcurrent::run([] {
            return spectrumPage(0, 120000, 12000, 128);
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        const auto page = future.result();
        QVERIFY(page.valid());
        QElapsedTimer timer;
        timer.start();
        canvas()->setSpectrumPage(page.spectrum, page.raster);
        const auto attachNs = timer.nsecsElapsed();
        QCOMPARE(&canvas()->spectrum(), page.spectrum.get());
        canvas()->setViewSynchronized(false);
        canvas()->setMillisecondsPerPixel(5);
        canvas()->setCurrentTime(1000);
        auto image = canvas()->grab();
        QVERIFY(!image.isNull());
        const auto count = canvas()->spectrumViewportBuildCount();
        timer.restart();
        for (int i = 0; i < 20; ++i)
        {
            canvas()->setRange(0, 1 + i * .01, true);
            image = canvas()->grab();
        }
        const auto paintNs = timer.nsecsElapsed();
        QCOMPARE(canvas()->spectrumViewportBuildCount(), count);
        canvas()->setCurrentTime(10000);
        image = canvas()->grab();
        QCOMPARE(canvas()->spectrumViewportBuildCount(), count + 1);
        std::atomic<bool> cancelled{true};
        QVERIFY(!analysis::prepareSpectrumRaster(*page.spectrum, &cancelled).valid());
        qInfo("Prepared-page attach: %.3f ms; 20 overlay paints: %.3f ms", attachNs / 1e6, paintNs / 1e6);
    }
    void nativeTabsFollowDarkLightAndThemeChanges()
    {
        const auto originalPalette = qApp->palette();
        const auto originalStyle = qApp->styleSheet();
        QVERIFY(originalStyle.contains("QTabBar::tab")); // actual MainWindow theme includes tabs
        const auto restoreTheme = qScopeGuard([&] {
            qApp->setStyleSheet(originalStyle);
            qApp->setPalette(originalPalette);
        });
        QTabWidget tabs;
        tabs.addTab(new QWidget, "Selected");
        tabs.addTab(new QWidget, "Other");
        tabs.resize(300, 180);
        tabs.show();
        const QString folder = QDir::current().filePath("artifacts/ae-spectrum-followup-20261008");
        QVERIFY(QDir().mkpath(folder));
        for (const QColor background : {QColor("#1e242d"), QColor("#e8eaee"), QColor("#1e242d")})
        {
            const auto theme = NativeWindowTheme::themeColorsFor(background);
            qApp->setStyleSheet(NativeWindowTheme::applicationStyleSheet(background));
            auto palette = originalPalette;
            palette.setColor(QPalette::Window, theme.window);
            palette.setColor(QPalette::WindowText, theme.text);
            palette.setColor(QPalette::Base, theme.base);
            palette.setColor(QPalette::Text, theme.text);
            qApp->setPalette(palette);
            QTest::qWait(30);
            const auto bar = tabs.tabBar()->grab().toImage();
            const auto page = tabs.currentWidget()->grab().toImage();
            for (int i = 0; i < 2; ++i)
            {
                const auto rect = tabs.tabBar()->tabRect(i);
                const auto color = bar.pixelColor(rect.left() + 5, rect.top() + 5);
                QVERIFY2(theme.dark ? color.lightness() < 110 : color.lightness() > 130, qPrintable(color.name()));
            }
            const auto pageColor = page.pixelColor(5, 5);
            QVERIFY2(theme.dark ? pageColor.lightness() < 110 : pageColor.lightness() > 130,
                     qPrintable(pageColor.name()));
            auto *aeTabs = m_editor->findChild<QTabBar *>("analysis.leftTabs");
            QVERIFY(aeTabs);
            const auto aeBar = aeTabs->grab().toImage();
            const auto rect = aeTabs->tabRect(0);
            const auto color = aeBar.pixelColor(rect.left() + 5, rect.top() + 5);
            QVERIFY2(theme.dark ? color.lightness() < 110 : color.lightness() > 130, qPrintable(color.name()));
            QVERIFY(tabs.grab().save(folder + (theme.dark ? "/tabs-dark.png" : "/tabs-light.png")));
        }
    }
    void preparedSpectrumWorkerReturnsActualCrop()
    {
        bool finished = false;
        SpectrumService::Page page;
        QElapsedTimer timer;
        timer.start();
        SpectrumService::prepareFileRangeAsync(this, m_wav, 2000, 2500, std::make_shared<std::atomic<bool>>(false),
                                               [&](SpectrumService::Page result) {
                                                   QCOMPARE(QThread::currentThread(), qApp->thread());
                                                   page = std::move(result);
                                                   finished = true;
                                               });
        QTRY_VERIFY_WITH_TIMEOUT(finished, 15000);
        QVERIFY2(page.valid(), page.spectrum ? page.spectrum->error.c_str() : "Missing worker result");
        QVERIFY(qAbs(page.spectrum->startSeconds - 2.) < .001);
        QVERIFY(qAbs(page.spectrum->durationSeconds - 2.5) < .001);
        QCOMPARE(page.raster.left.height(), int(page.spectrum->frames.size()));
        QCOMPARE(page.raster.right.width(), int(page.spectrum->frequencies.size()));
        canvas()->setSpectrumPage(page.spectrum, page.raster);
        QCOMPARE(&canvas()->spectrum(), page.spectrum.get());
        qInfo("Synthetic WAV decode + STFT + worker raster: %.3f ms", timer.nsecsElapsed() / 1e6);
    }
    void profileCompatibilityAndSnapshot()
    {
        auto defaults = analysis::defaultConfig();
        QVERIFY(analysis::validateConfig(defaults).isEmpty());
        auto edited = defaults;
        auto stable = edited.value("stable").toObject();
        stable["preferLocalTempoEvidence"] = true;
        edited["stable"] = stable;
        QVERIFY(analysis::configHash(defaults) != analysis::configHash(edited));
        auto options = analysis::configOptions(edited);
        QVERIFY(options.preferLocalTempoEvidence);
        QVERIFY(!options.enableComplexSubdivisionAnalysis);
        QCOMPARE(options.windowSpecs.size(), 3);
        auto old = edited;
        old["algorithmVersion"] = "another-build";
        auto debug = old.value("debug").toObject();
        debug["tempoTransitionPenalty"] = 9.;
        old["debug"] = debug;
        QJsonObject imported;
        QStringList report;
        QVERIFY(analysis::importConfig(old, imported, report));
        QVERIFY(!report.isEmpty());
        QCOMPARE(imported.value("debug").toObject().value("tempoTransitionPenalty").toDouble(), 2.);
        QVERIFY(imported.value("stable").toObject().value("preferLocalTempoEvidence").toBool());
        auto invalid = edited;
        stable["minimumTempoBpm"] = 500.;
        stable["maximumTempoBpm"] = 100.;
        invalid["stable"] = stable;
        const auto saved = imported;
        QVERIFY(!analysis::importConfig(invalid, imported, report));
        QCOMPARE(imported, saved);
        old["schemaVersion"] = 900;
        QVERIFY(!analysis::importConfig(old, imported, report));
        QCOMPARE(imported, saved);
        auto project = QJsonObject{{"algorithmVersion", AutoTiming2Bridge::algorithmVersion()},
                                   {"stable", QJsonObject{{"minimumTempoBpm", 60.}}}};
        auto effective = analysis::effectiveConfig(edited, project);
        QCOMPARE(effective.value("stable").toObject().value("minimumTempoBpm").toDouble(), 60.);
        QVERIFY(effective.value("stable").toObject().value("preferLocalTempoEvidence").toBool());
        QVERIFY(analysis::validateRequest(defaults, 0, 750123.456).isEmpty());
        QVERIFY(!analysis::validateRequest(defaults, 0, 250).isEmpty());
        auto dense = defaults;
        stable = defaults.value("stable").toObject();
        stable["fineTempoHopSeconds"] = .01;
        dense["stable"] = stable;
        QVERIFY(!analysis::validateRequest(dense, 0, 750123.456).isEmpty());
        for (const auto &entry : QJsonObject{{"tempoAgreementTolerance", .3},
                                             {"minimumRhythmLayerSupportWindows", 1},
                                             {"minimumSemanticSubdivision", 1},
                                             {"maximumPolyrhythmTerm", 1}}
                                     .keys())
        {
            auto rejected = defaults;
            auto values = rejected.value("debug").toObject();
            values[entry] = entry == "tempoAgreementTolerance" ? QJsonValue(.3) : QJsonValue(1);
            rejected["debug"] = values;
            QVERIFY(!analysis::validateConfig(rejected).isEmpty());
        }
        auto rejected = defaults;
        stable = rejected.value("stable").toObject();
        stable["fineTempoWindowSeconds"] = 3.;
        rejected["stable"] = stable;
        QVERIFY(!analysis::validateConfig(rejected).isEmpty());
        stable["fineTempoWindowSeconds"] = 4.;
        stable["maximumTrackedGapSeconds"] = 0.;
        rejected["stable"] = stable;
        QVERIFY(!analysis::validateConfig(rejected).isEmpty());
    }
    void profileUiPersistsSparseProjectOverrideAndProtectsDefault()
    {
        const auto path = m_temp.filePath("profiles.json");
        AnalysisConfigPanel profiles(nullptr, path);
        profiles.setLevel(2);
        profiles.show();
        auto *minimum = profiles.findChild<QDoubleSpinBox *>("analysis.option.minimumTempoBpm");
        auto *save = profiles.findChild<QPushButton *>("analysis.profile.save");
        auto namePreset = [&](const QString &name) {
            QTimer::singleShot(0, &profiles, [name] {
                auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->setTextValue(name);
                dialog->accept();
            });
        };
        minimum->setValue(60);
        profiles.findChild<QDoubleSpinBox *>("analysis.option.maximumTempoBpm")->setValue(1000);
        namePreset("Default");
        save->click();
        QVERIFY(!QFileInfo::exists(path));
        namePreset("Fixture preset");
        save->click();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto saved = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QCOMPARE(saved.value("profiles").toObject().size(), 1);
        QCOMPARE(saved.value("profiles")
                     .toObject()
                     .value("Fixture preset")
                     .toObject()
                     .value("stable")
                     .toObject()
                     .value("minimumTempoBpm")
                     .toDouble(),
                 60.);
        minimum->setValue(75);
        namePreset("Copied preset");
        profiles.findChild<QPushButton *>("analysis.profile.copy")->click();
        QVERIFY(file.open(QIODevice::ReadOnly));
        saved = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QCOMPARE(saved.value("profiles")
                     .toObject()
                     .value("Copied preset")
                     .toObject()
                     .value("stable")
                     .toObject()
                     .value("minimumTempoBpm")
                     .toDouble(),
                 60.);
        const auto chartPath = m_temp.filePath("override.mc");
        profiles.loadProject(chartPath);
        minimum->setValue(900);
        profiles.findChild<QPushButton *>("analysis.saveProjectProfile")->click();
        QFile sidecar(chartPath + ".analysis-config.json");
        QVERIFY(sidecar.open(QIODevice::ReadOnly));
        const auto project = QJsonDocument::fromJson(sidecar.readAll()).object();
        sidecar.close();
        QCOMPARE(project.value("stable").toObject(), QJsonObject({{"minimumTempoBpm", 900.}}));
        QCOMPARE(project.value("basePresetSnapshot")
                     .toObject()
                     .value("stable")
                     .toObject()
                     .value("maximumTempoBpm")
                     .toDouble(),
                 1000.);
        QVERIFY(!QFileInfo::exists(chartPath));
        AnalysisConfigPanel restored(nullptr, path);
        restored.loadProject(chartPath);
        QCOMPARE(restored.config().value("stable").toObject().value("minimumTempoBpm").toDouble(), 900.);
        QCOMPARE(restored.config().value("stable").toObject().value("maximumTempoBpm").toDouble(), 1000.);
        QVERIFY(analysis::validateConfig(restored.config()).isEmpty());
        const auto portablePath = m_temp.filePath("missing-global-profiles.json");
        AnalysisConfigPanel portable(nullptr, portablePath);
        portable.loadProject(chartPath);
        QCOMPARE(portable.config().value("stable").toObject().value("maximumTempoBpm").toDouble(), 1000.);
        QCOMPARE(portable.config().value("stable").toObject().value("minimumTempoBpm").toDouble(), 900.);
        profiles.findChild<QPushButton *>("analysis.clearProjectProfile")->click();
        QCOMPARE(profiles.config().value("stable").toObject().value("minimumTempoBpm").toDouble(), 60.);
        minimum->setValue(92);
        profiles.findChild<QPushButton *>("analysis.reset.minimumTempoBpm")->click();
        QCOMPARE(minimum->value(), 30.);
        profiles.findChild<QPushButton *>("analysis.resetAll")->click();
        QCOMPARE(profiles.config(), analysis::defaultConfig());
        // Unsupported stores stay byte-for-byte intact after an attempted save.
        const auto badPath = m_temp.filePath("bad-profiles.json");
        QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{\"schemaVersion\":999,\"profiles\":{}}");
        bad.close();
        AnalysisConfigPanel invalid(nullptr, badPath);
        invalid.findChild<QPushButton *>("analysis.profile.save")->click();
        QVERIFY(bad.open(QIODevice::ReadOnly));
        QCOMPARE(bad.readAll(), QByteArray("{\"schemaVersion\":999,\"profiles\":{}}"));
    }
    void requestIdentityRejectsOldConfigurationAndSource()
    {
        analysis::AnalysisSession session;
        auto request = session.begin("source-A", 2500, 8000, analysis::defaultConfig());
        QVERIFY(session.isCurrent(request, "source-A"));
        QVERIFY(!session.isCurrent(request, "source-B"));
        session.invalidateConfig();
        QVERIFY(!session.isCurrent(request, "source-A"));
        auto next = session.begin("source-A", 5000, 9000, analysis::defaultConfig());
        QVERIFY(session.isCurrent(next, "source-A"));
        session.discard();
        QVERIFY(!session.isCurrent(next, "source-A"));
        for (int i = 0; i < 12; ++i)
            session.remember({{"runId", i}});
        QCOMPARE(session.history.size(), 8);
        session.clearSource();
        QVERIFY(session.history.isEmpty());
    }
    void sessionDispatchFreezesConfigurationAndSerializesWork()
    {
        analysis::AnalysisSession session;
        BpmDetector::AsyncAnalysisCallback pending;
        std::shared_ptr<std::atomic<bool>> token;
        int runs = 0, completions = 0;
        auto config = analysis::defaultConfig();
        auto stable = config.value("stable").toObject();
        stable["preferLocalTempoEvidence"] = true;
        config["stable"] = stable;
        const auto request = session.begin("audio-A", 1234.567, 8000, config);
        analysis::AnalysisSession::Runner fixture =
            [&](QObject *, const QString &path, double start, double duration, const AutoTiming2Options &options,
                std::shared_ptr<std::atomic<bool>> cancel, BpmDetector::AsyncAnalysisCallback callback) {
                QCOMPARE(path, QString("fixture-only"));
                QCOMPARE(start, 1234.567);
                QCOMPARE(duration, 8000.);
                QVERIFY(options.preferLocalTempoEvidence);
                ++runs;
                token = cancel;
                pending = std::move(callback);
            };
        analysis::AnalysisCompletion completed;
        auto accept = [&](analysis::AnalysisCompletion value) {
            ++completions;
            completed = std::move(value);
        };
        QVERIFY(session.dispatch(this, "fixture-only", request, accept, fixture));
        QVERIFY(session.busy());
        QCOMPARE(runs, 1);
        QVERIFY(!session.dispatch(this, "fixture-only", request, accept, fixture));
        session.invalidateConfig();
        QVERIFY(token->load());
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        result.analysis.valid = true;
        pending(true, result, {});
        QVERIFY(!session.busy());
        QCOMPARE(completions, 1);
        QVERIFY(completed.discarded);
        QCOMPARE(completed.diagnostics.value("configSnapshot").toObject(), config);
        QCOMPARE(completed.diagnostics.value("configHash").toString(), analysis::configHash(config));
        QCOMPARE(completed.diagnostics.value("resultState").toString(), QString("Discarded"));
        const auto next = session.begin("audio-B", 1234.567, 8000, config);
        QVERIFY(session.dispatch(this, "fixture-only", next, accept, fixture));
        result.analysisStatus = BpmDetector::AnalysisStatus::Failed;
        pending(false, result, QStringLiteral("fixture preparation failure"));
        QVERIFY(!completed.discarded);
        QCOMPARE(completions, 2);
        QCOMPARE(completed.diagnostics.value("resultState").toString(), QString("Failed"));
        QCOMPARE(completed.error, QString("fixture preparation failure"));
        // A destroyed editor/session cancels preparation and suppresses a late callback.
        {
            analysis::AnalysisSession closing;
            const auto r = closing.begin("audio-A", 1234.567, 8000, config);
            QVERIFY(closing.dispatch(this, "fixture-only", r, accept, fixture));
        }
        QVERIFY(token->load());
        pending(true, result, {});
        QCOMPARE(completions, 2);
    }
    void independentTrackVisibilityIsWorkspaceState()
    {
        auto *workbench = m_editor->findChild<AnalysisWorkbench *>();
        auto *check = m_editor->findChild<QCheckBox *>("analysis.track.confidence");
        QVERIFY(check);
        QVERIFY(workbench->trackVisible("confidence"));
        const auto config = m_editor->findChild<AnalysisConfigPanel *>()->config();
        const auto revision = m_chart.revision();
        QSignalSpy changed(workbench, &AnalysisWorkbench::trackVisibilityChanged);
        check->setChecked(false);
        QCOMPARE(changed.size(), 1);
        QVERIFY(!workbench->trackVisible("confidence"));
        QVERIFY(workbench->trackVisible("tempo"));
        QCOMPARE(m_editor->findChild<AnalysisConfigPanel *>()->config(), config);
        QCOMPARE(m_chart.revision(), revision);
        m_editor.reset();
        createEditor();
        QVERIFY(!m_editor->findChild<AnalysisWorkbench *>()->trackVisible("confidence"));
    }
    void musicalTransformAndModelDomain()
    {
        canvas()->setViewSynchronized(false);
        canvas()->setMillisecondsPerPixel(2);
        canvas()->setCurrentTime(3500);
        const double center = canvas()->timeAtY((canvas()->height() + 28) / 2.);
        canvas()->setMusicalView(true);
        QVERIFY(qAbs(canvas()->timeAtY((canvas()->height() + 28) / 2.) - center) < 1e-6);
        for (double beat : {0., 2., 7.5, 8., 10.})
            QVERIFY(qAbs(canvas()->beatAtTime(canvas()->timeAtBeat(beat)) - beat) < 1e-9);
        canvas()->setBeatsPerPixel(.01);
        double y0 = canvas()->yAtTime(canvas()->timeAtBeat(7)), y1 = canvas()->yAtTime(canvas()->timeAtBeat(8)),
               y2 = canvas()->yAtTime(canvas()->timeAtBeat(9));
        QVERIFY(qAbs((y1 - y0) - (y2 - y1)) < 1e-6);
        AutoTiming2TempoMap map;
        map.available = true;
        AutoTiming2TempoCurveSegment segment;
        segment.startSeconds = 10;
        segment.endSeconds = 12;
        segment.startBeat = 0;
        segment.endBeat = 4;
        segment.phaseLinear = 3;
        segment.phaseQuadratic = 1;
        map.segments.append(segment);
        auto ms = analysis::MusicalTimeTransform::audioAtModelBeat(map, 2);
        QVERIFY(ms);
        QVERIFY(qAbs(*ms - (10000 + 2000 * (-3 + std::sqrt(17.)) / 2)) < 1e-6);
        QVERIFY(!analysis::MusicalTimeTransform::audioAtModelBeat(map, -1));
        QVERIFY(!analysis::MusicalTimeTransform::audioAtModelBeat(map, 5));
        map.segments[0].phaseLinear = 0;
        QVERIFY(!analysis::MusicalTimeTransform::audioAtModelBeat(map, 2));
    }
    void interfaceLevelsAndOldResultState()
    {
        auto *level = m_editor->findChild<QComboBox *>("analysis.interfaceLevel");
        auto *profiles = m_editor->findChild<AnalysisConfigPanel *>();
        auto *advanced = profiles->findChild<QWidget *>("analysis.option.minimumTempoBpm"),
             *debug = profiles->findChild<QWidget *>("analysis.option.tempoTransitionPenalty");
        QVERIFY(level && advanced && debug);
        QVERIFY(advanced->isHidden());
        level->setCurrentIndex(1);
        QVERIFY(!advanced->isHidden());
        QVERIFY(debug->isHidden());
        level->setCurrentIndex(2);
        QVERIFY(!debug->isHidden());
        const auto config = profiles->config();
        level->setCurrentIndex(0);
        QCOMPARE(profiles->config(), config);
        BpmDetector::DetectionResult r;
        r.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        r.analysis.valid = true;
        r.analysis.durationSeconds = 8;
        m_editor->showTimingResult(r);
        QVERIFY(!m_editor->resultDiagnostics().isEmpty());
        auto *start = m_editor->findChild<QDoubleSpinBox *>("analysis.timingStart");
        start->setValue(1.234567);
        QCOMPARE(m_editor->resultDiagnostics().value("resultState").toString(), QString("OldConfiguration"));
        QCOMPARE(start->value(), 1.234567);
        auto *duration = m_editor->findChild<QDoubleSpinBox *>("analysis.timingDuration");
        duration->setValue(.25);
        QCOMPARE(duration->value(), .25);
        duration->setValue(750.123456);
        QCOMPARE(duration->value(), 750.123456);
        duration->setValue(.25);
        m_editor->findChild<QPushButton *>("analysis.runTiming")->click();
        QVERIFY(m_editor->findChild<QLabel *>("analysis.timingSummary")->text().contains("minimum analysis window"));
    }
    void localTestRecordViewerAndOverviewNavigation()
    {
        auto *workbench = m_editor->findChild<AnalysisWorkbench *>();
        QVERIFY(workbench);
        QJsonObject record{{"reference_type", "synthetic_truth"},
                           {"cases", QJsonArray{QJsonObject{{"case", "fixed"},
                                                            {"mode", "default"},
                                                            {"status", "FAIL"},
                                                            {"phase_error_max_ms", 9.},
                                                            {"coverage", .5}}}}};
        QString path = m_temp.filePath("fixture-record.json"), error;
        QVERIFY(analysis::saveJson(path, record, &error));
        QVERIFY(workbench->loadRecord(path, &error));
        auto *table = m_editor->findChild<QTableWidget *>("analysis.referenceTests");
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 1)->text(), QString("FAIL"));
        const auto before = m_chart.revision();
        QVERIFY(analysis::saveJson(path, {{"schemaVersion", 999}}, &error));
        QVERIFY(!workbench->loadRecord(path, &error));
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(m_chart.revision(), before);
        QJsonObject current{{"schemaVersion", 1},
                            {"analysisStatus", "Succeeded"},
                            {"analysisPcmSha256", "fixture-PCM"},
                            {"analysisStartMs", 1200.},
                            {"analysisFrameCount", 441000.},
                            {"analysisSampleRate", 44100},
                            {"analysisPcmEncoding", "fixture-float32"},
                            {"algorithmVersion", "build-A"}};
        current["derivedAnchorFit"] =
            QJsonArray{QJsonObject{{"residualMilliseconds", 10.}}, QJsonObject{{"residualMilliseconds", 90.}}};
        auto baseline = current;
        baseline["algorithmVersion"] = "build-B";
        workbench->setResult(current);
        QVERIFY(workbench->findChild<QLabel *>("analysis.overviewSummary")->text().contains("50 / 82 / 90 ms"));
        QVERIFY(analysis::saveJson(path, baseline, &error));
        QVERIFY(workbench->loadRecord(path, &error));
        QCOMPARE(workbench->pairedBaseline(), baseline);
        QVERIFY(analysis::saveJson(path, workbench->comparisonRecord(), &error));
        QVERIFY(workbench->loadRecord(path, &error));
        QCOMPARE(workbench->pairedBaseline(), baseline);
        baseline["analysisFrameCount"] = 440999.;
        QVERIFY(analysis::saveJson(path, baseline, &error));
        QVERIFY(workbench->loadRecord(path, &error));
        QVERIFY(workbench->pairedBaseline().isEmpty());
        auto *overview = m_editor->findChild<AudioOverview *>();
        SpectrumService::EnergyEnvelope data;
        data.durationSeconds = 12;
        data.bins = {{0, 6, .2, .5}, {6, 12, .4, .8}};
        overview->setEnvelope(data);
        QVERIFY(overview->hasEnvelope());
        canvas()->setMillisecondsPerPixel(2);
        QSignalSpy seek(overview, &AudioOverview::seekRequested);
        QTest::mouseClick(overview, Qt::LeftButton, {}, QPoint(10, 28 + (overview->height() - 52) / 2));
        QCOMPARE(seek.size(), 1);
        QVERIFY(qAbs(seek.first().first().toDouble() - 6000) < 1e-6);
        QCOMPARE(m_chart.revision(), before);
    }
    void bridgeEvidenceProjectionRetainsCropAndNull()
    {
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        AutoTiming2Window window;
        window.startSeconds = 1;
        window.endSeconds = 5;
        window.signalMetrics = {{"signalScore", .63}};
        window.evidenceReasons = {"LowSignal", "FutureReason"};
        window.rawRhythmCandidates.append(QJsonObject{{"pulseTimeSeconds", 2.}, {"periodSeconds", .1}, {"score", .4}});
        result.analysis.windows.append(window);
        AutoTiming2Bridge::translateTimeline(result.analysis, 7);
        auto json = analysis::timingDiagnostics(result);
        auto projected = json.value("windows").toArray().first().toObject();
        QCOMPARE(projected.value("startSeconds").toDouble(), 8.);
        QCOMPARE(projected.value("signalMetrics").toObject().value("signalScore").toDouble(), .63);
        QCOMPARE(projected.value("evidenceReasons").toArray().last().toString(), QString("FutureReason"));
        const auto raw = projected.value("rawRhythmCandidates").toArray().first().toObject();
        QCOMPARE(raw.value("pulseTimeSeconds").toDouble(), 9.);
        QCOMPARE(raw.value("periodSeconds").toDouble(), .1);
        QVERIFY(json.value("analysisPcmSha256").isNull());
        QVERIFY(json.value("legacyOffsetMilliseconds").isNull());
    }
    void commonTracksPreserveProvenanceAndHitWithoutEditing()
    {
        analysis::StereoSpectrum spectrum;
        spectrum.startSeconds = 7;
        spectrum.hopSeconds = .1;
        spectrum.sampleRate = 44100;
        spectrum.frames = {{.5f, .6f, .2f, .3f}, {.7f, .8f, .4f, .5f}};
        const auto envelopes = analysis::spectrumTracks(spectrum, {{"sourceIdentity", "fixture-source"}});
        QCOMPARE(envelopes.size(), 4);
        QVERIFY(qAbs(envelopes.first().points[1].seconds - 7.1) < 1e-9);
        QCOMPARE(envelopes.first().provenance.value("sourceIdentity").toString(), QString("fixture-source"));
        QVERIFY(!envelopes.first().points.first().confidence);
        canvas()->setMillisecondsPerPixel(2);
        canvas()->setCurrentTime(12000);
        const double t0 = canvas()->timeAtY(210) / 1000, t1 = canvas()->timeAtY(310) / 1000;
        analysis::TransientResult transient;
        transient.frames = {{t0, .5f, .1f, {.2f, .3f, .1f}}, {t1, .2f, .1f, {.1f, .1f, .1f}}};
        transient.peaks = {{0, t0, .02, .5f, {}, analysis::PeakDisposition::Detected},
                           {1, t1, .02, .2f, {}, analysis::PeakDisposition::BelowThreshold}};
        auto tracks = analysis::transientTracks(transient, {{"sourceIdentity", "fixture-source"}});
        QCOMPARE(tracks.size(), 5);
        QCOMPARE(tracks.last().events.size(), 2);
        QVERIFY(!tracks.last().events.first().confidence);
        QCOMPARE(tracks.last().events.last().disposition, QString("BelowThreshold"));
        for (auto &track : tracks)
            track.visible = track.kind == analysis::TrackKind::Events;
        const auto candidates = analysis::diagnosticTracks(
            {{"tempoCandidates", QJsonArray{QJsonObject{{"bpm", 120}, {"pulseTimeSeconds", QJsonValue()}}}}});
        QVERIFY(!candidates.last().candidates.first().jumpSeconds);
        canvas()->setAnalysisTracks(tracks + candidates, {});
        QCOMPARE(canvas()->analysisTracks().first().transformId, QString("audio-seconds:v1"));
        const auto revision = m_chart.revision();
        const auto notes = m_chart.chart()->notes();
        QSignalSpy objects(canvas(), &AnalysisCanvas::analysisObjectSelected);
        QTest::mouseClick(canvas(), Qt::LeftButton, {}, QPoint(canvas()->spectrumWidth() - 8, 210));
        QCOMPARE(objects.size(), 1);
        QVERIFY(qAbs(canvas()->currentTime() - t0 * 1000) < 1e-6);
        const auto selected = m_editor->findChild<AnalysisWorkbench *>()->selectedObject();
        QCOMPARE(selected.value("trackId").toString(), QString("transient.peaks"));
        QCOMPARE(selected.value("frameIndex").toString(), QString("0"));
        QCOMPARE(m_chart.revision(), revision);
        QCOMPARE(m_chart.chart()->notes(), notes);
        QVERIFY(!m_chart.canUndo());
        canvas()->setMusicalView(true);
        const auto identity = canvas()->analysisTracks().first().transformId;
        QVERIFY(identity.startsWith("chart-time:"));
        QCOMPARE(canvas()->analysisTracks().last().events.first().seconds, t0);
        m_chart.addBpm(BpmEntry(4, 0, 1, 240));
        QVERIFY(identity != canvas()->analysisTracks().first().transformId);
    }
    void wholeAudioEnvelopeUsesBoundedDecode()
    {
        const auto data = SpectrumService::analyzeEnergy(m_wav, std::make_shared<std::atomic<bool>>(false));
        QVERIFY2(data.error.isEmpty(), qPrintable(data.error));
        QVERIFY(qAbs(data.durationSeconds - 12) < 1. / 44100);
        QVERIFY(!data.bins.isEmpty() && data.bins.size() <= 4096);
        double end = 0;
        for (const auto &bin : data.bins)
        {
            QCOMPARE(bin.startSeconds, end);
            QVERIFY(bin.endSeconds > bin.startSeconds);
            QVERIFY(std::isfinite(bin.rms) && bin.rms >= 0 && bin.peak >= bin.rms);
            end = bin.endSeconds;
        }
        QCOMPARE(end, data.durationSeconds);
        const auto cancelled = SpectrumService::analyzeEnergy(m_wav, std::make_shared<std::atomic<bool>>(true));
        QVERIFY(cancelled.bins.isEmpty());
        QCOMPARE(cancelled.error, QString("Cancelled"));
        const auto invalid = SpectrumService::analyzeEnergy(m_temp.filePath("not-present.wav"), {});
        QVERIFY(!invalid.error.isEmpty());
        QVERIFY(invalid.bins.isEmpty());
    }
    void timeOnlyMovePreservesXAndRainLength()
    {
        Note original(3, 1, 4, 123);
        m_chart.addNote(original);
        canvas()->setCurrentTime(2000);
        QTest::mousePress(canvas(), Qt::LeftButton, {}, atBeat(3.25));
        QTest::mouseMove(canvas(), atBeat(4));
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, atBeat(4));
        QCOMPARE(m_chart.chart()->notes().first().x, 123);
        QCOMPARE(m_chart.chart()->notes().first().getStartBeat(), 4.);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->notes().first().getStartBeat(), 3.25);
        m_chart.removeNote(m_chart.chart()->notes().first());
        Note rain(2, 2, 6, 3, 0, 1, 233);
        m_chart.addNote(rain);
        canvas()->setRainMode(true);
        QTest::mousePress(canvas(), Qt::LeftButton, {}, atBeat(2 + 1. / 3));
        QTest::mouseMove(canvas(), atBeat(4));
        QTest::mouseRelease(canvas(), Qt::LeftButton, {}, atBeat(4));
        const auto moved = m_chart.chart()->notes().first();
        QCOMPARE(moved.x, 233);
        QVERIFY(qAbs(moved.getStartBeat() - 4) < 1e-9);
        QVERIFY(qAbs(moved.getEndBeat() - 4 - 2. / 3) < 1e-9);
        m_chart.undo();
        QCOMPARE(m_chart.chart()->notes().first().numerator, 2);
        QCOMPARE(m_chart.chart()->notes().first().denominator, 6);
    }
    void autoTimingMapFixtureRequiresExplicitApplyAndOneUndo()
    {
        const Note note(9, 2, 6, 91);
        m_chart.addNote(note);
        const auto timings = m_chart.chart()->bpmList();
        auto unchangedTimings = [&] {
            const auto actual = m_chart.chart()->bpmList();
            QCOMPARE(actual.size(), timings.size());
            for (qsizetype i = 0; i < actual.size(); ++i)
            {
                QCOMPARE(actual[i].beatNum, timings[i].beatNum);
                QCOMPARE(actual[i].numerator, timings[i].numerator);
                QCOMPARE(actual[i].denominator, timings[i].denominator);
                QCOMPARE(actual[i].bpm, timings[i].bpm);
            }
        };
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        result.analysis.valid = true;
        auto &map = result.analysis.tempoMap;
        map.available = true;
        map.bpmListAvailable = true;
        map.hasTempoChange = true;
        map.hasAbruptChange = true;
        // The chart beat 1 is at audio .375 s with offset +125 ms.
        // The trusted step is at chart beat 8 / audio 3.875 s.
        map.startSeconds = .375;
        map.endSeconds = 7.875;
        map.startBeat = 0;
        map.endBeat = 23;
        AutoTiming2TempoCurveSegment first;
        first.startSeconds = .375;
        first.endSeconds = 3.875;
        first.startBeat = 0;
        first.endBeat = 7;
        first.startBpm = first.endBpm = 120;
        first.phaseLinear = 7;
        first.kind = "constant";
        first.confidence = 1;
        auto second = first;
        second.startSeconds = 3.875;
        second.endSeconds = 7.875;
        second.startBeat = 7;
        second.endBeat = 23;
        second.startBpm = second.endBpm = 240;
        second.phaseLinear = 16;
        map.segments = {first, second};
        map.bpmList = {{0, 120}, {7, 240}, {23, 240}};
        AutoTiming2TempoMapAnchor anchor;
        anchor.timeSeconds = 3.875;
        anchor.phaseBeat = 7;
        anchor.modelBpm = 240;
        anchor.confidence = anchor.phaseConfidence = 1;
        map.anchors.append(anchor);
        m_editor->showTimingResult(result);
        m_editor->findChild<QComboBox *>("analysis.interfaceLevel")->setCurrentIndex(1);
        auto *grid = m_editor->findChild<QCheckBox *>("analysis.modelGrid");
        QVERIFY(grid && grid->isEnabled());
        grid->setChecked(true);
        unchangedTimings();
        auto *apply = m_editor->findChild<QPushButton *>("analysis.applyTempoMap");
        QVERIFY(apply->isEnabled());
        result.analysisStatus = BpmDetector::AnalysisStatus::Failed;
        m_editor->showTimingResult(result);
        QVERIFY(!apply->isEnabled());
        unchangedTimings();
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        m_editor->showTimingResult(result);
        auto answer = [&](QMessageBox::StandardButton choice) {
            QTimer::singleShot(0, this, [choice] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->button(choice)->click();
            });
        };
        answer(QMessageBox::No);
        apply->click();
        unchangedTimings();
        answer(QMessageBox::Yes);
        apply->click();
        QCOMPARE(m_chart.chart()->bpmList().size(), timings.size());
        QCOMPARE(m_chart.chart()->bpmList().last().bpm, 240.);
        QCOMPARE(m_chart.chart()->notes().first(), note);
        QCOMPARE(m_chart.chart()->meta().offset, 125.);
        m_chart.undo();
        unchangedTimings();
        QCOMPARE(m_chart.chart()->notes().first(), note);
        m_chart.redo();
        QCOMPARE(m_chart.chart()->bpmList().last().bpm, 240.);
    }
    void workbenchVisualFixture()
    {
        analysis::StereoSpectrum spectrum;
        spectrum.sampleRate = 44100;
        spectrum.sourceChannels = 2;
        spectrum.startSeconds = 0;
        spectrum.durationSeconds = 12;
        spectrum.hopSeconds = .02;
        for (int i = 0; i < 32; ++i)
            spectrum.frequencies.push_back(40 * std::pow(1.2, i));
        for (int i = 0; i < 600; ++i)
        {
            analysis::SpectrumFrame frame;
            frame.leftPeak = .6;
            frame.rightPeak = .5;
            frame.leftRms = .2;
            frame.rightRms = .15;
            spectrum.frames.push_back(frame);
            for (int band = 0; band < 32; ++band)
            {
                float db = float(-70 + 40 * std::exp(-std::pow((band - 8 - 3 * std::sin(i * .03)) / 4, 2))
                                 + 15 * std::exp(-std::pow((i % 25) / 6., 2)));
                spectrum.leftDb.push_back(db);
                spectrum.rightDb.push_back(db - 4);
            }
        }
        canvas()->setSpectrum(spectrum);
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;
        result.analysis.valid = true;
        result.analysis.durationSeconds = 12;
        result.analysis.windowCount = 3;
        result.analysis.confidence = {.8, .8, .8, .8};
        AutoTiming2TempoCurveSegment segment;
        segment.startSeconds = 1;
        segment.endSeconds = 9;
        segment.startBeat = 0;
        segment.endBeat = 16;
        segment.phaseLinear = 12;
        segment.phaseQuadratic = 4;
        segment.startBpm = 90;
        segment.endBpm = 150;
        segment.kind = "continuous";
        result.analysis.tempoMap.available = true;
        result.analysis.tempoMap.segments.append(segment);
        for (int i = 1; i <= 9; ++i)
        {
            AutoTiming2TrackPoint point;
            point.timeSeconds = i;
            point.bpm = 90 + (i - 1) * 7.5;
            point.confidence = .7 + .1 * std::sin(i);
            point.phaseConfidence = .8;
            point.state = "Observed";
            result.analysis.tempoTrack.append(point);
        }
        AutoTiming2Candidate candidate;
        candidate.bpm = 120;
        candidate.hasPulseTime = true;
        candidate.pulseTimeSeconds = 1;
        candidate.phaseConfidence = .8;
        result.analysis.tempoCandidates.append(candidate);
        AutoTiming2Window window;
        window.id = 1;
        window.startSeconds = 1;
        window.endSeconds = 9;
        window.reliability = .8;
        window.evidenceReasons = {"AnchorSelected"};
        window.signalMetrics = {{"signalScore", .8}, {"rmsDb", -18.}};
        window.tempoCandidates.append(candidate);
        result.analysis.windows.append(window);
        m_chart.addNotes({Note(2, 0, 1, 200), Note(3, 0, 1, 320), Note(4, 0, 1, 6, 0, 1, 256)});
        m_editor->showTimingResult(result);
        canvas()->setCurrentTime(4000);
        SpectrumService::EnergyEnvelope energy;
        energy.durationSeconds = 12;
        for (int i = 0; i < 96; ++i)
            energy.bins.append(
                {i / 8., (i + 1) / 8., .1 + .2 * qAbs(std::sin(i * .07)), .4 + .2 * qAbs(std::sin(i * .11))});
        m_editor->findChild<AudioOverview *>()->setEnvelope(energy);
        const QString folder = QDir::current().filePath("artifacts/ae-workbench-20261007");
        QVERIFY(QDir().mkpath(folder));
        auto *levels = m_editor->findChild<QComboBox *>("analysis.interfaceLevel");
        m_editor->findChild<QLabel *>("analysis.audioName")->setText("Synthetic spectrum / diagnostics fixture");
        canvas()->setStatus({});
        QVERIFY(m_editor->grab().save(folder + "/normal-fixture.png"));
        levels->setCurrentIndex(1);
        m_editor->findChild<QCheckBox *>("analysis.modelGrid")->setChecked(true);
        QTest::qWait(30);
        canvas()->setStatus({});
        QVERIFY(m_editor->grab().save(folder + "/advanced-fixture.png"));
        levels->setCurrentIndex(2);
        auto *right = m_editor->findChild<QTabBar *>("analysis.rightTabs");
        QTest::mouseClick(right, Qt::LeftButton, {}, right->tabRect(2).center());
        QTest::qWait(30);
        QVERIFY(m_editor->findChild<AnalysisWorkbench *>()->isVisible());
        canvas()->setStatus({});
        QVERIFY(m_editor->grab().save(folder + "/debug-fixture.png"));
        m_editor->findChild<AnalysisWorkbench *>()->showTable("tempoTrack");
        QVERIFY(m_editor->grab().save(folder + "/tempo-phase-fixture.png"));
        auto *left = m_editor->findChild<QTabBar *>("analysis.leftTabs");
        QTest::mouseClick(left, Qt::LeftButton, {}, left->tabRect(3).center());
        QTest::qWait(30);
        QVERIFY(m_editor->findChild<AnalysisConfigPanel *>()->isVisible());
        canvas()->setStatus({});
        QVERIFY(m_editor->grab().save(folder + "/config-fixture.png"));
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
        QCOMPARE(m_chart.chart()->notes().size(), 0); // AE owns its time-only Delete command
        m_chart.undo();
        QCOMPARE(m_chart.chart()->notes().first().x, 7);
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
        result.analysis.tempoMap.available = true;
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
        auto *tables = m_editor->findChild<AnalysisWorkbench *>();
        tables->showTable("windows");
        QTest::mouseClick(windows->viewport(), Qt::LeftButton, {},
                          windows->visualItemRect(windows->item(0, 0)).center());
        QTest::mouseDClick(windows->viewport(), Qt::LeftButton, {},
                           windows->visualItemRect(windows->item(0, 0)).center());
        QCOMPARE(tables->currentDiagnosticTable(), static_cast<QWidget *>(locals));
        QVERIFY(qAbs(m_mainCanvas->currentPlayTime() - 10000) < .01);
        auto *globalTable = m_editor->findChild<QTableWidget *>("analysis.table.tempoCandidates");
        tables->showTable("tempoCandidates");
        QTest::mouseClick(globalTable->viewport(), Qt::LeftButton, {},
                          globalTable->visualItemRect(globalTable->item(0, 0)).center());
        QVERIFY(!preview->isEnabled());
        QVERIFY(!canvas()->timingPreviewVisible());
        tables->showTable("windows");
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
        m_editor->findChild<QComboBox *>("analysis.interfaceLevel")->setCurrentIndex(1);
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
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen")
        && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
#endif
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen"))
    {
        const int font = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        if (font >= 0 && !QFontDatabase::applicationFontFamilies(font).isEmpty())
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 9));
    }
#endif
    AnalysisEditorTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "analysis_editor_tests.moc"
