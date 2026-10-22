#include "ReviewCore.h"
#include "document/DocumentModel.h"
#include "document/ProjectStorage.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

namespace CceReview
{
namespace
{
constexpr qint64 MaxCompressed = 64LL * 1024 * 1024;
constexpr qint64 MaxExpanded = 128LL * 1024 * 1024;
constexpr qint64 MaxResource = 48LL * 1024 * 1024;
const QByteArray PackageMagic("CCEPR1\r\n", 8);
QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonValue canon(const QJsonValue &v, const QString &key = {})
{
    if (v.isObject())
    {
        const QJsonObject src = v.toObject();
        QStringList keys = src.keys();
        std::sort(keys.begin(), keys.end());
        QJsonObject out;
        for (const QString &k : keys)
        {
            if (k.compare("effect", Qt::CaseInsensitive) == 0 || k.compare("effects", Qt::CaseInsensitive) == 0
                || k.compare("extra", Qt::CaseInsensitive) == 0 || k.compare("extras", Qt::CaseInsensitive) == 0
                || k.compare("plugin", Qt::CaseInsensitive) == 0 || k.compare("plugins", Qt::CaseInsensitive) == 0
                || k.compare("pluginData", Qt::CaseInsensitive) == 0)
                continue;
            out.insert(k, canon(src.value(k), k));
        }
        return out;
    }
    if (v.isArray())
    {
        QJsonArray a = v.toArray();
        // BeatPosition's [measure,numerator,denominator] representation is exact.
        if ((key.compare("beat", Qt::CaseInsensitive) == 0 || key.compare("time", Qt::CaseInsensitive) == 0
             || key.compare("endbeat", Qt::CaseInsensitive) == 0
             || key.compare("positionBeat", Qt::CaseInsensitive) == 0
             || key.compare("startBeat", Qt::CaseInsensitive) == 0)
            && a.size() == 3 && a[0].isDouble() && a[1].isDouble() && a[2].isDouble())
        {
            const double md = a[0].toDouble(), nd = a[1].toDouble(), dd = a[2].toDouble();
            if (std::isfinite(md) && std::isfinite(nd) && std::isfinite(dd) && std::abs(md) < 9e15
                && std::abs(nd) < 9e15 && std::abs(dd) < 9e15 && md == qint64(md) && nd == qint64(nd)
                && dd == qint64(dd) && dd != 0)
            {
                qint64 m = qint64(md), n = qint64(nd), d = qint64(dd);
                if (d < 0)
                {
                    d = -d;
                    n = -n;
                }
                m += n / d;
                n %= d;
                if (n < 0)
                {
                    --m;
                    n += d;
                }
                const qint64 g = std::gcd(n, d);
                if (g)
                {
                    n /= g;
                    d /= g;
                }
                return QJsonArray{double(m), double(n), double(d)};
            }
        }
        QJsonArray out;
        for (const auto &x : a)
            out.append(canon(x));
        return out;
    }
    return v;
}
QByteArray valueBytes(const QJsonValue &v)
{
    if (v.isObject())
        return QJsonDocument(canon(v).toObject()).toJson(QJsonDocument::Compact);
    QJsonArray a;
    a.append(canon(v));
    QByteArray raw = QJsonDocument(a).toJson(QJsonDocument::Compact);
    return raw.mid(1, raw.size() - 2);
}
bool equal(const QJsonValue &a, const QJsonValue &b)
{
    if (a.isUndefined() || b.isUndefined())
        return a.isUndefined() && b.isUndefined();
    return valueBytes(a) == valueBytes(b);
}
QString ptrEscape(QString s)
{
    s.replace("~", "~0");
    s.replace("/", "~1");
    return s;
}
QString ptrUnescape(QString s)
{
    s.replace("~1", "/");
    s.replace("~0", "~");
    return s;
}
QString childPath(const QString &p, const QString &k)
{
    return p + "/" + ptrEscape(k);
}
QString pathDomain(const QString &path)
{
    const QStringList p = path.split('/', Qt::SkipEmptyParts);
    if (p.isEmpty())
        return "Snapshot";
    if (p[0] == "layers" && p.size() > 2 && p[2] == "notes")
        return "Note";
    if (p[0] == "difficulties" && p.size() > 2 && p[2] == "timing")
        return "Timing";
    if (p[0] == "difficulties" && p.contains("metadata"))
        return "Metadata";
    if (p[0] == "difficulties" && p.contains("exportedMetadataFields"))
        return "ExportConfiguration";
    if (p[0] == "layers")
        return "Layer";
    if (p[0] == "difficulties")
        return "Difficulty";
    if (p[0] == "resources")
        return "Resource";
    return p[0];
}
QString kindFor(bool had, bool has)
{
    return !had ? "Add" : (!has ? "Remove" : "Modify");
}
Change makeChange(QString path, QString kind, QJsonValue before, QJsonValue after, bool move = false)
{
    Change c;
    c.changeId = uuid();
    c.domain = pathDomain(path);
    c.path = path;
    c.kind = kind;
    c.before = before;
    c.after = after;
    c.moveIntent = move;
    return c;
}
bool multisetKey(const QString &path)
{
    const QString leaf = ptrUnescape(path.section('/', -1));
    return leaf == "notes" || leaf == "timing";
}
QByteArray timingBeatKey(const QJsonValue &v)
{
    if (!v.isObject())
        return {};
    const QJsonObject o = v.toObject();
    if (o.contains("beat"))
        return valueBytes(canon(o.value("beat"), "beat"));
    if (o.contains("beatNum") && o.contains("numerator") && o.contains("denominator"))
        return valueBytes(canon(QJsonArray{o.value("beatNum"), o.value("numerator"), o.value("denominator")}, "beat"));
    return {};
}
QByteArray noteSignature(const QJsonValue &v)
{
    if (!v.isObject())
        return {};
    QJsonObject o = v.toObject();
    for (const QString &k : {QStringLiteral("beat"), QStringLiteral("endBeat"), QStringLiteral("beatNum"),
                             QStringLiteral("numerator"), QStringLiteral("denominator"), QStringLiteral("endBeatNum"),
                             QStringLiteral("endNumerator"), QStringLiteral("endDenominator")})
        o.remove(k);
    return valueBytes(canon(o));
}
void diffValue(const QJsonValue &a, const QJsonValue &b, const QString &path, QVector<Change> &out)
{
    if (equal(a, b))
        return;
    if (a.isObject() && b.isObject())
    {
        const auto ao = a.toObject(), bo = b.toObject();
        QSet<QString> keys;
        for (auto i = ao.begin(); i != ao.end(); ++i)
            keys.insert(i.key());
        for (auto i = bo.begin(); i != bo.end(); ++i)
            keys.insert(i.key());
        QStringList sorted = keys.values();
        std::sort(sorted.begin(), sorted.end());
        for (const QString &k : sorted)
        {
            if (k.compare("effect", Qt::CaseInsensitive) == 0 || k.compare("effects", Qt::CaseInsensitive) == 0
                || k.compare("extra", Qt::CaseInsensitive) == 0 || k.compare("extras", Qt::CaseInsensitive) == 0
                || k.compare("plugin", Qt::CaseInsensitive) == 0 || k.compare("plugins", Qt::CaseInsensitive) == 0
                || k.compare("pluginData", Qt::CaseInsensitive) == 0)
                continue;
            const bool ah = ao.contains(k), bh = bo.contains(k);
            const QString p = childPath(path, k);
            if (!ah)
                out.append(makeChange(p, "Add", QJsonValue(QJsonValue::Undefined), bo.value(k)));
            else if (!bh)
                out.append(makeChange(p, "Remove", ao.value(k), QJsonValue(QJsonValue::Undefined)));
            else
                diffValue(ao.value(k), bo.value(k), p, out);
        }
        return;
    }
    if (a.isArray() && b.isArray() && multisetKey(path))
    {
        QJsonArray aa = a.toArray(), bb = b.toArray();
        if (ptrUnescape(path.section('/', -1)) == "timing")
        {
            QHash<QByteArray, QVector<int>> byBeatA, byBeatB;
            for (int i = 0; i < aa.size(); ++i)
            {
                auto k = timingBeatKey(aa[i]);
                if (!k.isEmpty())
                    byBeatA[k].append(i);
            }
            for (int i = 0; i < bb.size(); ++i)
            {
                auto k = timingBeatKey(bb[i]);
                if (!k.isEmpty())
                    byBeatB[k].append(i);
            }
            QSet<int> consumedA, consumedB;
            for (auto it = byBeatA.cbegin(); it != byBeatA.cend(); ++it)
            {
                const auto next = byBeatB.value(it.key());
                if (it.value().size() == 1 && next.size() == 1)
                {
                    const int ai = it.value().first(), bi = next.first();
                    if (!equal(aa[ai], bb[bi]))
                    {
                        out.append(makeChange(path, "Modify", canon(aa[ai]), canon(bb[bi])));
                        consumedA.insert(ai);
                        consumedB.insert(bi);
                    }
                }
            }
            QJsonArray ra, rb;
            for (int i = 0; i < aa.size(); ++i)
                if (!consumedA.contains(i))
                    ra.append(aa[i]);
            for (int i = 0; i < bb.size(); ++i)
                if (!consumedB.contains(i))
                    rb.append(bb[i]);
            aa = ra;
            bb = rb;
        }
        QHash<QByteArray, int> countsA, countsB;
        QHash<QByteArray, QJsonValue> values;
        for (const auto &v : aa)
        {
            auto k = valueBytes(v);
            ++countsA[k];
            values.insert(k, canon(v));
        }
        for (const auto &v : bb)
        {
            auto k = valueBytes(v);
            ++countsB[k];
            values.insert(k, canon(v));
        }
        QList<QByteArray> keys = countsA.keys();
        for (const auto &k : countsB.keys())
            if (!countsA.contains(k))
                keys.append(k);
        std::sort(keys.begin(), keys.end());
        for (const auto &k : keys)
        {
            int delta = countsB.value(k) - countsA.value(k);
            while (delta < 0)
            {
                out.append(makeChange(path, "Remove", values.value(k), QJsonValue(QJsonValue::Undefined)));
                ++delta;
            }
            while (delta > 0)
            {
                out.append(makeChange(path, "Add", QJsonValue(QJsonValue::Undefined), values.value(k)));
                --delta;
            }
        }
        return;
    }
    out.append(makeChange(path, kindFor(!a.isUndefined(), !b.isUndefined()), a, b));
}
QJsonValue getAt(const QJsonObject &o, const QString &path, bool *exists = nullptr)
{
    QJsonValue v(o);
    const auto parts = path.split('/', Qt::SkipEmptyParts);
    for (const QString &raw : parts)
    {
        if (!v.isObject())
        {
            if (exists)
                *exists = false;
            return {};
        }
        QJsonObject ob = v.toObject();
        QString k = ptrUnescape(raw);
        if (!ob.contains(k))
        {
            if (exists)
                *exists = false;
            return {};
        }
        v = ob.value(k);
    }
    if (exists)
        *exists = true;
    return v;
}
bool setAt(QJsonObject &root, const QString &path, const QJsonValue &val, bool remove)
{
    const auto parts = path.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    QStringList keys;
    for (auto p : parts)
        keys.append(ptrUnescape(p));
    std::function<bool(QJsonObject &, int)> rec = [&](QJsonObject &obj, int at) {
        const QString k = keys[at];
        if (at == keys.size() - 1)
        {
            if (remove)
                obj.remove(k);
            else
                obj.insert(k, val);
            return true;
        }
        if (!obj.value(k).isObject())
            return false;
        QJsonObject nested = obj.value(k).toObject();
        if (!rec(nested, at + 1))
            return false;
        obj.insert(k, nested);
        return true;
    };
    return rec(root, 0);
}
bool validRelativePath(const QString &p)
{
    if (p.isEmpty() || p.startsWith('/') || p.contains('\\') || p.contains(':'))
        return false;
    const auto parts = p.split('/', Qt::KeepEmptyParts);
    for (const QString &x : parts)
        if (x == ".." || x == "." || x.isEmpty() || x.contains(QChar::Null))
            return false;
    return true;
}
bool fail(QString *e, const QString &s)
{
    if (e)
        *e = s;
    return false;
}
QJsonArray stringArray(const QStringList &list)
{
    QJsonArray a;
    for (const auto &s : list)
        a.append(s);
    return a;
}
QStringList toStrings(const QJsonValue &v)
{
    QStringList out;
    for (const auto &x : v.toArray())
        if (x.isString())
            out.append(x.toString());
    return out;
}
QJsonArray threeWayRecords(const QJsonArray &base, const QJsonArray &local, const QJsonArray &incoming,
                           const QString &idField, const QString &label, QStringList *conflicts)
{
    QJsonArray out;
    QSet<QString> ids;
    QHash<QString, QJsonObject> baseMap, localMap, incomingMap;
    for (const auto &v : base)
        if (v.isObject())
        {
            const auto o = v.toObject();
            const auto id = o.value(idField).toString();
            if (!id.isEmpty())
            {
                ids.insert(id);
                baseMap.insert(id, o);
            }
        }
    for (const auto &v : local)
        if (v.isObject())
        {
            const auto o = v.toObject();
            const auto id = o.value(idField).toString();
            if (!id.isEmpty())
            {
                ids.insert(id);
                localMap.insert(id, o);
            }
        }
    for (const auto &v : incoming)
        if (v.isObject())
        {
            const auto o = v.toObject();
            const auto id = o.value(idField).toString();
            if (!id.isEmpty())
            {
                ids.insert(id);
                incomingMap.insert(id, o);
            }
        }
    QStringList sorted = ids.values();
    std::sort(sorted.begin(), sorted.end());
    for (const QString &id : sorted)
    {
        if (id.isEmpty())
            continue;
        const QJsonObject b = baseMap.value(id), l = localMap.value(id), r = incomingMap.value(id);
        const bool hb = !b.isEmpty(), hl = !l.isEmpty(), hr = !r.isEmpty();
        if (!hl && !hr)
            continue;
        if (!hl)
        {
            out.append(r);
            continue;
        }
        if (!hr)
        {
            out.append(l);
            continue;
        }
        QJsonObject merged = l;
        QStringList keys = l.keys();
        for (const QString &k : r.keys())
            if (!keys.contains(k))
                keys.append(k);
        QJsonArray alternatives = l.value("_conflictAlternatives").toArray();
        for (const QString &k : keys)
        {
            if (k == "_conflictAlternatives")
            {
                for (const auto &a : r.value(k).toArray())
                    if (!alternatives.contains(a))
                        alternatives.append(a);
                continue;
            }
            const QJsonValue bv = b.value(k), lv = l.value(k), rv = r.value(k);
            const bool be = hb && b.contains(k), le = l.contains(k), re = r.contains(k);
            if (le == re && (!le || equal(lv, rv)))
                continue;
            if (le == be && (!le || equal(lv, bv)))
            {
                if (re)
                    merged.insert(k, rv);
                else
                    merged.remove(k);
                continue;
            }
            if (re == be && (!re || equal(rv, bv)))
                continue;
            QJsonObject alt{{"field", k}};
            if (be)
                alt.insert("base", bv);
            if (le)
                alt.insert("local", lv);
            if (re)
                alt.insert("incoming", rv);
            alternatives.append(alt);
            if (conflicts)
                conflicts->append(label + ":" + id + ":" + k);
        }
        if (!alternatives.isEmpty())
            merged.insert("_conflictAlternatives", alternatives);
        // A deletion stays visible as a tombstone, but concurrent edits must be
        // retained for review rather than discarded by a last-writer rule.
        if (l.value("tombstone").toBool() || r.value("tombstone").toBool())
        {
            merged.insert("tombstone", true);
            const auto live = l.value("tombstone").toBool() ? r : l;
            if (hb && !b.value("tombstone").toBool() && !equal(live, b))
            {
                QJsonObject alt{{"field", "deletion"}, {"base", b}, {"local", l}, {"incoming", r}};
                alternatives.append(alt);
                merged.insert("_conflictAlternatives", alternatives);
                if (conflicts)
                    conflicts->append(label + ":" + id + ":deletion");
            }
        }
        out.append(merged);
    }
    return out;
}
QJsonObject ancestorState(const QJsonObject &local, const QJsonObject &incoming, QString *ancestorId)
{
    const QString lid = local.value("revisionId").toString(), iid = incoming.value("revisionId").toString();
    const QJsonArray lh = local.value("revisionHistory").toArray(), ih = incoming.value("revisionHistory").toArray();
    for (const auto &v : ih)
        if (v.toObject().value("revisionId").toString() == lid)
        {
            if (ancestorId)
                *ancestorId = lid;
            return {{"comments", local.value("comments")},
                    {"reviewDecisions", local.value("reviewDecisions")},
                    {"changes", local.value("changes")}};
        }
    for (const auto &v : lh)
        if (v.toObject().value("revisionId").toString() == iid)
        {
            if (ancestorId)
                *ancestorId = iid;
            return {{"comments", incoming.value("comments")},
                    {"reviewDecisions", incoming.value("reviewDecisions")},
                    {"changes", incoming.value("changes")}};
        }
    QHash<QString, QJsonObject> right;
    for (const auto &v : ih)
        if (v.isObject())
            right.insert(v.toObject().value("revisionId").toString(), v.toObject());
    for (int i = lh.size() - 1; i >= 0; --i)
        if (lh[i].isObject())
        {
            const auto rec = lh[i].toObject();
            const QString id = rec.value("revisionId").toString();
            if (right.contains(id))
            {
                if (ancestorId)
                    *ancestorId = id;
                return rec;
            }
        }
    return {};
}
QJsonObject revisionState(const QJsonObject &revision)
{
    return {{"revisionId", revision.value("revisionId")},
            {"parentRevisionIds", revision.value("parentRevisionIds")},
            {"baseSnapshotHash", revision.value("baseSnapshotHash")},
            {"comments", revision.value("comments")},
            {"events", revision.value("events")},
            {"author", revision.value("author")},
            {"description", revision.value("description")},
            {"reviewDecisions", revision.value("reviewDecisions")},
            {"changes", revision.value("changes")}};
}
// Every claimed ancestor must be reachable through stored parent edges. UUID
// strings alone are not proof that two offline branches share a revision.
bool validateGraph(const QJsonObject &revision, QString *error)
{
    QHash<QString, QJsonObject> records;
    const QString current = revision.value("revisionId").toString();
    for (const auto &v : revision.value("revisionHistory").toArray())
    {
        const auto record = v.toObject();
        const QString id = record.value("revisionId").toString();
        if (id == current)
            return fail(error, "Revision cannot be its own ancestor");
        records.insert(id, record);
    }
    records.insert(current, revision);
    QSet<QString> visited;
    QVector<QString> pending{current}, roots;
    QHash<QString, int> remainingParents;
    QHash<QString, QStringList> children;
    while (!pending.isEmpty())
    {
        const QString id = pending.takeLast();
        if (visited.contains(id))
            continue;
        if (!records.contains(id))
            return fail(error, "Missing revision parent payload");
        const auto parents = records.value(id).value("parentRevisionIds");
        if (!parents.isArray())
            return fail(error, "Revision parents must be an array");
        visited.insert(id);
        QSet<QString> unique;
        for (const auto &p : parents.toArray())
        {
            const QString parent = p.toString();
            if (!p.isString() || parent.isEmpty() || unique.contains(parent))
                return fail(error, "Invalid revision parent");
            unique.insert(parent);
            pending.append(parent);
            children[parent].append(id);
        }
        remainingParents[id] = unique.size();
        if (unique.isEmpty())
            roots.append(id);
    }
    int processed = 0;
    while (!roots.isEmpty())
    {
        const auto id = roots.takeLast();
        ++processed;
        for (const auto &child : children.value(id))
            if (--remainingParents[child] == 0)
                roots.append(child);
    }
    if (processed != visited.size())
        return fail(error, "Revision parent cycle");
    visited.remove(current);
    QSet<QString> declared;
    if (!revision.value("ancestors").isArray())
        return fail(error, "Revision ancestors must be an array");
    for (const auto &v : revision.value("ancestors").toArray())
    {
        if (!v.isString() || v.toString().isEmpty() || declared.contains(v.toString()))
            return fail(error, "Invalid ancestor list");
        declared.insert(v.toString());
    }
    return declared == visited || fail(error, "Revision ancestry does not match parent graph");
}
bool validateSnapshot(QJsonObject snapshot, QString *error)
{
    const auto difficulties = snapshot.value("difficulties").toObject();
    if (!snapshot.contains("activeDifficultyId"))
        snapshot.insert("activeDifficultyId", difficulties.keys().value(0));
    CceDocument::Project project;
    return CceDocument::Project::fromJson(snapshot, project, error);
}
} // namespace

QByteArray canonicalHash(const QJsonObject &object)
{
    const QByteArray compact = QJsonDocument(canon(object).toObject()).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(compact, QCryptographicHash::Sha256).toHex();
}
QVector<Change> diff(const QJsonObject &base, const QJsonObject &proposal, const QVector<Change> &intents)
{
    QVector<Change> out;
    diffValue(base, proposal, QString(), out);
    // Intent is the only authority for Move. Pair its explicit old/new values with matching Remove/Add entries.
    QHash<QString, QVector<int>> removes, adds;
    QSet<int> pairedAdds;
    for (int i = 0; i < out.size(); ++i)
        if (multisetKey(out[i].path))
        {
            const QByteArray key = valueBytes(out[i].kind == "Remove" ? out[i].before : out[i].after);
            const QString compound = out[i].path + "|" + out[i].kind + "|" + QString::fromLatin1(key.toHex());
            if (out[i].kind == "Remove")
                removes[compound].append(i);
            if (out[i].kind == "Add")
                adds[compound].append(i);
        }
    for (const auto &intent : intents)
        if ((intent.moveIntent || intent.kind == "Modify") && !intent.before.isUndefined()
            && !intent.after.isUndefined())
        {
            const QString rk = intent.path + "|Remove|" + QString::fromLatin1(valueBytes(intent.before).toHex());
            const QString ak = intent.path + "|Add|" + QString::fromLatin1(valueBytes(intent.after).toHex());
            if (!removes.value(rk).isEmpty() && !adds.value(ak).isEmpty())
            {
                const int ri = removes[rk].takeLast(), ai = adds[ak].takeLast();
                out[ri].kind = intent.moveIntent ? "Move" : "Modify";
                out[ri].after = intent.after;
                out[ri].moveIntent = intent.moveIntent;
                out[ri].changeId = intent.changeId.isEmpty() ? out[ri].changeId : intent.changeId;
                out[ri].operationGroupId = intent.operationGroupId;
                pairedAdds.insert(ai);
            }
        }
    // Explicit Add/Remove intents preserve operation identity without implying a Move.
    QSet<int> claimed;
    for (const auto &intent : intents)
    {
        if (intent.moveIntent || intent.changeId.isEmpty())
            continue;
        const QByteArray value = valueBytes(intent.kind == "Remove" ? intent.before : intent.after);
        const QString key = intent.path + "|" + intent.kind + "|" + QString::fromLatin1(value.toHex());
        auto &indexed = (intent.kind == "Remove" ? removes[key] : adds[key]);
        while (!indexed.isEmpty())
        {
            const int i = indexed.takeLast();
            if (i < 0 || i >= out.size() || pairedAdds.contains(i) || claimed.contains(i) || out[i].kind != intent.kind
                || !equal(out[i].before, intent.before) || !equal(out[i].after, intent.after))
                continue;
            out[i].changeId = intent.changeId;
            out[i].operationGroupId = intent.operationGroupId;
            claimed.insert(i);
            break;
        }
    }
    if (!pairedAdds.isEmpty())
    {
        QVector<Change> compact;
        compact.reserve(out.size() - pairedAdds.size());
        for (int i = 0; i < out.size(); ++i)
            if (!pairedAdds.contains(i))
                compact.append(out[i]);
        out = compact;
    }
    QHash<QString, Change> fields;
    for (const auto &intent : intents)
        if (!multisetKey(intent.path) && !intent.changeId.isEmpty() && !fields.contains(intent.path))
            fields[intent.path] = intent;
    for (auto &change : out)
        if (!multisetKey(change.path) && fields.contains(change.path))
        {
            const auto prior = fields.value(change.path);
            if (equal(prior.before, change.before))
            {
                change.changeId = prior.changeId;
                change.operationGroupId = prior.operationGroupId;
            }
        }
    assignDependencies(base, proposal, out);
    return out;
}

void assignDependencies(const QJsonObject &base, const QJsonObject &proposal, QVector<Change> &changes)
{
    QHash<QString, QSet<QString>> layerDifficulties;
    for (const auto &snapshot : {base, proposal})
    {
        const auto difficulties = snapshot.value("difficulties").toObject();
        for (auto it = difficulties.begin(); it != difficulties.end(); ++it)
            for (const auto &binding : it.value().toObject().value("bindings").toArray())
                layerDifficulties[binding.toObject().value("layerId").toString()].insert(it.key());
    }
    QVector<int> parents(changes.size());
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](int i) {
        while (parents[i] != i)
        {
            parents[i] = parents[parents[i]];
            i = parents[i];
        }
        return i;
    };
    QHash<QString, QVector<int>> related;
    QSet<QString> timingScopes;
    QHash<QString, QString> resourceChanges;
    for (auto &change : changes)
    {
        change.dependsOn.clear();
        change.dependencyGroupId.clear();
        if (change.domain == "Resource")
            resourceChanges[change.path] = change.changeId;
    }
    for (int i = 0; i < changes.size(); ++i)
    {
        auto &change = changes[i];
        const auto parts = change.path.split('/', Qt::SkipEmptyParts);
        QSet<QString> scopes;
        const bool timing = change.domain == "Timing" || change.path.endsWith("/oggHash")
                            || change.path.endsWith("/metadata/offset") || change.path.endsWith("/metadata/firstBpm")
                            || change.path.endsWith("/metadata/audioFile");
        if (parts.size() >= 2 && parts[0] == "difficulties" && timing)
            scopes.insert(ptrUnescape(parts[1]));
        else if (change.domain == "Note" && parts.size() >= 2)
            scopes = layerDifficulties.value(ptrUnescape(parts[1]));
        for (const auto &scope : scopes)
        {
            related[scope].append(i);
            if (timing)
                timingScopes.insert(scope);
        }
        QString resource;
        if (change.domain == "Note")
            resource = change.after.toObject().value("sound").toString();
        else if (change.path.endsWith("/metadata/audioFile") || change.path.endsWith("/metadata/backgroundFile"))
            resource = change.after.toString();
        else if (change.path.endsWith("/oggHash") && parts.size() >= 2)
            resource = proposal.value("difficulties")
                           .toObject()
                           .value(ptrUnescape(parts[1]))
                           .toObject()
                           .value("metadata")
                           .toObject()
                           .value("audioFile")
                           .toString();
        const QString dependency = resourceChanges.value("/resources/" + ptrEscape(resource));
        if (!resource.isEmpty() && !dependency.isEmpty())
            change.dependsOn.append(dependency);
    }
    for (const auto &scope : timingScopes)
    {
        const auto &indices = related[scope];
        for (int i = 1; i < indices.size(); ++i)
            parents[root(indices[i])] = root(indices[0]);
    }
    QHash<int, QStringList> members;
    for (int i = 0; i < changes.size(); ++i)
        members[root(i)].append(changes[i].changeId);
    QHash<int, QString> groups;
    for (auto it = members.begin(); it != members.end(); ++it)
        if (it->size() > 1)
        {
            std::sort(it->begin(), it->end());
            groups[it.key()] = QString::fromLatin1(canonicalHash({{"members", stringArray(*it)}}));
        }
    for (int i = 0; i < changes.size(); ++i)
        changes[i].dependencyGroupId = groups.value(root(i));
}

QSet<QString> dependencySelection(const QVector<Change> &changes, const QSet<QString> &selected)
{
    QHash<QString, const Change *> byId;
    QHash<QString, QStringList> byGroup;
    for (const auto &change : changes)
    {
        byId[change.changeId] = &change;
        if (!change.dependencyGroupId.isEmpty())
            byGroup[change.dependencyGroupId].append(change.changeId);
    }
    QSet<QString> result = selected, groups;
    QStringList queue = selected.values();
    for (int i = 0; i < queue.size(); ++i)
    {
        const auto *change = byId.value(queue[i], nullptr);
        if (!change)
            continue;
        QStringList next = change->dependsOn;
        if (!change->dependencyGroupId.isEmpty() && !groups.contains(change->dependencyGroupId))
        {
            groups.insert(change->dependencyGroupId);
            next += byGroup.value(change->dependencyGroupId);
        }
        for (const auto &id : next)
            if (!result.contains(id))
            {
                result.insert(id);
                queue.append(id);
            }
    }
    return result;
}

QJsonObject changeToJson(const Change &c)
{
    QJsonObject o{{"changeId", c.changeId},
                  {"operationGroupId", c.operationGroupId},
                  {"domain", c.domain},
                  {"path", c.path},
                  {"kind", c.kind},
                  {"beforePresent", !c.before.isUndefined()},
                  {"afterPresent", !c.after.isUndefined()},
                  {"moveIntent", c.moveIntent}};
    if (!c.before.isUndefined())
        o.insert("before", c.before);
    if (!c.after.isUndefined())
        o.insert("after", c.after);
    if (!c.dependencyGroupId.isEmpty())
        o.insert("dependencyGroupId", c.dependencyGroupId);
    if (!c.dependsOn.isEmpty())
        o.insert("dependsOn", stringArray(c.dependsOn));
    return o;
}
bool changeFromJson(const QJsonObject &o, Change &c, QString *error)
{
    const QStringList required = {"changeId", "domain", "path", "kind"};
    for (const QString &k : required)
        if (!o.value(k).isString() || o.value(k).toString().isEmpty())
            return fail(error, "Change missing required field: " + k);
    const QString kind = o.value("kind").toString();
    if (kind != "Add" && kind != "Remove" && kind != "Modify" && kind != "Move")
        return fail(error, "Unknown change kind");
    c.changeId = o.value("changeId").toString();
    c.operationGroupId = o.value("operationGroupId").toString();
    c.domain = o.value("domain").toString();
    c.path = o.value("path").toString();
    c.kind = kind;
    c.before = o.contains("beforePresent") && !o.value("beforePresent").toBool() ? QJsonValue(QJsonValue::Undefined)
                                                                                 : o.value("before");
    c.after = o.contains("afterPresent") && !o.value("afterPresent").toBool() ? QJsonValue(QJsonValue::Undefined)
                                                                              : o.value("after");
    c.moveIntent = o.value("moveIntent").toBool();
    c.dependencyGroupId = o.value("dependencyGroupId").toString();
    c.dependsOn.clear();
    if (o.contains("dependencyGroupId") && !o.value("dependencyGroupId").isString())
        return fail(error, "Malformed dependency group");
    if (o.contains("dependsOn") && !o.value("dependsOn").isArray())
        return fail(error, "Malformed dependencies");
    for (const auto &value : o.value("dependsOn").toArray())
    {
        if (!value.isString() || value.toString().isEmpty() || value.toString() == c.changeId
            || c.dependsOn.contains(value.toString()))
            return fail(error, "Invalid dependency ID");
        c.dependsOn.append(value.toString());
    }
    if (!c.path.startsWith('/'))
        return fail(error, "Change path must be an absolute JSON pointer");
    for (const auto &part : c.path.split('/', Qt::SkipEmptyParts))
    {
        const QString decoded = ptrUnescape(part);
        if (decoded.compare("effect", Qt::CaseInsensitive) == 0 || decoded.compare("effects", Qt::CaseInsensitive) == 0
            || decoded.compare("extra", Qt::CaseInsensitive) == 0 || decoded.compare("extras", Qt::CaseInsensitive) == 0
            || decoded.compare("plugin", Qt::CaseInsensitive) == 0
            || decoded.compare("plugins", Qt::CaseInsensitive) == 0
            || decoded.compare("pluginData", Qt::CaseInsensitive) == 0)
            return fail(error, "Uneditable extension fields cannot be changed");
    }
    static const QSet<QString> domains = {"Note",          "Timing",     "Metadata",          "ExportConfiguration",
                                          "Layer",         "Difficulty", "Resource",          "Snapshot",
                                          "schemaVersion", "projectId",  "activeDifficultyId"};
    if (!domains.contains(c.domain))
        return fail(error, "Unsupported change domain");
    if (c.domain != pathDomain(c.path))
        return fail(error, "Change domain does not match its path");
    const bool hasBefore = !c.before.isUndefined(), hasAfter = !c.after.isUndefined();
    if ((kind == "Add" && (hasBefore || !hasAfter)) || (kind == "Remove" && (!hasBefore || hasAfter))
        || ((kind == "Modify" || kind == "Move") && (!hasBefore || !hasAfter)))
        return fail(error, "Change before/after values do not match its kind");
    if (kind == "Move" && (!c.moveIntent || c.domain != "Note"))
        return fail(error, "Move requires explicit Note moveIntent");
    if (c.domain == "Note" && multisetKey(c.path))
    {
        Note note;
        if ((hasBefore && (!c.before.isObject() || !CceDocument::decodeNote(c.before.toObject(), note, error)))
            || (hasAfter && (!c.after.isObject() || !CceDocument::decodeNote(c.after.toObject(), note, error))))
            return fail(error, "Malformed Note change payload");
    }
    return true;
}
QString changeFingerprint(const Change &change)
{
    auto object = changeToJson(change);
    object.remove("changeId");
    object.remove("operationGroupId");
    return QString::fromLatin1(canonicalHash(object));
}
bool decisionMatchesChange(const QJsonObject &decision, const Change &change)
{
    return decision.value("changeId") == change.changeId
           && decision.value("changeHash").toString() == changeFingerprint(change);
}

static bool refreshRevisionDependencies(QJsonObject &revision, QString *error)
{
    QVector<Change> changes;
    for (const auto &value : revision.value("changes").toArray())
    {
        Change change;
        if (!changeFromJson(value.toObject(), change, error))
            return false;
        changes.append(change);
    }
    const auto base = revision.value("baseSnapshot").toObject();
    const auto rebuilt = merge(base, base, changes);
    if (!rebuilt.committed)
        return fail(error, "Cannot reconstruct change dependencies");
    assignDependencies(base, rebuilt.result, changes);
    QJsonArray encoded;
    for (const auto &change : changes)
        encoded.append(changeToJson(change));
    revision["changes"] = encoded;
    return true;
}
bool MergeResult::hasConflicts() const
{
    for (const auto &i : items)
        if (i.state == "Conflict" || i.state == "Invalid")
            return true;
    return false;
}
MergeResult merge(const QJsonObject &base, const QJsonObject &current, const QVector<Change> &changes,
                  const QSet<QString> &acceptedIds, const QHash<QString, QJsonValue> &resolutions)
{
    MergeResult r;
    r.result = current;
    if (!acceptedIds.isEmpty() && dependencySelection(changes, acceptedIds) != acceptedIds)
    {
        for (const auto &change : changes)
            r.items.append({change, acceptedIds.contains(change.changeId) ? "Invalid" : "Pending", {}});
        return r;
    }
    QJsonObject candidate = current;
    QSet<int> handled;
    QHash<QString, QVector<int>> arrayGroups;
    for (int i = 0; i < changes.size(); ++i)
    {
        const Change &c = changes[i];
        if (multisetKey(c.path)
            && (c.kind == "Add" || c.kind == "Remove" || (c.kind == "Move" && c.moveIntent) || c.kind == "Modify"))
            arrayGroups[c.path].append(i);
    }
    // Process each Note/Timing multiset in linear passes. This prevents repeated scans/removals
    // from becoming quadratic on large charts with many duplicate entries.
    for (auto groupIt = arrayGroups.begin(); groupIt != arrayGroups.end(); ++groupIt)
    {
        const QString path = groupIt.key();
        bool baseExists = false, currentExists = false;
        const QJsonValue baseValue = getAt(base, path, &baseExists),
                         currentValue = getAt(current, path, &currentExists);
        if (!baseValue.isArray() || !currentValue.isArray())
        {
            for (int idx : groupIt.value())
            {
                MergeItem item;
                item.change = changes[idx];
                item.state = "Invalid";
                item.current = currentValue;
                r.items.append(item);
                handled.insert(idx);
            }
            continue;
        }
        QHash<QByteArray, int> bc, cc, removeCounts, addCounts;
        QHash<QByteArray, QJsonValue> values;
        for (const auto &v : baseValue.toArray())
        {
            const auto k = valueBytes(v);
            ++bc[k];
            values.insert(k, canon(v));
        }
        for (const auto &v : currentValue.toArray())
        {
            const auto k = valueBytes(v);
            ++cc[k];
            values.insert(k, canon(v));
        }
        QHash<QByteArray, int> novelBeats, novelSignatures;
        if (pathDomain(path) == "Note")
            for (auto it = cc.cbegin(); it != cc.cend(); ++it)
                if (it.value() > bc.value(it.key()))
                {
                    const QJsonValue v = values.value(it.key());
                    const auto beat = timingBeatKey(v), sig = noteSignature(v);
                    if (!beat.isEmpty())
                        ++novelBeats[beat];
                    if (!sig.isEmpty())
                        ++novelSignatures[sig];
                }
        QHash<QString, int> ordinals, sourceOrdinals;
        for (int idx : groupIt.value())
        {
            handled.insert(idx);
            const Change &c = changes[idx];
            MergeItem item;
            item.change = c;
            item.current = currentValue;
            if (!acceptedIds.isEmpty() && !acceptedIds.contains(c.changeId))
            {
                item.state = "Pending";
                r.items.append(item);
                continue;
            }
            if (resolutions.contains(c.changeId))
            {
                const auto replacement = resolutions.value(c.changeId);
                if (!replacement.isNull())
                {
                    const auto key = valueBytes(replacement);
                    if (!replacement.isObject() || cc.value(key) <= removeCounts.value(key))
                    {
                        item.state = "Conflict";
                        r.items.append(item);
                        continue;
                    }
                    ++removeCounts[key];
                }
                if (!c.after.isUndefined())
                {
                    const auto key = valueBytes(c.after);
                    values[key] = canon(c.after);
                    ++addCounts[key];
                }
                item.state = replacement.isNull() && c.after.isUndefined() ? "AlreadySatisfied" : "Applied";
            }
            else if (c.kind == "Add" || c.kind == "Remove")
            {
                const QJsonValue target = c.kind == "Add" ? c.after : c.before;
                const QByteArray k = valueBytes(target);
                values.insert(k, canon(target));
                const QString ordinalKey = c.kind + "|" + QString::fromLatin1(k.toHex());
                const int ordinal = ++ordinals[ordinalKey];
                const int baseCount = bc.value(k), currentCount = cc.value(k);
                if (c.kind == "Add")
                {
                    if (currentCount >= baseCount + ordinal)
                        item.state = "AlreadySatisfied";
                    else
                    {
                        ++addCounts[k];
                        item.state = "Applied";
                    }
                }
                else
                {
                    const int targetCount = baseCount - ordinal;
                    if (currentCount <= targetCount)
                    {
                        const QJsonValue removed = canon(target);
                        const auto beat = timingBeatKey(removed), sig = noteSignature(removed);
                        if (pathDomain(path) == "Note" && currentCount < baseCount
                            && ((!beat.isEmpty() && novelBeats.value(beat) > 0)
                                || (!sig.isEmpty() && novelSignatures.value(sig) > 0)))
                            item.state = "Conflict";
                        else
                            item.state = "AlreadySatisfied";
                    }
                    else
                    {
                        ++removeCounts[k];
                        item.state = "Applied";
                    }
                }
            }
            else
            { // Explicit Move or a uniquely matched timing-entry edit.
                const QByteArray from = valueBytes(c.before), to = valueBytes(c.after);
                values.insert(from, canon(c.before));
                values.insert(to, canon(c.after));
                const int baseFrom = bc.value(from), currentFrom = cc.value(from);
                const int baseTo = bc.value(to), currentTo = cc.value(to);
                const QString transform =
                    c.kind + "|" + QString::fromLatin1(from.toHex()) + "|" + QString::fromLatin1(to.toHex());
                const QString ordinalKey = transform;
                const int ordinal = ++ordinals[ordinalKey];
                const int sourceOrdinal = ++sourceOrdinals[QString::fromLatin1(from.toHex())];
                if (sourceOrdinal > baseFrom)
                    item.state = "Conflict";
                else if (currentFrom < baseFrom && currentTo >= baseTo + ordinal)
                    item.state = "AlreadySatisfied";
                else if (currentFrom < baseFrom)
                    item.state = "Conflict";
                else
                {
                    ++removeCounts[from];
                    ++addCounts[to];
                    item.state = "Applied";
                }
            }
            r.items.append(item);
        }
        QJsonArray rebuilt;
        QHash<QByteArray, int> consumed;
        for (const auto &v : currentValue.toArray())
        {
            const QByteArray k = valueBytes(v);
            if (consumed.value(k) < removeCounts.value(k))
            {
                ++consumed[k];
                continue;
            }
            rebuilt.append(v);
        }
        for (auto it = addCounts.cbegin(); it != addCounts.cend(); ++it)
            for (int n = 0; n < it.value(); ++n)
                rebuilt.append(values.value(it.key()));
        if (rebuilt != currentValue.toArray())
            setAt(candidate, path, rebuilt, false);
    }
    for (const Change &c : changes)
    {
        const int index = int(&c - changes.constData());
        if (handled.contains(index))
            continue;
        MergeItem item;
        item.change = c;
        bool exists = false;
        item.current = getAt(candidate, c.path, &exists);
        const bool accepted = acceptedIds.isEmpty() || acceptedIds.contains(c.changeId);
        if (!accepted)
        {
            item.state = "Pending";
            r.items.append(item);
            continue;
        }
        if (c.path.isEmpty() || !c.path.startsWith('/'))
        {
            item.state = "Invalid";
            r.items.append(item);
            continue;
        }
        const auto expected = resolutions.contains(c.changeId) ? resolutions.value(c.changeId) : c.before;
        const bool beforeExists = !expected.isUndefined(), afterExists = !c.after.isUndefined();
        if (exists == afterExists && (!exists || equal(item.current, c.after)))
        {
            item.state = "AlreadySatisfied";
            r.items.append(item);
            continue;
        }
        const bool currentMatchesBefore = exists == beforeExists && (!exists || equal(item.current, expected));
        if (!currentMatchesBefore)
        {
            item.state = "Conflict";
            r.items.append(item);
            continue;
        }
        if (c.kind == "Remove")
        {
            setAt(candidate, c.path, {}, true);
            item.state = "Applied";
        }
        else if (c.kind == "Add" || c.kind == "Modify")
        {
            if (!afterExists || !setAt(candidate, c.path, c.after, false))
                item.state = "Invalid";
            else
                item.state = "Applied";
        }
        else if (c.kind == "Move" && c.moveIntent)
        { // Array move is remove+append and preserves explicit intent.
            bool arrExists = false;
            QJsonValue av = getAt(candidate, c.path, &arrExists);
            if (!av.isArray())
                item.state = "Invalid";
            else
            {
                QJsonArray a = av.toArray();
                int idx = -1;
                for (int i = 0; i < a.size(); ++i)
                    if (equal(a[i], c.before))
                    {
                        idx = i;
                        break;
                    }
                if (idx < 0)
                    item.state = "Conflict";
                else
                {
                    a.removeAt(idx);
                    a.append(c.after);
                    setAt(candidate, c.path, a, false);
                    item.state = "Applied";
                }
            }
        }
        else
            item.state = "Invalid";
        r.items.append(item);
    }
    QHash<QString, int> itemOrder;
    for (int i = 0; i < changes.size(); ++i)
        itemOrder.insert(changes[i].changeId, i);
    std::sort(r.items.begin(), r.items.end(), [&](const MergeItem &a, const MergeItem &b) {
        return itemOrder.value(a.change.changeId) < itemOrder.value(b.change.changeId);
    });
    if (!r.hasConflicts())
    {
        r.result = candidate;
        r.committed = true;
    }
    return r;
}

QJsonObject createRevision(const QJsonObject &base, const QJsonObject &proposal, const QJsonObject &author,
                           const QString &description, const QVector<Change> &intents)
{
    QJsonArray cs;
    for (const auto &c : diff(base, proposal, intents))
        cs.append(changeToJson(c));
    const QString id = uuid();
    return {{"formatVersion", 1},
            {"proposalId", uuid()},
            {"revisionId", id},
            {"parentRevisionIds", QJsonArray{}},
            {"ancestors", QJsonArray{}},
            {"author", author},
            {"description", description},
            {"baseDocument", base},
            {"baseSnapshot", base},
            {"baseSnapshotHash", QString::fromLatin1(canonicalHash(base))},
            {"changes", cs},
            {"comments", QJsonArray{}},
            {"reviewDecisions", QJsonArray{}},
            {"events", QJsonArray{}},
            {"resources", QJsonObject{}},
            {"revisionHistory", QJsonArray{}},
            {"mergeConflicts", QJsonArray{}}};
}
bool updateRevision(QJsonObject &revision, const QJsonObject &proposal, const QJsonObject &author,
                    const QVector<Change> &intents, QString *error, const QJsonObject &reviewState)
{
    if (!validateRevision(revision, error))
        return false;
    const QJsonObject base = revision.value("baseSnapshot").toObject();
    QVector<Change> effectiveIntents = intents;
    for (const auto &v : revision.value("changes").toArray())
    {
        Change prior;
        if (changeFromJson(v.toObject(), prior, nullptr) && (prior.moveIntent || prior.kind == "Modify"))
            effectiveIntents.append(prior);
    }
    QVector<Change> next = diff(base, proposal, effectiveIntents);
    QVector<Change> old;
    for (const auto &v : revision.value("changes").toArray())
    {
        Change c;
        if (changeFromJson(v.toObject(), c, nullptr))
            old.append(c);
    }
    QSet<QString> explicitIds;
    for (const auto &intent : intents)
        if (!intent.changeId.isEmpty())
            explicitIds.insert(intent.changeId);
    QHash<QString, QVector<int>> oldByPath, oldByExact;
    auto stableChangeKey = [](const Change &c) {
        return c.path + "|" + c.kind + "|" + QString::fromLatin1(valueBytes(c.before).toHex()) + "|"
               + QString::fromLatin1(valueBytes(c.after).toHex()) + "|" + QString::number(c.moveIntent);
    };
    for (int i = 0; i < old.size(); ++i)
    {
        oldByPath[old[i].path].append(i);
        oldByExact[stableChangeKey(old[i])].append(i);
    }
    QSet<QString> consumed;
    for (auto &c : next)
    {
        if (explicitIds.contains(c.changeId))
        {
            consumed.insert(c.changeId);
            continue;
        }
        auto &matches = oldByExact[stableChangeKey(c)];
        while (!matches.isEmpty())
        {
            const auto &prior = old[matches.takeLast()];
            if (consumed.contains(prior.changeId))
                continue;
            c.changeId = prior.changeId;
            c.operationGroupId = prior.operationGroupId;
            consumed.insert(prior.changeId);
            break;
        }
        if (!consumed.contains(c.changeId) && !multisetKey(c.path) && oldByPath.value(c.path).size() == 1)
        {
            const Change &prior = old[oldByPath.value(c.path).first()];
            if (!consumed.contains(prior.changeId))
            {
                c.changeId = prior.changeId;
                c.operationGroupId = prior.operationGroupId;
                consumed.insert(prior.changeId);
            }
        }
        if (c.changeId.isEmpty())
            c.changeId = uuid();
    }
    assignDependencies(base, proposal, next);
    QJsonArray changes;
    for (const auto &c : next)
        changes.append(changeToJson(c));
    const QString oldId = revision.value("revisionId").toString();
    if (oldId.isEmpty())
        return fail(error, "Revision ID is missing");
    QJsonArray parents{oldId};
    QSet<QString> ancestors;
    for (const auto &s : toStrings(revision.value("ancestors")))
        ancestors.insert(s);
    ancestors.insert(oldId);
    QStringList sorted = ancestors.values();
    std::sort(sorted.begin(), sorted.end());
    QJsonArray history = revision.value("revisionHistory").toArray();
    const QJsonObject historyEntry = revisionState(revision);
    bool known = false;
    for (const auto &v : history)
        if (v.toObject().value("revisionId").toString() == oldId)
        {
            known = true;
            break;
        }
    if (!known)
        history.append(historyEntry);
    QJsonObject candidate = revision;
    candidate.insert("revisionId", uuid());
    candidate.insert("parentRevisionIds", parents);
    candidate.insert("ancestors", stringArray(sorted));
    candidate.insert("author", author);
    candidate.insert("changes", changes);
    candidate.insert("revisionHistory", history);
    QJsonArray events = candidate.value("events").toArray();
    if (reviewState.contains("comments"))
        candidate.insert("comments", reviewState.value("comments"));
    if (reviewState.contains("reviewDecisions"))
        candidate.insert("reviewDecisions", reviewState.value("reviewDecisions"));
    if (reviewState.contains("events"))
        events = reviewState.value("events").toArray();
    if (reviewState.contains("resources"))
        candidate.insert("resources", reviewState.value("resources"));
    if (reviewState.contains("mergeConflicts"))
        candidate.insert("mergeConflicts", reviewState.value("mergeConflicts"));
    QHash<QString, Change> oldById, nextById;
    for (const auto &change : old)
        oldById.insert(change.changeId, change);
    for (const auto &change : next)
        nextById.insert(change.changeId, change);
    QJsonArray revalidated;
    for (const auto &value : candidate.value("reviewDecisions").toArray())
    {
        auto decision = value.toObject();
        const auto id = decision.value("changeId").toString();
        if (!nextById.contains(id))
            continue;
        const auto fingerprint = changeFingerprint(nextById.value(id));
        auto prior = decision.value("changeHash").toString();
        if (prior.isEmpty() && oldById.contains(id))
            prior = changeFingerprint(oldById.value(id));
        if (prior != fingerprint)
            decision.insert("decision", "Pending");
        decision.insert("changeHash", fingerprint);
        revalidated.append(decision);
    }
    candidate.insert("reviewDecisions", revalidated);
    const bool unchanged = equal(candidate.value("changes"), revision.value("changes"))
                           && equal(candidate.value("comments"), revision.value("comments"))
                           && equal(candidate.value("reviewDecisions"), revision.value("reviewDecisions"))
                           && equal(candidate.value("mergeConflicts"), revision.value("mergeConflicts"))
                           && equal(candidate.value("resources"), revision.value("resources"))
                           && equal(events, revision.value("events").toArray());
    if (unchanged)
        return true;
    events.append(QJsonObject{{"eventId", uuid()},
                              {"type", "UpdateProposal"},
                              {"author", author},
                              {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                              {"revisionId", candidate.value("revisionId")}});
    candidate.insert("events", events);
    if (!validateRevision(candidate, error))
        return false;
    revision = candidate;
    return true;
}
bool rebaseRevision(QJsonObject &revision, const QJsonObject &base, const QJsonObject &proposal,
                    const QJsonObject &author, const QVector<Change> &intents, QString *error)
{
    if (!validateRevision(revision, error))
        return false;
    auto candidate = revision;
    auto history = revision.value("revisionHistory").toArray();
    history.append(revisionState(revision));
    candidate.insert("revisionHistory", history);
    candidate.insert("revisionId", uuid());
    candidate.insert("parentRevisionIds", QJsonArray{revision.value("revisionId")});
    auto ancestors = revision.value("ancestors").toArray();
    ancestors.append(revision.value("revisionId"));
    candidate.insert("ancestors", ancestors);
    candidate.insert("baseDocument", base);
    candidate.insert("baseSnapshot", base);
    candidate.insert("baseSnapshotHash", QString::fromLatin1(canonicalHash(base)));
    candidate.insert("author", author);
    // Decisions were made against the previous base/change identities. The old
    // values remain in revisionHistory; review starts pending on the new base.
    candidate.insert("reviewDecisions", QJsonArray{});
    QJsonArray changes;
    for (const auto &c : diff(base, proposal, intents))
        changes.append(changeToJson(c));
    candidate.insert("changes", changes);
    auto events = candidate.value("events").toArray();
    events.append(QJsonObject{{"eventId", uuid()},
                              {"type", "Rebase"},
                              {"author", author},
                              {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    candidate.insert("events", events);
    if (!validateRevision(candidate, error))
        return false;
    revision = candidate;
    return true;
}
bool validateRevision(const QJsonObject &r, QString *error)
{
    if (r.value("formatVersion").toInt(-1) != 1)
        return fail(error, "Unsupported revision formatVersion");
    for (const auto &k : {"proposalId", "revisionId"})
        if (!r.value(k).isString() || r.value(k).toString().isEmpty())
            return fail(error, QString("Missing revision field: ") + k);
    if (!r.value("baseSnapshot").isObject() || !r.value("baseDocument").isObject())
        return fail(error, "Revision base snapshot is missing");
    if (r.value("baseSnapshotHash").toString()
        != QString::fromLatin1(canonicalHash(r.value("baseSnapshot").toObject())))
        return fail(error, "Base snapshot hash mismatch");
    if (!r.value("changes").isArray() || !r.value("comments").isArray() || !r.value("reviewDecisions").isArray()
        || !r.value("events").isArray() || !r.value("resources").isObject())
        return fail(error, "Malformed revision collections");
    if (r.contains("revisionHistory"))
    {
        if (!r.value("revisionHistory").isArray() || r.value("revisionHistory").toArray().size() > 10000)
            return fail(error, "Malformed or excessive revisionHistory");
        QSet<QString> historyIds;
        for (const auto &v : r.value("revisionHistory").toArray())
        {
            if (!v.isObject())
                return fail(error, "Malformed revision history record");
            const auto h = v.toObject();
            const QString id = h.value("revisionId").toString();
            if (id.isEmpty() || historyIds.contains(id) || !h.value("changes").isArray()
                || !h.value("comments").isArray() || !h.value("reviewDecisions").isArray())
                return fail(error, "Invalid revision history record");
            historyIds.insert(id);
        }
    }
    if (r.contains("mergeConflicts") && !r.value("mergeConflicts").isArray())
        return fail(error, "Malformed mergeConflicts");
    if (!validateGraph(r, error))
        return false;
    if (r.value("changes").toArray().size() > 500000 || r.value("comments").toArray().size() > 1000000
        || r.value("events").toArray().size() > 1000000)
        return fail(error, "Revision collection limit exceeded");
    QVector<Change> changes;
    QSet<QString> changeIds;
    for (const auto &v : r.value("changes").toArray())
    {
        Change c;
        if (!v.isObject() || !changeFromJson(v.toObject(), c, error))
            return false;
        if (changeIds.contains(c.changeId))
            return fail(error, "Duplicate changeId");
        changeIds.insert(c.changeId);
        changes.append(c);
    }
    for (const auto &change : changes)
        for (const auto &id : change.dependsOn)
            if (!changeIds.contains(id))
                return fail(error, "Dependency refers to a missing change");
    MergeResult rebuilt = merge(r.value("baseSnapshot").toObject(), r.value("baseSnapshot").toObject(), changes);
    if (!rebuilt.committed)
    {
        QStringList details;
        for (const auto &item : rebuilt.items)
            if (item.state == "Conflict" || item.state == "Invalid")
                details.append(item.change.path + " (" + item.state + ")");
        return fail(error, "Revision changes cannot be reconstructed: " + details.join(", "));
    }
    if (!validateSnapshot(r.value("baseSnapshot").toObject(), error) || !validateSnapshot(rebuilt.result, error))
        return false;
    QSet<QString> ids;
    for (const auto &v : r.value("comments").toArray())
    {
        if (!v.isObject())
            return fail(error, "Malformed comment");
        const auto o = v.toObject();
        const auto id = o.value("commentId").toString();
        if (id.isEmpty() || ids.contains(id))
            return fail(error, "Duplicate or missing commentId");
        ids.insert(id);
        if (o.value("threadId").toString().isEmpty() || !o.value("author").isObject() || !o.value("body").isString()
            || !o.value("anchor").isObject())
            return fail(error, "Comment missing required data");
        const QString state = o.value("state").toString();
        if (state != "Resolved" && state != "Unresolved")
            return fail(error, "Unknown comment state");
        if (!o.value("tombstone").isBool())
            return fail(error, "Comment tombstone must be boolean");
    }
    QSet<QString> eventIds;
    for (const auto &v : r.value("events").toArray())
    {
        if (!v.isObject())
            return fail(error, "Malformed event");
        const auto id = v.toObject().value("eventId").toString();
        if (id.isEmpty() || eventIds.contains(id))
            return fail(error, "Duplicate or missing eventId");
        eventIds.insert(id);
    }
    const QJsonObject resources = r.value("resources").toObject();
    qint64 totalResourceBytes = 0;
    for (auto it = resources.begin(); it != resources.end(); ++it)
    {
        if (!it.value().isObject())
            return fail(error, "Resource entry must contain dataBase64 and paths");
        const QJsonObject entry = it.value().toObject();
        const QByteArray encoded = entry.value("dataBase64").toString().toLatin1();
        QByteArray raw = QByteArray::fromBase64(encoded);
        if (raw.toBase64() != encoded)
            return fail(error, "Invalid resource base64 encoding");
        totalResourceBytes += raw.size();
        if (totalResourceBytes > MaxExpanded)
            return fail(error, "Aggregate resource size exceeds limit");
        if (raw.size() > MaxResource
            || QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex()) != it.key())
            return fail(error, "Resource hash or size mismatch");
        if (!entry.value("paths").isArray())
            return fail(error, "Resource paths must be an array");
        for (const auto &p : entry.value("paths").toArray())
            if (!p.isString() || !validRelativePath(p.toString()))
                return fail(error, "Unsafe resource path");
    }
    return true;
}

QJsonObject makeComment(const QString &threadId, const QString &commentId, const QJsonObject &author,
                        const QString &body, const QJsonObject &anchor, bool resolved)
{
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return {{"threadId", threadId}, {"commentId", commentId}, {"author", author},
            {"body", body},         {"anchor", anchor},       {"state", resolved ? "Resolved" : "Unresolved"},
            {"tombstone", false},   {"createdAt", now},       {"updatedAt", now}};
}
QJsonArray mergeById(const QJsonArray &local, const QJsonArray &incoming, const QString &idField,
                     QStringList *conflicts)
{
    QJsonArray out = local;
    QHash<QString, int> positions;
    for (int i = 0; i < out.size(); ++i)
        if (out[i].isObject())
        {
            QString id = out[i].toObject().value(idField).toString();
            if (!id.isEmpty())
                positions.insert(id, i);
        }
    for (const auto &v : incoming)
    {
        if (!v.isObject())
        {
            if (conflicts)
                conflicts->append("Malformed " + idField + " record");
            continue;
        }
        QJsonObject o = v.toObject();
        QString id = o.value(idField).toString();
        if (id.isEmpty())
        {
            if (conflicts)
                conflicts->append("Missing " + idField);
            continue;
        }
        if (!positions.contains(id))
        {
            positions.insert(id, out.size());
            out.append(o);
            continue;
        }
        int i = positions.value(id);
        QJsonObject old = out[i].toObject();
        if (equal(old, o))
            continue;
        // Tombstones are monotonic; older live records cannot resurrect a deletion.
        if (old.value("tombstone").toBool())
            continue;
        if (o.value("tombstone").toBool())
        {
            out[i] = o;
            continue;
        }
        bool recordConflict = false;
        QJsonArray alternatives = old.value("_conflictAlternatives").toArray();
        QStringList keys = o.keys();
        for (const QString &k : keys)
            if (old.contains(k) && !equal(old.value(k), o.value(k)))
            {
                // A field-level three-way ancestry merge is not available for comments: report divergence.
                recordConflict = true;
                if (conflicts)
                    conflicts->append(id + ":" + k);
                QJsonObject alt{{"field", k}, {"local", old.value(k)}, {"incoming", o.value(k)}};
                if (!alternatives.contains(alt))
                    alternatives.append(alt);
            }
        if (recordConflict)
        {
            old.insert("_conflictAlternatives", alternatives);
            out[i] = old;
        }
        else
            out[i] = o;
    }
    return out;
}
QJsonArray mergeComments(const QJsonArray &local, const QJsonArray &incoming, QStringList *conflicts)
{
    return mergeById(local, incoming, "commentId", conflicts);
}

QJsonObject mergeRevisions(const QJsonObject &local, const QJsonObject &incoming, QStringList *conflicts,
                           QString *error)
{
    if (conflicts)
        conflicts->clear();
    if (error)
        error->clear();
    if (!validateRevision(local, error) || !validateRevision(incoming, error))
        return {};
    if (local.value("proposalId") != incoming.value("proposalId"))
    {
        fail(error, "Cannot merge different proposals");
        return {};
    }
    if (local.value("baseSnapshotHash") != incoming.value("baseSnapshotHash"))
    {
        fail(error, "Rebase required: revision bases differ");
        return {};
    }
    const QString lid = local.value("revisionId").toString(), iid = incoming.value("revisionId").toString();
    if (lid == iid)
    {
        if (!equal(revisionState(local), revisionState(incoming))
            || !equal(local.value("resources"), incoming.value("resources")))
        {
            fail(error, "The same revisionId has different contents");
            return {};
        }
        return local;
    }
    const QStringList la = toStrings(local.value("ancestors")), ia = toStrings(incoming.value("ancestors"));
    QHash<QString, QJsonObject> localRecords, incomingRecords;
    for (const auto &v : local.value("revisionHistory").toArray())
        localRecords[v.toObject().value("revisionId").toString()] = v.toObject();
    for (const auto &v : incoming.value("revisionHistory").toArray())
        incomingRecords[v.toObject().value("revisionId").toString()] = v.toObject();
    localRecords[lid] = revisionState(local);
    incomingRecords[iid] = revisionState(incoming);
    for (auto it = localRecords.cbegin(); it != localRecords.cend(); ++it)
        if (incomingRecords.contains(it.key()) && !equal(it.value(), incomingRecords.value(it.key())))
        {
            fail(error, "Common revision payload differs between branches");
            return {};
        }
    const auto historyHas = [](const QJsonObject &r, const QString &id) {
        for (const auto &v : r.value("revisionHistory").toArray())
            if (v.toObject().value("revisionId").toString() == id)
                return true;
        return false;
    };
    if (la.contains(iid) || historyHas(local, iid))
        return local;
    if (ia.contains(lid) || historyHas(incoming, lid))
        return incoming;
    QSet<QString> common;
    for (const auto &x : la)
        if (ia.contains(x))
            common.insert(x);
    bool related = !common.isEmpty() || local.value("parentRevisionIds").toArray().contains(iid)
                   || incoming.value("parentRevisionIds").toArray().contains(lid);
    if (!related)
    {
        fail(error, "No common revision ancestor recorded");
        return {};
    }
    QString ancestorId;
    const QJsonObject ancestor = ancestorState(local, incoming, &ancestorId);
    if (ancestor.isEmpty())
    {
        fail(error, "No common ancestor payload recorded");
        return {};
    }
    const QJsonArray baseChanges = ancestor.value("changes").toArray();
    QJsonArray combined = local.value("changes").toArray();
    QHash<QString, int> byId;
    QHash<QString, QVector<int>> byPath, byTimingSource;
    QHash<QString, QJsonObject> baseChangeById;
    for (const auto &v : baseChanges)
        if (v.isObject())
            baseChangeById.insert(v.toObject().value("changeId").toString(), v.toObject());
    QHash<QString, QJsonObject> incomingById;
    for (const auto &v : incoming.value("changes").toArray())
        incomingById[v.toObject().value("changeId").toString()] = v.toObject();
    QJsonArray retained;
    for (const auto &v : combined)
    {
        const auto c = v.toObject();
        const QString id = c.value("changeId").toString();
        if (baseChangeById.contains(id) && !incomingById.contains(id) && equal(c, baseChangeById.value(id)))
            continue;
        retained.append(c);
    }
    combined = retained;
    auto timingSourceKey = [](const QJsonObject &c) {
        return c.value("path").toString() + "|" + QString::fromLatin1(valueBytes(c.value("before")).toHex());
    };
    for (int i = 0; i < combined.size(); ++i)
    {
        const auto c = combined[i].toObject();
        byId.insert(c.value("changeId").toString(), i);
        byPath[c.value("path").toString()].append(i);
        if (multisetKey(c.value("path").toString()) && c.value("kind").toString() != "Add")
            byTimingSource[timingSourceKey(c)].append(i);
    }
    QJsonArray mergeConflictsJson = local.value("mergeConflicts").toArray();
    for (const auto &v : incoming.value("mergeConflicts").toArray())
        if (!mergeConflictsJson.contains(v))
            mergeConflictsJson.append(v);
    QStringList foundConflicts;
    auto addChangeConflict = [&](const QJsonObject &l, const QJsonObject &r, const QString &reason) {
        QJsonObject b = baseChangeById.value(l.value("changeId").toString());
        if (b.isEmpty())
            b = baseChangeById.value(r.value("changeId").toString());
        QJsonObject item{{"type", "Change"}, {"path", l.value("path")}, {"base", b},
                         {"local", l},       {"incoming", r},           {"reason", reason}};
        mergeConflictsJson.append(item);
        foundConflicts.append(l.value("path").toString());
    };
    for (const auto &v : incoming.value("changes").toArray())
    {
        QJsonObject c = v.toObject();
        QString id = c.value("changeId").toString();
        if (byId.contains(id))
        {
            const QJsonObject old = combined[byId.value(id)].toObject();
            if (!equal(old, c))
            {
                const auto prior = baseChangeById.value(id);
                if (!prior.isEmpty() && equal(old, prior))
                    combined[byId.value(id)] = c;
                else if (prior.isEmpty() || !equal(c, prior))
                    addChangeConflict(old, c, "same changeId diverged");
            }
            continue;
        }
        if (baseChangeById.contains(id))
        {
            if (!equal(c, baseChangeById.value(id)))
                addChangeConflict({}, c, "removed change edited concurrently");
            continue;
        }
        bool collides = false;
        if (multisetKey(c.value("path").toString()) && c.value("kind").toString() != "Add")
            for (int index : byTimingSource.value(timingSourceKey(c)))
            {
                const QJsonObject old = combined[index].toObject();
                if (old.value("kind").toString() != "Add" && equal(old.value("before"), c.value("before")))
                {
                    if (!equal(old.value("after"), c.value("after")))
                        addChangeConflict(old, c, "same source changed differently");
                    collides = true;
                    break;
                }
            }
        if (!multisetKey(c.value("path").toString()))
            for (int index : byPath.value(c.value("path").toString()))
            {
                const QJsonObject old = combined[index].toObject();
                if (!equal(old.value("after"), c.value("after")) || !equal(old.value("before"), c.value("before")))
                {
                    addChangeConflict(old, c, "same field changed differently");
                    collides = true;
                    break;
                }
            }
        if (collides)
            continue;
        combined.append(c);
        byId.insert(id, combined.size() - 1);
        byPath[c.value("path").toString()].append(combined.size() - 1);
        if (multisetKey(c.value("path").toString()) && c.value("kind").toString() != "Add")
            byTimingSource[timingSourceKey(c)].append(combined.size() - 1);
    }
    QJsonObject out = local;
    out.insert("revisionId", uuid());
    out.insert("parentRevisionIds", QJsonArray{lid, iid});
    QJsonObject resources = local.value("resources").toObject();
    const auto incomingResources = incoming.value("resources").toObject();
    for (auto it = incomingResources.begin(); it != incomingResources.end(); ++it)
    {
        auto entry = resources.value(it.key()).toObject();
        if (entry.isEmpty())
        {
            resources.insert(it.key(), it.value());
            continue;
        }
        auto paths = entry.value("paths").toArray();
        for (const auto &p : it.value().toObject().value("paths").toArray())
            if (!paths.contains(p))
                paths.append(p);
        entry.insert("paths", paths);
        resources.insert(it.key(), entry);
    }
    out.insert("resources", resources);
    QSet<QString> ancestors;
    for (auto x : la)
        ancestors.insert(x);
    for (auto x : ia)
        ancestors.insert(x);
    ancestors.insert(lid);
    ancestors.insert(iid);
    QStringList sorted = ancestors.values();
    std::sort(sorted.begin(), sorted.end());
    out.insert("ancestors", stringArray(sorted));
    out.insert("changes", combined);
    QStringList recordConflicts;
    out.insert("comments",
               threeWayRecords(ancestor.value("comments").toArray(), local.value("comments").toArray(),
                               incoming.value("comments").toArray(), "commentId", "Comment", &recordConflicts));
    out.insert("reviewDecisions",
               threeWayRecords(ancestor.value("reviewDecisions").toArray(), local.value("reviewDecisions").toArray(),
                               incoming.value("reviewDecisions").toArray(), "changeId", "Decision", &recordConflicts));
    QStringList eventConflicts;
    out.insert("events", mergeById(local.value("events").toArray(), incoming.value("events").toArray(), "eventId",
                                   &eventConflicts));
    for (const auto &conflict : eventConflicts)
        recordConflicts.append("Event:" + conflict);
    for (const auto &c : recordConflicts)
        mergeConflictsJson.append(QJsonObject{{"type", "Record"}, {"path", c}});
    out.insert("mergeConflicts", mergeConflictsJson);
    QJsonArray history = local.value("revisionHistory").toArray();
    QSet<QString> historyIds;
    for (const auto &v : history)
        historyIds.insert(v.toObject().value("revisionId").toString());
    for (const auto &v : incoming.value("revisionHistory").toArray())
        if (!historyIds.contains(v.toObject().value("revisionId").toString()))
        {
            history.append(v);
            historyIds.insert(v.toObject().value("revisionId").toString());
        }
    const auto localState = revisionState(local), incomingState = revisionState(incoming);
    if (!historyIds.contains(lid))
        history.append(localState);
    if (!historyIds.contains(iid))
        history.append(incomingState);
    out.insert("revisionHistory", history);
    QJsonArray uniqueMergeConflicts;
    QSet<QByteArray> conflictKeys;
    for (const auto &v : mergeConflictsJson)
    {
        const QByteArray k = valueBytes(canon(v));
        if (!conflictKeys.contains(k))
        {
            conflictKeys.insert(k);
            uniqueMergeConflicts.append(v);
        }
    }
    out.insert("mergeConflicts", uniqueMergeConflicts);
    if (conflicts)
    {
        *conflicts = foundConflicts;
        conflicts->append(recordConflicts);
    }
    if (!refreshRevisionDependencies(out, error) || !validateRevision(out, error))
        return {};
    return out;
}

static bool validatePackageResources(const QJsonObject &revision, QString *error)
{
    const auto payloads = revision.value("resources").toObject();
    QVector<Change> changes;
    for (const auto &value : revision.value("changes").toArray())
    {
        Change change;
        if (!changeFromJson(value.toObject(), change, error))
            return false;
        changes.append(change);
    }
    const auto base = revision.value("baseSnapshot").toObject();
    const auto proposed = merge(base, base, changes);
    if (!proposed.committed)
        return fail(error, "Cannot reconstruct package resources");
    for (auto snapshot : {base, proposed.result})
    {
        const auto paths = snapshot.value("resources").toObject();
        for (auto it = paths.begin(); it != paths.end(); ++it)
            if (!validRelativePath(it.key()) || !it.value().isString() || !payloads.contains(it.value().toString()))
                return fail(error, "Missing or invalid resource mapping: " + it.key());
        if (!snapshot.contains("activeDifficultyId"))
            snapshot["activeDifficultyId"] = snapshot.value("difficulties").toObject().keys().value(0);
        CceDocument::Project project;
        if (!CceDocument::Project::fromJson(snapshot, project, error))
            return false;
        for (const auto &path : CceDocument::requiredResourcePaths(project))
            if (!paths.contains(path))
                return fail(error, "Required resource mapping is missing: " + path);
    }
    return true;
}

bool writePackage(const QString &path, const QJsonObject &revision, QString *error)
{
    if (!validateRevision(revision, error) || !validatePackageResources(revision, error))
        return false;
    QByteArray json = QJsonDocument(revision).toJson(QJsonDocument::Compact);
    if (json.size() > MaxExpanded)
        return fail(error, "Revision exceeds package size limit");
    QByteArray compressed = qCompress(json, 9);
    if (compressed.size() > MaxCompressed)
        return fail(error, "Compressed package exceeds size limit");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, file.errorString());
    if (file.write(PackageMagic) != PackageMagic.size() || file.write(compressed) != compressed.size())
    {
        file.cancelWriting();
        return fail(error, file.errorString());
    }
    if (!file.commit())
        return fail(error, file.errorString());
    return true;
}
bool resolveRevisionConflict(QJsonObject &revision, int index, bool incoming, const QJsonObject &author, QString *error)
{
    if (!validateRevision(revision, error))
        return false;
    auto conflicts = revision.value("mergeConflicts").toArray();
    if (index < 0 || index >= conflicts.size())
        return fail(error, "Choose a revision conflict");
    const auto conflict = conflicts[index].toObject();
    QVector<Change> changes;
    QJsonObject state{{"comments", revision.value("comments")},
                      {"reviewDecisions", revision.value("reviewDecisions")},
                      {"events", revision.value("events")}};
    if (conflict.value("type") == "Change")
    {
        const auto local = conflict.value("local").toObject(), remote = conflict.value("incoming").toObject();
        const auto chosen = incoming ? remote : local;
        for (const auto &v : revision.value("changes").toArray())
        {
            const auto o = v.toObject();
            if (o.value("changeId") == local.value("changeId") || o.value("changeId") == remote.value("changeId"))
                continue;
            Change c;
            if (!changeFromJson(o, c, error))
                return false;
            changes.append(c);
        }
        if (!chosen.isEmpty())
        {
            Change c;
            if (!changeFromJson(chosen, c, error))
                return false;
            changes.append(c);
        }
    }
    else if (conflict.value("type") == "Record")
    {
        const auto path = conflict.value("path").toString().split(':');
        if (path.size() < 3)
            return fail(error, "Malformed record conflict");
        const QString collection = path[0] == "Comment" ? "comments"
                                   : path[0] == "Event" ? "events"
                                                        : "reviewDecisions";
        const QString idField = collection == "comments" ? "commentId"
                                : collection == "events" ? "eventId"
                                                         : "changeId";
        auto records = state.value(collection).toArray();
        bool found = false;
        for (int i = 0; i < records.size(); ++i)
        {
            auto record = records[i].toObject();
            if (record.value(idField) != path[1])
                continue;
            auto alternatives = record.value("_conflictAlternatives").toArray();
            QJsonArray remaining;
            for (const auto &v : alternatives)
            {
                const auto alt = v.toObject();
                if (alt.value("field") != path[2])
                {
                    remaining.append(v);
                    continue;
                }
                const QString side = incoming ? "incoming" : "local";
                if (path[2] == "deletion")
                {
                    record = alt.value(side).toObject();
                    record.remove("_conflictAlternatives");
                }
                else if (alt.contains(side))
                    record.insert(path[2], alt.value(side));
                else
                    record.remove(path[2]);
                found = true;
            }
            if (remaining.isEmpty())
                record.remove("_conflictAlternatives");
            else
                record.insert("_conflictAlternatives", remaining);
            records[i] = record;
        }
        if (!found)
            return fail(error, "Record conflict alternative is missing");
        state.insert(collection, records);
        for (const auto &v : revision.value("changes").toArray())
        {
            Change c;
            if (!changeFromJson(v.toObject(), c, error))
                return false;
            changes.append(c);
        }
    }
    else
        return fail(error, "Unsupported revision conflict");
    conflicts.removeAt(index);
    state.insert("mergeConflicts", conflicts);
    const auto base = revision.value("baseSnapshot").toObject();
    const auto rebuilt = merge(base, base, changes);
    if (!rebuilt.committed)
        return fail(error, "Selected conflict resolution overlaps another change");
    return updateRevision(revision, rebuilt.result, author, changes, error, state);
}
bool readPackage(const QString &path, QJsonObject &out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, f.errorString());
    if (f.size() < PackageMagic.size() || f.size() > MaxCompressed + PackageMagic.size())
        return fail(error, "Package size out of range");
    QByteArray all = f.readAll();
    if (!all.startsWith(PackageMagic))
        return fail(error, "Invalid CCEPR magic");
    QByteArray compressed = all.mid(PackageMagic.size());
    if (compressed.size() < 4)
        return fail(error, "Truncated package payload");
    quint32 expected = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(compressed.constData()));
    if (expected > quint32(MaxExpanded))
        return fail(error, "Expanded package exceeds size limit");
    QByteArray json = qUncompress(compressed);
    if (json.isEmpty() && expected != 0)
        return fail(error, "Package decompression failed");
    if (json.size() != expected)
        return fail(error, "Invalid expanded package length");
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, "Invalid package JSON");
    QJsonObject candidate = doc.object();
    if (!refreshRevisionDependencies(candidate, error) || !validateRevision(candidate, error)
        || !validatePackageResources(candidate, error))
        return false;
    out = candidate;
    return true;
}
} // namespace CceReview
