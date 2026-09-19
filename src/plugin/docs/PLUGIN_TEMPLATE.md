# Minimal Native Plugin Template (Host API v3)

> Current host API: v3; accepted range: v2-v3; last verified: 2026-09-10.

```cpp
#include "plugin/PluginInterface.h"

class SamplePlugin final : public PluginInterface
{
public:
    QString pluginId() const override { return "sample.plugin"; }
    QString displayName() const override { return "Sample Plugin"; }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return "Minimal plugin example."; }
    QString author() const override { return "Your Name"; }

    QString localizedDisplayName(const QString &locale) const override
    {
        if (locale.startsWith("zh"))
            return "示例插件";
        return displayName();
    }

    QString localizedDescription(const QString &locale) const override
    {
        if (locale.startsWith("zh"))
            return "最小插件示例。";
        return description();
    }

    int pluginApiVersion() const override { return kHostApiVersion; }

    QStringList capabilities() const override
    {
        return {kCapabilityChartObserver};
    }

    bool initialize(QWidget *mainWindow) override
    {
        (void)mainWindow;
        return true;
    }

    void shutdown() override {}

    void onChartChanged() override {}
    void onChartLoaded(const QString &chartPath) override
    {
        (void)chartPath;
    }
    void onChartSaved(const QString &chartPath) override
    {
        (void)chartPath;
    }
};

PLUGIN_EXPORT_VERSION
{
    return PluginInterface::kHostApiVersion;
}

PLUGIN_EXPORT_CREATE
{
    return new SamplePlugin();
}

PLUGIN_EXPORT_DESTROY
{
    delete plugin;
}
```

## Notes

- Native plugin must export `pluginApiVersion/createPlugin/destroyPlugin`.
- The exported and instance API versions must be within the host-supported range; new plugins should compile against and return `kHostApiVersion`.
- Host unload flow calls `shutdown()` first, then `destroyPlugin()`.
- For non-C++ plugins, use `*.plugin.json` + process protocol (`PROCESS_PLUGIN_PROTOCOL.md`).
