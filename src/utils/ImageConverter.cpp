#include "utils/ImageConverter.h"

#include <QIODevice>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>

namespace ImageConverter
{
namespace
{
// QImageReader reports the content-sniffed format in lower case letters.
bool isNativePayload(const QByteArray &format)
{
    return format == "png" || format == "jpeg" || format == "jpg";
}
} // namespace

ImageCategory inspectImage(const QString &path)
{
    QImageReader reader(path);
    // Identification is always based on the actual payload, never on the
    // file extension.
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);

    // canRead() sniffs the actual payload: files whose content cannot be
    // recognized never pass, regardless of their extension.
    if (!reader.canRead())
        return ImageCategory::Unreadable;

    // QImageReader::imageCount() semantics:
    //   > 1 : multi-frame image
    //   = 1 : single frame
    //   = 0 : the format does not carry animation
    //   < 0 : reader error
    const int frameCount = reader.imageCount();
    if (frameCount < 0)
        return ImageCategory::Unknown;

    const QByteArray format = reader.format();
    if (frameCount > 1)
        return ImageCategory::MultiFrame;

    if (isNativePayload(format))
    {
        // canRead() is only a cheap header check; a file whose header looks
        // valid but whose body is broken would otherwise be copied into the
        // chart directory untouched. Perform a real decode before trusting
        // the Native classification.
        const QImage probe = reader.read();
        if (probe.isNull())
            return ImageCategory::Unreadable;
        return ImageCategory::Native;
    }

    // frameCount == 0 or 1: safe to convert when the payload is not already
    // PNG/JPEG (those are imported as-is).
    return ImageCategory::SingleFrame;
}

bool convertToPng(const QString &inputPath, const QString &outputPath, QString *outError)
{
    const auto fail = [outError](const QString &message) -> bool
    {
        if (outError)
            *outError = message;
        return false;
    };

    QImageReader reader(inputPath);
    // Same content-based detection strategy as inspectImage(), plus EXIF
    // auto-transform: without it a rotated JPEG would be exported lying on
    // its side.
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return fail(reader.errorString());

    // QSaveFile writes into a temporary file inside the final directory and
    // only renames it after a successful commit, so a failed conversion never
    // leaves a partially written PNG behind.
    QSaveFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly))
        return fail(out.errorString());

    QImageWriter writer;
    writer.setDevice(&out);
    writer.setFormat("png");
    if (!writer.write(image))
    {
        const QString error = writer.errorString();
        out.cancelWriting();
        return fail(error);
    }
    if (!out.commit())
        return fail(out.errorString());

    return true;
}
} // namespace ImageConverter
