// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace autotiming::detail {

struct PeriodicityEstimate {
    double bpm = 0.0;
    double score = 0.0;
    double uncertaintyBpm = 0.0;
    double phaseSeconds = 0.0;
    double phaseConfidence = 0.0;
};

std::vector<PeriodicityEstimate> estimateOnsetPeriodicity(
    const float* monoSamples,
    std::size_t frameCount,
    std::uint32_t sampleRate,
    double minimumBpm,
    double maximumBpm,
    double minimumScore,
    std::size_t maximumCandidates);

} // namespace autotiming::detail
