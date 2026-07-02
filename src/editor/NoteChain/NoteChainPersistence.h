#pragma once

// NoteChainPersistence.h — V3 JSON 序列化/反序列化
// 对应 Python modular/persistence/project_io.py
// 保持与 .mcce-plugin/*.curve_tbd.json 格式完全兼容

#include "NoteChainState.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace NoteChain {

class NoteChainPersistence
{
public:
    // 序列化：将 State 导出为 V3 JSON 对象
    static QJsonObject serialize(const NoteChainState &state);

    // 反序列化：从 V3 JSON 对象恢复 State
    static bool deserialize(const QJsonObject &json, NoteChainState &state, QString *errorMsg = nullptr);

    // 文件级读写
    static bool saveToFile(const NoteChainState &state, const QString &filePath, QString *errorMsg = nullptr);
    static bool loadFromFile(const QString &filePath, NoteChainState &state, QString *errorMsg = nullptr);

    // 从谱面路径推导侧车文件路径
    // 例如 chart.mc → .mcce-plugin/chart.curve_tbd.json
    static QString sidecarPathForChart(const QString &chartFilePath);

private:
    // 单锚点序列化
    static QJsonObject serializeAnchor(const Anchor &anchor);
    static bool deserializeAnchor(const QJsonObject &json, Anchor &anchor, QString *errorMsg);

    // V3 payload 版本号
    static constexpr int kV3Version = 3;
};

} // namespace NoteChain