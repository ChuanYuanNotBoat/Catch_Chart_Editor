#pragma once

#include "model/Chart.h"
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace CceDocument
{
QJsonObject encodeNote(const Note &note, bool canonical = false);
bool decodeNote(const QJsonObject &object, Note &note, QString *error = nullptr);
QJsonObject encodeChart(const Chart &chart, bool canonical = false);
bool decodeChart(const QJsonObject &object, Chart &chart, QString *error = nullptr);

struct Layer
{
    QString layerId;
    QString name;
    QVector<Note> notes;
};

struct LayerBinding
{
    QString layerId;
    bool visible = true;
    bool locked = false;
    bool exportEnabled = true;
    double opacity = 1.0;
    int order = 0;
};

struct DifficultyReference
{
    QString difficultyId;
    QStringList layerIds;
    bool visible = true;
    double opacity = 0.35;
};

struct Difficulty
{
    QString difficultyId;
    QString name;
    MetaData metadata;
    QVector<BpmEntry> timing;
    QString oggHash;
    QVector<LayerBinding> bindings;
    QVector<DifficultyReference> references;
    QSet<QString> exportedMetadataFields;

    Difficulty();
};

struct Project
{
    QString projectId;
    QString activeDifficultyId;
    QString activeLayerId; // runtime editor state; intentionally not serialized
    QMap<QString, Layer> layers;
    QMap<QString, Difficulty> difficulties;

    static Project fromChart(const Chart &chart, const QString &audioHash = QString());
    QJsonObject toJson(bool canonical = false) const;
    static bool fromJson(const QJsonObject &object, Project &project, QString *error = nullptr);
    bool validate(QString *error = nullptr) const;
    Chart compose(const QString &difficultyId, const QStringList &onlyLayerIds = {}, bool forExport = false) const;
    QString addDifficulty(const QString &name, const MetaData &metadata, const QVector<BpmEntry> &timing,
                          const QString &audioHash = QString());
    QString addLayer(const QString &difficultyId, const QString &name);
    bool bindLayer(const QString &difficultyId, const QString &layerId);
    bool unbindLayer(const QString &difficultyId, const QString &layerId);
    QString copyAsIndependent(const QString &difficultyId, const QString &layerId);
    bool addReference(const QString &targetDifficultyId, const QString &sourceDifficultyId,
                      const QStringList &layerIds = {}, QString *error = nullptr);
    int bindingCount(const QString &layerId) const;
    bool updateComposedChart(const QString &difficultyId, const Chart &before, const Chart &after,
                             QString *error = nullptr, const QString &activeLayerId = QString());
};
} // namespace CceDocument
