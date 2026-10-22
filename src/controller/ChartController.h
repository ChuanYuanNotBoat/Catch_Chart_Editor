// src/controller/ChartController.h
#pragma once

#include <QObject>
#include <QUndoStack>
#include <QVector>
#include <functional>
#include "model/Chart.h"
#include "controller/ChartChange.h"
#include "document/ChartSnapshot.h"

namespace CceDocument { class ChartDelta; }

/**
 * @brief 谱面编辑控制器，负责所有修改操作，并管理撤销/重做栈。
 *
 * 线程安全：所有方法必须在主线程调用。
 * 修改信号：任何数据变化都会发送 chartChangeCommitted()（带单调 revision
 * 和类型化变更集）以及兼容性的 chartChanged() 信号。
 *
 */
class ChartController : public QObject
{
    Q_OBJECT
public:
    // Keep the existing opaque-input size bound; Undo retains changed data.
    // The historical constant name remains source-compatible.
    static constexpr int kMaxOpaqueSnapshotNotes = 100000;

    explicit ChartController(QObject *parent = nullptr);
    ~ChartController();

    // 获取当前谱面（只读）
    const Chart *chart() const { return &m_chart; }
    // Capture on the controller thread; the returned value can be read by workers.
    ChartSnapshot snapshot() const { return ChartSnapshot(m_chart, m_readVersion, m_currentChartPath); }
    ChartReadVersion readVersion() const { return m_readVersion; }
    bool isCurrent(const ChartReadVersion &version, ChartChangeSet domains = {}) const
    { return m_readVersion.matches(version, domains); }

    // 获取当前谱面文件路径
    QString chartFilePath() const { return m_currentChartPath; }
    quint64 revision() const { return m_readVersion.revision(); }
    bool isReadOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    using EditGuard = std::function<bool(const QVector<Note> &, bool)>;
    void setEditGuard(EditGuard guard) { m_editGuard = std::move(guard); }
    // Project/review changes share the existing Undo timeline. The callback
    // restores auxiliary state before publishing the resulting chart change.
    bool applyDocumentMutation(const QString &name, const Chart &chart,
                               std::function<void(bool)> restoreAuxiliary, bool reviewTransaction = false);
    bool applyTransaction(const QString &name, const ChartReadVersion &base,
                          const CceDocument::ChartDelta &delta, QString *error = nullptr);

    // 编辑操作（都会自动压入撤销栈）
    void addNote(const Note &note);
    void addNotes(const QVector<Note> &notes); // 批量添加，复合命令
    void removeNote(const Note &note);
    void moveNote(const Note &original, const Note &newNote);
    void moveNotes(const QList<QPair<Note, Note>> &changes); // 批量移动，复合命令
    void removeNotes(const QVector<Note> &notes);            // 批量删除，复合命令
    void addBpm(const BpmEntry &bpm);
    void removeBpm(int index);
    void updateBpm(int index, const BpmEntry &bpm);
    // Timing-only transaction; stores BPM lists, never a full note snapshot.
    bool replaceBpmList(const QString &actionName, const QVector<BpmEntry> &entries);
    void setMetaData(const MetaData &meta);

    // 撤销/重做
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    QString nextUndoActionText() const;
    QString nextRedoActionText() const;

    // 保存/加载
    bool loadChart(const QString &path);
    bool loadChartFromData(const QString &path, Chart loadedChart);
    void clearUndoHistory();
    bool saveChart(const QString &path);
    bool applyExternalChartMutation(const QString &actionName, const Chart &mutatedChart);
    // Adds a no-op entry to the shared undo timeline. Native auxiliary
    // editors use the action text to undo/redo their own state in lockstep.
    void pushUndoMarker(const QString &actionName);
    bool applyBatchEdit(const QString &actionName,
                        const QVector<Note> &notesToAdd,
                        const QVector<Note> &notesToRemove,
                        const QList<QPair<Note, Note>> &notesToMove);

signals:
    void historyOperationCommitted(bool undo, const QString &action);
    void noteOperationCommitted(const QVector<Note> &removed, const QVector<Note> &added,
                                const QList<QPair<Note, Note>> &moved, const QString &operationGroupId);
    void chartChangeCommitted(const ChartChange &change);
    void undoStateChanged(); // stack state is settled, after command notifications
    void chartChanged(); // 任何数据变化
    void chartLoaded();  // 加载新谱面
    void notesChanged();   // 音符增删改
    void bpmListChanged(); // BPM 列表变化
    void metaDataChanged(); // 元数据变化
    void errorOccurred(const QString &msg);
private:
    class ChartCommand;
    class AddNoteCommand;
    class AddNotesCommand; // 批量添加命令
    class RemoveNoteCommand;
    class RemoveNotesCommand;
    class MoveNoteCommand;
    class MoveNotesCommand;
    class BatchEditCommand;
    class AddBpmCommand;
    class RemoveBpmCommand;
    class UpdateBpmCommand;
    class ReplaceBpmListCommand;
    class SetMetaCommand;
    class ExternalMutationCommand;
    class UndoMarkerCommand;
    class DocumentMutationCommand;

    void publishChange(ChartChangeSet changes);

    Chart m_chart;
    QUndoStack *m_undoStack;
    QString m_currentChartPath; // 当前加载的谱面文件路径
    ChartReadVersion m_readVersion;
    bool m_readOnly = false;
    EditGuard m_editGuard;
    bool allowEdit(const QVector<Note> &affected = {}, bool addsNotes = false);
};
