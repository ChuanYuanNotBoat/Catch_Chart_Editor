#pragma once

#include "plugin/PluginInterface.h"
#include <QJsonValue>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

class ExternalProcessPlugin final : public PluginInterface
{
public:
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
    void invalidateCanvasOverlayCache();
};