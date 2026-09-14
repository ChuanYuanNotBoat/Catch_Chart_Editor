#pragma once

#include <QString>

namespace ImageConverter
{
// Classification result for a source image file. Detection is always based
// on the actual file payload (QImageReader content sniffing), never on the
// file extension.
enum class ImageCategory
{
    Native,    // PNG or JPEG payload; safe to import as-is.
    SingleFrame, // Decodable single-frame image; convert to PNG.
    MultiFrame,  // imageCount() > 1; keep the original file untouched.
    Unknown,     // imageCount() < 0 (reader error); keep the original file
                 // untouched instead of risking silent frame loss.
    Unreadable   // QImageReader cannot decode the file at all.
};

// Inspects the file at path. Missing or undecodable files are reported as
// Unreadable.
ImageCategory inspectImage(const QString &path);

// Decodes inputPath (EXIF orientation applied via QImageReader::setAutoTransform)
// and writes it as PNG into outputPath. The payload is written through
// QSaveFile and only committed after QImageWriter succeeded, so a failed
// conversion never leaves a partially written PNG behind.
// Returns false and fills *outError on any failure.
bool convertToPng(const QString &inputPath,
                  const QString &outputPath,
                  QString *outError = nullptr);
} // namespace ImageConverter
