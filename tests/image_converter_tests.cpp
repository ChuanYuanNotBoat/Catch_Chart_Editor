#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

#include <cstdio>

#include "utils/ImageConverter.h"

namespace
{
constexpr int ResultPass = 0;
constexpr int ResultFail = 1;
constexpr int ResultSkip = 2;

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(bytes) == bytes.size();
}

bool writeImage(const QString &path, const QImage &image, const QByteArray &format)
{
    QImageWriter writer(path);
    writer.setFormat(format);
    return writer.write(image);
}

QStringList dirEntryNames(const QString &dirPath)
{
    QStringList names;
    const QFileInfoList entries = QDir(dirPath).entryInfoList(
        QDir::Files | QDir::Hidden, QDir::Name);
    for (const QFileInfo &entry : entries)
        names << entry.fileName();
    return names;
}

bool isFormatSupported(const QByteArray &format)
{
    return QImageReader::supportedImageFormats().contains(format);
}

bool isWritableFormat(const QByteArray &format)
{
    return QImageWriter::supportedImageFormats().contains(format);
}

// Well-known 1x1 static GIF89a (white background pixel).
QByteArray staticGifBytes()
{
    return QByteArray::fromBase64(
        "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
}

// Hand-assembled multi-frame GIF89a: `frames` identical 1x1 image blocks.
QByteArray animatedGifBytes(int frames)
{
    QByteArray gif;
    gif += "GIF89a";
    // Logical screen descriptor: 1x1, global color table with 2 colors.
    gif += char(0x01); gif += char(0x00);
    gif += char(0x01); gif += char(0x00);
    gif += char(0x80); gif += char(0x00); gif += char(0x00);
    // Global color table: white, black.
    gif += char(0xFF); gif += char(0xFF); gif += char(0xFF);
    gif += char(0x00); gif += char(0x00); gif += char(0x00);
    for (int i = 0; i < frames; ++i)
    {
        // Graphic Control Extension (no transparency, delay 10).
        gif += char(0x21); gif += char(0xF9); gif += char(0x04);
        gif += char(0x00);
        gif += char(0x0A); gif += char(0x00);
        gif += char(0x00);
        gif += char(0x00);
        // Image descriptor: 1x1 at (0,0), no local color table.
        gif += char(0x2C);
        gif += char(0x00); gif += char(0x00);
        gif += char(0x00); gif += char(0x00);
        gif += char(0x01); gif += char(0x00);
        gif += char(0x01); gif += char(0x00);
        gif += char(0x00);
        // LZW data: minimum code size 2 + one 1-pixel sub-block.
        gif += char(0x02);
        gif += char(0x02); gif += char(0x44); gif += char(0x01);
        gif += char(0x00);
    }
    gif += char(0x3B);
    return gif;
}

// Little-endian TIFF payload carrying a single Orientation tag inside APP1.
QByteArray buildExifPayload(quint16 orientation)
{
    QByteArray payload;
    payload += "Exif";
    payload += char(0x00);
    payload += char(0x00);
    QDataStream stream(&payload, QIODevice::Append | QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0x4949)   // "II" little-endian
           << quint16(42)       // TIFF magic
           << quint32(8)        // first IFD offset
           << quint16(1)        // IFD entry count
           << quint16(0x0112)   // Orientation tag
           << quint16(3)        // SHORT
           << quint32(1)        // value count
           << quint16(orientation)
           << quint16(0)        // padding
           << quint32(0);       // next IFD offset
    return payload;
}

QByteArray wrapApp1(const QByteArray &payload)
{
    QByteArray segment;
    segment += char(0xFF);
    segment += char(0xE1);
    const quint16 length = quint16(payload.size() + 2);
    segment += char(quint8(length >> 8));
    segment += char(quint8(length & 0xFF));
    segment += payload;
    return segment;
}

// A real 2x1 JPEG with an EXIF APP1 segment inserted right after SOI.
QByteArray makeJpegWithExif(quint16 orientation)
{
    QImage image(2, 1, QImage::Format_RGB32);
    image.fill(0xFF0000);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "jpeg");
    const QByteArray jpeg = buffer.data();
    return jpeg.left(2) + wrapApp1(buildExifPayload(orientation)) + jpeg.mid(2);
}

QSize readSize(const QString &path, bool autoTransform)
{
    QImageReader reader(path);
    reader.setAutoTransform(autoTransform);
    const QImage image = reader.read();
    return image.isNull() ? QSize() : image.size();
}

// ---------------------------------------------------------------------------
// Test cases. Each returns ResultPass / ResultFail / ResultSkip.
// ---------------------------------------------------------------------------

int testPngPayloadIsNativeEvenWithWrongExtension()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(0xFF00FF00);
    if (!writeImage(dir.filePath("real.png"), image, "png"))
        return ResultFail;

    // Same PNG payload stored with a misleading .jpg extension.
    QFile realFile(dir.filePath("real.png"));
    if (!realFile.open(QIODevice::ReadOnly))
        return ResultFail;
    const QByteArray bytes = realFile.readAll();
    realFile.close();
    if (!writeFile(dir.filePath("fake.jpg"), bytes))
        return ResultFail;

    if (ImageConverter::inspectImage(dir.filePath("real.png")) != ImageConverter::ImageCategory::Native)
        return ResultFail;
    if (ImageConverter::inspectImage(dir.filePath("fake.jpg")) != ImageConverter::ImageCategory::Native)
        return ResultFail;
    return ResultPass;
}

int testJpegPayloadIsNative()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    QImage image(3, 2, QImage::Format_RGB32);
    image.fill(0xFFFF0000);
    if (!writeImage(dir.filePath("photo.jpg"), image, "jpeg"))
        return ResultFail;
    if (ImageConverter::inspectImage(dir.filePath("photo.jpg")) != ImageConverter::ImageCategory::Native)
        return ResultFail;
    return ResultPass;
}

int testBmpConvertsToPngWithIdenticalPixels()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    QImage source(3, 3, QImage::Format_RGB32);
    source.fill(0xFF00FF00);
    source.setPixel(1, 1, 0xFFFF0000);
    if (!writeImage(dir.filePath("source.bmp"), source, "bmp"))
        return ResultFail;

    const ImageConverter::ImageCategory category =
        ImageConverter::inspectImage(dir.filePath("source.bmp"));
    if (category != ImageConverter::ImageCategory::SingleFrame)
    {
        std::fprintf(stderr, "  bmp category=%d\n", int(category));
        return ResultFail;
    }

    QString error;
    const QString out = dir.filePath("source.png");
    if (!ImageConverter::convertToPng(dir.filePath("source.bmp"), out, &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }

    QImageReader reader(out);
    if (!reader.canRead())
    {
        std::fprintf(stderr, "  output unreadable\n");
        return ResultFail;
    }
    const QByteArray outFormat = reader.format();
    const QImage result = reader.read();
    if (outFormat != "png" || result.size() != QSize(3, 3))
    {
        std::fprintf(stderr, "  output format=%s size=%dx%d valid=%d\n",
                     qUtf8Printable(QString(outFormat)),
                     result.width(), result.height(), int(!result.isNull()));
        return ResultFail;
    }
    if (result.pixel(1, 1) != 0xFFFF0000 || result.pixel(0, 0) != 0xFF00FF00)
    {
        std::fprintf(stderr, "  pixel mismatch: p11=%08x p00=%08x\n",
                     result.pixel(1, 1), result.pixel(0, 0));
        return ResultFail;
    }
    return ResultPass;
}

int testStaticWebpConvertsToPng()
{
    if (!isFormatSupported("webp") || !isWritableFormat("webp"))
        return ResultSkip;

    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    QImage source(2, 2, QImage::Format_RGB32);
    source.fill(0xFF0000FF);
    if (!writeImage(dir.filePath("pic.webp"), source, "webp"))
        return ResultFail;

    if (ImageConverter::inspectImage(dir.filePath("pic.webp"))
        != ImageConverter::ImageCategory::SingleFrame)
        return ResultFail;

    QString error;
    if (!ImageConverter::convertToPng(dir.filePath("pic.webp"), dir.filePath("pic.png"), &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }
    return ResultPass;
}

int testAlphaSurvivesConversionToPng()
{
    // Qt's BMP writer does not carry alpha in this environment, so a
    // transparent single-frame GIF is used as the non-native alpha source.
    if (!isFormatSupported("gif"))
        return ResultSkip;

    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    // Single-frame GIF89a with a fully transparent pixel (index 0).
    QByteArray gif;
    gif += "GIF89a";
    gif += char(0x01); gif += char(0x00);
    gif += char(0x01); gif += char(0x00);
    gif += char(0x80); gif += char(0x00); gif += char(0x00);
    // Global color table: opaque white, opaque black.
    gif += char(0xFF); gif += char(0xFF); gif += char(0xFF);
    gif += char(0x00); gif += char(0x00); gif += char(0x00);
    // GCE with transparency flag set, transparent index 0.
    gif += char(0x21); gif += char(0xF9); gif += char(0x04);
    gif += char(0x01);
    gif += char(0x00); gif += char(0x00);
    gif += char(0x00);
    gif += char(0x00);
    // Image descriptor: 1x1 at (0,0).
    gif += char(0x2C);
    gif += char(0x00); gif += char(0x00);
    gif += char(0x00); gif += char(0x00);
    gif += char(0x01); gif += char(0x00);
    gif += char(0x01); gif += char(0x00);
    gif += char(0x00);
    // LZW data: pixel color index 0 (the transparent one).
    gif += char(0x02);
    gif += char(0x02); gif += char(0x44); gif += char(0x01);
    gif += char(0x00);
    gif += char(0x3B);

    if (!writeFile(dir.filePath("alpha.gif"), gif))
        return ResultFail;

    // Sanity: the source must really carry alpha.
    QImageReader verifyReader(dir.filePath("alpha.gif"));
    if (!verifyReader.canRead())
        return ResultSkip;
    const QImage verify = verifyReader.read();
    if (verify.isNull() || !verify.hasAlphaChannel())
    {
        std::fprintf(stderr, "  note: transparent GIF round-trip has no alpha\n");
        return ResultSkip;
    }

    QString error;
    if (!ImageConverter::convertToPng(dir.filePath("alpha.gif"), dir.filePath("alpha.png"), &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }

    QImageReader reader(dir.filePath("alpha.png"));
    if (!reader.canRead())
    {
        std::fprintf(stderr, "  alpha.png unreadable\n");
        return ResultFail;
    }
    const QByteArray outFormat = reader.format();
    const QImage result = reader.read();
    if (outFormat != "png" || !result.hasAlphaChannel())
        return ResultFail;
    if (qAlpha(result.pixel(0, 0)) != 0)
        return ResultFail;
    return ResultPass;
}

int testJpegExifOrientationIsAppliedOnConversion()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    if (!writeFile(dir.filePath("rotated.jpg"), makeJpegWithExif(6)))
        return ResultFail;

    // Sanity: EXIF-aware reading must rotate the 2x1 source into 1x2.
    if (readSize(dir.filePath("rotated.jpg"), true) != QSize(1, 2))
    {
        std::fprintf(stderr, "  EXIF orientation not applied by QImageReader\n");
        return ResultSkip;
    }

    if (ImageConverter::inspectImage(dir.filePath("rotated.jpg"))
        != ImageConverter::ImageCategory::Native)
        return ResultFail;

    QString error;
    if (!ImageConverter::convertToPng(dir.filePath("rotated.jpg"), dir.filePath("rotated.png"), &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }
    if (readSize(dir.filePath("rotated.png"), false) != QSize(1, 2))
        return ResultFail;
    return ResultPass;
}

int testStaticGifConvertsToPng()
{
    if (!isFormatSupported("gif"))
        return ResultSkip;

    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    if (!writeFile(dir.filePath("still.gif"), staticGifBytes()))
        return ResultFail;
    if (ImageConverter::inspectImage(dir.filePath("still.gif"))
        != ImageConverter::ImageCategory::SingleFrame)
        return ResultFail;

    QString error;
    if (!ImageConverter::convertToPng(dir.filePath("still.gif"), dir.filePath("still.png"), &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }
    if (readSize(dir.filePath("still.png"), false) != QSize(1, 1))
        return ResultFail;
    return ResultPass;
}

int testAnimatedGifIsDetectedAsMultiFrame()
{
    if (!isFormatSupported("gif"))
        return ResultSkip;

    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    if (!writeFile(dir.filePath("anim.gif"), animatedGifBytes(2)))
        return ResultFail;

    QImageReader reader(dir.filePath("anim.gif"));
    if (!reader.canRead())
    {
        std::fprintf(stderr, "  crafted animated GIF cannot be read\n");
        return ResultFail;
    }
    if (reader.imageCount() != 2)
    {
        std::fprintf(stderr, "  expected imageCount()==2, got %d\n", reader.imageCount());
        return ResultFail;
    }
    if (ImageConverter::inspectImage(dir.filePath("anim.gif"))
        != ImageConverter::ImageCategory::MultiFrame)
        return ResultFail;
    return ResultPass;
}

int testOptionalWebpAndTiffStaticImagesAreRecognized()
{
    const bool webpSupported = isFormatSupported("webp");
    const bool tiffSupported = isFormatSupported("tiff");
    if (!webpSupported && !tiffSupported)
    {
        std::fprintf(stderr,
                     "  note: webp/tiff plugins absent in this environment; "
                     "multi-frame detection for them cannot be exercised\n");
        return ResultSkip;
    }
    // QImageWriter cannot produce multi-frame WebP/TIFF, so the converter
    // itself can only be smoke-tested on static payloads here.
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(0xFF00FFFF);
    if (webpSupported && isWritableFormat("webp"))
    {
        if (!writeImage(dir.filePath("s.webp"), image, "webp"))
            return ResultFail;
        if (ImageConverter::inspectImage(dir.filePath("s.webp"))
            != ImageConverter::ImageCategory::SingleFrame)
            return ResultFail;
    }
    if (tiffSupported && isWritableFormat("tiff"))
    {
        if (!writeImage(dir.filePath("s.tiff"), image, "tiff"))
            return ResultFail;
        if (ImageConverter::inspectImage(dir.filePath("s.tiff"))
            != ImageConverter::ImageCategory::SingleFrame)
            return ResultFail;
    }
    return ResultPass;
}

int testCorruptImagesFailWithoutLeavingArtifacts()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    // 1) Random garbage with an image extension.
    if (!writeFile(dir.filePath("junk.jpg"), "this is definitely not an image"))
        return ResultFail;
    if (ImageConverter::inspectImage(dir.filePath("junk.jpg"))
        != ImageConverter::ImageCategory::Unreadable)
        return ResultFail;
    QString error;
    if (ImageConverter::convertToPng(dir.filePath("junk.jpg"), dir.filePath("junk.png"), &error))
        return ResultFail;
    if (error.isEmpty())
        return ResultFail;

    // 2) Valid PNG signature followed by garbage: the header canRead()s, but
    // a real decode fails, so inspectImage() must reject it as Unreadable.
    QByteArray truncated = QByteArrayLiteral("\x89PNG\r\n\x1a\n");
    truncated += QByteArray(64, 'x');
    if (!writeFile(dir.filePath("trunc.png"), truncated))
        return ResultFail;
    if (ImageConverter::inspectImage(dir.filePath("trunc.png"))
        != ImageConverter::ImageCategory::Unreadable)
        return ResultFail;
    QString error2;
    if (ImageConverter::convertToPng(dir.filePath("trunc.png"), dir.filePath("trunc_out.png"), &error2))
        return ResultFail;

    // 3) Truncated JPEG: valid SOI/JFIF header (canRead() == true) but the
    // image data is garbage, so read() must fail.
    QImage validJpeg(2, 2, QImage::Format_RGB32);
    validJpeg.fill(0xFF000000);
    QBuffer jpegBuffer;
    jpegBuffer.open(QIODevice::WriteOnly);
    if (!validJpeg.save(&jpegBuffer, "jpeg"))
        return ResultFail;
    QByteArray truncatedJpeg = jpegBuffer.data().left(24); // SOI + APP0/JFIF header only
    truncatedJpeg += QByteArray(64, '\xAA');
    if (!writeFile(dir.filePath("trunc.jpg"), truncatedJpeg))
        return ResultFail;
    if (ImageConverter::inspectImage(dir.filePath("trunc.jpg"))
        != ImageConverter::ImageCategory::Unreadable)
        return ResultFail;
    QString error3;
    if (ImageConverter::convertToPng(dir.filePath("trunc.jpg"), dir.filePath("trunc_jpg.png"), &error3))
        return ResultFail;

    // No output files and no leftover temp files.
    const QStringList entries = dirEntryNames(dir.path());
    for (const QString &name : entries)
    {
        if (name == "junk.jpg" || name == "trunc.png" || name == "trunc.jpg")
            continue;
        std::fprintf(stderr, "  unexpected leftover file: %s\n", qUtf8Printable(name));
        return ResultFail;
    }
    return ResultPass;
}

int testContentSniffingOverridesExtensions()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    // BMP payload hidden behind a .gif extension must still be decoded by
    // content and converted to PNG.
    QImage source(2, 2, QImage::Format_RGB32);
    source.fill(0xFF123456);
    if (!writeImage(dir.filePath("payload.bmp"), source, "bmp"))
        return ResultFail;
    QFile bmpFile(dir.filePath("payload.bmp"));
    if (!bmpFile.open(QIODevice::ReadOnly))
        return ResultFail;
    const QByteArray bmpBytes = bmpFile.readAll();
    bmpFile.close();
    if (!writeFile(dir.filePath("mislabeled.gif"), bmpBytes))
        return ResultFail;

    if (ImageConverter::inspectImage(dir.filePath("mislabeled.gif"))
        != ImageConverter::ImageCategory::SingleFrame)
        return ResultFail;

    QString error;
    if (!ImageConverter::convertToPng(dir.filePath("mislabeled.gif"), dir.filePath("mislabeled.png"), &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }
    if (readSize(dir.filePath("mislabeled.png"), false) != QSize(2, 2))
        return ResultFail;
    return ResultPass;
}

int testLargeImageConversionPerformance()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return ResultFail;

    QImage source(3000, 2000, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
    {
        QRgb *line = reinterpret_cast<QRgb *>(source.scanLine(y));
        for (int x = 0; x < source.width(); ++x)
            line[x] = qRgb((x * 255) / source.width(), (y * 255) / source.height(), 128);
    }
    if (!writeImage(dir.filePath("big.bmp"), source, "bmp"))
        return ResultFail;

    QElapsedTimer timer;
    timer.start();
    QString error;
    if (!ImageConverter::convertToPng(dir.filePath("big.bmp"), dir.filePath("big.png"), &error))
    {
        std::fprintf(stderr, "  convert error: %s\n", qUtf8Printable(error));
        return ResultFail;
    }
    std::printf("  info: 3000x2000 BMP->PNG conversion took %lld ms\n", timer.elapsed());
    return ResultPass;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    struct TestCase
    {
        const char *name;
        int (*run)();
    };
    const TestCase cases[] = {
        {"PNG payload is native even with wrong extension", &testPngPayloadIsNativeEvenWithWrongExtension},
        {"JPEG payload is native", &testJpegPayloadIsNative},
        {"BMP converts to PNG with identical pixels", &testBmpConvertsToPngWithIdenticalPixels},
        {"Static WebP converts to PNG", &testStaticWebpConvertsToPng},
        {"Alpha survives conversion to PNG", &testAlphaSurvivesConversionToPng},
        {"JPEG EXIF orientation is applied on conversion", &testJpegExifOrientationIsAppliedOnConversion},
        {"Static GIF converts to PNG", &testStaticGifConvertsToPng},
        {"Animated GIF is detected as multi-frame", &testAnimatedGifIsDetectedAsMultiFrame},
        {"Optional WebP/TIFF static images are recognized", &testOptionalWebpAndTiffStaticImagesAreRecognized},
        {"Corrupt images fail without leaving artifacts", &testCorruptImagesFailWithoutLeavingArtifacts},
        {"Content sniffing overrides extensions", &testContentSniffingOverridesExtensions},
        {"Large image conversion performance", &testLargeImageConversionPerformance},
    };

    int failures = 0;
    for (const TestCase &test : cases)
    {
        const int result = test.run();
        if (result == ResultPass)
            std::fprintf(stdout, "PASSED: %s\n", test.name);
        else if (result == ResultSkip)
            std::fprintf(stdout, "SKIPPED: %s\n", test.name);
        else
        {
            std::fprintf(stderr, "FAILED: %s\n", test.name);
            ++failures;
        }
    }
    if (failures == 0)
    {
        std::printf("All image converter tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d image converter test(s) failed.\n", failures);
    return 1;
}

