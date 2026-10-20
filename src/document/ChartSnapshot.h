#pragma once

#include "controller/ChartChange.h"
#include "model/Chart.h"
#include <memory>
#include <utility>

class ChartController;

// A runtime read token, never serialized as chart or Note identity. Copies can
// outlive the controller and are safe to pass to workers together with a snapshot.
class ChartReadVersion final
{
public:
    bool isValid() const
    {
        return bool(m_source);
    }
    quint64 revision() const
    {
        return m_revision;
    }

    // No domain filter means exact revision equality. A filtered consumer may
    // reuse a result after edits to unrelated domains, but never after a load or
    // a change of controller, including reuse of the same controller address.
    bool matches(const ChartReadVersion &other, ChartChangeSet domains = {}) const
    {
        if (!isValid() || m_source != other.m_source)
            return false;
        if (!domains)
            return m_revision == other.m_revision;
        const auto known =
            ChartChangeType::Notes | ChartChangeType::Timing | ChartChangeType::Metadata | ChartChangeType::Resources;
        if (domains & ~known)
            return false;
        return (!domains.testFlag(ChartChangeType::Notes) || m_notes == other.m_notes)
               && (!domains.testFlag(ChartChangeType::Timing) || m_timing == other.m_timing)
               && (!domains.testFlag(ChartChangeType::Metadata) || m_metadata == other.m_metadata)
               && (!domains.testFlag(ChartChangeType::Resources) || m_resources == other.m_resources);
    }

private:
    friend class ChartController;
    struct Source
    {};
    std::shared_ptr<const Source> m_source;
    quint64 m_revision = 0;
    quint64 m_notes = 0, m_timing = 0, m_metadata = 0, m_resources = 0;
};

// Immutable, implicitly shared chart data captured on the controller's thread.
// Obtaining/copying a snapshot does not rebuild or sort the note/timing vectors.
// Workers use this value, never a pointer into the live controller. The file path
// is captured context, not a persistent document ID or part of the data revision.
class ChartSnapshot final
{
public:
    ChartSnapshot() = default;
    bool isValid() const
    {
        return m_version.isValid();
    }
    const ChartReadVersion &version() const
    {
        return m_version;
    }
    quint64 revision() const
    {
        return m_version.revision();
    }
    const Chart &chart() const
    {
        return m_chart;
    }
    const QString &filePath() const
    {
        return m_filePath;
    }

private:
    friend class ChartController;
    ChartSnapshot(const Chart &chart, ChartReadVersion version, QString filePath)
        : m_chart(chart), m_version(std::move(version)), m_filePath(std::move(filePath))
    {}

    Chart m_chart;
    ChartReadVersion m_version;
    QString m_filePath;
};
