#include "BpmAuxFiles.h"
#include "utils/Logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>

namespace BpmAuxFiles
{

// BpmExcludeRange 实现
QJsonObject BpmExcludeRange::toJson() const
{
    QJsonObject obj;
    QJsonObject startBeat;
    startBeat["beat_num"] = startBeatNum;
    startBeat["numerator"] = startNumerator;
    startBeat["denominator"] = startDenominator;
    obj["start"] = startBeat;

    QJsonObject endBeat;
    endBeat["beat_num"] = endBeatNum;
    endBeat["numerator"] = endNumerator;
    endBeat["denominator"] = endDenominator;
    obj["end"] = endBeat;

    if (!reason.isEmpty())
        obj["reason"] = reason;

    return obj;
}

BpmExcludeRange BpmExcludeRange::fromJson(const QJsonObject &obj)
{
    BpmExcludeRange range;

    QJsonObject startBeat = obj["start"].toObject();
    range.startBeatNum = startBeat["beat_num"].toInt(0);
    range.startNumerator = startBeat["numerator"].toInt(0);
    range.startDenominator = startBeat["denominator"].toInt(1);

    QJsonObject endBeat = obj["end"].toObject();
    range.endBeatNum = endBeat["beat_num"].toInt(0);
    range.endNumerator = endBeat["numerator"].toInt(0);
    range.endDenominator = endBeat["denominator"].toInt(1);

    range.reason = obj["reason"].toString();

    return range;
}

bool BpmExcludeRange::isValid() const
{
    return startDenominator > 0 && endDenominator > 0;
}

// SongBpmInfo 实现
QJsonObject SongBpmInfo::toJson() const
{
    QJsonObject obj;
    obj["original_bpm"] = originalBpm;
    obj["source"] = source;
    if (!note.isEmpty())
        obj["note"] = note;
    obj["timestamp"] = timestamp;
    return obj;
}

SongBpmInfo SongBpmInfo::fromJson(const QJsonObject &obj)
{
    SongBpmInfo info;
    info.originalBpm = obj["original_bpm"].toDouble(0.0);
    info.source = obj["source"].toString();
    info.note = obj["note"].toString();
    info.timestamp = obj["timestamp"].toVariant().toLongLong();
    return info;
}

bool SongBpmInfo::isValid() const
{
    return originalBpm > 0.0;
}

// BpmExcludesData 实现
QJsonObject BpmExcludesData::toJson() const
{
    QJsonObject obj;
    obj["version"] = version;

    QJsonArray excludesArray;
    for (const BpmExcludeRange &range : excludes)
    {
        if (range.isValid())
            excludesArray.append(range.toJson());
    }
    obj["excludes"] = excludesArray;

    return obj;
}

BpmExcludesData BpmExcludesData::fromJson(const QJsonObject &obj)
{
    BpmExcludesData data;
    data.version = obj["version"].toString("1.0");

    QJsonArray excludesArray = obj["excludes"].toArray();
    for (const QJsonValue &val : excludesArray)
    {
        BpmExcludeRange range = BpmExcludeRange::fromJson(val.toObject());
        if (range.isValid())
            data.excludes.append(range);
    }

    return data;
}

bool BpmExcludesData::isValid() const
{
    return !version.isEmpty();
}

// 工具函数实现
QString chartIdentifierForPath(const QString &chartPath)
{
    // TODO: Wave B replace with UUID
    // Wave A: 使用文件名主干作为标识
    QFileInfo fileInfo(chartPath);
    return fileInfo.baseName();
}

QString bpmExcludesFilePath(const QString &chartPath)
{
    QString identifier = chartIdentifierForPath(chartPath);
    QFileInfo chartFileInfo(chartPath);
    QDir chartDir = chartFileInfo.absoluteDir();
    
    QString sidecarDir = chartDir.absoluteFilePath(".mcce-plugin");
    return QDir(sidecarDir).absoluteFilePath(identifier + ".bpm_excludes.json");
}

QString songBpmFilePath(const QString &chartPath)
{
    QString identifier = chartIdentifierForPath(chartPath);
    QFileInfo chartFileInfo(chartPath);
    QDir chartDir = chartFileInfo.absoluteDir();
    
    QString sidecarDir = chartDir.absoluteFilePath(".mcce-plugin");
    return QDir(sidecarDir).absoluteFilePath(identifier + ".song_bpm.json");
}

bool validateJsonFields(const QJsonObject &obj, const QStringList &requiredFields)
{
    for (const QString &field : requiredFields)
    {
        if (!obj.contains(field))
            return false;
    }
    return true;
}

bool loadBpmExcludes(const QString &chartPath, BpmExcludesData &outData)
{
    QString filePath = bpmExcludesFilePath(chartPath);
    
    if (!QFile::exists(filePath))
    {
        Logger::debug(QString("BpmAuxFiles::loadBpmExcludes - File does not exist: %1").arg(filePath));
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Logger::warn(QString("BpmAuxFiles::loadBpmExcludes - Cannot open file: %1").arg(filePath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject())
    {
        Logger::warn(QString("BpmAuxFiles::loadBpmExcludes - Invalid JSON in file: %1").arg(filePath));
        return false;
    }

    QJsonObject root = doc.object();
    outData = BpmExcludesData::fromJson(root);

    if (!outData.isValid())
    {
        Logger::warn(QString("BpmAuxFiles::loadBpmExcludes - Invalid data format in file: %1").arg(filePath));
        return false;
    }

    Logger::info(QString("BpmAuxFiles::loadBpmExcludes - Loaded %1 exclude ranges from: %2")
                     .arg(outData.excludes.size())
                     .arg(filePath));
    return true;
}

bool saveBpmExcludes(const QString &chartPath, const BpmExcludesData &data)
{
    QString filePath = bpmExcludesFilePath(chartPath);
    
    QFileInfo fileInfo(filePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists() && !dir.mkpath("."))
    {
        Logger::error(QString("BpmAuxFiles::saveBpmExcludes - Cannot create directory: %1").arg(dir.absolutePath()));
        return false;
    }

    QJsonObject root = data.toJson();
    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        Logger::error(QString("BpmAuxFiles::saveBpmExcludes - Cannot open file for writing: %1").arg(filePath));
        return false;
    }

    file.write(doc.toJson());
    file.close();

    Logger::info(QString("BpmAuxFiles::saveBpmExcludes - Saved %1 exclude ranges to: %2")
                     .arg(data.excludes.size())
                     .arg(filePath));
    return true;
}

bool loadSongBpm(const QString &chartPath, SongBpmInfo &outInfo)
{
    QString filePath = songBpmFilePath(chartPath);
    
    if (!QFile::exists(filePath))
    {
        Logger::debug(QString("BpmAuxFiles::loadSongBpm - File does not exist: %1").arg(filePath));
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Logger::warn(QString("BpmAuxFiles::loadSongBpm - Cannot open file: %1").arg(filePath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject())
    {
        Logger::warn(QString("BpmAuxFiles::loadSongBpm - Invalid JSON in file: %1").arg(filePath));
        return false;
    }

    QJsonObject root = doc.object();
    outInfo = SongBpmInfo::fromJson(root);

    if (!outInfo.isValid())
    {
        Logger::warn(QString("BpmAuxFiles::loadSongBpm - Invalid data format in file: %1").arg(filePath));
        return false;
    }

    Logger::info(QString("BpmAuxFiles::loadSongBpm - Loaded song BPM %1 from: %2")
                     .arg(outInfo.originalBpm)
                     .arg(filePath));
    return true;
}

bool saveSongBpm(const QString &chartPath, const SongBpmInfo &info)
{
    QString filePath = songBpmFilePath(chartPath);
    
    QFileInfo fileInfo(filePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists() && !dir.mkpath("."))
    {
        Logger::error(QString("BpmAuxFiles::saveSongBpm - Cannot create directory: %1").arg(dir.absolutePath()));
        return false;
    }

    QJsonObject root = info.toJson();
    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        Logger::error(QString("BpmAuxFiles::saveSongBpm - Cannot open file for writing: %1").arg(filePath));
        return false;
    }

    file.write(doc.toJson());
    file.close();

    Logger::info(QString("BpmAuxFiles::saveSongBpm - Saved song BPM %1 to: %2")
                     .arg(info.originalBpm)
                     .arg(filePath));
    return true;
}

} // namespace BpmAuxFiles
