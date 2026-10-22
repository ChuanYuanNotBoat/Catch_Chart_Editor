#include "MergeExport.h"
#include "utils/MathUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QObject>

namespace MergeExport
{
bool previewBundle(const QVector<ChartMerger::Segment> &segments, const ChartMerger::Plan &plan,
                   const QString &directory, CceDocument::ProjectBundle &out, QString *error)
{
    Chart base;
    const auto meta = plan.chart["meta"].toObject(), song = meta["song"].toObject();
    base.meta().title = song["title"].toString(); base.meta().artist = song["artist"].toString();
    base.meta().difficulty = meta["version"].toString(); base.meta().chartAuthor = meta["creator"].toString();
    base.meta().audioFile = "merged.ogg"; base.meta().backgroundFile = meta["background"].toString();
    base.meta().speed = meta["mode_ext"].toObject().value("speed").toInt(2);
    base.meta().offset = 0;
    // CCE's BpmEntry has no stop/delay field. Only charts containing stops need
    // a continuous preview time axis; ordinary charts keep all merged BPMs.
    bool hasDelay = false;
    for (const auto &v : plan.chart["time"].toArray())
        hasDelay |= v.toObject()["delay"].toDouble() != 0;
    const auto timer = ChartMerger::makeTimer(plan.chart);
    const auto previewBeat = [&](const QJsonArray &b) {
        if (!hasDelay) return b;
        int w, n, d;
        MathUtils::floatToBeat(timer(b)*2, w, n, d, 1000000);
        return QJsonArray{w,n,d};
    };
    if (hasDelay) base.addBpm(BpmEntry(0,0,1,120));
    else
        for (const auto &v : plan.chart["time"].toArray())
        {
            const auto t = v.toObject(); const auto b = t["beat"].toArray();
            base.addBpm(BpmEntry(b[0].toInt(),b[1].toInt(),b[2].toInt(),t["bpm"].toDouble()));
        }
    base.meta().firstBpm = base.bpmList().first().bpm;
    CceDocument::ProjectBundle bundle;
    bundle.project = CceDocument::Project::fromChart(base);
    auto &difficulty = bundle.project.difficulties[bundle.project.activeDifficultyId];
    difficulty.bindings.clear(); bundle.project.layers.clear();
    for (int i = 0; i < segments.size(); ++i)
    {
        const auto layerId = bundle.project.addLayer(difficulty.difficultyId,
            QStringLiteral("%1. %2 [%3]").arg(i+1).arg(segments[i].source.title, segments[i].source.difficulty));
        auto &notes = bundle.project.layers[layerId].notes;
        if (i == 0) notes.append(Note(0,0,1,QStringLiteral("merged.ogg"),100,0));
        for (const auto &v : plan.segmentNotes[i])
        {
            const auto obj = v.toObject(); const auto b = previewBeat(obj["beat"].toArray());
            if (obj.contains("endbeat"))
            {
                const auto end = previewBeat(obj["endbeat"].toArray());
                notes.append(Note(b[0].toInt(),b[1].toInt(),b[2].toInt(),end[0].toInt(),end[1].toInt(),end[2].toInt(),obj["x"].toInt()));
            }
            else notes.append(Note(b[0].toInt(),b[1].toInt(),b[2].toInt(),obj["x"].toInt()));
        }
        if (i == 0) bundle.project.activeLayerId = layerId;
    }
    for (const auto &name : CceDocument::requiredResourcePaths(bundle.project))
    {
        QFile file(QDir(directory).filePath(name));
        if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
        const auto bytes = file.readAll();
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        bundle.resources[hash] = bytes; bundle.resourcePaths[name] = hash;
    }
    difficulty.oggHash = bundle.resourcePaths.value("merged.ogg");
    if (!bundle.project.validate(error)) return false;
    out = std::move(bundle); return true;
}

bool write(const QVector<ChartMerger::Segment> &segments, ChartMerger::Plan plan,
           const QString &directory, const QString &background, const QString &cover,
           QString *error, const MergeAudio::Progress &progress)
{
    const QDir dir(directory);
    if (!MergeAudio::render(segments, plan, dir.filePath("merged.ogg"), error, progress)) return false;
    auto meta = plan.chart["meta"].toObject();
    for (const auto &entry : {qMakePair(QStringLiteral("background"), background), qMakePair(QStringLiteral("cover"), cover)})
    {
        if (entry.second.isEmpty()) continue;
        const auto name = entry.first+"."+QFileInfo(entry.second).suffix();
        if (!QFile::copy(entry.second, dir.filePath(name)))
        { if (error) *error = QObject::tr("Cannot copy image: %1").arg(entry.second); return false; }
        meta[entry.first] = name;
    }
    plan.chart["meta"] = meta;
    QSaveFile chart(dir.filePath("merged.mc"));
    const auto bytes = QJsonDocument(plan.chart).toJson();
    if (!chart.open(QIODevice::WriteOnly) || chart.write(bytes) != bytes.size() || !chart.commit())
    { if (error) *error = chart.errorString(); return false; }
    CceDocument::ProjectBundle bundle;
    if (!previewBundle(segments, plan, directory, bundle, error)) return false;
    return CceDocument::writeProject(dir.filePath("merged.cceproj"),bundle,error);
}
}
