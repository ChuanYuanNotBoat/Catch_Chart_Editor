#pragma once

#include "ReviewCore.h"
#include "document/DocumentModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace CceReview
{
bool addComment(QJsonArray &comments, const QJsonObject &author, const QString &body, const QJsonObject &anchor,
                const QString &threadId = QString(), QString *newCommentId = nullptr, QString *error = nullptr);
bool editComment(QJsonArray &comments, const QString &commentId, const QString &activeAuthorId, const QString &body,
                 QString *error = nullptr);
bool deleteComment(QJsonArray &comments, const QString &commentId, const QString &activeAuthorId,
                   QString *error = nullptr);
bool setThreadResolved(QJsonArray &comments, const QString &threadId, bool resolved, QString *error = nullptr);
QString anchorReferenceState(const QJsonObject &anchor);

QJsonArray relocateComments(const QJsonArray &comments, const CceDocument::Project &before,
                            const CceDocument::Project &after, const QVector<Change> &explicitChanges = {});
} // namespace CceReview
