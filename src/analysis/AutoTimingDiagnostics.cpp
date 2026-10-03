#include "AutoTimingDiagnostics.h"
#include <QJsonArray>
#include <cmath>
namespace
{
QJsonValue value(double v)
{
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue();
}
QJsonValue value(bool v)
{
    return v;
}
QJsonValue value(const QString &v)
{
    return v;
}
template <class T> QJsonValue value(T v)
{
    return double(v);
}
QJsonObject object(const AutoTiming2Candidate &v);
QJsonObject object(const AutoTiming2Window &v);
QJsonObject object(const AutoTiming2Family &v);
QJsonObject object(const AutoTiming2TrackPoint &v);
QJsonObject object(const AutoTiming2TempoMapAnchor &v);
QJsonObject object(const AutoTiming2TempoCurveSegment &v);
QJsonObject object(const AutoTiming2BpmPoint &v);
QJsonObject object(const AutoTiming2TempoMap &v);
QJsonObject object(const AutoTiming2Hypothesis &v);
QJsonObject object(const AutoTiming2Layer &v);
QJsonObject object(const AutoTiming2RhythmLayer &v);
QJsonObject object(const AutoTiming2RhythmProfile &v);
QJsonObject object(const AutoTiming2Region &v);
QJsonObject object(const AutoTiming2Anchor &v);
QJsonObject object(const AutoTiming2Confidence &v);
QJsonObject object(const AutoTiming2Summary &v);
QJsonValue value(const AutoTiming2Candidate &v);
QJsonValue value(const AutoTiming2Window &v);
QJsonValue value(const AutoTiming2Family &v);
QJsonValue value(const AutoTiming2TrackPoint &v);
QJsonValue value(const AutoTiming2TempoMapAnchor &v);
QJsonValue value(const AutoTiming2TempoCurveSegment &v);
QJsonValue value(const AutoTiming2BpmPoint &v);
QJsonValue value(const AutoTiming2TempoMap &v);
QJsonValue value(const AutoTiming2Hypothesis &v);
QJsonValue value(const AutoTiming2Layer &v);
QJsonValue value(const AutoTiming2RhythmLayer &v);
QJsonValue value(const AutoTiming2RhythmProfile &v);
QJsonValue value(const AutoTiming2Region &v);
QJsonValue value(const AutoTiming2Anchor &v);
QJsonValue value(const AutoTiming2Confidence &v);
QJsonValue value(const AutoTiming2Summary &v);
template <class T> QJsonArray array(const QVector<T> &items)
{
    QJsonArray a;
    for (const auto &v : items)
        a.append(value(v));
    return a;
}
QJsonValue value(const AutoTiming2Candidate &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Window &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Family &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2TrackPoint &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2TempoMapAnchor &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2TempoCurveSegment &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2BpmPoint &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2TempoMap &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Hypothesis &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Layer &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2RhythmLayer &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2RhythmProfile &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Region &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Anchor &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Confidence &v)
{
    return object(v);
}
QJsonValue value(const AutoTiming2Summary &v)
{
    return object(v);
}
QJsonObject object(const AutoTiming2Candidate &v)
{
    QJsonObject o;
    o["bpm"] = value(v.bpm);
    o["rawBpm"] = value(v.rawBpm);
    o["periodSeconds"] = value(v.periodSeconds);
    o["pulseTimeSeconds"] = v.hasPulseTime ? value(v.pulseTimeSeconds) : QJsonValue();
    o["hasPulseTime"] = value(v.hasPulseTime);
    o["legacyOffsetMilliseconds"] = value(v.legacyOffsetMilliseconds);
    o["rawBpmUncertainty"] = value(v.rawBpmUncertainty);
    o["phaseConfidence"] = value(v.phaseConfidence);
    o["score"] = value(v.score);
    o["harmonicRatio"] = value(v.harmonicRatio);
    o["origin"] = value(v.origin);
    o["signature"] = value(v.signature);
    o["division"] = value(v.division);
    o["harmonicFamilyId"] = value(v.harmonicFamilyId);
    o["harmonicRatioToFamily"] = value(v.harmonicRatioToFamily);
    o["supportingWindowIds"] = array(v.supportingWindowIds);
    return o;
}
QJsonObject object(const AutoTiming2Window &v)
{
    QJsonObject o;
    o["id"] = value(v.id);
    o["scale"] = value(v.scale);
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["isFineTempoWindow"] = value(v.isFineTempoWindow);
    o["tempoEvidence"] = value(v.tempoEvidence);
    o["legacyTempoEvidence"] = value(v.legacyTempoEvidence);
    o["onsetPeriodicityEvidence"] = value(v.onsetPeriodicityEvidence);
    o["crossScaleConsistency"] = value(v.crossScaleConsistency);
    o["consistencyPeerCount"] = value(v.consistencyPeerCount);
    o["reliability"] = value(v.reliability);
    o["selectedAsAnchor"] = value(v.selectedAsAnchor);
    o["estimatorMessage"] = value(v.estimatorMessage);
    o["tempoCandidates"] = array(v.tempoCandidates);
    return o;
}
QJsonObject object(const AutoTiming2Family &v)
{
    QJsonObject o;
    o["id"] = value(v.id);
    o["primaryCandidateIndex"] = value(v.primaryCandidateIndex);
    o["referenceBpm"] = value(v.referenceBpm);
    o["relativeScore"] = value(v.relativeScore);
    o["memberCandidateIndices"] = array(v.memberCandidateIndices);
    return o;
}
QJsonObject object(const AutoTiming2TrackPoint &v)
{
    QJsonObject o;
    o["timeSeconds"] = value(v.timeSeconds);
    o["bpm"] = value(v.bpm);
    o["pulseTimeSeconds"] = value(v.pulseTimeSeconds);
    o["confidence"] = value(v.confidence);
    o["phaseConfidence"] = value(v.phaseConfidence);
    o["harmonicFamilyId"] = value(v.harmonicFamilyId);
    o["harmonicRatioToFamily"] = value(v.harmonicRatioToFamily);
    o["sourceWindowId"] = value(v.sourceWindowId);
    o["state"] = value(v.state);
    o["propagationReason"] = value(v.propagationReason);
    o["multiScalePhaseRefined"] = value(v.multiScalePhaseRefined);
    return o;
}
QJsonObject object(const AutoTiming2TempoMapAnchor &v)
{
    QJsonObject o;
    o["timeSeconds"] = value(v.timeSeconds);
    o["observedBpm"] = value(v.observedBpm);
    o["modelBpm"] = value(v.modelBpm);
    o["confidence"] = value(v.confidence);
    o["phaseConfidence"] = value(v.phaseConfidence);
    o["phaseBeat"] = value(v.phaseBeat);
    o["pulseIndex"] = QString::number(v.pulseIndex);
    return o;
}
QJsonObject object(const AutoTiming2TempoCurveSegment &v)
{
    QJsonObject o;
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["startBeat"] = value(v.startBeat);
    o["endBeat"] = value(v.endBeat);
    o["startBpm"] = value(v.startBpm);
    o["endBpm"] = value(v.endBpm);
    o["phaseCubic"] = value(v.phaseCubic);
    o["phaseQuadratic"] = value(v.phaseQuadratic);
    o["phaseLinear"] = value(v.phaseLinear);
    o["confidence"] = value(v.confidence);
    o["kind"] = value(v.kind);
    return o;
}
QJsonObject object(const AutoTiming2BpmPoint &v)
{
    QJsonObject o;
    o["beat"] = value(v.beat);
    o["bpm"] = value(v.bpm);
    return o;
}
QJsonObject object(const AutoTiming2TempoMap &v)
{
    QJsonObject o;
    o["available"] = value(v.available);
    o["failureReason"] = value(v.failureReason);
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["startBeat"] = value(v.startBeat);
    o["endBeat"] = value(v.endBeat);
    o["maximumAnchorResidualMilliseconds"] = value(v.maximumAnchorResidualMilliseconds);
    o["maximumTempoCorrectionBpm"] = value(v.maximumTempoCorrectionBpm);
    o["maximumTempoCorrectionRelative"] = value(v.maximumTempoCorrectionRelative);
    o["robustPulseCountCorrectionCount"] = value(v.robustPulseCountCorrectionCount);
    o["phaseCoherentIntervalCount"] = value(v.phaseCoherentIntervalCount);
    o["hasTempoChange"] = value(v.hasTempoChange);
    o["hasContinuousChange"] = value(v.hasContinuousChange);
    o["hasAbruptChange"] = value(v.hasAbruptChange);
    o["anchors"] = array(v.anchors);
    o["segments"] = array(v.segments);
    o["bpmListAvailable"] = value(v.bpmListAvailable);
    o["bpmListFailureReason"] = value(v.bpmListFailureReason);
    o["maximumBpmListModelErrorMilliseconds"] = value(v.maximumBpmListModelErrorMilliseconds);
    o["bpmList"] = array(v.bpmList);
    return o;
}
QJsonObject object(const AutoTiming2Hypothesis &v)
{
    QJsonObject o;
    o["kind"] = value(v.kind);
    o["averageObjectiveCost"] = value(v.averageObjectiveCost);
    o["selected"] = value(v.selected);
    o["track"] = array(v.track);
    o["tempoMap"] = value(v.tempoMap);
    return o;
}
QJsonObject object(const AutoTiming2Layer &v)
{
    QJsonObject o;
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["observedRateBpm"] = value(v.observedRateBpm);
    o["referenceTempoBpm"] = value(v.referenceTempoBpm);
    o["relativeRate"] = value(v.relativeRate);
    o["ratioNumerator"] = value(v.ratioNumerator);
    o["ratioDenominator"] = value(v.ratioDenominator);
    o["phaseOffsetCycles"] = value(v.phaseOffsetCycles);
    o["phaseConfidence"] = value(v.phaseConfidence);
    o["highRatePulseCoverage"] = value(v.highRatePulseCoverage);
    o["highRateTransientDensityCoverage"] = value(v.highRateTransientDensityCoverage);
    o["confidence"] = value(v.confidence);
    o["relation"] = value(v.relation);
    o["supportingWindowIds"] = array(v.supportingWindowIds);
    o["tempoCandidateSupportingWindowIds"] = array(v.tempoCandidateSupportingWindowIds);
    o["highRateRhythmSupportingWindowIds"] = array(v.highRateRhythmSupportingWindowIds);
    return o;
}
QJsonObject object(const AutoTiming2RhythmLayer &v)
{
    QJsonObject o;
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["observedRateBpm"] = value(v.observedRateBpm);
    o["referenceTempoBpm"] = value(v.referenceTempoBpm);
    o["relativeRate"] = value(v.relativeRate);
    o["ratioNumerator"] = value(v.ratioNumerator);
    o["ratioDenominator"] = value(v.ratioDenominator);
    o["phaseOffsetCycles"] = value(v.phaseOffsetCycles);
    o["phaseConfidence"] = value(v.phaseConfidence);
    o["pulseCoverage"] = value(v.pulseCoverage);
    o["transientDensityCoverage"] = value(v.transientDensityCoverage);
    o["confidence"] = value(v.confidence);
    o["role"] = value(v.role);
    o["supportingWindowIds"] = array(v.supportingWindowIds);
    return o;
}
QJsonObject object(const AutoTiming2RhythmProfile &v)
{
    QJsonObject o;
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["confidence"] = value(v.confidence);
    o["layers"] = array(v.layers);
    return o;
}
QJsonObject object(const AutoTiming2Region &v)
{
    QJsonObject o;
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["confidence"] = value(v.confidence);
    o["reason"] = value(v.reason);
    return o;
}
QJsonObject object(const AutoTiming2Anchor &v)
{
    QJsonObject o;
    o["startSeconds"] = value(v.startSeconds);
    o["endSeconds"] = value(v.endSeconds);
    o["tempoBpm"] = value(v.tempoBpm);
    o["reliability"] = value(v.reliability);
    o["harmonicFamilyId"] = value(v.harmonicFamilyId);
    o["harmonicRatioToFamily"] = value(v.harmonicRatioToFamily);
    o["supportingWindowIds"] = array(v.supportingWindowIds);
    return o;
}
QJsonObject object(const AutoTiming2Confidence &v)
{
    QJsonObject o;
    o["tempo"] = value(v.tempo);
    o["tempoFamily"] = value(v.tempoFamily);
    o["reliableCoverage"] = value(v.reliableCoverage);
    o["overall"] = value(v.overall);
    return o;
}
QJsonObject object(const AutoTiming2Summary &v)
{
    QJsonObject o;
    o["valid"] = value(v.valid);
    o["durationSeconds"] = value(v.durationSeconds);
    o["sampleRate"] = value(v.sampleRate);
    o["channels"] = value(v.channels);
    o["tempoCandidates"] = array(v.tempoCandidates);
    o["tempoFamilies"] = array(v.tempoFamilies);
    o["tempoTrack"] = array(v.tempoTrack);
    o["tempoMap"] = value(v.tempoMap);
    o["tempoHypotheses"] = array(v.tempoHypotheses);
    o["periodicityLayers"] = array(v.periodicityLayers);
    o["rhythmProfiles"] = array(v.rhythmProfiles);
    o["uncertainRegions"] = array(v.uncertainRegions);
    o["anchors"] = array(v.anchors);
    o["windows"] = array(v.windows);
    o["confidence"] = value(v.confidence);
    o["windowCount"] = value(v.windowCount);
    o["anchorSelectedWindowCount"] = value(v.anchorSelectedWindowCount);
    o["multiScalePhaseRefinedCount"] = value(v.multiScalePhaseRefinedCount);
    o["stableGridRegularizedCount"] = value(v.stableGridRegularizedCount);
    return o;
}
} // namespace
QJsonObject analysis::timingDiagnostics(const BpmDetector::DetectionResult &r)
{
    QJsonObject o = object(r.analysis);
    o["schemaVersion"] = 1;
    o["timeline"] = "whole-file audio seconds";
    const char *legacy[] = {"NotRequested", "Succeeded", "Failed", "Cancelled"};
    const char *status[] = {"NotRequested", "Succeeded", "Failed", "Cancelled"};
    o["legacyStatus"] = legacy[int(r.legacyStatus)];
    o["legacyError"] = r.legacyError;
    o["analysisStatus"] = status[int(r.analysisStatus)];
    o["analysisError"] = r.analysisError;
    o["analysisStartMs"] = value(r.analysisStartMs);
    o["legacyBpm"] = r.hasLegacyResult() ? value(r.bpm) : QJsonValue();
    o["legacyOffsetMilliseconds"] = r.hasLegacyResult() ? value(r.estimatedOffsetMs) : QJsonValue();
    return o;
}
