#include "DiagnosticCollector.h"
#include "FileUtils.h"
#include <QJsonArray>
#include <QMutexLocker>

DiagnosticCollector &DiagnosticCollector::instance()
{
    static DiagnosticCollector s_instance;
    return s_instance;
}

void DiagnosticCollector::recordSkippedNote(int noteIndex,
                                            int noteType,
                                            const QString &reason,
                                            const QStringList &missingFields,
                                            const QStringList &presentFields)
{
    QMutexLocker locker(&m_mutex);
    SkippedNoteDetail detail;
    detail.index = noteIndex;
    detail.type = noteType;
    detail.reason = reason;
    detail.missingFields = missingFields;
    detail.presentFields = presentFields;
    m_skippedNotes.append(detail);
}

void DiagnosticCollector::recordLoadMetrics(const QString &filePath,
                                            qint64 duration,
                                            int totalNotes,
                                            int loadedNotes,
                                            int skippedNotes)
{
    QMutexLocker locker(&m_mutex);
    LoadMetrics metrics;
    metrics.filePath = filePath;
    metrics.duration = duration;
    metrics.totalNotes = totalNotes;
    metrics.loadedNotes = loadedNotes;
    metrics.skippedNotes = skippedNotes;
    m_loadMetrics.append(metrics);
}

void DiagnosticCollector::recordSaveMetrics(const QString &filePath,
                                            qint64 duration,
                                            int notesCount)
{
    QMutexLocker locker(&m_mutex);
    SaveMetrics metrics;
    metrics.filePath = filePath;
    metrics.duration = duration;
    metrics.notesCount = notesCount;
    m_saveMetrics.append(metrics);
}

void DiagnosticCollector::recordRenderMetrics(qint64 frameTimeMs, int notesRenderedCount)
{
    QMutexLocker locker(&m_mutex);
    RenderMetricsData metrics;
    metrics.frameTimeMs = frameTimeMs;
    metrics.notesRenderedCount = notesRenderedCount;
    m_renderMetrics.append(metrics);
}

DiagnosticCollector::DiagnosticReport DiagnosticCollector::generateReport() const
{
    QMutexLocker locker(&m_mutex);
    DiagnosticReport report;

    // 统计跳过的notes
    for (const auto &detail : m_skippedNotes)
    {
        report.skippedNotesSummary.totalSkipped++;
        report.skippedNotesSummary.byReason[detail.reason]++;
        report.skippedNotesSummary.byType[detail.type]++;

        for (const auto &field : detail.missingFields)
        {
            report.skippedNotesSummary.byMissingField[field]++;
        }
    }

    // 聚合加载指标
    if (!m_loadMetrics.empty())
    {
        report.performanceMetrics.lastLoadDuration = m_loadMetrics.last().duration;
        report.lastLoadedFilePath = m_loadMetrics.last().filePath;

        report.performanceMetrics.loadCount = m_loadMetrics.size();
        for (const auto &metrics : m_loadMetrics)
        {
            report.performanceMetrics.totalLoadDuration += metrics.duration;
        }
    }

    // 聚合保存指标
    if (!m_saveMetrics.empty())
    {
        report.performanceMetrics.lastSaveDuration = m_saveMetrics.last().duration;
        report.lastSavedFilePath = m_saveMetrics.last().filePath;

        report.performanceMetrics.saveCount = m_saveMetrics.size();
        for (const auto &metrics : m_saveMetrics)
        {
            report.performanceMetrics.totalSaveDuration += metrics.duration;
        }
    }

    // 聚合渲染指标
    if (!m_renderMetrics.empty())
    {
        report.performanceMetrics.totalFrames = m_renderMetrics.size();
        for (const auto &metrics : m_renderMetrics)
        {
            report.performanceMetrics.totalRenderTime += metrics.frameTimeMs;
            if (metrics.frameTimeMs > report.performanceMetrics.maxFrameTime)
            {
                report.performanceMetrics.maxFrameTime = metrics.frameTimeMs;
            }
        }
        report.performanceMetrics.avgFrameTime = static_cast<double>(report.performanceMetrics.totalRenderTime) / report.performanceMetrics.totalFrames;
    }

    return report;
}

void DiagnosticCollector::clear()
{
    QMutexLocker locker(&m_mutex);
    m_skippedNotes.clear();
    m_loadMetrics.clear();
    m_saveMetrics.clear();
    m_renderMetrics.clear();
}

QVector<DiagnosticCollector::SkippedNoteDetail> DiagnosticCollector::getSkippedNoteDetails() const
{
    QMutexLocker locker(&m_mutex);
    return m_skippedNotes;
}

QJsonObject DiagnosticCollector::DiagnosticReport::toJsonObject() const
{
    QJsonObject root;

    // 跳过notes统计
    QJsonObject skippedObj;
    skippedObj["total"] = skippedNotesSummary.totalSkipped;

    QJsonObject byReasonObj;
    for (auto it = skippedNotesSummary.byReason.constBegin(); it != skippedNotesSummary.byReason.constEnd(); ++it)
    {
        byReasonObj[it.key()] = it.value();
    }
    skippedObj["by_reason"] = byReasonObj;

    QJsonObject byTypeObj;
    for (auto it = skippedNotesSummary.byType.constBegin(); it != skippedNotesSummary.byType.constEnd(); ++it)
    {
        byTypeObj[QString::number(it.key())] = it.value();
    }
    skippedObj["by_type"] = byTypeObj;

    QJsonObject byFieldObj;
    for (auto it = skippedNotesSummary.byMissingField.constBegin(); it != skippedNotesSummary.byMissingField.constEnd(); ++it)
    {
        byFieldObj[it.key()] = it.value();
    }
    skippedObj["by_missing_field"] = byFieldObj;

    root["skipped_notes"] = skippedObj;

    // 性能指标
    QJsonObject perfObj;
    QJsonObject loadObj;
    loadObj["last_duration_ms"] = static_cast<qint64>(performanceMetrics.lastLoadDuration);
    loadObj["total_duration_ms"] = static_cast<qint64>(performanceMetrics.totalLoadDuration);
    loadObj["count"] = performanceMetrics.loadCount;
    perfObj["load"] = loadObj;

    QJsonObject saveObj;
    saveObj["last_duration_ms"] = static_cast<qint64>(performanceMetrics.lastSaveDuration);
    saveObj["total_duration_ms"] = static_cast<qint64>(performanceMetrics.totalSaveDuration);
    saveObj["count"] = performanceMetrics.saveCount;
    perfObj["save"] = saveObj;

    QJsonObject renderObj;
    renderObj["max_frame_time_ms"] = static_cast<qint64>(performanceMetrics.maxFrameTime);
    renderObj["avg_frame_time_ms"] = performanceMetrics.avgFrameTime;
    renderObj["total_time_ms"] = static_cast<qint64>(performanceMetrics.totalRenderTime);
    renderObj["frame_count"] = performanceMetrics.totalFrames;
    perfObj["render"] = renderObj;

    root["performance_metrics"] = perfObj;

    // 文件信息
    root["last_loaded_file"] = lastLoadedFilePath;
    root["last_saved_file"] = lastSavedFilePath;

    return root;
}

QString DiagnosticCollector::DiagnosticReport::toFormattedString() const
{
    QString result;
    result += "========== Diagnostic Report ==========\n";
    result += QString("Skipped Notes: %1\n").arg(skippedNotesSummary.totalSkipped);

    if (!skippedNotesSummary.byReason.empty())
    {
        result += "  By Reason:\n";
        for (auto it = skippedNotesSummary.byReason.constBegin(); it != skippedNotesSummary.byReason.constEnd(); ++it)
        {
            result += QString("    %1: %2\n").arg(it.key(), QString::number(it.value()));
        }
    }

    if (!skippedNotesSummary.byMissingField.empty())
    {
        result += "  Missing Fields:\n";
        for (auto it = skippedNotesSummary.byMissingField.constBegin(); it != skippedNotesSummary.byMissingField.constEnd(); ++it)
        {
            result += QString("    %1: %2 notes\n").arg(it.key(), QString::number(it.value()));
        }
    }

    result += "\nPerformance Metrics:\n";
    result += QString("  Load: count=%1, avg=%2ms\n")
                  .arg(performanceMetrics.loadCount)
                  .arg(performanceMetrics.loadCount > 0 ? performanceMetrics.totalLoadDuration / performanceMetrics.loadCount : 0);

    result += QString("  Save: count=%1, avg=%2ms\n")
                  .arg(performanceMetrics.saveCount)
                  .arg(performanceMetrics.saveCount > 0 ? performanceMetrics.totalSaveDuration / performanceMetrics.saveCount : 0);

    if (performanceMetrics.totalFrames > 0)
    {
        result += QString("  Render: frames=%1, avg=%2ms, max=%3ms\n")
                      .arg(performanceMetrics.totalFrames)
                      .arg(performanceMetrics.avgFrameTime, 0, 'f', 2)
                      .arg(performanceMetrics.maxFrameTime);
    }

    result += "=======================================\n";
    return result;
}

QJsonDocument DiagnosticCollector::toJsonDocument() const
{
    QJsonObject root;
    root["report"] = generateReport().toJsonObject();

    // 添加详细跳过notes列表
    QJsonArray skippedDetailsArray;
    QMutexLocker locker(&m_mutex);
    for (const auto &detail : m_skippedNotes)
    {
        QJsonObject detailObj;
        detailObj["index"] = detail.index;
        detailObj["type"] = detail.type;
        detailObj["reason"] = detail.reason;

        QJsonArray missingArray;
        for (const auto &field : detail.missingFields)
        {
            missingArray.append(field);
        }
        detailObj["missing_fields"] = missingArray;

        QJsonArray presentArray;
        for (const auto &field : detail.presentFields)
        {
            presentArray.append(field);
        }
        detailObj["present_fields"] = presentArray;

        skippedDetailsArray.append(detailObj);
    }
    root["skipped_notes_details"] = skippedDetailsArray;

    return QJsonDocument(root);
}

bool DiagnosticCollector::exportReport(const QString &filePath,
                                       ReportFormat format,
                                       QString *errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();

    const QByteArray data = format == ReportFormat::Json
                                ? toJsonDocument().toJson(QJsonDocument::Indented)
                                : generateReport().toFormattedString().toUtf8();
    return FileUtils::writeFileAtomically(filePath, data, errorMessage);
}
