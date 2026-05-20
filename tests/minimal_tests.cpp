#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtGlobal>
#include <algorithm>
#include <cstdio>

#include "file/ProjectIO.h"
#include "file/ChartIO.h"
#include "file/BpmAuxFiles.h"
#include "file/ChartFileSystem.h"
#include "controller/ChartController.h"
#include "model/Chart.h"
#include "utils/MathUtils.h"

namespace
{
bool nearlyEqual(double a, double b, double eps = 1e-6)
{
    return qAbs(a - b) <= eps;
}

Note makeNormalNote(int beatNum, int num, int den, int x, const QString &id);
bool hasBpmEntry(const QVector<BpmEntry> &list, int beatNum, int num, int den, double bpm);

bool testMathUtilsRoundTrip()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};

    const double ms = MathUtils::beatToMs(1, 0, 1, bpmList, 0);
    if (!nearlyEqual(ms, 500.0))
        return false;

    int beatNum = 0;
    int num = 0;
    int den = 1;
    MathUtils::msToBeat(750.0, bpmList, 0, beatNum, num, den);

    const double beat = MathUtils::beatToFloat(beatNum, num, den);
    if (!nearlyEqual(beat, 1.5))
        return false;

    MathUtils::floatToBeat(3.125, beatNum, num, den, 1024);
    return beatNum == 3 && num == 1 && den == 8;
}

bool testMathUtilsCacheConsistency()
{
    const QVector<BpmEntry> bpmList = {
        BpmEntry(0, 0, 1, 120.0),
        BpmEntry(4, 0, 1, 180.0),
        BpmEntry(8, 0, 1, 90.0)};

    const auto cache = MathUtils::buildBpmTimeCache(bpmList, 120);
    if (cache.size() != 3)
        return false;

    struct Probe
    {
        int beatNum;
        int num;
        int den;
    };

    const Probe probes[] = {
        {0, 0, 1},
        {3, 1, 2},
        {5, 0, 1},
        {9, 0, 1}};

    for (const Probe &p : probes)
    {
        const double byList = MathUtils::beatToMs(p.beatNum, p.num, p.den, bpmList, 120);
        const double byCache = MathUtils::beatToMs(p.beatNum, p.num, p.den, cache);
        if (!nearlyEqual(byList, byCache))
            return false;
    }
    return true;
}

bool testMathUtilsEmptyBpmBoundary()
{
    const QVector<BpmEntry> bpmList;
    if (!nearlyEqual(MathUtils::beatToMs(2, 0, 1, bpmList, 1234), -1234.0))
        return false;

    int beatNum = -1;
    int num = -1;
    int den = -1;
    MathUtils::msToBeat(500.0, bpmList, 0, beatNum, num, den);
    return beatNum == 0 && num == 1 && den == 1;
}

bool testMathUtilsZeroBpmBoundary()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 0.0)};
    if (!nearlyEqual(MathUtils::beatToMs(8, 0, 1, bpmList, 250), -250.0))
        return false;

    int beatNum = -1;
    int num = -1;
    int den = -1;
    MathUtils::msToBeat(2000.0, bpmList, 0, beatNum, num, den);
    return beatNum == 0 && num == 0 && den == 1;
}

bool testMathUtilsExtremeOffsetBoundary()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    const int positiveOffset = 1000000;
    const int negativeOffset = -1000000;

    if (!nearlyEqual(MathUtils::beatToMs(0, 0, 1, bpmList, positiveOffset), -1000000.0))
        return false;
    if (!nearlyEqual(MathUtils::beatToMs(0, 0, 1, bpmList, negativeOffset), 1000000.0))
        return false;

    int beatNum = -1;
    int num = -1;
    int den = -1;
    MathUtils::msToBeat(-1000001.0, bpmList, positiveOffset, beatNum, num, den);
    return beatNum == 0 && num == 0 && den == 1;
}

bool testMathUtilsCrossSegmentRoundTripBoundary()
{
    const QVector<BpmEntry> bpmList = {
        BpmEntry(0, 0, 1, 120.0),
        BpmEntry(4, 0, 1, 240.0)};

    const struct BeatProbe
    {
        int beatNum;
        int num;
        int den;
    } probes[] = {
        {3, 999, 1000},
        {4, 1, 1000},
        {7, 1, 2},
    };

    for (const BeatProbe &probe : probes)
    {
        const double beatIn = MathUtils::beatToFloat(probe.beatNum, probe.num, probe.den);
        const double ms = MathUtils::beatToMs(probe.beatNum, probe.num, probe.den, bpmList, 0);

        int outBeatNum = 0;
        int outNum = 0;
        int outDen = 1;
        MathUtils::msToBeat(ms, bpmList, 0, outBeatNum, outNum, outDen);
        const double beatOut = MathUtils::beatToFloat(outBeatNum, outNum, outDen);
        if (!nearlyEqual(beatIn, beatOut, 1e-4))
            return false;
    }
    return true;
}

bool testChartRemoveById()
{
    Chart chart;
    chart.clearNotes();

    Note first(1, 0, 1, 128);
    first.id = "n1";
    Note second(1, 0, 1, 128);
    second.id = "n2";

    chart.addNote(first);
    chart.addNote(second);

    Note removeTarget = second;
    chart.removeNote(removeTarget);
    if (chart.notes().size() != 1)
        return false;
    return chart.notes().first().id == "n1";
}

bool testChartBpmSort()
{
    Chart chart;
    chart.bpmList().clear();
    chart.addBpm(BpmEntry(8, 0, 1, 180.0));
    chart.addBpm(BpmEntry(0, 0, 1, 120.0));
    chart.addBpm(BpmEntry(4, 1, 2, 150.0));

    if (chart.bpmList().size() != 3)
        return false;
    if (chart.bpmList()[0].beatNum != 0)
        return false;
    if (chart.bpmList()[1].beatNum != 4 || chart.bpmList()[1].numerator != 1)
        return false;
    return chart.bpmList()[2].beatNum == 8;
}

bool writeTextFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(content) == content.size();
}

bool loadChartFromJsonContent(const QByteArray &content, Chart &outChart)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString chartPath = tempDir.path() + "/chart.mc";
    if (!writeTextFile(chartPath, content))
        return false;

    return ChartIO::load(chartPath, outChart, false);
}

bool testProjectIoReadDifficultyAndScan()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString root = tempDir.path();
    const QString nestedDir = root + "/nested";
    if (!QDir().mkpath(nestedDir))
        return false;

    const QString hardMc = root + "/hard.mc";
    const QByteArray hardJson = R"({"meta":{"version":"Hard"}})";
    if (!writeTextFile(hardMc, hardJson))
        return false;

    const QString noVersionMc = nestedDir + "/fallback.mc";
    const QByteArray fallbackJson = R"({"meta":{}})";
    if (!writeTextFile(noVersionMc, fallbackJson))
        return false;

    if (ProjectIO::getDifficultyFromMc(hardMc) != "Hard")
        return false;

    const auto charts = ProjectIO::findChartsInDirectory(root);
    bool foundHard = false;
    bool foundFallback = false;
    for (const auto &entry : charts)
    {
        if (entry.first == hardMc && entry.second == "Hard")
            foundHard = true;
        if (entry.first == noVersionMc && entry.second == "fallback")
            foundFallback = true;
    }
    return foundHard && foundFallback;
}

bool testProjectIoGetDifficultyInvalidJsonReturnsEmpty()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString path = tempDir.path() + "/invalid.mc";
    if (!writeTextFile(path, QByteArray("{ this is not json }")))
        return false;

    return ProjectIO::getDifficultyFromMc(path).isEmpty();
}

bool testProjectIoFindChartsMissingDirReturnsEmpty()
{
    const QString missingDir = QDir::temp().absoluteFilePath("malody_tools_missing_dir_for_test");
    const auto charts = ProjectIO::findChartsInDirectory(missingDir);
    return charts.isEmpty();
}

bool testProjectIoExportToMczRejectsMissingChart()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString output = tempDir.path() + "/out.mcz";
    const QString missing = tempDir.path() + "/missing.mc";
    return !ProjectIO::exportToMcz(output, missing);
}

bool testProjectIoExtractMczRejectsMissingFile()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString missingMcz = tempDir.path() + "/missing.mcz";
    const QString outDir = tempDir.path() + "/out";
    QString extractedDir;
    return !ProjectIO::extractMcz(missingMcz, outDir, extractedDir);
}

bool testProjectIoFindChartsInvalidJsonFallsBackBaseName()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString badMc = tempDir.path() + "/broken_name.mc";
    if (!writeTextFile(badMc, QByteArray("{ not json }")))
        return false;

    const auto charts = ProjectIO::findChartsInDirectory(tempDir.path());
    for (const auto &entry : charts)
    {
        if (entry.first == badMc)
            return entry.second == "broken_name";
    }
    return false;
}

bool testChartIoLoadMissingFileFails()
{
    Chart chart;
    const QString missing = QDir::temp().absoluteFilePath("malody_nonexistent_chart_for_test.mc");
    return !ChartIO::load(missing, chart, false);
}

bool testChartIoLoadInvalidJsonFails()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    const QString path = tempDir.path() + "/bad.mc";
    if (!writeTextFile(path, QByteArray("{ definitely bad json }")))
        return false;

    Chart chart;
    return !ChartIO::load(path, chart, false);
}

bool testChartIoSaveInvalidPathFails()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    Chart chart;
    const QString invalidPath = tempDir.path() + "/missing_parent/out.mc";
    return !ChartIO::save(invalidPath, chart);
}

bool testChartClearResetsDefaults()
{
    Chart chart;
    chart.clearNotes();
    chart.bpmList().clear();
    chart.addNote(makeNormalNote(1, 0, 1, 128, "seed-a"));
    chart.addBpm(BpmEntry(4, 0, 1, 180.0));
    chart.meta().title = "Changed";

    chart.clear();

    if (!chart.notes().isEmpty())
        return false;
    if (chart.bpmList().size() != 1)
        return false;
    const BpmEntry &bpm = chart.bpmList().first();
    if (bpm.beatNum != 0 || bpm.numerator != 1 || bpm.denominator != 1 || !nearlyEqual(bpm.bpm, 120.0))
        return false;
    return chart.meta().title == "Untitled";
}

bool testChartIsValidRules()
{
    Chart chart;
    chart.clearNotes();
    chart.bpmList().clear();

    if (chart.isValid())
        return false;

    chart.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!chart.isValid())
        return false;

    chart.clearNotes();
    chart.addBpm(BpmEntry(0, 0, 1, 120.0));
    return chart.isValid();
}

bool testNoteTypeConversionFallback()
{
    if (Note::intToNoteType(0) != NoteType::NORMAL)
        return false;
    if (Note::intToNoteType(1) != NoteType::SOUND)
        return false;
    if (Note::intToNoteType(3) != NoteType::RAIN)
        return false;
    if (Note::intToNoteType(99) != NoteType::NORMAL)
        return false;
    if (Note::noteTypeToInt(NoteType::NORMAL) != 0)
        return false;
    if (Note::noteTypeToInt(NoteType::SOUND) != 1)
        return false;
    return Note::noteTypeToInt(NoteType::RAIN) == 3;
}

bool testMathUtilsSnapXGridAndBoundary()
{
    if (MathUtils::snapXToGrid(-1, 16) != 0)
        return false;
    if (MathUtils::snapXToGrid(600, 16) != 512)
        return false;

    if (MathUtils::snapXToBoundary(-10) != 0)
        return false;
    if (MathUtils::snapXToBoundary(10) != 0)
        return false;
    if (MathUtils::snapXToBoundary(502) != 512)
        return false;
    return MathUtils::snapXToBoundary(256) == 256;
}

bool testMathUtilsSnapTimeAndPixelRoundTrip()
{
    const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
    const double snapped = MathUtils::snapTimeToGrid(260.0, bpmList, 0, 4);
    if (!nearlyEqual(snapped, 250.0, 1e-3))
        return false;

    const double beat = 7.25;
    const double pixel = MathUtils::beatToPixel(beat, 2.0, 10.0, 800);
    const double beatBack = MathUtils::pixelToBeat(static_cast<int>(pixel), 2.0, 10.0, 800);
    return nearlyEqual(beatBack, beat, 0.02);
}

bool testMathUtilsSnapNoteToTimeWithBoundary()
{
    Note note = makeNormalNote(-1, 0, 1, 128, "n");
    Note snapped = MathUtils::snapNoteToTimeWithBoundary(note, 4);
    if (snapped.beatNum < 0)
        return false;

    Note rain(2, 0, 1, 1, 0, 1, 256);
    rain.id = "r";
    rain.endBeatNum = 1;
    rain.endNumerator = 0;
    rain.endDenominator = 1;
    Note snappedRain = MathUtils::snapNoteToTimeWithBoundary(rain, 4);

    const double startBeat = MathUtils::beatToFloat(snappedRain.beatNum, snappedRain.numerator, snappedRain.denominator);
    const double endBeat = MathUtils::beatToFloat(snappedRain.endBeatNum, snappedRain.endNumerator, snappedRain.endDenominator);
    return endBeat >= startBeat;
}

bool testMathUtilsCacheBeforeFirstSegmentConsistency()
{
    const QVector<BpmEntry> bpmList = {
        BpmEntry(4, 0, 1, 120.0),
        BpmEntry(8, 0, 1, 240.0)};
    const auto cache = MathUtils::buildBpmTimeCache(bpmList, 100);

    const double byList = MathUtils::beatToMs(2, 0, 1, bpmList, 100);
    const double byCache = MathUtils::beatToMs(2, 0, 1, cache);
    return nearlyEqual(byList, byCache, 1e-6);
}

bool testMathUtilsFloatToBeatSimplifiesFraction()
{
    int beatNum = 0;
    int num = 0;
    int den = 1;
    MathUtils::floatToBeat(5.5, beatNum, num, den, 64);
    return beatNum == 5 && num == 1 && den == 2;
}

bool testMathUtilsFloatToBeatIntegralCase()
{
    int beatNum = 0;
    int num = 0;
    int den = 1;
    MathUtils::floatToBeat(7.0, beatNum, num, den, 64);
    return beatNum == 7 && num == 0 && den == 1;
}

bool testMathUtilsIsSameTimeWithSnap()
{
    Note a = makeNormalNote(1, 1, 3, 100, "a");
    Note b = makeNormalNote(1, 2, 6, 200, "b");
    return MathUtils::isSameTime(a, b, 6);
}

bool testMathUtilsBeatPixelGuardValues()
{
    if (!nearlyEqual(MathUtils::beatToPixel(4.0, 0.0, 0.0, 600), 0.0))
        return false;
    if (!nearlyEqual(MathUtils::pixelToBeat(120, 3.5, 8.0, 0), 3.5))
        return false;
    return true;
}

bool testMathUtilsSnapNoteToTimeReducesFraction()
{
    Note note = makeNormalNote(1, 3, 12, 200, "snap");
    Note snapped = MathUtils::snapNoteToTime(note, 12);
    return snapped.beatNum == 1 &&
           snapped.numerator == 1 &&
           snapped.denominator == 4;
}

bool testMathUtilsSnapXToGridDivisionOne()
{
    return MathUtils::snapXToGrid(255, 1) == 0 &&
           MathUtils::snapXToGrid(511, 1) == 512;
}

bool testChartIoLoadAddsDefaultBpmWhenAllTimeInvalid()
{
    Chart chart;
    const QByteArray json = R"({
        "time":[
            {"beat":[0,0,0],"bpm":120.0},
            {"beat":[4,0,1],"bpm":0.0}
        ],
        "note":[]
    })";
    if (!loadChartFromJsonContent(json, chart))
        return false;

    if (chart.bpmList().size() != 1)
        return false;
    const BpmEntry &entry = chart.bpmList().first();
    return entry.beatNum == 0 &&
           entry.numerator == 1 &&
           entry.denominator == 1 &&
           nearlyEqual(entry.bpm, 120.0);
}

bool testChartIoLoadMetaAudioOffsetFallbackFromSoundNote()
{
    Chart chart;
    const QByteArray json = R"({
        "meta":{
            "song":{"title":"SongTitle","artist":"SongArtist"},
            "version":"Hard"
        },
        "time":[{"beat":[0,0,1],"bpm":128.0}],
        "note":[
            {"beat":[0,0,1],"type":1,"sound":"music.ogg","vol":90,"offset":321}
        ]
    })";
    if (!loadChartFromJsonContent(json, chart))
        return false;

    const MetaData &meta = chart.meta();
    return meta.title == "SongTitle" &&
           meta.artist == "SongArtist" &&
           meta.difficulty == "Hard" &&
           meta.audioFile == "music.ogg" &&
           meta.offset == 321;
}

bool testChartIoLoadClampsXAndSkipsInvalidNotes()
{
    Chart chart;
    const QByteArray json = R"({
        "time":[{"beat":[0,0,1],"bpm":120.0}],
        "note":[
            {"beat":[1,0,1],"x":-30},
            {"beat":[2,0,1],"x":999},
            {"beat":[3,0,1],"type":3,"x":999,"endbeat":[4,0,1]},
            {"beat":[3,0,1],"type":3,"x":100,"endbeat":[4,0,0]},
            {"beat":[5,0,1],"type":1},
            {"beat":[6,0,1]}
        ]
    })";
    if (!loadChartFromJsonContent(json, chart))
        return false;

    const QVector<Note> &notes = chart.notes();
    if (notes.size() != 3)
        return false;

    bool hasClampedLeft = false;
    bool hasClampedRight = false;
    bool hasClampedRain = false;
    for (const Note &note : notes)
    {
        if (note.type == NoteType::NORMAL && note.beatNum == 1 && note.x == 0)
            hasClampedLeft = true;
        if (note.type == NoteType::NORMAL && note.beatNum == 2 && note.x == 512)
            hasClampedRight = true;
        if (note.type == NoteType::RAIN &&
            note.beatNum == 3 &&
            note.endBeatNum == 4 &&
            note.x == 512)
        {
            hasClampedRain = true;
        }
    }
    return hasClampedLeft && hasClampedRight && hasClampedRain;
}

bool testChartIoSaveLoadRoundTripCore()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    Chart chart;
    chart.clearNotes();
    chart.bpmList().clear();
    chart.addBpm(BpmEntry(0, 0, 1, 150.0));

    MetaData meta;
    meta.title = "RoundTripTitle";
    meta.artist = "RoundTripArtist";
    meta.difficulty = "Insane";
    meta.audioFile = "roundtrip.ogg";
    meta.previewTime = 12345;
    meta.offset = 222;
    meta.speed = 7;
    meta.firstBpm = 150.0;
    chart.meta() = meta;

    chart.addNote(makeNormalNote(1, 0, 1, 64, "normal"));
    Note rain(2, 0, 1, 3, 0, 1, 300);
    rain.id = "rain";
    chart.addNote(rain);
    Note sound(4, 0, 1, "hit.wav", 88, 12);
    sound.id = "sound";
    chart.addNote(sound);

    const QString path = tempDir.path() + "/roundtrip.mc";
    if (!ChartIO::save(path, chart))
        return false;

    Chart loaded;
    if (!ChartIO::load(path, loaded, false))
        return false;

    if (loaded.notes().size() != 3)
        return false;
    if (loaded.bpmList().size() != 1 || !hasBpmEntry(loaded.bpmList(), 0, 0, 1, 150.0))
        return false;
    if (loaded.meta().title != "RoundTripTitle" ||
        loaded.meta().artist != "RoundTripArtist" ||
        loaded.meta().difficulty != "Insane" ||
        loaded.meta().audioFile != "roundtrip.ogg" ||
        loaded.meta().offset != 222 ||
        loaded.meta().speed != 7)
    {
        return false;
    }

    bool hasNormal = false;
    bool hasRain = false;
    bool hasSound = false;
    for (const Note &note : loaded.notes())
    {
        if (note.type == NoteType::NORMAL && note.beatNum == 1 && note.x == 64)
            hasNormal = true;
        if (note.type == NoteType::RAIN && note.beatNum == 2 && note.endBeatNum == 3 && note.x == 300)
            hasRain = true;
        if (note.type == NoteType::SOUND && note.beatNum == 4 && note.sound == "hit.wav" && note.vol == 88 && note.offset == 12)
            hasSound = true;
    }
    return hasNormal && hasRain && hasSound;
}

bool testChartIoLoadFlatMetaFields()
{
    Chart chart;
    const QByteArray json = R"({
        "meta":{
            "title":"FlatTitle",
            "title_org":"FlatTitleOrg",
            "artist":"FlatArtist",
            "artist_org":"FlatArtistOrg",
            "creator":"FlatCreator",
            "version":"FlatDiff",
            "background":"bg.png",
            "audio":"flat.ogg",
            "speed":3,
            "preview":456,
            "offset":78,
            "bpm":111.0
        },
        "time":[{"beat":[0,0,1],"bpm":111.0}],
        "note":[]
    })";
    if (!loadChartFromJsonContent(json, chart))
        return false;

    const MetaData &meta = chart.meta();
    return meta.title == "FlatTitle" &&
           meta.titleOrg == "FlatTitleOrg" &&
           meta.artist == "FlatArtist" &&
           meta.artistOrg == "FlatArtistOrg" &&
           meta.chartAuthor == "FlatCreator" &&
           meta.difficulty == "FlatDiff" &&
           meta.backgroundFile == "bg.png" &&
           meta.audioFile == "flat.ogg" &&
           meta.speed == 3 &&
           meta.previewTime == 456 &&
           meta.offset == 78 &&
           nearlyEqual(meta.firstBpm, 111.0);
}

bool testChartIoLoadModeExtSpeedOverridesFlatSpeed()
{
    Chart chart;
    const QByteArray json = R"({
        "meta":{
            "title":"T",
            "artist":"A",
            "speed":2,
            "mode_ext":{"speed":9}
        },
        "time":[{"beat":[0,0,1],"bpm":120.0}],
        "note":[]
    })";
    if (!loadChartFromJsonContent(json, chart))
        return false;
    return chart.meta().speed == 9;
}

bool testChartIoLoadMissingMetaKeepsDefaults()
{
    Chart chart;
    const QByteArray json = R"({
        "time":[{"beat":[0,0,1],"bpm":120.0}],
        "note":[]
    })";
    if (!loadChartFromJsonContent(json, chart))
        return false;

    const MetaData &meta = chart.meta();
    return meta.title == "Untitled" &&
           meta.artist == "Unknown" &&
           meta.difficulty == "Normal";
}

Note makeNormalNote(int beatNum, int num, int den, int x, const QString &id = QString())
{
    Note note(beatNum, num, den, x);
    if (!id.isEmpty())
        note.id = id;
    return note;
}

bool hasNoteById(const QVector<Note> &notes, const QString &id, int beatNum, int x)
{
    for (const Note &note : notes)
    {
        if (note.id == id && note.beatNum == beatNum && note.x == x)
            return true;
    }
    return false;
}

bool hasBpmEntry(const QVector<BpmEntry> &list, int beatNum, int num, int den, double bpm)
{
    for (const BpmEntry &entry : list)
    {
        if (entry.beatNum == beatNum &&
            entry.numerator == num &&
            entry.denominator == den &&
            nearlyEqual(entry.bpm, bpm))
        {
            return true;
        }
    }
    return false;
}

bool testChartControllerApplyBatchEditAcceptsValidPayload()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    const Note moved = makeNormalNote(2, 0, 1, 128, "seed-a");
    const Note added = makeNormalNote(3, 0, 1, 256, "seed-b");

    const bool ok = controller.applyBatchEdit(
        "batch edit valid",
        QVector<Note>{added},
        QVector<Note>{},
        QList<QPair<Note, Note>>{qMakePair(base, moved)});
    if (!ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    if (notes.size() != 2)
        return false;

    bool foundMoved = false;
    bool foundAdded = false;
    for (const Note &n : notes)
    {
        if (n.id == "seed-a" && n.beatNum == 2 && n.x == 128)
            foundMoved = true;
        if (n.id == "seed-b" && n.beatNum == 3 && n.x == 256)
            foundAdded = true;
    }
    return foundMoved && foundAdded;
}

bool testChartControllerApplyBatchEditRejectsInvalidAddNote()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    Note invalidAdd = makeNormalNote(2, 0, 1, 900, "bad-add");
    invalidAdd.type = NoteType::NORMAL;

    const bool ok = controller.applyBatchEdit(
        "batch edit invalid add",
        QVector<Note>{invalidAdd},
        QVector<Note>{},
        QList<QPair<Note, Note>>{});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsConflictingMoveAndRemove()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    const Note moved = makeNormalNote(2, 0, 1, 128, "seed-a");
    const bool ok = controller.applyBatchEdit(
        "batch edit conflict",
        QVector<Note>{},
        QVector<Note>{base},
        QList<QPair<Note, Note>>{qMakePair(base, moved)});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsMissingMoveSource()
{
    ChartController controller;
    const Note existing = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(existing);

    const Note missingSource = makeNormalNote(5, 0, 1, 300, "not-in-chart");
    const Note movedTarget = makeNormalNote(6, 0, 1, 200, "not-in-chart");
    const bool ok = controller.applyBatchEdit(
        "batch edit missing source",
        QVector<Note>{},
        QVector<Note>{},
        QList<QPair<Note, Note>>{qMakePair(missingSource, movedTarget)});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsEmptyPayload()
{
    ChartController controller;
    return !controller.applyBatchEdit(
        "batch edit empty",
        QVector<Note>{},
        QVector<Note>{},
        QList<QPair<Note, Note>>{});
}

bool testChartControllerApplyBatchEditRejectsInvalidRemoveReference()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    Note invalidRemove = base;
    invalidRemove.x = 900;

    const bool ok = controller.applyBatchEdit(
        "batch edit invalid remove",
        QVector<Note>{},
        QVector<Note>{invalidRemove},
        QList<QPair<Note, Note>>{});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsRemoveMissingNote()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    const Note missing = makeNormalNote(1, 0, 1, 64, "seed-missing");
    const bool ok = controller.applyBatchEdit(
        "batch edit remove missing",
        QVector<Note>{},
        QVector<Note>{missing},
        QList<QPair<Note, Note>>{});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsInvalidMoveTarget()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    Note invalidTarget = makeNormalNote(2, 0, 1, 128, "seed-a");
    invalidTarget.x = 900;

    const bool ok = controller.applyBatchEdit(
        "batch edit invalid move target",
        QVector<Note>{},
        QVector<Note>{},
        QList<QPair<Note, Note>>{qMakePair(base, invalidTarget)});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsDuplicatedMoveSource()
{
    ChartController controller;
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(base);

    const Note movedOne = makeNormalNote(2, 0, 1, 128, "seed-a");
    const Note movedTwo = makeNormalNote(3, 0, 1, 256, "seed-a");
    const bool ok = controller.applyBatchEdit(
        "batch edit duplicated source",
        QVector<Note>{},
        QVector<Note>{},
        QList<QPair<Note, Note>>{
            qMakePair(base, movedOne),
            qMakePair(base, movedTwo)});
    if (ok)
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 && notes.first().id == "seed-a";
}

bool testChartControllerApplyBatchEditRejectsOversizedBatch()
{
    ChartController controller;
    const Note seed = makeNormalNote(1, 0, 1, 64, "seed-a");
    QVector<Note> notesToAdd(20001, seed);
    return !controller.applyBatchEdit(
        "batch edit oversized",
        notesToAdd,
        QVector<Note>{},
        QList<QPair<Note, Note>>{});
}

bool testChartControllerApplyBatchEditEmptyActionUsesDefaultUndoText()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    const Note added = makeNormalNote(2, 0, 1, 128, "seed-b");
    if (!controller.applyBatchEdit(QString(),
                                   QVector<Note>{added},
                                   QVector<Note>{},
                                   QList<QPair<Note, Note>>{}))
    {
        return false;
    }

    return controller.canUndo() &&
           controller.nextUndoActionText() == "Plugin Batch Edit";
}

bool testChartControllerApplyBatchEditLimitBoundaryAccepted()
{
    ChartController controller;
    QVector<Note> notesToAdd;
    notesToAdd.reserve(20000);
    for (int i = 0; i < 20000; ++i)
    {
        Note n = makeNormalNote(i, 0, 1, i % 513, QString("bulk-%1").arg(i));
        notesToAdd.append(n);
    }

    const bool ok = controller.applyBatchEdit(
        "batch edit boundary accepted",
        notesToAdd,
        QVector<Note>{},
        QList<QPair<Note, Note>>{});
    if (!ok)
        return false;
    if (controller.chart()->notes().size() != 20000)
        return false;

    controller.undo();
    return controller.chart()->notes().isEmpty();
}

bool testChartControllerApplyBatchEditUndoRedo()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    const Note moved = makeNormalNote(2, 0, 1, 128, "seed-a");
    const Note added = makeNormalNote(3, 0, 1, 256, "seed-b");
    if (!controller.applyBatchEdit(
            "batch edit undo redo",
            QVector<Note>{added},
            QVector<Note>{},
            QList<QPair<Note, Note>>{qMakePair(seed.notes().first(), moved)}))
    {
        return false;
    }

    const QVector<Note> &afterApply = controller.chart()->notes();
    if (afterApply.size() != 2)
        return false;
    if (!hasNoteById(afterApply, "seed-a", 2, 128) || !hasNoteById(afterApply, "seed-b", 3, 256))
        return false;

    controller.undo();
    const QVector<Note> &afterUndo = controller.chart()->notes();
    if (afterUndo.size() != 1)
        return false;
    if (!hasNoteById(afterUndo, "seed-a", 1, 64))
        return false;

    controller.redo();
    const QVector<Note> &afterRedo = controller.chart()->notes();
    return afterRedo.size() == 2 &&
           hasNoteById(afterRedo, "seed-a", 2, 128) &&
           hasNoteById(afterRedo, "seed-b", 3, 256);
}

bool testChartControllerMoveNotesAcceptsValidPayload()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    seed.addNote(base);
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    const Note moved = makeNormalNote(4, 0, 1, 300, "seed-a");
    controller.moveNotes(QList<QPair<Note, Note>>{qMakePair(base, moved)});

    const QVector<Note> &afterMove = controller.chart()->notes();
    if (afterMove.size() != 1 || !hasNoteById(afterMove, "seed-a", 4, 300))
        return false;

    controller.undo();
    const QVector<Note> &afterUndo = controller.chart()->notes();
    if (afterUndo.size() != 1 || !hasNoteById(afterUndo, "seed-a", 1, 64))
        return false;

    controller.redo();
    const QVector<Note> &afterRedo = controller.chart()->notes();
    return afterRedo.size() == 1 && hasNoteById(afterRedo, "seed-a", 4, 300);
}

bool testChartControllerMoveNotesRejectsInvalidPayloadNoMutation()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    const Note base = makeNormalNote(1, 0, 1, 64, "seed-a");
    seed.addNote(base);
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    Note invalidTarget = makeNormalNote(3, 0, 1, 200, "seed-a");
    invalidTarget.x = 900;
    controller.moveNotes(QList<QPair<Note, Note>>{qMakePair(base, invalidTarget)});

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 &&
           hasNoteById(notes, "seed-a", 1, 64) &&
           !controller.canUndo() &&
           !controller.canRedo();
}

bool testChartControllerMoveNotesEmptyNoOp()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.moveNotes(QList<QPair<Note, Note>>{});
    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 &&
           hasNoteById(notes, "seed-a", 1, 64) &&
           !controller.canUndo() &&
           !controller.canRedo();
}

bool testChartControllerBpmAddUndoRedo()
{
    ChartController controller;
    Chart seed;
    seed.bpmList().clear();
    seed.addBpm(BpmEntry(0, 0, 1, 120.0));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.addBpm(BpmEntry(4, 0, 1, 180.0));
    const QVector<BpmEntry> &afterAdd = controller.chart()->bpmList();
    if (afterAdd.size() != 2 || !hasBpmEntry(afterAdd, 4, 0, 1, 180.0))
        return false;

    controller.undo();
    const QVector<BpmEntry> &afterUndo = controller.chart()->bpmList();
    if (afterUndo.size() != 1 || !hasBpmEntry(afterUndo, 0, 0, 1, 120.0))
        return false;

    controller.redo();
    const QVector<BpmEntry> &afterRedo = controller.chart()->bpmList();
    return afterRedo.size() == 2 &&
           hasBpmEntry(afterRedo, 0, 0, 1, 120.0) &&
           hasBpmEntry(afterRedo, 4, 0, 1, 180.0);
}

bool testChartControllerBpmRemoveUndoRedo()
{
    ChartController controller;
    Chart seed;
    seed.bpmList().clear();
    seed.addBpm(BpmEntry(0, 0, 1, 120.0));
    seed.addBpm(BpmEntry(4, 0, 1, 150.0));
    seed.addBpm(BpmEntry(8, 0, 1, 180.0));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.removeBpm(1);
    const QVector<BpmEntry> &afterRemove = controller.chart()->bpmList();
    if (afterRemove.size() != 2 || hasBpmEntry(afterRemove, 4, 0, 1, 150.0))
        return false;

    controller.undo();
    const QVector<BpmEntry> &afterUndo = controller.chart()->bpmList();
    if (afterUndo.size() != 3 || !hasBpmEntry(afterUndo, 4, 0, 1, 150.0))
        return false;

    controller.redo();
    const QVector<BpmEntry> &afterRedo = controller.chart()->bpmList();
    return afterRedo.size() == 2 && !hasBpmEntry(afterRedo, 4, 0, 1, 150.0);
}

bool testChartControllerBpmUpdateUndoRedoWithSort()
{
    ChartController controller;
    Chart seed;
    seed.bpmList().clear();
    seed.addBpm(BpmEntry(0, 0, 1, 120.0));
    seed.addBpm(BpmEntry(4, 0, 1, 150.0));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.updateBpm(0, BpmEntry(6, 0, 1, 200.0));
    const QVector<BpmEntry> &afterUpdate = controller.chart()->bpmList();
    if (afterUpdate.size() != 2)
        return false;
    if (afterUpdate[0].beatNum != 4 || afterUpdate[1].beatNum != 6)
        return false;
    if (!hasBpmEntry(afterUpdate, 6, 0, 1, 200.0))
        return false;

    controller.undo();
    const QVector<BpmEntry> &afterUndo = controller.chart()->bpmList();
    if (afterUndo.size() != 2)
        return false;
    if (afterUndo[0].beatNum != 0 || afterUndo[1].beatNum != 4)
        return false;
    if (!hasBpmEntry(afterUndo, 0, 0, 1, 120.0))
        return false;

    controller.redo();
    const QVector<BpmEntry> &afterRedo = controller.chart()->bpmList();
    return afterRedo.size() == 2 &&
           afterRedo[0].beatNum == 4 &&
           afterRedo[1].beatNum == 6 &&
           hasBpmEntry(afterRedo, 6, 0, 1, 200.0);
}

bool testChartControllerSetMetaUndoRedo()
{
    ChartController controller;
    Chart seed;
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    const QString originalTitle = controller.chart()->meta().title;

    MetaData nextMeta = controller.chart()->meta();
    nextMeta.title = "Unit Test Title";
    nextMeta.artist = "Unit Test Artist";
    nextMeta.audioFile = "audio.ogg";

    controller.setMetaData(nextMeta);
    if (controller.chart()->meta().title != "Unit Test Title")
        return false;

    controller.undo();
    if (controller.chart()->meta().title != originalTitle)
        return false;

    controller.redo();
    return controller.chart()->meta().title == "Unit Test Title";
}

bool testChartControllerAddNotesUndoRedo()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    const Note noteA = makeNormalNote(1, 0, 1, 64, "seed-a");
    const Note noteB = makeNormalNote(2, 0, 1, 128, "seed-b");
    controller.addNotes(QVector<Note>{noteA, noteB});

    const QVector<Note> &afterAdd = controller.chart()->notes();
    if (afterAdd.size() != 2)
        return false;
    if (!hasNoteById(afterAdd, "seed-a", 1, 64) || !hasNoteById(afterAdd, "seed-b", 2, 128))
        return false;

    controller.undo();
    if (!controller.chart()->notes().isEmpty())
        return false;

    controller.redo();
    const QVector<Note> &afterRedo = controller.chart()->notes();
    return afterRedo.size() == 2 &&
           hasNoteById(afterRedo, "seed-a", 1, 64) &&
           hasNoteById(afterRedo, "seed-b", 2, 128);
}

bool testChartControllerRemoveNotesUndoRedo()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    const Note noteA = makeNormalNote(1, 0, 1, 64, "seed-a");
    const Note noteB = makeNormalNote(2, 0, 1, 128, "seed-b");
    seed.addNote(noteA);
    seed.addNote(noteB);
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.removeNotes(QVector<Note>{noteA, noteB});
    if (!controller.chart()->notes().isEmpty())
        return false;

    controller.undo();
    const QVector<Note> &afterUndo = controller.chart()->notes();
    if (afterUndo.size() != 2)
        return false;
    if (!hasNoteById(afterUndo, "seed-a", 1, 64) || !hasNoteById(afterUndo, "seed-b", 2, 128))
        return false;

    controller.redo();
    return controller.chart()->notes().isEmpty();
}

bool testChartControllerEmptyBatchCommandsNoOp()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.addNotes(QVector<Note>{});
    controller.removeNotes(QVector<Note>{});
    controller.moveNotes(QList<QPair<Note, Note>>{});

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 &&
           hasNoteById(notes, "seed-a", 1, 64) &&
           !controller.canUndo() &&
           !controller.canRedo();
}

bool testChartControllerMoveNoteSameInputNoOp()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    const Note note = makeNormalNote(1, 0, 1, 64, "seed-a");
    seed.addNote(note);
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.moveNote(note, note);
    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 &&
           hasNoteById(notes, "seed-a", 1, 64) &&
           !controller.canUndo();
}

bool testChartControllerUndoRedoActionTextLifecycle()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    if (!controller.nextUndoActionText().isEmpty() || !controller.nextRedoActionText().isEmpty())
        return false;

    const Note note = makeNormalNote(1, 0, 1, 64, "seed-a");
    controller.addNote(note);
    if (!controller.canUndo() || controller.nextUndoActionText() != "Add Note")
        return false;
    if (controller.canRedo() || !controller.nextRedoActionText().isEmpty())
        return false;

    controller.undo();
    if (!controller.canRedo() || controller.nextRedoActionText() != "Add Note")
        return false;
    if (controller.canUndo() || !controller.nextUndoActionText().isEmpty())
        return false;

    controller.redo();
    return controller.canUndo() &&
           controller.nextUndoActionText() == "Add Note" &&
           !controller.canRedo() &&
           controller.nextRedoActionText().isEmpty();
}

bool testChartControllerLoadChartFromDataClearsUndoStack()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.canUndo())
        return false;

    Chart replacement;
    replacement.clearNotes();
    replacement.addNote(makeNormalNote(2, 0, 1, 200, "seed-b"));
    if (!controller.loadChartFromData(QString(), replacement))
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 &&
           hasNoteById(notes, "seed-b", 2, 200) &&
           !controller.canUndo() &&
           !controller.canRedo();
}

bool testChartControllerApplyExternalMutationUndoRedo()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    Chart mutated = seed;
    mutated.addNote(makeNormalNote(3, 0, 1, 256, "seed-b"));

    if (!controller.applyExternalChartMutation("external mutate", mutated))
        return false;

    const QVector<Note> &afterApply = controller.chart()->notes();
    if (afterApply.size() != 2 || !hasNoteById(afterApply, "seed-b", 3, 256))
        return false;

    controller.undo();
    const QVector<Note> &afterUndo = controller.chart()->notes();
    if (afterUndo.size() != 1 || !hasNoteById(afterUndo, "seed-a", 1, 64))
        return false;

    controller.redo();
    const QVector<Note> &afterRedo = controller.chart()->notes();
    return afterRedo.size() == 2 && hasNoteById(afterRedo, "seed-b", 3, 256);
}

bool testChartControllerApplyExternalMutationEmptyActionUsesDefaultUndoText()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    Chart mutated = seed;
    mutated.addNote(makeNormalNote(2, 0, 1, 200, "seed-b"));
    if (!controller.applyExternalChartMutation(QString(), mutated))
        return false;

    return controller.canUndo() &&
           controller.nextUndoActionText() == "Plugin Mutation";
}

bool testChartControllerLoadChartFromDataSetsPath()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    const QString expectedPath = "C:/tmp/unit_test_chart.mc";
    if (!controller.loadChartFromData(expectedPath, seed))
        return false;
    return controller.chartFilePath() == expectedPath;
}

bool testChartControllerLoadChartMissingFileKeepsState()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    const QString originalPath = "C:/tmp/original_chart.mc";
    if (!controller.loadChartFromData(originalPath, seed))
        return false;

    const QString missingPath = QDir::temp().absoluteFilePath("missing_chart_for_controller_test.mc");
    if (controller.loadChart(missingPath))
        return false;

    const QVector<Note> &notes = controller.chart()->notes();
    return notes.size() == 1 &&
           hasNoteById(notes, "seed-a", 1, 64) &&
           controller.chartFilePath() == originalPath;
}

bool testChartControllerSaveChartInvalidPathFails()
{
    ChartController controller;
    Chart seed;
    seed.clearNotes();
    seed.addNote(makeNormalNote(1, 0, 1, 64, "seed-a"));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;
    const QString invalidPath = tempDir.path() + "/missing_parent/out.mc";
    return !controller.saveChart(invalidPath);
}

bool testNoteIsXValidForSoundIgnoresRange()
{
    Note sound(1, 0, 1, "hit.wav", 50, 0);
    sound.id = "s";
    sound.x = -1000;
    if (!sound.isXValid())
        return false;
    sound.x = 9999;
    return sound.isXValid();
}

bool testChartControllerInvalidBpmIndexNoOp()
{
    ChartController controller;
    Chart seed;
    seed.bpmList().clear();
    seed.addBpm(BpmEntry(0, 0, 1, 120.0));
    if (!controller.loadChartFromData(QString(), seed))
        return false;

    controller.removeBpm(-1);
    controller.removeBpm(99);
    controller.updateBpm(-1, BpmEntry(2, 0, 1, 150.0));
    controller.updateBpm(99, BpmEntry(2, 0, 1, 150.0));

    const QVector<BpmEntry> &list = controller.chart()->bpmList();
    return list.size() == 1 &&
           hasBpmEntry(list, 0, 0, 1, 120.0) &&
           !controller.canUndo();
}

bool testChartRemoveByContentWhenIdMissing()
{
    Chart chart;
    chart.clearNotes();

    Note first = makeNormalNote(1, 0, 1, 100);
    first.id.clear();
    Note second = makeNormalNote(2, 0, 1, 200);
    second.id = "keep";

    chart.addNote(first);
    chart.addNote(second);
    chart.removeNote(first);

    const QVector<Note> &notes = chart.notes();
    return notes.size() == 1 && notes.first().id == "keep";
}

bool testChartSortNotesSoundAfterNormalAtSameBeat()
{
    Chart chart;
    chart.clearNotes();

    Note normal = makeNormalNote(4, 0, 1, 128, "normal");
    Note sound(4, 0, 1, "hit.wav", 80, 0);
    sound.id = "sound";

    chart.addNote(sound);
    chart.addNote(normal);

    const QVector<Note> &notes = chart.notes();
    return notes.size() == 2 &&
           notes[0].id == "normal" &&
           notes[1].id == "sound";
}

bool testNoteValidationBoundaries()
{
    Note normal = makeNormalNote(1, 0, 1, 0, "n");
    if (!normal.isValid())
        return false;

    Note invalidNormal = makeNormalNote(1, 0, 1, 700, "bad-n");
    if (invalidNormal.isValid())
        return false;

    Note sound(2, 0, 1, "hit.wav", 100, 0);
    sound.id = "s";
    if (!sound.isValid())
        return false;

    Note invalidSound(2, 0, 1, QString(), 100, 0);
    invalidSound.id = "bad-s";
    if (invalidSound.isValid())
        return false;

    Note rain(3, 0, 1, 4, 0, 1, 256);
    rain.id = "r";
    if (!rain.isValid())
        return false;

    Note invalidRain(4, 0, 1, 3, 0, 1, 256);
    invalidRain.id = "bad-r";
    return !invalidRain.isValid();
}

struct RenderBenchmarkResult
{
    int notesTotal = 0;
    int sampleCount = 0;
    qint64 elapsedNs = 0;
    double avgVisibleNotes = 0.0;
    int maxVisibleNotes = 0;
};

RenderBenchmarkResult runRenderVisibilityBenchmark(const QVector<Note> &inputNotes, int sampleCount)
{
    RenderBenchmarkResult result;
    result.notesTotal = inputNotes.size();
    result.sampleCount = qMax(1, sampleCount);
    if (inputNotes.isEmpty())
        return result;

    struct NoteCache
    {
        NoteType type = NoteType::NORMAL;
        double beat = 0.0;
        double endBeat = 0.0;
    };

    QVector<NoteCache> cache(inputNotes.size());
    QVector<int> normalIndices;
    QVector<int> rainIndices;
    normalIndices.reserve(inputNotes.size());
    rainIndices.reserve(inputNotes.size());

    double maxBeat = 0.0;
    for (int i = 0; i < inputNotes.size(); ++i)
    {
        const Note &note = inputNotes[i];
        const double beat = MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator);
        double endBeat = beat;
        if (note.type == NoteType::RAIN)
            endBeat = MathUtils::beatToFloat(note.endBeatNum, note.endNumerator, note.endDenominator);

        cache[i].type = note.type;
        cache[i].beat = beat;
        cache[i].endBeat = endBeat;
        maxBeat = qMax(maxBeat, qMax(beat, endBeat));

        if (note.type == NoteType::RAIN)
            rainIndices.append(i);
        else if (note.type == NoteType::NORMAL)
            normalIndices.append(i);
    }

    auto byBeat = [&cache](int lhs, int rhs) {
        if (cache[lhs].beat == cache[rhs].beat)
            return lhs < rhs;
        return cache[lhs].beat < cache[rhs].beat;
    };
    std::sort(normalIndices.begin(), normalIndices.end(), byBeat);
    std::sort(rainIndices.begin(), rainIndices.end(), byBeat);

    constexpr double visibleRange = 8.0;
    QElapsedTimer timer;
    timer.start();
    qint64 totalVisible = 0;

    for (int s = 0; s < result.sampleCount; ++s)
    {
        const double t = (result.sampleCount == 1) ? 0.0 : static_cast<double>(s) / (result.sampleCount - 1);
        const double scrollBeat = qMax(0.0, (maxBeat + 1.0) * t - (visibleRange * 0.5));
        const double startBeat = scrollBeat;
        const double endBeat = scrollBeat + visibleRange;

        int visibleCount = 0;

        auto rainBegin = std::lower_bound(
            rainIndices.begin(),
            rainIndices.end(),
            startBeat,
            [&cache](int idx, double beatValue) {
                return cache[idx].beat < beatValue;
            });
        auto rainStartIt = rainBegin;
        while (rainStartIt != rainIndices.begin())
        {
            auto prev = rainStartIt - 1;
            const int idx = *prev;
            if (cache[idx].endBeat <= startBeat)
                break;
            rainStartIt = prev;
        }
        for (auto it = rainStartIt; it != rainIndices.end(); ++it)
        {
            const int idx = *it;
            if (cache[idx].beat >= endBeat)
                break;
            if (cache[idx].endBeat > startBeat)
                ++visibleCount;
        }

        auto normalStart = std::lower_bound(
            normalIndices.begin(),
            normalIndices.end(),
            startBeat - 0.5,
            [&cache](int idx, double beatValue) {
                return cache[idx].beat < beatValue;
            });
        for (auto it = normalStart; it != normalIndices.end(); ++it)
        {
            const int idx = *it;
            if (cache[idx].beat > endBeat + 0.5)
                break;
            ++visibleCount;
        }

        totalVisible += visibleCount;
        if (visibleCount > result.maxVisibleNotes)
            result.maxVisibleNotes = visibleCount;
    }

    result.elapsedNs = timer.nsecsElapsed();
    result.avgVisibleNotes = static_cast<double>(totalVisible) / result.sampleCount;
    return result;
}

bool testKedamonoRenderBaseline()
{
    const QString chartPath =
        QStringLiteral("C:/Users/boatnotcy/AppData/Local/CatchEditor/Malody Catch Chart Editor/beatmap/KEDAMONO Drop-out/0/1737904376.mc");
    if (!QFileInfo::exists(chartPath))
    {
        std::fprintf(stdout, "SKIPPED: KEDAMONO baseline (chart not found at %s)\n", chartPath.toUtf8().constData());
        return true;
    }

    Chart chart;
    if (!ChartIO::load(chartPath, chart, false))
    {
        std::fprintf(stderr, "FAILED: KEDAMONO baseline (load failed)\n");
        return false;
    }

    QVector<Note> notes = chart.notes();
    if (notes.isEmpty())
    {
        std::fprintf(stderr, "FAILED: KEDAMONO baseline (no notes)\n");
        return false;
    }

    std::sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) {
        const double beatA = MathUtils::beatToFloat(a.beatNum, a.numerator, a.denominator);
        const double beatB = MathUtils::beatToFloat(b.beatNum, b.numerator, b.denominator);
        if (beatA == beatB)
            return a.x < b.x;
        return beatA < beatB;
    });

    const int total = notes.size();
    const int sample5k = qMin(5000, total);
    const int sample10k = qMin(10000, total);

    const RenderBenchmarkResult baseline5k = runRenderVisibilityBenchmark(notes.mid(0, sample5k), 200000);
    const RenderBenchmarkResult baseline10k = runRenderVisibilityBenchmark(notes.mid(0, sample10k), 200000);

    std::fprintf(stdout,
                 "KEDAMONO_BASELINE total=%d notes, sample5k=%d, sample10k=%d\n",
                 total,
                 sample5k,
                 sample10k);
    std::fprintf(stdout,
                 "KEDAMONO_BASELINE 5k elapsed_ms=%.3f avg_visible=%.2f max_visible=%d\n",
                 baseline5k.elapsedNs / 1000000.0,
                 baseline5k.avgVisibleNotes,
                 baseline5k.maxVisibleNotes);
    std::fprintf(stdout,
                 "KEDAMONO_BASELINE 10k elapsed_ms=%.3f avg_visible=%.2f max_visible=%d\n",
                 baseline10k.elapsedNs / 1000000.0,
                 baseline10k.avgVisibleNotes,
                 baseline10k.maxVisibleNotes);

    return true;
}

bool testBpmAuxFilesBpmExcludeRangeSerialization()
{
    BpmAuxFiles::BpmExcludeRange range(1, 0, 1, 4, 0, 1, "test reason");
    QJsonObject json = range.toJson();
    
    BpmAuxFiles::BpmExcludeRange loaded = BpmAuxFiles::BpmExcludeRange::fromJson(json);
    
    return loaded.startBeatNum == 1 &&
           loaded.startNumerator == 0 &&
           loaded.startDenominator == 1 &&
           loaded.endBeatNum == 4 &&
           loaded.endNumerator == 0 &&
           loaded.endDenominator == 1 &&
           loaded.reason == "test reason" &&
           loaded.isValid();
}

bool testBpmAuxFilesSongBpmInfoSerialization()
{
    BpmAuxFiles::SongBpmInfo info(128.5, "manual", "test note");
    QJsonObject json = info.toJson();
    
    BpmAuxFiles::SongBpmInfo loaded = BpmAuxFiles::SongBpmInfo::fromJson(json);
    
    return nearlyEqual(loaded.originalBpm, 128.5) &&
           loaded.source == "manual" &&
           loaded.note == "test note" &&
           loaded.isValid();
}

bool testBpmAuxFilesBpmExcludesDataSerialization()
{
    BpmAuxFiles::BpmExcludesData data;
    data.version = "1.0";
    data.excludes.append(BpmAuxFiles::BpmExcludeRange(1, 0, 1, 4, 0, 1, "reason1"));
    data.excludes.append(BpmAuxFiles::BpmExcludeRange(8, 1, 2, 12, 0, 1, "reason2"));
    
    QJsonObject json = data.toJson();
    BpmAuxFiles::BpmExcludesData loaded = BpmAuxFiles::BpmExcludesData::fromJson(json);
    
    return loaded.version == "1.0" &&
           loaded.excludes.size() == 2 &&
           loaded.isValid();
}

bool testBpmAuxFilesChartIdentifierForPath()
{
    QString path1 = "C:/charts/song_name.mc";
    QString id1 = BpmAuxFiles::chartIdentifierForPath(path1);
    
    QString path2 = "/home/user/charts/another_song.mc";
    QString id2 = BpmAuxFiles::chartIdentifierForPath(path2);
    
    return id1 == "song_name" && id2 == "another_song";
}

bool testChartFileSystemRegisterFileType()
{
    ChartFileSystem::Registry::clearRegistrations();
    
    bool ok1 = ChartFileSystem::Registry::registerFileType("test_ext", "Test File", false, nullptr, 50);
    bool ok2 = ChartFileSystem::Registry::registerFileType("test_ext", "Test File Updated", false, nullptr, 60);
    
    auto types = ChartFileSystem::Registry::registeredFileTypes();
    
    return ok1 && ok2 && types.size() == 1 && types.first().description == "Test File Updated";
}

bool testChartFileSystemIsAllowedFile()
{
    ChartFileSystem::Registry::clearRegistrations();
    ChartFileSystem::Registry::registerFileType("mc", "Malody Chart", false, nullptr, 100);
    ChartFileSystem::Registry::registerFileType("ogg", "Audio File", false, nullptr, 90);
    
    bool allowed1 = ChartFileSystem::Registry::isAllowedFile("chart.mc");
    bool allowed2 = ChartFileSystem::Registry::isAllowedFile("audio.ogg");
    bool allowed3 = ChartFileSystem::Registry::isAllowedFile("unknown.xyz");
    
    return allowed1 && allowed2 && !allowed3;
}

bool testChartFileSystemRequiredSidecarExtensions()
{
    ChartFileSystem::Registry::clearRegistrations();
    ChartFileSystem::Registry::registerFileType("curve_tbd.json", "Curve Sidecar", false, nullptr, 80);
    ChartFileSystem::Registry::registerFileType("bpm_excludes.json", "BPM Excludes", true, nullptr, 85);
    ChartFileSystem::Registry::registerFileType("song_bpm.json", "Song BPM", true, nullptr, 85);
    
    QStringList required = ChartFileSystem::Registry::requiredSidecarExtensions();
    
    return required.size() == 2 &&
           required.contains("bpm_excludes.json") &&
           required.contains("song_bpm.json") &&
           !required.contains("curve_tbd.json");
}

bool testChartFileSystemUnregisterFileType()
{
    ChartFileSystem::Registry::clearRegistrations();
    ChartFileSystem::Registry::registerFileType("test_ext", "Test File", false, nullptr, 50);
    
    bool ok = ChartFileSystem::Registry::unregisterFileType("test_ext");
    auto types = ChartFileSystem::Registry::registeredFileTypes();
    
    return ok && types.isEmpty();
}

bool testChartFileSystemClearRegistrations()
{
    ChartFileSystem::Registry::clearRegistrations();
    ChartFileSystem::Registry::registerFileType("test1", "Test1", false, nullptr, 50);
    ChartFileSystem::Registry::registerFileType("test2", "Test2", false, nullptr, 50);
    
    ChartFileSystem::Registry::clearRegistrations();
    auto types = ChartFileSystem::Registry::registeredFileTypes();
    
    return types.isEmpty();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    struct Case
    {
        const char *name;
        bool (*fn)();
    };

    const Case cases[] = {
        {"MathUtils round-trip", &testMathUtilsRoundTrip},
        {"MathUtils cache consistency", &testMathUtilsCacheConsistency},
        {"MathUtils empty BPM boundary", &testMathUtilsEmptyBpmBoundary},
        {"MathUtils zero BPM boundary", &testMathUtilsZeroBpmBoundary},
        {"MathUtils extreme offset boundary", &testMathUtilsExtremeOffsetBoundary},
        {"MathUtils cross-segment round-trip boundary", &testMathUtilsCrossSegmentRoundTripBoundary},
        {"Chart removeNote by id", &testChartRemoveById},
        {"Chart BPM sorting", &testChartBpmSort},
        {"ProjectIO scan + difficulty", &testProjectIoReadDifficultyAndScan},
        {"ProjectIO invalid difficulty json", &testProjectIoGetDifficultyInvalidJsonReturnsEmpty},
        {"ProjectIO find charts missing dir", &testProjectIoFindChartsMissingDirReturnsEmpty},
        {"ProjectIO export missing chart rejected", &testProjectIoExportToMczRejectsMissingChart},
        {"ProjectIO extract missing file rejected", &testProjectIoExtractMczRejectsMissingFile},
        {"ProjectIO invalid json fallback base name", &testProjectIoFindChartsInvalidJsonFallsBackBaseName},
        {"ChartIO load missing file fails", &testChartIoLoadMissingFileFails},
        {"ChartIO load invalid json fails", &testChartIoLoadInvalidJsonFails},
        {"ChartIO save invalid path fails", &testChartIoSaveInvalidPathFails},
        {"Chart clear resets defaults", &testChartClearResetsDefaults},
        {"Chart isValid rules", &testChartIsValidRules},
        {"Note type conversion fallback", &testNoteTypeConversionFallback},
        {"MathUtils snap x grid and boundary", &testMathUtilsSnapXGridAndBoundary},
        {"MathUtils snap time and pixel roundtrip", &testMathUtilsSnapTimeAndPixelRoundTrip},
        {"MathUtils snap note with boundary", &testMathUtilsSnapNoteToTimeWithBoundary},
        {"MathUtils cache before first segment", &testMathUtilsCacheBeforeFirstSegmentConsistency},
        {"MathUtils floatToBeat simplifies fraction", &testMathUtilsFloatToBeatSimplifiesFraction},
        {"MathUtils floatToBeat integral", &testMathUtilsFloatToBeatIntegralCase},
        {"MathUtils isSameTime with snap", &testMathUtilsIsSameTimeWithSnap},
        {"MathUtils beat/pixel guard values", &testMathUtilsBeatPixelGuardValues},
        {"MathUtils snap note reduces fraction", &testMathUtilsSnapNoteToTimeReducesFraction},
        {"MathUtils snapX grid division one", &testMathUtilsSnapXToGridDivisionOne},
        {"ChartIO default BPM fallback", &testChartIoLoadAddsDefaultBpmWhenAllTimeInvalid},
        {"ChartIO meta audio/offset fallback", &testChartIoLoadMetaAudioOffsetFallbackFromSoundNote},
        {"ChartIO clamp x and skip invalid notes", &testChartIoLoadClampsXAndSkipsInvalidNotes},
        {"ChartIO save/load roundtrip core", &testChartIoSaveLoadRoundTripCore},
        {"ChartIO load flat meta fields", &testChartIoLoadFlatMetaFields},
        {"ChartIO mode_ext speed override", &testChartIoLoadModeExtSpeedOverridesFlatSpeed},
        {"ChartIO missing meta keeps defaults", &testChartIoLoadMissingMetaKeepsDefaults},
        {"ChartController applyBatchEdit valid payload", &testChartControllerApplyBatchEditAcceptsValidPayload},
        {"ChartController applyBatchEdit invalid add", &testChartControllerApplyBatchEditRejectsInvalidAddNote},
        {"ChartController applyBatchEdit conflict remove+move", &testChartControllerApplyBatchEditRejectsConflictingMoveAndRemove},
        {"ChartController applyBatchEdit missing move source", &testChartControllerApplyBatchEditRejectsMissingMoveSource},
        {"ChartController applyBatchEdit empty payload", &testChartControllerApplyBatchEditRejectsEmptyPayload},
        {"ChartController applyBatchEdit invalid remove reference", &testChartControllerApplyBatchEditRejectsInvalidRemoveReference},
        {"ChartController applyBatchEdit remove missing note", &testChartControllerApplyBatchEditRejectsRemoveMissingNote},
        {"ChartController applyBatchEdit invalid move target", &testChartControllerApplyBatchEditRejectsInvalidMoveTarget},
        {"ChartController applyBatchEdit duplicated move source", &testChartControllerApplyBatchEditRejectsDuplicatedMoveSource},
        {"ChartController applyBatchEdit oversized batch", &testChartControllerApplyBatchEditRejectsOversizedBatch},
        {"ChartController applyBatchEdit empty action default text", &testChartControllerApplyBatchEditEmptyActionUsesDefaultUndoText},
        {"ChartController applyBatchEdit limit boundary accepted", &testChartControllerApplyBatchEditLimitBoundaryAccepted},
        {"ChartController applyBatchEdit undo redo", &testChartControllerApplyBatchEditUndoRedo},
        {"ChartController moveNotes valid payload", &testChartControllerMoveNotesAcceptsValidPayload},
        {"ChartController moveNotes invalid payload no mutation", &testChartControllerMoveNotesRejectsInvalidPayloadNoMutation},
        {"ChartController moveNotes empty no-op", &testChartControllerMoveNotesEmptyNoOp},
        {"ChartController BPM add undo redo", &testChartControllerBpmAddUndoRedo},
        {"ChartController BPM remove undo redo", &testChartControllerBpmRemoveUndoRedo},
        {"ChartController BPM update undo redo + sort", &testChartControllerBpmUpdateUndoRedoWithSort},
        {"ChartController setMeta undo redo", &testChartControllerSetMetaUndoRedo},
        {"ChartController addNotes undo redo", &testChartControllerAddNotesUndoRedo},
        {"ChartController removeNotes undo redo", &testChartControllerRemoveNotesUndoRedo},
        {"ChartController empty batch commands no-op", &testChartControllerEmptyBatchCommandsNoOp},
        {"ChartController moveNote same input no-op", &testChartControllerMoveNoteSameInputNoOp},
        {"ChartController undo/redo action text lifecycle", &testChartControllerUndoRedoActionTextLifecycle},
        {"ChartController loadChartFromData clears undo stack", &testChartControllerLoadChartFromDataClearsUndoStack},
        {"ChartController applyExternalMutation undo redo", &testChartControllerApplyExternalMutationUndoRedo},
        {"ChartController applyExternalMutation empty action default text", &testChartControllerApplyExternalMutationEmptyActionUsesDefaultUndoText},
        {"ChartController loadChartFromData sets path", &testChartControllerLoadChartFromDataSetsPath},
        {"ChartController loadChart missing file keeps state", &testChartControllerLoadChartMissingFileKeepsState},
        {"ChartController saveChart invalid path fails", &testChartControllerSaveChartInvalidPathFails},
        {"ChartController invalid BPM index no-op", &testChartControllerInvalidBpmIndexNoOp},
        {"Chart remove by content when id missing", &testChartRemoveByContentWhenIdMissing},
        {"Chart sort notes keeps sound after normal on same beat", &testChartSortNotesSoundAfterNormalAtSameBeat},
        {"Note validation boundaries", &testNoteValidationBoundaries},
        {"Note isXValid for sound ignores range", &testNoteIsXValidForSoundIgnoresRange},
        {"KEDAMONO render baseline", &testKedamonoRenderBaseline},
        {"BpmAuxFiles BpmExcludeRange serialization", &testBpmAuxFilesBpmExcludeRangeSerialization},
        {"BpmAuxFiles SongBpmInfo serialization", &testBpmAuxFilesSongBpmInfoSerialization},
        {"BpmAuxFiles BpmExcludesData serialization", &testBpmAuxFilesBpmExcludesDataSerialization},
        {"BpmAuxFiles chartIdentifierForPath", &testBpmAuxFilesChartIdentifierForPath},
        {"ChartFileSystem registerFileType", &testChartFileSystemRegisterFileType},
        {"ChartFileSystem isAllowedFile", &testChartFileSystemIsAllowedFile},
        {"ChartFileSystem requiredSidecarExtensions", &testChartFileSystemRequiredSidecarExtensions},
        {"ChartFileSystem unregisterFileType", &testChartFileSystemUnregisterFileType},
        {"ChartFileSystem clearRegistrations", &testChartFileSystemClearRegistrations},
    };

    int failed = 0;
    for (const Case &c : cases)
    {
        const bool ok = c.fn();
        if (!ok)
        {
            std::fprintf(stderr, "FAILED: %s\n", c.name);
            ++failed;
        }
        else
        {
            std::fprintf(stdout, "PASSED: %s\n", c.name);
        }
    }

    return failed == 0 ? 0 : 1;
}
