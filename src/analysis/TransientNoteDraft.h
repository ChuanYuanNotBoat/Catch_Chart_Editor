#pragma once
#include "TransientAnalysis.h"
#include "model/Note.h"
#include "utils/MathUtils.h"

namespace analysis
{
struct TransientNoteDraftOptions
{
    double startSeconds = 0, endSeconds = 0;
    double minimumRelativeStrength = 0, maximumSnapErrorMs = 40;
    int timeDivision = 4, x = 256, dominantBand = 0; // 0=all, 1=low, 2=mid, 3=high
};
enum class NoteDraftError { None, InvalidInput, InvalidTiming, TooManyNotes };
struct TransientNoteDraft
{
    QVector<Note> notes;
    QVector<double> sourceSeconds, snapErrorsMs;
    int filtered = 0, offGrid = 0, occupied = 0, merged = 0;
    NoteDraftError error = NoteDraftError::None;
    bool valid() const { return error == NoteDraftError::None && !notes.isEmpty(); }
};
// A read-only proposal. Beat quantization uses the chart map (including offset),
// never a diagnostic tempo model. Only the caller may commit the returned notes.
TransientNoteDraft buildTransientNoteDraft(const TransientResult &, const TransientNoteDraftOptions &,
    const QVector<MathUtils::BpmCacheEntry> &, const QVector<Note> &existing);
}
