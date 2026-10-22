#include "ChartDelta.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace CceDocument
{
namespace
{
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool noteEqual(const Note &a, const Note &b)
{
    return a.beatNum == b.beatNum && a.numerator == b.numerator && a.denominator == b.denominator && a.type == b.type
           && a.x == b.x && a.isRain == b.isRain && a.endBeatNum == b.endBeatNum && a.endNumerator == b.endNumerator
           && a.endDenominator == b.endDenominator && a.sound == b.sound && a.sound.isNull() == b.sound.isNull()
           && a.vol == b.vol && a.offset == b.offset;
}

struct Key
{
    Note note;
    bool operator==(const Key &other) const
    {
        return noteEqual(note, other.note);
    }
    friend size_t qHash(const Key &key, size_t seed = 0) noexcept
    {
        const auto &n = key.note;
        return qHashMulti(seed, n.beatNum, n.numerator, n.denominator, int(n.type), n.x, n.isRain, n.endBeatNum,
                          n.endNumerator, n.endDenominator, n.sound, n.vol, n.offset);
    }
};

QJsonValue stringJson(const QString &value)
{
    return value.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

QJsonObject metaJson(const MetaData &m)
{
    return {{"title", stringJson(m.title)},
            {"titleOrg", stringJson(m.titleOrg)},
            {"artist", stringJson(m.artist)},
            {"artistOrg", stringJson(m.artistOrg)},
            {"difficulty", stringJson(m.difficulty)},
            {"chartAuthor", stringJson(m.chartAuthor)},
            {"audioFile", stringJson(m.audioFile)},
            {"backgroundFile", stringJson(m.backgroundFile)},
            {"previewTime", m.previewTime},
            {"firstBpm", m.firstBpm},
            {"offset", m.offset},
            {"speed", m.speed}};
}

QJsonObject noteJson(const Note &n)
{
    return {{"beatNum", n.beatNum},
            {"numerator", n.numerator},
            {"denominator", n.denominator},
            {"type", int(n.type)},
            {"x", n.x},
            {"isRain", n.isRain},
            {"endBeatNum", n.endBeatNum},
            {"endNumerator", n.endNumerator},
            {"endDenominator", n.endDenominator},
            {"sound", stringJson(n.sound)},
            {"vol", n.vol},
            {"offset", n.offset}};
}

QJsonArray notesJson(const QVector<Note> &notes)
{
    QJsonArray result;
    for (const auto &n : notes)
        result.append(noteJson(n));
    return result;
}

QJsonArray timingJson(const QVector<BpmEntry> &timing)
{
    QJsonArray result;
    for (const auto &b : timing)
        result.append(QJsonObject{
            {"beatNum", b.beatNum}, {"numerator", b.numerator}, {"denominator", b.denominator}, {"bpm", b.bpm}});
    return result;
}

bool timingEqual(const QVector<BpmEntry> &a, const QVector<BpmEntry> &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].beatNum != b[i].beatNum || a[i].numerator != b[i].numerator || a[i].denominator != b[i].denominator
            || a[i].bpm != b[i].bpm)
            return false;
    return true;
}

bool fields(const QJsonObject &object, const QStringList &names)
{
    if (object.size() != names.size())
        return false;
    for (const auto &name : names)
        if (!object.contains(name))
            return false;
    return true;
}

bool integer(const QJsonValue &v, int &out)
{
    if (!v.isDouble())
        return false;
    const double value = v.toDouble();
    if (!std::isfinite(value) || std::floor(value) != value || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max())
        return false;
    out = int(value);
    return true;
}

bool finite(const QJsonValue &v, double &out)
{
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        return false;
    out = v.toDouble();
    return true;
}

bool textField(const QJsonObject &o, const QString &key, QString &out)
{
    if (o.value(key).isNull())
    {
        out = QString();
        return true;
    }
    if (!o.value(key).isString())
        return false;
    out = o.value(key).toString();
    return true;
}

bool readNote(const QJsonObject &o, Note &n)
{
    int type = 0;
    if (!fields(o, {"beatNum", "numerator", "denominator", "type", "x", "isRain", "endBeatNum", "endNumerator",
                    "endDenominator", "sound", "vol", "offset"})
        || !integer(o.value("beatNum"), n.beatNum) || !integer(o.value("numerator"), n.numerator)
        || !integer(o.value("denominator"), n.denominator) || !integer(o.value("type"), type)
        || (type != 0 && type != 1 && type != 3) || !integer(o.value("x"), n.x) || !o.value("isRain").isBool()
        || !integer(o.value("endBeatNum"), n.endBeatNum) || !integer(o.value("endNumerator"), n.endNumerator)
        || !integer(o.value("endDenominator"), n.endDenominator) || !textField(o, "sound", n.sound)
        || !integer(o.value("vol"), n.vol) || !integer(o.value("offset"), n.offset))
        return false;
    n.type = NoteType(type);
    n.isRain = o.value("isRain").toBool();
    return true;
}

bool readNotes(const QJsonValue &v, QVector<Note> &notes)
{
    if (!v.isArray())
        return false;
    for (const auto &item : v.toArray())
    {
        Note note;
        if (!item.isObject() || !readNote(item.toObject(), note))
            return false;
        notes.append(std::move(note));
    }
    return true;
}

bool readMeta(const QJsonObject &o, MetaData &m)
{
    return fields(o, {"title", "titleOrg", "artist", "artistOrg", "difficulty", "chartAuthor", "audioFile",
                      "backgroundFile", "previewTime", "firstBpm", "offset", "speed"})
           && textField(o, "title", m.title) && textField(o, "titleOrg", m.titleOrg) && textField(o, "artist", m.artist)
           && textField(o, "artistOrg", m.artistOrg) && textField(o, "difficulty", m.difficulty)
           && textField(o, "chartAuthor", m.chartAuthor) && textField(o, "audioFile", m.audioFile)
           && textField(o, "backgroundFile", m.backgroundFile) && integer(o.value("previewTime"), m.previewTime)
           && finite(o.value("firstBpm"), m.firstBpm) && integer(o.value("offset"), m.offset)
           && integer(o.value("speed"), m.speed);
}

bool readTiming(const QJsonValue &v, QVector<BpmEntry> &entries)
{
    if (!v.isArray())
        return false;
    for (const auto &item : v.toArray())
    {
        BpmEntry b;
        const auto o = item.toObject();
        if (!item.isObject() || !fields(o, {"beatNum", "numerator", "denominator", "bpm"})
            || !integer(o.value("beatNum"), b.beatNum) || !integer(o.value("numerator"), b.numerator)
            || !integer(o.value("denominator"), b.denominator) || !finite(o.value("bpm"), b.bpm))
            return false;
        entries.append(b);
    }
    return true;
}
} // namespace

bool ChartDelta::between(const Chart &before, const Chart &after, ChartDelta &delta, QString *error)
{
    if (before.notes().size() > std::numeric_limits<int>::max()
        || after.notes().size() > std::numeric_limits<int>::max())
        return fail(error, "Chart is too large.");
    ChartDelta next;
    if (metaJson(before.meta()) != metaJson(after.meta()))
        next.m_meta = Replace<MetaData>{before.meta(), after.meta()};
    if (!timingEqual(before.bpmList(), after.bpmList()))
        next.m_timing = Replace<QVector<BpmEntry>>{before.bpmList(), after.bpmList()};
    if (before.audioSourceFullPath() != after.audioSourceFullPath()
        || before.audioSourceFullPath().isNull() != after.audioSourceFullPath().isNull())
        next.m_audioSource = Replace<QString>{before.audioSourceFullPath(), after.audioSourceFullPath()};

    const auto &a = before.notes(), &b = after.notes();
    auto rememberHandle = [&](int ai, int bi) {
        if (a[ai].id != b[bi].id || a[ai].id.isNull() != b[bi].id.isNull())
            next.m_handles.append({ai, bi, a[ai].id, b[bi].id});
    };
    int prefix = 0;
    if (a.size() == b.size() && a.constData() == b.constData())
        prefix = int(a.size());
    else
        while (prefix < a.size() && prefix < b.size() && noteEqual(a[prefix], b[prefix]))
        {
            rememberHandle(prefix, prefix);
            ++prefix;
        }
    const bool equal = a.size() == b.size() && prefix == a.size();
    if (!equal)
    {
        int aEnd = int(a.size()), bEnd = int(b.size());
        while (aEnd > prefix && bEnd > prefix && noteEqual(a[aEnd - 1], b[bEnd - 1]))
        {
            --aEnd;
            --bEnd;
            rememberHandle(aEnd, bEnd);
        }
        // Match occurrences, then keep an increasing subsequence of unchanged
        // objects. A long-distance move retains two images, not the entire gap.
        // Match by data even when a project decode regenerated every handle.
        struct Occurrences
        {
            QVector<int> positions;
            int consumed = 0;
        };
        QHash<Key, Occurrences> positions;
        positions.reserve(aEnd - prefix);
        for (int i = prefix; i < aEnd; ++i)
            positions[Key{a[i]}].positions.append(i);
        struct Match
        {
            int before, after, predecessor;
        };
        QVector<Match> matches;
        QVector<int> tails, tailMatches;
        for (int j = prefix; j < bEnd; ++j)
        {
            auto found = positions.find(Key{b[j]});
            if (found == positions.end() || found->consumed == found->positions.size())
                continue;
            const int i = found->positions[found->consumed++];
            auto tail = std::lower_bound(tails.begin(), tails.end(), i);
            const int k = int(tail - tails.begin());
            const int predecessor = k ? tailMatches[k - 1] : -1;
            matches.append({i, j, predecessor});
            if (tail == tails.end())
            {
                tails.append(i);
                tailMatches.append(int(matches.size() - 1));
            }
            else
            {
                *tail = i;
                tailMatches[k] = int(matches.size() - 1);
            }
        }
        QVector<QPair<int, int>> anchors;
        for (int index = tailMatches.isEmpty() ? -1 : tailMatches.last(); index >= 0;
             index = matches[index].predecessor)
            anchors.append({matches[index].before, matches[index].after});
        std::reverse(anchors.begin(), anchors.end());
        for (const auto &anchor : anchors)
            rememberHandle(anchor.first, anchor.second);
        anchors.append({aEnd, bEnd});
        int ai = prefix, bi = prefix;
        QHash<int, Note> removed;
        QHash<QString, int> uniqueSource;
        QHash<QString, int> targetCounts;
        for (int i = 0; i < a.size(); ++i)
            if (!a[i].id.isEmpty())
                uniqueSource[a[i].id] = uniqueSource.contains(a[i].id) ? -1 : i;
        for (const auto &n : b)
            if (!n.id.isEmpty())
                ++targetCounts[n.id];
        for (const auto &anchor : anchors)
        {
            if (ai != anchor.first || bi != anchor.second)
            {
                next.m_notes.append({ai, bi, a.mid(ai, anchor.first - ai), b.mid(bi, anchor.second - bi)});
                for (int i = ai; i < anchor.first; ++i)
                    removed.insert(i, a[i]);
            }
            ai = anchor.first;
            bi = anchor.second;
            if (ai < aEnd)
                ++ai;
            if (bi < bEnd)
                ++bi;
        }
        for (const auto &splice : std::as_const(next.m_notes))
            for (int i = 0; i < splice.after.size(); ++i)
            {
                const auto &n = splice.after[i];
                const int sourceIndex = uniqueSource.value(n.id, -1);
                if (sourceIndex >= 0 && removed.contains(sourceIndex) && targetCounts.value(n.id) == 1)
                    next.m_links.append({sourceIndex, splice.afterIndex + i});
            }
    }
    if (!next.m_notes.isEmpty() || !next.m_handles.isEmpty())
    {
        next.m_beforeCount = int(a.size());
        next.m_afterCount = int(b.size());
    }
    if (!next.validateShape(error) || !next.validateTargets(error))
        return false;
    delta = std::move(next);
    return true;
}

bool ChartDelta::validateShape(QString *error) const
{
    if (m_notes.isEmpty() && m_handles.isEmpty())
    {
        if (!m_links.isEmpty() || m_beforeCount || m_afterCount)
            return fail(error, "Unexpected note counts or links.");
        return true;
    }
    if (m_beforeCount < 0 || m_afterCount < 0)
        return fail(error, "Invalid note counts.");
    qint64 beforeCursor = 0, afterCursor = 0;
    QSet<int> removed, added;
    for (const auto &s : m_notes)
    {
        if ((s.before.isEmpty() && s.after.isEmpty()) || s.beforeIndex < beforeCursor || s.afterIndex < afterCursor
            || qint64(s.beforeIndex) + s.before.size() > m_beforeCount
            || qint64(s.afterIndex) + s.after.size() > m_afterCount
            || s.beforeIndex - beforeCursor != s.afterIndex - afterCursor)
            return fail(error, "Invalid or overlapping note splice.");
        for (int i = 0; i < s.before.size(); ++i)
            removed.insert(s.beforeIndex + i);
        for (int i = 0; i < s.after.size(); ++i)
            added.insert(s.afterIndex + i);
        beforeCursor = qint64(s.beforeIndex) + s.before.size();
        afterCursor = qint64(s.afterIndex) + s.after.size();
    }
    if (m_beforeCount - beforeCursor != m_afterCount - afterCursor)
        return fail(error, "Inconsistent note counts.");
    QSet<int> from, to;
    for (const auto &link : m_links)
    {
        if (!removed.contains(link.beforeIndex) || !added.contains(link.afterIndex) || from.contains(link.beforeIndex)
            || to.contains(link.afterIndex))
            return fail(error, "Invalid handle link.");
        from.insert(link.beforeIndex);
        to.insert(link.afterIndex);
    }
    from.clear();
    to.clear();
    for (const auto &handle : m_handles)
    {
        if (handle.beforeIndex < 0 || handle.beforeIndex >= m_beforeCount || handle.afterIndex < 0
            || handle.afterIndex >= m_afterCount || removed.contains(handle.beforeIndex)
            || added.contains(handle.afterIndex) || from.contains(handle.beforeIndex) || to.contains(handle.afterIndex))
            return fail(error, "Invalid runtime handle mapping.");
        auto previous =
            std::upper_bound(m_notes.cbegin(), m_notes.cend(), handle.beforeIndex, [](int index, const Splice &s) {
                return index < s.beforeIndex;
            });
        qint64 shift = 0;
        if (previous != m_notes.cbegin())
        {
            --previous;
            shift =
                qint64(previous->afterIndex) + previous->after.size() - previous->beforeIndex - previous->before.size();
        }
        if (qint64(handle.beforeIndex) + shift != handle.afterIndex)
            return fail(error, "Inconsistent runtime handle mapping.");
        from.insert(handle.beforeIndex);
        to.insert(handle.afterIndex);
    }
    return true;
}

bool ChartDelta::validateTargets(QString *error) const
{
    for (const auto &s : m_notes)
        for (const auto &n : s.after)
            if (!n.isValid() || !n.isXValid())
                return fail(error, "Invalid note target.");
    if (m_timing)
        for (const auto &b : m_timing->after)
            if (!std::isfinite(b.bpm) || b.bpm <= 0 || b.denominator <= 0 || b.numerator < 0)
                return fail(error, "Invalid timing target.");
    if (m_meta && !std::isfinite(m_meta->after.firstBpm))
        return fail(error, "Invalid metadata target.");
    return true;
}

bool ChartDelta::validateSource(const Chart &source, QString *error) const
{
    if (!validateShape(error))
        return false;
    if (!m_notes.isEmpty() || !m_handles.isEmpty())
    {
        if (source.notes().size() != m_beforeCount)
            return fail(error, "Note count precondition failed.");
        for (const auto &s : m_notes)
            for (int i = 0; i < s.before.size(); ++i)
                if (!noteEqual(source.notes()[s.beforeIndex + i], s.before[i]))
                    return fail(error, "Note precondition failed.");
    }
    for (const auto &handle : m_handles)
        if (source.notes()[handle.beforeIndex].id != handle.beforeId
            || source.notes()[handle.beforeIndex].id.isNull() != handle.beforeId.isNull())
            return fail(error, "Runtime handle precondition failed.");
    if (m_meta && metaJson(source.meta()) != metaJson(m_meta->before))
        return fail(error, "Metadata precondition failed.");
    if (m_timing && !timingEqual(source.bpmList(), m_timing->before))
        return fail(error, "Timing precondition failed.");
    if (m_audioSource
        && (source.audioSourceFullPath() != m_audioSource->before
            || source.audioSourceFullPath().isNull() != m_audioSource->before.isNull()))
        return fail(error, "Audio source precondition failed.");
    return true;
}

bool ChartDelta::bindTo(const Chart &source, ChartDelta &bound, QString *error) const
{
    if (!validateSource(source, error))
        return false;
    ChartDelta next = *this;
    QHash<int, QString> reused;
    for (const auto &link : m_links)
        reused.insert(link.afterIndex, source.notes()[link.beforeIndex].id);
    for (auto &s : next.m_notes)
    {
        for (int i = 0; i < s.before.size(); ++i)
            s.before[i].id = source.notes()[s.beforeIndex + i].id;
        for (int i = 0; i < s.after.size(); ++i)
            if (reused.contains(s.afterIndex + i))
                s.after[i].id = reused.value(s.afterIndex + i);
    }
    for (auto &handle : next.m_handles)
        handle.beforeId = source.notes()[handle.beforeIndex].id;
    bound = std::move(next);
    return true;
}

bool ChartDelta::apply(const Chart &source, Chart &result, QString *error) const
{
    if (!validateSource(source, error))
        return false;
    Chart next = source;
    if (!m_notes.isEmpty())
    {
        QVector<Note> notes;
        notes.reserve(m_afterCount);
        int cursor = 0;
        for (const auto &s : m_notes)
        {
            for (; cursor < s.beforeIndex; ++cursor)
                notes.append(source.notes()[cursor]);
            notes.append(s.after);
            cursor += int(s.before.size());
        }
        for (; cursor < source.notes().size(); ++cursor)
            notes.append(source.notes()[cursor]);
        next.notes() = std::move(notes); // preserve exact source order, including ties
    }
    if (m_timing)
        next.bpmList() = m_timing->after;
    if (m_meta)
        next.meta() = m_meta->after;
    if (m_audioSource)
        next.setAudioSourceFullPath(m_audioSource->after);
    if (!m_handles.isEmpty())
    {
        auto &notes = next.notes();
        for (const auto &handle : m_handles)
            notes[handle.afterIndex].id = handle.afterId;
    }
    result = std::move(next);
    return true;
}

ChartDelta ChartDelta::inverted() const
{
    ChartDelta inverse = *this;
    std::swap(inverse.m_beforeCount, inverse.m_afterCount);
    for (auto &s : inverse.m_notes)
    {
        std::swap(s.beforeIndex, s.afterIndex);
        s.before.swap(s.after);
    }
    for (auto &link : inverse.m_links)
        std::swap(link.beforeIndex, link.afterIndex);
    for (auto &handle : inverse.m_handles)
    {
        std::swap(handle.beforeIndex, handle.afterIndex);
        std::swap(handle.beforeId, handle.afterId);
    }
    if (inverse.m_timing)
        std::swap(inverse.m_timing->before, inverse.m_timing->after);
    if (inverse.m_meta)
        std::swap(inverse.m_meta->before, inverse.m_meta->after);
    if (inverse.m_audioSource)
        std::swap(inverse.m_audioSource->before, inverse.m_audioSource->after);
    return inverse;
}

ChartChangeSet ChartDelta::changes() const
{
    ChartChangeSet types;
    if (!m_notes.isEmpty() || !m_handles.isEmpty())
        types |= ChartChangeType::Notes;
    if (m_timing)
        types |= ChartChangeType::Timing;
    if (m_meta)
    {
        types |= ChartChangeType::Metadata;
        if (m_meta->before.offset != m_meta->after.offset)
            types |= ChartChangeType::Timing;
        if (m_meta->before.audioFile != m_meta->after.audioFile
            || m_meta->before.backgroundFile != m_meta->after.backgroundFile)
            types |= ChartChangeType::Resources;
    }
    if (m_audioSource)
        types |= ChartChangeType::Resources;
    return types;
}

qsizetype ChartDelta::retainedNoteCount() const
{
    qsizetype count = 0;
    for (const auto &s : m_notes)
        count += s.before.size() + s.after.size();
    return count;
}

QVector<Note> ChartDelta::affectedNotes() const
{
    QVector<Note> notes;
    for (const auto &s : m_notes)
        notes.append(s.before);
    return notes;
}

bool ChartDelta::addsNotes() const
{
    qsizetype count = 0;
    for (const auto &s : m_notes)
        count += s.after.size();
    return count > m_links.size();
}

QJsonObject ChartDelta::toJson() const
{
    QJsonArray splices, links;
    for (const auto &s : m_notes)
        splices.append(QJsonObject{{"beforeIndex", s.beforeIndex},
                                   {"afterIndex", s.afterIndex},
                                   {"before", notesJson(s.before)},
                                   {"after", notesJson(s.after)}});
    for (const auto &link : m_links)
        links.append(QJsonObject{{"beforeIndex", link.beforeIndex}, {"afterIndex", link.afterIndex}});
    QJsonObject json{{"schemaVersion", 1},
                     {"beforeNoteCount", m_notes.isEmpty() ? 0 : m_beforeCount},
                     {"afterNoteCount", m_notes.isEmpty() ? 0 : m_afterCount},
                     {"notes", splices},
                     {"handleLinks", links}};
    if (m_timing)
        json["timing"] = QJsonObject{{"before", timingJson(m_timing->before)}, {"after", timingJson(m_timing->after)}};
    if (m_meta)
        json["metadata"] = QJsonObject{{"before", metaJson(m_meta->before)}, {"after", metaJson(m_meta->after)}};
    if (m_audioSource)
        json["audioSource"] =
            QJsonObject{{"before", stringJson(m_audioSource->before)}, {"after", stringJson(m_audioSource->after)}};
    return json;
}

bool ChartDelta::fromJson(const QJsonObject &json, ChartDelta &delta, QString *error)
{
    const QSet<QString> allowed{"schemaVersion", "beforeNoteCount", "afterNoteCount", "notes",
                                "handleLinks",   "timing",          "metadata",       "audioSource"};
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!allowed.contains(it.key()))
            return fail(error, "Unknown chart delta field.");
    ChartDelta next;
    int version = 0;
    if (!integer(json.value("schemaVersion"), version) || version != 1
        || !integer(json.value("beforeNoteCount"), next.m_beforeCount)
        || !integer(json.value("afterNoteCount"), next.m_afterCount) || !json.value("notes").isArray()
        || !json.value("handleLinks").isArray())
        return fail(error, "Invalid chart delta header.");
    for (const auto &v : json.value("notes").toArray())
    {
        const auto o = v.toObject();
        Splice s;
        if (!v.isObject() || !fields(o, {"beforeIndex", "afterIndex", "before", "after"})
            || !integer(o.value("beforeIndex"), s.beforeIndex) || !integer(o.value("afterIndex"), s.afterIndex)
            || !readNotes(o.value("before"), s.before) || !readNotes(o.value("after"), s.after))
            return fail(error, "Invalid note splice.");
        next.m_notes.append(std::move(s));
    }
    for (const auto &v : json.value("handleLinks").toArray())
    {
        const auto o = v.toObject();
        HandleLink link;
        if (!v.isObject() || !fields(o, {"beforeIndex", "afterIndex"})
            || !integer(o.value("beforeIndex"), link.beforeIndex) || !integer(o.value("afterIndex"), link.afterIndex))
            return fail(error, "Invalid handle link.");
        next.m_links.append(link);
    }
    for (const auto &key : {QStringLiteral("timing"), QStringLiteral("metadata"), QStringLiteral("audioSource")})
    {
        if (!json.contains(key))
            continue;
        const auto o = json.value(key).toObject();
        if (!json.value(key).isObject() || !fields(o, {"before", "after"}))
            return fail(error, "Invalid replacement.");
        if (key == "timing")
        {
            Replace<QVector<BpmEntry>> replacement;
            if (!readTiming(o.value("before"), replacement.before) || !readTiming(o.value("after"), replacement.after))
                return fail(error, "Invalid timing replacement.");
            next.m_timing = std::move(replacement);
        }
        else if (key == "metadata")
        {
            Replace<MetaData> replacement;
            if (!o.value("before").isObject() || !o.value("after").isObject()
                || !readMeta(o.value("before").toObject(), replacement.before)
                || !readMeta(o.value("after").toObject(), replacement.after))
                return fail(error, "Invalid metadata replacement.");
            next.m_meta = std::move(replacement);
        }
        else
        {
            Replace<QString> replacement;
            if (!textField(o, "before", replacement.before) || !textField(o, "after", replacement.after))
                return fail(error, "Invalid audio source replacement.");
            next.m_audioSource = std::move(replacement);
        }
    }
    if (!next.validateShape(error) || !next.validateTargets(error))
        return false;
    delta = std::move(next);
    return true;
}
} // namespace CceDocument
