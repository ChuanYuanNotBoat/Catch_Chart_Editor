#include "AnalysisConfig.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
namespace analysis
{
const QVector<ConfigField> &configFields()
{
    static const QVector<ConfigField> fields = {
        {"preferLocalTempoEvidence", "Prefer local tempo evidence", "Tempo", 0, 0, 1, true},
        {"minimumTempoBpm", "Minimum tempo (BPM)", "Tempo", 30, 1, 10000},
        {"maximumTempoBpm", "Maximum tempo (BPM)", "Tempo", 480, 1, 10000},
        {"enableFineTempoTracking", "Fine tempo tracking", "Tempo", 1, 0, 1, true},
        {"fineTempoWindowSeconds", "Fine window (s)", "Tempo", 4, 1, 120},
        {"fineTempoHopSeconds", "Fine hop (s)", "Tempo", 2, .01, 120},
        {"maximumTrackedGapSeconds", "Maximum tracked gap (s)", "Phase", 12, .001, 120},
        {"enableComplexSubdivisionAnalysis", "Experimental subdivision analysis", "Rhythm", 0, 0, 1, true},
        {"tempoMapMaximumTimeErrorMilliseconds", "BPM list error budget (ms)", "Export", 1, .001, 100},
        {"tempoMapMaximumEntries", "Maximum BPM list entries", "Export", 2048, 2, 100000, false, true},
        {"minimumWindowSeconds", "Minimum window (s)", "Windows", 4, 1, 120, false, false, true},
        {"tempoAgreementTolerance", "Tempo agreement tolerance", "Tempo", .015, .000001, .25, false, false, true},
        {"anchorReliabilityThreshold", "Anchor reliability gate", "Tempo", .62, 0, 1, false, false, true},
        {"minimumOnsetPeriodicityScore", "Onset periodicity gate", "Tempo", .18, 0, 1, false, false, true},
        {"maximumLocalTempoCandidates", "Local candidate limit", "Tempo", 5, 1, 128, false, true, true},
        {"maximumGlobalTempoCandidates", "Global candidate limit", "Tempo", 6, 1, 128, false, true, true},
        {"maximumPhaseBackPropagationSeconds", "Phase back-propagation (s)", "Phase", 60, 0, 600, false, false, true},
        {"maximumContinuousTempoSlopeOctavesPerSecond", "Continuous slope (octaves/s)", "Phase", .08, .000001, 10,
         false, false, true},
        {"tempoTransitionPenalty", "Tempo transition penalty", "Phase", 2, 0, 100, false, false, true},
        {"tempoSlopeChangePenalty", "Slope change penalty", "Phase", 4, 0, 100, false, false, true},
        {"phaseTransitionPenalty", "Phase transition penalty", "Phase", 1.25, 0, 100, false, false, true},
        {"maximumPeriodicityLayers", "Periodicity layer limit", "Rhythm", 48, 1, 1024, false, true, true},
        {"enableHighRateRhythmAnalysis", "High-rate rhythm analysis", "Rhythm", 1, 0, 1, true, false, true},
        {"minimumHighRhythmRateBpm", "Minimum high rhythm rate", "Rhythm", 600, 1, 10000, false, false, true},
        {"maximumHighRhythmRateBpm", "Maximum high rhythm rate", "Rhythm", 3000, 1, 10000, false, false, true},
        {"minimumRhythmPeriodicityScore", "Rhythm periodicity gate", "Rhythm", .16, 0, 1, false, false, true},
        {"maximumLocalRhythmCandidates", "Raw rhythm candidate limit", "Rhythm", 8, 1, 128, false, true, true},
        {"minimumRhythmLayerConfidence", "Rhythm layer confidence gate", "Rhythm", .20, 0, 1, false, false, true},
        {"strongRhythmLayerConfidence", "Strong rhythm confidence gate", "Rhythm", .45, 0, 1, false, false, true},
        {"minimumSemanticRhythmPhaseConfidence", "Semantic phase gate", "Rhythm", .45, 0, 1, false, false, true},
        {"minimumSemanticRhythmPulseCoverage", "Semantic pulse coverage gate", "Rhythm", .38, 0, 1, false, false, true},
        {"minimumSemanticRhythmTransientDensityCoverage", "Semantic transient density gate", "Rhythm", .25, 0, 1, false,
         false, true},
        {"minimumRhythmLayerSupportWindows", "Semantic support windows", "Rhythm", 3, 2, 128, false, true, true},
        {"maximumRhythmProfiles", "Rhythm profile limit", "Rhythm", 48, 1, 1024, false, true, true},
        {"minimumSemanticSubdivision", "Minimum semantic subdivision", "Rhythm", 9, 2, 128, false, true, true},
        {"maximumSubdivision", "Maximum subdivision", "Rhythm", 32, 2, 128, false, true, true},
        {"maximumPolyrhythmTerm", "Reserved polyrhythm term", "Rhythm", 8, 2, 128, false, true, true},
        {"maximumPolyrhythmLayersPerProfile", "Reserved polyrhythm layers", "Rhythm", 2, 1, 128, false, true, true}};
    return fields;
}
QJsonObject defaultConfig()
{
    QJsonObject stable, debug;
    for (const auto &f : configFields())
    {
        QJsonValue v = f.boolean ? QJsonValue(bool(f.initial)) : QJsonValue(f.initial);
        (f.debug ? debug : stable).insert(f.id, v);
    }
    stable["windowSpecs"] = QJsonArray{QJsonArray{8., 4.}, QJsonArray{24., 12.}, QJsonArray{48., 24.}};
    return {{"schemaVersion", 1},
            {"algorithmVersion", AutoTiming2Bridge::algorithmVersion()},
            {"stable", stable},
            {"debug", debug}};
}
QJsonObject effectiveConfig(const QJsonObject &preset, const QJsonObject &project)
{
    auto out = defaultConfig();
    for (const auto &layer : {preset, project})
    {
        for (const auto *key : {"stable", "debug"})
        {
            if (QLatin1String(key) == QLatin1String("debug")
                && layer.value("algorithmVersion").toString() != AutoTiming2Bridge::algorithmVersion())
                continue;
            auto section = out.value(key).toObject();
            const auto additions = layer.value(key).toObject();
            for (auto it = additions.begin(); it != additions.end(); ++it)
                section[it.key()] = it.value();
            out[key] = section;
        }
    }
    return out;
}
QStringList validateConfig(const QJsonObject &config)
{
    QStringList errors;
    if (!config.value("configurationError").toString().isEmpty())
        errors << config.value("configurationError").toString();
    if (config.value("schemaVersion").toInt(-1) != 1)
        errors << QStringLiteral("Unsupported profile schemaVersion");
    if (config.value("algorithmVersion").toString() != AutoTiming2Bridge::algorithmVersion())
        errors << QStringLiteral("Core build changed; import/migrate this profile before analysis");
    for (const auto &f : configFields())
    {
        const auto v = config.value(f.debug ? "debug" : "stable").toObject().value(f.id);
        if (f.boolean ? !v.isBool()
                      : (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < f.minimum
                         || v.toDouble() > f.maximum || (f.integer && std::floor(v.toDouble()) != v.toDouble())))
            errors << QStringLiteral("Invalid %1").arg(QLatin1String(f.id));
    }
    auto number = [&](const char *key, bool debug = false) {
        return config.value(debug ? "debug" : "stable").toObject().value(key).toDouble();
    };
    if (number("minimumTempoBpm") >= number("maximumTempoBpm"))
        errors << QStringLiteral("Minimum tempo must be below maximum tempo");
    if (number("fineTempoHopSeconds") > number("fineTempoWindowSeconds"))
        errors << QStringLiteral("Fine hop exceeds window duration");
    if (config.value("stable").toObject().value("enableFineTempoTracking").toBool()
        && number("fineTempoWindowSeconds") < number("minimumWindowSeconds", true))
        errors << QStringLiteral("Fine window is shorter than the configured minimum window");
    if (number("minimumHighRhythmRateBpm", true) >= number("maximumHighRhythmRateBpm", true))
        errors << QStringLiteral("High rhythm rate range is reversed");
    if (number("minimumSemanticSubdivision", true) > number("maximumSubdivision", true))
        errors << QStringLiteral("Subdivision range is reversed");
    if (number("minimumRhythmLayerConfidence", true) > number("strongRhythmLayerConfidence", true))
        errors << QStringLiteral("Strong confidence gate is below minimum gate");
    const auto specs = config.value("stable").toObject().value("windowSpecs").toArray();
    if (specs.isEmpty() || specs.size() > 8)
        errors << QStringLiteral("Provide 1–8 analysis windows");
    for (auto value : specs)
    {
        auto pair = value.toArray();
        if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble() || !std::isfinite(pair[0].toDouble())
            || !std::isfinite(pair[1].toDouble()) || pair[0].toDouble() < number("minimumWindowSeconds", true)
            || pair[0].toDouble() > 300 || pair[1].toDouble() <= 0 || pair[1].toDouble() > pair[0].toDouble())
            errors << QStringLiteral("Invalid analysis window duration/hop");
    }
    return errors;
}
QStringList validateRequest(const QJsonObject &config, double startMs, double durationMs)
{
    auto errors = validateConfig(config);
    if (!std::isfinite(startMs) || !std::isfinite(durationMs) || startMs < 0 || durationMs <= 0)
        errors << QStringLiteral("Analysis interval must be finite, positive and within audio time");
    if (durationMs < config.value("debug").toObject().value("minimumWindowSeconds").toDouble() * 1000)
        errors << QStringLiteral(
            "The selected interval is shorter than the minimum analysis window. Extend it explicitly.");
    if (!errors.isEmpty())
        return errors;
    const auto stable = config.value("stable").toObject();
    double windows = 0, seconds = durationMs / 1000;
    for (auto v : stable.value("windowSpecs").toArray())
    {
        auto pair = v.toArray();
        windows += std::ceil(seconds / pair[1].toDouble());
    }
    if (stable.value("enableFineTempoTracking").toBool())
        windows += std::ceil(seconds / stable.value("fineTempoHopSeconds").toDouble());
    if (windows > 20000)
        errors << QStringLiteral("Requested schedule exceeds 20,000 analysis windows. Increase hops or select a "
                                 "smaller interval; no partial analysis will run.");
    return errors;
}
bool importConfig(const QJsonObject &input, QJsonObject &out, QStringList &report)
{
    report.clear();
    if (input.value("schemaVersion").toInt(-1) != 1)
    {
        report << QStringLiteral("Unsupported profile schemaVersion; profile unchanged");
        return false;
    }
    auto candidate = defaultConfig();
    QJsonObject inactive;
    for (const auto *key : {"stable", "debug"})
    {
        if (!input.value(key).isObject())
        {
            report << QStringLiteral("Missing %1 section").arg(QLatin1String(key));
            return false;
        }
        auto section = candidate.value(key).toObject();
        const auto imported = input.value(key).toObject();
        const bool wrongCore = QLatin1String(key) == QLatin1String("debug")
                               && input.value("algorithmVersion").toString() != AutoTiming2Bridge::algorithmVersion();
        for (auto it = imported.begin(); it != imported.end(); ++it)
        {
            if (wrongCore || !section.contains(it.key()))
            {
                inactive[QString::fromLatin1(key) + '.' + it.key()] = it.value();
                report << QStringLiteral("Inactive field: %1.%2%3")
                              .arg(QLatin1String(key), it.key(),
                                   wrongCore ? QStringLiteral(" (different Core build)") : QString());
            }
            else
                section[it.key()] = it.value();
        }
        candidate[key] = section;
    }
    const auto errors = validateConfig(candidate);
    if (!errors.isEmpty())
    {
        report.append(errors);
        return false;
    }
    if (!inactive.isEmpty())
        candidate["inactiveImport"] =
            QJsonObject{{"algorithmVersion", input.value("algorithmVersion")}, {"fields", inactive}};
    out = candidate;
    return true;
}
QString configHash(const QJsonObject &config)
{
    auto snapshot = config;
    snapshot.remove("inactiveImport");
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(snapshot).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}
AutoTiming2Options configOptions(const QJsonObject &config)
{
    const auto stable = config.value("stable").toObject(), debug = config.value("debug").toObject();
    AutoTiming2Options o;
    o.preferLocalTempoEvidence = stable.value("preferLocalTempoEvidence").toBool();
    o.enableComplexSubdivisionAnalysis = stable.value("enableComplexSubdivisionAnalysis").toBool();
    o.minimumTempoBpm = stable.value("minimumTempoBpm").toDouble();
    o.maximumTempoBpm = stable.value("maximumTempoBpm").toDouble();
    o.minimumWindowSeconds = debug.value("minimumWindowSeconds").toDouble();
    o.anchorReliabilityThreshold = debug.value("anchorReliabilityThreshold").toDouble();
    o.maximumTrackedGapSeconds = stable.value("maximumTrackedGapSeconds").toDouble();
    o.tempoMapMaximumTimeErrorMilliseconds = stable.value("tempoMapMaximumTimeErrorMilliseconds").toDouble();
    o.tempoMapMaximumEntries = qsizetype(stable.value("tempoMapMaximumEntries").toDouble());
    for (auto v : stable.value("windowSpecs").toArray())
    {
        auto a = v.toArray();
        o.windowSpecs.append({a[0].toDouble(), a[1].toDouble()});
    }
    o.internalOptions = debug;
    for (const auto *key : {"enableFineTempoTracking", "fineTempoWindowSeconds", "fineTempoHopSeconds"})
        o.internalOptions[key] = stable.value(key);
    return o;
}
bool saveJson(const QString &path, const QJsonObject &data, QString *error)
{
    QSaveFile file(path);
    auto bytes = QJsonDocument(data).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
} // namespace analysis
