#pragma once

#include <QString>
#include "model/Chart.h"

class ChartIO
{
public:
    /**
     * @brief 加载谱面文件
     * @param filePath 文件路径
     * @param outChart 输出的谱面数据
     * @param verbose 是否输出详细日志。导入时设为false（只输出统计），编辑时设为true（输出详细信息）
     */
    static bool load(const QString &filePath, Chart &outChart, bool verbose = true);
    static bool save(const QString &filePath, const Chart &chart);

    /**
     * @brief 创建默认标准化空谱面（仅包含 meta 和默认 120 BPM time 条目，note/effect 为空）
     */
    static Chart createDefaultChart(const MetaData &meta);
};
