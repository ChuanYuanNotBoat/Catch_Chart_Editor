#pragma once

#include <QSet>
#include <QVector>
#include "model/Note.h"
#include "model/BpmEntry.h"

class HyperfruitDetector
{
public:
    HyperfruitDetector();
    // Malody judges A..E are 0..4. Invalid values fall back to E.
    void setJudgeLevel(int level, bool pro = false);
    QSet<int> detect(const QVector<Note> &notes,
                     const QVector<BpmEntry> &bpmList,
                     int offset) const;

private:
    double m_catcherWidth = 96.0 * 0.84;
};
