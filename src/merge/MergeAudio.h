#pragma once
#include "ChartMerger.h"
#include <functional>

namespace MergeAudio
{
constexpr int SampleRate = 44100;
using Progress = std::function<bool(float)>;
bool decode(const QString &path, QVector<float> &pcm, QString *error, const Progress &progress = {});
QVector<float> changeSpeed(const QVector<float> &pcm, double rate, bool changePitch,
                           const Progress &progress = {});
bool render(const QVector<ChartMerger::Segment> &segments, const ChartMerger::Plan &plan,
            const QString &outputPath, QString *error, const Progress &progress = {});
}
