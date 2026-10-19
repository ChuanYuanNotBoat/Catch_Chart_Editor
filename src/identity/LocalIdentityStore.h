#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace CceIdentity
{
struct Author
{
    QString authorId;
    QString authorName;

    QJsonObject snapshot() const;
};

class LocalIdentityStore : public QObject
{
    Q_OBJECT

public:
    explicit LocalIdentityStore(const QString &storageFilePath = QString(), QObject *parent = nullptr);

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    QVector<Author> identities() const;
    Author activeIdentity() const;

    bool createIdentity(const QString &name, QString *error = nullptr);
    bool renameIdentity(const QString &id, const QString &name, QString *error = nullptr);
    bool removeIdentity(const QString &id, QString *error = nullptr);
    bool setActiveIdentity(const QString &id, QString *error = nullptr);

signals:
    void changed();

private:
    bool persistMutation(const QVector<Author> &previousIdentities, const QString &previousActiveAuthorId,
                         QString *error);

    QString m_storageFilePath;
    QVector<Author> m_identities;
    QString m_activeAuthorId;
};
} // namespace CceIdentity
