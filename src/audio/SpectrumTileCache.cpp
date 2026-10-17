#include "SpectrumTileCache.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <list>

namespace spectrum_cache
{
namespace
{
using Page = SpectrumService::Page;
constexpr qint64 memoryBudget = 48 * 1024 * 1024;
constexpr qint64 diskBudget = 256 * 1024 * 1024;
constexpr quint32 magic = 0x43435333, version = 3;
struct Entry { QString identity; double start, duration; Page page; qint64 bytes; };
QMutex cacheMutex, buildMutex;
std::list<Entry> cache;
qint64 cacheBytes = 0, nextTrimMs = 0;
std::atomic<quint64> generated{0}, memoryHits{0}, diskHits{0}, shared{0};

Page failure(const char *message)
{
    auto spectrum = std::make_shared<analysis::StereoSpectrum>();
    spectrum->error = message;
    return {std::move(spectrum), {}};
}
QString directory()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return base.isEmpty() ? QString() : QDir(base).absoluteFilePath("spectrum-v3");
}
QString filename(const QString &identity, int index)
{
    const auto root = directory();
    const auto key = (identity + "|stereo-hann2048-log192-step5ms-v3|" + QString::number(index)).toUtf8();
    return root.isEmpty() ? QString() : QDir(root).filePath(
        QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex()) + ".csp");
}
Page readDisk(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return {};
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 tag, revision, byteOrder, bins, rows;
    analysis::StereoSpectrum data;
    stream >> tag >> revision >> byteOrder >> data.sampleRate >> data.sourceChannels >> data.fftSize
           >> data.startSeconds >> data.durationSeconds >> data.hopSeconds >> bins >> rows;
    if (stream.status() != QDataStream::Ok || tag != magic || revision != version || byteOrder != Q_BYTE_ORDER
        || bins != 192 || !rows || rows > 400 || data.sampleRate < 1000 || data.sampleRate > 384000
        || data.sourceChannels < 1 || data.sourceChannels > 32 || data.fftSize != 2048
        || !std::isfinite(data.startSeconds) || data.startSeconds < 0
        || !std::isfinite(data.durationSeconds) || data.durationSeconds <= 0 || data.durationSeconds > 2.001
        || std::abs(data.hopSeconds - .005) > 1e-9)
        return {};
    static_assert(sizeof(analysis::SpectrumFrame) == 4 * sizeof(float));
    data.frequencies.resize(bins);
    data.frames.resize(rows);
    data.leftDb.resize(size_t(rows) * bins);
    data.rightDb.resize(data.leftDb.size());
    const auto read = [&](auto &values) {
        const auto bytes = qint64(values.size() * sizeof(values[0]));
        return stream.readRawData(reinterpret_cast<char *>(values.data()), bytes) == bytes;
    };
    if (!read(data.frequencies) || !read(data.frames) || !read(data.leftDb) || !read(data.rightDb)
        || stream.status() != QDataStream::Ok || !file.atEnd())
        return {};
    if (rows != quint32(std::ceil(data.durationSeconds / .005 - 1e-9)))
        return {};
    float previous = 0;
    for (float hz : data.frequencies)
    {
        if (!std::isfinite(hz) || hz <= previous || hz > data.sampleRate / 2. + 1)
            return {};
        previous = hz;
    }
    for (const auto &frame : data.frames)
        for (float value : {frame.leftPeak, frame.rightPeak, frame.leftRms, frame.rightRms})
            if (!std::isfinite(value) || value < 0)
                return {};
    auto raster = analysis::prepareSpectrumRaster(data);
    if (!raster.valid())
        return {};
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return {std::make_shared<const analysis::StereoSpectrum>(std::move(data)), std::move(raster)};
}
void writeDisk(const QString &path, const Page &page)
{
    if (path.isEmpty() || !page.valid() || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    const auto &s = *page.spectrum;
    stream << magic << version << quint32(Q_BYTE_ORDER) << s.sampleRate << s.sourceChannels << s.fftSize
           << s.startSeconds << s.durationSeconds << s.hopSeconds << quint32(s.frequencies.size())
           << quint32(s.frames.size());
    const auto write = [&](const auto &values) {
        const auto bytes = qint64(values.size() * sizeof(values[0]));
        return stream.writeRawData(reinterpret_cast<const char *>(values.data()), bytes) == bytes;
    };
    if (!write(s.frequencies) || !write(s.frames) || !write(s.leftDb) || !write(s.rightDb)
        || stream.status() != QDataStream::Ok || !file.commit())
        return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker lock(&cacheMutex);
        if (now < nextTrimMs)
            return;
        nextTrimMs = now + 30000;
    }
    // Only disposable files inside our dedicated cache directory are trimmed.
    const auto files = QDir(QFileInfo(path).absolutePath()).entryInfoList({"*.csp"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const auto &entry : files)
        total += entry.size();
    int remaining = files.size();
    for (const auto &entry : files)
    {
        if (total <= diskBudget && remaining <= 1024)
            break;
        if (QFile::remove(entry.absoluteFilePath()))
        {
            total -= entry.size();
            --remaining;
        }
    }
}
}

QString sourceIdentity(const QString &path)
{
    const QFileInfo file(path);
    return file.exists() ? file.absoluteFilePath() + '|' + QString::number(file.size()) + '|'
                               + QString::number(file.lastModified().toMSecsSinceEpoch()) : QString();
}
Page find(const QString &identity, double start, double duration)
{
    QMutexLocker lock(&cacheMutex);
    for (auto it = cache.begin(); it != cache.end(); ++it)
        if (!identity.isEmpty() && it->identity == identity && it->start == start && it->duration == duration)
        {
            auto page = it->page;
            cache.splice(cache.begin(), cache, it);
            ++memoryHits;
            return page;
        }
    return {};
}
void remember(const QString &identity, double start, double duration, Page page)
{
    if (identity.isEmpty() || !page.valid())
        return;
    const auto bytes = page.memoryBytes();
    if (bytes > memoryBudget)
        return;
    QMutexLocker lock(&cacheMutex);
    for (auto it = cache.begin(); it != cache.end(); ++it)
        if (it->identity == identity && it->start == start && it->duration == duration)
        {
            cacheBytes -= it->bytes;
            cache.erase(it);
            break;
        }
    while (!cache.empty() && (cacheBytes + bytes > memoryBudget || cache.size() >= 128))
    {
        cacheBytes -= cache.back().bytes;
        cache.pop_back();
    }
    cache.push_front({identity, start, duration, std::move(page), bytes});
    cacheBytes += bytes;
}
Page tile(const QString &path, const QString &identity, int index)
{
    const double start = index * tileMs;
    if (auto page = find(identity, start, tileMs); page.valid())
        return page;
    // Also serialize synchronous callers: different viewport ranges may overlap
    // the same block, and must never run its decoder/FFT twice concurrently.
    QMutexLocker buildLock(&buildMutex);
    if (auto page = find(identity, start, tileMs); page.valid())
        return page;
    if (index < 0 || identity.isEmpty() || identity != sourceIdentity(path))
        return failure("Audio source changed");
    auto page = readDisk(filename(identity, index));
    if (page.valid() && std::abs(page.spectrum->startSeconds * 1000 - start) < .001)
        ++diskHits;
    else
    {
        auto spectrum = SpectrumService::analyzeFileRange(path, start, tileMs, {}, .005);
        if (!spectrum.valid())
            return {std::make_shared<const analysis::StereoSpectrum>(std::move(spectrum)), {}};
        auto raster = analysis::prepareSpectrumRaster(spectrum);
        page = {std::make_shared<const analysis::StereoSpectrum>(std::move(spectrum)), std::move(raster)};
        ++generated;
        if (identity == sourceIdentity(path))
            writeDisk(filename(identity, index), page);
    }
    if (identity != sourceIdentity(path))
        return failure("Audio changed during spectrum decode");
    remember(identity, start, tileMs, page);
    return page;
}
Page assemble(const QString &identity, double start, double duration, QVector<Page> tiles,
              const std::shared_ptr<std::atomic<bool>> &cancel)
{
    if (cancel && cancel->load())
        return failure("Cancelled");
    if (auto page = find(identity, start, duration); page.valid())
        return page;
    if (tiles.isEmpty())
        return failure("No audio in spectrum range");
    for (const auto &page : tiles)
        if (!page.valid())
            return failure("Invalid spectrum block");
    std::sort(tiles.begin(), tiles.end(), [](const Page &a, const Page &b) {
        return a.spectrum->startSeconds < b.spectrum->startSeconds;
    });
    const double first = start / 1000;
    double covered = tiles.front().spectrum->startSeconds;
    if (covered > first + 1e-8)
        return failure("Non-contiguous spectrum blocks");
    double end = first;
    for (const auto &page : tiles)
    {
        const auto &s = *page.spectrum;
        const auto &base = *tiles.front().spectrum;
        if (s.startSeconds > covered + 1e-8 || s.frequencies != base.frequencies
            || s.sampleRate != base.sampleRate || s.sourceChannels != base.sourceChannels
            || std::abs(s.hopSeconds - .005) > 1e-9)
            return failure("Non-contiguous spectrum blocks");
        covered = s.startSeconds + s.durationSeconds;
        end = std::max(end, covered);
    }
    end = std::min(end, (start + duration) / 1000);
    if (end <= first)
        return failure("No audio in spectrum range");
    if (tiles.size() == 1 && std::abs(tiles.front().spectrum->startSeconds - first) < 1e-8
        && std::abs(tiles.front().spectrum->durationSeconds - (end - first)) < 1e-8)
        return tiles.front();
    analysis::StereoSpectrum out;
    out.startSeconds = first;
    out.durationSeconds = end - first;
    out.sampleRate = tiles.front().spectrum->sampleRate;
    out.sourceChannels = tiles.front().spectrum->sourceChannels;
    out.frequencies = tiles.front().spectrum->frequencies;
    const size_t stride = std::max<size_t>(1, size_t(std::ceil(out.durationSeconds / (.005 * 12000) - 1e-9)));
    out.hopSeconds = .005 * stride;
    const size_t rows = std::min<size_t>(12000, size_t(std::ceil(out.durationSeconds / out.hopSeconds - 1e-9)));
    const auto bins = out.frequencies.size();
    out.frames.resize(rows);
    out.leftDb.resize(rows * bins, -90);
    out.rightDb.resize(rows * bins, -90);
    int block = 0;
    for (size_t row = 0; row < rows; ++row)
    {
        if (cancel && cancel->load())
            return failure("Cancelled");
        double squaresL = 0, squaresR = 0;
        size_t count = 0;
        for (size_t sub = 0; sub < stride; ++sub)
        {
            const double t = first + row * out.hopSeconds + sub * .005;
            if (t >= end - 1e-9)
                break;
            while (block + 1 < tiles.size() && t >= tiles[block + 1].spectrum->startSeconds - 1e-9)
                ++block;
            const auto &s = *tiles[block].spectrum;
            const auto frame = size_t(std::max(0., std::floor((t - s.startSeconds) / s.hopSeconds + 1e-7)));
            if (t < s.startSeconds - 1e-8 || frame >= s.frames.size())
                return failure("Non-contiguous spectrum blocks");
            const auto &level = s.frames[frame];
            auto &target = out.frames[row];
            target.leftPeak = std::max(target.leftPeak, level.leftPeak);
            target.rightPeak = std::max(target.rightPeak, level.rightPeak);
            squaresL += double(level.leftRms) * level.leftRms;
            squaresR += double(level.rightRms) * level.rightRms;
            ++count;
            for (size_t b = 0; b < bins; ++b)
            {
                out.leftDb[row * bins + b] = std::max(out.leftDb[row * bins + b], s.leftDb[frame * bins + b]);
                out.rightDb[row * bins + b] = std::max(out.rightDb[row * bins + b], s.rightDb[frame * bins + b]);
            }
        }
        out.frames[row].leftRms = float(std::sqrt(squaresL / std::max<size_t>(1, count)));
        out.frames[row].rightRms = float(std::sqrt(squaresR / std::max<size_t>(1, count)));
    }
    auto raster = analysis::prepareSpectrumRaster(out, cancel.get());
    Page page{std::make_shared<const analysis::StereoSpectrum>(std::move(out)), std::move(raster)};
    if (!(cancel && cancel->load()))
        remember(identity, start, duration, page);
    return page;
}
SpectrumService::CacheStats stats() { return {generated.load(), memoryHits.load(), diskHits.load(), shared.load()}; }
void sharedRequest() { ++shared; }
}
