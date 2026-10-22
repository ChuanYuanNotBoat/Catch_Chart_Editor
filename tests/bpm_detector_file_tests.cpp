#include "audio/AudioConverter.h"
#include "audio/BpmDetector.h"

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <cmath>
#include <limits>

namespace
{
constexpr double wholeFileDurationMs = double(std::numeric_limits<int>::max()) * 1000.0;
}

class BpmDetectorFileTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp;
    QString m_wav;

    bool writePulseWav(const QString &path, int sampleRate, int seconds)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        const int frames = sampleRate * seconds;
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + frames * 2);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(sampleRate)
            << quint32(sampleRate * 2) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(frames * 2);
        for (int i = 0; i < frames; ++i)
        {
            const double t = double(i % (sampleRate / 2)) / sampleRate;
            out << qint16(24000 * std::exp(-t * 55) * std::sin(t * 2 * 3.141592653589793 * 90));
        }
        return out.status() == QDataStream::Ok;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_temp.isValid());
        m_wav = m_temp.filePath(QStringLiteral("pulse.wav"));
        QVERIFY(writePulseWav(m_wav, 48000, 12));
    }

    void convertedAudioThenAsyncWholeFileDetection()
    {
        const QString ogg = m_temp.filePath(QString::fromUtf8("转换结果.ogg"));
        bool conversionDone = false, conversionOk = false;
        QString conversionError;
        AudioConverter::convertToOggWithProgressAsync(this, m_wav, ogg,
            [&](bool ok, const QString &error) {
                conversionDone = true;
                conversionOk = ok;
                conversionError = error;
            });
        QTRY_VERIFY_WITH_TIMEOUT(conversionDone, 15000);
        QVERIFY2(conversionOk, qPrintable(conversionError));
        QVERIFY(AudioConverter::isOggFile(ogg));

        bool detectionDone = false, detectionOk = false;
        BpmDetector::DetectionResult result;
        QString detectionError;
        QThread *callbackThread = nullptr;
        BpmDetector::detectFromFileDetailedAsync(this, ogg, 0.0, wholeFileDurationMs,
            [&](bool ok, BpmDetector::DetectionResult detected, const QString &error) {
                detectionDone = true;
                detectionOk = ok;
                result = std::move(detected);
                detectionError = error;
                callbackThread = QThread::currentThread();
            });
        QTRY_VERIFY_WITH_TIMEOUT(detectionDone, 15000);
        QCOMPARE(callbackThread, thread());
        QVERIFY2(detectionOk, qPrintable(detectionError));
        QVERIFY(result.hasLegacyResult());

        BpmDetector::DetectionResult bounded;
        QString error;
        QVERIFY2(BpmDetector::detectFromFileDetailed(ogg, 0.0, 12000.0, bounded, &error), qPrintable(error));
        QCOMPARE(result.bpm, bounded.bpm);
        QCOMPARE(result.estimatedOffsetMs, bounded.estimatedOffsetMs);
    }

    void wholeFileDurationAcrossSampleRates_data()
    {
        QTest::addColumn<int>("sampleRate");
        for (const int rate : {8000, 44100, 48000, 96000})
            QTest::newRow(qPrintable(QString::number(rate))) << rate;
    }

    void wholeFileDurationAcrossSampleRates()
    {
        QFETCH(int, sampleRate);
        const QString path = m_temp.filePath(QString::number(sampleRate) + ".wav");
        QVERIFY(writePulseWav(path, sampleRate, 1));
        BpmDetector::DetectionResult result;
        QString error;
        BpmDetector::detectFromFileDetailed(path, 0.0, wholeFileDurationMs, result, &error);
        // Tempo detection can abstain on a short clip; decoding must reach that stage.
        QVERIFY(result.legacyStatus != BpmDetector::LegacyStatus::NotRequested);
    }

    void fractionalCropKeepsSampleBoundaries()
    {
        BpmDetector::DetectionResult result;
        QString error;
        QVERIFY2(BpmDetector::analyzeFromFileDetailed(m_wav, 100.251, 200.502, result, &error), qPrintable(error));
        QCOMPARE(result.decodedSampleRate, 48000);
        QCOMPARE(result.decodedFrameCount, qsizetype(9625));
        QCOMPARE(result.analysisStartMs, 4812 * 1000.0 / 48000);
    }

    void unrepresentableRange_data()
    {
        QTest::addColumn<double>("start");
        QTest::addColumn<double>("duration");
        const double huge = std::numeric_limits<double>::max();
        const double frameLimitMs = std::ldexp(1.0, 63) / 48.0;
        QTest::newRow("start-out-of-range") << huge << 1000.0;
        QTest::newRow("end-out-of-range") << 0.0 << huge;
        QTest::newRow("sum-overflow") << huge << huge;
        QTest::newRow("int64-exclusive-limit") << 0.0 << frameLimitMs;
    }

    void unrepresentableRange()
    {
        QFETCH(double, start);
        QFETCH(double, duration);
        BpmDetector::DetectionResult result;
        QString error;
        QVERIFY(!BpmDetector::detectFromFileDetailed(m_wav, start, duration, result, &error));
        QVERIFY2(error.contains("range", Qt::CaseInsensitive), qPrintable(error));
        QCOMPARE(result.legacyStatus, BpmDetector::LegacyStatus::NotRequested);
    }

    void localImportedAudio()
    {
        const QString path = qEnvironmentVariable("CCE_BPM_IMPORT_AUDIO");
        if (path.isEmpty())
            QSKIP("Set CCE_BPM_IMPORT_AUDIO to verify an installed audio file read-only.");
        BpmDetector::DetectionResult result;
        QString error;
        QVERIFY2(BpmDetector::detectFromFileDetailed(path, 0.0, wholeFileDurationMs, result, &error), qPrintable(error));
        QVERIFY(result.hasLegacyResult());
        qInfo("Installed audio detected: BPM=%.6f offset=%.3f ms", result.bpm, result.estimatedOffsetMs);
    }
};

QTEST_MAIN(BpmDetectorFileTests)
#include "bpm_detector_file_tests.moc"
