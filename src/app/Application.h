#pragma once

#include <QApplication>

class ChartController;
class SelectionController;
class PlaybackController;
class PluginManager;
class Skin;
class MainWindow;

class Application : public QApplication
{
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    ~Application();

    ChartController *chartController() const { return m_chartController; }
    SelectionController *selectionController() const { return m_selectionController; }
    PlaybackController *playbackController() const { return m_playbackController; }
    PluginManager *pluginManager() const { return m_pluginManager; }
    bool pluginSystemReady() const { return m_pluginSystemReady; }
    Skin *skin() const { return m_skin; }
    MainWindow *mainWindow() const { return m_mainWindow; }

    bool initialize();
    void loadLastProject();
    void loadLanguage();

protected:
    bool notify(QObject *receiver, QEvent *event) override;

private:
    ChartController *m_chartController = nullptr;
    SelectionController *m_selectionController = nullptr;
    PlaybackController *m_playbackController = nullptr;
    PluginManager *m_pluginManager = nullptr;
    bool m_pluginSystemReady = false;
    Skin *m_skin = nullptr;
    MainWindow *m_mainWindow = nullptr;
};
