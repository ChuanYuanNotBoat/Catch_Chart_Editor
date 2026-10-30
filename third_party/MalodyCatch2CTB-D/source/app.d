module app;

// Standalone Malody Catch (.mc/.mcz) -> osu! Catch (.osz) converter,
// plus CCE Process Plugin API v2 via newline-delimited JSON on stdin/stdout.
// No Python runtime or third-party D dependencies.

import std.algorithm : sort, canFind;
import std.algorithm.mutation : SwapStrategy;
import std.array : join;
import std.conv : to;
import std.file : read, write, exists, isFile, isDir, mkdirRecurse, dirEntries, SpanMode;
import std.format : format;
import std.json : JSONValue, JSONType, parseJSON, toJSON;
import std.math : floor, fabs;
import std.math.traits : isFinite;
import std.path : baseName, dirName, extension, stripExtension, buildPath;
import std.stdio : stdin, stdout, stderr, writeln;
import std.string : strip, toLower, endsWith, startsWith, replace, split;
import std.string : representation;
import std.zip : ZipArchive, ArchiveMember, CompressionMethod;

private enum ulong MAX_ENTRY = 64UL * 1024 * 1024;
private enum ulong MAX_TOTAL = 256UL * 1024 * 1024;
private enum size_t MAX_ITEMS = 4096;

private JSONValue field(JSONValue v, string key) {
    if (v.type != JSONType.object) return JSONValue();
    auto obj = v.object;
    if (auto p = key in obj) return *p;
    return JSONValue();
}

private string str(JSONValue v, string fallback = "") {
    return v.type == JSONType.string ? v.str : fallback;
}

private double num(JSONValue v, double fallback = 0) {
    double r = fallback;
    switch (v.type) {
        case JSONType.integer: r = cast(double) v.integer; break;
        case JSONType.uinteger: r = cast(double) v.uinteger; break;
        case JSONType.float_: r = v.floating; break;
        default: break;
    }
    if (!isFinite(r)) throw new Exception("Non-finite numeric field");
    return r;
}

private bool hasField(JSONValue v, string key) {
    if (v.type != JSONType.object) return false;
    auto obj = v.object;
    return (key in obj) !is null;
}

private JSONValue[] arrayVal(JSONValue v) {
    return v.type == JSONType.array ? v.array : [];
}

// Match Python's default round-half-to-even for integer .osu timestamps.
private long roundEven(double x) {
    if (!isFinite(x) || fabs(x) > 9.0e15) throw new Exception("Timestamp out of range");
    const double f = floor(x);
    const double part = x - f;
    long y = cast(long) f;
    if (part > 0.5 || (part == 0.5 && y % 2 != 0)) ++y;
    return y;
}

private double beatFloat(JSONValue v) {
    auto a = arrayVal(v);
    if (a.length != 3) throw new Exception("Invalid beat triple");
    double den = num(a[2], 1);
    if (den == 0) den = 1;
    return num(a[0]) + num(a[1]) / den;
}

private string safeName(string input, string fallback = "output") {
    string output;
    bool ws = false;
    foreach (char c; strip(input)) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            output ~= '_'; ws = false;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!ws && output.length > 0) output ~= ' ';
            ws = true;
        } else {
            output ~= c;
            ws = false;
        }
    }
    output = strip(output, " .");
    return output.length ? output : fallback;
}

private string normalized(string p) { return replace(p, "\\", "/"); }
private bool allowedZipName(string p) {
    p = normalized(p);
    if (!p.length || p[0] == '/' || p[0] == '\\' || p.length >= 2 && p[1] == ':') return false;
    foreach (part; split(p, "/")) {
        if (part == ".." || part == ".") return false;
    }
    return true;
}
private bool excluded(string p) {
    foreach (part; split(normalized(p), "/"))
        if (toLower(part) == ".mcce-plugin") return true;
    return false;
}
private bool media(string p) {
    auto ext = toLower(extension(p));
    return ext == ".ogg" || ext == ".mp3" || ext == ".wav" ||
           ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
}
private bool audio(string p) {
    auto ext = toLower(extension(p));
    return ext == ".ogg" || ext == ".mp3" || ext == ".wav";
}

private struct Tempo {
    double beat;
    double atMs;
    double bpm;
}
private struct Hit {
    double atMs;
    double endMs;
    int x;
    int kind;
    bool rain;
}
private struct Chart {
    string title, artist, creator, versionName, background, sourceName;
    double speed;
    long offset;
    Tempo[] tempo;
    Hit[] hits;
    string[] sounds;
    int fruit, banana, skipped;
}

private double toMs(const ref Chart chart, double beat) {
    if (!chart.tempo.length) throw new Exception("Missing BPM timeline");
    // Extrapolate backward with first BPM segment, matching its initial anchor.
    Tempo s = chart.tempo[0];
    foreach (segment; chart.tempo) {
        if (beat < segment.beat) break;
        s = segment;
    }
    return s.atMs + (beat - s.beat) * 60000.0 / s.bpm;
}

private Chart parseChart(string raw, string sourceName) {
    if (startsWith(raw, "\xEF\xBB\xBF")) raw = raw[3 .. $];
    auto root = parseJSON(raw);
    auto meta = field(root, "meta");
    if (cast(int) num(field(meta, "mode"), -1) != 3)
        throw new Exception("Non-Catch chart (mode != 3)");
    auto song = field(meta, "song");
    Chart chart;
    chart.sourceName = sourceName;
    chart.title = str(field(song, "title"));
    chart.artist = str(field(song, "artist"));
    chart.creator = str(field(meta, "creator"));
    chart.versionName = str(field(meta, "version"));
    chart.background = str(field(meta, "background"));
    chart.speed = num(field(field(meta, "mode_ext"), "speed"), 5);
    if (chart.speed < -100 || chart.speed > 100) throw new Exception("Invalid speed");
    auto notes = arrayVal(field(root, "note"));
    auto ov = field(meta, "offset");
    if (ov.type == JSONType.null_) {
        foreach (n; notes) {
            if (cast(int) num(field(n,"type"),0) == 1 &&
                field(n,"offset").type != JSONType.null_) {
                ov = field(n,"offset"); break;
            }
        }
    }
    chart.offset = cast(long) num(ov);

    foreach (t; arrayVal(field(root, "time"))) {
        double bpm = num(field(t, "bpm"), 120);
        if (bpm <= 0) bpm = 120;
        chart.tempo ~= Tempo(beatFloat(field(t,"beat")), 0, bpm);
    }
    sort!((a,b) => a.beat < b.beat)(chart.tempo);
    if (!chart.tempo.length) chart.tempo ~= Tempo(0,0,120);
    if (chart.tempo[0].beat > 0)
        chart.tempo = [Tempo(0,0,chart.tempo[0].bpm)] ~ chart.tempo;
    double accumulated = -cast(double) chart.offset;
    foreach (i; 0 .. chart.tempo.length) {
        if (i > 0) {
            auto p = chart.tempo[i-1];
            accumulated += (chart.tempo[i].beat - p.beat) * 60000.0 / p.bpm;
        }
        chart.tempo[i].atMs = accumulated;
    }

    foreach (n; notes) {
        int kind = cast(int) num(field(n, "type"));
        if (kind == 1) {
            string soundName = str(field(n,"sound"));
            if (soundName.length) chart.sounds ~= soundName;
            continue;
        }
        double beat = beatFloat(field(n,"beat"));
        double t0 = toMs(chart, beat);
        bool combo = fabs(beat - cast(double) roundEven(beat)) < 1e-6;
        if (kind == 3) {
            auto ev = field(n, "endbeat");
            double t1 = toMs(chart, ev.type == JSONType.null_ ? beat : beatFloat(ev));
            if (t1 <= t0) { ++chart.skipped; continue; }
            chart.hits ~= Hit(t0, t1, 256, 8 | (combo ? 4 : 0), true);
            ++chart.banana;
        } else {
            if (!hasField(n, "x")) { ++chart.skipped; continue; }
            auto x = cast(int) num(field(n, "x"));
            if (x < 0) x = 0;
            if (x > 512) x = 512;
            chart.hits ~= Hit(t0, t0, x, 1 | (combo ? 4 : 0), false);
            ++chart.fruit;
        }
    }
    sort!((a,b) => a.atMs < b.atMs ||
                (a.atMs == b.atMs && a.endMs < b.endMs), SwapStrategy.stable)(chart.hits);
    return chart;
}

private int dedupe(ref Chart chart) {
    Hit[] kept, group;
    int removed = 0;
    void flushGroup() {
        if (!group.length) return;
        auto survivor = group[0];
        if (group.length > 1) {
            removed += cast(int) group.length - 1;
            survivor.x = group[group.length / 2].x;
        }
        kept ~= survivor;
        group.length = 0;
    }
    foreach (o; chart.hits) {
        if (o.rain) { kept ~= o; continue; }
        if (group.length && o.atMs != group[0].atMs) flushGroup();
        group ~= o;
    }
    flushGroup();
    chart.hits = kept;
    chart.fruit -= removed;
    return removed;
}

private string osuText(const ref Chart chart, string audioFile, string backgroundFile) {
    if (!audioFile.length) throw new Exception("Cannot determine audio file");
    double earliest = 0;
    bool initialized = false;
    foreach (h; chart.hits) {
        if (!initialized || h.atMs < earliest) { earliest = h.atMs; initialized = true; }
    }
    long lead = earliest < 0 ? cast(long)(-earliest + 500) : 0;
    string[] lines = [
        "osu file format v14", "", "[General]",
        "AudioFilename: " ~ audioFile,
        "AudioLeadIn: " ~ to!string(lead),
        "PreviewTime: -1", "Countdown: 0", "SampleSet: Normal",
        "StackLeniency: 0.7", "Mode: 2", "LetterboxInBreaks: 0", "",
        "[Editor]", "DistanceSpacing: 1", "BeatDivisor: 4", "GridSize: 8", "",
        "[Metadata]",
        "Title:" ~ (chart.title.length ? chart.title : "unknown"),
        "TitleUnicode:" ~ (chart.title.length ? chart.title : "unknown"),
        "Artist:" ~ (chart.artist.length ? chart.artist : "unknown"),
        "ArtistUnicode:" ~ (chart.artist.length ? chart.artist : "unknown"),
        "Creator:" ~ (chart.creator.length ? chart.creator : "unknown"),
        "Version:" ~ (chart.versionName.length ? chart.versionName : "Catch"),
        "Source:Malody catch",
        "Tags:malody catch " ~ chart.sourceName,
        "BeatmapID:0", "BeatmapSetID:-1", "", "[Difficulty]",
        "HPDrainRate:5", "CircleSize:3.8", "OverallDifficulty:5",
        "ApproachRate:" ~ format("%s", chart.speed - 0.5),
        "SliderMultiplier:1.8", "SliderTickRate:1", "", "[Events]",
        "//Background and Video events"
    ];
    if (backgroundFile.length) lines ~= "0,0," ~ backgroundFile ~ ",0,0";
    lines ~= ["//Break Periods", "//Storyboard Layer 0 (Background)",
        "//Storyboard Layer 1 (Fail)", "//Storyboard Layer 2 (Pass)",
        "//Storyboard Layer 3 (Foreground)", "//Storyboard Sound Samples",
        "", "[TimingPoints]"];
    foreach (t; chart.tempo)
        lines ~= format("%d,%.6f,4,2,0,100,1,0", roundEven(t.atMs), 60000.0 / t.bpm);
    lines ~= ["", "[HitObjects]"];
    foreach (h; chart.hits) {
        if (h.rain)
            lines ~= format("%d,0,%d,%d,0,%d", h.x, roundEven(h.atMs), h.kind, roundEven(h.endMs));
        else
            lines ~= format("%d,0,%d,%d,0", h.x, roundEven(h.atMs), h.kind);
    }
    return join(lines, "\r\n") ~ "\r\n";
}

private struct Resource {
    string internalName;
    string original;
    string directory;
    string flat;
    ubyte[] content;
}

private void nameResources(ref Resource[] resources) {
    bool[string] taken;
    foreach (ref r; resources) {
        string initial = safeName(r.original, "file");
        string candidate = initial;
        auto ext = extension(initial);
        auto stem = initial[0 .. $-ext.length];
        for (int i = 1; (toLower(candidate) in taken) !is null; ++i)
            candidate = stem ~ "_" ~ to!string(i) ~ ext;
        taken[toLower(candidate)] = true;
        r.flat = candidate;
    }
}

private string matchResource(string refName, string songDir, Resource[] resources) {
    if (!refName.length) return "";
    string leaf = baseName(normalized(refName));
    foreach (r; resources)
        if (r.directory == songDir && (r.original == leaf || r.internalName == normalized(refName))) return r.flat;
    foreach (r; resources)
        if (r.original == leaf) return r.flat;
    return "";
}

private string firstAudio(string songDir, Resource[] resources) {
    foreach (r; resources) if (r.directory == songDir && audio(r.flat)) return r.flat;
    foreach (r; resources) if (audio(r.flat)) return r.flat;
    return "";
}

private string uniqueOutput(string path) {
    if (!exists(path)) return path;
    auto ext = extension(path);
    auto base = path[0 .. $-ext.length];
    for (int i = 1; i < 100000; ++i) {
        string candidate = base ~ "_" ~ to!string(i) ~ ext;
        if (!exists(candidate)) return candidate;
    }
    throw new Exception("Too many output collisions");
}

private void addZipFile(ZipArchive z, string name, ubyte[] bytes) {
    if (!allowedZipName(name) || normalized(name).canFind("/"))
        throw new Exception("Unsafe output member path");
    auto member = new ArchiveMember();
    member.name = name;
    member.expandedData(bytes);
    member.compressionMethod = CompressionMethod.deflate;
    z.addMember(member);
}

private string saveArchive(string src, string outDir, Chart[] charts, string[] chartDirs,
                           Resource[] resources) {
    nameResources(resources);
    auto z = new ZipArchive();
    bool[string] written;
    ulong overall = 0;
    int converted = 0;
    foreach (i, chart; charts) {
        string songDir = chartDirs[i];
        string audioFile;
        foreach (sound; chart.sounds) {
            audioFile = matchResource(sound, songDir, resources);
            if (audioFile.length) break;
        }
        if (!audioFile.length) audioFile = firstAudio(songDir, resources);
        if (!audioFile.length) {
            stderr.writeln("[W1401] No audio for ", chart.sourceName);
            continue;
        }
        string bg = matchResource(chart.background, songDir, resources);
        string name = safeName((chart.artist.length ? chart.artist : "unknown") ~ " - " ~
            (chart.title.length ? chart.title : "unknown") ~ " (" ~
            (chart.creator.length ? chart.creator : "unknown") ~ ") [" ~
            (chart.versionName.length ? chart.versionName : chart.sourceName) ~ "]", chart.sourceName);
        string filename = name ~ ".osu";
        for (int n = 1; (toLower(filename) in written) !is null; ++n)
            filename = name ~ "_" ~ to!string(n) ~ ".osu";
        string output = osuText(chart, audioFile, bg);
        auto bytes = output.dup.representation;
        overall += bytes.length;
        if (overall > MAX_TOTAL) throw new Exception("Output too large");
        addZipFile(z, filename, bytes);
        written[toLower(filename)] = true;
        ++converted;
    }
    if (!converted) throw new Exception("No Catch chart could be converted with audio");
    foreach (r; resources) {
        if ((toLower(r.flat) in written) !is null) continue;
        overall += r.content.length;
        if (overall > MAX_TOTAL) throw new Exception("Output too large");
        addZipFile(z, r.flat, r.content);
        written[toLower(r.flat)] = true;
    }
    mkdirRecurse(outDir);
    string title = charts.length && charts[0].title.length ? charts[0].title : stripExtension(baseName(src));
    string outputPath = uniqueOutput(buildPath(outDir, safeName(title, "beatmap") ~ ".osz"));
    write(outputPath, z.build());
    return outputPath;
}

private string convertZip(string src, string outputDir, bool mergeDuplicates) {
    auto zipped = read(src);
    if (zipped.length > MAX_TOTAL) throw new Exception("Input ZIP too large");
    auto z = new ZipArchive(zipped);
    if (z.totalEntries > MAX_ITEMS) throw new Exception("Too many ZIP entries");
    string[] names;
    foreach (name, m; z.directory) if (allowedZipName(name) && !excluded(name)) names ~= name;
    sort(names);
    string[] chartNames;
    foreach (name; names)
        if (toLower(extension(name)) == ".mc") chartNames ~= name;
    if (!chartNames.length) throw new Exception("No .mc in archive");
    ulong total = 0;
    ubyte[] readMember(string memberName) {
        auto dir = z.directory;
        auto p = memberName in dir;
        if (p is null) throw new Exception("Missing ZIP member");
        if ((*p).expandedSize > MAX_ENTRY) throw new Exception("Oversized ZIP member");
        auto bytes = z.expand(*p);
        total += bytes.length;
        if (total > MAX_TOTAL) throw new Exception("Unpacked ZIP too large");
        return bytes.dup;
    }
    Chart[] charts;
    string[] chartDirs;
    foreach (name; chartNames) {
        try {
            auto chart = parseChart(cast(string) readMember(name), baseName(name));
            if (mergeDuplicates) dedupe(chart);
            charts ~= chart;
            chartDirs ~= normalized(dirName(normalized(name)));
        } catch (Exception e) {
            stderr.writeln("[W1402] Skip ", name, ": ", e.msg);
        }
    }
    if (!charts.length) throw new Exception("No valid Catch chart");
    Resource[] resources;
    foreach (name; names) {
        if (!media(name)) continue;
        string dir = normalized(dirName(normalized(name)));
        bool linked = false;
        foreach (cd; chartDirs) if (cd == dir) { linked = true; break; }
        if (!linked) {
            // Fallback by basename for externally referenced assets.
            foreach (c; charts) {
                if (baseName(normalized(c.background)) == baseName(normalized(name)) && c.background.length)
                    linked = true;
                foreach (sound; c.sounds)
                    if (baseName(normalized(sound)) == baseName(normalized(name))) linked = true;
            }
        }
        if (!linked) continue;
        resources ~= Resource(name, baseName(normalized(name)), dir, "", readMember(name));
    }
    return saveArchive(src, outputDir, charts, chartDirs, resources);
}

private string convertMc(string src, string outDir, bool mergeDuplicates) {
    auto bytes = read(src);
    if (bytes.length > MAX_ENTRY) throw new Exception("Chart too large");
    auto chart = parseChart(cast(string) bytes, baseName(src));
    if (mergeDuplicates) dedupe(chart);
    Resource[] resources;
    auto folder = dirName(src);
    foreach (entry; dirEntries(folder, SpanMode.shallow)) {
        if (!entry.isFile || !media(entry.name)) continue;
        if (entry.size > MAX_ENTRY) continue;
        auto name = baseName(entry.name);
        resources ~= Resource(name, name, ".", "", cast(ubyte[]) read(entry.name));
    }
    return saveArchive(src, outDir, [chart], ["."], resources);
}

private string convert(string input, string outputDir = "", bool mergeDuplicates = false) {
    if (!exists(input) || !isFile(input)) throw new Exception("Input not found: " ~ input);
    auto ext = toLower(extension(input));
    if (!outputDir.length) outputDir = buildPath(dirName(input), "ctb-output");
    if (ext == ".mcz") return convertZip(input, outputDir, mergeDuplicates);
    if (ext == ".mc") return convertMc(input, outputDir, mergeDuplicates);
    throw new Exception("Expected .mc or .mcz");
}

private JSONValue response(JSONValue id, JSONValue result) {
    JSONValue[string] m;
    m["type"] = JSONValue("response");
    m["id"] = id;
    m["result"] = result;
    return JSONValue(m);
}

private JSONValue actions() {
    return parseJSON(`[{"action_id":"convert_current_catch_to_osz",
        "title":"导出 osu! Catch (.osz)",
        "description":"将当前已保存的 Catch 谱面转换为 .osz，输出在谱面旁的 ctb-output 目录；不会修改原谱面。",
        "placement":"tools_menu","requires_undo_snapshot":false}]`);
}

private string chartPath(JSONValue ctx) {
    foreach (key; ["chart_path", "chart_path_native", "chart_path_canonical"]) {
        auto p = str(field(ctx, key));
        if (p.length && exists(p)) return p;
    }
    return "";
}

private void pluginLoop() {
    foreach (line; stdin.byLineCopy()) {
        try {
            auto message = parseJSON(line);
            if (str(field(message, "type")) != "request") continue;
            auto id = field(message, "id");
            auto method = str(field(message, "method"));
            auto payload = field(message, "payload");
            JSONValue result = JSONValue(false);
            if (method == "listToolActions") {
                result = actions();
            } else if (method == "runToolAction") {
                if (str(field(payload, "action_id")) == "convert_current_catch_to_osz") {
                    auto ctx = field(payload, "context");
                    auto path = chartPath(ctx);
                    if (!path.length) throw new Exception("Save the chart before export");
                    // CCE synchronizes the current document to a working copy.
                    // Read that copy, but keep exports beside the user's source
                    // so session cleanup cannot delete the converted archive.
                    auto sourcePath = str(field(ctx, "chart_path_source"));
                    string outputDir;
                    if (sourcePath.length && exists(sourcePath))
                        outputDir = buildPath(dirName(sourcePath), "ctb-output");
                    string target = convert(path, outputDir);
                    stderr.writeln("[I1001] Exported: ", target);
                    result = JSONValue(true);
                }
            }
            auto reply = response(id, result);
            stdout.writeln(toJSON(reply));
            stdout.flush();
        } catch (Exception e) {
            stderr.writeln("[E1501] Plugin request failed: ", e.msg);
            // Try to recover the request ID, but keep stdout protocol-only.
            try {
                auto raw = parseJSON(line);
                if (str(field(raw, "type")) == "request") {
                    auto reply = response(field(raw, "id"), JSONValue(false));
                    stdout.writeln(toJSON(reply));
                    stdout.flush();
                }
            } catch (Exception ignored) { }
        }
    }
}

int main(string[] args) {
    try {
        if (args.length > 1 && args[1] == "--plugin") {
            pluginLoop();
            return 0;
        }
        if (args.length < 2 || args[1] == "--help" || args[1] == "-h") {
            writeln("Usage: malodycatch2ctb-d <file.mc|file.mcz> [output-dir] [--dedupe]");
            writeln("       malodycatch2ctb-d --plugin  (CCE Process Plugin v2)");
            return args.length < 2 ? 1 : 0;
        }
        string outputDir;
        bool mergeDuplicates = false;
        foreach (arg; args[2 .. $]) {
            if (arg == "--dedupe") mergeDuplicates = true;
            else if (!outputDir.length) outputDir = arg;
            else throw new Exception("Unknown argument: " ~ arg);
        }
        writeln(convert(args[1], outputDir, mergeDuplicates));
        return 0;
    } catch (Exception e) {
        stderr.writeln("[E1500] ", e.msg);
        return 2;
    }
}
