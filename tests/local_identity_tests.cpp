#include "identity/LocalIdentityStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using CceIdentity::Author;
using CceIdentity::LocalIdentityStore;

namespace
{
bool writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(object).toJson()) >= 0;
}

QJsonObject identity(const QString &id, const QString &name)
{
    return {{QStringLiteral("authorId"), id}, {QStringLiteral("authorName"), name}};
}

QJsonObject identityDocument(const QJsonArray &identities, const QString &activeId, int version = 1)
{
    return {{QStringLiteral("version"), version},
            {QStringLiteral("identities"), identities},
            {QStringLiteral("activeAuthorId"), activeId}};
}
} // namespace

class LocalIdentityTests : public QObject
{
    Q_OBJECT

private slots:
    void lifecyclePersistsStableIdsAndSnapshots();
    void malformedJsonLoadKeepsExistingState();
    void invalidLoadKeepsExistingState_data();
    void invalidLoadKeepsExistingState();
    void failedWriteRollsBackEveryMutation();
    void emptyNamesAndUnknownIdsAreRejected();
};

void LocalIdentityTests::lifecyclePersistsStableIdsAndSnapshots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("identities.cceidentity"));
    LocalIdentityStore store(path);
    QVERIFY(store.load());
    QVERIFY(store.identities().isEmpty());
    QVERIFY(store.activeIdentity().authorId.isEmpty());

    QSignalSpy changedSpy(&store, &LocalIdentityStore::changed);
    QString error;
    QVERIFY2(store.createIdentity(QStringLiteral(" Alice "), &error), qPrintable(error));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(store.identities().size(), 1);
    const Author alice = store.identities().first();
    QVERIFY(!alice.authorId.isEmpty());
    QCOMPARE(alice.authorName, QStringLiteral("Alice"));
    QCOMPARE(store.activeIdentity().authorId, alice.authorId);
    QCOMPARE(alice.snapshot().value(QStringLiteral("authorId")).toString(), alice.authorId);
    QCOMPARE(alice.snapshot().value(QStringLiteral("authorNameSnapshot")).toString(), QStringLiteral("Alice"));

    QVERIFY2(store.renameIdentity(alice.authorId, QStringLiteral("Alicia"), &error), qPrintable(error));
    QCOMPARE(store.identities().first().authorId, alice.authorId);
    QCOMPARE(store.identities().first().authorName, QStringLiteral("Alicia"));
    QCOMPARE(alice.snapshot().value(QStringLiteral("authorNameSnapshot")).toString(), QStringLiteral("Alice"));

    QVERIFY2(store.createIdentity(QStringLiteral("Bob"), &error), qPrintable(error));
    const Author bob = store.identities().last();
    QVERIFY(bob.authorId != alice.authorId);
    QCOMPARE(store.activeIdentity().authorId, alice.authorId);
    QVERIFY2(store.setActiveIdentity(bob.authorId, &error), qPrintable(error));
    QCOMPARE(store.activeIdentity().authorId, bob.authorId);

    LocalIdentityStore restarted(path);
    QVERIFY2(restarted.load(&error), qPrintable(error));
    QCOMPARE(restarted.identities().size(), 2);
    QCOMPARE(restarted.identities().at(0).authorId, alice.authorId);
    QCOMPARE(restarted.identities().at(0).authorName, QStringLiteral("Alicia"));
    QCOMPARE(restarted.activeIdentity().authorId, bob.authorId);

    QVERIFY2(restarted.removeIdentity(alice.authorId, &error), qPrintable(error));
    QVERIFY(restarted.identities().size() == 1);
    QCOMPARE(restarted.activeIdentity().authorId, bob.authorId);
    QVERIFY2(restarted.createIdentity(QStringLiteral("Alice again"), &error), qPrintable(error));
    const Author replacement = restarted.identities().last();
    QVERIFY(replacement.authorId != alice.authorId);
}

void LocalIdentityTests::malformedJsonLoadKeepsExistingState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("identities.cceidentity"));
    LocalIdentityStore store(path);
    QVERIFY(store.createIdentity(QStringLiteral("Preserved")));
    const Author before = store.identities().first();
    QSignalSpy changedSpy(&store, &LocalIdentityStore::changed);

    QFile damaged(path);
    QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(damaged.write(QByteArrayLiteral("{broken json")), qint64(12));
    damaged.close();

    QString error;
    QVERIFY(!store.load(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.identities().size(), 1);
    QCOMPARE(store.identities().first().authorId, before.authorId);
    QCOMPARE(store.activeIdentity().authorId, before.authorId);
    QCOMPARE(changedSpy.count(), 0);
}

void LocalIdentityTests::invalidLoadKeepsExistingState_data()
{
    QTest::addColumn<QJsonObject>("document");
    QTest::addColumn<QString>("label");

    const QString id = QStringLiteral("123e4567-e89b-12d3-a456-426614174000");
    QTest::newRow("unknown-version") << identityDocument(QJsonArray{identity(id, QStringLiteral("Stored"))}, id, 2)
                                     << QStringLiteral("unknown version");
    QTest::newRow("duplicate-id") << identityDocument(
        QJsonArray{identity(id, QStringLiteral("One")), identity(id, QStringLiteral("Two"))}, id)
                                  << QStringLiteral("duplicate UUID");
    QTest::newRow("missing-active-reference") << identityDocument(
        QJsonArray{identity(id, QStringLiteral("Stored"))}, QStringLiteral("123e4567-e89b-12d3-a456-426614174001"))
                                              << QStringLiteral("unknown active identity");
    QTest::newRow("empty-name") << identityDocument(QJsonArray{identity(id, QStringLiteral("  "))}, id)
                                << QStringLiteral("empty name");
    QTest::newRow("invalid-uuid") << identityDocument(
        QJsonArray{identity(QStringLiteral("not-a-uuid"), QStringLiteral("Stored"))}, QStringLiteral(""))
                                  << QStringLiteral("invalid UUID");
}

void LocalIdentityTests::invalidLoadKeepsExistingState()
{
    QFETCH(QJsonObject, document);
    QFETCH(QString, label);
    Q_UNUSED(label);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("identities.cceidentity"));
    LocalIdentityStore store(path);
    QVERIFY(store.createIdentity(QStringLiteral("Keep me")));
    const QVector<Author> before = store.identities();
    const QString activeBefore = store.activeIdentity().authorId;
    QSignalSpy changedSpy(&store, &LocalIdentityStore::changed);
    changedSpy.clear();

    QVERIFY(writeJson(path, document));
    QString error;
    QVERIFY(!store.load(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.identities().size(), before.size());
    QCOMPARE(store.identities().first().authorId, before.first().authorId);
    QCOMPARE(store.identities().first().authorName, before.first().authorName);
    QCOMPARE(store.activeIdentity().authorId, activeBefore);
    QCOMPARE(changedSpy.count(), 0);
}

void LocalIdentityTests::failedWriteRollsBackEveryMutation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("identities.cceidentity"));
    LocalIdentityStore store(path);
    QVERIFY(store.createIdentity(QStringLiteral("Original")));
    QVERIFY(store.createIdentity(QStringLiteral("Second")));
    const QVector<Author> before = store.identities();
    const QString activeBefore = store.activeIdentity().authorId;

    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkdir(path));
    QSignalSpy changedSpy(&store, &LocalIdentityStore::changed);
    QString error;
    QVERIFY(!store.renameIdentity(before.first().authorId, QStringLiteral("Changed"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.identities().at(0).authorName, QStringLiteral("Original"));
    QVERIFY(!store.removeIdentity(before.first().authorId, &error));
    QCOMPARE(store.identities().size(), 2);
    QVERIFY(!store.setActiveIdentity(before.last().authorId, &error));
    QCOMPARE(store.activeIdentity().authorId, activeBefore);
    QVERIFY(!store.createIdentity(QStringLiteral("Third"), &error));
    QCOMPARE(store.identities().size(), 2);
    QCOMPARE(changedSpy.count(), 0);
}

void LocalIdentityTests::emptyNamesAndUnknownIdsAreRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalIdentityStore store(directory.filePath(QStringLiteral("identities.cceidentity")));
    QSignalSpy changedSpy(&store, &LocalIdentityStore::changed);
    QString error;
    QVERIFY(!store.createIdentity(QStringLiteral(" \t "), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.renameIdentity(QStringLiteral("missing"), QStringLiteral("Name"), &error));
    QVERIFY(!store.removeIdentity(QStringLiteral("missing"), &error));
    QVERIFY(!store.setActiveIdentity(QStringLiteral("missing"), &error));
    QVERIFY(store.identities().isEmpty());
    QCOMPARE(changedSpy.count(), 0);
}

QTEST_GUILESS_MAIN(LocalIdentityTests)
#include "local_identity_tests.moc"
