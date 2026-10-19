#pragma once

#include "DocumentModel.h"
#include <QByteArray>
#include <QMap>
#include <QString>

namespace CceDocument
{
struct ProjectBundle
{
    Project project;
    QJsonObject review;
    QMap<QString, QByteArray> resources;  // SHA-256 hex -> bytes
    QMap<QString, QString> resourcePaths; // logical relative path -> SHA-256 hex
};

bool writeProject(const QString &path, const ProjectBundle &bundle, QString *error = nullptr);
bool readProject(const QString &path, ProjectBundle &out, QString *error = nullptr);
bool materializeResources(const ProjectBundle &bundle, const QString &destinationRoot, QString *error = nullptr);
QStringList requiredResourcePaths(const Project &project);
} // namespace CceDocument
