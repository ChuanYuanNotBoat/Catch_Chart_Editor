#include "PluginManager.h"
#include "file/PluginLoader.h"
#include "utils/Logger.h"
#include "utils/PlaybackStutterProbe.h"
#include "utils/Settings.h"
#include <QLocale>
#include <QSet>
#include <QElapsedTimer>
#include <QTimer>
#include <exception>

namespace
{
    QString currentLocale()
    {
        QString locale = Settings::instance().language().trimmed();
        if (locale.isEmpty())
            locale = QLocale::system().name();
        return locale;
    }

    QVariantMap enrichContextWithLocale(const QVariantMap &context)
    {
        QVariantMap enriched = context;
        const QString locale = currentLocale();
        if (!enriched.contains("locale"))
            enriched.insert("locale", locale);

        QString language = locale;
        const int split = language.indexOf('_');
        if (split > 0)
            language = language.left(split);
        if (!enriched.contains("language"))
            enriched.insert("language", language);
        return enriched;
    }

    QString normalizedPluginDisplayName(PluginInterface *plugin, const QString &locale)
    {
        if (!plugin)
            return QString();
        QString name = plugin->localizedDisplayName(locale).trimmed();
        if (name.isEmpty())
            name = plugin->displayName().trimmed();
        return name.toCaseFolded();
    }

    bool isBuiltinPluginId(const QString &pluginId)
    {
        return pluginId.trimmed().toLower().startsWith("builtin.");
    }

    bool shouldPreferPlugin(PluginInterface *currentWinner, PluginInterface *candidate)
    {
        if (!candidate)
            return false;
        if (!currentWinner)
            return true;

        const bool winnerBuiltin = isBuiltinPluginId(currentWinner->pluginId());
        const bool candidateBuiltin = isBuiltinPluginId(candidate->pluginId());
        if (winnerBuiltin != candidateBuiltin)
            return candidateBuiltin;

        // Keep deterministic ordering for same tier.
        return false;
}

} // anonymous namespace

PluginManager::PluginManager(QObject *parent) : QObject(parent)
{
    m_disabledPluginIds = Settings::instance().disabledPluginIds();
}

PluginManager::~PluginManager()
{
    unloadPlugins();
}

void PluginManager::loadPlugins(const QString &pluginsDir, QWidget *parent)
{
    m_pluginsDir = pluginsDir;
    m_parentWidget = parent;
    m_pluginInfos.clear();
    unloadPlugins();

    try
    {
        QVector<PluginInterface *> loaded = PluginLoader::loadPlugins(pluginsDir);
        Logger::info(QString("Discovered %1 plugin candidates.").arg(loaded.size()));
        const QString locale = currentLocale();

        // Resolve same-display-name conflicts first: keep one canonical plugin
        // per display name and prefer builtin.* variants.
        QHash<QString, int> winnerByDisplayName;
        for (int i = 0; i < loaded.size(); ++i)
        {
            PluginInterface *p = loaded[i];
            if (!p)
                continue;
            if (p->pluginId().trimmed().compare("builtin.note_chain_assist", Qt::CaseInsensitive) == 0) {
                Logger::info(QString("Plugin '%1' skipped: replaced by native C++ NoteChain module.").arg(p->pluginId()));
                continue;
            }
            const QString key = normalizedPluginDisplayName(p, locale);
            if (key.isEmpty())
                continue;
            const int existing = winnerByDisplayName.value(key, -1);
            if (existing < 0 || shouldPreferPlugin(loaded[existing], p))
                winnerByDisplayName.insert(key, i);
        }

        QVector<PluginInterface *> rejected;
        QSet<QString> seenPluginIdsLower;

        for (int i = 0; i < loaded.size(); ++i)
        {
            PluginInterface *p = loaded[i];
            if (!p)
            {
                Logger::warn(QString("Plugin %1 is null!").arg(i));
                continue;
            }


            const QString displayKey = normalizedPluginDisplayName(p, locale);
            const int displayWinnerIndex = winnerByDisplayName.value(displayKey, -1);
            if (!displayKey.isEmpty() && displayWinnerIndex >= 0 && displayWinnerIndex != i)
            {
                Logger::info(QString("Plugin '%1' skipped: duplicate display name, keeping '%2'.")
                                 .arg(p->pluginId())
                                 .arg(loaded[displayWinnerIndex] ? loaded[displayWinnerIndex]->pluginId() : QString("unknown")));
                rejected.append(p);
                continue;
            }

            PluginInfo info;
            info.pluginId = p->pluginId();
            info.displayName = p->localizedDisplayName(locale);
            info.version = p->version();
            info.author = p->author();
            info.description = p->localizedDescription(locale);
            info.capabilities = p->capabilities();
            info.sourcePath = PluginLoader::pluginSourcePath(p);
            info.enabled = isPluginEnabled(info.pluginId);
            const QString pluginIdKey = info.pluginId.trimmed().toLower();

            if (!pluginIdKey.isEmpty() && seenPluginIdsLower.contains(pluginIdKey))
            {
                info.active = false;
                info.loadError = QString("Duplicate plugin id: %1").arg(info.pluginId);
                rejected.append(p);
                m_pluginInfos.append(info);
                Logger::warn(QString("Plugin '%1' skipped: duplicate plugin id.")
                                 .arg(info.displayName.isEmpty() ? info.pluginId : info.displayName));
                continue;
            }
            if (!pluginIdKey.isEmpty())
                seenPluginIdsLower.insert(pluginIdKey);

            const int pluginApi = p->pluginApiVersion();
            if (pluginApi < PluginInterface::kMinSupportedPluginApiVersion ||
                pluginApi > PluginInterface::kHostApiVersion)
            {
                info.active = false;
                info.loadError = QString("API mismatch: plugin=%1 supported=[%2..%3]")
                                     .arg(pluginApi)
                                     .arg(PluginInterface::kMinSupportedPluginApiVersion)
                                     .arg(PluginInterface::kHostApiVersion);
                rejected.append(p);
                m_pluginInfos.append(info);
                Logger::warn(QString("Plugin '%1' skipped: %2")
                                 .arg(info.displayName.isEmpty() ? info.pluginId : info.displayName)
                                 .arg(info.loadError));
                continue;
            }

            if (!info.enabled)
            {
                info.active = false;
                info.loadError = "Disabled by user";
                rejected.append(p);
                m_pluginInfos.append(info);
                Logger::info(QString("Plugin '%1' is disabled by user settings.").arg(info.pluginId));
                continue;
            }

            // Defer initialize() to background: collect metadata first, then
            // schedule async init via the event loop to avoid blocking UI thread
            // on process-plugin cold starts.
            info.active = false; // will become true after async init
            info.loadError = QString();
            m_pluginInfos.append(info);
            m_pendingInit.append(p);
        }

        if (!rejected.isEmpty())
            PluginLoader::unloadPlugins(rejected);

        Logger::info(QString("PluginManager metadata collected: %1 candidates, %2 pending init.")
                         .arg(m_pluginInfos.size())
                         .arg(m_pendingInit.size()));
        emit pluginsChanged();

        // Kick off async initialization in the next event-loop iteration so
        // the UI stays responsive.
        QTimer::singleShot(0, [this]() { initializePendingPlugins(); });
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("Error loading plugins: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("Unknown error loading plugins");
    }
}

void PluginManager::reloadPlugins()
{
    if (m_pluginsDir.isEmpty())
        return;
    loadPlugins(m_pluginsDir, m_parentWidget.data());
}

void PluginManager::unloadPlugins()
{
    if (!m_plugins.isEmpty())
    {
        for (PluginInterface *p : m_plugins)
        {
            if (!p)
                continue;
            try
            {
                p->shutdown();
            }
            catch (...)
            {
                Logger::warn(QString("Error in plugin '%1' shutdown").arg(localizedNameForLog(p)));
            }
        }

        PluginLoader::unloadPlugins(m_plugins);
        Logger::info("All plugins unloaded.");
        emit pluginsChanged();
    }
}

QVector<PluginInterface *> PluginManager::plugins() const
{
    return m_plugins;
}

QVector<PluginManager::PluginInfo> PluginManager::pluginInfos() const
{
    return m_pluginInfos;
}

QString PluginManager::pluginsDir() const
{
    return m_pluginsDir;
}

bool PluginManager::isPluginEnabled(const QString &pluginId) const
{
    return !m_disabledPluginIds.contains(pluginId, Qt::CaseSensitive);
}

void PluginManager::setPluginEnabled(const QString &pluginId, bool enabled)
{
    if (pluginId.isEmpty())
        return;

    if (enabled)
        m_disabledPluginIds.removeAll(pluginId);
    else if (!m_disabledPluginIds.contains(pluginId))
        m_disabledPluginIds.append(pluginId);

    m_disabledPluginIds.removeDuplicates();
    Settings::instance().setDisabledPluginIds(m_disabledPluginIds);
}

QStringList PluginManager::disabledPluginIds() const
{
    return m_disabledPluginIds;
}

QList<PluginManager::ToolActionEntry> PluginManager::toolActions() const
{
    QList<ToolActionEntry> entries;
    const QString locale = currentLocale();
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        const QList<PluginInterface::ToolAction> actions = p->toolActions();
        for (const PluginInterface::ToolAction &action : actions)
        {
            if (action.actionId.isEmpty() || action.title.isEmpty())
                continue;
            ToolActionEntry entry;
            entry.pluginId = p->pluginId();
            entry.pluginDisplayName = p->localizedDisplayName(locale);
            entry.action = action;
            entries.append(entry);
        }
    }
    Logger::info(QString("PluginManager::toolActions => %1 entries").arg(entries.size()));
    return entries;
}

QList<PluginManager::FloatingPanelEntry> PluginManager::floatingPanels() const
{
    QList<FloatingPanelEntry> entries;
    const QString locale = currentLocale();
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (!p->hasCapability(PluginInterface::kCapabilityFloatingPanel))
            continue;
        const QList<PluginInterface::FloatingPanelDescriptor> panels = p->floatingPanels();
        for (const PluginInterface::FloatingPanelDescriptor &panel : panels)
        {
            if (panel.panelId.isEmpty() || panel.title.isEmpty())
                continue;
            FloatingPanelEntry e;
            e.pluginId = p->pluginId();
            e.pluginDisplayName = p->localizedDisplayName(locale);
            e.panel = panel;
            entries.append(e);
        }
    }
    return entries;
}

bool PluginManager::runToolAction(const QString &pluginId, const QString &actionId, const QVariantMap &context)
{
    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (!p->hasCapability(PluginInterface::kCapabilityToolActions))
            continue;
        if (p->pluginId() != pluginId)
            continue;
        try
        {
            return p->runToolAction(actionId, enrichedContext);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' runToolAction(%2)")
                             .arg(localizedNameForLog(p))
                             .arg(actionId));
            return false;
        }
    }
    return false;
}

bool PluginManager::supportsHostBatchEdit(const QString &pluginId) const
{
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (p->pluginId() != pluginId)
            continue;
        return p->hasCapability(PluginInterface::kCapabilityHostBatchEdit);
    }
    return false;
}

bool PluginManager::buildToolActionBatchEdit(const QString &pluginId,
                                             const QString &actionId,
                                             const QVariantMap &context,
                                             PluginInterface::BatchEdit *outEdit)
{
    if (outEdit)
        *outEdit = PluginInterface::BatchEdit{};
    if (!outEdit)
        return false;

    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (p->pluginId() != pluginId)
            continue;
        try
        {
            return p->buildToolActionBatchEdit(actionId, enrichedContext, outEdit);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' buildToolActionBatchEdit(%2)")
                             .arg(localizedNameForLog(p))
                             .arg(actionId));
            return false;
        }
    }
    return false;
}

QWidget *PluginManager::createFloatingPanel(const QString &pluginId,
                                            const QString &panelId,
                                            QWidget *parent,
                                            const QVariantMap &context)
{
    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (p->pluginId() != pluginId)
            continue;
        try
        {
            return p->createFloatingPanel(panelId, parent, enrichedContext);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' createFloatingPanel(%2)")
                             .arg(localizedNameForLog(p))
                             .arg(panelId));
            return nullptr;
        }
    }
    return nullptr;
}

QList<PluginInterface::CanvasOverlayItem> PluginManager::canvasOverlays(const QVariantMap &context) const
{
    QList<PluginInterface::CanvasOverlayItem> items;
    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    const bool isPlaying = enrichedContext.value("is_playing").toBool();
    QElapsedTimer totalTimer;
    totalTimer.start();
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (!p->hasCapability(PluginInterface::kCapabilityCanvasOverlay))
            continue;
        try
        {
            QElapsedTimer pluginTimer;
            pluginTimer.start();
            const auto overlay = p->canvasOverlays(enrichedContext);
            const double elapsedMs = static_cast<double>(pluginTimer.nsecsElapsed()) / 1000000.0;
            const QString pluginId = p->pluginId().trimmed().isEmpty() ? QString("unknown") : p->pluginId().trimmed();
            const QString key = QString("plugin.overlay.%1").arg(pluginId);
            PlaybackStutterProbe::recordDuration(key, elapsedMs, 3.0, isPlaying);
            PlaybackStutterProbe::recordCounter(QString("%1.items").arg(key), overlay.size(), isPlaying);
            for (const auto &item : overlay)
                items.append(item);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' canvasOverlays").arg(localizedNameForLog(p)));
        }
    }
    PlaybackStutterProbe::recordDuration("plugin.overlay.total",
                                         static_cast<double>(totalTimer.nsecsElapsed()) / 1000000.0,
                                         4.0,
                                         isPlaying);
    return items;
}

bool PluginManager::handleCanvasInput(const QString &pluginId,
                                      const QVariantMap &context,
                                      const PluginInterface::CanvasInputEvent &event,
                                      PluginInterface::CanvasInputResult *outResult)
{
    if (outResult)
        *outResult = PluginInterface::CanvasInputResult{};
    if (!outResult || pluginId.trimmed().isEmpty())
        return false;

    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (p->pluginId() != pluginId)
            continue;
        if (!p->hasCapability(PluginInterface::kCapabilityCanvasInteraction))
            return false;
        try
        {
            return p->handleCanvasInput(enrichedContext, event, outResult);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' handleCanvasInput")
                             .arg(localizedNameForLog(p)));
            return false;
        }
    }
    return false;
}

QVariantMap PluginManager::panelWorkspaceConfig(const QString &pluginId, const QVariantMap &context) const
{
    if (pluginId.trimmed().isEmpty())
        return {};

    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        if (p->pluginId() != pluginId)
            continue;
        if (!p->hasCapability(PluginInterface::kCapabilityPanelWorkspace))
            return {};
        try
        {
            return p->panelWorkspaceConfig(enrichedContext);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' panelWorkspaceConfig")
                             .arg(localizedNameForLog(p)));
            return {};
        }
    }
    return {};
}

void PluginManager::notifyChartChanged()
{
    for (PluginInterface *p : m_plugins)
    {
        if (p)
        {
            try
            {
                p->onChartChanged();
            }
            catch (...)
            {
                Logger::warn(QString("Error in plugin '%1' onChartChanged").arg(localizedNameForLog(p)));
            }
        }
    }
}

void PluginManager::notifyChartLoaded(const QString &chartPath)
{
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        try
        {
            p->onChartLoaded(chartPath);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' onChartLoaded").arg(localizedNameForLog(p)));
        }
    }
}

void PluginManager::notifyChartSaved(const QString &chartPath)
{
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        try
        {
            p->onChartSaved(chartPath);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' onChartSaved").arg(localizedNameForLog(p)));
        }
    }
}

void PluginManager::notifyHostUndo(const QString &actionText)
{
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        try
        {
            p->onHostUndo(actionText);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' onHostUndo").arg(localizedNameForLog(p)));
        }
    }
}

void PluginManager::notifyHostRedo(const QString &actionText)
{
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        try
        {
            p->onHostRedo(actionText);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' onHostRedo").arg(localizedNameForLog(p)));
        }
    }
}

void PluginManager::notifyHostDiscardChanges(const QString &reasonText)
{
    for (PluginInterface *p : m_plugins)
    {
        if (!p)
            continue;
        try
        {
            p->onHostDiscardChanges(reasonText);
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' onHostDiscardChanges").arg(localizedNameForLog(p)));
        }
    }
}

bool PluginManager::tryOpenAdvancedColorEditor(const QVariantMap &context)
{
    const QVariantMap enrichedContext = enrichContextWithLocale(context);
    for (PluginInterface *p : m_plugins)
    {
        if (!p || !p->hasCapability(PluginInterface::kCapabilityAdvancedColorEditor))
            continue;

        try
        {
            if (p->openAdvancedColorEditor(enrichedContext))
            {
                Logger::info(QString("Advanced color editor handled by plugin '%1'.").arg(localizedNameForLog(p)));
                return true;
            }
        }
        catch (...)
        {
            Logger::warn(QString("Error in plugin '%1' openAdvancedColorEditor").arg(localizedNameForLog(p)));
        }
    }
    return false;
}

QString PluginManager::localizedNameForLog(PluginInterface *plugin) const
{
    if (!plugin)
        return "unknown";
    const QString locale = currentLocale();
    const QString name = plugin->localizedDisplayName(locale);
    if (!name.isEmpty())
        return name;
    return plugin->displayName();
}

void PluginManager::initializePendingPlugins()
{
    if (m_pendingInit.isEmpty())
        return;

    Logger::info(QString("Starting async initialization of %1 plugins...").arg(m_pendingInit.size()));
    QWidget *parent = m_parentWidget.data();

    for (int i = 0; i < m_pendingInit.size(); ++i)
    {
        PluginInterface *p = m_pendingInit[i];
        if (!p)
            continue;

        const QString pluginId = p->pluginId();

        try
        {
            const bool ok = p->initialize(parent);
            if (!ok)
            {
                Logger::warn(QString("Plugin '%1' async initialize returned false.").arg(pluginId));
                continue;
            }

            m_plugins.append(p);
            Logger::info(QString("Plugin async initialized: id='%1', name='%2', version='%3', author='%4'")
                             .arg(pluginId)
                             .arg(p->localizedDisplayName(currentLocale()))
                             .arg(p->version())
                             .arg(p->author()));

            // Update PluginInfo entry to reflect active status
            for (int j = 0; j < m_pluginInfos.size(); ++j)
            {
                if (m_pluginInfos[j].pluginId == pluginId)
                {
                    m_pluginInfos[j].active = true;
                    m_pluginInfos[j].loadError.clear();
                    break;
                }
            }
        }
        catch (const std::exception &e)
        {
            Logger::error(QString("Error during async init of plugin %1: %2").arg(pluginId).arg(e.what()));
        }
        catch (...)
        {
            Logger::error(QString("Unknown error during async init of plugin %1").arg(pluginId));
        }
    }

    m_pendingInit.clear();
    Logger::info(QString("PluginManager async init complete: %1 active plugins, %2 total entries.")
                     .arg(m_plugins.size())
                     .arg(m_pluginInfos.size()));
    emit pluginsChanged();
}