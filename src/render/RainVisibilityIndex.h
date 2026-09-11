#pragma once

#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace RainVisibilityIndex
{
inline QVector<double> buildPrefixMaxEndBeats(const QVector<int> &indicesByStartBeat,
                                               const QVector<double> &endBeats)
{
    QVector<double> prefix;
    prefix.reserve(indicesByStartBeat.size());
    double maximum = -std::numeric_limits<double>::infinity();
    for (int index : indicesByStartBeat)
    {
        if (index >= 0 && index < endBeats.size())
            maximum = std::max(maximum, endBeats[index]);
        prefix.append(maximum);
    }
    return prefix;
}

// firstStartingAtOrAfter is the lower-bound position in the start-sorted rain
// list. The returned position includes every earlier interval whose end can
// overlap startBeat, even when end beats are not monotonic.
inline qsizetype firstPotentiallyVisible(const QVector<double> &prefixMaxEndBeats,
                                         qsizetype firstStartingAtOrAfter,
                                         double startBeat)
{
    const qsizetype boundary = qBound<qsizetype>(0,
                                                 firstStartingAtOrAfter,
                                                 prefixMaxEndBeats.size());
    const auto begin = prefixMaxEndBeats.cbegin();
    const auto earlierEnd = begin + boundary;
    const auto firstOverlap = std::upper_bound(begin, earlierEnd, startBeat);
    return firstOverlap == earlierEnd ? boundary : std::distance(begin, firstOverlap);
}

struct IntervalRange
{
    qsizetype begin = 0;
    qsizetype end = 0;

    bool isEmpty() const noexcept { return begin >= end; }
};

// A start-sorted interval index with a prefix maximum of interval ends. The
// prefix lets a range query skip directly over short intervals that end before
// the query starts, while still finding an earlier long interval when ends are
// not monotonic.
class IntervalIndex
{
public:
    struct Entry
    {
        int index = -1;
        double start = 0.0;
        double end = 0.0;
    };

    void clear()
    {
        m_entries.clear();
        m_prefixMaxEnd.clear();
    }

    bool isEmpty() const noexcept { return m_entries.isEmpty(); }
    qsizetype size() const noexcept { return m_entries.size(); }

    const Entry &entryAt(qsizetype position) const { return m_entries.at(position); }

    void build(const QVector<int> &indices,
               const QVector<double> &starts,
               const QVector<double> &ends)
    {
        clear();
        m_entries.reserve(indices.size());
        for (int index : indices)
        {
            if (index < 0 || index >= starts.size() || index >= ends.size())
                continue;

            const double start = starts[index];
            const double end = ends[index];
            if (!std::isfinite(start) || !std::isfinite(end))
                continue;

            m_entries.append(Entry{index, start, std::max(start, end)});
        }

        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &left, const Entry &right) {
            if (left.start != right.start)
                return left.start < right.start;
            return left.index < right.index;
        });

        m_prefixMaxEnd.reserve(m_entries.size());
        double maximum = -std::numeric_limits<double>::infinity();
        for (const Entry &entry : m_entries)
        {
            maximum = std::max(maximum, entry.end);
            m_prefixMaxEnd.append(maximum);
        }
    }

    IntervalRange overlapping(double start, double end) const
    {
        if (m_entries.isEmpty() || !std::isfinite(start) || !std::isfinite(end))
            return {};
        if (end < start)
            std::swap(start, end);

        const auto lower = std::lower_bound(
            m_entries.cbegin(), m_entries.cend(), start,
            [](const Entry &entry, double value) { return entry.start < value; });
        const auto upper = std::upper_bound(
            m_entries.cbegin(), m_entries.cend(), end,
            [](double value, const Entry &entry) { return value < entry.start; });
        const qsizetype lowerPosition = std::distance(m_entries.cbegin(), lower);
        const qsizetype first = firstPotentiallyVisible(
            m_prefixMaxEnd, lowerPosition, start);
        return {first, std::distance(m_entries.cbegin(), upper)};
    }

    IntervalRange containing(double value) const { return overlapping(value, value); }

private:
    QVector<Entry> m_entries;
    QVector<double> m_prefixMaxEnd;
};
} // namespace RainVisibilityIndex
