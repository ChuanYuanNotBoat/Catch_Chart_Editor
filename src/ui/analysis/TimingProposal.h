#pragma once
#include "model/BpmEntry.h"
#include <QVector>
#include <QString>
#include <functional>
class QWidget;
class ChartController;
namespace analysis
{
QString validateTimingProposal(const QVector<BpmEntry> &);
// All proposal sources use one timing-only commit and recheck document/source
// identity after the modal confirmation. Preview generation remains separate.
bool confirmTimingProposal(QWidget *, ChartController *, const QString &title, const QString &description,
                           const QVector<BpmEntry> &, quint64 revision, std::function<bool()> sourceStillCurrent = {},
                           bool confirmEmptyChart = true);
} // namespace analysis
