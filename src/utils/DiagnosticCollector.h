#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutex>

/**
 * @brief 诊断数据收集器
 *
 * 统计和收集应用运行过程中的诊断信息，包括：
 * - 跳过的notes统计
 * - 字段缺失分析
 * - 性能指标
 * - 加载/保存操作的详细信息
 *
 * 单例模式，全局唯一实例
 */
class DiagnosticCollector
{
public:
    /**
     * @brief 获取单例实例
     */
    static DiagnosticCollector &instance();

    // Note跳过记录

    /**
     * @brief 记录跳过的note
     * @param noteIndex note在原始列表中的索引
     * @param noteType note的类型（0=normal, 1=sound, 3=rain）
     * @param reason 跳过原因（如"missing_endbeat"）
     * @param missingFields 缺失字段列表（如["endbeat"]）
     * @param presentFields 存在的字段列表（如["beat", "type"]）
     */
    void recordSkippedNote(int noteIndex,
                           int noteType,
                           const QString &reason,
                           const QStringList &missingFields = QStringList(),
                           const QStringList &presentFields = QStringList());

    // 操作指标记录

    /**
     * @brief 记录图表加载的性能指标
     * @param filePath 文件路径
     * @param duration 耗时（毫秒）
     * @param totalNotes 原始文件中的note总数
     * @param loadedNotes 成功加载的note数
     * @param skippedNotes 跳过的note数
     */
    void recordLoadMetrics(const QString &filePath,
                           qint64 duration,
                           int totalNotes,
                           int loadedNotes,
                           int skippedNotes);

    /**
     * @brief 记录图表保存的性能指标
     */
    void recordSaveMetrics(const QString &filePath,
                           qint64 duration,
                           int notesCount);

    /**
     * @brief 记录渲染帧的性能指标
     */
    void recordRenderMetrics(qint64 frameTimeMs, int notesRenderedCount);

    // 诊断报告生成

    /**
     * @brief 诊断报告结构体
     */
    struct DiagnosticReport
    {
        // Note跳过统计
        struct SkippedNotesSummary
        {
            int totalSkipped = 0;
            QMap<QString, int> byReason;       ///< 按原因分类的跳过统计（reason -> count）
            QMap<int, int> byType;             ///< 按note类型分类（type -> count）
            QMap<QString, int> byMissingField; ///< 按缺失字段分类（field -> count）
        } skippedNotesSummary;

        // 性能指标
        struct PerformanceMetrics
        {
            qint64 lastLoadDuration = 0;  ///< 最后一次加载耗时
            qint64 totalLoadDuration = 0; ///< 所有加载操作的总耗时
            int loadCount = 0;            ///< 加载操作次数

            qint64 lastSaveDuration = 0;
            qint64 totalSaveDuration = 0;
            int saveCount = 0;

            qint64 maxFrameTime = 0; ///< 最长帧时间
            double avgFrameTime = 0; ///< 平均帧时间
            qint64 totalRenderTime = 0;
            int totalFrames = 0;
        } performanceMetrics;

        // 最后操作的文件信息
        QString lastLoadedFilePath;
        QString lastSavedFilePath;

        /**
         * @brief 转换为JSON对象（用于导出和显示）
         */
        QJsonObject toJsonObject() const;

        /**
         * @brief 生成易读的文本报告
         */
        QString toFormattedString() const;
    };

    /**
     * @brief 生成诊断报告
     */
    DiagnosticReport generateReport() const;

    /**
     * @brief 清除所有诊断数据
     */
    void clear();

    /**
     * @brief 获取详细的跳过note列表
     */
    struct SkippedNoteDetail
    {
        int index;
        int type;
        QString reason;
        QStringList missingFields;
        QStringList presentFields;
    };

    QVector<SkippedNoteDetail> getSkippedNoteDetails() const;

    /**
     * @brief 以JSON格式导出诊断数据
     */
    QJsonDocument toJsonDocument() const;

private:
    // 私有构造函数（单例）
    DiagnosticCollector() = default;

    QVector<SkippedNoteDetail> m_skippedNotes;

    struct LoadMetrics
    {
        QString filePath;
        qint64 duration;
        int totalNotes;
        int loadedNotes;
        int skippedNotes;
    };
    QVector<LoadMetrics> m_loadMetrics;

    struct SaveMetrics
    {
        QString filePath;
        qint64 duration;
        int notesCount;
    };
    QVector<SaveMetrics> m_saveMetrics;

    struct RenderMetricsData
    {
        qint64 frameTimeMs;
        int notesRenderedCount;
    };
    QVector<RenderMetricsData> m_renderMetrics;
    mutable QMutex m_mutex;
};
