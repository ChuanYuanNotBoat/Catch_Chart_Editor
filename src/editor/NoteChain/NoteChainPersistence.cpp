// NoteChainPersistence.cpp — V3 JSON 持久化实现

#include "NoteChainPersistence.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>
#include <QDateTime>
#include <QCoreApplication>
#include <cmath>
#include <numeric>

namespace NoteChain {

// ---- 序列化工具函数 ----

static QJsonArray beatToTriplet(double beat) {
    const int den = Const::kSerializeDen;
    int beatNum = static_cast<int>(std::floor(beat + 1e-9));
    double frac = beat - beatNum;
    int num = static_cast<int>(qRound(frac * den));
    if (num >= den) {
        beatNum += num / den;
        num %= den;
    }
    if (num < 0) {
        const int borrow = static_cast<int>(std::ceil(std::abs(num) / static_cast<double>(den)));
        beatNum -= borrow;
        num += borrow * den;
    }
    int outDen = den;
    const int g = std::gcd(num < 0 ? -num : num, den);
    if (g > 0) {
        num /= g;
        outDen /= g;
    }
    QJsonArray arr;
    arr.append(beatNum); arr.append(num); arr.append(outDen);
    return arr;
}

static double doubleFromTriplet(const QJsonValue &val, double fallback = 0.0)
{
    if (val.isArray()) {
        QJsonArray arr = val.toArray();
        if (arr.size() >= 3) {
            const int den = arr[2].toInt(0);
            if (den != 0)
                return arr[0].toDouble(0.0) + arr[1].toDouble(0.0) / static_cast<double>(den);
        }
        if (arr.size() >= 1)
            return arr[0].toDouble(fallback);
    } else if (val.isDouble()) {
        return val.toDouble(fallback);
    }
    return fallback;
}

static int parseInt(const QJsonValue &val, int fallback = 0)
{
    if (val.isDouble())
        return static_cast<int>(val.toDouble(fallback));
    if (val.isString()) {
        bool ok = false;
        int i = val.toString().toInt(&ok);
        return ok ? i : fallback;
    }
    return fallback;
}

static QPointF handleOffsetFromJson(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return QPointF(object.value("lane_dx").toDouble(0.0),
                       doubleFromTriplet(object.value("beat_delta"), 0.0));
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        return QPointF(array.size() > 0 ? array[0].toDouble(0.0) : 0.0,
                       array.size() > 1 ? array[1].toDouble(0.0) : 0.0);
    }
    return {};
}

static QVector<int> positiveIdsFromJson(const QJsonValue &value, int fallback)
{
    QVector<int> result;
    QSet<int> seen;
    for (const QJsonValue &entry : value.toArray()) {
        const int id = parseInt(entry, 0);
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            result.append(id);
        }
    }
    if (result.isEmpty())
        result.append(fallback);
    return result;
}

static QJsonArray idsToJson(const QVector<int> &ids, int fallback)
{
    QJsonArray result;
    QSet<int> seen;
    for (int id : ids) {
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            result.append(id);
        }
    }
    if (result.isEmpty())
        result.append(fallback);
    return result;
}

static QVector<GroupPersistenceMeta> groupsFromJson(const QJsonValue &value, int fallbackId)
{
    QVector<GroupPersistenceMeta> result;
    QSet<int> seen;
    for (const QJsonValue &entry : value.toArray()) {
        const QJsonObject object = entry.toObject();
        const int id = parseInt(object.value("group_id"), 0);
        if (id <= 0 || seen.contains(id))
            continue;
        seen.insert(id);
        GroupPersistenceMeta group;
        group.id = id;
        group.name = object.value("group_name").toString();
        group.reserved = object.value("reserved").toObject();
        result.append(group);
    }
    if (result.isEmpty()) {
        GroupPersistenceMeta group;
        group.id = fallbackId;
        group.name = QStringLiteral("base");
        result.append(group);
    }
    return result;
}

static QJsonArray groupsToJson(const QVector<GroupPersistenceMeta> &groups, int fallbackId)
{
    QVector<GroupPersistenceMeta> effective = groups;
    if (effective.isEmpty()) {
        GroupPersistenceMeta group;
        group.id = fallbackId;
        group.name = QStringLiteral("base");
        effective.append(group);
    }
    QJsonArray result;
    for (const GroupPersistenceMeta &group : effective) {
        if (group.id <= 0)
            continue;
        QJsonObject object;
        object["group_id"] = group.id;
        object["group_name"] = group.name.isEmpty() ? QStringLiteral("base") : group.name;
        object["reserved"] = group.reserved;
        result.append(object);
    }
    return result;
}

// ---- 锚点序列化 ----

QJsonObject NoteChainPersistence::serializeAnchor(const Anchor &anchor, const NodePersistenceMeta &meta)
{
    QJsonObject obj;
    obj["node_id"] = anchor.id;
    obj["lane_x"] = anchor.laneX;
    obj["beat"] = beatToTriplet(anchor.beat);

    QJsonObject inObj;
    inObj["lane_dx"] = anchor.inDx;
    inObj["beat_delta"] = beatToTriplet(anchor.inDy);
    QJsonObject outObj;
    outObj["lane_dx"] = anchor.outDx;
    outObj["beat_delta"] = beatToTriplet(anchor.outDy);
    QJsonObject joystickObj;
    joystickObj["lane_dx"] = anchor.outDx;
    joystickObj["beat_delta"] = beatToTriplet(anchor.outDy);
    obj["joystick"] = joystickObj;

    QJsonObject compatObj;
    compatObj["in"] = inObj;
    compatObj["out"] = outObj;
    obj["compat_handles"] = compatObj;

    obj["group_ids"] = idsToJson(meta.groupIds, Const::kDefaultNodeGroupId);
    obj["reserved"] = meta.reserved;
    obj["smooth"] = anchor.smooth;
    return obj;
}

bool NoteChainPersistence::deserializeAnchor(const QJsonObject &json, Anchor &anchor, QString *errorMsg)
{
    Q_UNUSED(errorMsg)

    // V3 格式：包含 lane_x
    if (json.contains("lane_x")) {
        anchor.id  = parseInt(json.value("node_id"), parseInt(json.value("id"), -1));
        anchor.laneX = json.value("lane_x").toDouble(0.0);
        anchor.beat = doubleFromTriplet(json.value("beat"), 0.0);

        const QJsonObject compatRaw = json.value("compat_handles").toObject();
        QJsonValue inRaw = compatRaw.value("in");
        QJsonValue outRaw = compatRaw.value("out");
        if (inRaw.isUndefined() || inRaw.isNull())
            inRaw = json.value("in");
        if (outRaw.isUndefined() || outRaw.isNull())
            outRaw = json.value("out");

        if ((inRaw.isUndefined() || inRaw.isNull()) &&
            (outRaw.isUndefined() || outRaw.isNull())) {
            QJsonObject joystickRaw = json.value("joystick").toObject();
            const double joyDx = joystickRaw.value("lane_dx").toDouble(0.0);
            const double joyDy = doubleFromTriplet(joystickRaw.value("beat_delta"), 0.0);
            anchor.inDx = -joyDx;
            anchor.inDy = -joyDy;
            anchor.outDx = joyDx;
            anchor.outDy = joyDy;
        } else {
            const QPointF inOffset = handleOffsetFromJson(inRaw);
            const QPointF outOffset = handleOffsetFromJson(outRaw);
            anchor.inDx = inOffset.x();
            anchor.inDy = inOffset.y();
            anchor.outDx = outOffset.x();
            anchor.outDy = outOffset.y();
        }
        anchor.smooth = json.value("smooth").toBool(true);

        return anchor.id >= 0;
    }

    // 旧格式兼容：包含 x/y（画布坐标）
    if (json.contains("x") && json.contains("y")) {
        anchor.id  = parseInt(json.value("id"), -1);
        anchor.laneX = json.value("x").toDouble(0.0);
        anchor.beat = json.value("y").toDouble(0.0);

        QJsonValue inVal  = json.value("in");
        QJsonValue outVal = json.value("out");
        QJsonArray inArr  = inVal.isArray() ? inVal.toArray() : QJsonArray{0.0, 0.0};
        QJsonArray outArr = outVal.isArray() ? outVal.toArray() : QJsonArray{0.0, 0.0};

        anchor.inDx = inArr.size() >= 1  ? inArr[0].toDouble(0.0)  : 0.0;
        anchor.inDy = inArr.size() >= 2  ? inArr[1].toDouble(0.0)  : 0.0;
        anchor.outDx = outArr.size() >= 1 ? outArr[0].toDouble(0.0) : 0.0;
        anchor.outDy = outArr.size() >= 2 ? outArr[1].toDouble(0.0) : 0.0;
        anchor.smooth = json.value("smooth").toBool(true);

        return anchor.id >= 0;
    }

    return false;
}

// ---- 状态序列化/反序列化 ----

QJsonObject NoteChainPersistence::serialize(const NoteChainState &state)
{
    QJsonObject root;

    root["format_version"]   = 3;
    root["coordinate_space"] = QStringLiteral("chart");
    root["revision"]         = state.projectRevision();

    // file_uuid
    QString fileUuid = state.projectFileUuid();
    if (fileUuid.isEmpty()) {
        fileUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    root["file_uuid"] = fileUuid;

    root["updated_at"]            = QDateTime::currentMSecsSinceEpoch();
    root["last_writer_instance"]  = QString();

    // nodes 数组（对应 anchors）
    QJsonArray nodesArray;
    const QVector<Anchor> &anchorsVec = state.anchors();
    for (const Anchor &a : anchorsVec) {
        nodesArray.append(serializeAnchor(a, state.nodeMeta(a.id)));
    }
    root["nodes"] = nodesArray;

    // curves 数组（对应 links）
    QJsonArray curvesArray;
    const QVector<Link> &linksList = state.linksAll();
    for (const Link &link : linksList) {
        QJsonObject curveObj;
        const CurvePersistenceMeta meta = state.curveMeta(link.from, link.to);
        curveObj["curve_id"] = meta.curveId;
        curveObj["curve_no"] = meta.curveNo;
        curveObj["node_ids"] = QJsonArray{link.from, link.to};

        QJsonObject densityObj;
        int den = state.segmentDen(link.from, link.to);
        if (state.segmentDensityMode(link.from, link.to) == 0) {
            densityObj["mode"] = QStringLiteral("follow");
        } else if (den > 0) {
            densityObj["mode"]        = QStringLiteral("fixed");
            densityObj["denominator"] = den;
        } else {
            densityObj["mode"] = QStringLiteral("follow");
        }
        curveObj["density"] = densityObj;

        QString shape = state.segmentShape(link.from, link.to);
        curveObj["style_category"] = shape;

        curveObj["group_ids"] = idsToJson(meta.groupIds, Const::kDefaultCurveGroupId);
        curveObj["special_joystick_reserved"] = meta.specialJoystickReserved;
        curveObj["reserved"] = meta.reserved;

        curvesArray.append(curveObj);
    }
    root["curves"] = curvesArray;

    // 附加属性
    root["node_groups"] = groupsToJson(state.nodeGroups(), Const::kDefaultNodeGroupId);
    root["curve_groups"] = groupsToJson(state.curveGroups(), Const::kDefaultCurveGroupId);

    QJsonObject styleObj;
    QJsonArray dens;
    QVector<int> styleDenominators = state.style().denominators;
    if (styleDenominators.isEmpty())
        styleDenominators = {4, 8, 12, 16};
    for (int d : styleDenominators)
        if (d > 0)
            dens.append(d);
    styleObj["denominators"] = dens.isEmpty() ? QJsonArray{4, 8, 12, 16} : dens;
    styleObj["style_name"]   = state.style().name.isEmpty() ? QStringLiteral("balanced") : state.style().name;
    root["style"] = styleObj;

    root["active_link_shape"]       = state.activeLinkShape();
    root["note_curve_snap_enabled"] = state.noteCurveSnapEnabled();

    return root;
}

bool NoteChainPersistence::deserialize(const QJsonObject &json, NoteChainState &state, QString *errorMsg)
{
    const bool recognized = json.contains("format_version") || json.contains("nodes")
                         || json.contains("anchors") || json.contains("curves")
                         || json.contains("links");
    if (!recognized) {
        if (errorMsg) *errorMsg = QStringLiteral("unrecognized curve sidecar payload");
        return false;
    }

    if ((json.contains("nodes") && !json.value("nodes").isArray())
        || (json.contains("anchors") && !json.value("anchors").isArray())
        || (json.contains("curves") && !json.value("curves").isArray())
        || (json.contains("links") && !json.value("links").isArray())) {
        if (errorMsg) *errorMsg = QStringLiteral("curve sidecar arrays have invalid types");
        return false;
    }

    // 先解析到临时 state，全部校验通过后再赋值，避免部分加载破坏现有数据 (P2-3 fix)
    NoteChainState tmp;

    // nodes → anchors.  Python's older V2 sidecar calls this array
    // "anchors"; retain its chart-space handles rather than silently
    // loading an empty curve project.
    QJsonArray nodesArray = json.value("nodes").toArray();
    if (nodesArray.isEmpty() && json.contains("anchors"))
        nodesArray = json.value("anchors").toArray();
    for (const QJsonValue &nodeVal : nodesArray) {
        QJsonObject nodeObj = nodeVal.toObject();
        Anchor anchor;
        if (!deserializeAnchor(nodeObj, anchor, nullptr)) {
            if (errorMsg) *errorMsg = QStringLiteral("invalid anchor entry");
            return false;
        }
        const int index = tmp.insertAnchor(anchor);
        const int actualId = tmp.anchorAt(index).id;
        NodePersistenceMeta meta;
        meta.groupIds = positiveIdsFromJson(nodeObj.value("group_ids"), Const::kDefaultNodeGroupId);
        meta.reserved = nodeObj.value("reserved").toObject();
        tmp.setNodeMeta(actualId, meta);
    }

    // curves → links + segment 属性.  V2 stores plain [from, to] pairs in
    // "links" and keeps per-link properties in root-level maps.
    QJsonArray curvesArray = json.value("curves").toArray();
    for (const QJsonValue &curveVal : curvesArray) {
        QJsonObject curveObj = curveVal.toObject();
        QJsonArray nodeIds = curveObj.value("node_ids").toArray();
        if (nodeIds.size() < 2)
            continue;

        int id0 = nodeIds[0].toInt(-1);
        int id1 = nodeIds[1].toInt(-1);
        if (id0 < 0 || id1 < 0 || id0 == id1)
            continue;

        // 添加链接
        tmp.addLink(id0, id1);
        if (!tmp.hasLink(id0, id1))
            continue;

        CurvePersistenceMeta meta = tmp.curveMeta(id0, id1);
        const int curveId = parseInt(curveObj.value("curve_id"), meta.curveId);
        const int curveNo = parseInt(curveObj.value("curve_no"), meta.curveNo);
        // Zero/negative identities mean "unspecified" in old or partially
        // written sidecars. Keep the positive identities allocated by
        // addLink() instead of replacing them with unusable values.
        if (curveId > 0)
            meta.curveId = curveId;
        if (curveNo > 0)
            meta.curveNo = curveNo;
        meta.groupIds = positiveIdsFromJson(curveObj.value("group_ids"), Const::kDefaultCurveGroupId);
        meta.specialJoystickReserved = curveObj.value("special_joystick_reserved").toObject();
        meta.reserved = curveObj.value("reserved").toObject();
        tmp.setCurveMeta(id0, id1, meta);

        // 密度
        QJsonObject densityObj = curveObj.value("density").toObject();
        QString densityMode = densityObj.value("mode").toString();
        if (densityMode == QLatin1String("fixed")) {
            int den = parseInt(densityObj.value("denominator"), Const::kDefaultSegmentDen);
            tmp.setSegmentDen(id0, id1, den);
        } else if (densityMode == QLatin1String("follow")) {
            tmp.setDensityMode(id0, id1, 0);
        }

        // 形态
        QString shape = curveObj.value("style_category").toString();
        if (!shape.isEmpty()) {
            tmp.setSegmentShape(id0, id1, shape);
        }
    }

    // Some Python releases keep a legacy links array alongside (or instead
    // of) curves.  Import it unconditionally; addLink() normalizes and
    // deduplicates entries already supplied by V3 curves.
    {
        const QJsonArray linksArray = json.value("links").toArray();
        const QJsonObject legacyDensities = json.value("segment_denominators").toObject();
        const QJsonObject legacyDensityModes = json.value("curve_density_mode_by_link").toObject();
        const QJsonObject legacyShapes = json.value("segment_shapes").toObject();
        for (const QJsonValue &linkVal : linksArray) {
            const QJsonArray pair = linkVal.toArray();
            if (pair.size() < 2)
                continue;
            const int id0 = pair[0].toInt(-1);
            const int id1 = pair[1].toInt(-1);
            if (id0 < 0 || id1 < 0 || id0 == id1)
                continue;

            tmp.addLink(id0, id1);
            const Link normalized = tmp.normalizedLink(id0, id1);
            const QString orderedKey = QStringLiteral("%1:%2").arg(normalized.from).arg(normalized.to);
            const QString numericKey = QStringLiteral("%1:%2").arg(qMin(id0, id1)).arg(qMax(id0, id1));
            const QString key = legacyDensities.contains(orderedKey) || legacyDensityModes.contains(orderedKey)
                                    || legacyShapes.contains(orderedKey) ? orderedKey : numericKey;
            const QString densityMode = legacyDensityModes.value(key).toString().trimmed().toLower();
            if (densityMode == QLatin1String("follow")) {
                tmp.setDensityMode(id0, id1, 0);
            } else {
                const int denominator = parseInt(legacyDensities.value(key), Const::kDefaultSegmentDen);
                if (denominator > 0)
                    tmp.setSegmentDen(id0, id1, denominator);
            }
            const QString shape = legacyShapes.value(key).toString();
            if (!shape.isEmpty())
                tmp.setSegmentShape(id0, id1, shape);
        }
    }

    tmp.setNodeGroups(groupsFromJson(json.value("node_groups"), Const::kDefaultNodeGroupId));
    tmp.setCurveGroups(groupsFromJson(json.value("curve_groups"), Const::kDefaultCurveGroupId));

    // 元数据
    if (json.contains("revision")) {
        tmp.setProjectRevision(parseInt(json.value("revision"), 0));
    }
    tmp.setProjectFileUuid(json.value("file_uuid").toString());
    tmp.setLastWriterInstance(json.value("last_writer_instance").toString());
    QJsonObject styleObj = json.value("style").toObject();
    if (!styleObj.isEmpty()) {
        StylePreset style;
        style.name = styleObj.value("style_name").toString(QStringLiteral("loaded"));
        for (const QJsonValue &value : styleObj.value("denominators").toArray()) {
            const int den = parseInt(value, 0);
            if (den > 0)
                style.denominators.append(den);
        }
        if (style.denominators.isEmpty())
            style.denominators = {4, 8, 12, 16};
        tmp.setStyle(style);
    }
    tmp.setActiveLinkShape(json.value("active_link_shape").toString(QStringLiteral("curve")));
    tmp.setNoteCurveSnapEnabled(json.value("note_curve_snap_enabled").toBool(false));
    tmp.setProjectDirty(false);

    // 校验通过后再赋值
    tmp.cleanupLinksAndSelection();
    state = std::move(tmp);
    return true;
}

// ---- 文件读写 ----

bool NoteChainPersistence::saveToFile(NoteChainState &state, const QString &filePath, QString *errorMsg)
{
    QString effectivePath = filePath;
    if (effectivePath.isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("empty file path");
        return false;
    }

    // 确保目录存在
    QFileInfo fi(effectivePath);
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            if (errorMsg) *errorMsg = QStringLiteral("failed to create directory: %1").arg(dir.absolutePath());
            return false;
        }
    }

    QJsonObject payload = serialize(state);
    const QString fileUuid = state.projectFileUuid().isEmpty()
                                 ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                 : state.projectFileUuid();
    const QString writerInstance = state.lastWriterInstance().isEmpty()
                                       ? QStringLiteral("%1-%2")
                                             .arg(QCoreApplication::applicationPid())
                                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(12))
                                       : state.lastWriterInstance();
    payload["file_uuid"] = fileUuid;
    payload["last_writer_instance"] = writerInstance;

    // CAS 版本号递增
    int diskRevision = 0;
    if (fi.exists()) {
        QFile readFile(effectivePath);
        if (!readFile.open(QIODevice::ReadOnly)) {
            if (errorMsg) *errorMsg = QStringLiteral("failed to read existing sidecar: %1").arg(readFile.errorString());
            return false;
        }
        QJsonParseError diskParseError;
        const QJsonDocument diskDoc = QJsonDocument::fromJson(readFile.readAll(), &diskParseError);
        readFile.close();
        if (diskParseError.error != QJsonParseError::NoError || !diskDoc.isObject()) {
            if (errorMsg) *errorMsg = QStringLiteral("existing sidecar is not valid JSON: %1")
                                           .arg(diskParseError.errorString());
            return false;
        }
        diskRevision = parseInt(diskDoc.object().value("revision"), 0);
    }
    // CAS revision 冲突检测：比较内存 revision 与磁盘 revision
    int memRevision = state.projectRevision();
    if (fi.exists() && memRevision != diskRevision) {
        if (errorMsg) *errorMsg = QStringLiteral("revision conflict: file modified by another instance (disk=%1, mem=%2)")
                                       .arg(diskRevision).arg(memRevision);
        return false;
    }

    payload["revision"] = diskRevision + 1;
    payload["updated_at"] = QDateTime::currentMSecsSinceEpoch();

    // 使用 QSaveFile 做原子写入，避免 remove+rename 失败导致数据丢失
    QSaveFile saveFile(effectivePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        if (errorMsg) *errorMsg = QStringLiteral("failed to open save file: %1").arg(saveFile.errorString());
        return false;
    }

    QJsonDocument doc(payload);
    const QByteArray encoded = doc.toJson(QJsonDocument::Indented);
    if (saveFile.write(encoded) != encoded.size()) {
        if (errorMsg) *errorMsg = QStringLiteral("failed to write save file: %1").arg(saveFile.errorString());
        saveFile.cancelWriting();
        return false;
    }

    if (!saveFile.commit()) {
        if (errorMsg) *errorMsg = QStringLiteral("failed to commit save file: %1").arg(saveFile.errorString());
        return false;
    }

    state.setProjectRevision(diskRevision + 1);
    state.setProjectFileUuid(fileUuid);
    state.setLastWriterInstance(writerInstance);
    state.setProjectPath(effectivePath);
    state.setProjectDirty(false);
    return true;
}

bool NoteChainPersistence::loadFromFile(const QString &filePath, NoteChainState &state, QString *errorMsg)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorMsg) *errorMsg = QStringLiteral("file not found: %1").arg(filePath);
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QStringLiteral("failed to open file: %1").arg(filePath);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMsg) *errorMsg = QStringLiteral("JSON parse error: %1").arg(parseError.errorString());
        return false;
    }

    QJsonObject root = doc.object();

    // V2 currently shares the tolerant deserializer with V3.  Keep the
    // common post-load bookkeeping below so both formats participate in CAS.
    const bool ok = deserialize(root, state, errorMsg);
    if (ok)
        state.setProjectPath(filePath);
    return ok;
}

// ---- 侧车文件路径推导 ----

QString NoteChainPersistence::sidecarPathForChart(const QString &chartFilePath)
{
    QFileInfo fi(chartFilePath);
    QString baseName = fi.completeBaseName();
    QString sidecarName = baseName + QStringLiteral(".curve_tbd.json");
    QString sidecarDir = fi.absolutePath() + QStringLiteral("/.mcce-plugin");
    return sidecarDir + QStringLiteral("/") + sidecarName;
}

} // namespace NoteChain
