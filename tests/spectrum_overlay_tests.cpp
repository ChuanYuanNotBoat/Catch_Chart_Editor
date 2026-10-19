#include "analysis/SpectrumRaster.h"
#include "audio/AudioConverter.h"
#include "audio/SpectrumService.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "audio/AudioPlayer.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/CustomWidgets/ChartCanvas/ChartSpectrumOverlay.h"
#include "utils/Settings.h"
#include <QApplication>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtConcurrentRun>
#include <cmath>

class NativeTestCanvas final : public ChartCanvas
{
public:
    ~NativeTestCanvas() override
    {
        if (isVisible())
        {
            hide();
            // Deliver queued native hide/expose events before QWidget teardown.
            if (QApplication::platformName() == "windows")
                QTest::qWait(20);
        }
    }
};

class SpectrumOverlayTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp;
    QString m_wav;

    static SpectrumService::Page page(double start, double duration, double pulse = -1)
    {
        analysis::StereoSpectrum s;
        s.startSeconds = start / 1000;
        s.durationSeconds = duration / 1000;
        s.hopSeconds = .005;
        s.sampleRate = 44100;
        s.sourceChannels = 2;
        s.frequencies = {100, 400, 1000, 8000};
        s.frames.resize(size_t(std::ceil(s.durationSeconds / s.hopSeconds)));
        s.leftDb.resize(s.frames.size() * 4, -90);
        s.rightDb = s.leftDb;
        for (size_t i = 0; i < s.frames.size(); ++i)
            if (pulse < 0 || std::abs(s.startSeconds + i * s.hopSeconds - pulse) < .03)
                s.leftDb[i * 4 + 1] = 0;
        auto raster = analysis::prepareSpectrumRaster(s);
        return {std::make_shared<const analysis::StereoSpectrum>(std::move(s)), std::move(raster)};
    }
    Chart chart() const
    {
        Chart chart;
        chart.bpmList().clear();
        chart.addBpm({0, 0, 1, 120});
        chart.addBpm({4, 0, 1, 240});
        chart.meta().offset = 125;
        chart.meta().audioFile = m_wav;
        return chart;
    }
    static QImage render(ChartSpectrumOverlay &overlay, ChartCanvas &canvas)
    {
        QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            overlay.paint(painter, QRectF(20, 0, canvas.width() - 40, canvas.height()));
        }
        return image;
    }
    static double brightRow(const QImage &image)
    {
        double sum = 0, weight = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            const auto alpha = qAlpha(image.pixel(image.width() / 3, y));
            sum += y * alpha;
            weight += alpha;
        }
        return weight > 0 ? sum / weight : -1;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_temp.isValid());
        m_wav = m_temp.filePath("stereo.wav");
        QFile file(m_wav);
        QVERIFY(file.open(QIODevice::WriteOnly));
        constexpr int rate = 16000, seconds = 4, bytes = rate * seconds * 4;
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + bytes);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4)
               << quint16(4) << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(bytes);
        for (int i = 0; i < rate * seconds; ++i)
        {
            const double t = i / double(rate);
            stream << qint16(20000 * std::sin(2 * 3.141592653589793 * 440 * t))
                   << qint16(-12000 * std::sin(2 * 3.141592653589793 * 1760 * t));
        }
    }
    void indexedPyramidRetainsSingleFramePeaks()
    {
        const auto data = page(0, 1000, .5);
        QVERIFY(data.valid());
        QCOMPARE(data.raster.left.format(), QImage::Format_Indexed8);
        QCOMPARE(data.raster.overlay.back().height(), 1);
        QVERIFY(qAlpha(data.raster.overlay.back().pixel(1, 0)) > 0);
        QCOMPARE(qAlpha(data.raster.overlay.back().pixel(0, 0)), 0);
        QCOMPARE(qAlpha(data.raster.overlay.front().pixel(1, 0)), 0);
    }
    void sharedCacheAndSeekableVorbisKeepActualCrop()
    {
        auto future = QtConcurrent::run([this] {
            const auto cancel = std::make_shared<std::atomic<bool>>(false);
            const auto first = SpectrumService::prepareFileRange(m_wav, 500, 900, cancel);
            const auto second = SpectrumService::prepareFileRange(m_wav, 500, 900, cancel);
            return qMakePair(first, second);
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        const auto result = future.result();
        QVERIFY(result.first.valid());
        QCOMPARE(result.first.spectrum, result.second.spectrum);
        QCOMPARE(result.first.raster.left.constBits(), result.second.raster.left.constBits());
        QVERIFY(std::abs(result.first.spectrum->startSeconds - .5) < 1e-8);
        QVERIFY(std::abs(result.first.spectrum->durationSeconds - .9) < 1e-8);
        auto cancelled = std::make_shared<std::atomic<bool>>(true);
        auto stopped = QtConcurrent::run([this, cancelled] {
            return SpectrumService::prepareFileRange(m_wav, 500, 900, cancelled);
        });
        QTRY_VERIFY_WITH_TIMEOUT(stopped.isFinished(), 1000);
        QVERIFY(!stopped.result().valid());
        const auto ogg = m_temp.filePath("seekable.ogg");
        auto converted = QtConcurrent::run([this, ogg] { return AudioConverter::convertToOgg(m_wav, ogg); });
        QTRY_VERIFY_WITH_TIMEOUT(converted.isFinished(), 15000);
        QVERIFY(converted.result());
        auto seek = QtConcurrent::run([ogg] { return SpectrumService::analyzeFileRange(ogg, 2300, 800, {}); });
        QTRY_VERIFY_WITH_TIMEOUT(seek.isFinished(), 15000);
        const auto spectrum = seek.result();
        QVERIFY2(spectrum.valid(), spectrum.error.c_str());
        QCOMPARE(spectrum.sourceChannels, 2);
        QVERIFY(std::abs(spectrum.startSeconds - 2.3) < .001);
        QVERIFY(std::abs(spectrum.durationSeconds - .8) < .001);
        QVERIFY(spectrum.leftDb != spectrum.rightDb);
        QFile changed(m_wav);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write("x", 1), 1);
        changed.close();
        auto replaced = QtConcurrent::run([this] { return SpectrumService::prepareFileRange(m_wav, 500, 900, {}); });
        QTRY_VERIFY_WITH_TIMEOUT(replaced.isFinished(), 15000);
        QVERIFY(replaced.result().valid());
        QVERIFY(replaced.result().spectrum != result.first.spectrum);
    }
    void timingFlipZoomAndNotesShareOneProjection()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData(m_temp.filePath("fixture.mc"), chart()));
        NativeTestCanvas canvas;
        canvas.setChartController(&controller);
        canvas.resize(640, 600);
        canvas.setTimeScale(1);
        canvas.setVerticalFlip(false);
        canvas.show();
        int requests = 0;
        ChartSpectrumOverlay overlay(&canvas, [&](QObject *, const QString &, double start, double duration,
                                                  auto, auto callback) {
            ++requests;
            callback(page(start, duration, 2.5));
        });
        overlay.setChartController(&controller);
        QTRY_VERIFY(overlay.currentPage().valid());
        auto expected = [&] {
            const auto cache = MathUtils::buildBpmTimeCache(controller.chart()->bpmList(), controller.chart()->meta().offset);
            analysis::MusicalTimeTransform transform;
            transform.setChart(cache);
            return canvas.chartBeatToY(transform.beatAtAudio(2500));
        };
        QTRY_VERIFY(std::abs(brightRow(render(overlay, canvas)) - expected()) < 3);
        QTest::qWait(100); // visible blocks and the single neighbour
        const int loadedRequests = requests;
        canvas.setVerticalFlip(true);
        QVERIFY(std::abs(brightRow(render(overlay, canvas)) - expected()) < 3);
        controller.updateBpm(1, {4, 0, 1, 180});
        QVERIFY(std::abs(brightRow(render(overlay, canvas)) - expected()) < 3);
        controller.undo();
        canvas.setTimeScale(1.25);
        QVERIFY(std::abs(brightRow(render(overlay, canvas)) - expected()) < 3);
        controller.addNote(Note(3, 0, 1, 256));
        QTest::qWait(100);
        QCOMPARE(requests, loadedRequests); // edits reuse already loaded blocks
    }
    void overlappingViewsShareWorkAndCancellationIsLocal()
    {
        const auto source = m_temp.filePath("shared-views.wav");
        QVERIFY(QFile::copy(m_wav, source));
        QObject mainView, analysisView;
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        auto active = std::make_shared<std::atomic<bool>>(false);
        const auto before = SpectrumService::cacheStats();
        SpectrumService::Page first, second, progressive;
        bool firstDone = false, secondDone = false;
        SpectrumService::prepareFileRangeProgressiveAsync(&mainView, source, 0, 2000, cancelled,
            [&](auto page) { first = std::move(page); firstDone = true; }, {});
        SpectrumService::prepareFileRangeProgressiveAsync(&analysisView, source, 500, 900, active,
            [&](auto page) { second = std::move(page); secondDone = true; },
            [&](auto page) { progressive = std::move(page); });
        QTimer::singleShot(0, &mainView, [cancelled] { cancelled->store(true); });
        QTRY_VERIFY_WITH_TIMEOUT(firstDone && secondDone, 15000);
        QVERIFY(!first.valid());
        QVERIFY(second.valid());
        QVERIFY(progressive.valid());
        QCOMPARE(progressive.spectrum->startSeconds, 0.);
        QCOMPARE(progressive.spectrum->hopSeconds, .005);
        QCOMPARE(progressive.spectrum->frames.size(), size_t(400));
        const auto shared = SpectrumService::cacheStats();
        QCOMPARE(shared.generatedTiles - before.generatedTiles, quint64(1));
        QVERIFY(shared.sharedRequests > before.sharedRequests);
        auto extend = QtConcurrent::run([source] { return SpectrumService::prepareFileRange(source, 0, 4000, {}); });
        QTRY_VERIFY_WITH_TIMEOUT(extend.isFinished(), 15000);
        QVERIFY(extend.result().valid());
        QCOMPARE(SpectrumService::cacheStats().generatedTiles - before.generatedTiles, quint64(2));
        auto reused = QtConcurrent::run([source] { return SpectrumService::prepareFileRange(source, 0, 2000, {}); });
        QTRY_VERIFY_WITH_TIMEOUT(reused.isFinished(), 1000);
        QCOMPARE(reused.result().spectrum, progressive.spectrum);
        const auto &assembled = *extend.result().spectrum;
        const auto &tile = *progressive.spectrum;
        QVERIFY(std::equal(tile.leftDb.begin(), tile.leftDb.end(), assembled.leftDb.begin()));
        QVERIFY(std::equal(tile.rightDb.begin(), tile.rightDb.end(), assembled.rightDb.begin()));
    }
    void staleSourceCompletionAndHiddenRequestsAreRejected()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData(m_temp.filePath("fixture.mc"), chart()));
        NativeTestCanvas canvas;
        canvas.setChartController(&controller);
        canvas.resize(640, 480);
        canvas.show();
        struct Pending { std::shared_ptr<std::atomic<bool>> cancel; SpectrumService::PageCallback callback; };
        QVector<Pending> pending;
        ChartSpectrumOverlay overlay(&canvas, [&](QObject *, const QString &, double, double, auto cancel, auto callback) {
            pending.append({cancel, std::move(callback)});
        });
        overlay.setChartController(&controller);
        QTRY_COMPARE(pending.size(), 1);
        const auto copy = m_temp.filePath("replacement.wav");
        QVERIFY(QFile::copy(m_wav, copy));
        auto meta = controller.chart()->meta();
        meta.audioFile = copy;
        controller.setMetaData(meta);
        QVERIFY(pending[0].cancel->load());
        pending[0].callback(page(0, 12000));
        QVERIFY(!overlay.currentPage().valid());
        QTRY_COMPARE(pending.size(), 2);
        canvas.hide();
        QVERIFY(pending[1].cancel->load());
        pending[1].callback(page(0, 12000));
        QTest::qWait(100);
        QCOMPARE(pending.size(), 2);
        QVERIFY(!overlay.currentPage().valid());
        canvas.show();
        QTRY_COMPARE(pending.size(), 3);
        pending[2].callback(page(0, 12000));
        QVERIFY(overlay.currentPage().valid());
    }
    void optionalLayerHasNoWorkerWhenDisabled()
    {
        NativeTestCanvas canvas;
        QVERIFY(!canvas.spectrumOverlayEnabled());
        QVERIFY(!canvas.findChild<ChartSpectrumOverlay *>());
        canvas.setSpectrumOverlayEnabled(true);
        QVERIFY(canvas.findChild<ChartSpectrumOverlay *>());
        canvas.setSpectrumOverlayEnabled(false);
        QVERIFY(!canvas.findChild<ChartSpectrumOverlay *>());
        Settings::instance().setSpectrumOverlayEnabled(true);
        canvas.refreshRenderSettings();
        QVERIFY(canvas.spectrumOverlayEnabled());
        Settings::instance().setSpectrumOverlayEnabled(false);
        canvas.refreshRenderSettings();
        QVERIFY(!canvas.spectrumOverlayEnabled());
    }
    void playbackFollowRequestsNewTilesWithoutManualScrolling()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData(m_temp.filePath("playback.mc"), chart()));
        AudioPlayer audio;
        PlaybackController playback(&audio);
        NativeTestCanvas canvas;
        canvas.setChartController(&controller);
        canvas.setPlaybackController(&playback);
        canvas.resize(640, 480);
        canvas.setTimeScale(1);
        canvas.show();
        QVector<double> requests;
        ChartSpectrumOverlay overlay(&canvas,[&](QObject *,const QString &,double start,double duration,auto,auto callback) {
            requests.append(start);
            callback(page(start, duration));
        });
        overlay.setChartController(&controller);
        overlay.setPlaybackController(&playback);
        QTRY_VERIFY(overlay.currentPage().valid());
        QTest::qWait(150);
        playback.stateChanged(PlaybackController::Playing);
        // Settle the state-change request first: only the later playback frame
        // can cause the new range request being asserted below.
        QTest::qWait(50);
        const auto originalCount = requests.size();
        const auto originalScroll = canvas.scrollBeat();
        playback.playbackFrameTick(20000, 1);
        QVERIFY(canvas.scrollBeat() > originalScroll);
        QTRY_VERIFY(requests.size() > originalCount);
        QVERIFY(std::any_of(requests.cbegin() + originalCount, requests.cend(), [](double start) { return start >= 10000; }));
        playback.stateChanged(PlaybackController::Stopped);
    }
};

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    SpectrumOverlayTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "spectrum_overlay_tests.moc"
