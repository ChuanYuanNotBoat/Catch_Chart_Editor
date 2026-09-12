#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <utility>

#include "controller/ChartController.h"
#include "ui/MetaEditPanel.h"

namespace
{
    bool writeFakeVorbisFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write("OggS", 4);
        file.write(QByteArray(32, '\0'));
        file.write("\x01vorbis", 7);
        return file.error() == QFile::NoError;
    }

    bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 2000)
    {
        if (predicate())
            return true;

        QEventLoop loop;
        QTimer poll;
        QTimer timeout;
        bool matched = false;
        poll.setInterval(5);
        timeout.setSingleShot(true);
        timeout.setInterval(timeoutMs);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
            if (!predicate())
                return;
            matched = true;
            loop.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start();
        timeout.start();
        loop.exec();
        return matched;
    }

    class DeferredMetaEditPanel final : public MetaEditPanel
    {
    public:
        using MetaEditPanel::MetaEditPanel;

        int conversionStarts() const { return m_conversionStarts; }
        int conversionFinishes() const { return m_conversionFinishes; }

    protected:
        void convertAudioToOggAsync(const QString &,
                                    const QString &outputPath,
                                    AudioConversionCompletion completion) override
        {
            ++m_conversionStarts;
            QTimer::singleShot(100,
                               this,
                               [this, outputPath, completion = std::move(completion)]() mutable {
                                   ++m_conversionFinishes;
                                   const bool success = writeFakeVorbisFile(outputPath);
                                   completion(success,
                                              success ? QString()
                                                      : QStringLiteral("test output write failed"));
                               });
        }

    private:
        int m_conversionStarts = 0;
        int m_conversionFinishes = 0;
    };

    bool testSaveWaitsForAsyncAudioConversion()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        const QString sourcePath = directory.filePath(QStringLiteral("source.wav"));
        QFile source(sourcePath);
        if (!source.open(QIODevice::WriteOnly) || source.write("not-an-ogg") <= 0)
            return false;
        source.close();

        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;
        DeferredMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *audioEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaAudioFileEdit"));
        if (!audioEdit)
            return false;

        QSignalSpy saveSpy(&panel, &MetaEditPanel::saveRequested);
        audioEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        bool observedPendingConversion = false;
        const bool completed = waitUntil([&]() {
            if (panel.conversionStarts() == 1 && panel.conversionFinishes() == 0
                && saveSpy.isEmpty())
            {
                observedPendingConversion = true;
            }
            return saveSpy.size() == 1;
        });

        return completed
            && observedPendingConversion
            && panel.conversionStarts() == 1
            && panel.conversionFinishes() == 1
            && controller.chart()->meta().audioFile == QStringLiteral("source.ogg")
            && QFile::exists(directory.filePath(QStringLiteral("source.ogg")));
    }

    bool testStaleAudioConversionDoesNotReplaceNewChart()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString firstChartPath = directory.filePath(QStringLiteral("first.mc"));
        const QString secondChartPath = directory.filePath(QStringLiteral("second.mc"));
        const QString sourcePath = directory.filePath(QStringLiteral("stale.wav"));
        QFile source(sourcePath);
        if (!source.open(QIODevice::WriteOnly) || source.write("not-an-ogg") <= 0)
            return false;
        source.close();

        ChartController controller;
        if (!controller.loadChartFromData(firstChartPath, Chart{}))
            return false;
        DeferredMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *audioEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaAudioFileEdit"));
        if (!audioEdit)
            return false;

        QSignalSpy saveSpy(&panel, &MetaEditPanel::saveRequested);
        audioEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection)
            || panel.conversionStarts() != 1)
        {
            return false;
        }

        Chart replacement;
        replacement.meta().audioFile = QStringLiteral("new.ogg");
        if (!controller.loadChartFromData(secondChartPath, std::move(replacement)))
            return false;
        if (!waitUntil([&]() { return panel.conversionFinishes() == 1; }))
            return false;

        return controller.chartFilePath() == secondChartPath
            && controller.chart()->meta().audioFile == QStringLiteral("new.ogg")
            && saveSpy.isEmpty()
            && !QFile::exists(directory.filePath(QStringLiteral("stale.ogg")));
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    struct TestCase
    {
        const char *name;
        bool (*run)();
    };
    const TestCase cases[] = {
        {"Save waits for async audio conversion", &testSaveWaitsForAsyncAudioConversion},
        {"Stale audio conversion cannot replace a new chart", &testStaleAudioConversionDoesNotReplaceNewChart},
    };

    int failures = 0;
    for (const TestCase &test : cases)
    {
        if (test.run())
        {
            std::fprintf(stdout, "PASSED: %s\n", test.name);
        }
        else
        {
            std::fprintf(stderr, "FAILED: %s\n", test.name);
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
