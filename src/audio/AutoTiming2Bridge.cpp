#include "AutoTiming2Bridge.h"

#include "autotiming/Analysis.h"

#include <stdexcept>

namespace
{
    QString toStringField(autotiming::WindowScale v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::TempoTrackState v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::TempoCurveKind v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::TempoMapFailureReason v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::TempoMapApproximationFailureReason v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::TempoPropagationReason v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::TempoHypothesisKind v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::PeriodicityRelationKind v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::RhythmLayerRole v) { return QString::fromLatin1(autotiming::toString(v)); }
    QString toStringField(autotiming::UncertaintyReason v) { return QString::fromLatin1(autotiming::toString(v)); }

    QString toStringField(autotiming::CandidateOrigin v)
    {
        // No upstream toString overload for CandidateOrigin; mirror enumerator
        // names locally.
        switch (v)
        {
        case autotiming::CandidateOrigin::LegacyWindowEstimate:
            return QStringLiteral("LegacyWindowEstimate");
        case autotiming::CandidateOrigin::OnsetEnvelopePeriodicity:
            return QStringLiteral("OnsetEnvelopePeriodicity");
        case autotiming::CandidateOrigin::HarmonicAlias:
            return QStringLiteral("HarmonicAlias");
        }
        return QStringLiteral("Unknown");
    }

    AutoTiming2Candidate mapCandidate(const autotiming::TempoCandidate &c)
    {
        AutoTiming2Candidate out;
        out.bpm = c.bpm;
        out.rawBpm = c.rawBpm;
        out.periodSeconds = c.periodSeconds;
        out.pulseTimeSeconds = c.pulseTimeSeconds;
        out.hasPulseTime = true;
        out.legacyOffsetMilliseconds = c.legacyOffsetMilliseconds;
        out.rawBpmUncertainty = c.rawBpmUncertainty;
        out.phaseConfidence = c.phaseConfidence;
        out.score = c.score;
        out.harmonicRatio = c.harmonicRatio;
        out.origin = toStringField(c.origin);
        out.signature = c.signature;
        out.division = c.division;
        return out;
    }

    AutoTiming2TrackPoint mapTrackPoint(const autotiming::TempoTrackPoint &p)
    {
        AutoTiming2TrackPoint out;
        out.timeSeconds = p.timeSeconds;
        out.bpm = p.bpm;
        out.pulseTimeSeconds = p.pulseTimeSeconds;
        out.confidence = p.confidence;
        out.phaseConfidence = p.phaseConfidence;
        out.harmonicFamilyId = p.harmonicFamilyId == autotiming::NoTempoFamily
                                   ? qsizetype(-1)
                                   : qsizetype(p.harmonicFamilyId);
        out.harmonicRatioToFamily = p.harmonicRatioToFamily;
        out.sourceWindowId = qsizetype(p.sourceWindowId);
        out.state = toStringField(p.state);
        out.propagationReason = toStringField(p.propagationReason);
        out.multiScalePhaseRefined = p.multiScalePhaseRefined;
        return out;
    }

    QVector<AutoTiming2TrackPoint> mapTrack(const autotiming::TempoTrack &track)
    {
        QVector<AutoTiming2TrackPoint> out;
        out.reserve(track.points.size());
        for (const autotiming::TempoTrackPoint &p : track.points)
            out.append(mapTrackPoint(p));
        return out;
    }

    AutoTiming2TempoMap mapTempoMap(const autotiming::TempoMap &map,
                                    const AutoTiming2Options &options)
    {
        AutoTiming2TempoMap out;
        out.available = map.available;
        out.failureReason = toStringField(map.failureReason);
        out.startSeconds = map.startSeconds;
        out.endSeconds = map.endSeconds;
        out.startBeat = map.startBeat;
        out.endBeat = map.endBeat;
        out.maximumAnchorResidualMilliseconds = map.maximumAnchorResidualMilliseconds;
        out.maximumTempoCorrectionBpm = map.maximumTempoCorrectionBpm;
        out.maximumTempoCorrectionRelative = map.maximumTempoCorrectionRelative;
        out.robustPulseCountCorrectionCount = qsizetype(map.robustPulseCountCorrectionCount);
        out.hasTempoChange = map.hasTempoChange;
        out.hasContinuousChange = map.hasContinuousChange;
        out.hasAbruptChange = map.hasAbruptChange;

        out.anchors.reserve(map.anchors.size());
        for (const autotiming::TempoMapAnchor &anchor : map.anchors)
        {
            AutoTiming2TempoMapAnchor mapped;
            mapped.timeSeconds = anchor.timeSeconds;
            mapped.observedBpm = anchor.observedBpm;
            mapped.modelBpm = anchor.modelBpm;
            mapped.confidence = anchor.confidence;
            mapped.phaseConfidence = anchor.phaseConfidence;
            mapped.phaseBeat = anchor.phaseBeat;
            mapped.pulseIndex = qint64(anchor.pulseIndex);
            out.anchors.append(mapped);
        }

        out.segments.reserve(map.segments.size());
        for (const autotiming::TempoCurveSegment &segment : map.segments)
        {
            AutoTiming2TempoCurveSegment mapped;
            mapped.startSeconds = segment.startSeconds;
            mapped.endSeconds = segment.endSeconds;
            mapped.startBeat = segment.startBeat;
            mapped.endBeat = segment.endBeat;
            mapped.startBpm = segment.startBpm;
            mapped.endBpm = segment.endBpm;
            mapped.phaseCubic = segment.phaseCubic;
            mapped.phaseQuadratic = segment.phaseQuadratic;
            mapped.phaseLinear = segment.phaseLinear;
            mapped.confidence = segment.confidence;
            mapped.kind = toStringField(segment.kind);
            out.segments.append(mapped);
        }

        autotiming::TempoMapApproximationOptions approximationOptions;
        approximationOptions.maximumTimeErrorMilliseconds =
            options.tempoMapMaximumTimeErrorMilliseconds;
        approximationOptions.maximumEntries =
            static_cast<std::size_t>(options.tempoMapMaximumEntries);
        const autotiming::TempoMapApproximation approximation =
            autotiming::approximateTempoMap(map, approximationOptions);
        out.bpmListAvailable = approximation.available;
        out.bpmListFailureReason = toStringField(approximation.failureReason);
        out.maximumBpmListModelErrorMilliseconds =
            approximation.maximumModelErrorMilliseconds;
        out.bpmList.reserve(approximation.points.size());
        for (const autotiming::TempoMapBpmPoint &point : approximation.points)
            out.bpmList.append({point.beat, point.bpm});
        return out;
    }

    void translateTempoMap(AutoTiming2TempoMap &map, double offsetSeconds)
    {
        if (!map.available)
            return;
        map.startSeconds += offsetSeconds;
        map.endSeconds += offsetSeconds;
        for (AutoTiming2TempoMapAnchor &anchor : map.anchors)
            anchor.timeSeconds += offsetSeconds;
        for (AutoTiming2TempoCurveSegment &segment : map.segments)
        {
            segment.startSeconds += offsetSeconds;
            segment.endSeconds += offsetSeconds;
        }
    }
} // namespace

bool AutoTiming2Bridge::analyzeMono(const QVector<float> &mono,
                                    int sampleRate,
                                    const AutoTiming2Options &options,
                                    AutoTiming2Summary &outSummary,
                                    QString *outError)
{
    outSummary = AutoTiming2Summary();
    if (outError)
        outError->clear();

    if (mono.isEmpty())
    {
        if (outError)
            *outError = QStringLiteral("音频数据为空，无法分析。");
        return false;
    }
    if (sampleRate != 32000 && sampleRate != 44100 && sampleRate != 48000)
    {
        if (outError)
            *outError = QStringLiteral("不支持的采样率 %1 Hz：AutoTiming 2 仅支持 32000/44100/48000 Hz，请先重采样。")
                            .arg(sampleRate);
        return false;
    }
    if (!(options.tempoMapMaximumTimeErrorMilliseconds > 0.0) ||
        options.tempoMapMaximumEntries < 2)
    {
        if (outError)
            *outError = QStringLiteral("AutoTiming 2 tempo-map BPM list options are invalid.");
        return false;
    }

    try
    {
        autotiming::AnalysisOptions native;
        if (!options.windowSpecs.isEmpty())
        {
            native.windowSpecs.clear();
            for (const auto &spec : options.windowSpecs)
            {
                autotiming::WindowSpec ws;
                if (spec.first <= 8.0)
                    ws.scale = autotiming::WindowScale::Short;
                else if (spec.first <= 24.0)
                    ws.scale = autotiming::WindowScale::Medium;
                else
                    ws.scale = autotiming::WindowScale::Long;
                ws.durationSeconds = spec.first;
                ws.hopSeconds = spec.second;
                native.windowSpecs.push_back(ws);
            }
        }
        native.minimumWindowSeconds = options.minimumWindowSeconds;
        native.minimumTempoBpm = options.minimumTempoBpm;
        native.maximumTempoBpm = options.maximumTempoBpm;
        native.anchorReliabilityThreshold = options.anchorReliabilityThreshold;
        native.maximumTrackedGapSeconds = options.maximumTrackedGapSeconds;

        const autotiming::AudioView view{mono.constData(),
                                         static_cast<std::size_t>(mono.size()),
                                         static_cast<std::uint32_t>(sampleRate),
                                         1u};
        const autotiming::AnalysisResult result = autotiming::analyze(view, native);

        outSummary.valid = true;
        outSummary.durationSeconds = result.metadata.durationSeconds;
        outSummary.sampleRate = static_cast<int>(result.metadata.sampleRate);
        outSummary.channels = static_cast<int>(result.metadata.channels);

        // Global candidates and families.
        outSummary.tempoCandidates.reserve(result.tempoCandidates.size());
        for (const autotiming::GlobalTempoCandidate &c : result.tempoCandidates)
        {
            AutoTiming2Candidate out;
            out.bpm = c.bpm;
            out.score = c.relativeScore;
            out.harmonicFamilyId = c.harmonicFamilyId == autotiming::NoTempoFamily
                                       ? qsizetype(-1)
                                       : qsizetype(c.harmonicFamilyId);
            out.harmonicRatioToFamily = c.harmonicRatioToFamily;
            for (std::size_t w : c.supportingWindowIds)
                out.supportingWindowIds.append(qsizetype(w));
            outSummary.tempoCandidates.append(out);
        }

        outSummary.tempoFamilies.reserve(result.tempoFamilies.size());
        for (const autotiming::GlobalTempoFamily &f : result.tempoFamilies)
        {
            AutoTiming2Family out;
            out.id = f.id == autotiming::NoTempoFamily ? qsizetype(-1) : qsizetype(f.id);
            out.primaryCandidateIndex = qsizetype(f.primaryCandidateIndex);
            out.referenceBpm = f.referenceBpm;
            out.relativeScore = f.relativeScore;
            for (std::size_t i : f.memberCandidateIndices)
                out.memberCandidateIndices.append(qsizetype(i));
            outSummary.tempoFamilies.append(out);
        }
        outSummary.tempoTrack = mapTrack(result.tempoTrack);
        outSummary.tempoMap = mapTempoMap(result.tempoMap, options);

        outSummary.tempoHypotheses.reserve(result.tempoHypotheses.size());
        for (const autotiming::TempoTrackHypothesis &h : result.tempoHypotheses)
        {
            AutoTiming2Hypothesis out;
            out.kind = toStringField(h.kind);
            out.averageObjectiveCost = h.averageObjectiveCost;
            out.selected = h.selected;
            out.track = mapTrack(h.track);
            out.tempoMap = mapTempoMap(h.tempoMap, options);
            outSummary.tempoHypotheses.append(out);
        }

        outSummary.periodicityLayers.reserve(result.periodicityLayers.size());
        for (const autotiming::PeriodicityLayer &l : result.periodicityLayers)
        {
            AutoTiming2Layer out;
            out.startSeconds = l.startSeconds;
            out.endSeconds = l.endSeconds;
            out.observedRateBpm = l.observedRateBpm;
            out.referenceTempoBpm = l.referenceTempoBpm;
            out.relativeRate = l.relativeRate;
            out.ratioNumerator = l.ratioNumerator;
            out.ratioDenominator = l.ratioDenominator;
            out.phaseOffsetCycles = l.phaseOffsetCycles;
            out.phaseConfidence = l.phaseConfidence;
            out.confidence = l.confidence;
            out.relation = toStringField(l.relation);
            for (std::size_t w : l.supportingWindowIds)
                out.supportingWindowIds.append(qsizetype(w));
            outSummary.periodicityLayers.append(out);
        }

        outSummary.rhythmProfiles.reserve(result.rhythmProfiles.size());
        for (const autotiming::RhythmProfile &profile : result.rhythmProfiles)
        {
            AutoTiming2RhythmProfile outProfile;
            outProfile.startSeconds = profile.startSeconds;
            outProfile.endSeconds = profile.endSeconds;
            outProfile.confidence = profile.confidence;
            outProfile.layers.reserve(profile.layers.size());
            for (const autotiming::RhythmLayer &layer : profile.layers)
            {
                AutoTiming2RhythmLayer outLayer;
                outLayer.startSeconds = layer.startSeconds;
                outLayer.endSeconds = layer.endSeconds;
                outLayer.observedRateBpm = layer.observedRateBpm;
                outLayer.referenceTempoBpm = layer.referenceTempoBpm;
                outLayer.relativeRate = layer.relativeRate;
                outLayer.ratioNumerator = layer.ratioNumerator;
                outLayer.ratioDenominator = layer.ratioDenominator;
                outLayer.phaseOffsetCycles = layer.phaseOffsetCycles;
                outLayer.phaseConfidence = layer.phaseConfidence;
                outLayer.confidence = layer.confidence;
                outLayer.role = toStringField(layer.role);
                for (std::size_t w : layer.supportingWindowIds)
                    outLayer.supportingWindowIds.append(qsizetype(w));
                outProfile.layers.append(outLayer);
            }
            outSummary.rhythmProfiles.append(outProfile);
        }

        outSummary.uncertainRegions.reserve(result.uncertainRegions.size());
        for (const autotiming::UncertainRegion &r : result.uncertainRegions)
        {
            AutoTiming2Region out;
            out.startSeconds = r.startSeconds;
            out.endSeconds = r.endSeconds;
            out.confidence = r.confidence;
            out.reason = toStringField(r.reason);
            outSummary.uncertainRegions.append(out);
        }

        outSummary.anchors.reserve(result.diagnostics.anchorRegions.size());
        for (const autotiming::AnchorRegion &a : result.diagnostics.anchorRegions)
        {
            AutoTiming2Anchor out;
            out.startSeconds = a.startSeconds;
            out.endSeconds = a.endSeconds;
            out.tempoBpm = a.tempoBpm;
            out.reliability = a.reliability;
            out.harmonicFamilyId = a.harmonicFamilyId == autotiming::NoTempoFamily
                                       ? qsizetype(-1)
                                       : qsizetype(a.harmonicFamilyId);
            out.harmonicRatioToFamily = a.harmonicRatioToFamily;
            for (std::size_t w : a.supportingWindowIds)
                out.supportingWindowIds.append(qsizetype(w));
            outSummary.anchors.append(out);
        }
        outSummary.windows.reserve(result.diagnostics.windows.size());
        for (const autotiming::AnalysisWindow &w : result.diagnostics.windows)
        {
            AutoTiming2Window out;
            out.id = qsizetype(w.id);
            out.scale = toStringField(w.scale);
            out.startSeconds = w.startSeconds;
            out.endSeconds = w.endSeconds;
            out.isFineTempoWindow = w.isFineTempoWindow;
            out.tempoEvidence = w.tempoEvidence;
            out.legacyTempoEvidence = w.legacyTempoEvidence;
            out.onsetPeriodicityEvidence = w.onsetPeriodicityEvidence;
            out.crossScaleConsistency = w.crossScaleConsistency;
            out.consistencyPeerCount = qsizetype(w.consistencyPeerCount);
            out.reliability = w.reliability;
            out.selectedAsAnchor = w.selectedAsAnchor;
            out.estimatorMessage = QString::fromStdString(w.estimatorMessage);
            out.tempoCandidates.reserve(w.tempoCandidates.size());
            for (const autotiming::TempoCandidate &c : w.tempoCandidates)
                out.tempoCandidates.append(mapCandidate(c));
            outSummary.windows.append(out);
        }

        outSummary.confidence.tempo = result.confidence.tempo;
        outSummary.confidence.tempoFamily = result.confidence.tempoFamily;
        outSummary.confidence.reliableCoverage = result.confidence.reliableCoverage;
        outSummary.confidence.overall = result.confidence.overall;

        outSummary.windowCount = static_cast<int>(result.diagnostics.windows.size());
        for (const autotiming::AnalysisWindow &w : result.diagnostics.windows)
            if (w.selectedAsAnchor)
                ++outSummary.anchorSelectedWindowCount;
        for (const autotiming::TempoTrackPoint &p : result.tempoTrack.points)
        {
            if (p.multiScalePhaseRefined)
                ++outSummary.multiScalePhaseRefinedCount;
            if (p.propagationReason ==
                autotiming::TempoPropagationReason::StableGridRegularization)
                ++outSummary.stableGridRegularizedCount;
        }
        return true;
    }
    catch (const std::invalid_argument &e)
    {
        if (outError)
            *outError = QStringLiteral("AutoTiming 2 参数无效：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }
    catch (const std::exception &e)
    {
        if (outError)
            *outError = QStringLiteral("AutoTiming 2 分析异常：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }
    catch (...)
    {
        if (outError)
            *outError = QStringLiteral("AutoTiming 2 分析异常：未知错误。");
        return false;
    }
}

void AutoTiming2Bridge::translateTimeline(AutoTiming2Summary &summary, double offsetSeconds)
{
    if (offsetSeconds == 0.0)
        return;

    const auto translateTrack = [offsetSeconds](QVector<AutoTiming2TrackPoint> &track)
    {
        for (AutoTiming2TrackPoint &point : track)
        {
            point.timeSeconds += offsetSeconds;
            point.pulseTimeSeconds += offsetSeconds;
        }
    };

    // Global candidates currently carry tempo only. If the bridge gains a
    // phase-bearing global candidate later, hasPulseTime makes the translation
    // explicit instead of turning a default zero into a fake pulse position.
    for (AutoTiming2Candidate &candidate : summary.tempoCandidates)
    {
        if (candidate.hasPulseTime)
            candidate.pulseTimeSeconds += offsetSeconds;
    }

    translateTrack(summary.tempoTrack);
    translateTempoMap(summary.tempoMap, offsetSeconds);
    for (AutoTiming2Hypothesis &hypothesis : summary.tempoHypotheses)
    {
        translateTrack(hypothesis.track);
        translateTempoMap(hypothesis.tempoMap, offsetSeconds);
    }

    for (AutoTiming2Layer &layer : summary.periodicityLayers)
    {
        layer.startSeconds += offsetSeconds;
        layer.endSeconds += offsetSeconds;
    }
    for (AutoTiming2RhythmProfile &profile : summary.rhythmProfiles)
    {
        profile.startSeconds += offsetSeconds;
        profile.endSeconds += offsetSeconds;
        for (AutoTiming2RhythmLayer &layer : profile.layers)
        {
            layer.startSeconds += offsetSeconds;
            layer.endSeconds += offsetSeconds;
        }
    }
    for (AutoTiming2Region &region : summary.uncertainRegions)
    {
        region.startSeconds += offsetSeconds;
        region.endSeconds += offsetSeconds;
    }
    for (AutoTiming2Anchor &anchor : summary.anchors)
    {
        anchor.startSeconds += offsetSeconds;
        anchor.endSeconds += offsetSeconds;
    }
    for (AutoTiming2Window &window : summary.windows)
    {
        window.startSeconds += offsetSeconds;
        window.endSeconds += offsetSeconds;
        for (AutoTiming2Candidate &candidate : window.tempoCandidates)
        {
            if (candidate.hasPulseTime)
                candidate.pulseTimeSeconds += offsetSeconds;
        }
    }
}
