#pragma once

#include <QObject>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include "plugin/PluginInterface.h"

class PluginManager : public QObject
{
    Q_OBJECT
public:
    struct PluginInfo
    {
        QString pluginId;
        QString displayName;
        QString version;
        QString author;
        QString description;
        QString sourcePath;
        QStringList capabilities;
        bool enabled = true;
        bool active = false;
        QString loadError;
    };
    struct ToolActionEntry
    {
        QString pluginId;
        QString pluginDisplayName;
        PluginInterface::ToolAction action;
    };
    struct FloatingPanelEntry
    {
        QString pluginId;
        QString pluginDisplayName;
        PluginInterface::FloatingPanelDescriptor panel;
    };

    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager();

    void loadPlugins(const QString &pluginsDir, QWidget *parent = nullptr);
    void reloadPlugins();
    void unloadPlugins();
    void initializePendingPlugins();
    QVector<PluginInterface *> plugins() const;
    QVector<PluginInfo> pluginInfos() const;
    QString pluginsDir() const;

    bool isPluginEnabled(const QString &pluginId) const;
    void setPluginEnabled(const QString &pluginId, bool enabled);
    QStringList disabledPluginIds() const;
    QList<ToolActionEntry> toolActions() const;
    QList<FloatingPanelEntry> floatingPanels() const;
    bool runToolAction(const QString &pluginId, const QString &actionId, const QVariantMap &context);
    bool supportsHostBatchEdit(const QString &pluginId) const;
    bool buildToolActionBatchEdit(const QString &pluginId,
                                  const QString &actionId,
                                  const QVariantMap &context,
                                  PluginInterface::BatchEdit *outEdit);
    QWidget *createFloatingPanel(const QString &pluginId,
                                 const QString &panelId,
                                 QWidget *parent,
                                 const QVariantMap &context);
    QList<PluginInterface::CanvasOverlayItem> canvasOverlays(const QVariantMap &context) const;
    bool handleCanvasInput(const QString &pluginId,
                           const QVariantMap &context,
                           const PluginInterface::CanvasInputEvent &event,
                           PluginInterface::CanvasInputResult *outResult);
    QVariantMap panelWorkspaceConfig(const QString &pluginId, const QVariantMap &context) const;

    void notifyChartChanged();
    void notifyChartLoaded(const QString &chartPath);
    void notifyChartSaved(const QString &chartPath);
    void notifyHostUndo(const QString &actionText);
    void notifyHostRedo(const QString &actionText);
    void notifyHostDiscardChanges(const QString &reasonText);
    bool tryOpenAdvancedColorEditor(const QVariantMap &context);

signals:
    void pluginsChanged();

private:
    QString localizedNameForLog(PluginInterface *plugin) const;

private:
    QVector<PluginInterface *> m_plugins;
    QVector<PluginInfo> m_pluginInfos;
    QVector<PluginInterface *> m_pendingInit;
    QStringList m_disabledPluginIds;
    QString m_pluginsDir;
    QPointer<QWidget> m_parentWidget;
};
