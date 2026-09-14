#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QLineEdit>
#include <QSignalSpy>
#include <QStringList>
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

    // Non-modal MetaEditPanel that records background import feedback.
    class BackgroundSpyMetaEditPanel final : public MetaEditPanel
    {
    public:
        using MetaEditPanel::MetaEditPanel;

        int keptOriginalNotices() const { return m_keptOriginalNotices; }
        const QStringList &errorMessages() const { return m_errorMessages; }

    protected:
        void notifyBackgroundKeptOriginal(const QString &) override
        {
            ++m_keptOriginalNotices;
        }

        void showBackgroundImportError(const QString &message) override
        {
            m_errorMessages << message;
        }

    private:
        int m_keptOriginalNotices = 0;
        QStringList m_errorMessages;
    };

    bool writeImageFile(const QString &path, const QImage &image, const QByteArray &format)
    {
        QImageWriter writer(path);
        writer.setFormat(format);
        return writer.write(image);
    }

    // Hand-assembled two-frame 1x1 GIF89a (see image_converter_tests for the
    // byte layout rationale).
    QByteArray animatedGifBytes()
    {
        QByteArray gif;
        gif += "GIF89a";
        gif += char(0x01); gif += char(0x00);
        gif += char(0x01); gif += char(0x00);
        gif += char(0x80); gif += char(0x00); gif += char(0x00);
        gif += char(0xFF); gif += char(0xFF); gif += char(0xFF);
        gif += char(0x00); gif += char(0x00); gif += char(0x00);
        for (int i = 0; i < 2; ++i)
        {
            gif += char(0x21); gif += char(0xF9); gif += char(0x04);
            gif += char(0x00);
            gif += char(0x0A); gif += char(0x00);
            gif += char(0x00);
            gif += char(0x00);
            gif += char(0x2C);
            gif += char(0x00); gif += char(0x00);
            gif += char(0x00); gif += char(0x00);
            gif += char(0x01); gif += char(0x00);
            gif += char(0x01); gif += char(0x00);
            gif += char(0x00);
            gif += char(0x02);
            gif += char(0x02); gif += char(0x44); gif += char(0x01);
            gif += char(0x00);
        }
        gif += char(0x3B);
        return gif;
    }

    QStringList dirEntryNames(const QString &dirPath)
    {
        QStringList names;
        const QFileInfoList entries = QDir(dirPath).entryInfoList(QDir::Files | QDir::Hidden);
        for (const QFileInfo &entry : entries)
            names << entry.fileName();
        return names;
    }

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

    bool testBackgroundBmpImportConvertsToPng()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        const QString sourcePath = directory.filePath(QStringLiteral("source.bmp"));
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(0xFF00FF00);
        if (!writeImageFile(sourcePath, image, "bmp"))
            return false;

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        bgEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        return controller.chart()->meta().backgroundFile == QStringLiteral("source.png")
            && QFile::exists(directory.filePath(QStringLiteral("source.png")))
            && panel.keptOriginalNotices() == 0
            && panel.errorMessages().isEmpty();
    }

    bool testBackgroundImportTargetNameCollisionGetsSuffix()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        // Occupy the natural target name with a different valid PNG.
        QImage occupant(1, 1, QImage::Format_RGB32);
        occupant.fill(0xFFFFFFFF);
        if (!writeImageFile(directory.filePath(QStringLiteral("source.png")), occupant, "png"))
            return false;

        const QString sourcePath = directory.filePath(QStringLiteral("source.bmp"));
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(0xFF0000FF);
        if (!writeImageFile(sourcePath, image, "bmp"))
            return false;

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        bgEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        return controller.chart()->meta().backgroundFile == QStringLiteral("source_2.png")
            && QFile::exists(directory.filePath(QStringLiteral("source_2.png")));
    }

    bool testBackgroundImportFailureKeepsMetadata()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        const QString sourcePath = directory.filePath(QStringLiteral("corrupt.bmp"));
        QFile corrupt(sourcePath);
        if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("garbage-not-an-image") <= 0)
            return false;
        corrupt.close();

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        QSignalSpy saveSpy(&panel, &MetaEditPanel::saveRequested);
        bgEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        // Transactional: metadata untouched, no output, no temp leftovers.
        if (!controller.chart()->meta().backgroundFile.isEmpty())
            return false;
        if (panel.errorMessages().size() != 1 || !saveSpy.isEmpty())
            return false;
        const QStringList entries = dirEntryNames(directory.path());
        return entries.size() == 1 && entries.first() == QStringLiteral("corrupt.bmp");
    }

    bool testBackgroundCorruptNativeImageKeepsMetadata()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        // Valid PNG signature but broken body: canRead() would pass, so the
        // import must detect the failure through a real decode.
        QByteArray corruptPng = QByteArrayLiteral("\x89PNG\r\n\x1a\n");
        corruptPng += QByteArray(64, 'x');
        const QString sourcePath = directory.filePath(QStringLiteral("corrupt.png"));
        QFile corrupt(sourcePath);
        if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write(corruptPng) <= 0)
            return false;
        corrupt.close();

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        QSignalSpy saveSpy(&panel, &MetaEditPanel::saveRequested);
        bgEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        // Transactional: metadata untouched, error reported, and no target
        // resource file (or temp leftover) is created.
        if (!controller.chart()->meta().backgroundFile.isEmpty())
            return false;
        if (panel.errorMessages().size() != 1 || !saveSpy.isEmpty())
            return false;
        const QStringList entries = dirEntryNames(directory.path());
        return entries.size() == 1 && entries.first() == QStringLiteral("corrupt.png");
    }

    bool testBackgroundJpegImportedAsIs()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        const QString sourcePath = directory.filePath(QStringLiteral("photo.jpg"));
        QImage image(3, 2, QImage::Format_RGB32);
        image.fill(0xFFFF0000);
        if (!writeImageFile(sourcePath, image, "jpeg"))
            return false;

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        bgEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        return controller.chart()->meta().backgroundFile == QStringLiteral("photo.jpg")
            && QFile::exists(directory.filePath(QStringLiteral("photo.jpg")))
            && !QFile::exists(directory.filePath(QStringLiteral("photo.png")))
            && panel.keptOriginalNotices() == 0
            && panel.errorMessages().isEmpty();
    }

    bool testBackgroundAnimatedGifKeptOriginal()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        const QString sourcePath = directory.filePath(QStringLiteral("anim.gif"));
        QFile gif(sourcePath);
        if (!gif.open(QIODevice::WriteOnly) || gif.write(animatedGifBytes()) <= 0)
            return false;
        gif.close();

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        bgEdit->setText(sourcePath);
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        return controller.chart()->meta().backgroundFile == QStringLiteral("anim.gif")
            && QFile::exists(directory.filePath(QStringLiteral("anim.gif")))
            && !QFile::exists(directory.filePath(QStringLiteral("anim.png")))
            && panel.keptOriginalNotices() == 1
            && panel.errorMessages().isEmpty();
    }

    bool testBackgroundRelativeResourceNotReimported()
    {
        QTemporaryDir directory;
        if (!directory.isValid())
            return false;

        const QString chartPath = directory.filePath(QStringLiteral("chart.mc"));
        ChartController controller;
        if (!controller.loadChartFromData(chartPath, Chart{}))
            return false;

        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(0xFF00FF00);
        if (!writeImageFile(directory.filePath(QStringLiteral("bg.jpg")), image, "jpeg"))
            return false;

        BackgroundSpyMetaEditPanel panel;
        panel.setChartController(&controller);
        QLineEdit *bgEdit = panel.findChild<QLineEdit *>(QStringLiteral("metaBackgroundFileEdit"));
        if (!bgEdit)
            return false;

        // Legacy relative reference: must pass through untouched.
        bgEdit->setText(QStringLiteral("bg.jpg"));
        if (!QMetaObject::invokeMethod(&panel, "onSaveClicked", Qt::DirectConnection))
            return false;

        return controller.chart()->meta().backgroundFile == QStringLiteral("bg.jpg")
            && panel.keptOriginalNotices() == 0
            && panel.errorMessages().isEmpty()
            && dirEntryNames(directory.path()).size() == 1;
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
        {"Background BMP import converts to PNG", &testBackgroundBmpImportConvertsToPng},
        {"Background import target name collision gets suffix", &testBackgroundImportTargetNameCollisionGetsSuffix},
        {"Background import failure keeps metadata", &testBackgroundImportFailureKeepsMetadata},
        {"Background corrupt native image keeps metadata", &testBackgroundCorruptNativeImageKeepsMetadata},
        {"Background JPEG imported as-is", &testBackgroundJpegImportedAsIs},
        {"Background animated GIF kept original", &testBackgroundAnimatedGifKeptOriginal},
        {"Background relative resource not re-imported", &testBackgroundRelativeResourceNotReimported},
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
