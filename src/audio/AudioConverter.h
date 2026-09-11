#pragma once

#include <QString>
#include <functional>

class QWidget;
class QObject;

namespace AudioConverter
{
// True when the file is an Ogg bitstream ("OggS") that carries the Vorbis
// codec (identification packet "\x01vorbis"), regardless of its extension.
// Other Ogg-contained codecs (Opus, Theora, Speex, ...) return false on
// purpose: Malody only supports Vorbis-in-Ogg, so such files must be
// transcoded first.
bool isOggFile(const QString &path);

// Blocking conversion of any audio file decodable by QAudioDecoder
// (mp3 / wav / flac / m4a / aac / ...) into an OGG Vorbis file
// (VBR quality 0.5, original sample rate and channel layout are kept
// whenever the Vorbis encoder supports them, otherwise the audio is
// linearly resampled to the closest supported rate).
//
// This function blocks until the whole file is processed and therefore
// must be called from a worker thread (see convertToOggWithProgress for
// the UI-thread friendly wrapper).
//
// progress: optional callback receiving 0.0..1.0; return false to cancel.
bool convertToOgg(const QString &inputPath,
                  const QString &outputPath,
                  QString *outError = nullptr,
                  const std::function<bool(float)> &progress = {});

// Runs convertToOgg on a worker thread while showing a modal progress
// dialog. Completion is delivered on the context object's thread.
using AsyncCompletion = std::function<void(bool success, const QString &error)>;
void convertToOggWithProgressAsync(QObject *context,
                                   const QString &inputPath,
                                   const QString &outputPath,
                                   AsyncCompletion completion);

// Blocking compatibility wrapper. UI code should use
// convertToOggWithProgressAsync so the event loop remains responsive.
QString convertToOggWithProgress(QWidget *parent,
                                 const QString &inputPath,
                                 const QString &outputPath,
                                 QString *outError = nullptr);
} // namespace AudioConverter
