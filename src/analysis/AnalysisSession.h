#pragma once
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <QJsonArray>
#include "audio/AutoTiming2Bridge.h"
#include "audio/BpmDetector.h"
#include <functional>
#include "AnalysisTracks.h"
namespace analysis
{
// Source and request identity are separate from chart revision: editing a Note
// does not invalidate audio evidence, but it does invalidate an apply proposal.
struct AnalysisRequest
{
    quint64 id = 0, configRevision = 0;
    QString sourceIdentity;
    double startMs = 0, durationMs = 0;
    QJsonObject config;
};
struct AnalysisCompletion
{
    AnalysisRequest request;
    BpmDetector::DetectionResult result;
    QJsonObject diagnostics;
    QString error;
    bool discarded = false;
};
class AnalysisSession
{
public:
    using Completion = std::function<void(AnalysisCompletion)>;
    using Runner = std::function<void(QObject *, const QString &, double, double, const AutoTiming2Options &,
                                      std::shared_ptr<std::atomic<bool>>, BpmDetector::AsyncAnalysisCallback)>;
    ~AnalysisSession();
    AnalysisSession() = default;
    AnalysisSession(const AnalysisSession &) = delete;
    AnalysisSession &operator=(const AnalysisSession &) = delete;
    bool dispatch(QObject *, const QString &path, const AnalysisRequest &, Completion, Runner runner = {});
    bool busy() const
    {
        return m_busy;
    }
    quint64 configRevision = 0;
    AnalysisRequest begin(const QString &source, double start, double duration, const QJsonObject &config)
    {
        return {++m_next, configRevision, source, start, duration, config};
    }
    bool isCurrent(const AnalysisRequest &r, const QString &source) const
    {
        return r.id == m_next && r.configRevision == configRevision && r.sourceIdentity == source;
    }
    void invalidateConfig()
    {
        ++configRevision;
        if (m_cancel)
            *m_cancel = true;
    }
    void discard()
    {
        ++m_next;
        if (m_cancel)
            *m_cancel = true;
    }
    void remember(QJsonObject result)
    {
        history.append(std::move(result));
        if (history.size() > 8)
            history.removeFirst();
    }
    void clearSource()
    {
        discard();
        history.clear();
    }
    QVector<QJsonObject> history;

private:
    quint64 m_next = 0;
    bool m_busy = false;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};
QJsonArray anchorFit(const AutoTiming2TempoMap &);
} // namespace analysis
