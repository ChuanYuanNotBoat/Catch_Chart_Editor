#pragma once

#include "controller/ChartChange.h"
#include "model/Chart.h"
#include <QJsonObject>
#include <optional>

namespace CceDocument
{
// Local chart transaction data, independent of widgets and of .mc/.ccepr.
// Images retain runtime handles for in-session Undo. Serialization omits them:
// portable references are source positions plus exact data, never Note UUIDs.
class ChartDelta final
{
public:
    static bool between(const Chart &before, const Chart &after, ChartDelta &delta, QString *error = nullptr);
    static bool fromJson(const QJsonObject &json, ChartDelta &delta, QString *error = nullptr);
    QJsonObject toJson() const;
    ChartDelta inverted() const;
    ChartChangeSet changes() const;
    qsizetype retainedNoteCount() const;
    qsizetype retainedHandleCount() const
    {
        return m_handles.size();
    }
    QVector<Note> affectedNotes() const;
    bool addsNotes() const;

    // Bind portable preimages to the current runtime handles before retaining
    // the inverse for Undo. Does not mutate the source or output on failure.
    bool bindTo(const Chart &source, ChartDelta &bound, QString *error = nullptr) const;
    bool apply(const Chart &source, Chart &result, QString *error = nullptr) const;
    bool canApplyTo(const Chart &source, QString *error = nullptr) const
    {
        return validateSource(source, error);
    }
    bool validateTargets(QString *error = nullptr) const;

private:
    struct Splice
    {
        int beforeIndex = 0, afterIndex = 0;
        QVector<Note> before, after;
    };
    struct HandleLink
    {
        int beforeIndex = 0, afterIndex = 0;
    };
    struct RuntimeHandle
    {
        int beforeIndex = 0, afterIndex = 0;
        QString beforeId, afterId;
    };
    template <class T> struct Replace
    {
        T before, after;
    };
    bool validateShape(QString *error) const;
    bool validateSource(const Chart &source, QString *error) const;
    QVector<Splice> m_notes;
    QVector<HandleLink> m_links;
    QVector<RuntimeHandle> m_handles; // never serialized; no unchanged Note images
    int m_beforeCount = 0, m_afterCount = 0;
    std::optional<Replace<QVector<BpmEntry>>> m_timing;
    std::optional<Replace<MetaData>> m_meta;
    std::optional<Replace<QString>> m_audioSource;
};
} // namespace CceDocument
