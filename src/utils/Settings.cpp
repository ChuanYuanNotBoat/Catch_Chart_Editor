#include "Settings.h"
#include "PlaybackSpeed.h"
#include <QDir>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStandardPaths>
#include <QtGlobal>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr auto kTransferFormat = "malody-catch-editor-settings";
    constexpr int kTransferVersion = 1;
    constexpr qsizetype kMaximumEncodedPayloadBytes = 8 * 1024 * 1024;
    constexpr qsizetype kMaximumDecodedPayloadBytes = 6 * 1024 * 1024;
    constexpr qsizetype kMaximumSettingCount = 4096;
    constexpr qsizetype kMaximumKeyLength = 1024;
    constexpr qsizetype kMaximumStringLength = 2 * 1024 * 1024;
    constexpr qsizetype kMaximumByteArrayLength = 4 * 1024 * 1024;

    QString transferError(const char *text)
    {
        return QCoreApplication::translate("Settings", text);
    }

    bool isSafeSettingsKey(const QString &key)
    {
        if (key.isEmpty() || key.size() > kMaximumKeyLength || key.startsWith('/')
            || key.endsWith('/') || key.contains(QStringLiteral("//")) || key.contains('\\'))
        {
            return false;
        }

        for (const QChar ch : key)
        {
            if (ch.unicode() < 0x20 || ch == QChar(0x7f))
                return false;
        }
        return true;
    }

    QJsonObject serializeSettingValue(const QVariant &value, bool *ok)
    {
        QJsonObject result;
        *ok = true;
        switch (value.metaType().id())
        {
        case QMetaType::Bool:
            result.insert(QStringLiteral("type"), QStringLiteral("bool"));
            result.insert(QStringLiteral("value"), value.toBool());
            break;
        case QMetaType::Int:
            result.insert(QStringLiteral("type"), QStringLiteral("int"));
            result.insert(QStringLiteral("value"), value.toInt());
            break;
        case QMetaType::UInt:
            result.insert(QStringLiteral("type"), QStringLiteral("uint"));
            result.insert(QStringLiteral("value"), static_cast<qint64>(value.toUInt()));
            break;
        case QMetaType::LongLong:
            result.insert(QStringLiteral("type"), QStringLiteral("int64"));
            result.insert(QStringLiteral("value"), QString::number(value.toLongLong()));
            break;
        case QMetaType::ULongLong:
            result.insert(QStringLiteral("type"), QStringLiteral("uint64"));
            result.insert(QStringLiteral("value"), QString::number(value.toULongLong()));
            break;
        case QMetaType::Float:
        case QMetaType::Double:
        {
            const double number = value.toDouble();
            if (!std::isfinite(number))
            {
                *ok = false;
                break;
            }
            result.insert(QStringLiteral("type"), QStringLiteral("double"));
            result.insert(QStringLiteral("value"), number);
            break;
        }
        case QMetaType::QString:
            result.insert(QStringLiteral("type"), QStringLiteral("string"));
            result.insert(QStringLiteral("value"), value.toString());
            break;
        case QMetaType::QStringList:
        {
            result.insert(QStringLiteral("type"), QStringLiteral("string-list"));
            QJsonArray strings;
            for (const QString &item : value.toStringList())
                strings.append(item);
            result.insert(QStringLiteral("value"), strings);
            break;
        }
        case QMetaType::QByteArray:
            result.insert(QStringLiteral("type"), QStringLiteral("bytes"));
            result.insert(QStringLiteral("value"),
                          QString::fromLatin1(value.toByteArray().toBase64()));
            break;
        case QMetaType::QColor:
        {
            const QColor color = value.value<QColor>();
            if (!color.isValid())
            {
                *ok = false;
                break;
            }
            result.insert(QStringLiteral("type"), QStringLiteral("color"));
            result.insert(QStringLiteral("value"), color.name(QColor::HexArgb));
            break;
        }
        default:
            *ok = false;
            break;
        }
        return result;
    }

    bool jsonInteger(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 *result)
    {
        if (!value.isDouble())
            return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::floor(number) != number
            || number < static_cast<double>(minimum) || number > static_cast<double>(maximum))
        {
            return false;
        }
        *result = static_cast<qint64>(number);
        return true;
    }

    bool decodeSettingValue(const QJsonObject &object, QVariant *result, QString *errorMessage)
    {
        const QJsonValue typeValue = object.value(QStringLiteral("type"));
        const QJsonValue value = object.value(QStringLiteral("value"));
        if (!typeValue.isString())
        {
            if (errorMessage)
                *errorMessage = transferError("A setting has no valid type tag.");
            return false;
        }

        const QString type = typeValue.toString();
        if (type == QLatin1String("bool") && value.isBool())
            *result = value.toBool();
        else if (type == QLatin1String("int"))
        {
            qint64 number = 0;
            if (!jsonInteger(value, std::numeric_limits<int>::min(),
                             std::numeric_limits<int>::max(), &number))
            {
                if (errorMessage)
                    *errorMessage = transferError("A setting contains an invalid integer.");
                return false;
            }
            *result = static_cast<int>(number);
        }
        else if (type == QLatin1String("uint"))
        {
            qint64 number = 0;
            if (!jsonInteger(value, 0, std::numeric_limits<unsigned int>::max(), &number))
            {
                if (errorMessage)
                    *errorMessage = transferError("A setting contains an invalid unsigned integer.");
                return false;
            }
            *result = static_cast<unsigned int>(number);
        }
        else if (type == QLatin1String("int64") && value.isString())
        {
            bool ok = false;
            const qlonglong number = value.toString().toLongLong(&ok);
            if (!ok)
            {
                if (errorMessage)
                    *errorMessage = transferError("A setting contains an invalid 64-bit integer.");
                return false;
            }
            *result = number;
        }
        else if (type == QLatin1String("uint64") && value.isString())
        {
            bool ok = false;
            const qulonglong number = value.toString().toULongLong(&ok);
            if (!ok)
            {
                if (errorMessage)
                    *errorMessage = transferError("A setting contains an invalid unsigned 64-bit integer.");
                return false;
            }
            *result = number;
        }
        else if (type == QLatin1String("double") && value.isDouble()
                 && std::isfinite(value.toDouble()))
            *result = value.toDouble();
        else if (type == QLatin1String("string") && value.isString()
                 && value.toString().size() <= kMaximumStringLength)
            *result = value.toString();
        else if (type == QLatin1String("string-list") && value.isArray())
        {
            const QJsonArray array = value.toArray();
            if (array.size() > kMaximumSettingCount)
            {
                if (errorMessage)
                    *errorMessage = transferError("A string-list setting is too large.");
                return false;
            }
            QStringList strings;
            strings.reserve(array.size());
            for (const QJsonValue &item : array)
            {
                if (!item.isString() || item.toString().size() > kMaximumStringLength)
                {
                    if (errorMessage)
                        *errorMessage = transferError("A string-list setting is invalid.");
                    return false;
                }
                strings.append(item.toString());
            }
            *result = strings;
        }
        else if (type == QLatin1String("bytes") && value.isString())
        {
            const QByteArray encoded = value.toString().toLatin1();
            const auto decoded = QByteArray::fromBase64Encoding(
                encoded, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || decoded.decoded.size() > kMaximumByteArrayLength)
            {
                if (errorMessage)
                    *errorMessage = transferError("A byte-array setting is invalid or too large.");
                return false;
            }
            *result = decoded.decoded;
        }
        else if (type == QLatin1String("color") && value.isString())
        {
            const QColor color(value.toString());
            if (!color.isValid())
            {
                if (errorMessage)
                    *errorMessage = transferError("A color setting is invalid.");
                return false;
            }
            *result = color;
        }
        else
        {
            if (errorMessage)
                *errorMessage = transferError("A setting contains an unsupported or invalid value.");
            return false;
        }
        return true;
    }

    bool parseTransferText(const QString &text, QVariantMap *settings, QString *errorMessage)
    {
        QString normalized = text;
        if (!normalized.isEmpty() && normalized.front() == QChar::ByteOrderMark)
            normalized.remove(0, 1);

        QStringList lines = normalized.split('\n');
        while (!lines.isEmpty() && lines.front().trimmed().isEmpty())
            lines.removeFirst();
        while (!lines.isEmpty() && lines.back().trimmed().isEmpty())
            lines.removeLast();
        if (lines.size() < 3 || lines.front().trimmed() != Settings::transferBeginMarker()
            || lines.back().trimmed() != Settings::transferEndMarker())
        {
            if (errorMessage)
                *errorMessage = transferError("The settings text has an invalid header or footer.");
            return false;
        }

        QByteArray encoded;
        for (qsizetype i = 1; i + 1 < lines.size(); ++i)
            encoded.append(lines.at(i).trimmed().toLatin1());
        if (encoded.isEmpty() || encoded.size() > kMaximumEncodedPayloadBytes)
        {
            if (errorMessage)
                *errorMessage = transferError("The settings payload is empty or too large.");
            return false;
        }

        const auto decoded = QByteArray::fromBase64Encoding(
            encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() > kMaximumDecodedPayloadBytes)
        {
            if (errorMessage)
                *errorMessage = transferError("The settings payload is not valid Base64 data.");
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(decoded.decoded, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            if (errorMessage)
                *errorMessage = transferError("The settings payload does not contain valid JSON.");
            return false;
        }

        const QJsonObject root = document.object();
        const QJsonValue version = root.value(QStringLiteral("version"));
        if (root.value(QStringLiteral("format")).toString() != QLatin1String(kTransferFormat)
            || !version.isDouble() || version.toDouble() != kTransferVersion
            || !root.value(QStringLiteral("settings")).isArray())
        {
            if (errorMessage)
                *errorMessage = transferError("This settings bundle uses an unsupported format or version.");
            return false;
        }

        const QJsonArray entries = root.value(QStringLiteral("settings")).toArray();
        if (entries.size() > kMaximumSettingCount)
        {
            if (errorMessage)
                *errorMessage = transferError("The settings bundle contains too many entries.");
            return false;
        }

        const QByteArray expectedChecksum = root.value(QStringLiteral("sha256")).toString().toLatin1();
        const QByteArray actualChecksum = QCryptographicHash::hash(
                                              QJsonDocument(entries).toJson(QJsonDocument::Compact),
                                              QCryptographicHash::Sha256)
                                              .toHex();
        if (expectedChecksum.size() != 64 || expectedChecksum != actualChecksum)
        {
            if (errorMessage)
                *errorMessage = transferError("The settings bundle checksum does not match.");
            return false;
        }

        QVariantMap parsed;
        QSet<QString> seenKeys;
        for (const QJsonValue &entryValue : entries)
        {
            if (!entryValue.isObject())
            {
                if (errorMessage)
                    *errorMessage = transferError("The settings bundle contains an invalid entry.");
                return false;
            }
            const QJsonObject entry = entryValue.toObject();
            const QJsonValue keyValue = entry.value(QStringLiteral("key"));
            if (!keyValue.isString() || !isSafeSettingsKey(keyValue.toString())
                || seenKeys.contains(keyValue.toString()))
            {
                if (errorMessage)
                    *errorMessage = transferError("The settings bundle contains an invalid or duplicate key.");
                return false;
            }

            QVariant value;
            if (!decodeSettingValue(entry, &value, errorMessage))
                return false;
            const QString key = keyValue.toString();
            seenKeys.insert(key);
            parsed.insert(key, value);
        }

        *settings = parsed;
        return true;
    }

    int sanitizePlaybackFrameRateCap(int fpsCap)
    {
        switch (fpsCap)
        {
        case 0:
        case 60:
        case 90:
        case 120:
            return fpsCap;
        default:
            return 120;
        }
    }
}

Settings::Settings()
    : m_settings(QSettings::defaultFormat(), QSettings::UserScope,
                 "CatchEditor", "CatchChartEditor")
{
}

Settings &Settings::instance()
{
    static Settings inst;
    return inst;
}

QString Settings::lastOpenPath() const
{
    return m_settings.value("lastOpenPath", "").toString();
}
void Settings::setLastOpenPath(const QString &path)
{
    m_settings.setValue("lastOpenPath", path);
}

QString Settings::lastProjectPath() const
{
    return m_settings.value("lastProjectPath", defaultBeatmapPath()).toString();
}
void Settings::setLastProjectPath(const QString &path)
{
    m_settings.setValue("lastProjectPath", path);
}

QString Settings::defaultBeatmapPath() const
{
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appDataDir.isEmpty())
        return QDir(appDataDir).filePath("beatmap");

    // Fallback for environments where AppLocalDataLocation is unavailable.
    return QDir::home().filePath("CatchChartEditor/beatmap");
}

bool Settings::colorNoteEnabled() const
{
    return m_settings.value("colorNoteEnabled", true).toBool();
}
void Settings::setColorNoteEnabled(bool enabled)
{
    m_settings.setValue("colorNoteEnabled", enabled);
}

bool Settings::timelineDivisionColorEnabled() const
{
    return m_settings.value("view/timelineDivisionColorEnabled", false).toBool();
}
void Settings::setTimelineDivisionColorEnabled(bool enabled)
{
    m_settings.setValue("view/timelineDivisionColorEnabled", enabled);
}

QString Settings::timelineDivisionColorPreset() const
{
    return m_settings.value("view/timelineDivisionColorPreset", "custom").toString();
}

void Settings::setTimelineDivisionColorPreset(const QString &preset)
{
    m_settings.setValue("view/timelineDivisionColorPreset", preset);
}

QList<int> Settings::timelineDivisionColorCustomDivisions() const
{
    const QStringList raw = m_settings.value(
                                          "view/timelineDivisionColorCustomDivisions",
                                          QStringList({"1", "2", "3", "4", "6", "8", "12", "16", "24", "32"}))
                                .toStringList();

    QList<int> out;
    out.reserve(raw.size());
    for (const QString &s : raw)
    {
        bool ok = false;
        const int v = s.toInt(&ok);
        if (ok && v > 0)
            out.append(v);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Settings::setTimelineDivisionColorCustomDivisions(const QList<int> &divisions)
{
    QList<int> cleaned;
    cleaned.reserve(divisions.size());
    for (int v : divisions)
    {
        if (v > 0)
            cleaned.append(v);
    }
    std::sort(cleaned.begin(), cleaned.end());
    cleaned.erase(std::unique(cleaned.begin(), cleaned.end()), cleaned.end());

    QStringList serialized;
    serialized.reserve(cleaned.size());
    for (int v : cleaned)
        serialized.append(QString::number(v));
    m_settings.setValue("view/timelineDivisionColorCustomDivisions", serialized);
}

bool Settings::hyperfruitOutlineEnabled() const
{
    return m_settings.value("hyperfruitOutlineEnabled", true).toBool();
}
void Settings::setHyperfruitOutlineEnabled(bool enabled)
{
    m_settings.setValue("hyperfruitOutlineEnabled", enabled);
}

bool Settings::rainRewardPreviewEnabled() const
{
    return m_settings.value("render/rainRewardPreviewEnabled", false).toBool();
}

void Settings::setRainRewardPreviewEnabled(bool enabled)
{
    m_settings.setValue("render/rainRewardPreviewEnabled", enabled);
}

double Settings::playbackSpeed() const
{
    return PlaybackSpeed::sanitize(
        m_settings.value("playbackSpeed", PlaybackSpeed::Default).toDouble());
}
void Settings::setPlaybackSpeed(double speed)
{
    m_settings.setValue("playbackSpeed", PlaybackSpeed::sanitize(speed));
}

int Settings::audioLatency() const
{
    return m_settings.value("audio/latency", 0).toInt();
}
void Settings::setAudioLatency(int latency)
{
    m_settings.setValue("audio/latency", latency);
}
int Settings::globalAudioOffset() const
{
    return m_settings.value("audio/globalOffset", 0).toInt();
}
void Settings::setGlobalAudioOffset(int offset)
{
    m_settings.setValue("audio/globalOffset", offset);
}
bool Settings::audioCorrectionEnabled() const
{
    return m_settings.value("audio/correctionEnabled", true).toBool();
}
void Settings::setAudioCorrectionEnabled(bool enabled)
{
    m_settings.setValue("audio/correctionEnabled", enabled);
}
QString Settings::noteSoundPath() const
{
    return m_settings.value("audio/noteSoundPath", "").toString();
}
void Settings::setNoteSoundPath(const QString &path)
{
    m_settings.setValue("audio/noteSoundPath", path);
}
int Settings::noteSoundVolume() const
{
    return m_settings.value("audio/noteSoundVolume", 100).toInt();
}
void Settings::setNoteSoundVolume(int volume)
{
    m_settings.setValue("audio/noteSoundVolume", qBound(0, volume, 200));
}

QString Settings::currentSkin() const
{
    return m_settings.value("currentSkin", "default").toString();
}
void Settings::setCurrentSkin(const QString &skinName)
{
    m_settings.setValue("currentSkin", skinName);
}

int Settings::noteSize() const
{
    return m_settings.value("noteSize", 16).toInt();
}
void Settings::setNoteSize(int size)
{
    m_settings.setValue("noteSize", size);
}

int Settings::outlineWidth() const
{
    return m_settings.value("outlineWidth", 1).toInt();
}
void Settings::setOutlineWidth(int width)
{
    m_settings.setValue("outlineWidth", width);
}

QColor Settings::outlineColor() const
{
    return m_settings.value("outlineColor", QColor(Qt::black)).value<QColor>();
}
void Settings::setOutlineColor(const QColor &color)
{
    m_settings.setValue("outlineColor", color);
}

QString Settings::language() const
{
    return m_settings.value("language", "en_US").toString();
}
void Settings::setLanguage(const QString &languageCode)
{
    m_settings.setValue("language", languageCode);
}

bool Settings::verticalFlip() const
{
    return m_settings.value("view/verticalFlip", true).toBool();
}
void Settings::setVerticalFlip(bool flipped)
{
    m_settings.setValue("view/verticalFlip", flipped);
}

int Settings::beatNumberFontSize() const
{
    return qBound(6, m_settings.value("view/beatNumberFontSize", 9).toInt(), 24);
}

void Settings::setBeatNumberFontSize(int size)
{
    m_settings.setValue("view/beatNumberFontSize", qBound(6, size, 24));
}

bool Settings::hasShortcut(const QString &action) const
{
    return m_settings.contains("shortcut/" + action);
}

QKeySequence Settings::shortcut(const QString &action) const
{
    return QKeySequence(m_settings.value("shortcut/" + action).toString());
}
void Settings::setShortcut(const QString &action, const QKeySequence &seq)
{
    m_settings.setValue("shortcut/" + action, seq.toString());
}

bool Settings::pasteUse288Division() const
{
    return m_settings.value("editor/pasteUse288Division", false).toBool();
}
void Settings::setPasteUse288Division(bool enabled)
{
    m_settings.setValue("editor/pasteUse288Division", enabled);
}

int Settings::editorTimeDivision() const
{
    return qBound(1, m_settings.value("editor/timeDivision", 4).toInt(), 96);
}

void Settings::setEditorTimeDivision(int division)
{
    m_settings.setValue("editor/timeDivision", qBound(1, division, 96));
}

int Settings::editorGridDivision() const
{
    return qBound(4, m_settings.value("editor/gridDivision", 20).toInt(), 64);
}

void Settings::setEditorGridDivision(int division)
{
    m_settings.setValue("editor/gridDivision", qBound(4, division, 64));
}

bool Settings::editorGridSnapEnabled() const
{
    return m_settings.value("editor/gridSnapEnabled", true).toBool();
}

void Settings::setEditorGridSnapEnabled(bool enabled)
{
    m_settings.setValue("editor/gridSnapEnabled", enabled);
}

double Settings::editorTimeScale() const
{
    const double scale = m_settings.value("editor/timeScale", 2.25).toDouble();
    return std::isfinite(scale) ? qBound(0.2, scale, 10.0) : 2.25;
}

void Settings::setEditorTimeScale(double scale)
{
    m_settings.setValue("editor/timeScale",
                        std::isfinite(scale) ? qBound(0.2, scale, 10.0) : 2.25);
}

int Settings::mirrorAxisX() const
{
    return qBound(0, m_settings.value("editor/mirrorAxisX", 256).toInt(), 512);
}

void Settings::setMirrorAxisX(int axisX)
{
    m_settings.setValue("editor/mirrorAxisX", qBound(0, axisX, 512));
}

bool Settings::mirrorGuideVisible() const
{
    return m_settings.value("editor/mirrorGuideVisible", false).toBool();
}

void Settings::setMirrorGuideVisible(bool visible)
{
    m_settings.setValue("editor/mirrorGuideVisible", visible);
}

bool Settings::mirrorPreviewVisible() const
{
    return m_settings.value("editor/mirrorPreviewVisible", false).toBool();
}

void Settings::setMirrorPreviewVisible(bool visible)
{
    m_settings.setValue("editor/mirrorPreviewVisible", visible);
}

bool Settings::backgroundImageEnabled() const
{
    return m_settings.value("view/backgroundImageEnabled", true).toBool();
}
void Settings::setBackgroundImageEnabled(bool enabled)
{
    m_settings.setValue("view/backgroundImageEnabled", enabled);
}

int Settings::backgroundImageBrightness() const
{
    return qBound(0, m_settings.value("view/backgroundImageBrightness", 100).toInt(), 200);
}

void Settings::setBackgroundImageBrightness(int brightness)
{
    m_settings.setValue("view/backgroundImageBrightness", qBound(0, brightness, 200));
}

QColor Settings::backgroundColor() const
{
    return m_settings.value("view/backgroundColor", QColor(40, 40, 40)).value<QColor>();
}
void Settings::setBackgroundColor(const QColor &color)
{
    m_settings.setValue("view/backgroundColor", color);
}

QStringList Settings::disabledPluginIds() const
{
    return m_settings.value("plugins/disabledIds", QStringList()).toStringList();
}

void Settings::setDisabledPluginIds(const QStringList &pluginIds)
{
    m_settings.setValue("plugins/disabledIds", pluginIds);
}

bool Settings::autoSaveEnabled() const
{
    return m_settings.value("editor/autoSaveEnabled", true).toBool();
}

void Settings::setAutoSaveEnabled(bool enabled)
{
    m_settings.setValue("editor/autoSaveEnabled", enabled);
}

int Settings::autoSaveIntervalSec() const
{
    return qMax(15, m_settings.value("editor/autoSaveIntervalSec", 90).toInt());
}

void Settings::setAutoSaveIntervalSec(int seconds)
{
    m_settings.setValue("editor/autoSaveIntervalSec", qMax(15, seconds));
}

bool Settings::qtMessageFilterEnabled() const
{
    return m_settings.value("logging/qtMessageFilterEnabled", false).toBool();
}

void Settings::setQtMessageFilterEnabled(bool enabled)
{
    m_settings.setValue("logging/qtMessageFilterEnabled", enabled);
}

QStringList Settings::qtMessageFilterCategories() const
{
    const QStringList raw = m_settings.value("logging/qtMessageFilterCategories", QStringList()).toStringList();
    QStringList out;
    for (const QString &entry : raw)
    {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty() && !out.contains(trimmed))
            out.append(trimmed);
    }
    return out;
}

void Settings::setQtMessageFilterCategories(const QStringList &categories)
{
    QStringList cleaned;
    for (const QString &entry : categories)
    {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed))
            cleaned.append(trimmed);
    }
    m_settings.setValue("logging/qtMessageFilterCategories", cleaned);
}

QStringList Settings::qtMessageFilterPrefixes() const
{
    const QStringList raw = m_settings.value("logging/qtMessageFilterPrefixes", QStringList()).toStringList();
    QStringList out;
    for (const QString &entry : raw)
    {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty() && !out.contains(trimmed))
            out.append(trimmed);
    }
    return out;
}

void Settings::setQtMessageFilterPrefixes(const QStringList &prefixes)
{
    QStringList cleaned;
    for (const QString &entry : prefixes)
    {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed))
            cleaned.append(trimmed);
    }
    m_settings.setValue("logging/qtMessageFilterPrefixes", cleaned);
}

bool Settings::playbackStutterProbeEnabled() const
{
    return m_settings.value("logging/playbackStutterProbeEnabled", false).toBool();
}

void Settings::setPlaybackStutterProbeEnabled(bool enabled)
{
    m_settings.setValue("logging/playbackStutterProbeEnabled", enabled);
}

int Settings::playbackFrameRateCap() const
{
    return sanitizePlaybackFrameRateCap(m_settings.value("playback/frameRateCap", 0).toInt());
}

void Settings::setPlaybackFrameRateCap(int fpsCap)
{
    m_settings.setValue("playback/frameRateCap", sanitizePlaybackFrameRateCap(fpsCap));
}

int Settings::chartPickerPrimaryColumnWidth() const
{
    return qMax(320, m_settings.value("ui/chartPickerPrimaryColumnWidth", 500).toInt());
}

void Settings::setChartPickerPrimaryColumnWidth(int width)
{
    m_settings.setValue("ui/chartPickerPrimaryColumnWidth", qBound(320, width, 2000));
}

QByteArray Settings::mainWindowGeometry() const
{
    return m_settings.value("ui/mainWindowGeometry").toByteArray();
}

void Settings::setMainWindowGeometry(const QByteArray &geometry)
{
    m_settings.setValue("ui/mainWindowGeometry", geometry);
}

QByteArray Settings::dockLayoutState() const
{
    return m_settings.value("ui/dockLayoutState").toByteArray();
}

void Settings::setDockLayoutState(const QByteArray &state)
{
    m_settings.setValue("ui/dockLayoutState", state);
}

void Settings::clearDockLayoutState()
{
    m_settings.remove("ui/dockLayoutState");
}

QByteArray Settings::classicLayoutState() const
{
    return m_settings.value("ui/classicLayout/splitterState").toByteArray();
}

void Settings::setClassicLayoutState(const QByteArray &state)
{
    m_settings.setValue("ui/classicLayout/splitterState", state);
}

void Settings::clearClassicLayoutState()
{
    m_settings.remove("ui/classicLayout");
}

QString Settings::classicRightPanelId() const
{
    const QString panelId = m_settings.value("ui/classicLayout/rightPanel", "note").toString();
    if (panelId == QLatin1String("bpm") || panelId == QLatin1String("meta"))
        return panelId;
    return QStringLiteral("note");
}

void Settings::setClassicRightPanelId(const QString &panelId)
{
    const QString normalized = (panelId == QLatin1String("bpm")
                                || panelId == QLatin1String("meta"))
                                   ? panelId
                                   : QStringLiteral("note");
    m_settings.setValue("ui/classicLayout/rightPanel", normalized);
}

bool Settings::classicPluginToolsVisible() const
{
    return m_settings.value("ui/classicLayout/pluginToolsVisible", false).toBool();
}

void Settings::setClassicPluginToolsVisible(bool visible)
{
    m_settings.setValue("ui/classicLayout/pluginToolsVisible", visible);
}

bool Settings::floatingToolWindowsEnabled() const
{
    return m_settings.value("ui/floatingToolWindowsEnabled", true).toBool();
}

void Settings::setFloatingToolWindowsEnabled(bool enabled)
{
    m_settings.setValue("ui/floatingToolWindowsEnabled", enabled);
}

QString Settings::transferBeginMarker()
{
    return QStringLiteral("-----BEGIN MALODY CATCH EDITOR SETTINGS-----");
}

QString Settings::transferEndMarker()
{
    return QStringLiteral("-----END MALODY CATCH EDITOR SETTINGS-----");
}

QString Settings::exportTransferText(QString *errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();

    const QStringList keys = m_settings.allKeys();
    if (keys.size() > kMaximumSettingCount)
    {
        if (errorMessage)
            *errorMessage = transferError("There are too many settings to export.");
        return QString();
    }

    QJsonArray entries;
    for (const QString &key : keys)
    {
        if (!isSafeSettingsKey(key))
        {
            if (errorMessage)
                *errorMessage = transferError("A stored setting has an invalid key: %1").arg(key);
            return QString();
        }

        bool ok = false;
        QJsonObject entry = serializeSettingValue(m_settings.value(key), &ok);
        if (!ok)
        {
            if (errorMessage)
            {
                *errorMessage = transferError("A stored setting uses an unsupported value type: %1")
                                    .arg(key);
            }
            return QString();
        }
        entry.insert(QStringLiteral("key"), key);
        entries.append(entry);
    }

    const QByteArray entriesJson = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kTransferFormat));
    root.insert(QStringLiteral("version"), kTransferVersion);
    root.insert(QStringLiteral("settings"), entries);
    root.insert(QStringLiteral("sha256"),
                QString::fromLatin1(QCryptographicHash::hash(
                                        entriesJson, QCryptographicHash::Sha256)
                                        .toHex()));

    const QByteArray encoded = QJsonDocument(root).toJson(QJsonDocument::Compact).toBase64();
    QStringList lines;
    lines.append(transferBeginMarker());
    constexpr qsizetype lineLength = 76;
    for (qsizetype offset = 0; offset < encoded.size(); offset += lineLength)
        lines.append(QString::fromLatin1(encoded.mid(offset, lineLength)));
    lines.append(transferEndMarker());
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool Settings::importTransferText(const QString &text,
                                  QString *errorMessage,
                                  int *importedSettingCount)
{
    if (errorMessage)
        errorMessage->clear();
    if (importedSettingCount)
        *importedSettingCount = 0;

    QVariantMap imported;
    if (!parseTransferText(text, &imported, errorMessage))
        return false;

    QVariantMap previous;
    const QStringList previousKeys = m_settings.allKeys();
    for (const QString &key : previousKeys)
        previous.insert(key, m_settings.value(key));

    m_settings.clear();
    for (auto it = imported.cbegin(); it != imported.cend(); ++it)
        m_settings.setValue(it.key(), it.value());
    m_settings.sync();

    if (m_settings.status() != QSettings::NoError)
    {
        m_settings.clear();
        for (auto it = previous.cbegin(); it != previous.cend(); ++it)
            m_settings.setValue(it.key(), it.value());
        m_settings.sync();
        if (errorMessage)
            *errorMessage = transferError("The settings store could not be updated. Previous settings were restored.");
        return false;
    }

    if (importedSettingCount)
        *importedSettingCount = imported.size();
    return true;
}
