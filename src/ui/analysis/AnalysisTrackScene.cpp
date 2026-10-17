#include "AnalysisTrackScene.h"
#include <QPainter>
#include <QPainterPath>
#include <QHash>
#include <algorithm>
#include <cmath>
namespace
{
struct CurveLane
{
    QString key, label, unit;
    double minimum = 1e100, maximum = -1e100;
    int count = 0;
};
QVector<CurveLane> lanes(const QVector<analysis::AnalysisTrack> &tracks, double low, double high)
{
    QVector<CurveLane> result;
    for (const auto &track : tracks)
    {
        if (!track.visible || track.kind != analysis::TrackKind::Curve || track.points.isEmpty())
            continue;
        const auto key = track.laneGroup.isEmpty() ? track.id : track.laneGroup;
        auto it = std::find_if(result.begin(), result.end(), [&](const auto &lane) {
            return lane.key == key;
        });
        if (it == result.end())
        {
            result.append({key, track.label, track.unit});
            it = std::prev(result.end());
        }
        ++it->count;
        for (const auto &point : track.points)
            if (point.seconds >= low && point.seconds <= high && std::isfinite(point.value))
            {
                it->minimum = qMin(it->minimum, point.value);
                it->maximum = qMax(it->maximum, point.value);
            }
    }
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](const auto &lane) {
                                    return lane.minimum > lane.maximum;
                                }),
                 result.end());
    for (auto &lane : result)
    {
        if (lane.unit == "raw")
        {
            lane.minimum = 0;
            lane.maximum = 1;
        }
        else if (lane.unit == "ms")
        {
            lane.maximum = qMax(.001, qMax(qAbs(lane.minimum), qAbs(lane.maximum)));
            lane.minimum = -lane.maximum;
        }
        else if (lane.maximum - lane.minimum < .001)
        {
            lane.minimum -= 1;
            lane.maximum += 1;
        }
    }
    return result;
}
QColor color(const QString &id)
{
    if (id.startsWith("baseline"))
        return QColor(250, 159, 102);
    if (id == "tempo")
        return QColor(98, 216, 231);
    if (id == "confidence")
        return QColor(231, 201, 99);
    if (id == "phase")
        return QColor(194, 139, 240);
    const QColor colors[]{QColor(245, 148, 114), QColor(118, 224, 176), QColor(126, 168, 245), QColor(224, 164, 219)};
    return colors[qHash(id) % 4];
}
QPointF position(const analysis::TrackPoint &point, const CurveLane &lane, int index, int count, const QRectF &plot,
                 const AnalysisTrackScene::Projection &y)
{
    const double width = plot.width() / qMax(1, count);
    return {plot.left() + 4 + index * width
                + (point.value - lane.minimum) / (lane.maximum - lane.minimum) * qMax(1., width - 8),
            y(point.seconds)};
}
} // namespace
void AnalysisTrackScene::setTracks(QVector<analysis::AnalysisTrack> tracks)
{
    std::stable_sort(tracks.begin(), tracks.end(), [](const auto &a, const auto &b) {
        return a.zOrder < b.zOrder;
    });
    m_tracks = std::move(tracks);
}
void AnalysisTrackScene::setTransformIdentity(const QString &identity)
{
    for (auto &track : m_tracks)
        track.transformId = identity;
}
void AnalysisTrackScene::draw(QPainter &p, const QRectF &plot, double low, double high, const Projection &y) const
{
    const auto geometry = lanes(m_tracks, low, high);
    p.save();
    p.setClipRect(plot, Qt::IntersectClip);
    for (const auto &track : m_tracks)
    {
        if (!track.visible)
            continue;
        if (track.kind == analysis::TrackKind::Regions)
        {
            for (const auto &region : track.regions)
            {
                if (region.endSeconds < low || region.startSeconds > high)
                    continue;
                double a = y(region.startSeconds), b = y(region.endSeconds);
                p.fillRect(QRectF(plot.left(), qMin(a, b), plot.width(), qAbs(b - a)), QColor(232, 159, 89, 35));
                p.fillRect(QRectF(plot.left(), qMin(a, b), 7, qMax(2., qAbs(b - a))), QColor(232, 159, 89, 130));
            }
            continue;
        }
        if (track.kind == analysis::TrackKind::Events)
        {
            for (const auto &event : track.events)
            {
                if (event.seconds < low || event.seconds > high)
                    continue;
                p.setPen(QPen(event.disposition == "Detected" ? QColor(118, 224, 176) : QColor(222, 143, 112), 2));
                p.drawLine(QPointF(plot.right() - 12, y(event.seconds)), QPointF(plot.right() - 2, y(event.seconds)));
            }
            continue;
        }
        if (track.kind == analysis::TrackKind::Candidates)
        {
            p.setPen(QPen(QColor(98, 216, 231), 2));
            for (const auto &candidate : track.candidates)
            {
                if (!candidate.jumpSeconds || *candidate.jumpSeconds < low || *candidate.jumpSeconds > high)
                    continue;
                p.drawEllipse(QPointF(plot.right() - 24, y(*candidate.jumpSeconds)), 3, 3);
            }
            continue;
        }
        const auto key = track.laneGroup.isEmpty() ? track.id : track.laneGroup;
        int index = -1;
        for (int i = 0; i < geometry.size(); ++i)
            if (geometry[i].key == key)
            {
                index = i;
                break;
            }
        if (index < 0)
            continue;
        QPainterPath path;
        bool started = false;
        double previous = 0;
        p.setPen(QPen(color(track.id), 1.5, track.id.startsWith("baseline") ? Qt::DashLine : Qt::SolidLine));
        for (const auto &point : track.points)
        {
            if (point.seconds < low || point.seconds > high || !std::isfinite(point.value))
                continue;
            const auto xy = position(point, geometry[index], index, geometry.size(), plot, y);
            if (!started || point.seconds - previous > 12)
                path.moveTo(xy);
            else
                path.lineTo(xy);
            p.drawEllipse(xy, 1.5, 1.5);
            started = true;
            previous = point.seconds;
        }
        p.drawPath(path);
    }
    for (int i = 0; i < geometry.size(); ++i)
    {
        const auto &lane = geometry[i];
        double width = plot.width() / geometry.size();
        QRectF label(plot.left() + 4 + i * width, plot.top() + 3, width - 8, 50);
        p.fillRect(label, QColor(20, 24, 31, 230));
        p.setPen(QColor(230, 238, 245));
        p.drawText(label, Qt::AlignLeft | Qt::TextWordWrap,
                   lane.label + (lane.count > 1 ? QStringLiteral(" +%1").arg(lane.count - 1) : QString()) + " ("
                       + lane.unit + ")\n"
                       + QStringLiteral("%1 ↔ %2").arg(lane.minimum, 0, 'g', 5).arg(lane.maximum, 0, 'g', 5));
    }
    p.restore();
}
std::optional<AnalysisTrackHit> AnalysisTrackScene::hit(const QPointF &point, const QRectF &plot, double low,
                                                        double high, const Projection &y) const
{
    if (!plot.contains(point) || point.y() < plot.top() + 55)
        return {};
    const auto geometry = lanes(m_tracks, low, high);
    std::optional<AnalysisTrackHit> best;
    double distance = 7;
    auto accept = [&](const analysis::AnalysisTrack &track, const QString &handle, double seconds, QJsonObject details,
                      double d) {
        if (d > distance || seconds < low || seconds > high)
            return;
        distance = d;
        details["provenance"] = track.provenance;
        details["transformId"] = track.transformId;
        best = AnalysisTrackHit{track.id, handle, seconds, details};
    };
    for (const auto &track : m_tracks)
    {
        if (!track.visible || !track.interactive)
            continue;
        if (track.kind == analysis::TrackKind::Regions && point.x() <= plot.left() + 8)
        {
            for (const auto &r : track.regions)
            {
                double a = y(r.startSeconds), b = y(r.endSeconds);
                if (point.y() >= qMin(a, b) && point.y() <= qMax(a, b))
                    accept(track, r.handle, (qMax(low, r.startSeconds) + qMin(high, r.endSeconds)) / 2, r.details, 0);
            }
            continue;
        }
        if (track.kind == analysis::TrackKind::Events && point.x() >= plot.right() - 16)
        {
            for (const auto &e : track.events)
                accept(track, e.handle, e.seconds, e.details, qAbs(point.y() - y(e.seconds)));
            continue;
        }
        if (track.kind == analysis::TrackKind::Candidates && qAbs(point.x() - (plot.right() - 24)) < 7)
        {
            for (const auto &c : track.candidates)
                if (c.jumpSeconds)
                    accept(track, c.handle, *c.jumpSeconds, c.details, qAbs(point.y() - y(*c.jumpSeconds)));
            continue;
        }
        if (track.kind != analysis::TrackKind::Curve)
            continue;
        const auto key = track.laneGroup.isEmpty() ? track.id : track.laneGroup;
        int index = -1;
        for (int i = 0; i < geometry.size(); ++i)
            if (geometry[i].key == key)
            {
                index = i;
                break;
            }
        if (index < 0)
            continue;
        const analysis::TrackPoint *previous = nullptr;
        QPointF previousXY;
        for (const auto &p : track.points)
        {
            if (p.seconds < low || p.seconds > high || !std::isfinite(p.value))
                continue;
            const auto xy = position(p, geometry[index], index, geometry.size(), plot, y);
            accept(track, p.handle, p.seconds, p.details, std::hypot(point.x() - xy.x(), point.y() - xy.y()));
            if (previous && p.seconds - previous->seconds <= 12)
            {
                const auto delta = xy - previousXY;
                const double length = delta.x() * delta.x() + delta.y() * delta.y();
                if (length > 0)
                {
                    const auto offset = point - previousXY;
                    const double fraction = qBound(0., (offset.x() * delta.x() + offset.y() * delta.y()) / length, 1.);
                    const auto closest = previousXY + fraction * delta;
                    const auto &target = fraction < .5 ? *previous : p;
                    accept(track, target.handle, target.seconds, target.details,
                           std::hypot(point.x() - closest.x(), point.y() - closest.y()));
                }
            }
            previous = &p;
            previousXY = xy;
        }
    }
    return best;
}
