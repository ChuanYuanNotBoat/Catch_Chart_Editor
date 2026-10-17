#include "SpectrumService.h"
#include "SpectrumTileCache.h"
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QtConcurrentTask>
#include <cstring>
#include <limits>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
namespace
{
constexpr qint64 pcmLimit = 32 * 1024 * 1024; // float elements, including window context
// Seek using the codec already shipped with CCE. QFile callbacks preserve
// Unicode paths on Windows and never touch the playback decoder or its clock.
bool vorbisRange(const QString &path, double startMs, double durationMs,
                 const std::shared_ptr<std::atomic<bool>> &cancel, SpectrumService::Result &out, double frameStep)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.peek(4) != "OggS")
        return false;
    const ov_callbacks callbacks{
        [](void *data, size_t size, size_t count, void *source) -> size_t {
            if (!size || count > size_t(std::numeric_limits<qint64>::max()) / size)
                return 0;
            const auto read = static_cast<QFile *>(source)->read(static_cast<char *>(data), qint64(size * count));
            return read > 0 ? size_t(read) / size : 0;
        },
        [](void *source, ogg_int64_t offset, int whence) -> int {
            auto *f = static_cast<QFile *>(source);
            const qint64 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos() : f->size();
            if (offset < -base || offset > std::numeric_limits<qint64>::max() - base)
                return -1;
            return f->seek(base + offset) ? 0 : -1;
        },
        nullptr,
        [](void *source) -> long {
            const auto pos = static_cast<QFile *>(source)->pos();
            return pos > std::numeric_limits<long>::max() ? -1 : long(pos);
        }};
    OggVorbis_File decoder{};
    if (ov_open_callbacks(&file, &decoder, nullptr, 0, callbacks) != 0)
        return false; // e.g. Ogg Opus: use Qt's supported-format fallback
    struct Close
    {
        OggVorbis_File *decoder;
        ~Close() { ov_clear(decoder); }
    } close{&decoder};
    const auto *info = ov_info(&decoder, 0);
    if (!info || info->rate < 1000 || info->rate > 384000 || info->channels < 1 || info->channels > 32)
    {
        out.error = "Unsupported Vorbis PCM format";
        return true;
    }
    const int rate = int(info->rate), channels = info->channels;
    const int outputChannels = std::min(2, channels);
    const auto total = ov_pcm_total(&decoder, -1);
    const qint64 first = qint64(std::floor(startMs * rate / 1000));
    const qint64 end = std::min<qint64>(total, qint64(std::ceil((startMs + durationMs) * rate / 1000)));
    const qint64 from = std::max<qint64>(0, first - 1024), to = std::min<qint64>(total, end + 1024);
    if (end <= first || (to - from) * outputChannels > pcmLimit)
    {
        out.error = end <= first ? "No audio in spectrum range" : "Spectrum PCM memory limit reached";
        return true;
    }
    if (ov_pcm_seek(&decoder, from) != 0)
    {
        out.error = "Cannot seek Vorbis spectrum range";
        return true;
    }
    std::vector<float> pcm;
    pcm.reserve(size_t(to - from) * outputChannels);
    qint64 position = from;
    while (position < to)
    {
        if (cancel && cancel->load())
        {
            out.error = "Cancelled";
            return true;
        }
        float **samples = nullptr;
        int link = 0;
        const long count = ov_read_float(&decoder, &samples, int(std::min<qint64>(4096, to - position)), &link);
        const auto *current = ov_info(&decoder, link);
        if (count <= 0 || !current || current->rate != rate || current->channels != channels)
        {
            out.error = "Invalid/changing Vorbis data";
            return true;
        }
        for (long i = 0; i < count; ++i)
        {
            pcm.push_back(samples[0][i]);
            if (outputChannels > 1)
                pcm.push_back(samples[1][i]);
        }
        position += count;
    }
    out = analysis::analyzeStereo(pcm, rate, outputChannels, double(first) / rate, cancel.get(), size_t(first - from), size_t(end - first), frameStep);
    out.sourceChannels = channels;
    return true;
}

QThreadPool &spectrumWorkerPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(1);
        pool.setThreadPriority(QThread::LowestPriority);
        pool.setExpiryTimeout(-1);
        return true;
    }();
    Q_UNUSED(configured);
    return pool;
}

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
                                                          const std::shared_ptr<std::atomic<bool>> &cancel, double frameStep)
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
    if (vorbisRange(path, startMs, durationMs, cancel, out, frameStep))
        return out;
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
        const qint64 start = std::max<qint64>(0, qint64(std::floor(startMs * rate / 1000)) - 1024);
        const qint64 end = qint64(std::ceil((startMs + durationMs) * rate / 1000)) + 1024;
        const qint64 from = std::max(processed, start), to = std::min(processed + b.frameCount(), end);
        if (to > from)
        {
            if (first < 0)
                first = from;
            if (pcm.size() + size_t(to - from) * std::min(2, channels) > pcmLimit)
            {
                out.error = "Spectrum PCM memory limit reached";
                finish();
                return;
            }
            for (qint64 i = from - processed; i < to - processed; ++i)
            {
                pcm.push_back(sample(b, int(i) * channels));
                if (channels > 1)
                    pcm.push_back(sample(b, int(i) * channels + 1));
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
    const qint64 requested = qint64(std::floor(startMs * rate / 1000));
    const qint64 end = qint64(std::ceil((startMs + durationMs) * rate / 1000));
    const qint64 count = std::min<qint64>(qint64(pcm.size() / std::min(2, channels)), end - first) - (requested - first);
    if (count <= 0)
    {
        out.error = "No audio in spectrum range";
        return out;
    }
    out = analysis::analyzeStereo(pcm, rate, std::min(2, channels), double(requested) / rate, cancel.get(),
                                 size_t(requested - first), size_t(count), frameStep);
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
    watcher->setFuture(QtConcurrent::run(&spectrumWorkerPool(),
                                         [path, start, duration, cancel] {
                                             return analyzeFileRange(path, start, duration, cancel);
                                         }));
}
namespace
{
using Page = SpectrumService::Page;
bool validRange(double start, double duration)
{
    return std::isfinite(start) && std::isfinite(duration) && start >= 0 && start <= 86400000
           && duration > 0 && duration <= 120000;
}
Page failedPage(const char *error)
{
    auto data = std::make_shared<analysis::StereoSpectrum>();
    data->error = error;
    return {std::move(data), {}};
}
struct TileWork
{
    QFuture<Page> future;
    QVector<std::weak_ptr<std::atomic<bool>>> subscribers;
};
QMutex workMutex;
QHash<QString, std::shared_ptr<TileWork>> activeTiles;
QFuture<Page> requestTile(const QString &path, const QString &identity, int index,
                          const std::shared_ptr<std::atomic<bool>> &cancel, int priority)
{
    const auto key = identity + '|' + QString::number(index);
    QMutexLocker lock(&workMutex);
    for (auto it = activeTiles.begin(); it != activeTiles.end();)
        if (it.value()->future.isFinished())
            it = activeTiles.erase(it);
        else
            ++it;
    if (auto it = activeTiles.find(key); it != activeTiles.end())
    {
        it.value()->subscribers.append(cancel);
        spectrum_cache::sharedRequest();
        return it.value()->future;
    }
    auto work = std::make_shared<TileWork>();
    work->subscribers.append(cancel);
    activeTiles.insert(key, work);
    work->future = QtConcurrent::task([path, identity, index, work] {
        bool wanted = false;
        {
            QMutexLocker lock(&workMutex);
            for (const auto &weak : work->subscribers)
                if (auto flag = weak.lock(); flag && !flag->load())
                    wanted = true;
        }
        // Abandoned queued work is cheap to skip. Once a small shared block is
        // running it completes into cache; cancelling one view cannot abort AE.
        return wanted ? spectrum_cache::tile(path, identity, index) : failedPage("Cancelled");
    }).onThreadPool(spectrumWorkerPool()).withPriority(priority).spawn();
    return work->future;
}

class SpectrumRangeRequest final : public QObject
{
public:
    SpectrumRangeRequest(QObject *context, QString path, double start, double duration,
        std::shared_ptr<std::atomic<bool>> cancel, SpectrumService::PageCallback done,
        SpectrumService::PageCallback progress, double focusStart, double focusEnd)
        : QObject(context), m_path(std::move(path)), m_start(start), m_duration(duration),
          m_cancel(cancel ? std::move(cancel) : std::make_shared<std::atomic<bool>>(false)),
          m_done(std::move(done)), m_progress(std::move(progress)), m_watcher(this)
    {
        if (validRange(start, duration))
            for (int i = int(std::floor(start / spectrum_cache::tileMs));
                 i < int(std::ceil((start + duration) / spectrum_cache::tileMs - 1e-9)); ++i)
                m_order.append(i);
        m_focusStart = focusStart >= 0 ? focusStart : start;
        m_focusEnd = focusEnd > m_focusStart ? focusEnd : start + duration;
        std::stable_sort(m_order.begin(), m_order.end(), [this](int a, int b) {
            return visible(a) > visible(b);
        });
        connect(&m_watcher, &QFutureWatcher<Page>::finished, this, [this] {
            auto page = m_watcher.result();
            if (m_cancel->load())
                return finish(failedPage("Cancelled"));
            if (m_assembling)
                return finish(std::move(page));
            if (!page.valid())
            {
                if (!page.spectrum || page.spectrum->error != "No audio in spectrum range")
                    return finish(std::move(page));
                m_eof = std::min(m_eof, m_currentIndex * spectrum_cache::tileMs);
            }
            else
            {
                m_tiles.append(page);
                if (page.spectrum->durationSeconds < 1.99999)
                    m_eof = std::min(m_eof, (page.spectrum->startSeconds + page.spectrum->durationSeconds) * 1000);
            }
            QTimer::singleShot(0, this, [this] { advance(); });
            if (page.valid() && m_progress)
                m_progress(std::move(page));
        });
    }
    void start()
    {
        if (!validRange(m_start, m_duration))
            return finish(failedPage("Invalid spectrum range (maximum 120 s)"));
        m_identity = spectrum_cache::sourceIdentity(m_path);
        advance();
    }
private:
    bool visible(int i) const
    {
        return i * spectrum_cache::tileMs < m_focusEnd && (i + 1) * spectrum_cache::tileMs > m_focusStart;
    }
    void advance()
    {
        if (m_cancel->load())
            return finish(failedPage("Cancelled"));
        while (m_next < m_order.size() && m_order[m_next] * spectrum_cache::tileMs >= m_eof)
            ++m_next;
        if (m_next < m_order.size())
        {
            m_currentIndex = m_order[m_next++];
            m_watcher.setFuture(requestTile(m_path, m_identity, m_currentIndex, m_cancel, visible(m_currentIndex) ? 1 : 0));
            return;
        }
        m_assembling = true;
        m_watcher.setFuture(QtConcurrent::run(&spectrumWorkerPool(),
            [path = m_path, identity = m_identity, start = m_start, duration = m_duration,
             tiles = std::move(m_tiles), cancel = m_cancel] () mutable {
            if (identity != spectrum_cache::sourceIdentity(path))
                return failedPage("Audio source changed");
            auto page = spectrum_cache::assemble(identity, start, duration, std::move(tiles), cancel);
            return identity == spectrum_cache::sourceIdentity(path) ? page : failedPage("Audio source changed");
        }));
    }
    void finish(Page page)
    {
        auto done = std::move(m_done);
        deleteLater();
        if (done)
            done(std::move(page));
    }
    QString m_path, m_identity;
    double m_start, m_duration, m_focusStart = 0, m_focusEnd = 0, m_eof = 86400001;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    SpectrumService::PageCallback m_done, m_progress;
    QFutureWatcher<Page> m_watcher;
    QVector<int> m_order;
    QVector<Page> m_tiles;
    int m_next = 0, m_currentIndex = 0;
    bool m_assembling = false;
};
}
void SpectrumService::prepareFileRangeAsync(QObject *context, const QString &path, double start, double duration,
                                            std::shared_ptr<std::atomic<bool>> cancel, PageCallback callback)
{
    prepareFileRangeProgressiveAsync(context, path, start, duration, std::move(cancel), std::move(callback), {});
}
void SpectrumService::prepareFileRangeProgressiveAsync(QObject *context, const QString &path, double start,
    double duration, std::shared_ptr<std::atomic<bool>> cancel, PageCallback done, PageCallback progress,
    double focusStart, double focusEnd)
{
    auto *request = new SpectrumRangeRequest(context, path, start, duration, std::move(cancel),
        std::move(done), std::move(progress), focusStart, focusEnd);
    QTimer::singleShot(0, request, [request] { request->start(); });
}
SpectrumService::Page SpectrumService::prepareFileRange(const QString &path, double start, double duration,
                                                        const std::shared_ptr<std::atomic<bool>> &cancel)
{
    if (!validRange(start, duration))
        return failedPage("Invalid spectrum range (maximum 120 s)");
    if (cancel && cancel->load())
        return failedPage("Cancelled");
    const auto identity = spectrum_cache::sourceIdentity(path);
    if (auto page = spectrum_cache::find(identity, start, duration); page.valid())
        return page;
    QVector<Page> tiles;
    for (int i = int(std::floor(start / spectrum_cache::tileMs));
         i < int(std::ceil((start + duration) / spectrum_cache::tileMs - 1e-9)); ++i)
    {
        if (cancel && cancel->load())
            return failedPage("Cancelled");
        auto page = spectrum_cache::tile(path, identity, i);
        if (!page.valid())
        {
            if (!tiles.isEmpty() && page.spectrum && page.spectrum->error == "No audio in spectrum range")
                break;
            return page;
        }
        tiles.append(std::move(page));
        if (tiles.back().spectrum->durationSeconds < 1.99999)
            break;
    }
    return identity == spectrum_cache::sourceIdentity(path)
               ? spectrum_cache::assemble(identity, start, duration, std::move(tiles), cancel)
               : failedPage("Audio source changed");
}
SpectrumService::CacheStats SpectrumService::cacheStats() { return spectrum_cache::stats(); }
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
                                         std::shared_ptr<std::atomic<bool>> cancel, EnergyCallback callback,
                                         QThreadPool *pool)
{
    auto *watcher = new QFutureWatcher<EnergyEnvelope>(context);
    QObject::connect(watcher, &QFutureWatcher<EnergyEnvelope>::finished, context,
                     [watcher, callback = std::move(callback)] {
                         auto result = watcher->result();
                         watcher->deleteLater();
                         callback(std::move(result));
                     });
    auto task = [path, cancel] { return analyzeEnergy(path, cancel); };
    watcher->setFuture(pool ? QtConcurrent::run(pool, task) : QtConcurrent::run(task));
}
