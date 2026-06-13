#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace BpmAuxFiles
{

/**
 * @brief BPM 排除时间段
 * 用于标记需要排除 BPM 测量的时间段
 */
struct BpmExcludeRange
{
    int startBeatNum;      // 起始拍号
    int startNumerator;    // 起始分子
    int startDenominator;  // 起始分母
    int endBeatNum;        // 结束拍号
    int endNumerator;      // 结束分子
    int endDenominator;    // 结束分母
    QString reason;        // 排除原因（可选）

    BpmExcludeRange()
        : startBeatNum(0), startNumerator(0), startDenominator(1)
        , endBeatNum(0), endNumerator(0), endDenominator(1)
    {}

    BpmExcludeRange(int sBeatNum, int sNum, int sDen,
                   int eBeatNum, int eNum, int eDen,
                   const QString &r = QString())
        : startBeatNum(sBeatNum), startNumerator(sNum), startDenominator(sDen)
        , endBeatNum(eBeatNum), endNumerator(eNum), endDenominator(eDen)
        , reason(r)
    {}

    QJsonObject toJson() const;
    static BpmExcludeRange fromJson(const QJsonObject &obj);
    bool isValid() const;
};

/**
 * @brief 歌曲 BPM 信息
 * 存储歌曲的原始 BPM 信息（非谱面 BPM）
 */
struct SongBpmInfo
{
    double originalBpm;      // 原始 BPM
    QString source;          // BPM 来源（如 "manual", "auto_detect"）
    QString note;            // 备注（可选）
    qint64 timestamp;         // 记录时间戳

    SongBpmInfo()
        : originalBpm(0.0), timestamp(0)
    {}

    SongBpmInfo(double bpm, const QString &src = QString(), const QString &n = QString())
        : originalBpm(bpm), source(src), note(n)
        , timestamp(QDateTime::currentSecsSinceEpoch())
    {}

    QJsonObject toJson() const;
    static SongBpmInfo fromJson(const QJsonObject &obj);
    bool isValid() const;
};

/**
 * @brief BPM 排除文件数据
 */
struct BpmExcludesData
{
    QVector<BpmExcludeRange> excludes;
    QString version;  // 文件版本

    BpmExcludesData() : version("1.0") {}

    QJsonObject toJson() const;
    static BpmExcludesData fromJson(const QJsonObject &obj);
    bool isValid() const;
};

/**
 * @brief 生成 BPM 排除文件路径
 * @param chartPath 谱面文件路径
 * @return BPM 排除文件路径（.mcce-plugin/{chart_stem}.bpm_excludes.json）
 */
QString bpmExcludesFilePath(const QString &chartPath);

/**
 * @brief 生成歌曲 BPM 文件路径
 * @param chartPath 谱面文件路径
 * @return 歌曲 BPM 文件路径（.mcce-plugin/{chart_stem}.song_bpm.json）
 */
QString songBpmFilePath(const QString &chartPath);

/**
 * @brief 加载 BPM 排除文件
 * @param chartPath 谱面文件路径
 * @param outData 输出数据
 * @return 成功返回 true，文件不存在或格式错误时返回 false（不报错）
 */
bool loadBpmExcludes(const QString &chartPath, BpmExcludesData &outData);

/**
 * @brief 保存 BPM 排除文件
 * @param chartPath 谱面文件路径
 * @param data 数据
 * @return 成功返回 true
 */
bool saveBpmExcludes(const QString &chartPath, const BpmExcludesData &data);

/**
 * @brief 加载歌曲 BPM 文件
 * @param chartPath 谱面文件路径
 * @param outInfo 输出信息
 * @return 成功返回 true，文件不存在或格式错误时返回 false（不报错）
 */
bool loadSongBpm(const QString &chartPath, SongBpmInfo &outInfo);

/**
 * @brief 保存歌曲 BPM 文件
 * @param chartPath 谱面文件路径
 * @param info 信息
 * @return 成功返回 true
 */
bool saveSongBpm(const QString &chartPath, const SongBpmInfo &info);

} // namespace BpmAuxFiles
