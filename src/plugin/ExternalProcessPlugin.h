#pragma once

#include "plugin/PluginInterface.h"
#include <QJsonValue>
#include <QJsonObject>
#include <QFuture>
#include <QHash>
#include <QMutex>
#include <QProcess>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>

class QObject;

class ExternalProcessPlugin final : public PluginInterface
{
public:
    using AsyncRequestId = quint64;
    using AsyncToolActionCallback = std::function<void(bool)>;
    using AsyncBatchEditCallback = std::function<void(bool, BatchEdit)>;

    // Tool requests issued immediately after a process starts are deferred to
    // prevent a restart/request timeout loop while the plugin runtime boots.
    static constexpr int kPostStartCooldownMs = 100;

    // Keep process-plugin IPC bounded even when a plugin is misbehaving or a
    // chart context accidentally contains a large serialized document.
    static constexpr qint64 kMaxRequestPayloadBytes = 1024 * 1024;
    static constexpr qint64 kMaxResponsePayloadBytes = 4 * 1024 * 1024;

    struct Manifest
    {
        QString pluginId;
        QString displayName;
        QString version;
        QString description;
        QString author;
        int apiVersion = 0;
        QString executable;
        QStringList args;
        QStringList capabilities;
        QJsonObject localizedDisplayName;
        QJsonObject localizedDescription;
        QString manifestPath;
    };

    explicit ExternalProcessPlugin(Manifest manifest);
    ~ExternalProcessPlugin() override;

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString description() const override;
    QString author() const override;
    QString pluginSourcePath() const override;
    QString localizedDisplayName(const QString &locale) const override;
    QString localizedDescription(const QString &locale) const override;
    int pluginApiVersion() const override;
    QStringList capabilities() const override;

    bool initialize(QWidget *mainWindow) override;
    void shutdown() override;

    void onChartChanged() override;
    void onChartLoaded(const QString &chartPath) override;
    void onChartSaved(const QString &chartPath) override;
    void onHostUndo(const QString &actionText) override;
    void onHostRedo(const QString &actionText) override;
    void onHostDiscardChanges(const QString &reasonText) override;
    bool openAdvancedColorEditor(const QVariantMap &context) override;
    QList<ToolAction> toolActions() const override;
    bool runToolAction(const QString &actionId, const QVariantMap &context) override;
    bool buildToolActionBatchEdit(const QString &actionId, const QVariantMap &context, BatchEdit *outEdit) override;

    // Expensive action requests run in an isolated worker process so the host
    // event loop stays responsive. The callback is queued to callbackContext.
    // A request id can be cancelled; cancellation terminates the worker
    // process and completes the callback with false.
    AsyncRequestId runToolActionAsync(const QString &actionId,
                                      const QVariantMap &context,
                                      QObject *callbackContext,
                                      AsyncToolActionCallback callback);
    AsyncRequestId buildToolActionBatchEditAsync(const QString &actionId,
                                                 const QVariantMap &context,
                                                 QObject *callbackContext,
                                                 AsyncBatchEditCallback callback);
    void cancelAsyncRequest(AsyncRequestId requestId);

    QList<CanvasOverlayItem> canvasOverlays(const QVariantMap &context) const override;
    bool handleCanvasInput(const QVariantMap &context,
                           const CanvasInputEvent &event,
                           CanvasInputResult *outResult) override;
    QVariantMap panelWorkspaceConfig(const QVariantMap &context) const override;

private:
    bool sendNotification(const QString &event, const QJsonObject &payload = QJsonObject());
    bool requestBool(const QString &method, const QJsonObject &payload, bool defaultValue) const;
    bool requestJson(const QString &method, const QJsonObject &payload, QJsonValue *result) const;
    bool runToolActionOneShot(const QString &actionId, const QVariantMap &context) const;
    using AsyncJsonCompletion = std::function<void(bool, const QJsonValue &)>;
    AsyncRequestId startAsyncJsonRequest(const QString &method,
                                         const QJsonObject &payload,
                                         const QString &locale,
                                         QObject *callbackContext,
                                         AsyncJsonCompletion callback);
    bool probeProcessHealth(int timeoutMs) const;
    void forceRestartProcess(const QString &reason);
    bool sendInitializeNotification();
    bool ensureProcessRunning();
    QString resolveLocalizedValue(const QJsonObject &table, const QString &locale, const QString &fallback) const;
    QString readSingleLine(int timeoutMs) const;
    bool writeLine(const QByteArray &line);
    static bool parseNoteJson(const QJsonObject &obj, Note *outNote);
    static bool parseBatchEditJson(const QJsonObject &obj, BatchEdit *outEdit);
    static QList<CanvasOverlayItem> parseOverlayItems(const QJsonArray &arr);

    void invalidateToolActionsCache();

    struct AsyncJob
    {
        std::shared_ptr<std::atomic_bool> cancelled;
        QFuture<void> future;
    };

    void pruneCompletedAsyncJobs() const;

private:
    Manifest m_manifest;
    mutable QProcess m_process;
    bool m_initialized = false;
    bool m_needsReinitialize = false;
    mutable QList<ToolAction> m_cachedToolActions;
    mutable bool m_toolActionsCached = false;
    mutable QList<CanvasOverlayItem> m_cachedCanvasOverlays;
    mutable bool m_canvasOverlaysCached = false;
    mutable bool m_pendingProcessRestart = false;
    mutable qint64 m_processStartEpochMs = 0;
    mutable QMutex m_asyncJobsMutex;
    mutable QHash<AsyncRequestId, AsyncJob> m_asyncJobs;
    mutable AsyncRequestId m_nextAsyncRequestId = 1;
    void invalidateCanvasOverlayCache();
};
