#pragma once

#include <Windows.h>
#include <iomanip>

#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QtMessageHandler>
#include <QMap>
#include <QStringList>

class Logger
{
public:
    enum Level
    {
        DEBUG,
        INFO,
        WARN,
        ERR
    };

    enum class ConsoleColor 
    {
        WHITE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        GREEN = FOREGROUND_GREEN | FOREGROUND_INTENSITY,
        YELLOW = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
        RED = FOREGROUND_RED | FOREGROUND_INTENSITY,
        DEFAULT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        BLUE = FOREGROUND_BLUE | FOREGROUND_INTENSITY,

        // 其他颜色
        CYAN = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        MAGENTA = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        BLACK = 0,
        GRAY = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    };

    // 日志文件最大大小：5MB
    static constexpr qint64 MAX_LOG_FILE_SIZE = 5 * 1024 * 1024;
    /**
     * @brief 初始化日志系统，自动创建日志文件夹
     * 多次调用安全，仅第一次调用会真正初始化
     * @param logsDir 日志文件夹路径（相对于可执行文件目录），默认为 "logs"
     */
    static void init(const QString &logsDir = "logs");

    /**
     * @brief 关闭日志文件
     */
    static void shutdown();

    /**
     * @brief 获取日志文件路径
     */
    static QString logFilePath();

    /**
     * @brief 检查日志系统是否已初始化
     */
    static bool isInitialized();

    /**
     * @brief 设置详细日志模式（用于区分导入/非导入场景）
     * 在导入模式下（verbose=false），只输出统计日志
     * 非导入时（verbose=true），输出详细日志
     */
    static void setVerbose(bool verbose);

    /**
     * @brief 获取当前是否为详细日志模式
     */
    static bool isVerbose();

    /**
     * @brief 写入日志消息
     */
    static void log(Level level, const QString &message);
    static void debug(const QString &msg);
    static void info(const QString &msg);
    static void warn(const QString &msg);
    static void error(const QString &msg);
    static bool s_verbose;    // 详细日志模式
    static QString s_logsDir; // 日志文件夹路径

    /**
     * @brief 如果日志文件超过大小限制，自动轮换到新文件
     */
    static void rotateLogIfNeeded();

    /**
     * @brief Qt 消息处理回调（供 qInstallMessageHandler 使用）
     */
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

    // 结构化日志和诊断功能

    /**
     * @brief 启用/禁用JSON结构化日志输出
     * 启用后，日志将同时以JSON格式输出到单独的日志文件
     */
    static void setJsonLoggingEnabled(bool enabled);

    /**
     * @brief 检查JSON日志是否启用
     */
    static bool isJsonLoggingEnabled();

    /**
     * @brief 获取JSON日志文件路径
     */
    static QString jsonLogFilePath();

    static void setQtMessageFilterEnabled(bool enabled);
    static bool isQtMessageFilterEnabled();
    static void setQtMessageFilterCategories(const QStringList &categories);
    static QStringList qtMessageFilterCategories();
    static void setQtMessageFilterPrefixes(const QStringList &prefixes);
    static QStringList qtMessageFilterPrefixes();

    /**
     * @brief 输出结构化日志消息（包含额外的上下文元数据）
     * @param level 日志级别
     * @param message 日志消息
     * @param module 模块名称（如"ChartIO", "ChartController"）
     * @param context 上下文信息（自定义字段）
     */
    static void logStructured(Level level,
        const QString& message,
        const QString& module = "",
        const QMap<QString, QString>& context = QMap<QString, QString>());

    /**
	 * @brief 通过枚举预设设置控制台输出颜色
    */
    static void setColor(ConsoleColor color);

    /**
	 * @brief 重置控制台输出颜色为默认值
    */
    static void resetColor();

private:
    static QFile m_file;
    static QTextStream m_stream;
    static QMutex m_mutex;
    static QtMessageHandler m_previousHandler;
    static bool s_initialized; // 标记是否已初始化

    // JSON日志成员
    static bool s_jsonLoggingEnabled;
    static QFile m_jsonFile;
    static QTextStream m_jsonStream;
    static bool s_qtMessageFilterEnabled;
    static QStringList s_qtMessageFilterCategories;
    static QStringList s_qtMessageFilterPrefixes;

    // 控制台颜色支持成员
    static HANDLE hConsole;
    static WORD defaultColor;

};
