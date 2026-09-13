// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#pragma once

#include "autotiming/Analysis.h"

#include <vector>

namespace autotiming::detail {

std::vector<PeriodicityLayer> buildPeriodicityLayers(
    const std::vector<AnalysisWindow>& windows,
    const TempoTrack& tempoTrack,
    const AnalysisOptions& options);

} // namespace autotiming::detail
