#pragma once
#include "analysis/AnalysisTracks.h"
#include <QPointF>
#include <QRectF>
#include <functional>
class QPainter;
struct AnalysisTrackHit
{
    QString trackId, handle;
    double seconds = 0;
    QJsonObject details;
};
// One renderer/hit boundary for every read-only analysis track. No document,
// playback, estimator or persisted configuration ownership.
class AnalysisTrackScene
{
public:
    using Projection = std::function<double(double)>;
    void setTracks(QVector<analysis::AnalysisTrack>);
    void setTransformIdentity(const QString &);
    const QVector<analysis::AnalysisTrack> &tracks() const
    {
        return m_tracks;
    }
    void draw(QPainter &, const QRectF &, double lowSeconds, double highSeconds, const Projection &) const;
    std::optional<AnalysisTrackHit> hit(const QPointF &, const QRectF &, double lowSeconds, double highSeconds,
                                        const Projection &) const;

private:
    QVector<analysis::AnalysisTrack> m_tracks;
};
