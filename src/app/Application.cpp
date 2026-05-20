#include "Application.h"
#include "MainWindow.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "plugin/PluginManager.h"
#include "model/Skin.h"
#include "file/SkinIO.h"
#include "file/ProjectIO.h"
#include "utils/Settings.h"
#include "utils/Translator.h"
#include "utils/Logger.h"
#include <QDir>
#include <QDebug>
#include <QFileInfo>

namespace
{
QStringList availableSkinBaseDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << (appDir + "/skins")
               << (appDir + "/resources/default_skin");

    QStringList result;
    for (const QString &dir : candidates)
    {
        if (dir.isEmpty() || result.contains(dir))
            continue;
        if (!QDir(dir).exists())
            continue;
        if (SkinIO::getSkinList(dir).isEmpty())
            continue;
        result.append(dir);
    }
    return result;
}

QString resolvePluginsDir()
{
    const QString envDirRaw = qEnvironmentVariable("MCCE_PLUGINS_DIR").trimmed();
    if (!envDirRaw.isEmpty())
    {
        QFileInfo envInfo(envDirRaw);
        const QString envPath = envInfo.isAbsolute()
                                    ? envInfo.absoluteFilePath()
                                    : QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(envDirRaw);
        return QDir(envPath).absolutePath();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QDir appDirObj(appDir);

    // Developer convenience: when running from a local build directory, prefer
    // repository plugins so script/manifest edits are hot-reloadable without
    // recompiling the host executable.
    const QStringList devRootCandidates = {
        appDirObj.absoluteFilePath("../.."),
        appDirObj.absoluteFilePath(".."),
    };
    for (const QString &root : devRootCandidates)
    {
        const QString cmakePath = QDir(root).filePath("CMakeLists.txt");
        const QString srcPath = QDir(root).filePath("src");
        const QString pluginsPath = QDir(root).filePath("plugins");
        if (QFileInfo::exists(cmakePath) &&
            QFileInfo(srcPath).isDir() &&
            QFileInfo(pluginsPath).isDir())
        {
            return QDir(pluginsPath).absolutePath();
        }
    }

    return QDir(appDir).filePath("plugins");
}
} // namespace

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv),
      m_chartController(nullptr),
      m_selectionController(nullptr),
      m_playbackController(nullptr),
      m_pluginManager(nullptr),
      m_pluginSystemReady(false),
      m_skin(nullptr),
      m_mainWindow(nullptr)
{
    setOrganizationName("CatchEditor");
    setApplicationName("Malody Catch Chart Editor");
}

Application::~Application()
{
    delete m_mainWindow;
    delete m_chartController;
    delete m_selectionController;
    delete m_playbackController;
    delete m_pluginManager;
}

bool Application::initialize()
{
    try
    {
        Logger::init("logs");
        Logger::info("========== Application Starting ==========");
        Logger::info(QString("Log file: %1").arg(Logger::logFilePath()));

        // 初始化 ChartFileSystem 注册表
        ProjectIO::initializeBuiltinFileTypes();

        Settings::instance();
        Logger::info("Settings loaded.");
        Logger::setQtMessageFilterEnabled(Settings::instance().qtMessageFilterEnabled());
        Logger::setQtMessageFilterCategories(Settings::instance().qtMessageFilterCategories());
        Logger::setQtMessageFilterPrefixes(Settings::instance().qtMessageFilterPrefixes());

        loadLanguage();
        Logger::info("Language loaded.");

        m_chartController = new ChartController(this);
        m_selectionController = new SelectionController(this);
        m_playbackController = new PlaybackController(new AudioPlayer(this), this);
        Logger::info("Controllers created.");

        m_skin = new Skin();
        QString skinName = Settings::instance().currentSkin();

        const QStringList baseDirs = availableSkinBaseDirs();
        Logger::info(QString("Skin base directory candidates: %1").arg(baseDirs.join(", ")));

        if (baseDirs.isEmpty())
        {
            Logger::warn("No skin directories found, using fallback colors.");
            m_skin = nullptr; // Fall back to built-in note colors.
        }
        else
        {
            bool loaded = false;
            if (!skinName.isEmpty())
            {
                for (const QString &baseDir : baseDirs)
                {
                    const QString candidatePath = baseDir + "/" + skinName;
                    if (!QDir(candidatePath).exists())
                        continue;
                    Logger::info(QString("Trying to load skin '%1' from %2").arg(skinName).arg(candidatePath));
                    if (SkinIO::loadSkin(candidatePath, *m_skin))
                    {
                        loaded = true;
                        Logger::info(QString("Skin '%1' loaded successfully").arg(skinName));
                        break;
                    }
                }
            }
            if (!loaded)
            {
                for (const QString &baseDir : baseDirs)
                {
                    const QStringList skinDirs = SkinIO::getSkinList(baseDir);
                    if (skinDirs.isEmpty())
                        continue;

                    const QString firstSkin = skinDirs.first();
                    const QString skinPath = baseDir + "/" + firstSkin;
                    Logger::info(QString("Loading first skin: %1 from %2").arg(firstSkin).arg(skinPath));
                    if (!SkinIO::loadSkin(skinPath, *m_skin))
                        continue;

                    loaded = true;
                    Logger::info(QString("Skin '%1' loaded successfully").arg(firstSkin));
                    Settings::instance().setCurrentSkin(firstSkin);
                    Logger::info(QString("Set default skin to %1").arg(firstSkin));
                    break;
                }
            }

            if (!loaded)
            {
                Logger::error("Failed to load any skin, using fallback colors.");
                m_skin = nullptr;
            }
        }

        m_mainWindow = new MainWindow(m_chartController, m_selectionController, m_playbackController, m_skin);
        // Skin ownership is transferred to MainWindow.
        m_skin = nullptr;
        m_mainWindow->show();
        Logger::info("Main window created and shown.");

        m_pluginManager = new PluginManager(this);
        const QString pluginsDir = resolvePluginsDir();
        if (pluginsDir.isEmpty())
        {
            Logger::error("Failed to resolve writable plugins directory.");
            m_pluginSystemReady = false;
        }
        else
        {
            if (!QDir().mkpath(pluginsDir))
                Logger::warn(QString("Failed to create plugins directory: %1").arg(pluginsDir));
            Logger::info(QString("Plugin directory: %1").arg(QDir(pluginsDir).absolutePath()));
            Logger::info(QString("Plugin hot-reload mode: edit files under this directory and click Reload Plugins."));
            m_pluginManager->loadPlugins(pluginsDir, m_mainWindow);
            m_pluginSystemReady = true;
            Logger::info("Plugins loaded.");
        }

        if (m_pluginSystemReady)
        {
            connect(m_chartController, &ChartController::chartChanged, m_pluginManager, &PluginManager::notifyChartChanged);
            connect(m_chartController, &ChartController::chartLoaded, this, [this]()
                    { m_pluginManager->notifyChartLoaded(m_chartController->chartFilePath()); });
        }

        loadLastProject();

        Logger::info("Application initialized successfully.");
        return true;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("Exception during initialization: %1").arg(e.what()));
        return false;
    }
    catch (...)
    {
        Logger::error("Unknown exception during initialization");
        return false;
    }
}

void Application::loadLastProject()
{
    QString lastPath = Settings::instance().lastOpenPath();
    if (!lastPath.isEmpty())
    {
        Logger::info(QString("Last project path: %1 (auto-load not implemented)").arg(lastPath));
    }
}

void Application::loadLanguage()
{
    Translator::instance().setLanguage(Settings::instance().language());
}
