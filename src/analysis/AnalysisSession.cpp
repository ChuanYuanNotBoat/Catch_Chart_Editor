#include "AnalysisSession.h"
#include "MusicalTimeTransform.h"
#include "AnalysisConfig.h"
#include "AutoTimingDiagnostics.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonArray>
#include <cmath>
namespace analysis
{
AnalysisSession::~AnalysisSession()
{
    *m_alive = false;
    if (m_cancel)
        *m_cancel = true;
}
bool AnalysisSession::dispatch(QObject *context, const QString &path, const AnalysisRequest &request,
                               Completion complete, Runner runner)
{
    if (m_busy || !context || !isCurrent(request, request.sourceIdentity)
        || !validateRequest(request.config, request.startMs, request.durationMs).isEmpty())
        return false;
    m_busy = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_cancel, alive = m_alive;
    const QPointer<QObject> owner(context);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    if (!runner)
        runner = [](QObject *c, const QString &p, double start, double duration, const AutoTiming2Options &options,
                    std::shared_ptr<std::atomic<bool>> flag, BpmDetector::AsyncAnalysisCallback callback) {
            BpmDetector::analyzeFromFileDetailedAsync(c, p, start, duration, std::move(callback), options,
                                                      std::move(flag));
        };
    runner(context, path, request.startMs, request.durationMs, configOptions(request.config), cancel,
           [this, owner, alive, cancel, elapsed, request,
            complete = std::move(complete)](bool success, BpmDetector::DetectionResult result, const QString &error) {
               if (!alive->load())
                   return;
               m_busy = false;
               if (!owner)
                   return;
               AnalysisCompletion out;
               out.request = request;
               out.result = result;
               out.error = error;
               out.discarded = cancel->load() || !isCurrent(request, request.sourceIdentity);
               out.diagnostics = timingDiagnostics(result);
               out.diagnostics["runId"] = QString::number(request.id);
               out.diagnostics["configRevision"] = QString::number(request.configRevision);
               out.diagnostics["configSnapshot"] = request.config;
               out.diagnostics["configHash"] = configHash(request.config);
               out.diagnostics["sourceIdentity"] = request.sourceIdentity;
               out.diagnostics["requestedStartMs"] = request.startMs;
               out.diagnostics["requestedDurationMs"] = request.durationMs;
               out.diagnostics["elapsedMilliseconds"] = double(elapsed->elapsed());
               out.diagnostics["pipelineError"] = error;
               out.diagnostics["preparationSucceeded"] = success;
               out.diagnostics["resultState"] =
                   out.discarded ? "Discarded"
                   : success && error.isEmpty() && result.analysisStatus == BpmDetector::AnalysisStatus::Succeeded
                       ? "Ready"
                       : "Failed";
               out.diagnostics["qtVersion"] = QString::fromLatin1(qVersion());
#ifdef CCE_HOST_SOURCE_VERSION
               out.diagnostics["hostSourceVersion"] = CCE_HOST_SOURCE_VERSION;
#endif
#ifdef QT_DEBUG
               out.diagnostics["buildConfiguration"] = "Debug";
#else
            out.diagnostics["buildConfiguration"]="Release";
#endif
               complete(std::move(out));
           });
    return true;
}
QJsonArray anchorFit(const AutoTiming2TempoMap &map)
{
    QJsonArray rows;
    auto number = [](double v) {
        return std::isfinite(v) ? QJsonValue(v) : QJsonValue();
    };
    for (const auto &a : map.anchors)
    {
        auto predicted = MusicalTimeTransform::audioAtModelBeat(map, a.phaseBeat);
        rows.append(QJsonObject{{"timeSeconds", number(a.timeSeconds)},
                                {"phaseBeat", number(a.phaseBeat)},
                                {"pulseIndex", QString::number(a.pulseIndex)},
                                {"observedBpm", number(a.observedBpm)},
                                {"modelBpm", number(a.modelBpm)},
                                {"confidence", number(a.confidence)},
                                {"phaseConfidence", number(a.phaseConfidence)},
                                {"residualMilliseconds", predicted && std::isfinite(a.timeSeconds)
                                                             ? number(*predicted - a.timeSeconds * 1000)
                                                             : QJsonValue()}});
    }
    return rows;
}
} // namespace analysis
