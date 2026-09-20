#pragma once

#include <QSettings>
#include <QKeySequence>
#include <QColor>
#include <QList>
#include <QByteArray>

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

    bool rainRewardPreviewEnabled() const;
    void setRainRewardPreviewEnabled(bool enabled);

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

    int beatNumberFontSize() const;
    void setBeatNumberFontSize(int size);

    bool hasShortcut(const QString &action) const;
    QKeySequence shortcut(const QString &action) const;
    void setShortcut(const QString &action, const QKeySequence &seq);

    bool pasteUse288Division() const;
    void setPasteUse288Division(bool enabled);

    int editorTimeDivision() const;
    void setEditorTimeDivision(int division);
    int editorGridDivision() const;
    void setEditorGridDivision(int division);
    bool editorGridSnapEnabled() const;
    void setEditorGridSnapEnabled(bool enabled);
    double editorTimeScale() const;
    void setEditorTimeScale(double scale);
    int mirrorAxisX() const;
    void setMirrorAxisX(int axisX);
    bool mirrorGuideVisible() const;
    void setMirrorGuideVisible(bool visible);
    bool mirrorPreviewVisible() const;
    void setMirrorPreviewVisible(bool visible);

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

    QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray &geometry);
    QByteArray dockLayoutState() const;
    void setDockLayoutState(const QByteArray &state);
    void clearDockLayoutState();
    QByteArray classicLayoutState() const;
    void setClassicLayoutState(const QByteArray &state);
    void clearClassicLayoutState();
    QString classicRightPanelId() const;
    void setClassicRightPanelId(const QString &panelId);
    bool classicPluginToolsVisible() const;
    void setClassicPluginToolsVisible(bool visible);
    bool floatingToolWindowsEnabled() const;
    void setFloatingToolWindowsEnabled(bool enabled);

    // Portable, human-copyable settings bundle. The payload is a typed JSON
    // document wrapped in Base64 with fixed text markers.
    QString exportTransferText(QString *errorMessage = nullptr) const;
    bool importTransferText(const QString &text,
                            QString *errorMessage = nullptr,
                            int *importedSettingCount = nullptr);
    static QString transferBeginMarker();
    static QString transferEndMarker();

private:
    Settings();
    QSettings m_settings;
};
