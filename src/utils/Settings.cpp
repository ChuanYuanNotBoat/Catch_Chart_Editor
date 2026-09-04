#include "Settings.h"
#include "PlaybackSpeed.h"
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QtGlobal>
#include <QStringList>
#include <algorithm>

namespace
{
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

Settings::Settings() : m_settings("CatchEditor", "CatchChartEditor") {}

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

bool Settings::floatingToolWindowsEnabled() const
{
    return m_settings.value("ui/floatingToolWindowsEnabled", true).toBool();
}

void Settings::setFloatingToolWindowsEnabled(bool enabled)
{
    m_settings.setValue("ui/floatingToolWindowsEnabled", enabled);
}
