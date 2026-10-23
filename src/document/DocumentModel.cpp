#include "DocumentModel.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <QHash>
#include <utility>

namespace CceDocument
{
namespace
{
const QStringList kMetaFields = {
    QStringLiteral("title"),     QStringLiteral("titleOrg"),       QStringLiteral("artist"),
    QStringLiteral("artistOrg"), QStringLiteral("difficulty"),     QStringLiteral("chartAuthor"),
    QStringLiteral("audioFile"), QStringLiteral("backgroundFile"), QStringLiteral("previewTime"),
    QStringLiteral("firstBpm"),  QStringLiteral("offset"),         QStringLiteral("speed")};

bool relativeResource(const QString &path);

QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool validUuid(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    return pattern.match(value).hasMatch()
           && QUuid(value).toString(QUuid::WithoutBraces).compare(value, Qt::CaseInsensitive) == 0;
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QJsonArray beatArray(int whole, int numerator, int denominator, bool canonical)
{
    if (!canonical || denominator <= 0 || numerator < 0)
        return {whole, numerator, denominator};
    qint64 w = whole;
    qint64 n = numerator;
    qint64 d = denominator;
    w += n / d;
    n %= d;
    const qint64 gcd = std::gcd(n, d);
    n /= gcd;
    d /= gcd;
    return {static_cast<int>(w), static_cast<int>(n), static_cast<int>(d)};
}

bool readBeat(const QJsonValue &value, int &whole, int &num, int &den)
{
    if (!value.isArray())
        return false;
    const QJsonArray beat = value.toArray();
    if (beat.size() != 3 || !beat[0].isDouble() || !beat[1].isDouble() || !beat[2].isDouble())
        return false;
    const double w = beat[0].toDouble(), n = beat[1].toDouble(), d = beat[2].toDouble();
    if (std::floor(w) != w || std::floor(n) != n || std::floor(d) != d || w < std::numeric_limits<int>::min()
        || w > std::numeric_limits<int>::max() || n < 0 || n > std::numeric_limits<int>::max() || d <= 0
        || d > std::numeric_limits<int>::max())
        return false;
    whole = static_cast<int>(w);
    num = static_cast<int>(n);
    den = static_cast<int>(d);
    return true;
}

QJsonObject encodeMeta(const MetaData &m)
{
    return {{QStringLiteral("title"), m.title},
            {QStringLiteral("titleOrg"), m.titleOrg},
            {QStringLiteral("artist"), m.artist},
            {QStringLiteral("artistOrg"), m.artistOrg},
            {QStringLiteral("difficulty"), m.difficulty},
            {QStringLiteral("chartAuthor"), m.chartAuthor},
            {QStringLiteral("audioFile"), relativeResource(m.audioFile) ? m.audioFile : QString()},
            {QStringLiteral("backgroundFile"), relativeResource(m.backgroundFile) ? m.backgroundFile : QString()},
            {QStringLiteral("previewTime"), m.previewTime},
            {QStringLiteral("firstBpm"), m.firstBpm},
            {QStringLiteral("offset"), m.offset},
            {QStringLiteral("speed"), m.speed}};
}

bool decodeMeta(const QJsonObject &o, MetaData &m, QString *error)
{
    for (const QString &key : kMetaFields)
        if (!o.contains(key))
            return fail(error, QStringLiteral("metadata missing field: %1").arg(key));
    for (const QString &key : {QStringLiteral("title"), QStringLiteral("titleOrg"), QStringLiteral("artist"),
                               QStringLiteral("artistOrg"), QStringLiteral("difficulty"), QStringLiteral("chartAuthor"),
                               QStringLiteral("audioFile"), QStringLiteral("backgroundFile")})
        if (!o.value(key).isString())
            return fail(error, QStringLiteral("metadata field must be text: %1").arg(key));
    for (const QString &key : {QStringLiteral("previewTime"), QStringLiteral("offset"), QStringLiteral("speed")})
        if (!o.value(key).isDouble() || std::floor(o.value(key).toDouble()) != o.value(key).toDouble())
            return fail(error, QStringLiteral("metadata field must be an integer: %1").arg(key));
    if (!o.value(QStringLiteral("firstBpm")).isDouble()
        || !std::isfinite(o.value(QStringLiteral("firstBpm")).toDouble()))
        return fail(error, QStringLiteral("metadata firstBpm must be finite"));
    m.title = o.value(QStringLiteral("title")).toString();
    m.titleOrg = o.value(QStringLiteral("titleOrg")).toString();
    m.artist = o.value(QStringLiteral("artist")).toString();
    m.artistOrg = o.value(QStringLiteral("artistOrg")).toString();
    m.difficulty = o.value(QStringLiteral("difficulty")).toString();
    m.chartAuthor = o.value(QStringLiteral("chartAuthor")).toString();
    m.audioFile = o.value(QStringLiteral("audioFile")).toString();
    m.backgroundFile = o.value(QStringLiteral("backgroundFile")).toString();
    const double previewTime = o.value(QStringLiteral("previewTime")).toDouble();
    const double offset = o.value(QStringLiteral("offset")).toDouble();
    const double speed = o.value(QStringLiteral("speed")).toDouble();
    if (previewTime < std::numeric_limits<int>::min() || previewTime > std::numeric_limits<int>::max()
        || offset < std::numeric_limits<int>::min() || offset > std::numeric_limits<int>::max()
        || speed < std::numeric_limits<int>::min() || speed > std::numeric_limits<int>::max())
        return fail(error, QStringLiteral("metadata integer is out of range"));
    m.previewTime = static_cast<int>(previewTime);
    m.firstBpm = o.value(QStringLiteral("firstBpm")).toDouble();
    m.offset = static_cast<int>(offset);
    m.speed = static_cast<int>(speed);
    return true;
}

bool relativeResource(const QString &path)
{
    if (path.isEmpty())
        return true;
    if (path.startsWith('/') || path.startsWith('\\') || path.contains('\\') || QDir::isAbsolutePath(path)
        || path.contains(':') || path.contains(QChar::Null))
        return false;
    const QStringList parts = path.split('/', Qt::KeepEmptyParts);
    for (const QString &part : parts)
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
            return false;
    return true;
}

QJsonArray encodeTiming(const QVector<BpmEntry> &timing, bool canonical)
{
    QJsonArray out;
    for (const BpmEntry &b : timing)
        out.append(QJsonObject{{QStringLiteral("beat"), beatArray(b.beatNum, b.numerator, b.denominator, canonical)},
                               {QStringLiteral("bpm"), b.bpm}});
    return out;
}

bool decodeTiming(const QJsonValue &value, QVector<BpmEntry> &out, QString *error)
{
    if (!value.isArray())
        return fail(error, QStringLiteral("timing must be an array"));
    QVector<BpmEntry> parsed;
    for (const QJsonValue &v : value.toArray())
    {
        if (!v.isObject())
            return fail(error, QStringLiteral("timing entry must be an object"));
        const QJsonObject o = v.toObject();
        int w, n, d;
        if (!readBeat(o.value(QStringLiteral("beat")), w, n, d) || !o.value(QStringLiteral("bpm")).isDouble())
            return fail(error, QStringLiteral("invalid timing entry"));
        const double bpm = o.value(QStringLiteral("bpm")).toDouble();
        if (!std::isfinite(bpm) || bpm <= 0)
            return fail(error, QStringLiteral("BPM must be positive and finite"));
        parsed.append(BpmEntry(w, n, d, bpm));
    }
    if (parsed.isEmpty())
        return fail(error, QStringLiteral("timing cannot be empty"));
    out = parsed;
    return true;
}

QJsonObject encodeBinding(const LayerBinding &b)
{
    return {{QStringLiteral("layerId"), b.layerId}, {QStringLiteral("visible"), b.visible},
            {QStringLiteral("locked"), b.locked},   {QStringLiteral("exportEnabled"), b.exportEnabled},
            {QStringLiteral("opacity"), b.opacity}, {QStringLiteral("order"), b.order}};
}

QJsonObject encodeReference(const DifficultyReference &r)
{
    QJsonArray ids;
    for (const QString &id : r.layerIds)
        ids.append(id);
    return {{QStringLiteral("difficultyId"), r.difficultyId},
            {QStringLiteral("layerIds"), ids},
            {QStringLiteral("visible"), r.visible},
            {QStringLiteral("opacity"), r.opacity}};
}

bool hasBinding(const Difficulty &d, const QString &layerId)
{
    for (const LayerBinding &b : d.bindings)
        if (b.layerId == layerId)
            return true;
    return false;
}

} // namespace

Difficulty::Difficulty() : exportedMetadataFields(kMetaFields.begin(), kMetaFields.end()) {}

QJsonObject encodeNote(const Note &note, bool canonical)
{
    QJsonObject o;
    o.insert(QStringLiteral("beat"), beatArray(note.beatNum, note.numerator, note.denominator, canonical));
    o.insert(QStringLiteral("type"), Note::noteTypeToInt(note.type));
    if (note.type == NoteType::RAIN)
    {
        if (!canonical)
            o.insert(QStringLiteral("x"), note.x); // Preserve legacy data only for lossless storage.
        o.insert(QStringLiteral("endbeat"),
                 beatArray(note.endBeatNum, note.endNumerator, note.endDenominator, canonical));
    }
    else if (note.type == NoteType::SOUND)
    {
        o.insert(QStringLiteral("sound"), relativeResource(note.sound) ? note.sound : QString());
        o.insert(QStringLiteral("vol"), note.vol);
        o.insert(QStringLiteral("offset"), note.offset);
    }
    else
        o.insert(QStringLiteral("x"), note.x);
    return o;
}

bool decodeNote(const QJsonObject &o, Note &note, QString *error)
{
    int w, n, d;
    if (!readBeat(o.value(QStringLiteral("beat")), w, n, d) || !o.value(QStringLiteral("type")).isDouble())
        return fail(error, QStringLiteral("note requires a valid beat and type"));
    const int type = o.value(QStringLiteral("type")).toInt(-1);
    Note parsed;
    if (type == 0)
    {
        if (!o.value(QStringLiteral("x")).isDouble())
            return fail(error, QStringLiteral("normal note requires x"));
        parsed = Note(w, n, d, o.value(QStringLiteral("x")).toInt());
    }
    else if (type == 3)
    {
        int ew, en, ed;
        if (!readBeat(o.value(QStringLiteral("endbeat")), ew, en, ed))
            return fail(error, QStringLiteral("rain note requires endbeat"));
        parsed = Note(w, n, d, ew, en, ed, o.value(QStringLiteral("x")).toInt(256));
    }
    else if (type == 1)
    {
        if (!o.value(QStringLiteral("sound")).isString() || !o.value(QStringLiteral("vol")).isDouble()
            || !o.value(QStringLiteral("offset")).isDouble())
            return fail(error, QStringLiteral("sound note requires sound, vol and offset"));
        parsed = Note(w, n, d, o.value(QStringLiteral("sound")).toString(), o.value(QStringLiteral("vol")).toInt(),
                      o.value(QStringLiteral("offset")).toInt());
        if (!relativeResource(parsed.sound))
            return fail(error, QStringLiteral("sound resource path must be relative"));
    }
    else
        return fail(error, QStringLiteral("unsupported note type"));
    if (!parsed.isValid())
        return fail(error, QStringLiteral("invalid note values"));
    note = parsed;
    return true;
}

QJsonObject encodeChart(const Chart &chart, bool canonical)
{
    QJsonArray notes;
    for (const Note &n : chart.notes())
        notes.append(encodeNote(n, canonical));
    return {{QStringLiteral("notes"), notes},
            {QStringLiteral("timing"), encodeTiming(chart.bpmList(), canonical)},
            {QStringLiteral("metadata"), encodeMeta(chart.meta())}};
}

bool decodeChart(const QJsonObject &o, Chart &chart, QString *error)
{
    if (!o.value(QStringLiteral("notes")).isArray())
        return fail(error, QStringLiteral("notes must be an array"));
    Chart parsed;
    QVector<Note> notes;
    for (const QJsonValue &v : o.value(QStringLiteral("notes")).toArray())
    {
        Note n;
        if (!v.isObject() || !decodeNote(v.toObject(), n, error))
            return false;
        notes.append(n);
    }
    QVector<BpmEntry> timing;
    MetaData metadata;
    if (!decodeTiming(o.value(QStringLiteral("timing")), timing, error)
        || !decodeMeta(o.value(QStringLiteral("metadata")).toObject(), metadata, error))
        return false;
    if (!relativeResource(metadata.audioFile) || !relativeResource(metadata.backgroundFile))
        return fail(error, QStringLiteral("chart resource paths must be relative"));
    parsed.setNotes(notes);
    parsed.bpmList() = timing;
    parsed.meta() = metadata;
    chart = parsed;
    return true;
}

Project Project::fromChart(const Chart &chart, const QString &audioHash)
{
    Project p;
    p.projectId = uuid();
    Difficulty d;
    d.difficultyId = uuid();
    d.name = chart.meta().difficulty;
    d.metadata = chart.meta();
    d.timing = chart.bpmList();
    d.oggHash = audioHash;
    Layer l;
    l.layerId = uuid();
    l.name = QStringLiteral("Layer 1");
    l.notes = chart.notes();
    d.bindings.append(LayerBinding{l.layerId});
    p.activeDifficultyId = d.difficultyId;
    p.layers.insert(l.layerId, l);
    p.difficulties.insert(d.difficultyId, d);
    return p;
}

QJsonObject Project::toJson(bool canonical) const
{
    QJsonObject layerObjects;
    for (auto it = layers.cbegin(); it != layers.cend(); ++it)
    {
        QJsonArray notes;
        for (const Note &n : it->notes)
            notes.append(encodeNote(n, canonical));
        layerObjects.insert(it.key(), QJsonObject{{QStringLiteral("layerId"), it->layerId},
                                                  {QStringLiteral("name"), it->name},
                                                  {QStringLiteral("notes"), notes}});
    }
    QJsonObject difficultyObjects;
    for (auto it = difficulties.cbegin(); it != difficulties.cend(); ++it)
    {
        const Difficulty &d = it.value();
        QJsonArray bindings, references, exported;
        for (const LayerBinding &b : d.bindings)
            bindings.append(encodeBinding(b));
        for (const DifficultyReference &r : d.references)
            references.append(encodeReference(r));
        QStringList fields = d.exportedMetadataFields.values();
        std::sort(fields.begin(), fields.end());
        for (const QString &field : fields)
            exported.append(field);
        difficultyObjects.insert(it.key(), QJsonObject{{QStringLiteral("difficultyId"), d.difficultyId},
                                                       {QStringLiteral("name"), d.name},
                                                       {QStringLiteral("metadata"), encodeMeta(d.metadata)},
                                                       {QStringLiteral("timing"), encodeTiming(d.timing, canonical)},
                                                       {QStringLiteral("oggHash"), d.oggHash},
                                                       {QStringLiteral("bindings"), bindings},
                                                       {QStringLiteral("references"), references},
                                                       {QStringLiteral("exportedMetadataFields"), exported}});
    }
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("projectId"), projectId},
            {QStringLiteral("activeDifficultyId"), activeDifficultyId},
            {QStringLiteral("layers"), layerObjects},
            {QStringLiteral("difficulties"), difficultyObjects}};
}

bool Project::fromJson(const QJsonObject &o, Project &project, QString *error)
{
    if (o.value(QStringLiteral("schemaVersion")).toInt(-1) != 1 || !o.value(QStringLiteral("projectId")).isString()
        || !o.value(QStringLiteral("activeDifficultyId")).isString() || !o.value(QStringLiteral("layers")).isObject()
        || !o.value(QStringLiteral("difficulties")).isObject())
        return fail(error, QStringLiteral("invalid project header or unsupported schema"));
    Project parsed;
    parsed.projectId = o.value(QStringLiteral("projectId")).toString();
    parsed.activeDifficultyId = o.value(QStringLiteral("activeDifficultyId")).toString();
    const QJsonObject layerObjects = o.value(QStringLiteral("layers")).toObject();
    for (auto it = layerObjects.begin(); it != layerObjects.end(); ++it)
    {
        if (!it.value().isObject())
            return fail(error, QStringLiteral("layer must be an object"));
        const QJsonObject lo = it.value().toObject();
        if (lo.value(QStringLiteral("layerId")).toString() != it.key() || !lo.value(QStringLiteral("name")).isString()
            || !lo.value(QStringLiteral("notes")).isArray())
            return fail(error, QStringLiteral("invalid layer object"));
        Layer layer{it.key(), lo.value(QStringLiteral("name")).toString(), {}};
        for (const QJsonValue &nv : lo.value(QStringLiteral("notes")).toArray())
        {
            Note n;
            if (!nv.isObject() || !decodeNote(nv.toObject(), n, error))
                return false;
            layer.notes.append(n);
        }
        parsed.layers.insert(it.key(), layer);
    }
    const QJsonObject diffs = o.value(QStringLiteral("difficulties")).toObject();
    for (auto it = diffs.begin(); it != diffs.end(); ++it)
    {
        if (!it.value().isObject())
            return fail(error, QStringLiteral("difficulty must be an object"));
        const QJsonObject dObj = it.value().toObject();
        Difficulty d;
        d.difficultyId = dObj.value(QStringLiteral("difficultyId")).toString();
        d.name = dObj.value(QStringLiteral("name")).toString();
        if (d.difficultyId != it.key() || !dObj.value(QStringLiteral("name")).isString()
            || !dObj.value(QStringLiteral("metadata")).isObject() || !dObj.value(QStringLiteral("oggHash")).isString()
            || !dObj.value(QStringLiteral("bindings")).isArray() || !dObj.value(QStringLiteral("references")).isArray()
            || !dObj.value(QStringLiteral("exportedMetadataFields")).isArray()
            || !decodeMeta(dObj.value(QStringLiteral("metadata")).toObject(), d.metadata, error)
            || !decodeTiming(dObj.value(QStringLiteral("timing")), d.timing, error))
            return false;
        d.oggHash = dObj.value(QStringLiteral("oggHash")).toString();
        d.bindings.clear();
        for (const QJsonValue &bv : dObj.value(QStringLiteral("bindings")).toArray())
        {
            if (!bv.isObject())
                return fail(error, QStringLiteral("binding must be an object"));
            const QJsonObject b = bv.toObject();
            const QString id = b.value(QStringLiteral("layerId")).toString();
            const double opacity = b.value(QStringLiteral("opacity")).toDouble(-1);
            if (!b.value(QStringLiteral("visible")).isBool() || !b.value(QStringLiteral("locked")).isBool()
                || !b.value(QStringLiteral("exportEnabled")).isBool() || !b.value(QStringLiteral("order")).isDouble()
                || std::floor(b.value(QStringLiteral("order")).toDouble())
                       != b.value(QStringLiteral("order")).toDouble()
                || !b.value(QStringLiteral("opacity")).isDouble())
                return fail(error, QStringLiteral("invalid binding fields"));
            d.bindings.append(LayerBinding{
                id, b.value(QStringLiteral("visible")).toBool(), b.value(QStringLiteral("locked")).toBool(),
                b.value(QStringLiteral("exportEnabled")).toBool(), opacity, b.value(QStringLiteral("order")).toInt()});
        }
        for (const QJsonValue &rv : dObj.value(QStringLiteral("references")).toArray())
        {
            if (!rv.isObject())
                return fail(error, QStringLiteral("reference must be an object"));
            const QJsonObject r = rv.toObject();
            if (!r.value(QStringLiteral("difficultyId")).isString() || !r.value(QStringLiteral("layerIds")).isArray()
                || !r.value(QStringLiteral("visible")).isBool() || !r.value(QStringLiteral("opacity")).isDouble())
                return fail(error, QStringLiteral("invalid reference fields"));
            DifficultyReference ref;
            ref.difficultyId = r.value(QStringLiteral("difficultyId")).toString();
            ref.visible = r.value(QStringLiteral("visible")).toBool();
            ref.opacity = r.value(QStringLiteral("opacity")).toDouble();
            for (const QJsonValue &id : r.value(QStringLiteral("layerIds")).toArray())
            {
                if (!id.isString())
                    return fail(error, QStringLiteral("reference layer id must be text"));
                ref.layerIds.append(id.toString());
            }
            d.references.append(ref);
        }
        d.exportedMetadataFields.clear();
        for (const QJsonValue &field : dObj.value(QStringLiteral("exportedMetadataFields")).toArray())
        {
            if (!field.isString())
                return fail(error, QStringLiteral("exported metadata field must be text"));
            d.exportedMetadataFields.insert(field.toString());
        }
        parsed.difficulties.insert(it.key(), d);
    }
    if (!parsed.validate(error))
        return false;
    project = parsed;
    return true;
}

bool Project::validate(QString *error) const
{
    if (!validUuid(projectId) || !difficulties.contains(activeDifficultyId))
        return fail(error, QStringLiteral("invalid project UUID or active difficulty"));
    QSet<QString> noteIds;
    for (auto it = layers.cbegin(); it != layers.cend(); ++it)
    {
        const Layer &l = it.value();
        if (!validUuid(it.key()) || l.layerId != it.key() || l.name.trimmed().isEmpty())
            return fail(error, QStringLiteral("invalid layer identity or name"));
        for (const Note &n : l.notes)
        {
            if (!n.isValid() || (n.isSound() && !relativeResource(n.sound)))
                return fail(error, QStringLiteral("layer contains invalid note or resource reference"));
            if (!n.id.isEmpty())
            {
                if (noteIds.contains(n.id))
                    return fail(error, QStringLiteral("runtime note id has multiple owners"));
                noteIds.insert(n.id);
            }
        }
    }
    for (auto it = difficulties.cbegin(); it != difficulties.cend(); ++it)
    {
        const Difficulty &d = it.value();
        if (!validUuid(it.key()) || d.difficultyId != it.key() || d.name.trimmed().isEmpty()
            || !relativeResource(d.metadata.audioFile) || !relativeResource(d.metadata.backgroundFile))
            return fail(error, QStringLiteral("invalid difficulty identity, name, or resource path"));
        QSet<QString> bound;
        for (const LayerBinding &b : d.bindings)
        {
            if (!layers.contains(b.layerId) || bound.contains(b.layerId) || !std::isfinite(b.opacity) || b.opacity < 0
                || b.opacity > 1)
                return fail(error, QStringLiteral("invalid or duplicate layer binding"));
            bound.insert(b.layerId);
        }
        for (const BpmEntry &b : d.timing)
            if (b.denominator <= 0 || b.numerator < 0 || !std::isfinite(b.bpm) || b.bpm <= 0)
                return fail(error, QStringLiteral("invalid timing entry"));
        if (d.timing.isEmpty())
            return fail(error, QStringLiteral("difficulty timing cannot be empty"));
        for (const QString &field : d.exportedMetadataFields)
            if (!kMetaFields.contains(field))
                return fail(error, QStringLiteral("unknown exported metadata field"));
        QSet<QString> referencedDifficulties;
        for (const DifficultyReference &r : d.references)
        {
            if (!difficulties.contains(r.difficultyId) || r.difficultyId == d.difficultyId
                || referencedDifficulties.contains(r.difficultyId) || !std::isfinite(r.opacity) || r.opacity < 0
                || r.opacity > 1)
                return fail(error, QStringLiteral("invalid difficulty reference"));
            referencedDifficulties.insert(r.difficultyId);
            const Difficulty &source = difficulties[r.difficultyId];
            if (d.oggHash.isEmpty() || source.oggHash.isEmpty() || d.oggHash != source.oggHash)
                return fail(error, QStringLiteral("reference audio hashes must match and be nonempty"));
            QSet<QString> ids;
            for (const QString &id : r.layerIds)
            {
                if (ids.contains(id) || !hasBinding(source, id))
                    return fail(error,
                                QStringLiteral("reference layer is duplicated or absent from source difficulty"));
                ids.insert(id);
            }
        }
    }
    return true;
}

Chart Project::compose(const QString &difficultyId, const QStringList &onlyLayerIds, bool forExport) const
{
    Chart chart;
    if (!difficulties.contains(difficultyId))
        return chart;
    const Difficulty &d = difficulties[difficultyId];
    chart.meta() = d.metadata;
    chart.bpmList() = d.timing;
    QSet<QString> requested;
    for (const QString &id : onlyLayerIds)
        requested.insert(id);
    QVector<LayerBinding> bindings = d.bindings;
    std::stable_sort(bindings.begin(), bindings.end(), [](const LayerBinding &a, const LayerBinding &b) {
        return a.order < b.order;
    });
    QVector<Note> notes;
    for (const LayerBinding &b : bindings)
    {
        if ((!requested.isEmpty() && !requested.contains(b.layerId)) || (forExport && !b.exportEnabled))
            continue;
        const auto layer = layers.constFind(b.layerId);
        if (layer != layers.cend())
            notes += layer->notes;
    }
    chart.setNotes(notes);
    return chart;
}

QString Project::addDifficulty(const QString &name, const MetaData &metadata, const QVector<BpmEntry> &timing,
                               const QString &audioHash)
{
    if (name.trimmed().isEmpty() || timing.isEmpty())
        return {};
    Difficulty d;
    d.difficultyId = uuid();
    d.name = name;
    d.metadata = metadata;
    d.metadata.difficulty = name;
    d.timing = timing;
    d.oggHash = audioHash;
    difficulties.insert(d.difficultyId, d);
    return d.difficultyId;
}

QString Project::addLayer(const QString &difficultyId, const QString &name)
{
    if (!difficulties.contains(difficultyId) || name.trimmed().isEmpty())
        return {};
    Layer layer;
    layer.layerId = uuid();
    layer.name = name;
    layers.insert(layer.layerId, layer);
    if (!bindLayer(difficultyId, layer.layerId))
    {
        layers.remove(layer.layerId);
        return {};
    }
    return layer.layerId;
}

bool Project::bindLayer(const QString &difficultyId, const QString &layerId)
{
    if (!difficulties.contains(difficultyId) || !layers.contains(layerId))
        return false;
    Difficulty &d = difficulties[difficultyId];
    if (hasBinding(d, layerId))
        return false;
    LayerBinding binding;
    binding.layerId = layerId;
    binding.order = d.bindings.size();
    d.bindings.append(binding);
    return true;
}

bool Project::unbindLayer(const QString &difficultyId, const QString &layerId)
{
    if (!difficulties.contains(difficultyId))
        return false;
    Difficulty &d = difficulties[difficultyId];
    for (int i = 0; i < d.bindings.size(); ++i)
        if (d.bindings[i].layerId == layerId)
        {
            d.bindings.removeAt(i);
            return true;
        }
    return false;
}

QString Project::copyAsIndependent(const QString &difficultyId, const QString &layerId)
{
    if (!difficulties.contains(difficultyId) || !layers.contains(layerId)
        || !hasBinding(difficulties[difficultyId], layerId))
        return {};
    Layer copy = layers[layerId];
    copy.layerId = uuid();
    copy.name += QStringLiteral(" copy");
    for (Note &n : copy.notes)
        n.id = Note::generateId();
    layers.insert(copy.layerId, copy);
    if (!unbindLayer(difficultyId, layerId) || !bindLayer(difficultyId, copy.layerId))
    {
        layers.remove(copy.layerId);
        return {};
    }
    return copy.layerId;
}

bool Project::addReference(const QString &targetId, const QString &sourceId, const QStringList &layerIds,
                           QString *error)
{
    if (!difficulties.contains(targetId) || !difficulties.contains(sourceId) || targetId == sourceId)
        return fail(error, QStringLiteral("reference target/source is invalid or self-referential"));
    Difficulty &target = difficulties[targetId];
    const Difficulty &source = difficulties[sourceId];
    if (target.oggHash.isEmpty() || source.oggHash.isEmpty() || target.oggHash != source.oggHash)
        return fail(error, QStringLiteral("reference requires matching nonempty audio hashes"));
    DifficultyReference ref;
    ref.difficultyId = sourceId;
    for (const QString &id : layerIds)
    {
        if (!hasBinding(source, id))
            return fail(error, QStringLiteral("selected layer is not bound to source difficulty"));
        if (!ref.layerIds.contains(id))
            ref.layerIds.append(id);
    }
    for (DifficultyReference &old : target.references)
        if (old.difficultyId == sourceId)
        {
            if (old.layerIds.isEmpty() || ref.layerIds.isEmpty())
                old.layerIds.clear();
            else
                for (const QString &id : ref.layerIds)
                    if (!old.layerIds.contains(id))
                        old.layerIds.append(id);
            return true;
        }
    target.references.append(ref);
    return true;
}

int Project::bindingCount(const QString &layerId) const
{
    int count = 0;
    for (const Difficulty &d : difficulties)
        if (hasBinding(d, layerId))
            ++count;
    return count;
}

bool Project::updateComposedChart(const QString &difficultyId, const Chart &before, const Chart &after, QString *error,
                                  const QString &requestedActiveLayerId)
{
    if (!difficulties.contains(difficultyId))
        return fail(error, QStringLiteral("difficulty does not exist"));
    Difficulty &d = difficulties[difficultyId];
    if (!relativeResource(after.meta().audioFile) || !relativeResource(after.meta().backgroundFile))
        return fail(error, QStringLiteral("updated chart contains an absolute resource path"));
    if (after.bpmList().isEmpty())
        return fail(error, QStringLiteral("updated chart timing cannot be empty"));
    for (const BpmEntry &b : after.bpmList())
        if (b.denominator <= 0 || b.numerator < 0 || !std::isfinite(b.bpm) || b.bpm <= 0)
            return fail(error, QStringLiteral("updated chart contains invalid timing"));
    QHash<QString, QString> owner;
    for (auto it = layers.cbegin(); it != layers.cend(); ++it)
        for (const Note &n : it->notes)
            if (!n.id.isEmpty())
            {
                if (owner.contains(n.id))
                    return fail(error, QStringLiteral("note id has ambiguous layer ownership"));
                owner.insert(n.id, it.key());
            }
    QHash<QString, const Note *> prior, next;
    for (const Note &n : before.notes())
        if (!n.id.isEmpty())
            prior.insert(n.id, &n);
    for (const Note &n : after.notes())
    {
        if (n.id.isEmpty())
            return fail(error, QStringLiteral("updated notes must have runtime ids"));
        if (!n.isValid() || (n.isSound() && !relativeResource(n.sound)))
            return fail(error, QStringLiteral("updated chart contains invalid note"));
        if (next.contains(n.id))
            return fail(error, QStringLiteral("updated chart contains duplicate note ids"));
        next.insert(n.id, &n);
    }
    QString destination = requestedActiveLayerId.isEmpty() ? activeLayerId : requestedActiveLayerId;
    QHash<QString, bool> unlockedBindings;
    for (const LayerBinding &b : d.bindings)
        unlockedBindings.insert(b.layerId, !b.locked);
    if (!destination.isEmpty() && (!unlockedBindings.contains(destination) || !unlockedBindings.value(destination)))
        return fail(error, QStringLiteral("active layer is not an unlocked binding of the difficulty"));
    if (destination.isEmpty())
        for (const LayerBinding &b : d.bindings)
            if (!b.locked)
            {
                destination = b.layerId;
                break;
            }
    for (auto it = prior.cbegin(); it != prior.cend(); ++it)
    {
        const bool changed = !next.contains(it.key()) || *next.value(it.key()) != *it.value();
        if (!changed)
            continue;
        if (!owner.contains(it.key()))
            return fail(error, QStringLiteral("before chart contains an unowned note"));
        const QString ownerLayer = owner.value(it.key());
        if (!unlockedBindings.contains(ownerLayer))
            return fail(error, QStringLiteral("cannot edit or remove a note outside the difficulty bindings"));
        if (!unlockedBindings.value(ownerLayer))
            return fail(error, QStringLiteral("cannot edit or remove a note in a locked layer"));
    }
    for (auto it = next.cbegin(); it != next.cend(); ++it)
        if (!prior.contains(it.key()) && owner.contains(it.key()))
            return fail(error, QStringLiteral("new note id already belongs to a layer"));
    if (destination.isEmpty())
        for (auto it = next.cbegin(); it != next.cend(); ++it)
            if (!prior.contains(it.key()))
                return fail(error, QStringLiteral("no unlocked bound layer for new notes"));
    QHash<QString, QHash<QString, const Note *>> replacementsByLayer;
    QHash<QString, QSet<QString>> removalsByLayer;
    for (auto it = prior.cbegin(); it != prior.cend(); ++it)
    {
        if (next.contains(it.key()) && *next.value(it.key()) == *it.value())
            continue;
        const QString layerId = owner.value(it.key());
        if (next.contains(it.key()))
            replacementsByLayer[layerId].insert(it.key(), next.value(it.key()));
        else
            removalsByLayer[layerId].insert(it.key());
    }
    for (auto layerIt = layers.begin(); layerIt != layers.end(); ++layerIt)
    {
        const auto replacements = replacementsByLayer.constFind(layerIt.key());
        const auto removals = removalsByLayer.constFind(layerIt.key());
        if (replacements == replacementsByLayer.cend() && removals == removalsByLayer.cend())
            continue;
        QVector<Note> kept;
        kept.reserve(layerIt->notes.size());
        for (const Note &stored : std::as_const(layerIt->notes))
        {
            if (removals != removalsByLayer.cend() && removals->contains(stored.id))
                continue;
            if (replacements != replacementsByLayer.cend() && replacements->contains(stored.id))
                kept.append(*replacements->value(stored.id));
            else
                kept.append(stored);
        }
        layerIt->notes = std::move(kept);
    }
    for (auto it = next.cbegin(); it != next.cend(); ++it)
    {
        if (prior.contains(it.key()))
            continue;
        layers[destination].notes.append(*it.value());
    }
    d.metadata = after.meta();
    d.timing = after.bpmList();
    return true;
}
} // namespace CceDocument
