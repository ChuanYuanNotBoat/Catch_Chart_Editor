#include "PluginLoader.h"
#include "plugin/ExternalProcessPlugin.h"
#include "utils/Logger.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QFile>
#include <exception>
#include <utility>

namespace
{
    struct PluginRuntime
    {
        QLibrary *library = nullptr;
        DestroyPluginFn destroy = nullptr;
        QString filePath;
    };

    QHash<PluginInterface *, PluginRuntime> g_pluginRuntime;

    bool isNativePluginFile(const QString &fileName)
    {
        return fileName.endsWith(".dll", Qt::CaseInsensitive) ||
               fileName.endsWith(".so", Qt::CaseInsensitive) ||
               fileName.endsWith(".dylib", Qt::CaseInsensitive);
    }

    bool isProcessPluginManifestFile(const QString &fileName)
    {
        return fileName.endsWith(".plugin.json", Qt::CaseInsensitive);
    }

    bool isSamplePluginPath(const QString &relativePath)
    {
        const QString normalized = QDir::fromNativeSeparators(relativePath).toLower();
        return normalized.startsWith("samples/");
    }

    QStringList jsonArrayToStringList(const QJsonArray &arr)
    {
        QStringList out;
        out.reserve(arr.size());
        for (const QJsonValue &v : arr)
            out.append(v.toString());
        return out;
    }

    ExternalProcessPlugin::Manifest parseProcessManifest(const QString &manifestPath, bool *ok)
    {
        *ok = false;
        ExternalProcessPlugin::Manifest manifest;
        manifest.manifestPath = manifestPath;
        QFileInfo manifestInfo(manifestPath);
        QString baseName = manifestInfo.completeBaseName();
        if (baseName.endsWith(".plugin", Qt::CaseInsensitive))
            baseName.chop(QString(".plugin").size());

        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly))
        {
            Logger::warn(QString("Failed to open process plugin manifest: %1").arg(manifestPath));
            return manifest;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            Logger::warn(QString("Invalid process plugin manifest JSON: %1 %2").arg(manifestPath, parseError.errorString()));
            return manifest;
        }

        const QJsonObject obj = doc.object();
        manifest.pluginId = obj.value("pluginId").toString().trimmed();
        manifest.displayName = obj.value("displayName").toString().trimmed();
        manifest.version = obj.value("version").toString().trimmed();
        manifest.description = obj.value("description").toString().trimmed();
        manifest.author = obj.value("author").toString().trimmed();
        manifest.apiVersion = obj.value("pluginApiVersion").toInt(-1);
        manifest.executable = obj.value("executable").toString();
        manifest.args = jsonArrayToStringList(obj.value("args").toArray());
        manifest.capabilities = jsonArrayToStringList(obj.value("capabilities").toArray());

        const QJsonValue displayNameL10n = obj.value("localizedDisplayName");
        if (displayNameL10n.isObject())
            manifest.localizedDisplayName = displayNameL10n.toObject();

        const QJsonValue descL10n = obj.value("localizedDescription");
        if (descL10n.isObject())
            manifest.localizedDescription = descL10n.toObject();

        if (manifest.pluginId.isEmpty())
            manifest.pluginId = QString("process.%1").arg(baseName);
        if (manifest.displayName.isEmpty())
            manifest.displayName = baseName;
        if (manifest.version.isEmpty())
            manifest.version = "0.0.0";

        const bool hasRequired = !manifest.pluginId.isEmpty() &&
                                 !manifest.displayName.isEmpty() &&
                                 !manifest.version.isEmpty() &&
                                 !manifest.executable.isEmpty() &&
                                 manifest.apiVersion > 0;
        if (!hasRequired)
        {
            Logger::warn(QString("Process plugin manifest missing required fields: %1").arg(manifestPath));
            return manifest;
        }

        *ok = true;
        return manifest;
    }
}

QVector<PluginInterface *> PluginLoader::loadPlugins(const QString &pluginsDir)
{
    QVector<PluginInterface *> plugins;
    QDir dir(pluginsDir);
    if (!dir.exists())
    {
        Logger::warn(QString("Plugin directory does not exist: %1").arg(pluginsDir));
        return plugins;
    }

    QStringList files;
    QDirIterator it(pluginsDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString absPath = it.next();
        const QString rel = dir.relativeFilePath(absPath);
        files.append(rel);
    }

    for (const QString &file : files)
    {
        if (isSamplePluginPath(file))
            continue;
        if (!isNativePluginFile(file))
            continue;

        const QString absPath = dir.filePath(file);
        QLibrary *lib = new QLibrary(absPath);
        if (!lib->load())
        {
            Logger::warn(QString("Failed to load plugin library: %1 %2").arg(absPath, lib->errorString()));
            delete lib;
            continue;
        }

        const auto getApiVersion = reinterpret_cast<PluginApiVersionFn>(lib->resolve("pluginApiVersion"));
        const auto create = reinterpret_cast<CreatePluginFn>(lib->resolve("createPlugin"));
        const auto destroy = reinterpret_cast<DestroyPluginFn>(lib->resolve("destroyPlugin"));

        if (!getApiVersion || !create || !destroy)
        {
            Logger::warn(QString("Plugin missing required exports (pluginApiVersion/createPlugin/destroyPlugin): %1").arg(absPath));
            lib->unload();
            delete lib;
            continue;
        }

        const int runtimeApiVersion = getApiVersion();
        if (runtimeApiVersion < PluginInterface::kMinSupportedPluginApiVersion ||
            runtimeApiVersion > PluginInterface::kHostApiVersion)
        {
            Logger::warn(QString("Plugin API mismatch: %1 plugin=%2 supported=[%3..%4]")
                             .arg(absPath)
                             .arg(runtimeApiVersion)
                             .arg(PluginInterface::kMinSupportedPluginApiVersion)
                             .arg(PluginInterface::kHostApiVersion));
            lib->unload();
            delete lib;
            continue;
        }

        PluginInterface *plugin = nullptr;
        try
        {
            plugin = create();
        }
        catch (const std::exception &e)
        {
            Logger::warn(QString("Plugin createPlugin exception: %1 %2").arg(absPath, QString::fromUtf8(e.what())));
            lib->unload();
            delete lib;
            continue;
        }
        catch (...)
        {
            Logger::warn(QString("Plugin createPlugin unknown exception: %1").arg(absPath));
            lib->unload();
            delete lib;
            continue;
        }

        if (!plugin)
        {
            Logger::warn(QString("Plugin createPlugin returned null: %1").arg(absPath));
            lib->unload();
            delete lib;
            continue;
        }

        const int pluginApi = plugin->pluginApiVersion();
        if (pluginApi < PluginInterface::kMinSupportedPluginApiVersion ||
            pluginApi > PluginInterface::kHostApiVersion)
        {
            Logger::warn(QString("Plugin instance API mismatch: %1 plugin=%2 supported=[%3..%4]")
                             .arg(absPath)
                             .arg(pluginApi)
                             .arg(PluginInterface::kMinSupportedPluginApiVersion)
                             .arg(PluginInterface::kHostApiVersion));
            destroy(plugin);
            lib->unload();
            delete lib;
            continue;
        }

        plugins.append(plugin);
        g_pluginRuntime.insert(plugin, PluginRuntime{lib, destroy, absPath});
    }

    for (const QString &file : files)
    {
        if (isSamplePluginPath(file))
            continue;
        if (!isProcessPluginManifestFile(file))
            continue;

        const QString manifestPath = dir.filePath(file);
        bool manifestOk = false;
        ExternalProcessPlugin::Manifest manifest = parseProcessManifest(manifestPath, &manifestOk);
        if (!manifestOk)
            continue;

        if (manifest.apiVersion < PluginInterface::kMinSupportedPluginApiVersion ||
            manifest.apiVersion > PluginInterface::kHostApiVersion)
        {
            Logger::warn(QString("Process plugin API mismatch: %1 plugin=%2 supported=[%3..%4]")
                             .arg(manifestPath)
                             .arg(manifest.apiVersion)
                             .arg(PluginInterface::kMinSupportedPluginApiVersion)
                             .arg(PluginInterface::kHostApiVersion));
            continue;
        }

        plugins.append(new ExternalProcessPlugin(std::move(manifest)));
    }

    return plugins;
}

void PluginLoader::unloadPlugins(QVector<PluginInterface *> &plugins)
{
    for (PluginInterface *plugin : plugins)
    {
        if (!plugin)
            continue;

        const PluginRuntime runtime = g_pluginRuntime.take(plugin);
        if (runtime.destroy)
        {
            try
            {
                runtime.destroy(plugin);
            }
            catch (...)
            {
                Logger::warn(QString("Plugin destroyPlugin threw exception: %1").arg(runtime.filePath));
            }
        }

        if (runtime.library)
        {
            runtime.library->unload();
            delete runtime.library;
        }

        if (!runtime.library)
            delete plugin;
    }

    plugins.clear();
}

QString PluginLoader::pluginSourcePath(PluginInterface *plugin)
{
    if (!plugin)
        return QString();

    const auto it = g_pluginRuntime.constFind(plugin);
    if (it != g_pluginRuntime.constEnd())
        return it->filePath;
    return plugin->pluginSourcePath();
}
