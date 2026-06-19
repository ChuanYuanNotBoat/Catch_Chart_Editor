#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <functional>

namespace ChartFileSystem
{

    /**
     * @brief 已注册文件类型信息
     */
    struct RegisteredTypeInfo
    {
        QString extension;   // 文件扩展名（不含点，如 "bpm_excludes.json"）
        QString description; // 描述
        bool isRequired;     // 是否必须打包
        int priority;        // 优先级（用于同扩展名多格式时的排序）

        RegisteredTypeInfo()
            : isRequired(false), priority(0)
        {
        }

        RegisteredTypeInfo(const QString &ext, const QString &desc, bool required = false, int prio = 0)
            : extension(ext), description(desc), isRequired(required), priority(prio)
        {
        }
    };

    /**
     * @brief ChartFileSystem 注册表
     *
     * 用于管理谱面相关的文件类型，替代 ProjectIO 硬编码白名单。
     * 支持运行时动态注册/注销文件类型。
     */
    class ChartFileSystemRegistry
    {
    public:
        /**
         * @brief 注册文件类型
         * @param extension 文件扩展名（不含点，如 "bpm_excludes.json"）
         * @param description 描述
         * @param isRequired 是否必须打包
         * @param validator 验证器函数（可选，用于同扩展名多格式时的验证）
         * @param priority 优先级（用于同扩展名多格式时的排序，数值越大优先级越高）
         * @return 成功返回 true
         */
        static bool registerFileType(const QString &extension,
                                     const QString &description,
                                     bool isRequired = false,
                                     std::function<bool(const QString &)> validator = nullptr,
                                     int priority = 0);

        /**
         * @brief 注销文件类型
         * @param extension 文件扩展名
         * @return 成功返回 true
         */
        static bool unregisterFileType(const QString &extension);

        /**
         * @brief 查询是否允许打包
         * @param relativePath 相对路径
         * @return 允许返回 true
         */
        static bool isAllowedFile(const QString &relativePath);

        /**
         * @brief 获取必须打包的 sidecar 扩展名列表（供 MCZ 打包强制包含）
         * @return 扩展名列表
         */
        static QStringList requiredSidecarExtensions();

        /**
         * @brief 枚举已注册类型（供 UI）
         * @return 已注册类型信息列表
         */
        static QVector<RegisteredTypeInfo> registeredFileTypes();

        /**
         * @brief 清空所有注册（测试用）
         */
        static void clearRegistrations();

        /**
         * @brief 获取谱面标识
         * TODO: Wave B replace with UUID
         * @param chartPath 谱面文件路径
         * @return 谱面标识（当前版本基于文件名）
         */
        static QString chartIdentifierForPath(const QString &chartPath);

    private:
        struct FileTypeEntry
        {
            RegisteredTypeInfo info;
            std::function<bool(const QString &)> validator;

            FileTypeEntry() {}
            FileTypeEntry(const RegisteredTypeInfo &i, std::function<bool(const QString &)> v)
                : info(i), validator(v)
            {
            }
        };

        static QHash<QString, FileTypeEntry> s_registeredTypes;
    };

} // namespace ChartFileSystem
