#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtGlobal>
#include <QSignalSpy>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "file/ProjectIO.h"
#include "file/ChartIO.h"
#include "file/ChartFileSystem.h"
#include "controller/ChartController.h"
#include "editor/NoteChain/NoteChainCurveSampler.h"
#include "editor/NoteChain/NoteChainEditor.h"
#include "editor/NoteChain/NoteChainPersistence.h"
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


    // ---- ChartController signal tests ----

    bool testChartControllerSignalAddNoteEmitsNotesChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.addNote(makeNormalNote(1, 0, 1, 64, "sig-add"));

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1 &&
               bpmSpy.count() == 0 &&
               metaSpy.count() == 0;
    }


    bool testChartControllerSignalRemoveNoteEmitsNotesChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        const Note note = makeNormalNote(1, 0, 1, 64, "sig-remove");
        seed.addNote(note);
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.removeNote(note);

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1 &&
               bpmSpy.count() == 0 &&
               metaSpy.count() == 0;
    }

    bool testChartControllerSignalMoveNoteEmitsNotesChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        const Note original = makeNormalNote(1, 0, 1, 64, "sig-move");
        seed.addNote(original);
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        Note moved = original;
        moved.beatNum = 2;
        controller.moveNote(original, moved);

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1 &&
               bpmSpy.count() == 0 &&
               metaSpy.count() == 0;
    }


    bool testChartControllerSignalAddBpmEmitsBpmListChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.bpmList().clear();
        seed.addBpm(BpmEntry(0, 0, 1, 120.0));
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.addBpm(BpmEntry(4, 0, 1, 180.0));

        return chartSpy.count() == 1 &&
               notesSpy.count() == 0 &&
               bpmSpy.count() == 1 &&
               metaSpy.count() == 0;
    }

    bool testChartControllerSignalRemoveBpmEmitsBpmListChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.bpmList().clear();
        seed.addBpm(BpmEntry(0, 0, 1, 120.0));
        seed.addBpm(BpmEntry(4, 0, 1, 150.0));
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.removeBpm(1);

        return chartSpy.count() == 1 &&
               notesSpy.count() == 0 &&
               bpmSpy.count() == 1 &&
               metaSpy.count() == 0;
    }

    bool testChartControllerSignalUpdateBpmEmitsBpmListChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.bpmList().clear();
        seed.addBpm(BpmEntry(0, 0, 1, 120.0));
        seed.addBpm(BpmEntry(4, 0, 1, 150.0));
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.updateBpm(1, BpmEntry(4, 0, 1, 200.0));

        return chartSpy.count() == 1 &&
               notesSpy.count() == 0 &&
               bpmSpy.count() == 1 &&
               metaSpy.count() == 0;
    }


    bool testChartControllerSignalSetMetaEmitsMetaChangedAndChartChanged()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        MetaData meta;
        meta.title = "Signal Test";
        meta.artist = "Test Artist";
        controller.setMetaData(meta);

        return chartSpy.count() == 1 &&
               notesSpy.count() == 0 &&
               bpmSpy.count() == 0 &&
               metaSpy.count() == 1;
    }

    bool testChartControllerSignalLoadChartFromDataEmitsAllSignals()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        seed.addNote(makeNormalNote(1, 0, 1, 64, "load-test"));

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);
        QSignalSpy loadedSpy(&controller, &ChartController::chartLoaded);

        controller.loadChartFromData("test.mc", seed);

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1 &&
               bpmSpy.count() == 1 &&
               metaSpy.count() == 1 &&
               loadedSpy.count() == 1;
    }

    bool testChartControllerSignalUndoRedoEmitsCorrectSignals()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        seed.addNote(makeNormalNote(1, 0, 1, 64, "undo-test"));
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        controller.addNote(makeNormalNote(2, 0, 1, 128, "added"));

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.undo();
        if (chartSpy.count() != 1 || notesSpy.count() != 1 ||
            bpmSpy.count() != 0 || metaSpy.count() != 0)
            return false;

        chartSpy.clear();
        notesSpy.clear();

        controller.redo();
        return chartSpy.count() == 1 &&
               notesSpy.count() == 1 &&
               bpmSpy.count() == 0 &&
               metaSpy.count() == 0;
    }


    bool testChartControllerSignalExternalMutationEmitsAllSubdividedSignals()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        seed.addNote(makeNormalNote(1, 0, 1, 64, "ext-test"));
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        Chart mutated = seed;
        mutated.addNote(makeNormalNote(3, 0, 1, 256, "ext-add"));

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);
        QSignalSpy metaSpy(&controller, &ChartController::metaDataChanged);

        controller.applyExternalChartMutation("external test", mutated);

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1 &&
               bpmSpy.count() == 1 &&
               metaSpy.count() == 1;
    }

    bool testChartControllerSignalAddNotesEmitsOnce()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);

        Note noteA = makeNormalNote(1, 0, 1, 64, "batch-a");
        Note noteB = makeNormalNote(2, 0, 1, 128, "batch-b");
        controller.addNotes(QVector<Note>{noteA, noteB});

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1;
    }

    bool testChartControllerSignalRemoveNotesEmitsOnce()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        Note noteA = makeNormalNote(1, 0, 1, 64, "batch-a");
        Note noteB = makeNormalNote(2, 0, 1, 128, "batch-b");
        seed.addNote(noteA);
        seed.addNote(noteB);
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);

        controller.removeNotes(QVector<Note>{noteA, noteB});

        return chartSpy.count() == 1 &&
               notesSpy.count() == 1;
    }

    bool testChartControllerSignalNoOpDoesNotEmit()
    {
        ChartController controller;
        Chart seed;
        seed.clearNotes();
        seed.addNote(makeNormalNote(1, 0, 1, 64, "noop-test"));
        if (!controller.loadChartFromData(QString(), seed))
            return false;

        QSignalSpy chartSpy(&controller, &ChartController::chartChanged);
        QSignalSpy notesSpy(&controller, &ChartController::notesChanged);
        QSignalSpy bpmSpy(&controller, &ChartController::bpmListChanged);

        // Empty batch commands should NOT emit any signals.
        controller.addNotes(QVector<Note>{});
        controller.removeNotes(QVector<Note>{});
        controller.moveNotes(QList<QPair<Note, Note>>{});

        return chartSpy.count() == 0 &&
               notesSpy.count() == 0 &&
               bpmSpy.count() == 0;
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

        auto byBeat = [&cache](int lhs, int rhs)
        {
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
                [&cache](int idx, double beatValue)
                {
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
                [&cache](int idx, double beatValue)
                {
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

        std::sort(notes.begin(), notes.end(), [](const Note &a, const Note &b)
                  {
        const double beatA = MathUtils::beatToFloat(a.beatNum, a.numerator, a.denominator);
        const double beatB = MathUtils::beatToFloat(b.beatNum, b.numerator, b.denominator);
        if (beatA == beatB)
            return a.x < b.x;
        return beatA < beatB; });

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

    bool testChartFileSystemRegisterFileType()
    {
        ChartFileSystem::ChartFileSystemRegistry::clearRegistrations();

        bool ok1 = ChartFileSystem::ChartFileSystemRegistry::registerFileType("test_ext", "Test File", false, nullptr, 50);
        bool ok2 = ChartFileSystem::ChartFileSystemRegistry::registerFileType("test_ext", "Test File Updated", false, nullptr, 60);

        auto types = ChartFileSystem::ChartFileSystemRegistry::registeredFileTypes();

        return ok1 && ok2 && types.size() == 1 && types.first().description == "Test File Updated";
    }

    bool testChartFileSystemIsAllowedFile()
    {
        ChartFileSystem::ChartFileSystemRegistry::clearRegistrations();
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("mc", "Malody Chart", false, nullptr, 100);
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("ogg", "Audio File", false, nullptr, 90);

        bool allowed1 = ChartFileSystem::ChartFileSystemRegistry::isAllowedFile("chart.mc");
        bool allowed2 = ChartFileSystem::ChartFileSystemRegistry::isAllowedFile("audio.ogg");
        bool allowed3 = ChartFileSystem::ChartFileSystemRegistry::isAllowedFile("unknown.xyz");

        return allowed1 && allowed2 && !allowed3;
    }

    bool testChartFileSystemRequiredSidecarExtensions()
    {
        ChartFileSystem::ChartFileSystemRegistry::clearRegistrations();
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("curve_tbd.json", "Curve Sidecar", false, nullptr, 80);
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("bpm_excludes.json", "BPM Excludes", true, nullptr, 85);
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("song_bpm.json", "Song BPM", true, nullptr, 85);

        QStringList required = ChartFileSystem::ChartFileSystemRegistry::requiredSidecarExtensions();

        return required.size() == 2 &&
               required.contains("bpm_excludes.json") &&
               required.contains("song_bpm.json") &&
               !required.contains("curve_tbd.json");
    }

    bool testChartFileSystemUnregisterFileType()
    {
        ChartFileSystem::ChartFileSystemRegistry::clearRegistrations();
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("test_ext", "Test File", false, nullptr, 50);

        bool ok = ChartFileSystem::ChartFileSystemRegistry::unregisterFileType("test_ext");
        auto types = ChartFileSystem::ChartFileSystemRegistry::registeredFileTypes();

        return ok && types.isEmpty();
    }

    bool testChartFileSystemClearRegistrations()
    {
        ChartFileSystem::ChartFileSystemRegistry::clearRegistrations();
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("test1", "Test1", false, nullptr, 50);
        ChartFileSystem::ChartFileSystemRegistry::registerFileType("test2", "Test2", false, nullptr, 50);

        ChartFileSystem::ChartFileSystemRegistry::clearRegistrations();
        auto types = ChartFileSystem::ChartFileSystemRegistry::registeredFileTypes();

        return types.isEmpty();
    }

    bool testNoteChainSidecarSaveUpdatesRevisionAndRejectsStaleWrite()
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid())
            return false;

        const QString sidecarPath = tempDir.filePath("curve_tbd.json");
        NoteChain::NoteChainState state;
        state.appendAnchor(128.0, 1.25);
        state.setProjectDirty(true);
        if (!NoteChain::NoteChainPersistence::saveToFile(state, sidecarPath))
            return false;
        if (state.projectRevision() != 1 || state.projectFileUuid().isEmpty() || state.projectDirty())
            return false;

        state.appendAnchor(256.0, 2.5);
        state.setProjectDirty(true);
        if (!NoteChain::NoteChainPersistence::saveToFile(state, sidecarPath))
            return false;
        if (state.projectRevision() != 2 || state.projectDirty())
            return false;

        NoteChain::NoteChainState staleState;
        staleState.setProjectRevision(1);
        staleState.setProjectDirty(true);
        QString error;
        return !NoteChain::NoteChainPersistence::saveToFile(staleState, sidecarPath, &error)
               && error.contains("revision conflict");
    }

    bool testNoteChainLoadsPythonLegacyAnchorsAndHandles()
    {
        const QJsonObject firstAnchor{
            {"id", 7},
            {"lane_x", 96.0},
            {"beat", QJsonArray{2, 1, 4}},
            {"in", QJsonArray{-32.0, -0.5}},
            {"out", QJsonObject{{"lane_dx", 48.0}, {"beat_delta", QJsonArray{0, 3, 4}}}},
            {"smooth", false}};
        const QJsonObject secondAnchor{
            {"id", 9},
            {"lane_x", 256.0},
            {"beat", QJsonArray{3, 0, 1}},
            {"in", QJsonArray{-24.0, -0.25}},
            {"out", QJsonArray{24.0, 0.25}}};
        QJsonArray legacyLinks;
        legacyLinks.append(QJsonValue(QJsonArray{7, 9}));
        const QJsonObject legacyProject{
            {"anchors", QJsonArray{firstAnchor, secondAnchor}},
            {"links", legacyLinks},
            {"segment_denominators", QJsonObject{{"7:9", 12}}},
            {"segment_shapes", QJsonObject{{"7:9", "polyline"}}}};

        NoteChain::NoteChainState state;
        if (!NoteChain::NoteChainPersistence::deserialize(legacyProject, state))
            return false;
        if (state.anchors().size() != 2 || state.linksAll().size() != 1)
            return false;
        const NoteChain::Anchor &anchor = state.anchorAt(0);
        return nearlyEqual(anchor.beat, 2.25)
               && nearlyEqual(anchor.inDx, -32.0)
               && nearlyEqual(anchor.inDy, -0.5)
               && nearlyEqual(anchor.outDx, 48.0)
               && nearlyEqual(anchor.outDy, 0.75)
               && !anchor.smooth
               && state.segmentDen(7, 9) == 12
               && state.segmentShape(7, 9) == QStringLiteral("polyline");
    }

    bool testNoteChainAnchorInsertionOrderAndDefaultHandles()
    {
        NoteChain::NoteChainState state;
        const int laterIndex = state.appendAnchor(320.0, 4.0);
        const int laterId = state.anchorAt(laterIndex).id;
        state.appendAnchor(64.0, 1.0);
        const int middleIndex = state.appendAnchor(192.0, 2.0);

        if (state.anchors().size() != 3
            || !nearlyEqual(state.anchorAt(0).beat, 1.0)
            || !nearlyEqual(state.anchorAt(1).beat, 2.0)
            || !nearlyEqual(state.anchorAt(2).beat, 4.0)
            || state.anchorAt(2).id != laterId
            || middleIndex != 1) {
            return false;
        }

        const NoteChain::Anchor &middle = state.anchorAt(middleIndex);
        return nearlyEqual(middle.inDx, -32.0)
            && nearlyEqual(middle.outDx, 32.0)
            && nearlyEqual(middle.inDy, -0.25)
            && nearlyEqual(middle.outDy, 0.25);
    }

    bool testNoteChainTripletPreservesRequestedDenominator()
    {
        const QVector<int> triplet = NoteChain::floatBeatToTriplet(2.5, 12);
        const QVector<int> rounded = NoteChain::floatBeatToTriplet(1.0 + 5.0 / 24.0, 24);
        return triplet == QVector<int>({2, 6, 12})
            && rounded == QVector<int>({1, 5, 24})
            && nearlyEqual(NoteChain::tripletToFloat(rounded), 1.0 + 5.0 / 24.0);
    }

    bool testNoteChainNormalizesNonMonotonicBezierSamples()
    {
        NoteChain::Anchor first;
        first.laneX = 32.0;
        first.beat = 0.0;
        first.outDx = 160.0;
        first.outDy = 4.0;
        NoteChain::Anchor second;
        second.laneX = 480.0;
        second.beat = 2.0;
        second.inDx = -160.0;
        second.inDy = -4.0;

        const QVector<NoteChain::SampledPoint> raw =
            NoteChain::sampleSegment(first, second, QStringLiteral("curve"), 64);
        bool hasBeatReversal = false;
        for (int i = 1; i < raw.size(); ++i)
            hasBeatReversal = hasBeatReversal || raw[i].beat < raw[i - 1].beat;
        if (!hasBeatReversal)
            return false;

        const QVector<NoteChain::SampledPoint> normalized = NoteChain::normalizeSamplesByBeat(raw);
        if (normalized.size() < 2)
            return false;
        for (int i = 1; i < normalized.size(); ++i) {
            if (normalized[i].beat <= normalized[i - 1].beat)
                return false;
        }

        const double probeBeat = (normalized.first().beat + normalized.last().beat) * 0.5;
        const double laneX = NoteChain::laneXAtBeat(normalized, probeBeat);
        return std::isfinite(laneX) && laneX >= 0.0 && laneX <= NoteChain::Const::kLaneWidth;
    }

    bool testNoteChainV3MetadataAndDensityRoundTrip()
    {
        NoteChain::NoteChainState source;
        NoteChain::Anchor first;
        first.id = 11;
        first.laneX = 80.0;
        first.beat = 1.25;
        NoteChain::Anchor second;
        second.id = 22;
        second.laneX = 400.0;
        second.beat = 3.5;
        source.insertAnchor(first);
        source.insertAnchor(second);
        source.addLink(11, 22);

        NoteChain::NodePersistenceMeta nodeMeta;
        nodeMeta.groupIds = {2, 7};
        nodeMeta.reserved = QJsonObject{{"python_node_data", "keep"}};
        source.setNodeMeta(11, nodeMeta);

        NoteChain::CurvePersistenceMeta curveMeta;
        curveMeta.curveId = 43;
        curveMeta.curveNo = 5;
        curveMeta.groupIds = {3, 9};
        curveMeta.specialJoystickReserved = QJsonObject{{"axis", "x"}};
        curveMeta.reserved = QJsonObject{{"python_curve_data", 17}};
        source.setCurveMeta(11, 22, curveMeta);
        source.setDensityMode(11, 22, 0);

        NoteChain::GroupPersistenceMeta nodeGroup;
        nodeGroup.id = 2;
        nodeGroup.name = QStringLiteral("nodes");
        nodeGroup.reserved = QJsonObject{{"color", "blue"}};
        source.setNodeGroups({nodeGroup});
        NoteChain::GroupPersistenceMeta curveGroup;
        curveGroup.id = 3;
        curveGroup.name = QStringLiteral("curves");
        curveGroup.reserved = QJsonObject{{"color", "gold"}};
        source.setCurveGroups({curveGroup});

        NoteChain::NoteChainState loaded;
        if (!NoteChain::NoteChainPersistence::deserialize(
                NoteChain::NoteChainPersistence::serialize(source), loaded)) {
            return false;
        }
        if (!(loaded.nodeMeta(11) == nodeMeta)
            || !(loaded.curveMeta(11, 22) == curveMeta)
            || loaded.nodeGroups() != QVector<NoteChain::GroupPersistenceMeta>{nodeGroup}
            || loaded.curveGroups() != QVector<NoteChain::GroupPersistenceMeta>{curveGroup}
            || loaded.segmentDensityMode(11, 22) != 0) {
            return false;
        }

        loaded.setSegmentDen(11, 22, 24);
        NoteChain::NoteChainState fixedLoaded;
        return NoteChain::NoteChainPersistence::deserialize(
                   NoteChain::NoteChainPersistence::serialize(loaded), fixedLoaded)
            && fixedLoaded.segmentDensityMode(11, 22) == 24
            && fixedLoaded.segmentDen(11, 22) == 24;
    }

    bool testNoteChainInvalidCurveIdentityFallsBackToGeneratedValues()
    {
        QJsonArray nodes;
        nodes.append(QJsonObject{{"node_id", 1}, {"lane_x", 64.0}, {"beat", QJsonArray{0, 0, 1}}});
        nodes.append(QJsonObject{{"node_id", 2}, {"lane_x", 448.0}, {"beat", QJsonArray{1, 0, 1}}});
        QJsonArray curves;
        curves.append(QJsonObject{{"curve_id", 0}, {"curve_no", -3},
                                  {"node_ids", QJsonArray{1, 2}}});
        NoteChain::NoteChainState state;
        if (!NoteChain::NoteChainPersistence::deserialize(
                QJsonObject{{"format_version", 3}, {"nodes", nodes}, {"curves", curves}}, state)) {
            return false;
        }
        const NoteChain::CurvePersistenceMeta meta = state.curveMeta(1, 2);
        return meta.curveId > 0 && meta.curveNo > 0;
    }

    bool testNoteChainBrokenPayloadDoesNotReplaceState()
    {
        NoteChain::NoteChainState state;
        state.appendAnchor(144.0, 2.0);
        const NoteChain::StateSnapshot before = state.captureSnapshot();

        const QJsonObject broken{
            {"anchors", QJsonArray{
                QJsonObject{{"id", 7}, {"lane_x", 64.0}, {"beat", QJsonArray{1, 0, 1}}},
                QJsonObject{{"id", 9}, {"beat", QJsonArray{2, 0, 1}}}}}};
        QString error;
        return !NoteChain::NoteChainPersistence::deserialize(broken, state, &error)
            && !error.isEmpty()
            && state.captureSnapshot() == before;
    }

    bool testNoteChainEditorFailedProjectSwitchIsTransactional()
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid())
            return false;

        const QString validPath = tempDir.filePath(QStringLiteral("valid.curve_tbd.json"));
        NoteChain::NoteChainState source;
        source.appendAnchor(216.0, 2.75);
        if (!NoteChain::NoteChainPersistence::saveToFile(source, validPath))
            return false;

        NoteChain::NoteChainEditor editor;
        if (!editor.loadProject(validPath))
            return false;
        const NoteChain::StateSnapshot before = editor.state().captureSnapshot();

        const QString brokenPath = tempDir.filePath(QStringLiteral("broken.curve_tbd.json"));
        QFile brokenFile(brokenPath);
        if (!brokenFile.open(QIODevice::WriteOnly)
            || brokenFile.write("{ invalid json") <= 0) {
            return false;
        }
        brokenFile.close();

        return !editor.loadProject(brokenPath)
            && editor.currentSidecarPath() == validPath
            && editor.state().captureSnapshot() == before;
    }

    bool testNoteChainHostNoteSelectionSynchronizesNearestAnchors()
    {
        NoteChain::NoteChainEditor editor;
        const int firstIndex = editor.state().appendAnchor(64.0, 1.0);
        const int firstId = editor.state().anchorAt(firstIndex).id;
        const int secondIndex = editor.state().appendAnchor(448.0, 6.0);
        const int secondId = editor.state().anchorAt(secondIndex).id;
        editor.setSelectNotesEnabled(true);

        QVariantMap selectedNote;
        selectedNote.insert(QStringLiteral("id"), QStringLiteral("note-b"));
        selectedNote.insert(QStringLiteral("lane_x"), 430.0);
        selectedNote.insert(QStringLiteral("beat"), 5.75);
        QVariantMap context;
        context.insert(QStringLiteral("selected_note_ids"), QVariantList{QStringLiteral("note-b")});
        context.insert(QStringLiteral("selected_notes"), QVariantList{selectedNote});
        editor.setHostContext(context);
        if (!editor.state().isAnchorSelected(secondId)
            || editor.state().isAnchorSelected(firstId)) {
            return false;
        }

        editor.setSelectNotesEnabled(false);
        editor.state().setSingleSelectedAnchor(firstId);
        context.insert(QStringLiteral("selected_note_ids"), QVariantList{QStringLiteral("note-c")});
        editor.setHostContext(context);
        return editor.state().isAnchorSelected(firstId)
            && !editor.state().isAnchorSelected(secondId);
    }

    bool testChartControllerUndoMarkerTextLifecycle()
    {
        ChartController controller;
        controller.pushUndoMarker(QStringLiteral("Plugin Curve Edit: Move Anchor"));
        if (!controller.canUndo()
            || controller.nextUndoActionText() != QStringLiteral("Plugin Curve Edit: Move Anchor")
            || controller.canRedo()) {
            return false;
        }
        controller.undo();
        if (controller.canUndo() || !controller.canRedo()
            || controller.nextRedoActionText() != QStringLiteral("Plugin Curve Edit: Move Anchor")) {
            return false;
        }
        controller.redo();
        return controller.canUndo() && !controller.canRedo()
            && controller.nextUndoActionText() == QStringLiteral("Plugin Curve Edit: Move Anchor");
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
        {"ChartController signal addNote emits notesChanged+chartChanged", &testChartControllerSignalAddNoteEmitsNotesChangedAndChartChanged},
        {"ChartController signal removeNote emits notesChanged+chartChanged", &testChartControllerSignalRemoveNoteEmitsNotesChangedAndChartChanged},
        {"ChartController signal moveNote emits notesChanged+chartChanged", &testChartControllerSignalMoveNoteEmitsNotesChangedAndChartChanged},
        {"ChartController signal addBpm emits bpmListChanged+chartChanged", &testChartControllerSignalAddBpmEmitsBpmListChangedAndChartChanged},
        {"ChartController signal removeBpm emits bpmListChanged+chartChanged", &testChartControllerSignalRemoveBpmEmitsBpmListChangedAndChartChanged},
        {"ChartController signal updateBpm emits bpmListChanged+chartChanged", &testChartControllerSignalUpdateBpmEmitsBpmListChangedAndChartChanged},
        {"ChartController signal setMeta emits metaDataChanged+chartChanged", &testChartControllerSignalSetMetaEmitsMetaChangedAndChartChanged},
        {"ChartController signal loadChartFromData emits all 5 signals", &testChartControllerSignalLoadChartFromDataEmitsAllSignals},
        {"ChartController signal undo+redo emits correct signals", &testChartControllerSignalUndoRedoEmitsCorrectSignals},
        {"ChartController signal externalMutation emits all subdivided signals", &testChartControllerSignalExternalMutationEmitsAllSubdividedSignals},
        {"ChartController signal addNotes batch emits once", &testChartControllerSignalAddNotesEmitsOnce},
        {"ChartController signal removeNotes batch emits once", &testChartControllerSignalRemoveNotesEmitsOnce},
        {"ChartController signal no-op does not emit", &testChartControllerSignalNoOpDoesNotEmit},
        {"ChartController invalid BPM index no-op", &testChartControllerInvalidBpmIndexNoOp},
        {"Chart remove by content when id missing", &testChartRemoveByContentWhenIdMissing},
        {"Chart sort notes keeps sound after normal on same beat", &testChartSortNotesSoundAfterNormalAtSameBeat},
        {"Note validation boundaries", &testNoteValidationBoundaries},
        {"Note isXValid for sound ignores range", &testNoteIsXValidForSoundIgnoresRange},
        {"KEDAMONO render baseline", &testKedamonoRenderBaseline},
        {"ChartFileSystem registerFileType", &testChartFileSystemRegisterFileType},
        {"ChartFileSystem isAllowedFile", &testChartFileSystemIsAllowedFile},
        {"ChartFileSystem requiredSidecarExtensions", &testChartFileSystemRequiredSidecarExtensions},
        {"ChartFileSystem unregisterFileType", &testChartFileSystemUnregisterFileType},
        {"ChartFileSystem clearRegistrations", &testChartFileSystemClearRegistrations},
        {"NoteChain sidecar revision + CAS", &testNoteChainSidecarSaveUpdatesRevisionAndRejectsStaleWrite},
        {"NoteChain Python legacy handles", &testNoteChainLoadsPythonLegacyAnchorsAndHandles},
        {"NoteChain anchor order + default handles", &testNoteChainAnchorInsertionOrderAndDefaultHandles},
        {"NoteChain triplet denominator preservation", &testNoteChainTripletPreservesRequestedDenominator},
        {"NoteChain non-monotonic Bezier normalization", &testNoteChainNormalizesNonMonotonicBezierSamples},
        {"NoteChain V3 metadata + density round-trip", &testNoteChainV3MetadataAndDensityRoundTrip},
        {"NoteChain invalid curve identity fallback", &testNoteChainInvalidCurveIdentityFallsBackToGeneratedValues},
        {"NoteChain broken payload preserves state", &testNoteChainBrokenPayloadDoesNotReplaceState},
        {"NoteChain failed project switch is transactional", &testNoteChainEditorFailedProjectSwitchIsTransactional},
        {"NoteChain host note selection sync", &testNoteChainHostNoteSelectionSynchronizesNearestAnchors},
        {"ChartController undo marker text lifecycle", &testChartControllerUndoMarkerTextLifecycle},
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
