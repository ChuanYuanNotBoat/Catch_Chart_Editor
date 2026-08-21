// NoteChainPersistence.h - V3 sidecar I/O (based on Python sidecar_v3.py)
#pragma once
#include "NoteChainState.h"
#include <QJsonObject>
#include <QString>

namespace NoteChain {

class NoteChainPersistence {
public:
    static QJsonObject serialize(const NoteChainState &state);
    static bool deserialize(const QJsonObject &json, NoteChainState &state, QString *errorMsg = nullptr);
    static bool saveToFile(NoteChainState &state, const QString &path, QString *errorMsg = nullptr);
    static bool loadFromFile(const QString &path, NoteChainState &state, QString *errorMsg = nullptr);
    static QString sidecarPathForChart(const QString &chartPath);

private:
    static QJsonObject serializeAnchor(const Anchor &anchor);
    static bool deserializeAnchor(const QJsonObject &json, Anchor &anchor, QString *errorMsg);
};

} // namespace NoteChain
