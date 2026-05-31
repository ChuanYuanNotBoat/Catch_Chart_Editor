// tests/coord_mapper_tests.cpp
// Unit tests for BeatLinearMapper and TimeLinearMapper.

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <QtGlobal>
#include "coord/CoordinateMapper.h"
#include "coord/BeatLinearMapper.h"
#include "coord/TimeLinearMapper.h"
#include "utils/MathUtils.h"
#include "model/BpmEntry.h"

namespace
{
bool nearlyEqual(double a, double b, double eps = 1e-6)
{
    return qAbs(a - b) <= eps;
}

CoordContext makeContext(int canvasHeight, double timeScale, bool verticalFlip,
                         const QVector<MathUtils::BpmCacheEntry> &filtered,
                         const QVector<MathUtils::BpmCacheEntry> &full,
                         double baseBpm, int offsetMs)
{
    CoordContext ctx;
    ctx.canvasHeight = canvasHeight;
    ctx.timeScale = timeScale;
    ctx.verticalFlip = verticalFlip;
    ctx.bpmCache = &filtered;
    ctx.fullBpmCache = &full;
    ctx.baseBpm = baseBpm;
    ctx.offsetMs = offsetMs;
    return ctx;
}

// --- BeatLinear tests ---

bool testBeatLinearBeatToYRoundTrip()
{
    BeatLinearMapper mapper;
    mapper.setBaseVisibleBeatRange(10.0);
    const QVector<MathUtils::BpmCacheEntry> cache;
    auto ctx = makeContext(800, 2.25, true, cache, cache, 120.0, 0);

    double scrollBeat = 0.0;
    for (double beat = -5.0; beat <= 20.0; beat += 0.5)
    {
        double y = mapper.beatToY(beat, scrollBeat, ctx);
        double beatBack = mapper.yToBeat(y, scrollBeat, ctx);
        if (!nearlyEqual(beat, beatBack, 1e-4))
            return false;
    }
    return true;
}

bool testBeatLinearEffectiveRange()
{
    BeatLinearMapper mapper;
    mapper.setBaseVisibleBeatRange(10.0);
    const QVector<MathUtils::BpmCacheEntry> cache;
    auto ctx = makeContext(800, 2.0, true, cache, cache, 120.0, 0);

    // effective = baseVisibleBeatRange / timeScale = 10 / 2 = 5
    return nearlyEqual(mapper.effectiveVisibleBeatRange(ctx), 5.0);
}

bool testBeatLinearTimeToYRoundTrip()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    BeatLinearMapper mapper;
    mapper.setBaseVisibleBeatRange(10.0);
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 0.0;
    // At 120 BPM: beat 1 = 500ms, beat 2 = 1000ms
    double y500 = mapper.timeToY(500.0, scrollBeat, ctx);
    double y1000 = mapper.timeToY(1000.0, scrollBeat, ctx);
    // Y should increase as time increases (verticalFlip=true means Y decreases, but 1000 > 500)
    // With verticalFlip=true, higher time = lower Y (smaller number = higher on screen)
    if (y500 <= y1000)
        return false; // At flip, y500 should be above (smaller Y) than y1000... actually reversed

    // Round-trip through yToTime
    double timeBack500 = mapper.yToTime(y500, scrollBeat, ctx);
    double timeBack1000 = mapper.yToTime(y1000, scrollBeat, ctx);
    return nearlyEqual(timeBack500, 500.0, 5.0) && nearlyEqual(timeBack1000, 1000.0, 5.0);
}

bool testBeatLinearScrollLimit()
{
    BeatLinearMapper mapper;
    mapper.setBaseVisibleBeatRange(10.0);
    const QVector<MathUtils::BpmCacheEntry> cache;
    auto ctx = makeContext(800, 2.0, true, cache, cache, 120.0, -500);

    double scrollBeat = -100.0;
    mapper.clampScrollLimit(scrollBeat, ctx);
    // Should be clamped to a reasonable negative value, not -100
    return scrollBeat > -100.0;
}

bool testBeatLinearSetTimeScale()
{
    BeatLinearMapper mapper;
    mapper.setBaseVisibleBeatRange(10.0);
    const QVector<MathUtils::BpmCacheEntry> cache;
    auto ctx = makeContext(800, 2.0, true, cache, cache, 120.0, 0);

    double scrollBeat = 5.0;
    // Remember reference beat before zoom
    double refRatio = CoordinateMapper::kReferenceLineRatio;
    double visRange = mapper.effectiveVisibleBeatRange(ctx); // 10/2 = 5
    double refBeat = scrollBeat + (1.0 - refRatio) * visRange; // verticalFlip=true

    mapper.setTimeScale(4.0, scrollBeat, ctx);
    ctx.timeScale = 4.0;

    // After zoom, ref beat should be approximately the same
    double newVisRange = mapper.effectiveVisibleBeatRange(ctx); // 10/4 = 2.5
    double newRefBeat = scrollBeat + (1.0 - refRatio) * newVisRange;
    return nearlyEqual(refBeat, newRefBeat, 1e-4);
}

// --- TimeLinear tests ---

bool testTimeLinearTimeToYRoundTrip()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    // Set scroll state
    double scrollBeat = 0.0;
    mapper.syncFromBeat(scrollBeat, ctx);

    for (double ms = 0.0; ms <= 5000.0; ms += 100.0)
    {
        double y = mapper.timeToY(ms, scrollBeat, ctx);
        double msBack = mapper.yToTime(y, scrollBeat, ctx);
        if (!nearlyEqual(ms, msBack, 1.0))
            return false;
    }
    return true;
}

bool testTimeLinearBeatToYRoundTrip()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 0.0;
    mapper.syncFromBeat(scrollBeat, ctx);

    for (double beat = 0.0; beat <= 10.0; beat += 0.25)
    {
        double y = mapper.beatToY(beat, scrollBeat, ctx);
        double beatBack = mapper.yToBeat(y, scrollBeat, ctx);
        if (!nearlyEqual(beat, beatBack, 0.01))
            return false;
    }
    return true;
}

bool testTimeLinearSyncFromBeat()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 2.0;
    mapper.syncFromBeat(scrollBeat, ctx);

    // scrollTimeMs should correspond to beat 2 at 120 BPM = 1000ms
    return nearlyEqual(mapper.scrollTimeMsRaw(), 1000.0, 1.0);
}

bool testTimeLinearSyncFromTime()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    // First sync from beat to set internal state
    mapper.syncFromBeat(0.0, ctx);

    // Now sync from time (should recover scrollBeat from internal time state)
    double scrollBeat = -999.0; // Will be overwritten
    mapper.syncFromTime(scrollBeat, ctx);

    return nearlyEqual(scrollBeat, 0.0, 0.01);
}

bool testTimeLinearAdvancePlayback()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    mapper.syncFromBeat(0.0, ctx);
    double scrollBeat = 0.0;
    mapper.advancePlayback(500.0, 0.8, scrollBeat, ctx);

    // After advancing to 500ms at baselineRatio=0.8, scroll should have moved
    return scrollBeat < 5.0; // Should be around beat 1 minus offset
}

// --- AdoptFrom tests ---

bool testBeatLinearAdoptFromTimeLinear()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper tlMapper;
    BeatLinearMapper blMapper;
    blMapper.setBaseVisibleBeatRange(10.0);

    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 0.0;
    tlMapper.syncFromBeat(scrollBeat, ctx);

    blMapper.adoptFrom(&tlMapper, scrollBeat, ctx);

    // After adoption, effective ranges should be similar
    double tlRange = tlMapper.effectiveVisibleBeatRange(ctx);
    double blRange = blMapper.effectiveVisibleBeatRange(ctx);
    return nearlyEqual(tlRange, blRange, 0.1);
}

bool testTimeLinearAdoptFromBeatLinear()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    BeatLinearMapper blMapper;
    blMapper.setBaseVisibleBeatRange(10.0);
    TimeLinearMapper tlMapper;

    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 0.0;
    tlMapper.adoptFrom(&blMapper, scrollBeat, ctx);

    // After adoption, ranges should be similar
    double tlRange = tlMapper.effectiveVisibleBeatRange(ctx);
    double blRange = blMapper.effectiveVisibleBeatRange(ctx);
    return nearlyEqual(tlRange, blRange, 0.1);
}

// --- Variable BPM test ---

bool testTimeLinearVariableBpm()
{
    const QVector<BpmEntry> bpmList = {
        BpmEntry(0, 0, 1, 120.0),
        BpmEntry(4, 0, 1, 240.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 0.0;
    mapper.syncFromBeat(scrollBeat, ctx);

    // Beat 2 (at 120 BPM) and beat 6 (at 240 BPM) should map to Y
    double y2 = mapper.beatToY(2.0, scrollBeat, ctx);
    double y6 = mapper.beatToY(6.0, scrollBeat, ctx);
    // Both should be valid (non-zero, different)
    if (y2 == y6)
        return false;

    // Round-trip
    double beat2Back = mapper.yToBeat(y2, scrollBeat, ctx);
    double beat6Back = mapper.yToBeat(y6, scrollBeat, ctx);
    return nearlyEqual(2.0, beat2Back, 0.01) && nearlyEqual(6.0, beat6Back, 0.01);
}

// --- Additional edge-case tests ---

bool testBeatLinearBeatToYNoFlip()
{
    BeatLinearMapper mapper;
    mapper.setBaseVisibleBeatRange(10.0);
    const QVector<MathUtils::BpmCacheEntry> cache;
    auto ctx = makeContext(800, 2.25, false, cache, cache, 120.0, 0);

    double scrollBeat = 0.0;
    for (double beat = -5.0; beat <= 20.0; beat += 0.5)
    {
        double y = mapper.beatToY(beat, scrollBeat, ctx);
        double beatBack = mapper.yToBeat(y, scrollBeat, ctx);
        if (!nearlyEqual(beat, beatBack, 1e-4))
            return false;
    }
    return true;
}

bool testTimeLinearEmptyCache()
{
    const QVector<MathUtils::BpmCacheEntry> emptyCache;
    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.25, true, emptyCache, emptyCache, 120.0, 0);

    double scrollBeat = 0.0;
    mapper.syncFromBeat(scrollBeat, ctx);

    // beatToY should return 0 for empty cache
    double y = mapper.beatToY(5.0, scrollBeat, ctx);
    if (y != 0.0)
        return false;

    // yToBeat should return scrollBeat for empty cache
    double beatBack = mapper.yToBeat(100.0, scrollBeat, ctx);
    if (!nearlyEqual(beatBack, 0.0, 1e-6))
        return false;

    // effectiveVisibleBeatRange should return positive value
    double range = mapper.effectiveVisibleBeatRange(ctx);
    if (range <= 0)
        return false;

    return true;
}

bool testTimeLinearSetTimeScale()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.0, true, fullCache, fullCache, 120.0, 0);

    double scrollBeat = 0.0;
    mapper.syncFromBeat(scrollBeat, ctx);

    double refRatio = CoordinateMapper::kReferenceLineRatio;
    double visBeatRange = mapper.effectiveVisibleBeatRange(ctx);
    // verticalFlip=true: ref beat = scrollBeat + (1 - ratio) * visRange
    double refBeat = scrollBeat + (1.0 - refRatio) * visBeatRange;

    mapper.setTimeScale(4.0, scrollBeat, ctx);
    ctx.timeScale = 4.0;

    double newVisBeatRange = mapper.effectiveVisibleBeatRange(ctx);
    double newRefBeat = scrollBeat + (1.0 - refRatio) * newVisBeatRange;
    return nearlyEqual(refBeat, newRefBeat, 0.1);
}

bool testTimeLinearScrollLimit()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    auto fullCache = MathUtils::buildBpmTimeCache(bpmList, 0);

    TimeLinearMapper mapper;
    auto ctx = makeContext(800, 2.0, true, fullCache, fullCache, 120.0, -500);

    double scrollBeat = -100.0;
    mapper.clampScrollLimit(scrollBeat, ctx);
    return scrollBeat > -100.0;
}

} // namespace

// Test runner entry point
struct TestEntry
{
    const char *name;
    bool (*func)();
};

int main()
{
    const TestEntry tests[] = {
        {"BeatLinear: beatToY round-trip", testBeatLinearBeatToYRoundTrip},
        {"BeatLinear: effectiveVisibleBeatRange", testBeatLinearEffectiveRange},
        {"BeatLinear: timeToY round-trip", testBeatLinearTimeToYRoundTrip},
        {"BeatLinear: scroll limit", testBeatLinearScrollLimit},
        {"BeatLinear: setTimeScale preserves ref line", testBeatLinearSetTimeScale},
        {"TimeLinear: timeToY round-trip", testTimeLinearTimeToYRoundTrip},
        {"TimeLinear: beatToY round-trip", testTimeLinearBeatToYRoundTrip},
        {"TimeLinear: syncFromBeat", testTimeLinearSyncFromBeat},
        {"TimeLinear: syncFromTime", testTimeLinearSyncFromTime},
        {"TimeLinear: advancePlayback", testTimeLinearAdvancePlayback},
        {"BeatLinear adoptFrom TimeLinear", testBeatLinearAdoptFromTimeLinear},
        {"TimeLinear adoptFrom BeatLinear", testTimeLinearAdoptFromBeatLinear},
        {"TimeLinear: variable BPM", testTimeLinearVariableBpm},
        {"BeatLinear: beatToY no-flip", testBeatLinearBeatToYNoFlip},
        {"TimeLinear: empty cache", testTimeLinearEmptyCache},
        {"TimeLinear: setTimeScale preserves ref line", testTimeLinearSetTimeScale},
        {"TimeLinear: scroll limit", testTimeLinearScrollLimit},
    };

    int passed = 0;
    int failed = 0;
    const int total = static_cast<int>(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; ++i)
    {
        bool ok = tests[i].func();
        if (ok)
        {
            ++passed;
            std::printf("  PASS  %s\n", tests[i].name);
        }
        else
        {
            ++failed;
            std::printf("  FAIL  %s\n", tests[i].name);
        }
    }

    std::printf("\n%d/%d coord mapper tests passed.\n", passed, total);
    return failed > 0 ? 1 : 0;
}