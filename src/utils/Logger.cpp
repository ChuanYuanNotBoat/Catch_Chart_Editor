#include "Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

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

namespace
{
    QString levelPrefix(Logger::Level level)
    {
        switch (level)
        {
        case Logger::Debug:
            return "[DEBUG] ";
        case Logger::Info:
            return "[INFO] ";
        case Logger::Warning:
            return "[WARN] ";
        case Logger::Error:
        default:
            return "[ERROR] ";
        }
    }

    Logger::Level qtTypeToLevel(QtMsgType type)
    {
        switch (type)
        {
        case QtDebugMsg:
            return Logger::Debug;
        case QtInfoMsg:
            return Logger::Info;
        case QtWarningMsg:
            return Logger::Warning;
        case QtCriticalMsg:
        case QtFatalMsg:
        default:
            return Logger::Error;
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
} // namespace

void Logger::init(const QString &logsDir)
{
    QMutexLocker locker(&m_mutex);

    if (s_initialized)
        return;

#ifdef Q_OS_WIN
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    s_logsDir = logsDir;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString logDirPath = appDir + "/" + logsDir;

    QDir logDir(logDirPath);
    if (!logDir.exists() && !logDir.mkpath("."))
    {
        std::cerr << "Failed to create log directory: " << logDirPath.toStdString() << std::endl;
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    const QString logFilePath = logDirPath + "/CatchEditor_" + timestamp + ".log";

    m_file.setFileName(logFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    {
        std::cerr << "Failed to open log file: " << logFilePath.toStdString() << std::endl;
        return;
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

    if (level == Debug && !s_verbose)
        return;

    if (!m_file.isOpen())
    {
        qDebug() << message;
        return;
    }

    rotateLogIfNeeded();

    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    const QString levelStr = levelPrefix(level);

    m_stream << timestamp << " " << levelStr << message << Qt::endl;
    m_stream.flush();

    std::cout << timestamp.toUtf8().constData() << " "
              << levelStr.toUtf8().constData() << message.toUtf8().constData()
              << std::endl;
}

void Logger::debug(const QString &msg)
{
    log(Debug, msg);
}

void Logger::info(const QString &msg)
{
    log(Info, msg);
}

void Logger::warn(const QString &msg)
{
    log(Warning, msg);
}

void Logger::error(const QString &msg)
{
    log(Error, msg);
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
    m_file.close();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString logDirPath = appDir + "/" + s_logsDir;
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
    case Debug:
        levelStr = "DEBUG";
        break;
    case Info:
        levelStr = "INFO";
        break;
    case Warning:
        levelStr = "WARN";
        break;
    case Error:
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
