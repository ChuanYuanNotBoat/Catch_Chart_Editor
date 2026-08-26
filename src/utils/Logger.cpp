#include "Logger.h"

#include <Windows.h>
#include <iomanip>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

QFile Logger::m_file;
QTextStream Logger::m_stream;
QMutex Logger::m_mutex;
QtMessageHandler Logger::m_previousHandler = nullptr;
bool Logger::s_initialized = false;
bool Logger::s_verbose = false;
QString Logger::s_logsDir = "logs";

bool Logger::s_jsonLoggingEnabled = false;
QFile Logger::m_jsonFile;
QTextStream Logger::m_jsonStream;

bool Logger::s_qtMessageFilterEnabled = false;
QStringList Logger::s_qtMessageFilterCategories;
QStringList Logger::s_qtMessageFilterPrefixes;

#ifdef _WIN32
    HANDLE Logger::hConsole = nullptr;
    WORD Logger::defaultColor = 0;
#else
    void* Logger::hConsole = nullptr;
    unsigned short Logger::defaultColor = 0;
#endif

namespace
{
    QString levelPrefix(Logger::Level level)
    {
        switch (level)
        {
        case Logger::DEBUG:
            return "[DEBUG] ";
        case Logger::INFO:
            return "[INFO] ";
        case Logger::WARN:
            return "[WARN] ";
        case Logger::ERR:
        default:
            return "[ERROR] ";
        }
    }

    Logger::Level qtTypeToLevel(QtMsgType type)
    {
        switch (type)
        {
        case QtDebugMsg:
            return Logger::DEBUG;
        case QtInfoMsg:
            return Logger::INFO;
        case QtWarningMsg:
            return Logger::WARN;
        case QtCriticalMsg:
        case QtFatalMsg:
        default:
            return Logger::ERR;
        }
    }

    bool shouldSuppressQtMessage(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &msg,
                                 bool filterEnabled,
                                 const QStringList &categories,
                                 const QStringList &prefixes)
    {
        if (!filterEnabled)
            return false;
        if (type == QtCriticalMsg || type == QtFatalMsg)
            return false;

        const QString category = context.category ? QString::fromUtf8(context.category).trimmed() : QString();
        const QString trimmedMsg = msg.trimmed();

        for (const QString &pattern : categories)
        {
            if (category.compare(pattern, Qt::CaseInsensitive) == 0)
                return true;
        }

        for (const QString &prefix : prefixes)
        {
            if (trimmedMsg.startsWith(prefix, Qt::CaseInsensitive))
                return true;
        }

        return false;
    }

    /**
     * @brief 获取日志级别对应的字符串和控制台颜色
     * @param level 日志级别
     * @return std::pair<std::string, ConsoleColor> 日志级别字符串, 对应的控制台颜色
     */
    std::pair<std::string, Logger::ConsoleColor> getLevelInfo(Logger::Level level)
    {
        switch (level)
        {
        case Logger::Level::DEBUG:
            return { "DEBUG", Logger::ConsoleColor::BLUE };
        case Logger::Level::INFO:
            return { "INFO", Logger::ConsoleColor::GREEN };
        case Logger::Level::WARN:
            return { "WARN", Logger::ConsoleColor::YELLOW };
        case Logger::Level::ERR:
            return { "ERROR", Logger::ConsoleColor::RED };
        default:
            return { "-", Logger::ConsoleColor::DEFAULT };
        }
    }

} // namespace

void Logger::init(const QString &logsDir)
{
    QMutexLocker locker(&m_mutex);

    if (s_initialized)
        return;

#ifdef Q_OS_WIN
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // QuickEdit/select mode suspends a console writer while text is selected.
    // Keep stdout available for benchmark automation without allowing a click
    // in the developer console to freeze the editor's GUI and playback clocks.
    const HANDLE consoleInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD consoleMode = 0;
    if (consoleInput && consoleInput != INVALID_HANDLE_VALUE &&
        GetConsoleMode(consoleInput, &consoleMode))
    {
        consoleMode |= ENABLE_EXTENDED_FLAGS;
        consoleMode &= ~ENABLE_QUICK_EDIT_MODE;
        SetConsoleMode(consoleInput, consoleMode);
    }
#endif

    s_logsDir = logsDir;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    const QString logFileName = "/CatchEditor_" + timestamp + ".log";

    // 先尝试在 appDir 下创建日志文件（开发构建场景）
    QString logDirPath = appDir + "/" + logsDir;
    QDir logDir(logDirPath);
    logDir.mkpath(".");  // 尝试创建目录，忽略返回值

    QString logFilePath = logDirPath + logFileName;
    m_file.setFileName(logFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    {
        // 回退到用户 AppData 目录（解决安装到 Program Files 后无写入权限的问题）
        const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!appDataPath.isEmpty())
        {
            logDirPath = appDataPath + "/" + logsDir;
            logDir.setPath(logDirPath);
            logDir.mkpath(".");
            logFilePath = logDirPath + logFileName;
            m_file.close();
            m_file.setFileName(logFilePath);
        }
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
        {
            std::cerr << "Failed to open log file: " << logFilePath.toStdString() << std::endl;
            return;
        }
    }

    m_stream.setDevice(&m_file);
    m_stream << "======================================" << Qt::endl;
    m_stream << "Log started at " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << Qt::endl;
    m_stream << "Application: " << QCoreApplication::applicationName() << Qt::endl;
    m_stream << "Version: " << QCoreApplication::applicationVersion() << Qt::endl;
    m_stream << "======================================" << Qt::endl;
    m_stream.flush();

    if (s_jsonLoggingEnabled)
    {
        const QString jsonLogPath = logDirPath + "/CatchEditor_" + timestamp + ".jsonl";
        m_jsonFile.setFileName(jsonLogPath);
        if (m_jsonFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
        {
            m_jsonStream.setDevice(&m_jsonFile);
            m_jsonStream.flush();
        }
    }

#ifdef _WIN32
    Logger::hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (Logger::hConsole && Logger::hConsole != INVALID_HANDLE_VALUE)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(Logger::hConsole, &csbi))
            Logger::defaultColor = csbi.wAttributes;
    }
#endif

    m_previousHandler = qInstallMessageHandler(qtMessageHandler);
    s_initialized = true;

    std::cout << "Log file created: " << logFilePath.toUtf8().constData() << std::endl;
}

void Logger::shutdown()
{
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen())
    {
        m_stream << "======================================" << Qt::endl;
        m_stream << "Log ended at " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << Qt::endl;
        m_stream << "======================================" << Qt::endl;
        m_stream.flush();
        m_file.close();
    }

    if (m_jsonFile.isOpen())
        m_jsonFile.close();

    if (m_previousHandler)
        qInstallMessageHandler(m_previousHandler);
}

QString Logger::logFilePath()
{
    return m_file.fileName();
}

bool Logger::isInitialized()
{
    return s_initialized;
}

void Logger::setVerbose(bool verbose)
{
    QMutexLocker locker(&m_mutex);
    s_verbose = verbose;
}

bool Logger::isVerbose()
{
    return s_verbose;
}

void Logger::log(Level level, const QString &message)
{
    QMutexLocker locker(&m_mutex);

    if (level == DEBUG && !s_verbose)
        return;

    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    const QString levelStr = levelPrefix(level);

    // 文件写入（仅在日志文件已打开时执行）
    if (m_file.isOpen())
    {
        rotateLogIfNeeded();
        m_stream << timestamp << " " << levelStr << message << Qt::endl;
        m_stream.flush();
    }

    // 控制台输出（始终执行，即使日志文件未打开也保留等级标签和颜色）
    std::pair<std::string, ConsoleColor> p = getLevelInfo(level);

    std::cout << "[" << timestamp.toUtf8().constData() << "]";

    setColor(p.second);
	std::cout << levelStr.toUtf8().constData();
    resetColor();

	std::cout << message.toUtf8().constData() << std::endl;
}

void Logger::debug(const QString &msg)
{
    log(DEBUG, msg);
}

void Logger::info(const QString &msg)
{
    log(INFO, msg);
}

void Logger::warn(const QString &msg)
{
    log(WARN, msg);
}

void Logger::error(const QString &msg)
{
    log(ERR, msg);
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QtMessageHandler previousHandler = nullptr;
    QString formattedMsg = msg;
    const Level level = qtTypeToLevel(type);
    {
        QMutexLocker locker(&m_mutex);

        if (shouldSuppressQtMessage(type,
                                    context,
                                    msg,
                                    s_qtMessageFilterEnabled,
                                    s_qtMessageFilterCategories,
                                    s_qtMessageFilterPrefixes))
            return;

        if (context.file && context.line > 0)
            formattedMsg = QString("[%1:%2] %3").arg(context.file).arg(context.line).arg(msg);

        if (m_file.isOpen())
        {
            const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
            const QString levelStr = levelPrefix(level);
            m_stream << timestamp << " " << levelStr << formattedMsg << Qt::endl;
            m_stream.flush();
        }

        previousHandler = m_previousHandler;
    }

    std::cerr << formattedMsg.toUtf8().constData() << std::endl;

    if (previousHandler)
        previousHandler(type, context, msg);
}

void Logger::rotateLogIfNeeded()
{
    if (!m_file.isOpen())
        return;

    if (m_file.size() < MAX_LOG_FILE_SIZE)
        return;

    m_stream << "======================================" << Qt::endl;
    m_stream << "Log rotated at " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << Qt::endl;
    m_stream << "======================================" << Qt::endl;
    m_stream.flush();

    // 复用当前日志文件所在的目录（init() 中已确保可写），避免在 Program Files 下无写入权限
    const QString logDirPath = QFileInfo(m_file).absolutePath();
    m_file.close();

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz");
    const QString newLogFilePath = logDirPath + "/CatchEditor_" + timestamp + ".log";

    m_file.setFileName(newLogFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    {
        std::cerr << "Failed to open new log file: " << newLogFilePath.toUtf8().constData() << std::endl;
        return;
    }

    m_stream.setDevice(&m_file);
    m_stream << "======================================" << Qt::endl;
    m_stream << "Log rotated from previous file" << Qt::endl;
    m_stream << "Rotated at " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << Qt::endl;
    m_stream << "======================================" << Qt::endl;
    m_stream.flush();

    std::cout << "Log file rotated to: " << newLogFilePath.toUtf8().constData() << std::endl;
}

void Logger::setJsonLoggingEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    s_jsonLoggingEnabled = enabled;
}

bool Logger::isJsonLoggingEnabled()
{
    return s_jsonLoggingEnabled;
}

QString Logger::jsonLogFilePath()
{
    return m_jsonFile.fileName();
}

void Logger::setQtMessageFilterEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    s_qtMessageFilterEnabled = enabled;
}

bool Logger::isQtMessageFilterEnabled()
{
    QMutexLocker locker(&m_mutex);
    return s_qtMessageFilterEnabled;
}

void Logger::setQtMessageFilterCategories(const QStringList &categories)
{
    QMutexLocker locker(&m_mutex);
    QStringList cleaned;
    for (const QString &entry : categories)
    {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed))
            cleaned.append(trimmed);
    }
    s_qtMessageFilterCategories = cleaned;
}

QStringList Logger::qtMessageFilterCategories()
{
    QMutexLocker locker(&m_mutex);
    return s_qtMessageFilterCategories;
}

void Logger::setQtMessageFilterPrefixes(const QStringList &prefixes)
{
    QMutexLocker locker(&m_mutex);
    QStringList cleaned;
    for (const QString &entry : prefixes)
    {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed))
            cleaned.append(trimmed);
    }
    s_qtMessageFilterPrefixes = cleaned;
}

QStringList Logger::qtMessageFilterPrefixes()
{
    QMutexLocker locker(&m_mutex);
    return s_qtMessageFilterPrefixes;
}

void Logger::logStructured(Level level,
                           const QString &message,
                           const QString &module,
                           const QMap<QString, QString> &context)
{
    log(level, message);

    QMutexLocker locker(&m_mutex);
    if (!s_jsonLoggingEnabled || !m_jsonFile.isOpen())
        return;

    QJsonObject logEntry;
    logEntry["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    QString levelStr;
    switch (level)
    {
    case DEBUG:
        levelStr = "DEBUG";
        break;
    case INFO:
        levelStr = "INFO";
        break;
    case WARN:
        levelStr = "WARN";
        break;
    case ERR:
    default:
        levelStr = "ERROR";
        break;
    }
    logEntry["level"] = levelStr;
    logEntry["module"] = module;
    logEntry["message"] = message;

    QJsonObject contextObj;
    for (auto it = context.constBegin(); it != context.constEnd(); ++it)
        contextObj[it.key()] = it.value();
    logEntry["context"] = contextObj;

    const QJsonDocument doc(logEntry);
    m_jsonStream << doc.toJson(QJsonDocument::Compact) << "\n";
    m_jsonStream.flush();
}

void Logger::setColor(Logger::ConsoleColor color) 
{
#ifdef _WIN32
    if (hConsole && hConsole != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(static_cast<HANDLE>(hConsole), static_cast<WORD>(color));
#else
    Q_UNUSED(color);
#endif
}

void Logger::resetColor() 
{
#ifdef _WIN32
    if (hConsole && hConsole != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(static_cast<HANDLE>(hConsole), static_cast<WORD>(defaultColor));
#endif
}
