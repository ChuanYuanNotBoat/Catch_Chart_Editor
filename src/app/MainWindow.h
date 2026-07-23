#pragma once

#include <QMainWindow>
#include <QVariantMap>

class ChartController;
class SelectionController;
class PlaybackController;
class Skin;
class RightPanel;
class LeftPanel;
class QSplitter;
class QMenu;
class QKeySequence;
class QCloseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class ChartCanvas;
class NoteEditPanel;
class BPMTimePanel;
class MetaEditPanel;
class QToolBar;
class QAction;
class QWidget;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ChartController *chartCtrl,
                        SelectionController *selCtrl,
                        PlaybackController *playCtrl,
                        Skin *skin,
                        QWidget *parent = nullptr);
    ~MainWindow();

    void setSkin(Skin *skin);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newChart();
    void openChart();
    void openFolder();
    void openImportedLibrary();
    void saveChart();
    void saveChartAs();
    void exportMcz();
    void exportMczPure();
    void undo();
    void redo();
    void toggleColorMode(bool on);
    void toggleTimelineDivisionColorMode(bool on);
    void openTimelineDivisionColorSettings();
    void toggleHyperfruitMode(bool on);
    void toggleVerticalFlip(bool flipped);
    void togglePlayback();
    void changeSkin(const QString &skinName);
    void switchDifficulty();
    void adjustNoteSize();
    void adjustNoteSoundVolume();
    void calibrateSkin();
    void configureOutline();
    void openSessionSettings();
    void openLogSettings();
    void openPluginManager();
    void triggerPluginToolAction();
    void triggerPluginQuickAction(const QString &pluginId, const QString &actionId);
    void triggerPluginPanelAction();
    void togglePluginEnhancedToolMode(bool enabled);
    void exportDiagnosticsReport();
    void togglePaste288Division(bool enabled);
    void changeNoteSound(const QString &soundPath);
    void changeLanguage();
    void configureShortcuts();
    void checkForUpdates();
    void showHelpPage();
    void showAboutPage();
    void showVersionPage();
    void showLogsPage();

private:
    void setupUi();
    void createMenus();
    void createCentralArea();
    void retranslateUi();
    void populateSkinMenu();
    void populateNoteSoundMenu();
    void populatePluginToolsMenu();
    void populatePluginPanelsMenu();
    void refreshPluginUiExtensions();
    bool runPluginActionWithMeta(const QVariantMap &meta);
    void closePluginPanels(const QString &reasonText = QString());
    bool confirmSaveIfModified(const QString &reasonText);
    void loadChartFile(const QString &filePath);
    void persistRecoveryState();
    void tryRecoverPreviousSession();
    void clearWorkingCopySession(bool removeWorkingFile);
    void setupAutoSaveTimer();
    void performAutoSaveTick();
    QString selectChartFromList(const QList<QPair<QString, QString>> &charts, const QString &title);
    QString selectChartFromFolder(const QString &rootDir,
                                  const QList<QPair<QString, QString>> &charts,
                                  const QString &title);
    QString selectChartFromLibrary(const QString &libraryRoot, const QString &preferredSong = QString());
    void registerShortcutAction(QAction *action, const QString &actionId, const QKeySequence &defaultShortcut);
    void updatePlaybackAvailability(bool canPlay);
    QString beatmapRootPath() const; // Return beatmap root directory.
    void exportMczInternal(bool pureMode);
    void showInfoCenter(int initialTab);
    void applySidebarTheme();
    void showEditorPanel(QWidget *panel);

    class Private;
    Private *d;
};
