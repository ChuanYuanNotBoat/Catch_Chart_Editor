#pragma once

#include <QFlags>
#include <QMetaType>
#include <QtGlobal>

// A chart change may affect more than one logical cache domain. For example,
// changing the metadata offset affects both metadata consumers and beat/time
// conversion, while changing the audio file is both metadata and a resource
// dependency.
enum class ChartChangeType : quint8
{
    None = 0,
    Notes = 1 << 0,
    Timing = 1 << 1,
    Metadata = 1 << 2,
    Resources = 1 << 3,
};

Q_DECLARE_FLAGS(ChartChangeSet, ChartChangeType)
Q_DECLARE_OPERATORS_FOR_FLAGS(ChartChangeSet)

struct ChartChange
{
    quint64 revision = 0;
    ChartChangeSet types;

    bool affects(ChartChangeType type) const { return types.testFlag(type); }
};

Q_DECLARE_METATYPE(ChartChange)
