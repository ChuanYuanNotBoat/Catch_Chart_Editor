#include "ExternalProcessPlugin.h"
#include "utils/Logger.h"
#include "utils/Settings.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QVariantMap>
#include <QProcessEnvironment>
#include <QtConcurrent/QtConcurrentRun>
#include <QMutexLocker>
#include <atomic>
#include <memory>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
    constexpr int kShiftModifierMask = 0x02000000;
    constexpr int kCtrlModifierMask = 0x04000000;

    QJsonObject toJsonObject(const QVariantMap &map)
    {
        return QJsonObject::fromVariantMap(map);
    }

    QString currentLocale()
    {
        QString locale = Settings::instance().language().trimmed();
        if (locale.isEmpty())
            locale = QLocale::system().name();
        return locale;
    }

    QString languageFromLocale(const QString &locale)
    {
        QString language = locale.trimmed();
        const int split = language.indexOf('_');
        if (split > 0)
            language = language.left(split);
        return language;
    }

    void applyUtf8ProcessEnv(QProcessEnvironment *env)
    {
        if (!env)
            return;
        // Force UTF-8 stdio for cross-locale plugin protocol I/O on Windows.
        env->insert("PYTHONUTF8", "1");
        env->insert("PYTHONIOENCODING", "utf-8");
        if (!env->contains("LC_ALL") || env->value("LC_ALL").trimmed().isEmpty())
            env->insert("LC_ALL", "C.UTF-8");
    }

    int requestTimeoutMsForMethod(const QString &method)
    {
        if (method == "runToolAction")
            return 15000;
        if (method == "openAdvancedColorEditor")
            return 10000;
        if (method == "listToolActions")
            return 500;
        if (method == "buildBatchEdit")
            return 8000;
        if (method == "listCanvasOverlays")
            return 80;
        if (method == "handleCanvasInput")
            return 150;
        if (method == "getPanelWorkspaceConfig")
            return 3000;
        return 5000;
    }

    constexpr int kHealthProbeTimeoutMs = 300;
    constexpr int kAsyncStartTimeoutMs = 2000;
    constexpr int kAsyncCancelPollMs = 25;
    constexpr int kMaxConcurrentAsyncRequests = 2;
    constexpr int kMaxStderrPreviewBytes = 8192;
    constexpr int kMaxNoteIdLength = 256;
    constexpr int kMaxSoundPathLength = 1024;
    constexpr int kMaxAbsBeatComponent = 1000000;
    constexpr int kMaxDenominator = 8192;
    constexpr int kMaxAbsOffsetMs = 3600000;
    constexpr int kMaxAbsVolume = 1000;

    QJsonObject initializePayloadFor(const ExternalProcessPlugin::Manifest &manifest)
    {
        QString locale = Settings::instance().language().trimmed();
        if (locale.isEmpty())
            locale = QLocale::system().name();
        return QJsonObject{
            {"plugin_id", manifest.pluginId},
            {"locale", locale},
            {"host_api_version", PluginInterface::kHostApiVersion},
        };
    }

    QJsonObject initializePayloadFor(const ExternalProcessPlugin::Manifest &manifest,
                                     const QString &localeOverride)
    {
        QString locale = localeOverride.trimmed();
        if (locale.isEmpty())
            locale = Settings::instance().language().trimmed();
        if (locale.isEmpty())
            locale = QLocale::system().name();
        return QJsonObject{
            {"plugin_id", manifest.pluginId},
            {"locale", locale},
            {"host_api_version", PluginInterface::kHostApiVersion},
        };
    }

    QStringList resolveProcessArguments(const ExternalProcessPlugin::Manifest &manifest,
                                        QString *outExecutable,
                                        QString *outBaseDir)
    {
        const QFileInfo manifestInfo(manifest.manifestPath);
        const QString baseDir = manifestInfo.absolutePath();
        if (outBaseDir)
            *outBaseDir = baseDir;

        QString executable = manifest.executable.trimmed();
        QFileInfo execInfo(executable);
        const bool looksLikePath = executable.contains('/') || executable.contains('\\')
            || executable.startsWith('.');
        if (looksLikePath && execInfo.isRelative())
            executable = QDir(baseDir).filePath(executable);

        QStringList args;
        args.reserve(manifest.args.size());
        for (const QString &arg : manifest.args)
        {
            const QString trimmed = arg.trimmed();
            const bool argLooksLikePath = trimmed.contains('/') || trimmed.contains('\\')
                || trimmed.startsWith('.');
            QFileInfo argInfo(trimmed);
            if (argLooksLikePath && argInfo.isRelative())
                args.append(QDir(baseDir).filePath(trimmed));
            else
                args.append(trimmed);
        }

        if (outExecutable)
            *outExecutable = executable;
        return args;
    }

    void stopProcess(QProcess *process)
    {
        if (!process || process->state() == QProcess::NotRunning)
            return;
        process->terminate();
        if (!process->waitForFinished(100))
        {
            process->kill();
            process->waitForFinished(200);
        }
    }

    bool writeProcessLine(QProcess *process,
                          const QByteArray &line,
                          QString *errorMessage)
    {
        if (!process || process->state() != QProcess::Running)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("process is not running");
            return false;
        }
        if (line.size() > ExternalProcessPlugin::kMaxRequestPayloadBytes)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("request payload exceeds the host limit");
            return false;
        }
        const qint64 written = process->write(line);
        if (written != line.size())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("failed to write the complete request");
            return false;
        }
        return true;
    }

    bool waitForProcessResponse(QProcess *process,
                                const QString &requestId,
                                int timeoutMs,
                                const std::shared_ptr<std::atomic_bool> &cancelled,
                                QJsonValue *outResult,
                                QByteArray *outStderr,
                                QString *errorMessage)
    {
        if (!process)
            return false;

        QByteArray pending;
        QByteArray stderrPreview;
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + qMax(1, timeoutMs);
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
        {
            if (cancelled && cancelled->load(std::memory_order_relaxed))
            {
                if (errorMessage)
                    *errorMessage = QStringLiteral("request cancelled");
                stopProcess(process);
                return false;
            }

            const QByteArray chunk = process->readAllStandardOutput();
            if (!chunk.isEmpty())
            {
                pending.append(chunk);
                if (pending.size() > ExternalProcessPlugin::kMaxResponsePayloadBytes)
                {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("response payload exceeds the host limit");
                    stopProcess(process);
                    return false;
                }
            }

            const QByteArray stderrChunk = process->readAllStandardError();
            if (!stderrChunk.isEmpty() && stderrPreview.size() < kMaxStderrPreviewBytes)
            {
                stderrPreview.append(stderrChunk.left(kMaxStderrPreviewBytes - stderrPreview.size()));
            }

            int newline = -1;
            while ((newline = pending.indexOf('\n')) >= 0)
            {
                const QByteArray line = pending.left(newline).trimmed();
                pending.remove(0, newline + 1);
                if (line.isEmpty())
                    continue;

                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject())
                    continue;
                const QJsonObject obj = doc.object();
                if (obj.value(QStringLiteral("type")).toString() != QLatin1String("response")
                    || obj.value(QStringLiteral("id")).toString() != requestId)
                    continue;
                if (outResult)
                    *outResult = obj.value(QStringLiteral("result"));
                if (outStderr)
                    *outStderr = stderrPreview;
                return true;
            }

            const qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
            if (remaining <= 0)
                break;
            const int waitMs = qMax(1, qMin(static_cast<int>(remaining), kAsyncCancelPollMs));
            process->waitForReadyRead(waitMs);
        }

        if (outStderr)
            *outStderr = stderrPreview;
        if (cancelled && cancelled->load(std::memory_order_relaxed))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("request cancelled");
        }
        else if (errorMessage)
        {
            *errorMessage = QStringLiteral("request timed out");
        }
        stopProcess(process);
        return false;
    }

    bool runIsolatedProcessRequest(const ExternalProcessPlugin::Manifest &manifest,
                                   const QString &locale,
                                   const QString &method,
                                   const QJsonObject &payload,
                                   int timeoutMs,
                                   const std::shared_ptr<std::atomic_bool> &cancelled,
                                   QJsonValue *outResult,
                                   QString *errorMessage)
    {
        QString executable;
        QString baseDir;
        const QStringList args = resolveProcessArguments(manifest, &executable, &baseDir);
        QProcess process;
        process.setWorkingDirectory(baseDir);
        process.setProgram(executable);
        process.setArguments(args);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        applyUtf8ProcessEnv(&env);
        const QString resolvedLocale = locale.trimmed().isEmpty() ? QLocale::system().name() : locale.trimmed();
        const QString language = languageFromLocale(resolvedLocale);
        if (!resolvedLocale.isEmpty())
            env.insert(QStringLiteral("MALODY_LOCALE"), resolvedLocale);
        if (!language.isEmpty())
            env.insert(QStringLiteral("MALODY_LANGUAGE"), language);
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
                                                   { args->flags |= CREATE_NO_WINDOW; });
#endif

        process.start();
        const qint64 startDeadline = QDateTime::currentMSecsSinceEpoch() + kAsyncStartTimeoutMs;
        while (process.state() != QProcess::Running
               && QDateTime::currentMSecsSinceEpoch() < startDeadline)
        {
            if (cancelled && cancelled->load(std::memory_order_relaxed))
            {
                stopProcess(&process);
                if (errorMessage)
                    *errorMessage = QStringLiteral("request cancelled");
                return false;
            }
            const qint64 remaining = startDeadline - QDateTime::currentMSecsSinceEpoch();
            process.waitForStarted(qMax(1, qMin(static_cast<int>(remaining), kAsyncCancelPollMs)));
        }
        if (process.state() != QProcess::Running)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("process failed to start");
            stopProcess(&process);
            return false;
        }

        const QJsonObject initialize = {
            {QStringLiteral("type"), QStringLiteral("notify")},
            {QStringLiteral("event"), QStringLiteral("initialize")},
            {QStringLiteral("payload"), initializePayloadFor(manifest, resolvedLocale)},
        };
        const QByteArray initializeLine = QJsonDocument(initialize).toJson(QJsonDocument::Compact) + '\n';
        if (!writeProcessLine(&process, initializeLine, errorMessage))
        {
            stopProcess(&process);
            return false;
        }

        const QString requestId = QStringLiteral("async_%1_%2")
                                      .arg(QDateTime::currentMSecsSinceEpoch())
                                      .arg(QRandomGenerator::global()->generate());
        const QJsonObject request = {
            {QStringLiteral("type"), QStringLiteral("request")},
            {QStringLiteral("id"), requestId},
            {QStringLiteral("method"), method},
            {QStringLiteral("payload"), payload},
        };
        const QByteArray requestLine = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (!writeProcessLine(&process, requestLine, errorMessage))
        {
            stopProcess(&process);
            return false;
        }

        QByteArray stderrPreview;
        const bool ok = waitForProcessResponse(&process,
                                               requestId,
                                               timeoutMs,
                                               cancelled,
                                               outResult,
                                               &stderrPreview,
                                               errorMessage);
        if (!stderrPreview.isEmpty())
        {
            Logger::warn(QStringLiteral("Process plugin async request '%1' stderr: %2")
                             .arg(method, QString::fromUtf8(stderrPreview).trimmed()));
        }
        stopProcess(&process);
        return ok;
    }

}

ExternalProcessPlugin::ExternalProcessPlugin(Manifest manifest)
    : m_manifest(std::move(manifest))
{
    QObject::connect(&m_process,
                     &QProcess::readyReadStandardOutput,
                     &m_process,
                     [this]() { handlePersistentAsyncOutput(); });
    QObject::connect(&m_process,
                     &QProcess::finished,
                     &m_process,
                     [this](int, QProcess::ExitStatus) { handlePersistentProcessFinished(); });
}

ExternalProcessPlugin::~ExternalProcessPlugin()
{
    cancelAllPersistentAsyncRequests();
    {
        QMutexLocker locker(&m_asyncJobsMutex);
        for (auto it = m_asyncJobs.begin(); it != m_asyncJobs.end(); ++it)
        {
            if (it->cancelled)
                it->cancelled->store(true, std::memory_order_relaxed);
        }
    }
    QHash<AsyncRequestId, AsyncJob> jobs;
    {
        QMutexLocker locker(&m_asyncJobsMutex);
        jobs = m_asyncJobs;
        m_asyncJobs.clear();
    }
    for (auto it = jobs.begin(); it != jobs.end(); ++it)
    {
        if (it->future.isRunning() || !it->future.isFinished())
            it->future.waitForFinished();
    }
    shutdown();
}

QString ExternalProcessPlugin::pluginId() const
{
    return m_manifest.pluginId;
}

QString ExternalProcessPlugin::displayName() const
{
    return m_manifest.displayName;
}

QString ExternalProcessPlugin::version() const
{
    return m_manifest.version;
}

QString ExternalProcessPlugin::description() const
{
    return m_manifest.description;
}

QString ExternalProcessPlugin::author() const
{
    return m_manifest.author;
}

QString ExternalProcessPlugin::pluginSourcePath() const
{
    return m_manifest.manifestPath;
}

QString ExternalProcessPlugin::localizedDisplayName(const QString &locale) const
{
    return resolveLocalizedValue(m_manifest.localizedDisplayName, locale, m_manifest.displayName);
}

QString ExternalProcessPlugin::localizedDescription(const QString &locale) const
{
    return resolveLocalizedValue(m_manifest.localizedDescription, locale, m_manifest.description);
}

int ExternalProcessPlugin::pluginApiVersion() const
{
    return m_manifest.apiVersion;
}

QStringList ExternalProcessPlugin::capabilities() const
{
    return m_manifest.capabilities;
}

bool ExternalProcessPlugin::initialize(QWidget *mainWindow)
{
    (void)mainWindow;
    if (m_initialized)
        return true;

    if (!ensureProcessRunning())
        return false;

    const QJsonObject payload = initializePayloadFor(m_manifest);
    m_initialized = sendNotification("initialize", payload);
    if (m_initialized)
        m_needsReinitialize = false;
    return m_initialized;
}

void ExternalProcessPlugin::shutdown()
{
    cancelAllPersistentAsyncRequests();
    if (m_initialized)
    {
        sendNotification("shutdown");
        m_initialized = false;
    }

    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.terminate();
        if (!m_process.waitForFinished(1000))
        {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
    m_needsReinitialize = false;
}

void ExternalProcessPlugin::onChartChanged()
{
    sendNotification("onChartChanged");
}

void ExternalProcessPlugin::onChartLoaded(const QString &chartPath)
{
    sendNotification("onChartLoaded", QJsonObject{{"chart_path", chartPath}});
}

void ExternalProcessPlugin::onChartSaved(const QString &chartPath)
{
    sendNotification("onChartSaved", QJsonObject{{"chart_path", chartPath}});
}

void ExternalProcessPlugin::onHostUndo(const QString &actionText)
{
    sendNotification("onHostUndo", QJsonObject{{"action_text", actionText}});
}

void ExternalProcessPlugin::onHostRedo(const QString &actionText)
{
    sendNotification("onHostRedo", QJsonObject{{"action_text", actionText}});
}

void ExternalProcessPlugin::onHostDiscardChanges(const QString &reasonText)
{
    sendNotification("onHostDiscardChanges", QJsonObject{{"reason_text", reasonText}});
}

bool ExternalProcessPlugin::openAdvancedColorEditor(const QVariantMap &context)
{
    if (!hasCapability(kCapabilityAdvancedColorEditor))
        return false;
    return requestBool("openAdvancedColorEditor", toJsonObject(context), false);
}

QList<PluginInterface::ToolAction> ExternalProcessPlugin::toolActions() const
{
    if (m_toolActionsCached)
        return m_cachedToolActions;

    // If process is not even running yet (async init still in progress),
    // return empty immediately without blocking the UI thread.
    if (m_process.state() != QProcess::Running)
    {
        return {};
    }

    QJsonValue result;
    if (!requestJson("listToolActions", QJsonObject(), &result))
    {
        // Don't cache on failure — process may not be ready yet.
        // Next call will retry without blocking for the full timeout.
        return {};
    }
    if (!result.isArray())
    {
        m_toolActionsCached = true;
        return {};
    }

    QList<ToolAction> actions;
    const QJsonArray arr = result.toArray();
    for (const QJsonValue &v : arr)
    {
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();
        ToolAction action;
        action.actionId = obj.value("action_id").toString().trimmed();
        action.title = obj.value("title").toString().trimmed();
        action.description = obj.value("description").toString().trimmed();
        action.confirmMessage = obj.value("confirm_message").toString().trimmed();
        action.hostAction = obj.value("host_action").toString().trimmed().toLower();
        action.placement = obj.value("placement").toString().trimmed();
        action.scopeSelector = obj.value("scope_selector").toString().trimmed().toLower();
        if (action.placement.isEmpty())
            action.placement = PluginInterface::kPlacementToolsMenu;
        action.requiresUndoSnapshot = obj.value("requires_undo_snapshot").toBool(true);
        action.checkable = obj.value("checkable").toBool(false);
        action.checked = obj.value("checked").toBool(false);
        action.syncPluginToolModeWithChecked = obj.value("sync_plugin_tool_mode_with_checked").toBool(false);
        if (action.actionId.isEmpty() || action.title.isEmpty())
            continue;
        actions.append(action);
    }

    m_cachedToolActions = actions;
    m_toolActionsCached = true;
    return actions;
}

void ExternalProcessPlugin::invalidateToolActionsCache()
{
    m_toolActionsCached = false;
    m_cachedToolActions.clear();
}

bool ExternalProcessPlugin::runToolAction(const QString &actionId, const QVariantMap &context)
{
    if (actionId.isEmpty())
        return false;

    // Stateful interaction plugins must execute tool actions in the persistent
    // session process; one-shot child process would lose in-memory state.
    if (!requiresPersistentSession())
    {
        // Prefer one-shot execution for stateless/script-like actions to avoid
        // request/response channel stalls on long-running file operations.
        if (runToolActionOneShot(actionId, context))
            return true;

        Logger::warn(QString("Process plugin '%1' runToolAction(%2) one-shot path failed, trying protocol fallback.")
                         .arg(m_manifest.pluginId)
                         .arg(actionId));
    }

    QJsonObject payload{
        {"action_id", actionId},
        {"context", toJsonObject(context)},
    };
    return requestBool("runToolAction", payload, false);
}

ExternalProcessPlugin::AsyncRequestId ExternalProcessPlugin::runToolActionAsync(
    const QString &actionId,
    const QVariantMap &context,
    QObject *callbackContext,
    AsyncToolActionCallback callback)
{
    if (actionId.trimmed().isEmpty() || !callbackContext || !callback)
        return 0;

    const QString locale = context.value(QStringLiteral("locale")).toString();
    const QJsonObject payload{
        {QStringLiteral("action_id"), actionId},
        {QStringLiteral("context"), toJsonObject(context)},
    };
    AsyncJsonCompletion completion = [callback = std::move(callback)](
                                         bool ok, const QJsonValue &result) {
        const bool success = ok && result.isBool() && result.toBool(false);
        callback(success);
    };
    if (requiresPersistentSession())
    {
        return startPersistentAsyncJsonRequest(QStringLiteral("runToolAction"),
                                               payload,
                                               callbackContext,
                                               std::move(completion));
    }
    return startAsyncJsonRequest(QStringLiteral("runToolAction"),
                                 payload,
                                 locale,
                                 callbackContext,
                                 std::move(completion));
}

ExternalProcessPlugin::AsyncRequestId ExternalProcessPlugin::buildToolActionBatchEditAsync(
    const QString &actionId,
    const QVariantMap &context,
    QObject *callbackContext,
    AsyncBatchEditCallback callback)
{
    if (actionId.trimmed().isEmpty() || !callbackContext || !callback)
        return 0;

    const QString locale = context.value(QStringLiteral("locale")).toString();
    const QJsonObject payload{
        {QStringLiteral("action_id"), actionId},
        {QStringLiteral("context"), toJsonObject(context)},
    };
    AsyncJsonCompletion completion = [callback = std::move(callback)](
                                         bool ok, const QJsonValue &result) {
        BatchEdit edit;
        const bool success = ok && result.isObject()
            && ExternalProcessPlugin::parseBatchEditJson(result.toObject(), &edit);
        callback(success, std::move(edit));
    };
    if (requiresPersistentSession())
    {
        return startPersistentAsyncJsonRequest(QStringLiteral("buildBatchEdit"),
                                               payload,
                                               callbackContext,
                                               std::move(completion));
    }
    return startAsyncJsonRequest(QStringLiteral("buildBatchEdit"),
                                 payload,
                                 locale,
                                 callbackContext,
                                 std::move(completion));
}

bool ExternalProcessPlugin::requiresPersistentSession() const
{
    return hasCapability(kCapabilityCanvasInteraction)
        || hasCapability(kCapabilityPanelWorkspace)
        || hasCapability(kCapabilityContextualToolActions);
}

ExternalProcessPlugin::AsyncRequestId ExternalProcessPlugin::startAsyncJsonRequest(
    const QString &method,
    const QJsonObject &payload,
    const QString &locale,
    QObject *callbackContext,
    AsyncJsonCompletion callback)
{
    if (!callbackContext || !callback)
        return 0;

    const QByteArray serializedPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (serializedPayload.size() > kMaxRequestPayloadBytes)
    {
        Logger::warn(QString("Process plugin '%1' async request '%2' rejected: payload is %3 bytes (limit %4).")
                         .arg(m_manifest.pluginId)
                         .arg(method)
                         .arg(serializedPayload.size())
                         .arg(kMaxRequestPayloadBytes));
        return 0;
    }

    pruneCompletedAsyncJobs();
    {
        QMutexLocker locker(&m_asyncJobsMutex);
        if (m_asyncJobs.size() >= kMaxConcurrentAsyncRequests)
        {
            Logger::warn(QString("Process plugin '%1' async request '%2' rejected: too many requests in flight.")
                             .arg(m_manifest.pluginId)
                             .arg(method));
            return 0;
        }
    }

    AsyncRequestId requestId = m_nextAsyncRequestId++;
    if (requestId == 0)
        requestId = m_nextAsyncRequestId++;
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    const Manifest manifest = m_manifest;
    const QPointer<QObject> target(callbackContext);
    const int timeoutMs = requestTimeoutMsForMethod(method);
    const AsyncJsonCompletion completion = std::move(callback);

    QFuture<void> future = QtConcurrent::run(
        [manifest, locale, method, payload, timeoutMs, cancelled, target, completion]() mutable {
            QJsonValue result;
            QString errorMessage;
            const bool ok = runIsolatedProcessRequest(manifest,
                                                      locale,
                                                      method,
                                                      payload,
                                                      timeoutMs,
                                                      cancelled,
                                                      &result,
                                                      &errorMessage);
            if (!ok)
            {
                Logger::warn(QString("Process plugin async request '%1' failed: %2")
                                 .arg(method, errorMessage));
            }

            if (!target)
                return;
            QMetaObject::invokeMethod(
                target.data(),
                [completion, ok, result]() { completion(ok, result); },
                Qt::QueuedConnection);
        });

    {
        QMutexLocker locker(&m_asyncJobsMutex);
        m_asyncJobs.insert(requestId, AsyncJob{cancelled, std::move(future)});
    }
    return requestId;
}

ExternalProcessPlugin::AsyncRequestId ExternalProcessPlugin::startPersistentAsyncJsonRequest(
    const QString &method,
    const QJsonObject &payload,
    QObject *callbackContext,
    AsyncJsonCompletion callback)
{
    if (!callbackContext || !callback)
        return 0;

    const QByteArray serializedPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (serializedPayload.size() > kMaxRequestPayloadBytes)
    {
        Logger::warn(QString("Process plugin '%1' persistent async request '%2' rejected: "
                             "payload is %3 bytes (limit %4).")
                         .arg(m_manifest.pluginId)
                         .arg(method)
                         .arg(serializedPayload.size())
                         .arg(kMaxRequestPayloadBytes));
        return 0;
    }

    if (m_persistentAsyncRequests.size() >= kMaxConcurrentAsyncRequests)
    {
        Logger::warn(QString("Process plugin '%1' persistent async request '%2' rejected: "
                             "too many requests in flight.")
                         .arg(m_manifest.pluginId)
                         .arg(method));
        return 0;
    }

    AsyncRequestId requestId = m_nextAsyncRequestId++;
    if (requestId == 0)
        requestId = m_nextAsyncRequestId++;
    PersistentAsyncRequest request;
    request.requestId = requestId;
    request.protocolRequestId = QStringLiteral("persistent_async_%1_%2")
                                    .arg(requestId)
                                    .arg(QRandomGenerator::global()->generate());
    request.method = method;
    request.payload = payload;
    request.callbackContext = callbackContext;
    request.completion = std::move(callback);
    m_persistentAsyncRequests.append(std::move(request));
    startNextPersistentAsyncRequest();
    return requestId;
}

void ExternalProcessPlugin::startNextPersistentAsyncRequest()
{
    if (m_activePersistentRequestId != 0 || m_persistentAsyncRequests.isEmpty())
        return;
    if (!ensureProcessRunning())
    {
        finishPersistentAsyncRequest(m_persistentAsyncRequests.first().requestId, false);
        return;
    }

    const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_processStartEpochMs;
    if (age < kPostStartCooldownMs)
    {
        if (!m_persistentStartScheduled)
        {
            const qint64 remaining = kPostStartCooldownMs - age;
            const int delayMs = static_cast<int>(qBound<qint64>(qint64(1),
                                                                remaining,
                                                                qint64(kPostStartCooldownMs)));
            m_persistentStartScheduled = true;
            QTimer::singleShot(delayMs, Qt::PreciseTimer, &m_process, [this]() {
                m_persistentStartScheduled = false;
                startNextPersistentAsyncRequest();
            });
        }
        return;
    }

    const PersistentAsyncRequest &request = m_persistentAsyncRequests.first();
    m_activePersistentRequestId = request.requestId;
    m_persistentResponseBuffer.clear();
    const QJsonObject message{
        {QStringLiteral("type"), QStringLiteral("request")},
        {QStringLiteral("id"), request.protocolRequestId},
        {QStringLiteral("method"), request.method},
        {QStringLiteral("payload"), request.payload},
    };
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (!writeLine(line))
    {
        const AsyncRequestId failedId = request.requestId;
        finishPersistentAsyncRequest(failedId, false);
        forceRestartProcess(QStringLiteral("persistent async request write failed"));
        return;
    }

    const AsyncRequestId activeId = request.requestId;
    const int timeoutMs = requestTimeoutMsForMethod(request.method);
    QTimer::singleShot(timeoutMs, Qt::PreciseTimer, &m_process, [this, activeId]() {
        if (m_activePersistentRequestId != activeId)
            return;
        const QString method = m_persistentAsyncRequests.isEmpty()
            ? QString()
            : m_persistentAsyncRequests.first().method;
        const QByteArray stderrMessage = m_process.readAllStandardError().left(kMaxStderrPreviewBytes);
        Logger::warn(QString("Process plugin '%1' persistent async request '%2' timed out%3.")
                         .arg(m_manifest.pluginId)
                         .arg(method)
                         .arg(stderrMessage.isEmpty()
                                  ? QString()
                                  : QStringLiteral(", stderr: %1")
                                        .arg(QString::fromUtf8(stderrMessage).trimmed())));
        finishPersistentAsyncRequest(activeId, false);
        forceRestartProcess(QStringLiteral("persistent async request timed out"));
    });
}

void ExternalProcessPlugin::handlePersistentAsyncOutput()
{
    if (m_activePersistentRequestId == 0 || m_persistentAsyncRequests.isEmpty())
        return;

    m_persistentResponseBuffer.append(m_process.readAllStandardOutput());
    if (m_persistentResponseBuffer.size() > kMaxResponsePayloadBytes)
    {
        const AsyncRequestId failedId = m_activePersistentRequestId;
        Logger::warn(QString("Process plugin '%1' persistent async response exceeded %2 bytes.")
                         .arg(m_manifest.pluginId)
                         .arg(kMaxResponsePayloadBytes));
        finishPersistentAsyncRequest(failedId, false);
        forceRestartProcess(QStringLiteral("persistent async response exceeded host limit"));
        return;
    }

    int newline = -1;
    while ((newline = m_persistentResponseBuffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = m_persistentResponseBuffer.left(newline).trimmed();
        m_persistentResponseBuffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            continue;

        const QJsonObject response = document.object();
        const PersistentAsyncRequest &request = m_persistentAsyncRequests.first();
        if (response.value(QStringLiteral("type")).toString() != QLatin1String("response")
            || response.value(QStringLiteral("id")).toString() != request.protocolRequestId)
        {
            continue;
        }

        finishPersistentAsyncRequest(request.requestId,
                                     true,
                                     response.value(QStringLiteral("result")));
        return;
    }
}

void ExternalProcessPlugin::handlePersistentProcessFinished()
{
    if (m_initialized)
        m_needsReinitialize = true;
    if (m_activePersistentRequestId == 0)
        return;

    handlePersistentAsyncOutput();
    if (m_activePersistentRequestId == 0)
        return;

    const AsyncRequestId failedId = m_activePersistentRequestId;
    const QString method = m_persistentAsyncRequests.isEmpty()
        ? QString()
        : m_persistentAsyncRequests.first().method;
    const QByteArray stderrMessage = m_process.readAllStandardError().left(kMaxStderrPreviewBytes);
    Logger::warn(QString("Process plugin '%1' exited during persistent async request '%2'%3.")
                     .arg(m_manifest.pluginId)
                     .arg(method)
                     .arg(stderrMessage.isEmpty()
                              ? QString()
                              : QStringLiteral(", stderr: %1")
                                    .arg(QString::fromUtf8(stderrMessage).trimmed())));
    finishPersistentAsyncRequest(failedId, false);
}

void ExternalProcessPlugin::finishPersistentAsyncRequest(AsyncRequestId requestId,
                                                         bool ok,
                                                         const QJsonValue &result)
{
    int requestIndex = -1;
    for (int i = 0; i < m_persistentAsyncRequests.size(); ++i)
    {
        if (m_persistentAsyncRequests.at(i).requestId == requestId)
        {
            requestIndex = i;
            break;
        }
    }
    if (requestIndex < 0)
        return;

    const bool wasActive = m_activePersistentRequestId == requestId;
    const bool wasFront = requestIndex == 0;
    PersistentAsyncRequest request = std::move(m_persistentAsyncRequests[requestIndex]);
    m_persistentAsyncRequests.removeAt(requestIndex);
    if (wasActive)
    {
        m_activePersistentRequestId = 0;
        m_persistentResponseBuffer.clear();
    }

    if (request.callbackContext && request.completion)
    {
        const QPointer<QObject> target = request.callbackContext;
        const AsyncJsonCompletion completion = std::move(request.completion);
        QMetaObject::invokeMethod(target.data(),
                                  [completion, ok, result]() { completion(ok, result); },
                                  Qt::QueuedConnection);
    }

    if ((wasActive || wasFront) && !m_persistentAsyncRequests.isEmpty())
    {
        QTimer::singleShot(0, &m_process, [this]() { startNextPersistentAsyncRequest(); });
    }
}

void ExternalProcessPlugin::cancelAllPersistentAsyncRequests()
{
    QList<PersistentAsyncRequest> requests;
    requests.swap(m_persistentAsyncRequests);
    m_activePersistentRequestId = 0;
    m_persistentResponseBuffer.clear();

    for (PersistentAsyncRequest &request : requests)
    {
        if (!request.callbackContext || !request.completion)
            continue;
        const QPointer<QObject> target = request.callbackContext;
        const AsyncJsonCompletion completion = std::move(request.completion);
        QMetaObject::invokeMethod(target.data(),
                                  [completion]() { completion(false, QJsonValue()); },
                                  Qt::QueuedConnection);
    }
}

void ExternalProcessPlugin::cancelAsyncRequest(AsyncRequestId requestId)
{
    if (requestId == 0)
        return;

    for (int i = 0; i < m_persistentAsyncRequests.size(); ++i)
    {
        if (m_persistentAsyncRequests.at(i).requestId != requestId)
            continue;
        const bool wasActive = m_activePersistentRequestId == requestId;
        finishPersistentAsyncRequest(requestId, false);
        if (wasActive)
            forceRestartProcess(QStringLiteral("persistent async request cancelled"));
        return;
    }

    QMutexLocker locker(&m_asyncJobsMutex);
    const auto it = m_asyncJobs.constFind(requestId);
    if (it != m_asyncJobs.constEnd() && it->cancelled)
        it->cancelled->store(true, std::memory_order_relaxed);
}

void ExternalProcessPlugin::pruneCompletedAsyncJobs() const
{
    QMutexLocker locker(&m_asyncJobsMutex);
    for (auto it = m_asyncJobs.begin(); it != m_asyncJobs.end();)
    {
        if (it->future.isFinished())
            it = m_asyncJobs.erase(it);
        else
            ++it;
    }
}

bool ExternalProcessPlugin::parseNoteJson(const QJsonObject &obj, Note *outNote)
{
    if (!outNote || !obj.contains("beat") || !obj.value("beat").isArray())
        return false;
    const QJsonArray beat = obj.value("beat").toArray();
    if (beat.size() != 3)
        return false;

    const int beatNum = beat[0].toInt();
    const int num = beat[1].toInt();
    const int den = beat[2].toInt(1);
    if (qAbs(beatNum) > kMaxAbsBeatComponent || qAbs(num) > kMaxAbsBeatComponent)
        return false;
    if (den <= 0 || den > kMaxDenominator)
        return false;
    const int typeInt = obj.value("type").toInt(0);
    const NoteType type = Note::intToNoteType(typeInt);

    Note note;
    note.beatNum = beatNum;
    note.numerator = num;
    note.denominator = den;
    note.type = type;
    note.isRain = (type == NoteType::RAIN);
    note.id = obj.value("id").toString();
    if (note.id.size() > kMaxNoteIdLength)
        return false;

    note.x = obj.value("x").toInt(256);
    note.sound = obj.value("sound").toString();
    note.vol = obj.value("vol").toInt(note.vol);
    note.offset = obj.value("offset").toInt(note.offset);
    if (note.sound.size() > kMaxSoundPathLength)
        return false;
    if (qAbs(note.offset) > kMaxAbsOffsetMs)
        return false;
    if (qAbs(note.vol) > kMaxAbsVolume)
        return false;
    if (type != NoteType::SOUND)
        note.x = qBound(0, note.x, 512);

    if (type == NoteType::RAIN && obj.contains("endbeat") && obj.value("endbeat").isArray())
    {
        const QJsonArray endBeat = obj.value("endbeat").toArray();
        if (endBeat.size() == 3)
        {
            note.endBeatNum = endBeat[0].toInt();
            note.endNumerator = endBeat[1].toInt();
            note.endDenominator = endBeat[2].toInt(1);
            if (qAbs(note.endBeatNum) > kMaxAbsBeatComponent || qAbs(note.endNumerator) > kMaxAbsBeatComponent)
                return false;
            if (note.endDenominator <= 0 || note.endDenominator > kMaxDenominator)
                return false;
        }
    }
    else
    {
        note.endBeatNum = note.beatNum;
        note.endNumerator = note.numerator;
        note.endDenominator = note.denominator;
    }

    *outNote = note;
    return true;
}

bool ExternalProcessPlugin::buildToolActionBatchEdit(const QString &actionId,
                                                     const QVariantMap &context,
                                                     BatchEdit *outEdit)
{
    if (!outEdit)
        return false;
    *outEdit = BatchEdit{};
    QJsonValue result;
    QJsonObject payload{
        {"action_id", actionId},
        {"context", toJsonObject(context)},
    };
    if (!requestJson("buildBatchEdit", payload, &result))
        return false;
    if (!result.isObject())
        return false;

    return parseBatchEditJson(result.toObject(), outEdit);
}

QList<PluginInterface::CanvasOverlayItem> ExternalProcessPlugin::canvasOverlays(const QVariantMap &context) const
{
    Q_UNUSED(context);
    if (m_canvasOverlaysCached)
        return m_cachedCanvasOverlays;

    QJsonValue result;
    if (!requestJson("listCanvasOverlays", QJsonObject(), &result))
    {
        return {};
    }
    if (!result.isArray())
    {
        m_canvasOverlaysCached = true;
        return {};
    }

    m_cachedCanvasOverlays = parseOverlayItems(result.toArray());
    m_canvasOverlaysCached = true;
    return m_cachedCanvasOverlays;
}

void ExternalProcessPlugin::invalidateCanvasOverlayCache()
{
    m_canvasOverlaysCached = false;
    m_cachedCanvasOverlays.clear();
}

bool ExternalProcessPlugin::handleCanvasInput(const QVariantMap &context,
                                              const CanvasInputEvent &event,
                                              CanvasInputResult *outResult)
{
    if (outResult)
        *outResult = CanvasInputResult{};
    if (!outResult || !hasCapability(kCapabilityCanvasInteraction))
        return false;

    const bool shiftDown = event.shiftDown || (event.modifiers & kShiftModifierMask) != 0;
    const bool ctrlDown = event.ctrlDown || (event.modifiers & kCtrlModifierMask) != 0;
    QJsonObject eventObj{
        {"type", event.type},
        {"x", event.x},
        {"y", event.y},
        {"button", event.button},
        {"buttons", event.buttons},
        {"modifiers", event.modifiers},
        {"shift_down", shiftDown},
        {"ctrl_down", ctrlDown},
        {"wheel_delta", event.wheelDelta},
        {"key", event.key},
        {"timestamp_ms", static_cast<qint64>(event.timestampMs)},
    };
    QJsonObject payload{
        {"context", toJsonObject(context)},
        {"event", eventObj},
    };

    QJsonValue result;
    if (!requestJson("handleCanvasInput", payload, &result) || !result.isObject())
        return false;

    const QJsonObject obj = result.toObject();
    outResult->consumed = obj.value("consumed").toBool(false);
    outResult->cursor = obj.value("cursor").toString();
    outResult->statusText = obj.value("status_text").toString();
    outResult->requestUndoCheckpoint = obj.value("request_undo_checkpoint").toBool(false);
    outResult->undoCheckpointLabel = obj.value("undo_checkpoint_label").toString().trimmed();
    // After handling canvas input (drag/move/click), invalidate the
    // internal overlay cache so the next timer-driven listCanvasOverlays
    // query fetches fresh overlay data.  This avoids duplicating the
    // expensive _build_overlay call inside handleCanvasInput.
    invalidateCanvasOverlayCache();
    if (obj.value("overlay").isArray())
    {
        const QJsonArray arr = obj.value("overlay").toArray();
        // Only update the cache when the plugin actually returned overlay
        // data; an empty array means "no overlay in this response, fetch
        // via listCanvasOverlays on the next timer tick".  Without this
        // guard the empty response would overwrite valid cached overlays.
        if (!arr.isEmpty())
        {
            outResult->overlay = parseOverlayItems(arr);
            m_cachedCanvasOverlays = outResult->overlay;
            m_canvasOverlaysCached = true;
        }
    }
    if (obj.value("preview_batch_edit").isObject())
        parseBatchEditJson(obj.value("preview_batch_edit").toObject(), &outResult->previewEdit);
    return true;
}

QVariantMap ExternalProcessPlugin::panelWorkspaceConfig(const QVariantMap &context) const
{
    if (!hasCapability(kCapabilityPanelWorkspace))
        return {};

    QJsonValue result;
    if (!requestJson("getPanelWorkspaceConfig", toJsonObject(context), &result))
        return {};
    if (!result.isObject())
        return {};
    return result.toObject().toVariantMap();
}

bool ExternalProcessPlugin::sendNotification(const QString &event, const QJsonObject &payload)
{
    if (!ensureProcessRunning())
        return false;

    QJsonObject msg{
        {"type", "notify"},
        {"event", event},
    };
    if (!payload.isEmpty())
        msg.insert("payload", payload);

    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    return writeLine(line);
}

bool ExternalProcessPlugin::requestBool(const QString &method, const QJsonObject &payload, bool defaultValue) const
{
    QJsonValue result;
    if (!requestJson(method, payload, &result))
        return defaultValue;
    if (!result.isBool())
        return defaultValue;
    return result.toBool(defaultValue);
}

bool ExternalProcessPlugin::requestJson(const QString &method, const QJsonObject &payload, QJsonValue *result) const
{
    if (result)
        *result = QJsonValue();
    // The persistent protocol has a single response channel. Synchronous
    // latency-sensitive queries must not consume an action response while an
    // asynchronous stateful request is queued or active.
    if (!m_persistentAsyncRequests.isEmpty())
        return false;
    if (m_process.state() != QProcess::Running)
    {
        if (!const_cast<ExternalProcessPlugin *>(this)->ensureProcessRunning())
        {
            Logger::warn(QString("Process plugin '%1' request '%2' skipped: process not running.")
                             .arg(m_manifest.pluginId)
                             .arg(method));
            return false;
        }
    }

    // Cooldown after process (re)start: skip the first request that triggered
    // the cold-start to avoid overwhelming a not-yet-ready process.  This
    // breaks the "timeout → restart → immediate timeout" storm.
    {
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_processStartEpochMs;
        if (age < kPostStartCooldownMs)
        {
            Logger::info(QString("Process plugin '%1' request '%2' deferred: process started %3 ms ago (cooldown).")
                             .arg(m_manifest.pluginId)
                             .arg(method)
                             .arg(age));
            return false;
        }
    }

    const QString requestId = QString::number(QDateTime::currentMSecsSinceEpoch());
    QJsonObject req{
        {"type", "request"},
        {"id", requestId},
        {"method", method},
        {"payload", payload},
    };
    const QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
    if (!const_cast<ExternalProcessPlugin *>(this)->writeLine(line))
    {
        Logger::warn(QString("Process plugin '%1' request '%2' failed: writeLine error.")
                         .arg(m_manifest.pluginId)
                         .arg(method));
        m_pendingProcessRestart = true;
        return false;
    }

    const int timeoutMs = requestTimeoutMsForMethod(method);
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
    {
        const int remaining = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
        const int waitSlice = qMax(1, qMin(remaining, 50));
        if (!m_process.waitForReadyRead(waitSlice))
            continue;
        const QByteArray rawResponseLine = m_process.readLine(kMaxResponsePayloadBytes + 1);
        if (rawResponseLine.size() > kMaxResponsePayloadBytes)
        {
            Logger::warn(QString("Process plugin '%1' request '%2' rejected: response payload exceeds %3 bytes.")
                             .arg(m_manifest.pluginId)
                             .arg(method)
                             .arg(kMaxResponsePayloadBytes));
            m_pendingProcessRestart = true;
            return false;
        }
        const QString responseLine = QString::fromUtf8(rawResponseLine).trimmed();
        if (responseLine.isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(responseLine.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        if (obj.value("type").toString() != "response")
            continue;
        if (obj.value("id").toString() != requestId)
            continue;

        if (result)
            *result = obj.value("result");
        return true;
    }
    const QByteArray stderrMsg = m_process.readAllStandardError();
    if (!stderrMsg.isEmpty())
    {
        Logger::warn(QString("Process plugin '%1' request '%2' timeout, stderr: %3")
                         .arg(m_manifest.pluginId)
                         .arg(method)
                         .arg(QString::fromUtf8(stderrMsg).trimmed()));
    }
    else
    {
        Logger::warn(QString("Process plugin '%1' request '%2' timeout without response.")
                         .arg(m_manifest.pluginId)
                         .arg(method));
    }

    if (!probeProcessHealth(kHealthProbeTimeoutMs))
    {
        m_pendingProcessRestart = true;
        Logger::warn(QString("Process plugin '%1' health probe failed after request '%2' timeout; "
                             "deferred restart pending.")
                         .arg(m_manifest.pluginId)
                         .arg(method));
    }
    else
    {
        Logger::warn(QString("Process plugin '%1' request '%2' timeout recovered: process replied to health probe.")
                         .arg(m_manifest.pluginId)
                         .arg(method));
    }

    return false;
}

bool ExternalProcessPlugin::runToolActionOneShot(const QString &actionId, const QVariantMap &context) const
{
    const QFileInfo manifestInfo(m_manifest.manifestPath);
    const QString baseDir = manifestInfo.absolutePath();

    QString executable = m_manifest.executable.trimmed();
    QFileInfo execInfo(executable);
    const bool looksLikePath = executable.contains('/') || executable.contains('\\') || executable.startsWith('.');
    if (looksLikePath && execInfo.isRelative())
        executable = QDir(baseDir).filePath(executable);

    QStringList args;
    for (const QString &arg : m_manifest.args)
    {
        const QString trimmed = arg.trimmed();
        if (trimmed == "--plugin")
            continue;
        const bool argLooksLikePath = trimmed.contains('/') || trimmed.contains('\\') || trimmed.startsWith('.');
        QFileInfo argInfo(trimmed);
        if (argLooksLikePath && argInfo.isRelative())
            args.append(QDir(baseDir).filePath(trimmed));
        else
            args.append(trimmed);
    }

    QStringList candidates;
    const QString pNative = context.value("chart_path_native").toString();
    const QString pPath = context.value("chart_path").toString();
    const QString pCanonical = context.value("chart_path_canonical").toString();
    if (!pNative.isEmpty())
        candidates << pNative;
    if (!pPath.isEmpty())
        candidates << pPath;
    if (!pCanonical.isEmpty())
        candidates << pCanonical;

    QString chartPath;
    for (const QString &candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            chartPath = candidate;
            break;
        }
    }
    if (chartPath.isEmpty() && !candidates.isEmpty())
        chartPath = candidates.first();

    args << "--run-tool-action" << actionId;
    if (!chartPath.isEmpty())
        args << chartPath;

    Logger::info(QString("Process plugin one-shot start (%1): action=%2 path=%3 exists=%4")
                     .arg(m_manifest.pluginId)
                     .arg(actionId)
                     .arg(chartPath)
                     .arg(QFileInfo::exists(chartPath)));

    QProcess oneShot;
    oneShot.setWorkingDirectory(baseDir);
    oneShot.setProgram(executable);
    oneShot.setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    applyUtf8ProcessEnv(&env);
    QString locale = context.value("locale").toString().trimmed();
    QString language = context.value("language").toString().trimmed();
    if (locale.isEmpty())
        locale = currentLocale();
    if (language.isEmpty())
        language = languageFromLocale(locale);
    if (!locale.isEmpty())
        env.insert("MALODY_LOCALE", locale);
    if (!language.isEmpty())
        env.insert("MALODY_LANGUAGE", language);
    oneShot.setProcessEnvironment(env);
    oneShot.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    oneShot.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *p)
                                              { p->flags |= CREATE_NO_WINDOW; });
#endif
    oneShot.start();
    if (!oneShot.waitForStarted(3000))
    {
        Logger::warn(QString("Process plugin '%1' one-shot fallback start failed.").arg(m_manifest.pluginId));
        return false;
    }

    if (!oneShot.waitForFinished(120000))
    {
        oneShot.kill();
        oneShot.waitForFinished(1000);
        Logger::warn(QString("Process plugin '%1' one-shot fallback timed out.").arg(m_manifest.pluginId));
        return false;
    }

    const QByteArray out = oneShot.read(kMaxResponsePayloadBytes + 1);
    if (out.size() > kMaxResponsePayloadBytes)
    {
        oneShot.kill();
        oneShot.waitForFinished(1000);
        Logger::warn(QString("Process plugin '%1' one-shot output exceeded %2 bytes.")
                         .arg(m_manifest.pluginId)
                         .arg(kMaxResponsePayloadBytes));
        return false;
    }
    const QString outText = QString::fromUtf8(out).trimmed();
    if (!outText.isEmpty())
    {
        Logger::info(QString("Process plugin one-shot output (%1): %2")
                         .arg(m_manifest.pluginId)
                         .arg(outText));
    }

    const bool ok = (oneShot.exitStatus() == QProcess::NormalExit && oneShot.exitCode() == 0);
    if (!ok)
    {
        Logger::warn(QString("Process plugin one-shot failed (%1): exitStatus=%2 exitCode=%3")
                         .arg(m_manifest.pluginId)
                         .arg(static_cast<int>(oneShot.exitStatus()))
                         .arg(oneShot.exitCode()));
    }
    return ok;
}

bool ExternalProcessPlugin::probeProcessHealth(int timeoutMs) const
{
    if (m_process.state() != QProcess::Running)
        return false;

    const QString probeId = QString("health_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QJsonObject req{
        {"type", "request"},
        {"id", probeId},
        {"method", "__hostPing"},
        {"payload", QJsonObject()},
    };
    const QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
    if (!const_cast<ExternalProcessPlugin *>(this)->writeLine(line))
        return false;

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
    {
        const int remaining = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
        const int waitSlice = qMax(1, qMin(remaining, 50));
        if (!m_process.waitForReadyRead(waitSlice))
            continue;

        const QByteArray rawResponseLine = m_process.readLine(kMaxResponsePayloadBytes + 1);
        if (rawResponseLine.size() > kMaxResponsePayloadBytes)
            return false;
        const QString responseLine = QString::fromUtf8(rawResponseLine).trimmed();
        if (responseLine.isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(responseLine.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        if (obj.value("type").toString() != "response")
            continue;
        if (obj.value("id").toString() != probeId)
            continue;
        return true;
    }
    return false;
}

void ExternalProcessPlugin::forceRestartProcess(const QString &reason)
{
    const bool wasInitialized = m_initialized;
    Logger::warn(QString("Process plugin '%1' restarting process: %2")
                     .arg(m_manifest.pluginId)
                     .arg(reason));

    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.terminate();
        if (!m_process.waitForFinished(800))
        {
            m_process.kill();
            m_process.waitForFinished(800);
        }
    }
    m_process.readAllStandardOutput();
    m_process.readAllStandardError();
    m_needsReinitialize = wasInitialized;
    invalidateToolActionsCache();
    invalidateCanvasOverlayCache();
}

bool ExternalProcessPlugin::sendInitializeNotification()
{
    const QJsonObject msg{
        {"type", "notify"},
        {"event", "initialize"},
        {"payload", initializePayloadFor(m_manifest)},
    };
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    if (!writeLine(line))
        return false;
    m_needsReinitialize = false;
    return true;
}

bool ExternalProcessPlugin::ensureProcessRunning()
{
    if (m_pendingProcessRestart)
    {
        m_pendingProcessRestart = false;
        forceRestartProcess("deferred restart from prior I/O failure");
    }
    if (m_process.state() == QProcess::Running)
    {
        if (m_needsReinitialize && !sendInitializeNotification())
        {
            forceRestartProcess("failed to reinitialize running process");
            return false;
        }
        return true;
    }

    const QFileInfo manifestInfo(m_manifest.manifestPath);
    const QString baseDir = manifestInfo.absolutePath();

    QString executable = m_manifest.executable.trimmed();
    QFileInfo execInfo(executable);
    const bool looksLikePath = executable.contains('/') || executable.contains('\\') || executable.startsWith('.');
    if (looksLikePath && execInfo.isRelative())
    {
        executable = QDir(baseDir).filePath(executable);
    }

    QStringList resolvedArgs;
    resolvedArgs.reserve(m_manifest.args.size());
    for (const QString &arg : m_manifest.args)
    {
        const QString trimmed = arg.trimmed();
        const bool argLooksLikePath = trimmed.contains('/') || trimmed.contains('\\') || trimmed.startsWith('.');
        QFileInfo argInfo(trimmed);
        if (argLooksLikePath && argInfo.isRelative())
            resolvedArgs.append(QDir(baseDir).filePath(trimmed));
        else
            resolvedArgs.append(trimmed);
    }

    m_process.setWorkingDirectory(baseDir);
    m_process.setProgram(executable);
    m_process.setArguments(resolvedArgs);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    applyUtf8ProcessEnv(&env);
    const QString locale = currentLocale();
    const QString language = languageFromLocale(locale);
    if (!locale.isEmpty())
        env.insert("MALODY_LOCALE", locale);
    if (!language.isEmpty())
        env.insert("MALODY_LANGUAGE", language);
    m_process.setProcessEnvironment(env);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
                                                { args->flags |= CREATE_NO_WINDOW; });
#endif
    m_process.start();
    if (!m_process.waitForStarted(2000))
    {
        Logger::warn(QString("Failed to start process plugin '%1' (%2)")
                         .arg(m_manifest.pluginId)
                         .arg(executable));
        return false;
    }
    m_processStartEpochMs = QDateTime::currentMSecsSinceEpoch();
    if (m_needsReinitialize && !sendInitializeNotification())
    {
        forceRestartProcess("failed to send initialize after restart");
        return false;
    }
    return true;
}

QString ExternalProcessPlugin::resolveLocalizedValue(const QJsonObject &table,
                                                     const QString &locale,
                                                     const QString &fallback) const
{
    if (table.contains(locale))
        return table.value(locale).toString();

    const int split = locale.indexOf('_');
    if (split > 0)
    {
        const QString languageOnly = locale.left(split);
        if (table.contains(languageOnly))
            return table.value(languageOnly).toString();
    }

    if (table.contains("default"))
        return table.value("default").toString();

    return fallback;
}

QString ExternalProcessPlugin::readSingleLine(int timeoutMs) const
{
    if (!m_process.waitForReadyRead(timeoutMs))
        return QString();
    const QByteArray line = m_process.readLine();
    return QString::fromUtf8(line).trimmed();
}

bool ExternalProcessPlugin::writeLine(const QByteArray &line)
{
    if (m_process.state() != QProcess::Running)
        return false;

    if (line.size() > kMaxRequestPayloadBytes)
    {
        Logger::warn(QString("Process plugin '%1' request rejected: payload is %2 bytes (limit %3).")
                         .arg(m_manifest.pluginId)
                         .arg(line.size())
                         .arg(kMaxRequestPayloadBytes));
        return false;
    }

    const qint64 written = m_process.write(line);
    return written == line.size();
}

bool ExternalProcessPlugin::parseBatchEditJson(const QJsonObject &obj, BatchEdit *outEdit)
{
    if (!outEdit)
        return false;
    *outEdit = BatchEdit{};
    auto parseNotes = [](const QJsonValue &value, QVector<Note> *out) -> bool
    {
        if (!out)
            return false;
        if (!value.isArray())
            return false;
        const QJsonArray arr = value.toArray();
        for (const QJsonValue &v : arr)
        {
            if (!v.isObject())
                continue;
            Note n;
            if (ExternalProcessPlugin::parseNoteJson(v.toObject(), &n))
                out->append(n);
        }
        return true;
    };

    parseNotes(obj.value("add"), &outEdit->notesToAdd);
    parseNotes(obj.value("remove"), &outEdit->notesToRemove);

    const QJsonValue moveVal = obj.value("move");
    if (moveVal.isArray())
    {
        const QJsonArray moves = moveVal.toArray();
        for (const QJsonValue &mv : moves)
        {
            if (!mv.isObject())
                continue;
            const QJsonObject moveObj = mv.toObject();
            Note from;
            Note to;
            if (!moveObj.contains("from") || !moveObj.contains("to"))
                continue;
            if (!moveObj.value("from").isObject() || !moveObj.value("to").isObject())
                continue;
            if (!parseNoteJson(moveObj.value("from").toObject(), &from))
                continue;
            if (!parseNoteJson(moveObj.value("to").toObject(), &to))
                continue;
            outEdit->notesToMove.append(qMakePair(from, to));
        }
    }

    return !(outEdit->notesToAdd.isEmpty() && outEdit->notesToRemove.isEmpty() && outEdit->notesToMove.isEmpty());
}

QList<PluginInterface::CanvasOverlayItem> ExternalProcessPlugin::parseOverlayItems(const QJsonArray &arr)
{
    QList<PluginInterface::CanvasOverlayItem> items;
    for (const QJsonValue &v : arr)
    {
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();
        PluginInterface::CanvasOverlayItem item;
        const QString kind = obj.value("kind").toString().toLower();
        if (kind == "rect")
            item.kind = PluginInterface::CanvasOverlayItem::Rect;
        else if (kind == "text")
            item.kind = PluginInterface::CanvasOverlayItem::Text;
        else
            item.kind = PluginInterface::CanvasOverlayItem::Line;

        item.from = QPointF(obj.value("x1").toDouble(), obj.value("y1").toDouble());
        item.to = QPointF(obj.value("x2").toDouble(), obj.value("y2").toDouble());
        item.rect = QRectF(obj.value("x").toDouble(),
                           obj.value("y").toDouble(),
                           obj.value("w").toDouble(),
                           obj.value("h").toDouble());
        item.text = obj.value("text").toString();
        if (obj.contains("color"))
            item.color = QColor(obj.value("color").toString());
        if (obj.contains("fill_color"))
            item.fillColor = QColor(obj.value("fill_color").toString());
        if (obj.contains("width"))
            item.width = obj.value("width").toDouble(item.width);
        if (obj.contains("font_px"))
            item.fontPx = obj.value("font_px").toInt(item.fontPx);
        item.noteSnapReference = obj.value("note_snap_reference").toBool(false);

        const QString coordSpace = obj.value("coord_space").toString().trimmed().toLower();
        if (coordSpace == "chart")
        {
            item.chartSpace = true;
            item.chartFrom = QPointF(
                obj.contains("lane_x1") ? obj.value("lane_x1").toDouble() : obj.value("lane_x").toDouble(),
                obj.contains("beat1") ? obj.value("beat1").toDouble() : obj.value("beat").toDouble());
            item.chartTo = QPointF(
                obj.contains("lane_x2") ? obj.value("lane_x2").toDouble() : item.chartFrom.x(),
                obj.contains("beat2") ? obj.value("beat2").toDouble() : item.chartFrom.y());
            item.rectCenterOnChartPoint =
                obj.value("rect_anchor").toString().trimmed().toLower() != "top_left";
        }
        items.append(item);
    }
    return items;
}
