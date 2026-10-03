#pragma once
#include "audio/BpmDetector.h"
#include <QJsonObject>
namespace analysis
{
QJsonObject timingDiagnostics(const BpmDetector::DetectionResult &);
}
