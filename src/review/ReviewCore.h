#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CceReview
{

struct Change
{
    QString changeId;
    QString operationGroupId;
    QString domain;
    QString path;
    QString kind;
    QJsonValue before = QJsonValue(QJsonValue::Undefined);
    QJsonValue after = QJsonValue(QJsonValue::Undefined);
    bool moveIntent = false;
    QString dependencyGroupId;
    QStringList dependsOn;
};
using Snapshot = QJsonObject;
QByteArray canonicalHash(const QJsonObject &object);
QVector<Change> diff(const QJsonObject &base, const QJsonObject &proposal, const QVector<Change> &intents = {});
QJsonObject changeToJson(const Change &change);
QString changeFingerprint(const Change &change);
bool decisionMatchesChange(const QJsonObject &decision, const Change &change);
bool changeFromJson(const QJsonObject &object, Change &change, QString *error = nullptr);
void assignDependencies(const QJsonObject &base, const QJsonObject &proposal, QVector<Change> &changes);
QSet<QString> dependencySelection(const QVector<Change> &changes, const QSet<QString> &selected);

struct MergeItem
{
    Change change;
    QString state; // Pending, AlreadySatisfied, Conflict, Invalid
    QJsonValue current;
};
struct MergeResult
{
    QJsonObject result;
    QVector<MergeItem> items;
    bool committed = false;
    bool hasConflicts() const;
};
// Empty acceptedIds means accept all changes. Any conflict or invalid path makes the batch atomic (committed=false).
MergeResult merge(const QJsonObject &base, const QJsonObject &current, const QVector<Change> &changes,
                  const QSet<QString> &acceptedIds = {}, const QHash<QString, QJsonValue> &resolutions = {});

QJsonObject createRevision(const QJsonObject &base, const QJsonObject &proposal, const QJsonObject &author,
                           const QString &description, const QVector<Change> &intents = {});
bool updateRevision(QJsonObject &revision, const QJsonObject &proposal, const QJsonObject &author,
                    const QVector<Change> &intents = {}, QString *error = nullptr, const QJsonObject &reviewState = {});
bool validateRevision(const QJsonObject &revision, QString *error = nullptr);
bool rebaseRevision(QJsonObject &revision, const QJsonObject &base, const QJsonObject &proposal,
                    const QJsonObject &author, const QVector<Change> &intents = {}, QString *error = nullptr);
QJsonObject mergeRevisions(const QJsonObject &local, const QJsonObject &incoming, QStringList *conflicts = nullptr,
                           QString *error = nullptr);
bool resolveRevisionConflict(QJsonObject &revision, int conflictIndex, bool useIncoming, const QJsonObject &author,
                             QString *error = nullptr);

QJsonObject makeComment(const QString &threadId, const QString &commentId, const QJsonObject &author,
                        const QString &body, const QJsonObject &anchor, bool resolved = false);
QJsonArray mergeComments(const QJsonArray &local, const QJsonArray &incoming, QStringList *conflicts = nullptr);
QJsonArray mergeById(const QJsonArray &local, const QJsonArray &incoming, const QString &idField,
                     QStringList *conflicts = nullptr);

bool writePackage(const QString &path, const QJsonObject &revision, QString *error = nullptr);
bool readPackage(const QString &path, QJsonObject &out, QString *error = nullptr);

} // namespace CceReview
