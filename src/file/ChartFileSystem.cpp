#include "ChartFileSystem.h"
#include "utils/Logger.h"
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace ChartFileSystem
{

    // 静态成员初始化
    QHash<QString, ChartFileSystemRegistry::FileTypeEntry> ChartFileSystemRegistry::s_registeredTypes;
    static QMutex s_registryMutex;

    bool ChartFileSystemRegistry::registerFileType(const QString &extension,
                                                   const QString &description,
                                                   bool isRequired,
                                                   std::function<bool(const QString &)> validator,
                                                   int priority)
    {
        if (extension.isEmpty())
        {
            Logger::warn("ChartFileSystemRegistry::registerFileType - Extension is empty");
            return false;
        }

        QMutexLocker locker(&s_registryMutex);

        RegisteredTypeInfo info(extension, description, isRequired, priority);
        FileTypeEntry entry(info, validator);

        // 如果已存在，更新
        if (s_registeredTypes.contains(extension))
        {
            Logger::debug(QString("ChartFileSystemRegistry::registerFileType - Updating existing type: %1").arg(extension));
            s_registeredTypes[extension] = entry;
        }
        else
        {
            Logger::info(QString("ChartFileSystemRegistry::registerFileType - Registered new type: %1 (required=%2)")
                             .arg(extension)
                             .arg(isRequired ? "true" : "false"));
            s_registeredTypes[extension] = entry;
        }

        return true;
    }

    bool ChartFileSystemRegistry::unregisterFileType(const QString &extension)
    {
        if (extension.isEmpty())
            return false;

        QMutexLocker locker(&s_registryMutex);

        if (s_registeredTypes.remove(extension) != 0)
        {
            Logger::info(QString("ChartFileSystemRegistry::unregisterFileType - Unregistered type: %1").arg(extension));
            return true;
        }

        Logger::warn(QString("ChartFileSystemRegistry::unregisterFileType - Type not found: %1").arg(extension));
        return false;
    }

    bool ChartFileSystemRegistry::isAllowedFile(const QString &relativePath)
    {
        if (relativePath.isEmpty())
            return false;

        QFileInfo fi(relativePath);
        QString fileName = fi.fileName();

        QMutexLocker locker(&s_registryMutex);

        // 检查是否匹配任何已注册的扩展名
        for (auto it = s_registeredTypes.begin(); it != s_registeredTypes.end(); ++it)
        {
            const QString &extension = it.key();
            // 使用 suffix() 而不是 endsWith()，确保只匹配真正的文件后缀
            if (fi.suffix().compare(extension, Qt::CaseInsensitive) == 0 ||
                fileName.endsWith("." + extension, Qt::CaseInsensitive))
            {
                // 如果有验证器，调用验证器
                const auto &entry = it.value();
                if (entry.validator)
                {
                    if (entry.validator(relativePath))
                    {
                        return true;
                    }
                    else
                    {
                        Logger::debug(QString("ChartFileSystemRegistry::isAllowedFile - Validator rejected: %1").arg(relativePath));
                    }
                }
                else
                {
                    return true;
                }
            }
        }

        return false;
    }

    QStringList ChartFileSystemRegistry::requiredSidecarExtensions()
    {
        QMutexLocker locker(&s_registryMutex);

        QStringList required;
        for (auto it = s_registeredTypes.begin(); it != s_registeredTypes.end(); ++it)
        {
            if (it.value().info.isRequired)
            {
                required.append(it.key());
            }
        }

        // 按优先级排序
        std::sort(required.begin(), required.end(), [&](const QString &a, const QString &b)
                  {
        int prioA = s_registeredTypes.value(a).info.priority;
        int prioB = s_registeredTypes.value(b).info.priority;
        return prioA > prioB; });

        return required;
    }

    QVector<RegisteredTypeInfo> ChartFileSystemRegistry::registeredFileTypes()
    {
        QMutexLocker locker(&s_registryMutex);

        QVector<RegisteredTypeInfo> types;
        for (auto it = s_registeredTypes.begin(); it != s_registeredTypes.end(); ++it)
        {
            types.append(it.value().info);
        }

        // 按优先级排序
        std::sort(types.begin(), types.end(), [](const RegisteredTypeInfo &a, const RegisteredTypeInfo &b)
                  { return a.priority > b.priority; });

        return types;
    }

    void ChartFileSystemRegistry::clearRegistrations()
    {
        QMutexLocker locker(&s_registryMutex);

        int count = s_registeredTypes.size();
        s_registeredTypes.clear();

        Logger::info(QString("ChartFileSystemRegistry::clearRegistrations - Cleared %1 types").arg(count));
    }

    QString ChartFileSystemRegistry::chartIdentifierForPath(const QString &chartPath)
    {
        // TODO: Wave B replace with UUID
        // Wave A: 使用文件名主干作为标识
        QFileInfo fileInfo(chartPath);
        return fileInfo.baseName();
    }

} // namespace ChartFileSystem
