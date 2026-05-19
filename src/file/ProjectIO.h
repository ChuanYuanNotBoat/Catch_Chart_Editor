#pragma once

#include <QString>
#include <QStringList>
#include <QPair>

class ProjectIO
{
public:
    /**
     * @brief 解压 MCZ 文件到指定目录
     * @param mczPath MCZ 文件路径
     * @param outputDir 目标目录（将创建）
     * @param outExtractedDir 输出：解压后的根目录路径
     * @return 成功返回 true
     */
    static bool extractMcz(const QString &mczPath, const QString &outputDir, QString &outExtractedDir);

    /**
     * @brief 将谱面目录打包为 MCZ
     */
    static bool exportToMcz(const QString &outputMczPath, const QString &sourceChartPath);
    static bool exportToMczPure(const QString &outputMczPath, const QString &sourceChartPath);

    /**
     * @brief 递归扫描目录中所有 .mc 文件，并提取难度名
     * @param dirPath 要扫描的目录
     * @return 返回 (文件路径, 难度名) 列表
     */
    static QList<QPair<QString, QString>> findChartsInDirectory(const QString &dirPath);

    /**
     * @brief 从 .mc 文件中读取难度名（meta.version）
     */
    static QString getDifficultyFromMc(const QString &mcPath);
};
