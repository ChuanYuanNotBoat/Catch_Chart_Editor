#include "BpmDetector.h"
#include "autotiming/AutoTiming.h"
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QUrl>
#include <QtMath>

namespace
{
static constexpr int kFmtPcmFloat = 5; // FMOD_SOUND_FORMAT_PCMFLOAT

static float sampleToFloat(const QAudioBuffer &buffer, int frameIndex, int channelIndex)
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

static QVector<float> resampleLinear(const QVector<float> &in, int inRate, int outRate)
{
    if (inRate <= 0 || outRate <= 0 || in.isEmpty() || inRate == outRate)
        return in;
    const double ratio = static_cast<double>(outRate) / static_cast<double>(inRate);
    const int outCount = qMax(1, static_cast<int>(qRound(in.size() * ratio)));
    QVector<float> out;
    out.resize(outCount);
    for (int i = 0; i < outCount; ++i)
    {
        const double src = static_cast<double>(i) / ratio;
        const int i0 = qBound(0, static_cast<int>(qFloor(src)), in.size() - 1);
        const int i1 = qMin(i0 + 1, in.size() - 1);
        const double t = src - static_cast<double>(i0);
        out[i] = static_cast<float>(in[i0] * (1.0 - t) + in[i1] * t);
    }
    return out;
}

static bool decodeMonoRange(const QString &audioFilePath,
                            double startMs,
                            double durationMs,
                            QVector<float> &outMono,
                            int &outSampleRate,
                            QString *outError)
{
    outMono.clear();
    outSampleRate = 0;
    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(audioFilePath));

    qint64 processedFrames = 0;
    bool success = true;
    QString errorText;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid())
            return;
        const QAudioFormat fmt = buffer.format();
        if (fmt.sampleRate() <= 0 || fmt.channelCount() <= 0 || fmt.sampleFormat() == QAudioFormat::Unknown)
            return;
        outSampleRate = fmt.sampleRate();
        const int channels = fmt.channelCount();
        const qint64 startFrame = static_cast<qint64>(qFloor(qMax(0.0, startMs) * outSampleRate / 1000.0));
        const qint64 endFrame = static_cast<qint64>(qCeil((qMax(0.0, startMs) + durationMs) * outSampleRate / 1000.0));
        const qint64 bufStart = processedFrames;
        const qint64 bufEnd = processedFrames + buffer.frameCount();
        processedFrames = bufEnd;
        const qint64 pickStart = qMax(bufStart, startFrame);
        const qint64 pickEnd = qMin(bufEnd, endFrame);
        if (pickStart >= pickEnd)
            return;
        const int localStart = static_cast<int>(pickStart - bufStart);
        const int localEnd = static_cast<int>(pickEnd - bufStart);
        outMono.reserve(outMono.size() + (localEnd - localStart));
        for (int frame = localStart; frame < localEnd; ++frame)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                sum += sampleToFloat(buffer, frame, ch);
            outMono.append(sum / static_cast<float>(channels));
        }
    });

    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), &decoder, [&](QAudioDecoder::Error) {
        success = false;
        errorText = decoder.errorString().isEmpty() ? QStringLiteral("QAudioDecoder failed.") : decoder.errorString();
    });

    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), &loop, &QEventLoop::quit);
    decoder.start();
    loop.exec();

    if (!success)
    {
        if (outError)
            *outError = errorText;
        return false;
    }
    if (outSampleRate <= 0 || outMono.isEmpty())
    {
        if (outError)
            *outError = "No decodable PCM samples in requested range.";
        return false;
    }
    return true;
}

static bool detectByMalodyCore(const QVector<float> &mono, int sampleRate, BpmDetector::DetectionResult &outResult, QString *outError)
{
    if (mono.size() < qMax(8000, sampleRate / 2))
    {
        if (outError)
            *outError = "Audio too short for reliable measurement.";
        return false;
    }

    int useRate = sampleRate;
    QVector<float> work = mono;
    if (useRate != 32000 && useRate != 44100 && useRate != 48000)
    {
        useRate = 44100;
        work = resampleLinear(mono, sampleRate, useRate);
    }

    try
    {
        const char *buf = reinterpret_cast<const char *>(work.constData());
        const uint32_t sizeBytes = static_cast<uint32_t>(work.size() * static_cast<int>(sizeof(float)));
        const AutoTiming::AutoTimingResult ret = AutoTiming::detect(buf, sizeBytes, kFmtPcmFloat, useRate, 1);
        if (!(ret.bpm > 0.0))
        {
            if (outError)
                *outError = "Auto timing returned no stable BPM.";
            return false;
        }

        outResult.bpm = ret.bpm;
        outResult.estimatedOffsetMs = ret.offset;
        return true;
    }
    catch (const std::exception &e)
    {
        if (outError)
            *outError = QString("Auto timing exception: %1").arg(QString::fromUtf8(e.what()));
        return false;
    }
    catch (...)
    {
        if (outError)
            *outError = "Auto timing exception: unknown error.";
        return false;
    }
}
} // namespace

bool BpmDetector::detectFromFile(const QString &audioFilePath,
                                 double startMs,
                                 double durationMs,
                                 double &outBpm,
                                 QString *outError)
{
    if (outError)
        outError->clear();
    outBpm = 0.0;
    DetectionResult ret;
    if (!detectFromFileDetailed(audioFilePath, startMs, durationMs, ret, outError))
        return false;
    outBpm = ret.bpm;
    return true;
}

bool BpmDetector::detectFromFileDetailed(const QString &audioFilePath,
                                         double startMs,
                                         double durationMs,
                                         DetectionResult &outResult,
                                         QString *outError)
{
    outResult = DetectionResult();
    if (audioFilePath.isEmpty())
    {
        if (outError)
            *outError = "Audio path is empty.";
        return false;
    }
    if (durationMs <= 0.0)
    {
        if (outError)
            *outError = "Duration must be > 0.";
        return false;
    }

    const double maxDurationMs = 120000.0;
    if (durationMs > maxDurationMs)
        durationMs = maxDurationMs;

    QVector<float> mono;
    int sampleRate = 0;
    if (!decodeMonoRange(audioFilePath, startMs, durationMs, mono, sampleRate, outError))
        return false;

    if (!detectByMalodyCore(mono, sampleRate, outResult, outError))
        return false;

    // Segment diagnostics are disabled for full-song default measurement.
    return true;
}
