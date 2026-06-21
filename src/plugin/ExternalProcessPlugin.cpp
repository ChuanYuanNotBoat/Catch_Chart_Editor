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
#include <QVariantMap>
#include <QProcessEnvironment>
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
            return 30;
        if (method == "handleCanvasInput")
            return 50;
        if (method == "getPanelWorkspaceConfig")
            return 3000;
        return 5000;
    }

    constexpr int kHealthProbeTimeoutMs = 300;
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

}

ExternalProcessPlugin::ExternalProcessPlugin(Manifest manifest)
    : m_manifest(std::move(manifest))
{
}

ExternalProcessPlugin::~ExternalProcessPlugin()
{
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
    const bool requiresPersistentSession =
        hasCapability(kCapabilityCanvasInteraction) ||
        hasCapability(kCapabilityPanelWorkspace);

    if (!requiresPersistentSession)
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
        m_canvasOverlaysCached = true;
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
    if (obj.value("overlay").isArray())
        outResult->overlay = parseOverlayItems(obj.value("overlay").toArray());
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
        const_cast<ExternalProcessPlugin *>(this)->forceRestartProcess(
            QString("writeLine failed for request '%1'").arg(method));
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
        const QString responseLine = QString::fromUtf8(m_process.readLine()).trimmed();
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
        const_cast<ExternalProcessPlugin *>(this)->forceRestartProcess(
            QString("health probe failed after request timeout '%1'").arg(method));
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

    const QByteArray out = oneShot.readAll();
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

        const QString responseLine = QString::fromUtf8(m_process.readLine()).trimmed();
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

    const qint64 written = m_process.write(line);
    return written > 0;
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
