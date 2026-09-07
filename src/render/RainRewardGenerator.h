#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>

#include "model/BpmEntry.h"
#include "model/Note.h"

// One generated Catch reward note. The game stores the horizontal value as
// an integer in [0, 512]; xRatio is the renderer-friendly rawX / 512 value.
struct RainDrop
{
    double beatOffset = 0.0;
    double xRatio = 0.0;
    std::uint32_t rawX = 0;
};

class RainRewardGenerator
{
public:
    static RainRewardGenerator &instance();

    RainRewardGenerator(const RainRewardGenerator &) = delete;
    RainRewardGenerator &operator=(const RainRewardGenerator &) = delete;

    // Mark the chart cache stale. The next ensureChart() starts the official
    // deterministic stream again for the Catch gameplay-note count.
    void invalidate();

    void ensureChart(const QVector<Note> &notes,
                     const QVector<BpmEntry> &bpmList,
                     int offsetMs = 0);

    QVector<RainDrop> dropsFor(const Note &rain) const;

    // These helpers expose the small integer core of 00469F40/004C33F6 for
    // regression tests. All arithmetic is deliberately modulo 2^32.
    static std::uint32_t seedForNoteCount(qsizetype totalInputNoteCount);
    static std::uint32_t rotateRight(std::uint32_t value, unsigned bits);
    static void initializeState(std::uint32_t seed, std::uint32_t state[4]);
    static void advanceState(std::uint32_t state[4]);
    static std::uint32_t rawXFromState(const std::uint32_t state[4]);

private:
    RainRewardGenerator() = default;

    void rebuild(const QVector<Note> &notes);
    QString cacheKey(const Note &rain) const;
    void fillDrops(const Note &rain, QVector<RainDrop> &out);
    static std::uint32_t advanceRainState(std::uint32_t state[4]);

    std::uint32_t m_state[4] = {};
    bool m_dirty = true;
    const Note *m_lastNotesData = nullptr;
    qsizetype m_lastNotesSize = -1;
    const BpmEntry *m_lastBpmData = nullptr;
    qsizetype m_lastBpmSize = -1;
    std::uint64_t m_lastNotesFingerprint = 0;
    std::uint64_t m_lastBpmFingerprint = 0;
    int m_lastOffsetMs = 0;
    QVector<BpmEntry> m_bpmList;
    int m_offsetMs = 0;
    QHash<QString, QVector<RainDrop>> m_cache;
};
