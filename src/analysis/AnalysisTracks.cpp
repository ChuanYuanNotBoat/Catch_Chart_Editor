#include "AnalysisTracks.h"
#include <QJsonArray>
#include <QStringList>
#include <cmath>
namespace analysis
{
namespace
{
std::optional<double> number(const QJsonValue &v)
{
    return v.isDouble() && std::isfinite(v.toDouble()) ? std::optional<double>(v.toDouble()) : std::nullopt;
}
QJsonObject provenance(const QJsonObject &result, const QString &method)
{
    QJsonObject p{{"timeline", "whole-file audio seconds"}, {"method", method}};
    for (const auto *key :
         {"sourceIdentity", "encodedSha256", "analysisPcmSha256", "algorithmVersion", "configHash", "runId",
          "sampleRate", "analysisStartMs", "analysisSampleRate", "sourceChannels", "hopSeconds", "fftSize",
          "audioStartSeconds", "audioDurationSeconds", "relativeThreshold", "minimumIntervalSeconds"})
        if (result.contains(key))
            p[key] = result.value(key);
    return p;
}
QJsonObject linked(QJsonObject details, const QString &table, int row)
{
    details["sourceTable"] = table;
    details["sourceRow"] = row;
    return details;
}
} // namespace
QVector<AnalysisTrack> diagnosticTracks(const QJsonObject &result)
{
    QVector<AnalysisTrack> tracks;
    auto append = [&](const QString &id, const QString &label, const QString &unit, const char *table,
                      const char *value) {
        AnalysisTrack t{id, label, unit};
        t.provenance = provenance(result, "AutoTiming output projection");
        t.viewGroup = id;
        t.laneGroup = id == "phase" ? "confidence" : id;
        const auto rows = result.value(table).toArray();
        for (int i = 0; i < rows.size(); ++i)
        {
            auto row = rows[i].toObject();
            auto x = number(row.value("timeSeconds")), y = number(row.value(value));
            if (x && y)
                t.points.append({*x, *y, id + ":" + QString::number(i), row.value("state").toString(),
                                 number(row.value("confidence")), linked(row, table, i)});
        }
        if (!t.points.isEmpty())
            tracks.append(t);
    };
    append("tempo", "Observed tempo", "BPM", "tempoTrack", "bpm");
    append("confidence", "Confidence", "raw", "tempoTrack", "confidence");
    append("phase", "Phase confidence", "raw", "tempoTrack", "phaseConfidence");
    append("residual", "Observation / model residual", "ms", "derivedAnchorFit", "residualMilliseconds");
    AnalysisTrack regions{"uncertainty", "Uncertain coverage", "reason"};
    regions.kind = TrackKind::Regions;
    regions.zOrder = 5;
    regions.viewGroup = "uncertainty";
    regions.provenance = provenance(result, "Core uncertainty regions");
    const auto rows = result.value("uncertainRegions").toArray();
    for (int i = 0; i < rows.size(); ++i)
    {
        auto row = rows[i].toObject();
        auto a = number(row.value("startSeconds")), b = number(row.value("endSeconds"));
        if (a && b && *b > *a)
            regions.regions.append({"uncertainty:" + QString::number(i), row.value("reason").toString(),
                                    row.value("reason").toString(), *a, *b, number(row.value("confidence")),
                                    linked(row, "uncertainRegions", i)});
    }
    if (!regions.regions.isEmpty())
        tracks.append(regions);
    AnalysisTrack candidates{"candidateEvents", "Phase-bearing candidates", "BPM"};
    candidates.kind = TrackKind::Candidates;
    candidates.viewGroup = "candidateEvents";
    candidates.zOrder = 35;
    candidates.provenance = provenance(result, "Core candidates; absent phase has no jump target");
    const auto global = result.value("tempoCandidates").toArray();
    for (int i = 0; i < global.size(); ++i)
    {
        auto row = global[i].toObject();
        candidates.candidates.append({"candidate:" + QString::number(i),
                                      QString::number(row.value("bpm").toDouble()) + " BPM",
                                      row.value("origin").toString(), number(row.value("pulseTimeSeconds")),
                                      number(row.value("phaseConfidence")), linked(row, "tempoCandidates", i)});
    }
    const auto windows = result.value("windows").toArray();
    for (int w = 0; w < windows.size(); ++w)
    {
        const auto window = windows[w].toObject();
        const auto local = window.value("tempoCandidates").toArray();
        for (int i = 0; i < local.size(); ++i)
        {
            auto row = linked(local[i].toObject(), "windowCandidates", i);
            row["sourceWindowRow"] = w;
            row["windowId"] = window.value("id");
            row["startSeconds"] = window.value("startSeconds");
            row["endSeconds"] = window.value("endSeconds");
            candidates.candidates.append({QStringLiteral("window:%1:candidate:%2").arg(w).arg(i),
                                          QString::number(row.value("bpm").toDouble()) + " BPM",
                                          row.value("origin").toString(), number(row.value("pulseTimeSeconds")),
                                          number(row.value("phaseConfidence")), row});
        }
    }
    if (!candidates.candidates.isEmpty())
        tracks.append(candidates);
    return tracks;
}
QVector<AnalysisTrack> spectrumTracks(const StereoSpectrum &s, const QJsonObject &source)
{
    if (!s.valid() || !std::isfinite(s.startSeconds) || !std::isfinite(s.hopSeconds) || s.hopSeconds <= 0)
        return {};
    const QStringList ids{"spectrum.leftRms", "spectrum.rightRms", "spectrum.leftPeak", "spectrum.rightPeak"};
    const QStringList labels{"Spectrum left RMS", "Spectrum right RMS", "Spectrum left peak", "Spectrum right peak"};
    QVector<AnalysisTrack> tracks;
    for (int channel = 0; channel < 4; ++channel)
    {
        AnalysisTrack t{ids[channel], labels[channel], "amplitude"};
        t.viewGroup = "spectrumEnvelope";
        t.laneGroup = "spectrumEnvelope";
        t.provenance = provenance(source, "Stereo spectrum frame envelope");
        t.provenance["sampleRate"] = s.sampleRate;
        t.provenance["hopSeconds"] = s.hopSeconds;
        t.provenance["startSeconds"] = s.startSeconds;
        for (size_t i = 0; i < s.frames.size(); ++i)
        {
            const auto &f = s.frames[i];
            const double values[]{f.leftRms, f.rightRms, f.leftPeak, f.rightPeak};
            if (std::isfinite(values[channel]))
                t.points.append({s.startSeconds + i * s.hopSeconds,
                                 values[channel],
                                 ids[channel] + ":" + QString::number(i),
                                 "Measured",
                                 {},
                                 {{"frameIndex", QString::number(i)}}});
        }
        tracks.append(t);
    }
    return tracks;
}
QVector<AnalysisTrack> transientTracks(const TransientResult &r, const QJsonObject &source)
{
    if (!r.valid())
        return {};
    QVector<AnalysisTrack> tracks;
    const QStringList ids{"transient.all", "transient.low", "transient.mid", "transient.high"};
    for (int band = 0; band < 4; ++band)
    {
        AnalysisTrack t{ids[band], ids[band], "flux"};
        t.viewGroup = "transientFlux";
        t.laneGroup = "transientFlux";
        t.provenance = provenance(source, "Positive stereo display-band flux");
        for (size_t i = 0; i < r.frames.size(); ++i)
        {
            const auto &f = r.frames[i];
            const double value = band ? f.bands[band - 1] : f.strength;
            if (std::isfinite(f.timeSeconds) && std::isfinite(value))
                t.points.append({f.timeSeconds,
                                 value,
                                 ids[band] + ":" + QString::number(i),
                                 "Observed",
                                 {},
                                 {{"frameIndex", QString::number(i)}, {"threshold", f.threshold}}});
        }
        tracks.append(t);
    }
    AnalysisTrack events{"transient.peaks", "Transient peaks", "flux"};
    events.viewGroup = "transientEvents";
    events.kind = TrackKind::Events;
    events.zOrder = 40;
    events.provenance = provenance(source, "Transient peak dispositions; strength is not confidence");
    const QStringList dispositions{"Detected", "BelowThreshold", "TooClose", "PageBoundary"};
    for (size_t i = 0; i < r.peaks.size(); ++i)
    {
        const auto &p = r.peaks[i];
        if (!std::isfinite(p.timeSeconds))
            continue;
        events.events.append({"transient.peaks:" + QString::number(i),
                              "Transient peak",
                              dispositions[int(p.disposition)],
                              p.timeSeconds,
                              {},
                              {{"sourceTable", "transientPeaks"},
                               {"sourceRow", double(i)},
                               {"frameIndex", QString::number(p.frame)},
                               {"timeSeconds", p.timeSeconds},
                               {"strength", p.strength},
                               {"widthSeconds", p.widthSeconds},
                               {"disposition", dispositions[int(p.disposition)]}}});
    }
    if (!events.events.isEmpty())
        tracks.append(events);
    return tracks;
}
} // namespace analysis
