#include "HyperfruitDetector.h"
#include "utils/MathUtils.h"
#include <algorithm>
#include <cmath>
#include <limits>

HyperfruitDetector::HyperfruitDetector() : m_cs(3.8)
{
}

void HyperfruitDetector::setCS(double cs)
{
    m_cs = cs;
}

double HyperfruitDetector::catcherWidth() const
{
    return 106.75 * (1.0 - 0.14 * (m_cs - 5));
}

QSet<int> HyperfruitDetector::detect(const QVector<Note> &notes,
                                     const QVector<BpmEntry> &bpmList,
                                     int offset) const
{
    QSet<int> hyperSet;
    if (notes.size() < 2 || bpmList.isEmpty())
        return hyperSet;

    const QVector<MathUtils::BpmCacheEntry> bpmCache = MathUtils::buildBpmTimeCache(bpmList, offset);
    if (bpmCache.isEmpty())
        return hyperSet;

    const double halfCatcherWidth = catcherWidth() / 2.0;
    const double graceTimeMs = 1000.0 / 60.0 / 4.0;

    int timelineDirection = 0;
    double timelineExcess = halfCatcherWidth;

    for (int i = 0; i < notes.size() - 1;)
    {
        if (notes[i].type != NoteType::NORMAL)
        {
            ++i;
            continue;
        }

        const double groupMs = MathUtils::beatToMs(notes[i].beatNum, notes[i].numerator, notes[i].denominator, bpmCache);
        const int groupStart = i;
        int groupEnd = i;

        while (groupEnd + 1 < notes.size())
        {
            const Note &nextInGroup = notes[groupEnd + 1];
            if (nextInGroup.type != NoteType::NORMAL)
                break;
            const double nextInGroupMs = MathUtils::beatToMs(nextInGroup.beatNum, nextInGroup.numerator, nextInGroup.denominator, bpmCache);
            if (nextInGroupMs != groupMs)
                break;
            ++groupEnd;
        }

        // Keep the same incoming state for all notes in this time group.
        const int groupDirection = timelineDirection;
        const double groupExcess = timelineExcess;

        bool hasStateUpdate = false;
        int newTimelineDirection = timelineDirection;
        double newTimelineExcess = timelineExcess;

        for (int k = groupStart; k <= groupEnd; ++k)
        {
            const Note &currentNote = notes[k];
            const double currentMs = MathUtils::beatToMs(currentNote.beatNum, currentNote.numerator, currentNote.denominator, bpmCache);

            int nextIdx = k + 1;
            while (nextIdx < notes.size())
            {
                if (notes[nextIdx].type != NoteType::NORMAL)
                {
                    ++nextIdx;
                    continue;
                }

                const Note &candidate = notes[nextIdx];
                const double candidateMs = MathUtils::beatToMs(candidate.beatNum, candidate.numerator, candidate.denominator, bpmCache);
                if (candidateMs > currentMs)
                    break;

                ++nextIdx;
            }

            if (nextIdx >= notes.size())
                continue;

            const Note &nextNote = notes[nextIdx];
            const double nextMs = MathUtils::beatToMs(nextNote.beatNum, nextNote.numerator, nextNote.denominator, bpmCache);
            const double dt = nextMs - currentMs;
            if (dt <= 0.0)
                continue;

            const int thisDirection = nextNote.x > currentNote.x ? 1 : -1;
            const double timeToNext = static_cast<int>(nextMs) - static_cast<int>(currentMs) - graceTimeMs;
            const double distanceToNext = std::abs(nextNote.x - currentNote.x) - (groupDirection == thisDirection ? groupExcess : halfCatcherWidth);
            const double distanceToHyper = timeToNext - distanceToNext;

            if (timeToNext < distanceToNext)
            {
                hyperSet.insert(k);
                if (!hasStateUpdate)
                {
                    hasStateUpdate = true;
                    newTimelineDirection = thisDirection;
                    newTimelineExcess = halfCatcherWidth;
                }
            }
            else
            {
                if (!hasStateUpdate)
                {
                    hasStateUpdate = true;
                    newTimelineDirection = thisDirection;
                    newTimelineExcess = std::clamp(distanceToHyper, 0.0, halfCatcherWidth);
                }
            }
        }

        if (hasStateUpdate)
        {
            timelineDirection = newTimelineDirection;
            timelineExcess = newTimelineExcess;
        }

        i = groupEnd + 1;
    }

    return hyperSet;
}
