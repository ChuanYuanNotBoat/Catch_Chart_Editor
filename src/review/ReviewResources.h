#pragma once

#include "document/ProjectStorage.h"
#include <QHash>

namespace CceReview
{
// Pure value input allows recovery jobs to collect resources away from widgets.
bool collectBundleResources(CceDocument::ProjectBundle &bundle, const QHash<QString, QString> &sources,
                            const QString &fallbackRoot, QString *error = nullptr);
} // namespace CceReview
