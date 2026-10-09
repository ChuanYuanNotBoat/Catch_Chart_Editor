#include "SpectrumService.h"
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QDateTime>
namespace
{
float sample(const QAudioBuffer &b, int i)
{
    switch (b.format().sampleFormat())
    {
    case QAudioFormat::UInt8:
        return (b.constData<quint8>()[i] - 128.f) / 128.f;
    case QAudioFormat::Int16:
        return b.constData<qint16>()[i] / 32768.f;
    case QAudioFormat::Int32:
        return float(b.constData<qint32>()[i] / 2147483648.0);
    case QAudioFormat::Float:
        return b.constData<float>()[i];
    default:
        return 0;
    }
}
} // namespace
SpectrumService::Result SpectrumService::analyzeFileRange(const QString &path, double startMs,
                                                          double durationMs,
                                                          const std::shared_ptr<std::atomic<bool>> &cancel)
{
    Result out;
    if (path.isEmpty() || !std::isfinite(startMs) || !std::isfinite(durationMs) || startMs < 0 ||
        startMs > 86400000 || durationMs <= 0 || durationMs > 120000)
    {
        out.error = "Invalid spectrum range (maximum 120 s)";
        return out;
    }
    if (cancel && cancel->load())
    {
        out.error = "Cancelled";
        return out;
    }
    QAudioDecoder decoder;
    QEventLoop loop;
    QTimer deadline, cancelPoll;
    deadline.setSingleShot(true);
    deadline.setInterval(60000);
    cancelPoll.setInterval(50);
    bool done = false;
    int rate = 0, channels = 0;
    qint64 processed = 0, first = -1;
    std::vector<float> pcm;
    auto finish = [&] {
        done = true;
        loop.quit();
    };
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        out.error = "Audio decode timed out";
        finish();
    });
    QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&] {
        if (cancel && cancel->load())
        {
            out.error = "Cancelled";
            finish();
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        if (done)
            return;
        if (cancel && cancel->load())
        {
            out.error = "Cancelled";
            finish();
            return;
        }
        const QAudioBuffer b = decoder.read();
        if (!b.isValid())
            return;
        const auto f = b.format();
        if (f.sampleFormat() == QAudioFormat::Unknown || f.sampleRate() < 1000 || f.sampleRate() > 384000 ||
            f.channelCount() < 1 || f.channelCount() > 32)
        {
            out.error = "Unsupported PCM format";
            finish();
            return;
        }
        if (rate && (rate != f.sampleRate() || channels != f.channelCount()))
        {
            out.error = "Audio format changed during decode";
            finish();
            return;
        }
        rate = f.sampleRate();
        channels = f.channelCount();
        const qint64 start = qint64(std::floor(startMs * rate / 1000));
        const qint64 end = qint64(std::ceil((startMs + durationMs) * rate / 1000));
        const qint64 from = std::max(processed, start), to = std::min(processed + b.frameCount(), end);
        if (to > from)
        {
            if (first < 0)
                first = from;
            if (pcm.size() + size_t(to - from) * 2 > 32 * 1024 * 1024)
            {
                out.error = "Spectrum PCM memory limit reached";
                finish();
                return;
            }
            for (qint64 i = from - processed; i < to - processed; ++i)
            {
                pcm.push_back(sample(b, int(i) * channels));
                pcm.push_back(sample(b, int(i) * channels + std::min(1, channels - 1)));
            }
        }
        processed += b.frameCount();
        if (processed >= end)
            finish();
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, finish);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), &loop,
                     [&](QAudioDecoder::Error) {
                         out.error = decoder.errorString().toStdString();
                         finish();
                     });
    decoder.setSource(QUrl::fromLocalFile(path));
    deadline.start();
    cancelPoll.start();
    decoder.start();
    if (!done)
        loop.exec();
    decoder.stop();
    if (!out.error.empty())
        return out;
    if (pcm.empty())
    {
        out.error = "No audio in spectrum range";
        return out;
    }
    out = analysis::analyzeStereo(pcm, rate, 2, double(first) / rate, cancel.get());
    out.sourceChannels = channels;
    return out;
}
void SpectrumService::analyzeFileRangeAsync(QObject *context, const QString &path, double start,
                                            double duration, std::shared_ptr<std::atomic<bool>> cancel,
                                            Callback callback)
{
    auto *watcher = new QFutureWatcher<Result>(context);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, context,
                     [watcher, callback = std::move(callback)] {
                         auto result = watcher->result();
                         watcher->deleteLater();
                         callback(std::move(result));
                     });
    watcher->setFuture(QtConcurrent::run(
        [path, start, duration, cancel] { return analyzeFileRange(path, start, duration, cancel); }));
}
void SpectrumService::prepareFileRangeAsync(QObject *context, const QString &path, double start, double duration,
                                            std::shared_ptr<std::atomic<bool>> cancel, PageCallback callback)
{
    auto *watcher = new QFutureWatcher<Page>(context);
    QObject::connect(watcher, &QFutureWatcher<Page>::finished, context, [watcher, callback = std::move(callback)] {
        auto result = watcher->result();
        watcher->deleteLater();
        callback(std::move(result));
    });
    watcher->setFuture(QtConcurrent::run([path, start, duration, cancel] {
        Page page;
        auto spectrum = analyzeFileRange(path, start, duration, cancel);
        if (spectrum.valid())
            page.raster = analysis::prepareSpectrumRaster(spectrum, cancel.get());
        if (spectrum.valid() && !page.raster.valid())
            spectrum.error = cancel && cancel->load() ? "Cancelled" : "Invalid spectrum raster";
        page.spectrum = std::make_shared<const Result>(std::move(spectrum));
        return page;
    }));
}
SpectrumService::EnergyEnvelope SpectrumService::analyzeEnergy(const QString &path,
                                                               const std::shared_ptr<std::atomic<bool>> &cancel)
{
    EnergyEnvelope out;
    struct Bucket
    {
        double squares = 0, peak = 0;
        qint64 frames = 0;
    };
    QVector<Bucket> buckets;
    Bucket current;
    int rate = 0, channels = 0;
    qint64 stride = 1, processed = 0;
    QFileInfo identity(path);
    const auto initialSize = identity.size();
    const auto initialTime = identity.lastModified();
    QAudioDecoder decoder;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(50);
    deadline.setSingleShot(true);
    deadline.setInterval(120000);
    bool done = false;
    auto finish = [&] {
        done = true;
        loop.quit();
    };
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancel && cancel->load())
        {
            out.error = "Cancelled";
            finish();
        }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        out.error = "Whole-audio energy decode timed out";
        finish();
    });
    auto store = [&] {
        buckets.append(current);
        current = {};
        if (buckets.size() >= 4096)
        {
            QVector<Bucket> reduced;
            reduced.reserve(2048);
            for (int i = 0; i < buckets.size(); i += 2)
                reduced.append({buckets[i].squares + buckets[i + 1].squares, qMax(buckets[i].peak, buckets[i + 1].peak),
                                buckets[i].frames + buckets[i + 1].frames});
            buckets = std::move(reduced);
            stride *= 2;
        }
    };
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        if (done)
            return;
        const auto buffer = decoder.read();
        if (!buffer.isValid())
            return;
        auto format = buffer.format();
        if (format.sampleRate() < 1000 || format.sampleRate() > 384000 || format.channelCount() < 1
            || format.channelCount() > 32 || format.sampleFormat() == QAudioFormat::Unknown
            || (rate && (rate != format.sampleRate() || channels != format.channelCount())))
        {
            out.error = "Unsupported/changing audio format";
            finish();
            return;
        }
        if (!rate)
        {
            rate = format.sampleRate();
            channels = format.channelCount();
            stride = qMax(1, rate / 8);
        }
        for (int frame = 0; frame < buffer.frameCount(); ++frame)
        {
            if ((frame & 4095) == 0 && cancel && cancel->load())
            {
                out.error = "Cancelled";
                finish();
                return;
            }
            double energy = 0, peak = 0;
            for (int ch = 0; ch < channels; ++ch)
            {
                double v = sample(buffer, frame * channels + ch);
                if (!std::isfinite(v))
                {
                    out.error = "Non-finite audio sample";
                    finish();
                    return;
                }
                energy += v * v;
                peak = qMax(peak, qAbs(v));
            }
            current.squares += energy / channels;
            current.peak = qMax(current.peak, peak);
            ++current.frames;
            ++processed;
            if (current.frames >= stride)
                store();
        }
        if (processed / double(rate) > 86400)
        {
            out.error = "Whole-audio overview exceeds 24 hours";
            finish();
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, finish);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), &loop, [&](auto) {
        out.error = decoder.errorString();
        finish();
    });
    decoder.setSource(QUrl::fromLocalFile(path));
    poll.start();
    deadline.start();
    decoder.start();
    if (!done)
        loop.exec();
    decoder.stop();
    identity.refresh();
    if (identity.size() != initialSize || identity.lastModified() != initialTime)
        out.error = "Audio changed during overview decode";
    if (!out.error.isEmpty())
        return out;
    if (current.frames)
        store();
    if (!rate || buckets.isEmpty())
    {
        out.error = "No audio for overview";
        return out;
    }
    double time = 0;
    for (const auto &b : buckets)
    {
        double end = time + b.frames / double(rate);
        out.bins.append({time, end, std::sqrt(b.squares / b.frames), b.peak});
        time = end;
    }
    out.durationSeconds = time;
    return out;
}
void SpectrumService::analyzeEnergyAsync(QObject *context, const QString &path,
                                         std::shared_ptr<std::atomic<bool>> cancel, EnergyCallback callback)
{
    auto *watcher = new QFutureWatcher<EnergyEnvelope>(context);
    QObject::connect(watcher, &QFutureWatcher<EnergyEnvelope>::finished, context,
                     [watcher, callback = std::move(callback)] {
                         auto result = watcher->result();
                         watcher->deleteLater();
                         callback(std::move(result));
                     });
    watcher->setFuture(QtConcurrent::run([path, cancel] {
        return analyzeEnergy(path, cancel);
    }));
}
