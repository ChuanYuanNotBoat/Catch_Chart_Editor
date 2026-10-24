#include "app/MainWindow.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "controller/SelectionController.h"
#include "file/ChartIO.h"
#include "file/ProjectIO.h"
#include "model/Skin.h"
#include "utils/Logger.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

class ResourceCopyTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp{QDir(QCoreApplication::applicationDirPath()).filePath("resource-copy-XXXXXX")};

    static QByteArray readFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static bool writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    static bool writeWav(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + 96000);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(48000)
            << quint32(96000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(96000);
        const QByteArray pcm(96000, '\0');
        out.writeRawData(pcm.constData(), pcm.size());
        return out.status() == QDataStream::Ok;
    }

private slots:
    void initTestCase()
    {
#if defined(_MSC_VER) && defined(_DEBUG)
        // Automated failures must fail the process without opening a native
        // CRT dialog on the developer's desktop. Preserve the error in stderr.
        _CrtSetReportHook([](int type, char *message, int *) -> int {
            if (type == _CRT_ASSERT || type == _CRT_ERROR)
            {
                std::fputs(message, stderr);
                std::fflush(stderr);
                std::_Exit(86);
            }
            return false;
        });
#endif
        QVERIFY(m_temp.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temp.path());
        // Logger accepts paths relative to the executable directory.
        const QString logs = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(m_temp.filePath("logs"));
        Logger::init(logs);
        QVERIFY(Logger::isInitialized());
    }

    void existingDestinationAudioDoesNotRequireSourcePath()
    {
        const QString folder = m_temp.filePath("new-chart");
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(writeFile(QDir(folder).filePath("converted.ogg"), "already converted"));
        MetaData meta;
        meta.audioFile = "converted.ogg";
        const Chart chart = ChartIO::createDefaultChart(meta);
        const qsizetype logStart = readFile(Logger::logFilePath()).size();
        QVERIFY(ChartIO::save(QDir(folder).filePath("chart.mc"), chart));
        const QByteArray messages = readFile(Logger::logFilePath()).mid(logStart);
        QVERIFY2(!messages.contains("cannot copy"), messages.constData());
        QCOMPARE(readFile(QDir(folder).filePath("converted.ogg")), QByteArray("already converted"));
    }

    void missingDestinationAudioStillReportsMissingSource()
    {
        MetaData meta;
        meta.audioFile = "missing.ogg";
        const Chart chart = ChartIO::createDefaultChart(meta);
        const qsizetype logStart = readFile(Logger::logFilePath()).size();
        QVERIFY(ChartIO::save(m_temp.filePath("missing-audio.mc"), chart));
        const QByteArray messages = readFile(Logger::logFilePath()).mid(logStart);
        QVERIFY2(messages.contains("cannot copy"), messages.constData());
    }

    void unicodePlaybackUsesTemporaryCopy()
    {
        const QString original = m_temp.filePath(QString::fromUtf8("音频.wav"));
        QVERIFY(writeWav(original));
        QString prepared;
        {
            AudioPlayer audio;
            QVERIFY(audio.load(original));
            const auto *media = audio.findChild<QMediaPlayer *>();
            QVERIFY(media);
            prepared = media->source().toLocalFile();
            QVERIFY2(prepared != original, qPrintable(prepared));
            QCOMPARE(readFile(prepared), readFile(original));
            QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
            const QString previousCopy = prepared;
            QVERIFY(audio.load(original));
            prepared = media->source().toLocalFile();
            QVERIFY(prepared != original);
            QVERIFY(prepared != previousCopy);
            QVERIFY(!QFileInfo::exists(previousCopy));
            QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
        }
        QVERIFY(!QFileInfo::exists(prepared));
        QVERIFY(QFileInfo::exists(original));
    }

    void savedChartReceivesWorkingCopyResources()
    {
        const QString library = m_temp.filePath("library");
        QVERIFY(QDir().mkpath(library));
        const QString savedPath = QDir(library).filePath("chart.with.dots.mc");
        QVERIFY(ChartIO::save(savedPath, ChartIO::createDefaultChart(MetaData{})));
        const QByteArray originalConfig("{\"schemaVersion\":1,\"stable\":{\"minimumTempoBpm\":60}}");
        QVERIFY(writeFile(savedPath + ".analysis-config.json", originalConfig));
        ChartController charts;
        SelectionController selection;
        AudioPlayer audio;
        PlaybackController playback(&audio);
        MainWindow window(&charts, &selection, &playback, new Skin);
        // Let startup recovery/cleanup finish before starting an open transaction.
        QTest::qWait(50);
        QTimer dialogGuard;
        connect(&dialogGuard, &QTimer::timeout, this, [] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QMessageBox *>(widget))
                    dialog->reject();
        });
        dialogGuard.start(50);
        QString error;
        QVERIFY2(window.loadChartForAutomation(savedPath, &error), qPrintable(error));
        const QString workingDir = QFileInfo(charts.chartFilePath()).absolutePath();
        QVERIFY(workingDir != library);
        const QString workingConfig = charts.chartFilePath() + ".analysis-config.json";
        QCOMPARE(readFile(workingConfig), originalConfig);
        const QByteArray editedConfig("{\"schemaVersion\":1,\"stable\":{\"minimumTempoBpm\":75}}");
        QVERIFY(writeFile(workingConfig, editedConfig));

        QImage cover(8, 8, QImage::Format_RGB32);
        cover.fill(Qt::blue);
        const QString workingCover = QDir(workingDir).filePath("added.png");
        QVERIFY(cover.save(workingCover));
        QVERIFY(writeWav(QDir(workingDir).filePath("tap.wav")));
        MetaData next = charts.chart()->meta();
        next.backgroundFile = "added.png";
        charts.setMetaData(next);
        charts.addNote(Note(0, 0, 1, QStringLiteral("tap.wav"), 100, 0));
        const qsizetype logStart = readFile(Logger::logFilePath()).size();
        QVERIFY(QMetaObject::invokeMethod(&window, "saveChart", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(readFile(Logger::logFilePath()).mid(logStart).contains("Chart saved:"), 10000);
        QCOMPARE(readFile(QDir(library).filePath("added.png")), readFile(workingCover));
        QCOMPARE(readFile(QDir(library).filePath("tap.wav")), readFile(QDir(workingDir).filePath("tap.wav")));
        QCOMPARE(readFile(savedPath + ".analysis-config.json"), editedConfig);

        ProjectIO::initializeBuiltinFileTypes();
        const QString package = m_temp.filePath("analysis-config.mcz");
        QVERIFY(ProjectIO::exportToMczPure(package, savedPath));
        QString extracted;
        QVERIFY(ProjectIO::extractMcz(package, m_temp.filePath("analysis-config-unpacked"), extracted));
        const auto extractedCharts = ProjectIO::findChartsInDirectory(extracted);
        QCOMPARE(extractedCharts.size(), 1);
        QCOMPARE(QFileInfo(extractedCharts.first().first).fileName(), QString("chart.with.dots.mc"));
        QCOMPARE(readFile(extractedCharts.first().first + ".analysis-config.json"), editedConfig);
    }

    void localLongAudioReadOnly()
    {
        const QString path = qEnvironmentVariable("CCE_RESOURCE_COPY_AUDIO");
        if (path.isEmpty())
            QSKIP("Set CCE_RESOURCE_COPY_AUDIO to verify an installed Unicode-path audio file read-only.");
        const QByteArray original = readFile(path);
        QVERIFY(!original.isEmpty());
        QString prepared;
        {
            AudioPlayer audio;
            QVERIFY(audio.load(path));
            prepared = audio.findChild<QMediaPlayer *>()->source().toLocalFile();
            QVERIFY(prepared != path);
            QCOMPARE(readFile(prepared), original);
            QTRY_VERIFY_WITH_TIMEOUT(audio.canPlay(), 6000);
            qInfo("Installed audio loaded: duration=%lld ms", audio.duration());
        }
        QVERIFY(!QFileInfo::exists(prepared));
        QCOMPARE(readFile(path), original);
    }

    void cleanupTestCase()
    {
        Logger::shutdown();
    }
};

QTEST_MAIN(ResourceCopyTests)
#include "resource_copy_tests.moc"
