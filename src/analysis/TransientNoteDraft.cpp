#include "TransientNoteDraft.h"
#include "MusicalTimeTransform.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <limits>

namespace analysis
{
TransientNoteDraft buildTransientNoteDraft(const TransientResult &result, const TransientNoteDraftOptions &options,
    const QVector<MathUtils::BpmCacheEntry> &timing, const QVector<Note> &existing)
{
    TransientNoteDraft out;
    if (!result.valid() || result.peaks.size() > 12000 || !std::isfinite(options.startSeconds)
        || !std::isfinite(options.endSeconds) || options.startSeconds < 0 || options.endSeconds <= options.startSeconds
        || options.endSeconds - options.startSeconds > 120 || options.endSeconds > 86400
        || !std::isfinite(options.minimumRelativeStrength) || options.minimumRelativeStrength < 0
        || options.minimumRelativeStrength > 1 || !std::isfinite(options.maximumSnapErrorMs)
        || options.maximumSnapErrorMs < 0 || options.maximumSnapErrorMs > 1000
        || options.timeDivision < 1 || options.timeDivision > 96 || options.x < 0 || options.x > 512
        || options.dominantBand < 0 || options.dominantBand > 3)
    {
        out.error = NoteDraftError::InvalidInput;
        return out;
    }
    if (timing.isEmpty())
        out.error = NoteDraftError::InvalidTiming;
    for (int i = 0; i < timing.size(); ++i)
    {
        const auto &t = timing[i];
        if (!std::isfinite(t.beatPos) || !std::isfinite(t.accumulatedMs) || !std::isfinite(t.bpm) || t.bpm <= 0
            || (i && (t.beatPos <= timing[i - 1].beatPos || t.accumulatedMs <= timing[i - 1].accumulatedMs)))
            out.error = NoteDraftError::InvalidTiming;
    }
    if (out.error != NoteDraftError::None)
        return out;
    MusicalTimeTransform transform;
    transform.setChart(timing);
    std::set<BeatPosition> occupied;
    for (const auto &note : existing)
        if (!note.isSound() && note.startPosition().isValid())
            occupied.insert(note.startPosition());
    struct Candidate { Note note; double seconds, error; float strength; };
    std::map<BeatPosition, Candidate> candidates;
    float maximum = 0;
    for (const auto &peak : result.peaks)
    {
        if (!std::isfinite(peak.timeSeconds) || !std::isfinite(peak.strength) || peak.strength < 0)
        { out.error = NoteDraftError::InvalidInput; return out; }
        for (float band : peak.bands)
            if (!std::isfinite(band) || band < 0)
            { out.error = NoteDraftError::InvalidInput; return out; }
        if (peak.disposition == PeakDisposition::Detected && peak.timeSeconds >= options.startSeconds
            && peak.timeSeconds < options.endSeconds)
            maximum = std::max(maximum, peak.strength);
    }
    for (const auto &peak : result.peaks)
    {
        if (peak.disposition != PeakDisposition::Detected || peak.timeSeconds < options.startSeconds
            || peak.timeSeconds >= options.endSeconds)
            continue;
        if (peak.strength < maximum * options.minimumRelativeStrength
            || (options.dominantBand && peak.bands[options.dominantBand - 1]
                    < *std::max_element(peak.bands.begin(), peak.bands.end())))
        { ++out.filtered; continue; }
        const double beat = transform.beatAtAudio(peak.timeSeconds * 1000);
        int whole = 0, numerator = 0, denominator = 1;
        if (!std::isfinite(beat) || beat < 0 || beat > std::numeric_limits<int>::max() - 1.
            || !MathUtils::quantizeBeatToDivision(beat, options.timeDivision, whole, numerator, denominator))
        { ++out.offGrid; continue; }
        Note note(whole, numerator, denominator, options.x);
        const double snapped = transform.audioAtBeat(note.getStartBeat()) / 1000;
        const double error = (snapped - peak.timeSeconds) * 1000;
        if (snapped < options.startSeconds || snapped >= options.endSeconds || std::abs(error) > options.maximumSnapErrorMs + 1e-7)
        { ++out.offGrid; continue; }
        const auto position = note.startPosition();
        if (occupied.count(position))
        { ++out.occupied; continue; }
        auto found = candidates.find(position);
        if (found != candidates.end())
        {
            ++out.merged;
            if (found->second.strength >= peak.strength)
                continue;
        }
        candidates.insert_or_assign(position, Candidate{std::move(note), peak.timeSeconds, error, peak.strength});
        if (candidates.size() > 4096)
        { out.error = NoteDraftError::TooManyNotes; return out; }
    }
    for (auto &entry : candidates)
    {
        out.notes.append(std::move(entry.second.note));
        out.sourceSeconds.append(entry.second.seconds);
        out.snapErrorsMs.append(entry.second.error);
    }
    return out;
}
}
