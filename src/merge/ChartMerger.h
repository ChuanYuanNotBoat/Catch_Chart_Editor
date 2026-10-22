#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QString>
#include <functional>

namespace ChartMerger
{
struct Source
{
    QString path, audioPath, title, artist, difficulty;
    QJsonObject json;
    int normalCount = 0, rainCount = 0, soundCount = 0;
    double firstBeat = 0, lastBeat = 0;
    double firstSeconds = 0, lastSeconds = 0, audioOrigin = 0;
    double minBpm = 0, maxBpm = 0;
    int primarySoundIndex = -1;
};

struct Segment
{
    Source source;
    double rate = 1.0;
    bool changePitch = false;
    double fadeIn = 5.0, fadeOut = 5.0;
    // Last playable note (including Rain end) -> next first playable note.
    double gapAfter = 10.0;
};

struct Placement
{
    double firstSeconds = 0, lastSeconds = 0, audioStart = 0;
    double clipStart = 0, clipEnd = 0; // Output seconds; infinity for final tail.
};

struct Plan
{
    QJsonObject chart;
    QVector<Placement> placements;
    QVector<QJsonArray> segmentNotes;
};

bool load(const QString &path, Source &source, QString *error = nullptr);
bool plan(const QVector<Segment> &segments, const QString &title, const QString &artist,
          const QString &creator, const QString &difficulty, Plan &result, QString *error = nullptr);
double secondsAt(const QJsonObject &chart, const QJsonArray &beat);
std::function<double(const QJsonArray &)> makeTimer(const QJsonObject &chart);
QString resourcePath(const Source &source, const QString &reference);
}
