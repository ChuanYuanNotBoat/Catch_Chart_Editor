#include "ChartMerger.h"
#include "model/BeatPosition.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace
{
double value(const QJsonArray &b)
{
    if (b.size() != 3 || !b[0].isDouble() || !b[1].isDouble() || !b[2].isDouble() ||
        b[2].toDouble() <= 0 || b[2].toDouble() > 1000000000)
        throw std::runtime_error("Invalid beat triplet");
    for (const auto &v : b)
        if (v.toDouble() != std::floor(v.toDouble()) || std::abs(v.toDouble()) > 1000000000)
            throw std::runtime_error("Invalid beat triplet");
    return BeatPosition(b[0].toInt(), b[1].toInt(), b[2].toInt()).toDouble();
}

QVector<QJsonObject> timing(const QJsonObject &chart)
{
    QVector<QJsonObject> out;
    for (const auto &v : chart["time"].toArray())
    {
        auto t = v.toObject();
        value(t["beat"].toArray());
        if (!(t["bpm"].toDouble() > 0) || !std::isfinite(t["bpm"].toDouble()) ||
            !std::isfinite(t["delay"].toDouble()) || t["delay"].toDouble() < 0)
            throw std::runtime_error("Invalid BPM or delay");
        out.append(t);
    }
    if (out.isEmpty()) throw std::runtime_error("Missing BPM table");
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        const auto x = a["beat"].toArray(), y = b["beat"].toArray();
        return BeatPosition(x[0].toInt(), x[1].toInt(), x[2].toInt()) <
               BeatPosition(y[0].toInt(), y[1].toInt(), y[2].toInt());
    });
    for (int i = 1; i < out.size(); ++i)
        if (value(out[i]["beat"].toArray()) == value(out[i-1]["beat"].toArray()))
            throw std::runtime_error("Duplicate BPM position");
    return out;
}

double at(const QVector<QJsonObject> &times, double beat)
{
    double t = 0;
    for (int i = 0; i < times.size(); ++i)
    {
        const auto &entry = times[i];
        const double start = value(entry["beat"].toArray());
        const double end = i+1 < times.size() ? value(times[i+1]["beat"].toArray()) : beat;
        if (beat <= start) return t + (beat-start)*60/entry["bpm"].toDouble();
        t += entry["delay"].toDouble()/1000;
        t += (std::min(beat, end)-start)*60/entry["bpm"].toDouble();
        if (beat < end || i+1 == times.size()) return t;
    }
    return t;
}

QJsonArray integerBeat(int b) { return {b, 0, 1}; }

// Add a rational translation without changing the source subdivision when the
// translation is integral. Zero-length gaps may require a fractional shift.
QJsonArray shift(const QJsonArray &b, const QJsonArray &from, const QJsonArray &to)
{
    qint64 whole = qint64(b[0].toInt()) + to[0].toInt() - from[0].toInt();
    qint64 n = b[1].toInt(), d = b[2].toInt();
    const auto add = [&](qint64 p, qint64 q) {
        const qint64 g = std::gcd(d, q);
        if (d/g > 1000000000/q) throw std::runtime_error("Beat denominator overflow");
        n = n*(q/g) + p*(d/g); d *= q/g;
        const qint64 reduce = std::gcd(std::abs(n), d); n /= reduce; d /= reduce;
    };
    if (from[1].toInt() != to[1].toInt() || from[2].toInt() != to[2].toInt())
    {
        add(-from[1].toInt(), from[2].toInt());
        add(to[1].toInt(), to[2].toInt());
    }
    if (n < 0)
    {
        const qint64 borrow = (-n+d-1)/d;
        whole -= borrow; n += borrow*d;
    }
    if (whole < 0 || whole > 100000000 || std::abs(n) > 1000000000)
        throw std::runtime_error("Merged chart exceeds supported beat range");
    return {int(whole), int(n), int(d)};
}

QJsonArray firstBeat(const ChartMerger::Source &source)
{
    for (const auto &v : source.json["note"].toArray())
    {
        const auto n = v.toObject();
        if (!n.contains("sound") && value(n["beat"].toArray()) == source.firstBeat)
            return n["beat"].toArray();
    }
    throw std::runtime_error("No playable notes");
}
}

namespace ChartMerger
{
QString resourcePath(const Source &source, const QString &reference)
{
    return QDir::isAbsolutePath(reference) ? reference :
        QDir(QFileInfo(source.path).absolutePath()).filePath(reference);
}

double secondsAt(const QJsonObject &chart, const QJsonArray &beat)
{
    return at(timing(chart), value(beat));
}

std::function<double(const QJsonArray &)> makeTimer(const QJsonObject &chart)
{
    struct Point { double beat, seconds, bpm, delay; };
    QVector<Point> points;
    for (const auto &t : timing(chart))
    {
        const double b = value(t["beat"].toArray());
        const double seconds = points.isEmpty() ? 0 : points.last().seconds+points.last().delay+
            (b-points.last().beat)*60/points.last().bpm;
        points.append({b,seconds,t["bpm"].toDouble(),t["delay"].toDouble()/1000});
    }
    return [points](const QJsonArray &beat) {
        const double b = value(beat);
        auto it = std::upper_bound(points.cbegin(),points.cend(),b,[](double b,const Point &p) { return b < p.beat; });
        if (it != points.cbegin()) --it;
        return it->seconds+(b-it->beat)*60/it->bpm+(b > it->beat ? it->delay : 0);
    };
}

bool load(const QString &path, Source &source, QString *error)
{
    try
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read chart");
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        if (!doc.isObject() || parseError.error != QJsonParseError::NoError)
            throw std::runtime_error("Invalid chart JSON");
        Source s;
        s.path = QFileInfo(path).absoluteFilePath(); s.json = doc.object();
        const auto meta = s.json["meta"].toObject(), song = meta["song"].toObject();
        if (meta["mode"].toInt(-1) != 3) throw std::runtime_error("Only Catch (mode 3) charts can be merged");
        s.title = song["title"].toString(meta["title"].toString());
        s.artist = song["artist"].toString(meta["artist"].toString());
        s.difficulty = meta["version"].toString();
        const auto times = timing(s.json);
        s.minBpm = std::numeric_limits<double>::infinity();
        for (const auto &t : times)
        {
            s.minBpm = std::min(s.minBpm, t["bpm"].toDouble());
            s.maxBpm = std::max(s.maxBpm, t["bpm"].toDouble());
        }
        QString audio = meta["audio"].toString();
        if (audio.isEmpty()) audio = song["file"].toString();
        const auto notes = s.json["note"].toArray();
        if (audio.isEmpty())
            for (const auto &v : notes)
                if (!v.toObject()["sound"].toString().isEmpty()) { audio = v.toObject()["sound"].toString(); break; }
        if (audio.isEmpty()) audio = QFileInfo(path).completeBaseName()+".ogg";
        s.audioPath = resourcePath(s, audio);
        if (!QFileInfo::exists(s.audioPath)) throw std::runtime_error("Chart audio is missing");
        s.firstBeat = std::numeric_limits<double>::infinity(); s.lastBeat = -s.firstBeat;
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto n = notes[i].toObject();
            const double b = value(n["beat"].toArray());
            if (n.contains("sound"))
            {
                ++s.soundCount;
                const auto reference = QFileInfo(resourcePath(s, n["sound"].toString())).canonicalFilePath();
                if (s.primarySoundIndex < 0 && !reference.isEmpty() && reference == QFileInfo(s.audioPath).canonicalFilePath())
                {
                    s.primarySoundIndex = i;
                    const double offset = meta["offset"].toDouble() != 0 ? meta["offset"].toDouble() : n["offset"].toDouble();
                    s.audioOrigin = at(times, b) + offset/1000;
                }
                continue;
            }
            if (!n["x"].isDouble() || n["x"].toDouble() < 0 || n["x"].toDouble() > 512 ||
                (n["type"].toInt() == 3 && !n.contains("endbeat")))
                throw std::runtime_error("Unsupported playable note");
            double end = b;
            if (n.contains("endbeat"))
            {
                ++s.rainCount; end = value(n["endbeat"].toArray());
                if (end < b) throw std::runtime_error("Rain ends before it starts");
            }
            else ++s.normalCount;
            s.firstBeat = std::min(s.firstBeat, b); s.lastBeat = std::max(s.lastBeat, end);
        }
        if (!std::isfinite(s.firstBeat)) throw std::runtime_error("Chart has no playable notes");
        if (s.primarySoundIndex < 0) s.audioOrigin = meta["offset"].toDouble()/1000;
        s.firstSeconds = at(times, s.firstBeat)-s.audioOrigin;
        s.lastSeconds = at(times, s.lastBeat)-s.audioOrigin;
        for (const auto &v : s.json["effect"].toArray())
        {
            const auto e = v.toObject(); value(e["beat"].toArray());
            if (e.contains("endbeat")) value(e["endbeat"].toArray());
        }
        source = s;
        return true;
    }
    catch (const std::exception &e)
    {
        if (error) *error = QObject::tr("%1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
        return false;
    }
}

bool plan(const QVector<Segment> &segments, const QString &title, const QString &artist,
          const QString &creator, const QString &difficulty, Plan &result, QString *error)
{
    try
    {
        if (segments.isEmpty()) throw std::runtime_error("Add at least one chart");
        Plan p;
        QJsonArray notes, times, effects;
        QJsonArray previousEnd = integerBeat(0);
        double previousSeconds = 0;
        const auto putTime = [&](const QJsonObject &t) {
            if (!times.isEmpty() && value(times.last().toObject()["beat"].toArray()) == value(t["beat"].toArray()))
                times.removeLast();
            times.append(t);
        };
        for (int i = 0; i < segments.size(); ++i)
        {
            const auto &s = segments[i]; const auto &src = s.source;
            if (!std::isfinite(s.rate) || s.rate < 0.25 || s.rate > 4 ||
                !std::isfinite(s.gapAfter) || s.gapAfter < 0 || s.gapAfter > 600 ||
                !std::isfinite(s.fadeIn) || !std::isfinite(s.fadeOut) || s.fadeIn < 0 || s.fadeOut < 0)
                throw std::runtime_error("Invalid segment settings");
            Placement place;
            place.firstSeconds = i ? previousSeconds+segments[i-1].gapAfter : std::max(0.0, src.firstSeconds/s.rate);
            place.lastSeconds = place.firstSeconds+(src.lastSeconds-src.firstSeconds)/s.rate;
            place.audioStart = place.firstSeconds-src.firstSeconds/s.rate;
            place.clipStart = i ? place.firstSeconds-segments[i-1].gapAfter/2 : 0;
            place.clipEnd = i+1 < segments.size() ? place.lastSeconds+s.gapAfter/2 : std::numeric_limits<double>::infinity();
            p.placements.append(place);
            const auto from = firstBeat(src);
            auto anchor = from;
            const double gap = place.firstSeconds-previousSeconds;
            if (gap == 0) anchor = previousEnd;
            else
            {
                anchor[0] = int(std::ceil(value(previousEnd))) + (i ? 4 : 0);
                if (value(anchor) <= value(previousEnd) && gap > 0) anchor[0] = anchor[0].toInt()+1;
            }
            if (gap > 0)
                putTime({{"beat", previousEnd}, {"bpm", (value(anchor)-value(previousEnd))*60/gap}});
            const auto sourceTimes = timing(src.json);
            QJsonObject active = sourceTimes.first();
            for (const auto &t : sourceTimes)
                if (value(t["beat"].toArray()) <= src.firstBeat) active = t;
            // A delay at the first note belongs after that note, not before it.
            if (value(active["beat"].toArray()) != src.firstBeat) active.remove("delay");
            active["beat"] = anchor; active["bpm"] = active["bpm"].toDouble()*s.rate;
            if (active.contains("delay")) active["delay"] = active["delay"].toDouble()/s.rate;
            putTime(active);
            for (auto t : sourceTimes)
            {
                const double b = value(t["beat"].toArray());
                if (b <= src.firstBeat || b > src.lastBeat) continue;
                t["beat"] = shift(t["beat"].toArray(), from, anchor);
                t["bpm"] = t["bpm"].toDouble()*s.rate;
                if (t.contains("delay")) t["delay"] = t["delay"].toDouble()/s.rate;
                putTime(t);
            }
            QJsonArray partNotes;
            for (const auto &v : src.json["note"].toArray())
            {
                auto n = v.toObject(); if (n.contains("sound")) continue;
                n["beat"] = shift(n["beat"].toArray(), from, anchor);
                if (n.contains("endbeat")) n["endbeat"] = shift(n["endbeat"].toArray(), from, anchor);
                notes.append(n);
                partNotes.append(n);
                const auto end = n.value("endbeat").toArray(n["beat"].toArray());
                if (value(end) >= value(previousEnd)) previousEnd = end;
            }
            p.segmentNotes.append(partNotes);
            for (const auto &v : src.json["effect"].toArray())
            {
                auto e = v.toObject();
                if (value(e["beat"].toArray()) < src.firstBeat || value(e["beat"].toArray()) > src.lastBeat) continue;
                e["beat"] = shift(e["beat"].toArray(), from, anchor);
                if (e.contains("endbeat")) e["endbeat"] = shift(e["endbeat"].toArray(), from, anchor);
                effects.append(e);
            }
            previousSeconds = place.lastSeconds;
        }
        auto meta = segments.first().source.json["meta"].toObject();
        auto song = meta["song"].toObject();
        song["title"] = title.isEmpty() ? segments.first().source.title : title;
        song["artist"] = artist.isEmpty() ? segments.first().source.artist : artist;
        song.remove("id"); song.remove("file"); song.remove("titleorg"); song.remove("artistorg");
        meta["song"] = song; meta["creator"] = creator;
        meta["version"] = difficulty.trimmed().isEmpty() ? QStringLiteral("Mix") : difficulty;
        meta["audio"] = "merged.ogg"; meta["offset"] = 0; meta["preview"] = 0;
        meta.remove("id"); meta.remove("background"); meta.remove("cover");
        notes.prepend(QJsonObject{{"beat", integerBeat(0)}, {"type", 1}, {"sound", "merged.ogg"}, {"vol", 100}, {"offset", 0}});
        p.chart = {{"meta", meta}, {"time", times}, {"note", notes}, {"effect", effects}};
        result = p; return true;
    }
    catch (const std::exception &e)
    {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}
}
