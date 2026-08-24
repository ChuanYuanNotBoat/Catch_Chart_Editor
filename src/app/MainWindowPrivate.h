#pragma once

#include "MainWindow.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <QKeySequence>
#include <QSet>
#include <QString>
#include <QByteArray>
#include <QVariantMap>
#include <limits>

class ChartController;
class SelectionController;
class PlaybackController;
class Skin;
class LeftPanel;
class DensityCurve;
class QWidget;
class NoteEditPanel;
class BPMTimePanel;
class MetaEditPanel;
class QAction;
class QActionGroup;
class QMenu;
class ChartCanvas;
class QToolBar;
class QTimer;
class RealtimePreviewWidget;

namespace ads
{
class CDockManager;
class CDockWidget;
}

class MainWindow::Private
{
public:
    ChartController *chartController = nullptr;
    SelectionController *selectionController = nullptr;
    PlaybackController *playbackController = nullptr;
    Skin *skin = nullptr;
    ChartCanvas *canvas = nullptr;
    RealtimePreviewWidget *previewWidget = nullptr;
    DensityCurve *rightDensityBar = nullptr;
    ads::CDockManager *dockManager = nullptr;
    ads::CDockWidget *workspaceDock = nullptr;
    ads::CDockWidget *leftPanelDock = nullptr;
    ads::CDockWidget *previewDock = nullptr;
    ads::CDockWidget *notePanelDock = nullptr;
    ads::CDockWidget *bpmPanelDock = nullptr;
    ads::CDockWidget *metaPanelDock = nullptr;
    QByteArray defaultDockLayoutState;
    NoteEditPanel *notePanel = nullptr;
    BPMTimePanel *bpmPanel = nullptr;
    MetaEditPanel *metaPanel = nullptr;
    LeftPanel *leftPanel = nullptr;
    QAction *undoAction = nullptr;
    QAction *redoAction = nullptr;
    QAction *deleteAction = nullptr;
    QAction *paste288Action = nullptr;
    QAction *colorAction = nullptr;
    QAction *timelineDivisionColorAction = nullptr;
    QAction *timelineDivisionColorSettingsAction = nullptr;
    QAction *hyperfruitAction = nullptr;
    QAction *verticalFlipAction = nullptr;
    QAction *playAction = nullptr;
    QActionGroup *speedActionGroup = nullptr;
    QMenu *skinMenu = nullptr;
    QMenu *noteSoundMenu = nullptr;
    QMenu *helpMenu = nullptr;
    QMenu *pluginsMenu = nullptr;
    QMenu *pluginToolsMenu = nullptr;
    QMenu *pluginPanelsMenu = nullptr;
    QAction *pluginToolModeAction = nullptr;
    QAction *pluginToolModeToolbarAction = nullptr;
    QAction *pluginManagerToolbarAction = nullptr;
    QAction *noteSizeAction = nullptr;
    QAction *noteSoundVolumeAction = nullptr;
    QAction *calibrateSkinAction = nullptr;
    QAction *outlineAction = nullptr;
    QAction *notePanelAction = nullptr;
    QAction *bpmPanelAction = nullptr;
    QAction *metaPanelAction = nullptr;
    QAction *curvePanelAction = nullptr;
    QAction *checkUpdatesAction = nullptr;
    QAction *helpDocAction = nullptr;
    QAction *aboutAction = nullptr;
    QAction *versionAction = nullptr;
    QAction *logsAction = nullptr;
    QToolBar *mainToolBar = nullptr;
    QToolBar *pluginToolBar = nullptr;
    QTimer *autoSaveTimer = nullptr;
    bool compactUiMode = false;
    QMenu *languageMenu = nullptr;
    QActionGroup *languageActionGroup = nullptr;
    QList<QAction *> pluginToolbarActions;
    QHash<QString, QVariantMap> pluginActionMeta;
    QHash<QString, QPointer<ads::CDockWidget>> pluginPanelDocks;
    QSet<QString> batchEditDisabledActions;
    QString pluginToolModePluginId;
    QHash<QString, QAction *> shortcutActions;
    QHash<QString, QKeySequence> shortcutDefaults;
    QList<QString> shortcutActionOrder;

    QString currentChartPath;
    QString sourceChartPath;
    QString workingChartPath;
    bool isModified = false;
    bool audioPlaybackReady = false;

    // Cached resource paths for detecting changes after undo/redo/plugin edits.
    QString lastLoadedAudioFile;
    QString lastLoadedBackgroundFile;

    // Density bar scrub state: preview updates canvas only; commit seeks audio once.
    bool densitySeekGestureActive = false;
    double densityPendingSeekMs = std::numeric_limits<double>::quiet_NaN();
};
