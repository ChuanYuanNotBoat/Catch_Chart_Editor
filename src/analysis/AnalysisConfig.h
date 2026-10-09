#pragma once
#include "audio/AutoTiming2Bridge.h"
#include <QJsonObject>
#include <QStringList>

namespace analysis
{
struct ConfigField
{
    const char *id;
    const char *label;
    const char *section;
    double initial, minimum, maximum;
    bool boolean = false, integer = false, debug = false;
};
const QVector<ConfigField> &configFields();
QJsonObject defaultConfig();
QStringList validateConfig(const QJsonObject &config);
QStringList validateRequest(const QJsonObject &config, double startMs, double durationMs);
// Import is transactional: failure leaves out untouched. Debug values from a
// different Core build are retained as inactive metadata, never silently used.
bool importConfig(const QJsonObject &, QJsonObject &out, QStringList &report);
QJsonObject effectiveConfig(const QJsonObject &preset, const QJsonObject &projectOverride);
QString configHash(const QJsonObject &);
AutoTiming2Options configOptions(const QJsonObject &);
bool saveJson(const QString &path, const QJsonObject &, QString *error = nullptr);
} // namespace analysis
