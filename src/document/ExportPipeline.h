#pragma once

#include "ProjectStorage.h"

namespace CceDocument
{
bool exportDifficulty(const ProjectBundle &bundle, const QString &difficultyId, const QString &outputPath,
                      const QStringList &onlyLayerIds = {}, QString *error = nullptr);
}
