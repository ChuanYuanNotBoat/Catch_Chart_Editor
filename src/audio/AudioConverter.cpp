#include "audio/AudioConverter.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QThread>
#include <QUrl>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
// VBR quality 0.5 (~160 kbps stereo) — comparable to typical Malody packs.
constexpr float kVorbisQuality = 0.5f;

// Vorbis supports 8000..192000 Hz. Sources outside that range are resampled
// to the closest supported rate.
bool isRateSupportedByVorbis(int rate)
{
    return rate >= 8000 && rate <= 192000;
}

int pickFallbackRate(int sourceRate)
{
    static constexpr int kCandidates[] = {8000, 11025, 22050, 44100, 48000, 96000, 192000};
    int best = 48000;
    for (int candidate : kCandidates)
        if (qAbs(candidate - sourceRate) < qAbs(best - sourceRate))
            best = candidate;
    return best;
}

float sampleToFloat(const QAudioBuffer &buffer, int frameIndex, int channelIndex)
{
    const QAudioFormat fmt = buffer.format();
    const int channels = qMax(1, fmt.channelCount());
    const int idx = frameIndex * channels + channelIndex;
    switch (fmt.sampleFormat())
    {
    case QAudioFormat::UInt8:
    {
        const auto *p = buffer.constData<quint8>();
        return (static_cast<float>(p[idx]) - 128.0f) / 128.0f;
    }
    case QAudioFormat::Int16:
    {
        const auto *p = buffer.constData<qint16>();
        return static_cast<float>(p[idx]) / 32768.0f;
    }
    case QAudioFormat::Int32:
    {
        const auto *p = buffer.constData<qint32>();
        return static_cast<float>(p[idx]) / 2147483648.0f;
    }
    case QAudioFormat::Float:
    {
        const auto *p = buffer.constData<float>();
        return p[idx];
    }
    default:
        return 0.0f;
    }
}

// RAII wrapper around the libvorbis encoder state.
struct VorbisEncoder
{
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    ogg_stream_state os;
    bool inited = false;

    bool init(int channels, int sampleRate)
    {
        vorbis_info_init(&vi);
        if (vorbis_encode_init_vbr(&vi, channels, sampleRate, kVorbisQuality) != 0)
        {
            vorbis_info_clear(&vi);
            return false;
        }
        vorbis_comment_init(&vc);
        vorbis_comment_add_tag(&vc, "ENCODER", "Malody Catch Editor");
        vorbis_analysis_init(&vd, &vi);
        vorbis_block_init(&vd, &vb);
        const long serial = static_cast<long>(QRandomGenerator::global()->generate());
        ogg_stream_init(&os, serial);
        inited = true;
        return true;
    }

    void cleanup()
    {
        if (!inited)
            return;
        ogg_stream_clear(&os);
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        inited = false;
    }

    ~VorbisEncoder() { cleanup(); }
};

bool writePage(QFile &out, const ogg_page &page)
{
    return out.write(reinterpret_cast<const char *>(page.header), page.header_len) ==
               static_cast<qint64>(page.header_len) &&
           out.write(reinterpret_cast<const char *>(page.body), page.body_len) ==
               static_cast<qint64>(page.body_len);
}

bool writeHeaderPages(QFile &out, VorbisEncoder &enc, QString *outError)
{
    ogg_packet headerMain;
    ogg_packet headerComments;
    ogg_packet headerCode;
    if (vorbis_analysis_headerout(&enc.vd, &enc.vc, &headerMain, &headerComments, &headerCode) != 0)
    {
        if (outError)
            *outError = QStringLiteral("Failed to build Vorbis stream headers.");
        return false;
    }
    ogg_stream_packetin(&enc.os, &headerMain);
    ogg_stream_packetin(&enc.os, &headerComments);
    ogg_stream_packetin(&enc.os, &headerCode);

    ogg_page page;
    while (ogg_stream_flush(&enc.os, &page))
    {
        if (!writePage(out, page))
        {
            if (outError)
                *outError = QStringLiteral("Failed to write Ogg headers to output file.");
            return false;
        }
    }
    return true;
}

// Drains all pending analysis blocks/packets into Ogg pages.
bool drainEncoder(QFile &out, VorbisEncoder &enc, bool flush, QString *outError)
{
    ogg_packet packet;
    ogg_page page;
    while (vorbis_analysis_blockout(&enc.vd, &enc.vb) == 1)
    {
        vorbis_analysis(&enc.vb, nullptr);
        vorbis_bitrate_addblock(&enc.vb);
        while (vorbis_bitrate_flushpacket(&enc.vd, &packet))
        {
            ogg_stream_packetin(&enc.os, &packet);
            while (ogg_stream_pageout(&enc.os, &page))
            {
                if (!writePage(out, page))
                {
                    if (outError)
                        *outError = QStringLiteral("Failed to write Ogg page to output file.");
                    return false;
                }
            }
        }
    }
    if (flush)
    {
        while (ogg_stream_flush(&enc.os, &page))
        {
            if (!writePage(out, page))
            {
                if (outError)
                    *outError = QStringLiteral("Failed to flush Ogg page to output file.");
                return false;
            }
        }
    }
    return true;
}

// Streaming linear resampler helper used only when the source sample rate
// is outside the Vorbis-supported range. `pos` is the fractional input-frame
// position relative to the current buffer; it carries over between buffers.
int resampledFrameCount(double pos, double step, int inFrames)
{
    if (inFrames <= pos)
        return 0;
    return static_cast<int>(std::floor((static_cast<double>(inFrames) - pos) / step));
}
} // namespace

namespace AudioConverter
{
bool isOggFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(4096);
    if (!head.startsWith(QByteArray("OggS")))
        return false;
    // Malody only supports Vorbis-in-Ogg. Opus/Theora/Speex streams use the
    // same OggS container but must be transcoded, so require the Vorbis
    // identification packet within the first pages of the stream.
    return head.contains(QByteArray("\x01vorbis", 7));
}

bool convertToOgg(const QString &inputPath,
                  const QString &outputPath,
                  QString *outError,
                  const std::function<bool(float)> &progress)
{
    auto fail = [outError](const QString &message)
    {
        if (outError)
            *outError = message;
        return false;
    };

    const QFileInfo inInfo(inputPath);
    if (!inInfo.exists() || !inInfo.isFile())
        return fail(QStringLiteral("Input audio file does not exist: %1").arg(inputPath));

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(QStringLiteral("Cannot open output file: %1").arg(outputPath));

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(inInfo.absoluteFilePath()));

    VorbisEncoder enc;
    int channels = 0;
    int sourceRate = 0;
    int encodeRate = 0;
    double resamplePos = 0.0;
    double resampleStep = 1.0;
    bool headersWritten = false;
    bool cancelled = false;
    bool decodeFailed = false;
    bool sawAnyBuffer = false;
    qint64 durationMs = 0;
    qint64 lastPositionMs = 0;
    QString decodeError;

    // Encodes one decoded buffer. Returns false on cancel or IO failure.
    const auto processBuffer = [&](const QAudioBuffer &buffer) -> bool
    {
        const QAudioFormat fmt = buffer.format();
        if (fmt.sampleRate() <= 0 || fmt.channelCount() <= 0 ||
            fmt.sampleFormat() == QAudioFormat::Unknown)
            return true;

        const int frames = buffer.frameCount();
        if (frames <= 0)
            return true;

        const int bufferChannels = fmt.channelCount();
        const int bufferRate = fmt.sampleRate();
        if (bufferChannels != channels || bufferRate != sourceRate)
        {
            // Format changed mid-stream (should not happen); re-align state.
            channels = bufferChannels;
            sourceRate = bufferRate;
            encodeRate = isRateSupportedByVorbis(sourceRate) ? sourceRate : pickFallbackRate(sourceRate);
            resamplePos = 0.0;
            resampleStep = static_cast<double>(sourceRate) / static_cast<double>(encodeRate);
            enc.cleanup();
            headersWritten = false;
        }

        if (!enc.inited)
        {
            if (!enc.init(channels, encodeRate))
                return fail(QStringLiteral("Vorbis encoder init failed "
                                           "(unsupported channel count / sample rate)."));
        }

        // Decode into per-channel float planes.
        std::vector<std::vector<float>> inPlanes(static_cast<size_t>(channels));
        for (auto &plane : inPlanes)
            plane.resize(static_cast<size_t>(frames));
        for (int frame = 0; frame < frames; ++frame)
            for (int ch = 0; ch < channels; ++ch)
                inPlanes[static_cast<size_t>(ch)][static_cast<size_t>(frame)] =
                    sampleToFloat(buffer, frame, ch);

        const bool needsResample = !qFuzzyCompare(resampleStep, 1.0);
        int outFrames = frames;
        std::vector<std::vector<float>> outPlanes;
        if (needsResample)
        {
            outFrames = resampledFrameCount(resamplePos, resampleStep, frames);
            if (outFrames <= 0)
            {
                resamplePos -= frames;
                return true;
            }
            outPlanes.resize(static_cast<size_t>(channels));
            for (int ch = 0; ch < channels; ++ch)
            {
                const std::vector<float> &src = inPlanes[static_cast<size_t>(ch)];
                std::vector<float> &dst = outPlanes[static_cast<size_t>(ch)];
                dst.resize(static_cast<size_t>(outFrames));
                for (int j = 0; j < outFrames; ++j)
                {
                    const double srcPos = resamplePos + static_cast<double>(j) * resampleStep;
                    const int i0 = qBound(0, static_cast<int>(srcPos), frames - 1);
                    const int i1 = qMin(i0 + 1, frames - 1);
                    const double t = srcPos - static_cast<double>(i0);
                    dst[static_cast<size_t>(j)] = static_cast<float>(
                        src[static_cast<size_t>(i0)] * (1.0 - t) + src[static_cast<size_t>(i1)] * t);
                }
            }
            resamplePos += static_cast<double>(outFrames) * resampleStep - frames;
        }
        else
        {
            outPlanes = std::move(inPlanes);
            resamplePos -= frames;
        }

        if (!headersWritten)
        {
            if (!writeHeaderPages(outFile, enc, outError))
                return false;
            headersWritten = true;
        }

        float **pcm = vorbis_analysis_buffer(&enc.vd, outFrames);
        for (int ch = 0; ch < channels; ++ch)
            memcpy(pcm[ch], outPlanes[static_cast<size_t>(ch)].data(),
                   sizeof(float) * static_cast<size_t>(outFrames));
        vorbis_analysis_wrote(&enc.vd, outFrames);
        sawAnyBuffer = true;

        return drainEncoder(outFile, enc, false, outError);
    };

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]()
                     {
        if (cancelled)
            return;
        QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid())
            return;
        if (!processBuffer(buffer))
        {
            decodeFailed = true;
            decoder.stop();
        } });

    QObject::connect(&decoder, &QAudioDecoder::durationChanged, &decoder, [&](qint64 duration)
                     { durationMs = qMax<qint64>(0, duration); });

    QObject::connect(&decoder, &QAudioDecoder::positionChanged, &decoder, [&](qint64 position)
                     {
        lastPositionMs = position;
        if (progress && durationMs > 0)
        {
            const float fraction = qBound(0.0f,
                static_cast<float>(static_cast<double>(lastPositionMs) / durationMs), 1.0f);
            if (!progress(fraction))
            {
                cancelled = true;
                decoder.stop();
            }
        } });

    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
                     &decoder, [&](QAudioDecoder::Error)
                     {
        decodeFailed = true;
        decodeError = decoder.errorString().isEmpty()
                          ? QStringLiteral("QAudioDecoder failed.")
                          : decoder.errorString(); });

    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    decoder.start();
    loop.exec();

    if (cancelled)
    {
        outFile.close();
        QFile::remove(outputPath);
        return fail(QStringLiteral("Conversion cancelled."));
    }
    if (decodeFailed)
    {
        outFile.close();
        QFile::remove(outputPath);
        return fail(QStringLiteral("Audio decode failed: %1").arg(decodeError));
    }
    if (!sawAnyBuffer || !enc.inited)
    {
        outFile.close();
        QFile::remove(outputPath);
        return fail(QStringLiteral("No decodable audio samples found in: %1").arg(inputPath));
    }

    // Flush the encoder and write the end-of-stream page.
    vorbis_analysis_wrote(&enc.vd, 0);
    if (!drainEncoder(outFile, enc, true, outError))
    {
        outFile.close();
        QFile::remove(outputPath);
        return false;
    }
    if (outError)
        outError->clear();
    return true;
}

QString convertToOggWithProgress(QWidget *parent,
                                 const QString &inputPath,
                                 const QString &outputPath,
                                 QString *outError)
{
    QProgressDialog dialog(QObject::tr("Converting audio to OGG, please wait..."),
                           QObject::tr("Cancel"), 0, 100, parent);
    dialog.setWindowTitle(QObject::tr("Audio Conversion"));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(0);
    dialog.setValue(0);
    dialog.show();

    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    std::atomic<float> progressValue{0.0f};
    QString threadError;

    QThread *worker = QThread::create([&]()
                                      {
        convertToOgg(inputPath, outputPath, &threadError,
                     [&progressValue, &cancelled](float fraction)
                     {
                         progressValue.store(fraction);
                         return !cancelled.load();
                     });
        done.store(true); });
    worker->start();

    while (!done.load())
    {
        if (dialog.wasCanceled())
            cancelled.store(true);
        dialog.setValue(qRound(progressValue.load() * 100.0f));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    worker->wait();
    delete worker;

    dialog.setValue(100);
    dialog.close();

    if (cancelled.load())
        QFile::remove(outputPath);
    if (outError)
        *outError = threadError;
    return (threadError.isEmpty() && !cancelled.load()) ? outputPath : QString();
}
} // namespace AudioConverter
