// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#pragma once

#include "autotiming/Analysis.h"

#include <vector>

namespace autotiming::detail {

struct TempoTrackingOutput {
    TempoTrack selectedTrack;
    std::vector<TempoTrackHypothesis> hypotheses;
};

TempoTrackingOutput buildTempoTracks(
    const std::vector<AnalysisWindow>& windows,
    const std::vector<GlobalTempoFamily>& families,
    double audioDurationSeconds,
    const AnalysisOptions& options);

} // namespace autotiming::detail
