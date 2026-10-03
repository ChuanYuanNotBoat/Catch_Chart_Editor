#pragma once

#include "BeatPosition.h"

struct BpmEntry
{
    int beatNum;
    int numerator;
    int denominator;
    double bpm;

    BpmEntry();
    BpmEntry(int beatNum, int numerator, int denominator, double bpm);
    BeatPosition position() const { return {beatNum, numerator, denominator}; }
};
