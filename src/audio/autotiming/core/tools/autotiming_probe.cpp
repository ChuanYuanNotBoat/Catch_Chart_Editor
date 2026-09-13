// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#include "autotiming/Analysis.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printUsage(std::ostream& output)
{
    output <<
        "Usage: autotiming_probe <audio.f32le> <sample-rate> <channels>\n"
        "\n"
        "The input is headerless, interleaved IEEE float32 little-endian PCM.\n"
        "The probe writes AutoTiming 2 evidence diagnostics as JSON to stdout.\n";
}

std::uint32_t parsePositiveInteger(const char* text, const char* name)
{
    const std::string value(text);
    std::size_t parsed = 0;
    const unsigned long number = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || number == 0 ||
        number > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return static_cast<std::uint32_t>(number);
}

std::vector<float> readFloatPcm(const char* path, std::uint32_t channels)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(std::string("cannot open input: ") + path);
    }

    const std::streamoff byteCount = input.tellg();
    if (byteCount < 0 || byteCount % static_cast<std::streamoff>(sizeof(float) * channels) != 0) {
        throw std::runtime_error("input size is not a whole number of PCM frames");
    }
    if (static_cast<unsigned long long>(byteCount) >
        static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("input is too large for this process");
    }

    std::vector<float> samples(static_cast<std::size_t>(byteCount) / sizeof(float));
    input.seekg(0, std::ios::beg);
    if (!samples.empty()) {
        input.read(reinterpret_cast<char*>(samples.data()), byteCount);
        if (!input) {
            throw std::runtime_error("failed while reading input PCM");
        }
    }
    return samples;
}

void writeQuoted(std::ostream& output, const std::string& value)
{
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20) {
                output << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(character)
                    << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
}

void writeNumber(std::ostream& output, double value)
{
    if (std::isfinite(value)) {
        output << value;
    } else {
        output << "null";
    }
}

void writeTempoFamilyId(std::ostream& output, std::size_t value)
{
    if (value == autotiming::NoTempoFamily) {
        output << "null";
    } else {
        output << value;
    }
}

template <typename Value, typename Writer>
void writeArray(std::ostream& output, const std::vector<Value>& values, Writer writer)
{
    output << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            output << ',';
        }
        writer(values[i]);
    }
    output << ']';
}

void writeTempoCandidate(std::ostream& output, const autotiming::GlobalTempoCandidate& candidate)
{
    output << "{\"bpm\":";
    writeNumber(output, candidate.bpm);
    output << ",\"relativeScore\":";
    writeNumber(output, candidate.relativeScore);
    output << ",\"harmonicFamilyId\":";
    writeTempoFamilyId(output, candidate.harmonicFamilyId);
    output << ",\"harmonicRatioToFamily\":";
    writeNumber(output, candidate.harmonicRatioToFamily);
    output << ",\"supportingWindowIds\":";
    writeArray(output, candidate.supportingWindowIds, [&](std::size_t id) {
        output << id;
    });
    output << '}';
}

void writeAnchor(std::ostream& output, const autotiming::AnchorRegion& region)
{
    output << "{\"startSeconds\":";
    writeNumber(output, region.startSeconds);
    output << ",\"endSeconds\":";
    writeNumber(output, region.endSeconds);
    output << ",\"tempoBpm\":";
    writeNumber(output, region.tempoBpm);
    output << ",\"reliability\":";
    writeNumber(output, region.reliability);
    output << ",\"harmonicFamilyId\":";
    writeTempoFamilyId(output, region.harmonicFamilyId);
    output << ",\"harmonicRatioToFamily\":";
    writeNumber(output, region.harmonicRatioToFamily);
    output << ",\"supportingWindowIds\":";
    writeArray(output, region.supportingWindowIds, [&](std::size_t id) {
        output << id;
    });
    output << '}';
}

void writeTempoFamily(std::ostream& output, const autotiming::GlobalTempoFamily& family)
{
    output << "{\"id\":" << family.id;
    output << ",\"primaryCandidateIndex\":" << family.primaryCandidateIndex;
    output << ",\"referenceBpm\":";
    writeNumber(output, family.referenceBpm);
    output << ",\"relativeScore\":";
    writeNumber(output, family.relativeScore);
    output << ",\"memberCandidateIndices\":";
    writeArray(output, family.memberCandidateIndices, [&](std::size_t index) {
        output << index;
    });
    output << '}';
}

void writeTempoTrackPoint(std::ostream& output, const autotiming::TempoTrackPoint& point)
{
    output << "{\"timeSeconds\":";
    writeNumber(output, point.timeSeconds);
    output << ",\"bpm\":";
    writeNumber(output, point.bpm);
    output << ",\"pulseTimeSeconds\":";
    writeNumber(output, point.pulseTimeSeconds);
    output << ",\"confidence\":";
    writeNumber(output, point.confidence);
    output << ",\"phaseConfidence\":";
    writeNumber(output, point.phaseConfidence);
    output << ",\"harmonicFamilyId\":";
    writeTempoFamilyId(output, point.harmonicFamilyId);
    output << ",\"harmonicRatioToFamily\":";
    writeNumber(output, point.harmonicRatioToFamily);
    output << ",\"sourceWindowId\":" << point.sourceWindowId;
    output << ",\"state\":";
    writeQuoted(output, autotiming::toString(point.state));
    output << ",\"propagationReason\":";
    writeQuoted(output, autotiming::toString(point.propagationReason));
    output << ",\"multiScalePhaseRefined\":"
        << (point.multiScalePhaseRefined ? "true" : "false");
    output << '}';
}

void writeTempoSegment(std::ostream& output, const autotiming::TempoSegment& segment)
{
    output << "{\"startSeconds\":";
    writeNumber(output, segment.startSeconds);
    output << ",\"endSeconds\":";
    writeNumber(output, segment.endSeconds);
    output << ",\"startBpm\":";
    writeNumber(output, segment.startBpm);
    output << ",\"endBpm\":";
    writeNumber(output, segment.endBpm);
    output << ",\"pulseTimeAtStartSeconds\":";
    writeNumber(output, segment.pulseTimeAtStartSeconds);
    output << ",\"confidence\":";
    writeNumber(output, segment.confidence);
    output << ",\"harmonicFamilyId\":";
    writeTempoFamilyId(output, segment.harmonicFamilyId);
    output << ",\"state\":";
    writeQuoted(output, autotiming::toString(segment.state));
    output << ",\"isContinuousChange\":"
        << (segment.isContinuousChange ? "true" : "false");
    output << ",\"supportingWindowIds\":";
    writeArray(output, segment.supportingWindowIds, [&](std::size_t id) {
        output << id;
    });
    output << '}';
}

void writeTempoTrack(std::ostream& output, const autotiming::TempoTrack& track)
{
    output << "{\"points\":";
    writeArray(output, track.points, [&](const autotiming::TempoTrackPoint& point) {
        writeTempoTrackPoint(output, point);
    });
    output << ",\"segments\":";
    writeArray(output, track.segments, [&](const autotiming::TempoSegment& segment) {
        writeTempoSegment(output, segment);
    });
    output << '}';
}

void writeTempoHypothesis(
    std::ostream& output,
    const autotiming::TempoTrackHypothesis& hypothesis)
{
    output << "{\"kind\":";
    writeQuoted(output, autotiming::toString(hypothesis.kind));
    output << ",\"averageObjectiveCost\":";
    writeNumber(output, hypothesis.averageObjectiveCost);
    output << ",\"selected\":" << (hypothesis.selected ? "true" : "false");
    output << ",\"track\":";
    writeTempoTrack(output, hypothesis.track);
    output << '}';
}

void writePeriodicityLayer(
    std::ostream& output,
    const autotiming::PeriodicityLayer& layer)
{
    output << "{\"startSeconds\":";
    writeNumber(output, layer.startSeconds);
    output << ",\"endSeconds\":";
    writeNumber(output, layer.endSeconds);
    output << ",\"observedRateBpm\":";
    writeNumber(output, layer.observedRateBpm);
    output << ",\"referenceTempoBpm\":";
    writeNumber(output, layer.referenceTempoBpm);
    output << ",\"relativeRate\":";
    writeNumber(output, layer.relativeRate);
    output << ",\"ratioNumerator\":" << layer.ratioNumerator;
    output << ",\"ratioDenominator\":" << layer.ratioDenominator;
    output << ",\"confidence\":";
    writeNumber(output, layer.confidence);
    output << ",\"relation\":";
    writeQuoted(output, autotiming::toString(layer.relation));
    output << ",\"supportingWindowIds\":";
    writeArray(output, layer.supportingWindowIds, [&](std::size_t id) {
        output << id;
    });
    output << '}';
}

void writeUncertainRegion(std::ostream& output, const autotiming::UncertainRegion& region)
{
    output << "{\"startSeconds\":";
    writeNumber(output, region.startSeconds);
    output << ",\"endSeconds\":";
    writeNumber(output, region.endSeconds);
    output << ",\"confidence\":";
    writeNumber(output, region.confidence);
    output << ",\"reason\":";
    writeQuoted(output, autotiming::toString(region.reason));
    output << '}';
}

void writeLocalCandidate(std::ostream& output, const autotiming::TempoCandidate& candidate)
{
    output << "{\"bpm\":";
    writeNumber(output, candidate.bpm);
    output << ",\"score\":";
    writeNumber(output, candidate.score);
    output << ",\"harmonicRatio\":";
    writeNumber(output, candidate.harmonicRatio);
    output << ",\"origin\":";
    switch (candidate.origin) {
    case autotiming::CandidateOrigin::LegacyWindowEstimate:
        writeQuoted(output, "legacy_window_estimate");
        break;
    case autotiming::CandidateOrigin::OnsetEnvelopePeriodicity:
        writeQuoted(output, "onset_envelope_periodicity");
        break;
    case autotiming::CandidateOrigin::HarmonicAlias:
        writeQuoted(output, "harmonic_alias");
        break;
    }
    output << ",\"pulseTimeSeconds\":";
    writeNumber(output, candidate.pulseTimeSeconds);
    output << ",\"rawBpm\":";
    writeNumber(output, candidate.rawBpm);
    output << ",\"rawBpmUncertainty\":";
    writeNumber(output, candidate.rawBpmUncertainty);
    output << ",\"phaseConfidence\":";
    writeNumber(output, candidate.phaseConfidence);
    output << '}';
}

void writeWindow(std::ostream& output, const autotiming::AnalysisWindow& window)
{
    output << "{\"id\":" << window.id << ",\"scale\":";
    writeQuoted(output, autotiming::toString(window.scale));
    output << ",\"startSeconds\":";
    writeNumber(output, window.startSeconds);
    output << ",\"endSeconds\":";
    writeNumber(output, window.endSeconds);
    output << ",\"reliability\":";
    writeNumber(output, window.reliability);
    output << ",\"tempoEvidence\":";
    writeNumber(output, window.tempoEvidence);
    output << ",\"legacyTempoEvidence\":";
    writeNumber(output, window.legacyTempoEvidence);
    output << ",\"onsetPeriodicityEvidence\":";
    writeNumber(output, window.onsetPeriodicityEvidence);
    output << ",\"crossScaleConsistency\":";
    writeNumber(output, window.crossScaleConsistency);
    output << ",\"consistencyPeerCount\":" << window.consistencyPeerCount;
    output << ",\"selectedAsAnchor\":" << (window.selectedAsAnchor ? "true" : "false");
    output << ",\"signal\":{\"rmsDb\":";
    writeNumber(output, window.signal.rmsDb);
    output << ",\"peakAmplitude\":";
    writeNumber(output, window.signal.peakAmplitude);
    output << ",\"silenceRatio\":";
    writeNumber(output, window.signal.silenceRatio);
    output << ",\"clippingRatio\":";
    writeNumber(output, window.signal.clippingRatio);
    output << ",\"dynamicRangeDb\":";
    writeNumber(output, window.signal.dynamicRangeDb);
    output << ",\"transientDensityHz\":";
    writeNumber(output, window.signal.transientDensityHz);
    output << ",\"signalScore\":";
    writeNumber(output, window.signal.signalScore);
    output << ",\"transientScore\":";
    writeNumber(output, window.signal.transientScore);
    output << "},\"reasons\":";
    writeArray(output, window.reasons, [&](autotiming::EvidenceReason reason) {
        writeQuoted(output, autotiming::toString(reason));
    });
    output << ",\"tempoCandidates\":";
    writeArray(output, window.tempoCandidates, [&](const autotiming::TempoCandidate& candidate) {
        writeLocalCandidate(output, candidate);
    });
    if (!window.estimatorMessage.empty()) {
        output << ",\"estimatorMessage\":";
        writeQuoted(output, window.estimatorMessage);
    }
    output << '}';
}

void writeResult(std::ostream& output, const autotiming::AnalysisResult& result)
{
    output << std::setprecision(10);
    output << "{\n  \"schemaVersion\": 1,\n  \"metadata\": {\"frameCount\":"
        << result.metadata.frameCount
        << ",\"sampleRate\":" << result.metadata.sampleRate
        << ",\"channels\":" << result.metadata.channels
        << ",\"durationSeconds\":";
    writeNumber(output, result.metadata.durationSeconds);
    output << "},\n  \"confidence\": {\"tempo\":";
    writeNumber(output, result.confidence.tempo);
    output << ",\"tempoFamily\":";
    writeNumber(output, result.confidence.tempoFamily);
    output << ",\"reliableCoverage\":";
    writeNumber(output, result.confidence.reliableCoverage);
    output << ",\"overall\":";
    writeNumber(output, result.confidence.overall);
    output << "},\n  \"tempoCandidates\": ";
    writeArray(output, result.tempoCandidates, [&](const autotiming::GlobalTempoCandidate& candidate) {
        writeTempoCandidate(output, candidate);
    });
    output << ",\n  \"tempoFamilies\": ";
    writeArray(output, result.tempoFamilies, [&](const autotiming::GlobalTempoFamily& family) {
        writeTempoFamily(output, family);
    });
    output << ",\n  \"tempoTrack\": ";
    writeTempoTrack(output, result.tempoTrack);
    output << ",\n  \"tempoHypotheses\": ";
    writeArray(
        output,
        result.tempoHypotheses,
        [&](const autotiming::TempoTrackHypothesis& hypothesis) {
            writeTempoHypothesis(output, hypothesis);
        });
    output << ",\n  \"periodicityLayers\": ";
    writeArray(output, result.periodicityLayers, [&](const autotiming::PeriodicityLayer& layer) {
        writePeriodicityLayer(output, layer);
    });
    output << ",\n  \"anchorRegions\": ";
    writeArray(output, result.diagnostics.anchorRegions, [&](const autotiming::AnchorRegion& region) {
        writeAnchor(output, region);
    });
    output << ",\n  \"uncertainRegions\": ";
    writeArray(output, result.uncertainRegions, [&](const autotiming::UncertainRegion& region) {
        writeUncertainRegion(output, region);
    });
    output << ",\n  \"windows\": ";
    writeArray(output, result.diagnostics.windows, [&](const autotiming::AnalysisWindow& window) {
        writeWindow(output, window);
    });
    output << "\n}\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string(argv[1]) == "--help") {
        printUsage(std::cout);
        return 0;
    }
    if (argc != 4) {
        printUsage(std::cerr);
        return 2;
    }

    try {
        const std::uint32_t sampleRate = parsePositiveInteger(argv[2], "sample rate");
        const std::uint32_t channels = parsePositiveInteger(argv[3], "channel count");
        const std::vector<float> samples = readFloatPcm(argv[1], channels);
        const autotiming::AudioView audio{
            samples.data(),
            samples.size() / channels,
            sampleRate,
            channels,
        };
        writeResult(std::cout, autotiming::analyze(audio));
    } catch (const std::exception& error) {
        std::cerr << "autotiming_probe: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
