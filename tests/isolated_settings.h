#pragma once

#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

inline bool initializeIsolatedSettings()
{
    // Keep the directory alive until the application's Settings singleton shuts down.
    static const QTemporaryDir settings;
    if (!settings.isValid())
        return false;

    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.filePath("system"));
    return true;
}
