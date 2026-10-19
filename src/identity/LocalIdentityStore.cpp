#include "LocalIdentityStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace CceIdentity
{
namespace
{
void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool parseAuthorId(const QString &value, QString *canonical)
{
    const QUuid uuid = QUuid::fromString(value);
    if (uuid.isNull())
        return false;
    *canonical = uuid.toString(QUuid::WithoutBraces).toLower();
    return true;
}

bool validName(const QString &name, QString *normalized)
{
    *normalized = name.trimmed();
    return !normalized->isEmpty();
}
} // namespace

QJsonObject Author::snapshot() const
{
    return {{QStringLiteral("authorId"), authorId}, {QStringLiteral("authorNameSnapshot"), authorName}};
}

LocalIdentityStore::LocalIdentityStore(const QString &storageFilePath, QObject *parent)
    : QObject(parent), m_storageFilePath(storageFilePath.isEmpty()
                                             ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                                   .filePath(QStringLiteral("identities.cceidentity"))
                                             : storageFilePath)
{}

bool LocalIdentityStore::load(QString *error)
{
    if (error)
        error->clear();

    QFile file(m_storageFilePath);
    if (!file.exists())
    {
        const bool changedState = !m_identities.isEmpty() || !m_activeAuthorId.isEmpty();
        m_identities.clear();
        m_activeAuthorId.clear();
        if (changedState)
            emit changed();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error, QStringLiteral("Cannot read identity file: %1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(error, QStringLiteral("Invalid identity file: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    const QJsonValue identityArrayValue = root.value(QStringLiteral("identities"));
    const QJsonValue activeValue = root.value(QStringLiteral("activeAuthorId"));
    if (!version.isDouble() || version.toDouble() != 1.0 || !identityArrayValue.isArray() || !activeValue.isString())
    {
        setError(error, QStringLiteral("Unsupported or malformed identity file"));
        return false;
    }

    QVector<Author> loadedIdentities;
    QSet<QString> seenIds;
    const QJsonArray identityArray = identityArrayValue.toArray();
    loadedIdentities.reserve(identityArray.size());
    for (const QJsonValue &entryValue : identityArray)
    {
        if (!entryValue.isObject())
        {
            setError(error, QStringLiteral("Identity entry must be an object"));
            return false;
        }
        const QJsonObject entry = entryValue.toObject();
        const QJsonValue idValue = entry.value(QStringLiteral("authorId"));
        const QJsonValue nameValue = entry.value(QStringLiteral("authorName"));
        QString canonicalId;
        QString normalizedName;
        if (!idValue.isString() || !parseAuthorId(idValue.toString(), &canonicalId))
        {
            setError(error, QStringLiteral("Identity has an invalid authorId"));
            return false;
        }
        if (seenIds.contains(canonicalId))
        {
            setError(error, QStringLiteral("Identity file contains a duplicate authorId"));
            return false;
        }
        if (!nameValue.isString() || !validName(nameValue.toString(), &normalizedName))
        {
            setError(error, QStringLiteral("Identity name cannot be empty"));
            return false;
        }
        seenIds.insert(canonicalId);
        loadedIdentities.append({canonicalId, normalizedName});
    }

    const QString activeIdValue = activeValue.toString();
    QString canonicalActiveId;
    if (!activeIdValue.isEmpty()
        && (!parseAuthorId(activeIdValue, &canonicalActiveId) || !seenIds.contains(canonicalActiveId)))
    {
        setError(error, QStringLiteral("Active identity does not exist"));
        return false;
    }

    const bool changedState =
        m_identities.size() != loadedIdentities.size() || m_activeAuthorId != canonicalActiveId
        || !std::equal(m_identities.cbegin(), m_identities.cend(), loadedIdentities.cbegin(), loadedIdentities.cend(),
                       [](const Author &left, const Author &right) {
                           return left.authorId == right.authorId && left.authorName == right.authorName;
                       });
    m_identities = std::move(loadedIdentities);
    m_activeAuthorId = canonicalActiveId;
    if (changedState)
        emit changed();
    return true;
}

bool LocalIdentityStore::save(QString *error) const
{
    if (error)
        error->clear();

    QJsonArray identitiesArray;
    for (const Author &author : m_identities)
    {
        identitiesArray.append(QJsonObject{{QStringLiteral("authorId"), author.authorId},
                                           {QStringLiteral("authorName"), author.authorName}});
    }
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("identities"), identitiesArray},
                           {QStringLiteral("activeAuthorId"), m_activeAuthorId}};

    const QFileInfo fileInfo(m_storageFilePath);
    if (!QDir().mkpath(fileInfo.absolutePath()))
    {
        setError(error, QStringLiteral("Cannot create identity directory: %1").arg(fileInfo.absolutePath()));
        return false;
    }

    QSaveFile file(m_storageFilePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error, QStringLiteral("Cannot write identity file: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size())
    {
        setError(error, QStringLiteral("Cannot write identity file: %1").arg(file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, QStringLiteral("Cannot commit identity file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

QVector<Author> LocalIdentityStore::identities() const
{
    return m_identities;
}

Author LocalIdentityStore::activeIdentity() const
{
    for (const Author &author : m_identities)
    {
        if (author.authorId == m_activeAuthorId)
            return author;
    }
    return {};
}

bool LocalIdentityStore::persistMutation(const QVector<Author> &previousIdentities,
                                         const QString &previousActiveAuthorId, QString *error)
{
    if (!save(error))
    {
        m_identities = previousIdentities;
        m_activeAuthorId = previousActiveAuthorId;
        return false;
    }
    emit changed();
    return true;
}

bool LocalIdentityStore::createIdentity(const QString &name, QString *error)
{
    if (error)
        error->clear();
    QString normalizedName;
    if (!validName(name, &normalizedName))
    {
        setError(error, QStringLiteral("Identity name cannot be empty"));
        return false;
    }

    const QVector<Author> previousIdentities = m_identities;
    const QString previousActiveAuthorId = m_activeAuthorId;
    Author author{QUuid::createUuid().toString(QUuid::WithoutBraces).toLower(), normalizedName};
    m_identities.append(author);
    if (m_activeAuthorId.isEmpty())
        m_activeAuthorId = author.authorId;
    return persistMutation(previousIdentities, previousActiveAuthorId, error);
}

bool LocalIdentityStore::renameIdentity(const QString &id, const QString &name, QString *error)
{
    if (error)
        error->clear();
    QString normalizedName;
    if (!validName(name, &normalizedName))
    {
        setError(error, QStringLiteral("Identity name cannot be empty"));
        return false;
    }
    for (int i = 0; i < m_identities.size(); ++i)
    {
        if (m_identities.at(i).authorId == id)
        {
            const QVector<Author> previousIdentities = m_identities;
            const QString previousActiveAuthorId = m_activeAuthorId;
            m_identities[i].authorName = normalizedName;
            return persistMutation(previousIdentities, previousActiveAuthorId, error);
        }
    }
    setError(error, QStringLiteral("Identity not found"));
    return false;
}

bool LocalIdentityStore::removeIdentity(const QString &id, QString *error)
{
    if (error)
        error->clear();
    for (int i = 0; i < m_identities.size(); ++i)
    {
        if (m_identities.at(i).authorId == id)
        {
            const QVector<Author> previousIdentities = m_identities;
            const QString previousActiveAuthorId = m_activeAuthorId;
            m_identities.removeAt(i);
            if (m_activeAuthorId == id)
                m_activeAuthorId.clear();
            return persistMutation(previousIdentities, previousActiveAuthorId, error);
        }
    }
    setError(error, QStringLiteral("Identity not found"));
    return false;
}

bool LocalIdentityStore::setActiveIdentity(const QString &id, QString *error)
{
    if (error)
        error->clear();
    for (const Author &author : m_identities)
    {
        if (author.authorId == id)
        {
            const QVector<Author> previousIdentities = m_identities;
            const QString previousActiveAuthorId = m_activeAuthorId;
            m_activeAuthorId = id;
            return persistMutation(previousIdentities, previousActiveAuthorId, error);
        }
    }
    setError(error, QStringLiteral("Identity not found"));
    return false;
}
} // namespace CceIdentity
