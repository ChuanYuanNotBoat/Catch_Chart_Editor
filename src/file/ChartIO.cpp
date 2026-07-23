// src/file/ChartIO.cpp
#include "ChartIO.h"
#include "utils/Logger.h"
#include "utils/DiagnosticCollector.h"
#include "utils/PerformanceTimer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <algorithm>
#include <cmath>

namespace
{
    /// 计算文件前 64KB 的 SHA-256 哈希，用于快速比较文件内容
    QString computeQuickHash(const QString &filePath)
    {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        const QByteArray chunk = f.read(65536);
        f.close();
        return QString::fromLatin1(
            QCryptographicHash::hash(chunk, QCryptographicHash::Sha256).toHex());
    }

    /**
     * 补丁：兼容旧版本错误保存的完整路径，提取文件名并尝试收集资源到 .mc 目录。
     *
     * @param rawValue  从 meta 中读取的原始值（可能是路径或纯文件名）
     * @param mcDir     .mc 文件所在目录
     * @param outFull   输出：解析后的完整路径（供编辑器内部使用）
     * @return 纯文件名（供存储到 meta.backgroundFile / audioFile 的临时值）
     */
    QString resolveAndCollectResource(const QString &rawValue, const QString &mcDir, QString &outFull)
    {
        if (rawValue.isEmpty())
        {
            outFull.clear();
            return rawValue;
        }

        // 步骤1：提取纯文件名（补丁兼容：旧版本可能写了完整路径）
        QString fileName;
        const bool hasPathSep = rawValue.contains('/') || rawValue.contains('\\');
        if (hasPathSep)
        {
            fileName = QFileInfo(rawValue).fileName();
            if (fileName.isEmpty())
            {
                // 无法提取文件名，保持原值
                outFull = rawValue;
                return rawValue;
            }
            Logger::info(QString("ChartIO::load - [PATCH] Extracted filename '%1' from path '%2'")
                             .arg(fileName, rawValue));
        }
        else
        {
            fileName = rawValue;
        }

        const QString localCandidate = QDir(mcDir).absoluteFilePath(fileName);

        // 步骤2：如果 .mc 同目录下已经存在同名文件
        if (QFileInfo::exists(localCandidate))
        {
            // 如果原始值是路径且指向真实文件，比较哈希
            if (hasPathSep && QFileInfo::exists(rawValue))
            {
                const QString localHash = computeQuickHash(localCandidate);
                const QString sourceHash = computeQuickHash(rawValue);
                if (!localHash.isEmpty() && !sourceHash.isEmpty() && localHash != sourceHash)
                {
                    // 哈希不同，弹窗询问
                    const QString defaultRename = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_" + fileName;
                    QMessageBox msgBox;
                    msgBox.setIcon(QMessageBox::Warning);
                    msgBox.setWindowTitle(QObject::tr("Resource Conflict"));
                    msgBox.setText(QObject::tr("File '%1' already exists but content differs.\nDo you want to rename the imported file?").arg(fileName));
                    msgBox.setInformativeText(QObject::tr("Default new name: %1").arg(defaultRename));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::Yes);
                    msgBox.button(QMessageBox::Yes)->setText(QObject::tr("Rename Import"));
                    msgBox.button(QMessageBox::No)->setText(QObject::tr("Use Existing"));

                    if (msgBox.exec() == QMessageBox::Yes)
                    {
                        // 使用新名称复制
                        const QString newPath = QDir(mcDir).absoluteFilePath(defaultRename);
                        if (QFile::copy(rawValue, newPath))
                        {
                            Logger::info(QString("ChartIO::load - [PATCH] Copied to '%1'").arg(defaultRename));
                            outFull = newPath;
                            return defaultRename;
                        }
                    }
                    // 否则使用已存在的本地文件
                }
            }
            // 哈希相同、或原始值不是真实路径、或用户选择跳过 → 直接使用本地文件
            outFull = localCandidate;
            return fileName;
        }

        // 步骤3：.mc 目录下不存在，尝试从原始路径复制
        if (hasPathSep && QFileInfo::exists(rawValue))
        {
            if (QFile::copy(rawValue, localCandidate))
            {
                Logger::info(QString("ChartIO::load - [PATCH] Collected '%1' from '%2'")
                                 .arg(fileName, rawValue));
                outFull = localCandidate;
                return fileName;
            }
            Logger::warn(QString("ChartIO::load - [PATCH] Failed to copy '%1' to .mc directory").arg(rawValue));
        }

        // 步骤4：无法收集，使用原始值（fallback）
        outFull = rawValue;
        return fileName;
    }
}

bool ChartIO::load(const QString &filePath, Chart &outChart, bool verbose)
{
    PerformanceTimer loadTimer("ChartIO::load", "chart_loading");

    Logger::info(QString("ChartIO::load - Loading chart from: %1 (verbose mode: %2)").arg(filePath).arg(verbose ? "on" : "off"));

    // 保存原来的 verbose 设置
    bool previousVerbose = Logger::isVerbose();
    // 设置为导入所需的 verbose 模式
    Logger::setVerbose(verbose);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Logger::error(QString("ChartIO::load - Cannot open file: %1").arg(filePath));
        Logger::setVerbose(previousVerbose); // 恢复设置
        return false;
    }

    QByteArray data = file.readAll();
    file.close();
    Logger::debug(QString("ChartIO::load - Read %1 bytes from file").arg(data.size()));

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull())
    {
        Logger::error(QString("ChartIO::load - Invalid JSON in file: %1").arg(filePath));
        Logger::setVerbose(previousVerbose); // 恢复设置
        return false;
    }

    QJsonObject root = doc.object();
    outChart.clear();
    Logger::debug("ChartIO::load - JSON parsed successfully, starting to load data");

    // 读取 time 数组（BPM 表）
    int bpmCount = 0;
    // 清空现有的 BPM 列表（包括 clear() 添加的默认条目）
    outChart.bpmList().clear();
    if (root.contains("time") && root["time"].isArray())
    {
        QJsonArray timeArray = root["time"].toArray();
        Logger::debug(QString("ChartIO::load - Found 'time' array with %1 entries").arg(timeArray.size()));
        for (const QJsonValue &val : timeArray)
        {
            QJsonObject obj = val.toObject();
            if (!obj.contains("beat") || !obj.contains("bpm"))
                continue;
            QJsonArray beatArr = obj["beat"].toArray();
            if (beatArr.size() < 3)
                continue;
            int beatNum = beatArr[0].toInt();
            int num = beatArr[1].toInt();
            int den = beatArr[2].toInt();
            double bpm = obj["bpm"].toDouble();
            if (den <= 0 || !std::isfinite(bpm) || bpm <= 0.0)
            {
                Logger::warn(QString("ChartIO::load - Skip invalid BPM entry at beat [%1,%2,%3], bpm=%4")
                                 .arg(beatNum)
                                 .arg(num)
                                 .arg(den)
                                 .arg(bpm));
                continue;
            }
            outChart.addBpm(BpmEntry(beatNum, num, den, bpm));
            bpmCount++;
        }
        Logger::info(QString("ChartIO::load - Loaded %1 BPM entries").arg(bpmCount));
    }
    else
    {
        Logger::debug("ChartIO::load - No 'time' array found in file");
    }
    // 如果文件中没有 BPM 条目，确保至少有一个默认 BPM 条目
    if (bpmCount == 0)
    {
        outChart.addBpm(BpmEntry(0, 1, 1, 120.0));
        Logger::debug("ChartIO::load - Added default BPM entry (120.0)");
    }

    // 读取 note 数组
    int normalNoteCount = 0, rainNoteCount = 0, soundNoteCount = 0, skippedNoteCount = 0;
    int totalNoteCount = 0; // 用于统计成功率

    if (root.contains("note") && root["note"].isArray())
    {
        QJsonArray noteArray = root["note"].toArray();
        totalNoteCount = noteArray.size();
        Logger::debug(QString("ChartIO::load - Found 'note' array with %1 entries").arg(totalNoteCount));

        for (int i = 0; i < noteArray.size(); i++)
        {
            QJsonObject obj = noteArray[i].toObject();
            QJsonArray beatArr = obj["beat"].toArray();

            if (!obj.contains("beat") || beatArr.size() < 3)
            {
                Logger::warn(QString("ChartIO::load - Note %1: Skipped (invalid beat array)").arg(i));
                skippedNoteCount++;

                // 记录诊断信息
                QStringList missingFields;
                missingFields << "beat";
                DiagnosticCollector::instance().recordSkippedNote(i, -1, "invalid_beat", missingFields);
                continue;
            }

            int beatNum = beatArr[0].toInt();
            int num = beatArr[1].toInt();
            int den = beatArr[2].toInt();
            int type = obj.value("type").toInt(0);
            if (den <= 0)
            {
                Logger::warn(QString("ChartIO::load - Note %1: Skipped (invalid beat denominator=%2)").arg(i).arg(den));
                skippedNoteCount++;
                continue;
            }

            // 音效音符（type=1）：有 sound 字段，无 x 字段
            if (type == 1 && obj.contains("sound"))
            {
                QString sound = obj["sound"].toString();
                int vol = obj.value("vol").toInt(100);
                int offset = obj.value("offset").toInt(0);
                outChart.addNote(Note(beatNum, num, den, sound, vol, offset));
                soundNoteCount++;
                if (Logger::isVerbose())
                {
                    if (verbose && i < 20)
                        Logger::debug(QString("ChartIO::load - Note %1: Sound note at [%2,%3,%4], sound=%5").arg(i).arg(beatNum).arg(num).arg(den).arg(sound));
                }
            }
            // Rain 音符（type=3）：有 endbeat 字段，x 字段可选
            else if (type == 3 && obj.contains("endbeat"))
            {
                QJsonArray endBeatArr = obj["endbeat"].toArray();
                if (endBeatArr.size() >= 3)
                {
                    int x = obj.value("x").toInt(256); // 如果没有 x 字段，默认值为 256（屏幕中央）
                    int endBeatNum = endBeatArr[0].toInt();
                    int endNum = endBeatArr[1].toInt();
                    int endDen = endBeatArr[2].toInt();
                    if (endDen <= 0)
                    {
                        Logger::warn(QString("ChartIO::load - Note %1: Skipped (type=3, invalid endbeat denominator=%2)").arg(i).arg(endDen));
                        skippedNoteCount++;
                        continue;
                    }
                    x = std::clamp(x, 0, 512);
                    outChart.addNote(Note(beatNum, num, den, endBeatNum, endNum, endDen, x));
                    rainNoteCount++;
                    if (Logger::isVerbose())
                    {
                        if (verbose && i < 20)
                            Logger::debug(QString("ChartIO::load - Note %1: Rain note at [%2,%3,%4]-[%5,%6,%7], x=%8").arg(i).arg(beatNum).arg(num).arg(den).arg(endBeatNum).arg(endNum).arg(endDen).arg(x));
                    }
                }
                else
                {
                    Logger::warn(QString("ChartIO::load - Note %1: Skipped (type=3, invalid endbeat format)").arg(i));
                    skippedNoteCount++;

                    // 记录诊断信息
                    QStringList missingFields;
                    missingFields << "endbeat";
                    QStringList presentFields;
                    presentFields << "beat" << "type";
                    if (obj.contains("x"))
                        presentFields << "x";
                    DiagnosticCollector::instance().recordSkippedNote(i, 3, "invalid_endbeat_format", missingFields, presentFields);
                }
            }
            // 普通音符（type=0）：必须有 x 字段
            else if (obj.contains("x"))
            {
                int x = std::clamp(obj["x"].toInt(), 0, 512);
                outChart.addNote(Note(beatNum, num, den, x));
                normalNoteCount++;
                if (Logger::isVerbose())
                {
                    if (verbose && i < 20)
                        Logger::debug(QString("ChartIO::load - Note %1: Normal note at [%2,%3,%4], x=%5").arg(i).arg(beatNum).arg(num).arg(den).arg(x));
                }
            }
            // Sound note检查（type=1但缺少sound字段）
            else if (type == 1)
            {
                Logger::warn(QString("ChartIO::load - Note %1: Skipped (type=1, missing 'sound' field)").arg(i));
                skippedNoteCount++;

                QStringList missingFields;
                missingFields << "sound";
                QStringList presentFields;
                presentFields << "beat" << "type";
                if (obj.contains("vol"))
                    presentFields << "vol";
                if (obj.contains("offset"))
                    presentFields << "offset";
                DiagnosticCollector::instance().recordSkippedNote(i, 1, "missing_sound_field", missingFields, presentFields);
            }
            // Rain note检查（type=3但缺少endbeat字段）
            else if (type == 3)
            {
                Logger::warn(QString("ChartIO::load - Note %1: Skipped (type=3, missing 'endbeat' field)").arg(i));
                skippedNoteCount++;

                QStringList missingFields;
                missingFields << "endbeat";
                QStringList presentFields;
                presentFields << "beat" << "type";
                if (obj.contains("x"))
                    presentFields << "x";
                DiagnosticCollector::instance().recordSkippedNote(i, 3, "missing_endbeat_field", missingFields, presentFields);
            }
            // 普通音符缺少x字段
            else
            {
                Logger::warn(QString("ChartIO::load - Note %1: Skipped (type=%2, missing 'x' field)").arg(i).arg(type));
                skippedNoteCount++;

                QStringList missingFields;
                missingFields << "x";
                QStringList presentFields;
                presentFields << "beat" << "type";
                DiagnosticCollector::instance().recordSkippedNote(i, type, "missing_x_field", missingFields, presentFields);
            }
        }

        // 输出详细统计信息
        int totalLoadedNotes = normalNoteCount + rainNoteCount + soundNoteCount;
        double successRate = totalNoteCount > 0 ? (totalLoadedNotes * 100.0 / totalNoteCount) : 0.0;

        Logger::info(QString("ChartIO::load - Loaded %1 normal notes, %2 rain notes, %3 sound notes")
                         .arg(normalNoteCount)
                         .arg(rainNoteCount)
                         .arg(soundNoteCount));
        Logger::info(QString("ChartIO::load - Note Summary: %1 / %2 notes loaded (%3%% success rate)")
                         .arg(totalLoadedNotes)
                         .arg(totalNoteCount)
                         .arg(QString::number(successRate, 'f', 1)));

        if (skippedNoteCount > 0)
        {
            Logger::warn(QString("ChartIO::load - Skipped %1 notes with missing/invalid fields").arg(skippedNoteCount));
        }

        // 记录加载指标
        DiagnosticCollector::instance().recordLoadMetrics(filePath, loadTimer.elapsed(),
                                                          totalNoteCount, totalLoadedNotes, skippedNoteCount);
    }
    else
    {
        Logger::debug("ChartIO::load - No 'note' array found in file");
    }

    // 读取 meta（如果有）
    if (root.contains("meta") && root["meta"].isObject())
    {
        QJsonObject metaObj = root["meta"].toObject();
        // 调试：记录所有meta键
        for (auto it = metaObj.begin(); it != metaObj.end(); ++it)
        {
            Logger::info(QString("ChartIO::load - meta key '%1' = '%2'").arg(it.key()).arg(it.value().toString()));
        }
        MetaData &meta = outChart.meta();

        // 支持嵌套 song 对象
        QJsonObject songObj;
        if (metaObj.contains("song") && metaObj["song"].isObject())
        {
            songObj = metaObj["song"].toObject();
            meta.title = songObj.value("title").toString();
            meta.titleOrg = songObj.value("titleorg").toString();
            meta.artist = songObj.value("artist").toString();
            meta.artistOrg = songObj.value("artistorg").toString();
        }
        else
        {
            // 回退到平面字段
            meta.title = metaObj.value("title").toString();
            meta.titleOrg = metaObj.value("title_org").toString();
            meta.artist = metaObj.value("artist").toString();
            meta.artistOrg = metaObj.value("artist_org").toString();
        }

        meta.chartAuthor = metaObj.value("creator").toString();
        meta.difficulty = metaObj.value("version").toString();

        // 补丁：兼容旧版本错误保存的完整背景路径，提取文件名并尝试收集资源
        {
            const QString bgValue = metaObj.value("background").toString();
            QString fullPath;
            const QString bgFileName = resolveAndCollectResource(bgValue, QFileInfo(filePath).absolutePath(), fullPath);
            meta.backgroundFile = bgFileName.isEmpty() ? bgValue : bgFileName;
        }

        // 读取 mode_ext.speed
        if (metaObj.contains("mode_ext") && metaObj["mode_ext"].isObject())
        {
            QJsonObject modeExtObj = metaObj["mode_ext"].toObject();
            meta.speed = modeExtObj.value("speed").toInt();
        }
        else
        {
            meta.speed = metaObj.value("speed").toInt();
        }

        // 音频文件：优先使用 meta.audio，否则从 note 数组中寻找 sound 字段
        // 补丁：兼容旧版本错误保存的完整音频路径，提取文件名并尝试收集资源
        {
            QString audioValue = metaObj.value("audio").toString();
            if (audioValue.isEmpty() && root.contains("note") && root["note"].isArray())
            {
                QJsonArray noteArray = root["note"].toArray();
                for (const QJsonValue &noteVal : noteArray)
                {
                    QJsonObject noteObj = noteVal.toObject();
                    if (noteObj.contains("sound") && noteObj["sound"].isString())
                    {
                        audioValue = noteObj["sound"].toString();
                        break;
                    }
                }
            }
            QString fullPath;
            QString audioFileName = resolveAndCollectResource(audioValue, QFileInfo(filePath).absolutePath(), fullPath);
            if (audioFileName.isEmpty())
                audioFileName = audioValue;

            // Rename long audio filenames to timestamp-based names to avoid
            // path length limits and Malody V issues with non-ASCII filenames.
            const int kMaxAudioNameLen = 24;
            if (!audioFileName.isEmpty() && audioFileName.length() > kMaxAudioNameLen)
            {
                const QFileInfo afi(audioFileName);
                const QString suffix = afi.suffix();
                const QString mcDir = QFileInfo(filePath).absolutePath();
                const QString oldPath = QDir(mcDir).filePath(audioFileName);
                const QString newName = QString::number(static_cast<uint>(QDateTime::currentSecsSinceEpoch())) + "." + suffix;
                const QString newPath = QDir(mcDir).filePath(newName);
                if (QFileInfo::exists(oldPath))
                {
                    if (QFile::rename(oldPath, newPath))
                    {
                        Logger::info(QString("ChartIO::load - Renamed long audio '%1' -> '%2'")
                                         .arg(audioFileName, newName));
                        audioFileName = newName;
                    }
                    else
                    {
                        Logger::warn(QString("ChartIO::load - Failed to rename '%1' to '%2'")
                                         .arg(audioFileName, newName));
                    }
                }
                else
                {
                    Logger::info(QString("ChartIO::load - Shortened audio reference '%1' -> '%2' (file not on disk)")
                                     .arg(audioFileName, newName));
                    audioFileName = newName;
                }
            }
            meta.audioFile = audioFileName;
        }

        // 预览时间和偏移量
        meta.previewTime = metaObj.value("preview").toInt(); // 字段名是 preview 不是 preview_time
        meta.offset = metaObj.value("offset").toInt();

        // 如果 offset 为0，尝试从 note 数组中的 sound 音符读取 offset 字段
        if (meta.offset == 0 && root.contains("note") && root["note"].isArray())
        {
            QJsonArray noteArray = root["note"].toArray();
            for (const QJsonValue &noteVal : noteArray)
            {
                QJsonObject noteObj = noteVal.toObject();
                if (noteObj.contains("type") && noteObj["type"].toInt() == 1 && noteObj.contains("offset"))
                {
                    meta.offset = noteObj["offset"].toInt();
                    break;
                }
            }
        }

        // 第一个 BPM 从 time 数组读取
        if (!outChart.bpmList().empty())
        {
            meta.firstBpm = outChart.bpmList()[0].bpm;
        }
        else
        {
            meta.firstBpm = metaObj.value("bpm").toDouble();
        }

        Logger::info(QString("ChartIO::load - Meta loaded: title=%1, artist=%2, difficulty=%3, speed=%4, firstBpm=%5")
                         .arg(meta.title)
                         .arg(meta.artist)
                         .arg(meta.difficulty)
                         .arg(meta.speed)
                         .arg(meta.firstBpm));
    }
    else
    {
        Logger::info("ChartIO::load - No 'meta' object found in file");
    }

    outChart.sortNotes();
    Logger::info(QString("ChartIO::load - Chart loaded successfully from: %1").arg(filePath));
    Logger::setVerbose(previousVerbose); // 恢复原来的设置
    return true;
}

Chart ChartIO::createDefaultChart(const MetaData &meta)
{
    Chart chart;
    chart.meta() = meta;
    const double bpm = meta.firstBpm > 0.0 ? meta.firstBpm : 120.0;
    chart.addBpm(BpmEntry(0, 0, 1, bpm));

    // Add a sound note referencing the audio file so that Malody V can
    // locate the audio resource (Malody V resolves audio via note[type=1].sound,
    // not via meta.audio).
    if (!meta.audioFile.isEmpty())
    {
        const int offset = meta.offset;
        chart.addNote(Note(0, 0, 1, meta.audioFile, 100, offset));
    }

    chart.sortNotes();
    return chart;
}

bool ChartIO::save(const QString &filePath, const Chart &chart)
{
    PerformanceTimer saveTimer("ChartIO::save", "chart_saving");

    Logger::info(QString("ChartIO::save - Saving chart to: %1").arg(filePath));
    Logger::debug(QString("ChartIO::save - Chart has %1 notes").arg(chart.notes().size()));

    QJsonObject root;

    // 补丁：辅助 lambda，将资源文件的完整路径转换为纯文件名并复制到 .mc 目录
    const QDir chartSaveDir(QFileInfo(filePath).absolutePath());
    auto resolveResourcePath = [&chartSaveDir](const QString &value) -> QString {
        if (value.isEmpty()) return value;
        // 如果已经是纯文件名（不含路径分隔符），直接返回
        if (!QDir::isAbsolutePath(value) && !value.contains('/') && !value.contains('\\'))
            return value;
        // 如果是路径，提取文件名
        QString fileName = QFileInfo(value).fileName();
        if (fileName.isEmpty()) return value;
        // 如果目标目录没有这个文件，尝试复制
        QString targetPath = chartSaveDir.filePath(fileName);
        if (!QFileInfo::exists(targetPath) && QFileInfo::exists(value))
            QFile::copy(value, targetPath);
        return fileName;
    };

    // 保存 meta（使用嵌套结构以匹配示例格式）
    QJsonObject metaObj;
    metaObj["$ver"] = 0;
    metaObj["creator"] = chart.meta().chartAuthor;
    metaObj["background"] = resolveResourcePath(chart.meta().backgroundFile);
    metaObj["version"] = chart.meta().difficulty;
    metaObj["id"] = 0;                                    // 暂未存储歌曲ID，用0代替
    metaObj["mode"] = 3;                                  // Catch模式
    metaObj["time"] = QDateTime::currentSecsSinceEpoch(); // 当前时间戳

    // song 子对象
    QJsonObject songObj;
    songObj["title"] = chart.meta().title;
    songObj["artist"] = chart.meta().artist;
    songObj["id"] = 0; // 暂未存储歌曲ID
    if (!chart.meta().titleOrg.isEmpty())
        songObj["titleorg"] = chart.meta().titleOrg;
    if (!chart.meta().artistOrg.isEmpty())
        songObj["artistorg"] = chart.meta().artistOrg;
    metaObj["song"] = songObj;

    // mode_ext 子对象
    QJsonObject modeExtObj;
    modeExtObj["speed"] = chart.meta().speed;
    metaObj["mode_ext"] = modeExtObj;

    // 注意：示例谱面中没有 audio、preview_time、offset、bpm 字段
    // 这些信息可能通过其他方式存储（例如 note 数组中的 sound 字段）
    // 如果需要保留，可以额外添加平面字段，但为了兼容性暂时省略
    // 如果 chart.meta() 中有这些值，可以选择性添加
    if (!chart.meta().audioFile.isEmpty())
    {
        metaObj["audio"] = resolveResourcePath(chart.meta().audioFile);
    }
    if (chart.meta().previewTime != 0)
    {
        metaObj["preview"] = chart.meta().previewTime; // 字段名是 preview
    }
    if (chart.meta().offset != 0)
    {
        metaObj["offset"] = chart.meta().offset;
    }
    // firstBpm 已经通过 time 数组存储，但也可以保存在 meta.bpm 中
    // 为了兼容性，可以同时保存
    if (chart.meta().firstBpm > 0)
    {
        metaObj["bpm"] = chart.meta().firstBpm;
    }

    root["meta"] = metaObj;
    Logger::debug(QString("ChartIO::save - Meta saved: title=%1, difficulty=%2, speed=%3")
                      .arg(chart.meta().title)
                      .arg(chart.meta().difficulty)
                      .arg(chart.meta().speed));

    // 保存 time 数组
    QJsonArray timeArray;
    for (const BpmEntry &bpm : chart.bpmList())
    {
        QJsonObject obj;
        QJsonArray beatArr;
        beatArr.append(bpm.beatNum);
        beatArr.append(bpm.numerator);
        beatArr.append(bpm.denominator);
        obj["beat"] = beatArr;
        obj["bpm"] = bpm.bpm;
        timeArray.append(obj);
    }
    root["time"] = timeArray;
    Logger::debug(QString("ChartIO::save - Time array saved with %1 entries").arg(timeArray.size()));

    // 保存 effect 数组（暂时为空，留作扩展）
    QJsonArray effectArray;
    root["effect"] = effectArray;
    Logger::debug("ChartIO::save - Effect array saved (empty)");

    // 保存 note 数组
    QJsonArray noteArray;
    int normalNoteCount = 0, rainNoteCount = 0, soundNoteCount = 0;

    for (const Note &note : chart.notes())
    {
        QJsonObject obj;
        QJsonArray beatArr;
        beatArr.append(note.beatNum);
        beatArr.append(note.numerator);
        beatArr.append(note.denominator);
        obj["beat"] = beatArr;

        if (note.type == NoteType::SOUND)
        {
            // 音效音符
            obj["type"] = 1;
            obj["sound"] = note.sound;
            obj["vol"] = note.vol;
            obj["offset"] = note.offset;
            soundNoteCount++;
            Logger::debug(QString("ChartIO::save - Sound note: [%1,%2,%3], sound=%4").arg(note.beatNum).arg(note.numerator).arg(note.denominator).arg(note.sound));
        }
        else if (note.type == NoteType::RAIN)
        {
            // Rain 音符
            obj["type"] = 3;
            obj["x"] = note.x;
            QJsonArray endBeatArr;
            endBeatArr.append(note.endBeatNum);
            endBeatArr.append(note.endNumerator);
            endBeatArr.append(note.endDenominator);
            obj["endbeat"] = endBeatArr;
            rainNoteCount++;
            Logger::debug(QString("ChartIO::save - Rain note: [%1,%2,%3]-[%4,%5,%6], x=%7").arg(note.beatNum).arg(note.numerator).arg(note.denominator).arg(note.endBeatNum).arg(note.endNumerator).arg(note.endDenominator).arg(note.x));
        }
        else
        {
            // 普通音符
            obj["x"] = note.x;
            normalNoteCount++;
            Logger::debug(QString("ChartIO::save - Normal note: [%1,%2,%3], x=%4").arg(note.beatNum).arg(note.numerator).arg(note.denominator).arg(note.x));
        }
        noteArray.append(obj);
    }

    // Ensure a sound note referencing the audio file is always present.
    // Malody V resolves audio via note[type=1].sound, not via meta.audio.
    // This also upgrades legacy charts that only had meta.audio without a sound note.
    if (soundNoteCount == 0 && !chart.meta().audioFile.isEmpty())
    {
        QJsonObject soundObj;
        QJsonArray beatArr;
        beatArr.append(0);
        beatArr.append(0);
        beatArr.append(1);
        soundObj["beat"] = beatArr;
        soundObj["type"] = 1;
        soundObj["sound"] = resolveResourcePath(chart.meta().audioFile);
        soundObj["vol"] = 100;
        soundObj["offset"] = chart.meta().offset;
        noteArray.insert(0, soundObj);
        soundNoteCount = 1;
        Logger::debug(QString("ChartIO::save - Injected sound note for audio: %1").arg(chart.meta().audioFile));
    }

    root["note"] = noteArray;
    Logger::info(QString("ChartIO::save - Saved %1 normal notes, %2 rain notes, %3 sound notes").arg(normalNoteCount).arg(rainNoteCount).arg(soundNoteCount));

    // 保存 extra（可选，包含测试配置等）
    QJsonObject extraObj;
    QJsonObject testObj;
    testObj["divide"] = 4;
    testObj["speed"] = 100;
    testObj["save"] = 0;
    testObj["lock"] = 0;
    testObj["edit_mode"] = 0;
    extraObj["test"] = testObj;
    root["extra"] = extraObj;
    Logger::debug("ChartIO::save - Extra (test config) saved");

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        Logger::error(QString("ChartIO::save - Cannot open file for writing: %1").arg(filePath));
        return false;
    }

    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    file.write(jsonData);
    file.close();

    Logger::info(QString("ChartIO::save - Chart saved successfully (%1 bytes) to: %2").arg(jsonData.size()).arg(filePath));

    // 记录保存指标
    DiagnosticCollector::instance().recordSaveMetrics(filePath, saveTimer.elapsed(), chart.notes().size());

    return true;
}
