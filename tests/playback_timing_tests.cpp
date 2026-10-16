#include "audio/AudioPlayer.h"
#include "audio/NoteSoundPlayer.h"
#include "audio/PlaybackTiming.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/Chart.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include <QAudioDevice>
#include <QAudioBuffer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#endif
#include <QDataStream>
#include <QFile>
#include <QMediaDevices>
#include <QSettings>
#include <QSignalSpy>
#include <QSoundEffect>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

class PlaybackTimingTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp;
    QString m_music, m_hit;

    bool writeWav(const QString &path, int frames)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + frames * 4);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
            << quint32(192000) << quint16(4) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(frames * 4);
        const QByteArray silence(frames * 4, '\0');
        out.writeRawData(silence.constData(), silence.size());
        return out.status() == QDataStream::Ok;
    }

    Chart chartWithNotes() const
    {
        Chart chart;
        chart.meta().offset = 0;
        chart.bpmList().clear();
        chart.addBpm(BpmEntry(0, 0, 1, 120));
        for (int i = 1; i <= 30; ++i)
            chart.addNote(Note(i / 5, i % 5, 5, 256)); // every 100 ms
        return chart;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temp.path());
        m_music = m_temp.filePath("timeline.wav");
        m_hit = m_temp.filePath("hit.wav");
        // Long enough for FFmpeg stream probing to consume several seconds.
        QVERIFY(writeWav(m_music, 48000 * 12));
        QVERIFY(writeWav(m_hit, 480));
    }

    void filePlaybackPreservesFirstDecodedSamples()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        AudioPlayer audio;
        audio.setAudioCorrectionEnabled(false);
        auto *media = audio.findChild<QMediaPlayer *>();
        QVERIFY(media);
        auto *output = audio.findChild<QAudioBufferOutput *>();
        QVERIFY(output);
        qint64 firstStartUs = -1;
        connect(output, &QAudioBufferOutput::audioBufferReceived, this, [&](const QAudioBuffer &buffer) {
            if (buffer.isValid() && firstStartUs < 0) firstStartUs = buffer.startTime();
        });
        QVERIFY(audio.load(m_music));
        QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
        audio.play();
        QTRY_VERIFY_WITH_TIMEOUT(firstStartUs >= 0, 7000);
        audio.stop();
        QCOMPARE(firstStartUs, qint64(0));
        QVERIFY(audio.positionGranularityMs() > 80);
#else
        QSKIP("Decoded-buffer timestamps require Qt 6.8");
#endif
    }

    void coarseAudioFramesDoNotCauseClockResync()
    {
        for (double rate : {.1, .25, 1., 2., 10.}) {
            // Captured PCM frame PTS=170 at wall time ~2500 ms at 0.1x.
            // Treating this 80 ms media discrepancy as exact caused a seek-
            // sized clock jump even though decoded audio was progressing.
            const auto adjustment = PlaybackTiming::adjustClockTowardObservation(
                250, 170, rate, 0, true, 4096.0 / 48000 * 1000);
            QVERIFY(!adjustment.hardResync);
            QCOMPARE(adjustment.anchorTimeMs, 250.0);
            QCOMPARE(adjustment.rateCorrection, 0.0);
        }
        // Real discontinuities outside the uncertainty window still resync.
        const auto discontinuity = PlaybackTiming::adjustClockTowardObservation(
            1000, 2000, 1, 0, true, 85);
        QVERIFY(discontinuity.hardResync);
        QCOMPARE(discontinuity.anchorTimeMs, 1915.0);
    }

    void notesContinueWhilePaintIsNotAcknowledged()
    {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device");
        AudioPlayer audio;
        audio.setAudioCorrectionEnabled(false);
        PlaybackController playback(&audio);
        playback.setFrameRateCap(60);
        playback.setDisplayRefreshRate(60);
        ChartController charts;
        QVERIFY(charts.loadChartFromData(m_temp.filePath("test.mc"), chartWithNotes()));
        ChartCanvas canvas;
        canvas.setChartController(&charts);
        canvas.setPlaybackController(&playback);
        canvas.setNoteSoundFile(m_hit);
        canvas.setNoteSoundEnabled(true);
        auto *sounds = canvas.findChild<NoteSoundPlayer *>();
        QVERIFY(sounds);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.findChild<QSoundEffect *>()->status() == QSoundEffect::Ready, 5000);
        QSignalSpy hits(sounds, &NoteSoundPlayer::hitSoundTriggered);
        QSignalSpy frames(&playback, &PlaybackController::playbackFrameTick);
        QVERIFY(audio.load(m_music));
        QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
        playback.play();
        // Canvas stays hidden, so its first frame never acknowledges paint.
        QTRY_VERIFY_WITH_TIMEOUT(hits.size() >= 3, 2000);
        QCOMPARE(frames.size(), 1);
        playback.pause();
        const auto pausedCount = hits.size();
        QTest::qWait(180);
        QCOMPARE(hits.size(), pausedCount);
    }

    void seekDoesNotPlaySkippedNotes()
    {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device");
        AudioPlayer audio;
        audio.setAudioCorrectionEnabled(false);
        PlaybackController playback(&audio);
        ChartController charts;
        QVERIFY(charts.loadChartFromData(m_temp.filePath("seek.mc"), chartWithNotes()));
        ChartCanvas canvas;
        canvas.setChartController(&charts);
        canvas.setPlaybackController(&playback);
        canvas.setNoteSoundFile(m_hit);
        canvas.setNoteSoundEnabled(true);
        auto *sounds = canvas.findChild<NoteSoundPlayer *>();
        QTRY_VERIFY_WITH_TIMEOUT(canvas.findChild<QSoundEffect *>()->status() == QSoundEffect::Ready, 5000);
        QSignalSpy hits(sounds, &NoteSoundPlayer::hitSoundTriggered);
        QVERIFY(audio.load(m_music));
        QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
        playback.play();
        QTRY_VERIFY_WITH_TIMEOUT(hits.size() >= 1, 2000);
        playback.seekTo(2050);
        hits.clear();
        // No note is due before 2100. A catch-up sweep would submit 19 hits.
        QTest::qWait(20);
        QCOMPARE(hits.size(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(hits.size() >= 1, 1500);
        QVERIFY(hits.size() < 5);
        playback.pause();
        playback.seekTo(0);
        hits.clear();
        playback.play();
        QTRY_VERIFY_WITH_TIMEOUT(hits.size() >= 1, 1500);
        playback.stop();
    }

    void overdueDenseNotesDoNotStackOnRecovery()
    {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device");
        AudioPlayer audio;
        audio.setAudioCorrectionEnabled(false);
        PlaybackController playback(&audio);
        Chart dense = chartWithNotes();
        dense.notes().clear();
        for (int i = 1; i <= 100; ++i)
            dense.addNote(Note(i / 25, i % 25, 25, 256)); // every 20 ms
        ChartController charts;
        QVERIFY(charts.loadChartFromData(m_temp.filePath("dense.mc"), dense));
        ChartCanvas canvas;
        canvas.setChartController(&charts);
        canvas.setPlaybackController(&playback);
        canvas.setNoteSoundFile(m_hit);
        canvas.setNoteSoundEnabled(true);
        auto *sounds = canvas.findChild<NoteSoundPlayer *>();
        QTRY_VERIFY_WITH_TIMEOUT(canvas.findChild<QSoundEffect *>()->status() == QSoundEffect::Ready, 5000);
        QSignalSpy hits(sounds, &NoteSoundPlayer::hitSoundTriggered);
        QVERIFY(audio.load(m_music));
        QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
        playback.play();
        QTRY_VERIFY_WITH_TIMEOUT(hits.size() >= 2, 1500);
        hits.clear();
        // The music continues while this event loop is unavailable. Its
        // seven-or-more expired cues must not become one stacked attack.
        QThread::msleep(170);
        QCoreApplication::processEvents();
        playback.pause();
        QCOMPARE(hits.size(), 1);
    }
};

QTEST_MAIN(PlaybackTimingTests)
#include "playback_timing_tests.moc"
