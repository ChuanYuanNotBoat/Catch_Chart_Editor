#pragma once
#include "SpectrumAnalysis.h"
#include "TransientAnalysis.h"
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <optional>
namespace analysis
{
enum class TrackKind
{
    Curve,
    Events,
    Regions,
    Candidates
};
struct TrackPoint
{
    double seconds = 0, value = 0;
    QString handle, disposition;
    std::optional<double> confidence;
    QJsonObject details;
};
struct AnalysisEvent
{
    QString handle, label, disposition;
    double seconds = 0;
    std::optional<double> confidence;
    QJsonObject details;
};
struct AnalysisRegion
{
    QString handle, label, disposition;
    double startSeconds = 0, endSeconds = 0;
    std::optional<double> confidence;
    QJsonObject details;
};
struct AnalysisCandidate
{
    QString handle, label, disposition;
    std::optional<double> jumpSeconds, confidence;
    QJsonObject details;
};
// Audio coordinates stay authoritative. transformId identifies a display
// projection only; it never changes stored seconds or sample/frame references.
// Confidence is an optional raw upstream field, not a calibrated probability.
struct AnalysisTrack
{
    QString id, label, unit;
    QVector<TrackPoint> points;
    bool visible = true;
    TrackKind kind = TrackKind::Curve;
    QJsonObject provenance;
    QString transformId, viewGroup, laneGroup;
    int zOrder = 20;
    bool readOnly = true, interactive = true;
    QVector<AnalysisEvent> events;
    QVector<AnalysisRegion> regions;
    QVector<AnalysisCandidate> candidates;
};
QVector<AnalysisTrack> diagnosticTracks(const QJsonObject &);
QVector<AnalysisTrack> spectrumTracks(const StereoSpectrum &, const QJsonObject &source = {});
QVector<AnalysisTrack> transientTracks(const TransientResult &, const QJsonObject &source = {});
} // namespace analysis
