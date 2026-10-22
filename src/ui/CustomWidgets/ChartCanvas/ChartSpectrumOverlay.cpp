#include "ChartSpectrumOverlay.h"
#include "ChartCanvas.h"
#include "analysis/SpectrumPaging.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace
{
bool covers(const SpectrumService::Page &page, double start, double end)
{
    return page.valid() && page.spectrum->startSeconds * 1000 <= start + .1
           && (page.spectrum->startSeconds + page.spectrum->durationSeconds) * 1000 + .1 >= end;
}
}

ChartSpectrumOverlay::ChartSpectrumOverlay(ChartCanvas *canvas, Runner runner)
    : QObject(canvas), m_canvas(canvas), m_runner(std::move(runner))
{
    if (!m_runner)
        m_runner = [this](QObject *context, const QString &path, double start, double duration,
                          std::shared_ptr<std::atomic<bool>> cancel, SpectrumService::PageCallback done) {
            const double a = m_transform.audioAtBeat(m_canvas->scrollBeat());
            const double b = m_transform.audioAtBeat(m_canvas->scrollBeat() + m_canvas->visibleBeatRange());
            SpectrumService::prepareFileRangeProgressiveAsync(context, path, start, duration,
                std::move(cancel), std::move(done), {}, qMin(a, b), qMax(a, b));
        };
    setObjectName("chart.spectrumOverlay");
    m_requestTimer.setSingleShot(true);
    m_sourceTimer.setInterval(1000);
    connect(&m_requestTimer, &QTimer::timeout, this, &ChartSpectrumOverlay::request);
    connect(&m_sourceTimer, &QTimer::timeout, this, &ChartSpectrumOverlay::refreshSource);
    connect(canvas, &ChartCanvas::scrollPositionChanged, this, &ChartSpectrumOverlay::schedule);
    connect(canvas, &ChartCanvas::timeScaleChanged, this, &ChartSpectrumOverlay::schedule);
    connect(canvas, &ChartCanvas::verticalFlipChanged, this, &ChartSpectrumOverlay::schedule);
    canvas->installEventFilter(this);
    if (canvas->isVisible())
        m_sourceTimer.start();
}

ChartSpectrumOverlay::~ChartSpectrumOverlay()
{
    suspend(); // disconnect completion on context destruction; never wait on the GUI thread
}

void ChartSpectrumOverlay::suspend()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_requestTimer.stop();
    m_sourceTimer.stop();
}

bool ChartSpectrumOverlay::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_canvas)
    {
        if (event->type() == QEvent::Hide)
            suspend();
        else if (event->type() == QEvent::Show)
        {
            m_sourceTimer.start();
            refreshSource();
            schedule();
        }
        else if (event->type() == QEvent::Resize)
            schedule();
    }
    return QObject::eventFilter(object, event);
}

void ChartSpectrumOverlay::setChartController(ChartController *chart)
{
    if (m_chart)
        disconnect(m_chart, nullptr, this, nullptr);
    m_chart = chart;
    if (chart)
    {
        connect(chart, &ChartController::chartChangeCommitted, this, [this](const ChartChange &change) {
            if (change.affects(ChartChangeType::Timing))
                refreshTiming();
            if (change.affects(ChartChangeType::Resources) || change.affects(ChartChangeType::Metadata))
                refreshSource();
        });
        connect(chart, &ChartController::chartLoaded, this, [this] {
            refreshTiming();
            refreshSource();
        });
    }
    refreshTiming();
    refreshSource();
}

void ChartSpectrumOverlay::setPlaybackController(PlaybackController *playback)
{
    if (m_playback)
        disconnect(m_playback, nullptr, this, nullptr);
    m_playback = playback;
    if (playback)
    {
        connect(playback, &PlaybackController::stateChanged, this, &ChartSpectrumOverlay::schedule);
        // Playback follows the clock without emitting the general scroll signal
        // on every frame. Coalesce requests after that frame updates the viewport.
        connect(playback, &PlaybackController::playbackFrameTick, this, &ChartSpectrumOverlay::schedule);
    }
}

void ChartSpectrumOverlay::refreshTiming()
{
    m_timings = m_chart ? MathUtils::buildBpmTimeCache(m_chart->chart()->bpmList(), m_chart->chart()->meta().offset)
                        : QVector<MathUtils::BpmCacheEntry>{};
    m_transform.setChart(m_timings);
    schedule(); // BPM/offset/undo changes projection, never the audio cache
}

void ChartSpectrumOverlay::refreshSource()
{
    QString path;
    if (m_chart)
    {
        path = m_chart->chart()->audioSourceFullPath();
        if (path.isEmpty())
            path = m_chart->chart()->meta().audioFile;
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            path = QFileInfo(m_chart->chartFilePath()).dir().absoluteFilePath(path);
    }
    const QFileInfo file(path);
    const auto identity = path.isEmpty() ? QString() : file.absoluteFilePath() + '|' + QString::number(file.size())
                                                        + '|' + QString::number(file.lastModified().toMSecsSinceEpoch());
    if (identity == m_identity)
        return;
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_identity = identity;
    m_path = file.exists() ? file.absoluteFilePath() : QString();
    m_pages.clear();
    m_current = {};
    m_failedTiles.clear();
    m_eof = -1;
    m_canvas->update();
    schedule();
}

void ChartSpectrumOverlay::schedule()
{
    if (m_canvas->isVisible() && !m_requestTimer.isActive())
        m_requestTimer.start(16);
}

void ChartSpectrumOverlay::request()
{
    if (!m_canvas->isVisible() || m_path.isEmpty() || m_timings.isEmpty())
        return;
    double eof = m_eof;
    if (m_playback && m_playback->audioPlayer()->isLoaded() && m_playback->audioPlayer()->duration() > 0)
        eof = eof < 0 ? m_playback->audioPlayer()->duration() : qMin(eof, double(m_playback->audioPlayer()->duration()));
    const auto range = analysis::spectrumPageRange(m_transform.audioAtBeat(m_canvas->scrollBeat()),
        m_transform.audioAtBeat(m_canvas->scrollBeat() + m_canvas->visibleBeatRange()), eof);
    if (!range.valid)
        return;
    constexpr double tileMs = 2000;
    const double center = (range.requiredStartMs + range.requiredEndMs) / 2;
    m_current = {};
    for (const auto &page : m_pages)
        if (covers(page, center, center))
            m_current = page;
    if (!m_current.valid())
        for (const auto &page : m_pages)
            if (page.spectrum->startSeconds * 1000 < range.requiredEndMs
                && (page.spectrum->startSeconds + page.spectrum->durationSeconds) * 1000 > range.requiredStartMs)
            { m_current = page; break; }
    const int first = int(std::floor(range.requiredStartMs / tileMs));
    const int last = int(std::ceil(range.requiredEndMs / tileMs - 1e-9));
    // Visible blocks precede a single neighbour. Each completion yields to the
    // event loop and the shared worker, so AE can make progress at the same time.
    int missing = -1;
    for (int index = first; index <= last; ++index)
    {
        const double start = index * tileMs;
        if ((eof >= 0 && start >= eof) || m_failedTiles.contains(index))
            continue;
        const double end = eof < 0 ? start + tileMs : qMin(eof, start + tileMs);
        bool found = false;
        for (const auto &page : m_pages)
            if (covers(page, start, end))
            { found = true; break; }
        if (!found)
        { missing = index; break; }
    }
    if (m_busy)
    {
        if (m_pendingEnd <= range.requiredStartMs || m_pendingStart >= (last + 1) * tileMs)
            m_cancel->store(true);
        return;
    }
    if (missing < 0)
        return;
    m_busy = true;
    m_pendingStart = missing * tileMs;
    m_pendingEnd = m_pendingStart + tileMs;
    const auto generation = m_generation;
    const auto cancel = m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_runner(this, m_path, m_pendingStart, tileMs, cancel,
             [this, generation, cancel, missing](SpectrumService::Page page) {
        m_busy = false;
        if (generation != m_generation || cancel->load())
        {
            schedule();
            return;
        }
        if (!page.valid())
        {
            if (page.spectrum && page.spectrum->error == "No audio in spectrum range")
                m_eof = missing * 2000.;
            else
                m_failedTiles.insert(missing);
            schedule();
            return;
        }
        if (page.spectrum->durationSeconds < 1.99999)
            m_eof = (page.spectrum->startSeconds + page.spectrum->durationSeconds) * 1000;
        m_pages.append(std::move(page));
        qint64 bytes = 0;
        for (const auto &cached : m_pages)
            bytes += cached.memoryBytes();
        const double a = m_transform.audioAtBeat(m_canvas->scrollBeat());
        const double b = m_transform.audioAtBeat(m_canvas->scrollBeat() + m_canvas->visibleBeatRange());
        while (m_pages.size() > 1 && (m_pages.size() > 64 || bytes > 48 * 1024 * 1024))
        {
            auto victim = std::find_if(m_pages.begin(), m_pages.end(), [a, b](const auto &cached) {
                const auto &s = *cached.spectrum;
                return s.startSeconds * 1000 >= qMax(a, b) || (s.startSeconds + s.durationSeconds) * 1000 <= qMin(a, b);
            });
            if (victim == m_pages.end())
                break; // the bounded visible range is pinned
            bytes -= victim->memoryBytes();
            m_pages.erase(victim);
        }
        if (!m_playback || m_playback->state() != PlaybackController::Playing)
            m_canvas->update();
        schedule();
        // Update the public ready marker immediately, without starting recursive
        // work when a test runner completes synchronously.
        if (!m_current.valid())
            m_current = m_pages.back();
    });
}

void ChartSpectrumOverlay::paint(QPainter &painter, const QRectF &rect)
{
    if (m_pages.isEmpty() || m_timings.isEmpty() || rect.isEmpty())
        return;
    const double firstBeat = m_canvas->scrollBeat(), lastBeat = firstBeat + m_canvas->visibleBeatRange();
    const double visibleStart = m_transform.audioAtBeat(firstBeat), visibleEnd = m_transform.audioAtBeat(lastBeat);
    const auto initialTiming = std::upper_bound(m_timings.cbegin(), m_timings.cend(), firstBeat,
                                   [](double beat, const auto &entry) { return beat < entry.beatPos; });
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const auto &page : m_pages)
    {
        const auto &s = *page.spectrum;
        const double pageStart = s.startSeconds * 1000, pageEnd = pageStart + s.durationSeconds * 1000;
        if (pageEnd <= visibleStart || pageStart >= visibleEnd)
            continue;
        auto timing = initialTiming;
        if (timing != m_timings.cbegin())
            --timing;
        for (; timing != m_timings.cend() && (timing == m_timings.cbegin() || timing->beatPos < lastBeat); ++timing)
        {
            if (timing->bpm <= 0)
                continue;
            const auto next = std::next(timing);
            const double a = timing == m_timings.cbegin() ? firstBeat : qMax(firstBeat, timing->beatPos);
            const double b = next == m_timings.cend() ? lastBeat : qMin(lastBeat, next->beatPos);
            const double start = qMax(pageStart, timing->accumulatedMs + (a - timing->beatPos) * 60000 / timing->bpm);
            const double end = qMin(pageEnd, timing->accumulatedMs + (b - timing->beatPos) * 60000 / timing->bpm);
            if (end <= start)
                continue;
            const auto yAt = [&](double ms) {
                return m_canvas->chartBeatToY(timing->beatPos + (ms - timing->accumulatedMs) * timing->bpm / 60000);
            };
            const double y0 = yAt(start), y1 = yAt(end), height = std::abs(y1 - y0);
            if (height <= 0)
                continue;
            int level = 0;
            double stride = 1;
            const double rowsPerPixel = (end - start) / (s.hopSeconds * 1000 * height * m_canvas->devicePixelRatioF());
            while (level + 1 < page.raster.overlay.size() && stride * 2 <= rowsPerPixel)
            {
                ++level;
                stride *= 2;
            }
            const auto &image = page.raster.overlay[level];
            const double step = s.hopSeconds * 1000 * stride;
            const QRectF source(0, (start - pageStart) / step, image.width(), (end - start) / step);
            painter.save();
            painter.translate(0, y0);
            if (y1 < y0)
                painter.scale(1, -1);
            painter.drawImage(QRectF(rect.left(), 0, rect.width(), height), image, source);
            painter.restore();
        }
    }
    painter.restore();
}
