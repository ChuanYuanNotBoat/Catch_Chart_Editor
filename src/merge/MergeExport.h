#pragma once
#include "ChartMerger.h"
#include "MergeAudio.h"
#include "document/ProjectStorage.h"

namespace MergeExport
{
bool write(const QVector<ChartMerger::Segment> &segments, ChartMerger::Plan plan,
           const QString &directory, const QString &background, const QString &cover,
           QString *error, const MergeAudio::Progress &progress = {});
bool previewBundle(const QVector<ChartMerger::Segment> &segments, const ChartMerger::Plan &plan,
                   const QString &directory, CceDocument::ProjectBundle &bundle, QString *error);
}
