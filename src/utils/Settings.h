#pragma once

#include <QSettings>
#include <QKeySequence>
#include <QColor>
#include <QList>

class Settings
{
public:
    static Settings &instance();

    QString lastOpenPath() const;
    void setLastOpenPath(const QString &path);

    QString lastProjectPath() const;
    void setLastProjectPath(const QString &path);
    QString defaultBeatmapPath() const;

    bool colorNoteEnabled() const;
    void setColorNoteEnabled(bool enabled);
    bool timelineDivisionColorEnabled() const;
    void setTimelineDivisionColorEnabled(bool enabled);
    QString timelineDivisionColorPreset() const;
    void setTimelineDivisionColorPreset(const QString &preset);
    QList<int> timelineDivisionColorCustomDivisions() const;
    void setTimelineDivisionColorCustomDivisions(const QList<int> &divisions);

    bool hyperfruitOutlineEnabled() const;
    void setHyperfruitOutlineEnabled(bool enabled);

    double playbackSpeed() const;
    void setPlaybackSpeed(double speed);

    int audioLatency() const;
    void setAudioLatency(int latency);
    int globalAudioOffset() const;
    void setGlobalAudioOffset(int offset);
    bool audioCorrectionEnabled() const;
    void setAudioCorrectionEnabled(bool enabled);
    QString noteSoundPath() const;
    void setNoteSoundPath(const QString &path);
    int noteSoundVolume() const;
    void setNoteSoundVolume(int volume);

    QString currentSkin() const;
    void setCurrentSkin(const QString &skinName);

    int noteSize() const;
    void setNoteSize(int size);

    int outlineWidth() const;
    void setOutlineWidth(int width);
    QColor outlineColor() const;
    void setOutlineColor(const QColor &color);

    QString language() const;
    void setLanguage(const QString &languageCode);

    bool verticalFlip() const;
    void setVerticalFlip(bool flipped);

    QKeySequence shortcut(const QString &action) const;
    void setShortcut(const QString &action, const QKeySequence &seq);

    bool pasteUse288Division() const;
    void setPasteUse288Division(bool enabled);

    bool backgroundImageEnabled() const;
    void setBackgroundImageEnabled(bool enabled);

    int backgroundImageBrightness() const;
    void setBackgroundImageBrightness(int brightness);

    QColor backgroundColor() const;
    void setBackgroundColor(const QColor &color);

    QStringList disabledPluginIds() const;
    void setDisabledPluginIds(const QStringList &pluginIds);

    bool autoSaveEnabled() const;
    void setAutoSaveEnabled(bool enabled);
    int autoSaveIntervalSec() const;
    void setAutoSaveIntervalSec(int seconds);

    bool qtMessageFilterEnabled() const;
    void setQtMessageFilterEnabled(bool enabled);
    QStringList qtMessageFilterCategories() const;
    void setQtMessageFilterCategories(const QStringList &categories);
    QStringList qtMessageFilterPrefixes() const;
    void setQtMessageFilterPrefixes(const QStringList &prefixes);
    bool playbackStutterProbeEnabled() const;
    void setPlaybackStutterProbeEnabled(bool enabled);
    int playbackFrameRateCap() const;
    void setPlaybackFrameRateCap(int fpsCap);

    int chartPickerPrimaryColumnWidth() const;
    void setChartPickerPrimaryColumnWidth(int width);

    int coordinateMode() const;
    void setCoordinateMode(int mode);

private:
    Settings();
    QSettings m_settings;
};
