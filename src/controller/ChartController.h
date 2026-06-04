// src/controller/ChartController.h
#pragma once

#include <QObject>
#include <QUndoStack>
#include <QVector>
#include "model/Chart.h"
#include "file/BpmAuxFiles.h"

/**
 * @brief 谱面编辑控制器，负责所有修改操作，并管理撤销/重做栈。
 *
 * 线程安全：所有方法必须在主线程调用。
 * 修改信号：任何数据变化都会发送 chartChanged() 信号。
 * 
 * ChartController 暂时不添加辅助文件访问接口，因为辅助文件通过 BpmAuxFiles 命名空间函数直接访问即可
 */
class ChartController : public QObject
{
    Q_OBJECT
public:
    explicit ChartController(QObject *parent = nullptr);
    ~ChartController();

    // 获取当前谱面（只读）
    const Chart *chart() const { return &m_chart; }
    Chart *mutableChart() { return &m_chart; }

    // 获取当前谱面文件路径
    QString chartFilePath() const { return m_currentChartPath; }

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
    bool saveChart(const QString &path);
    bool applyExternalChartMutation(const QString &actionName, const Chart &mutatedChart);
    bool applyBatchEdit(const QString &actionName,
                        const QVector<Note> &notesToAdd,
                        const QVector<Note> &notesToRemove,
                        const QList<QPair<Note, Note>> &notesToMove);

    // 不可达分度启用的原子操作（低级：BPM插入 + Note移动 + 排除项更新）
    void applyUnreachableDivisionAtomic(
        const QVector<BpmEntry> &newBpms,
        const Note &originalNote,
        const Note &replacementNote,
        const BpmAuxFiles::BpmExcludesData &oldExcludes,
        const BpmAuxFiles::BpmExcludesData &newExcludes,
        const QString &chartPath);

    // 不可达分度启用的原子操作（高级：从UI层调用，内部完成BPM计算/排除项/Note变更）
    bool applyUnreachableDivisionAtomic(
        const QVector<int> &noteIndices,
        int targetDenominator);

    // 获取上一次操作的错误信息
    QString lastOperationError() const { return m_lastOperationError; }

signals:
    void chartChanged(); // 任何数据变化
    void chartLoaded();  // 加载新谱面
    void errorOccurred(const QString &msg);

private:
    class ChartCommand;
    class AddNoteCommand;
    class AddNotesCommand; // 批量添加命令
    class RemoveNoteCommand;
    class RemoveNotesCommand;
    class MoveNoteCommand;
    class MoveNotesCommand;
    class AddBpmCommand;
    class RemoveBpmCommand;
    class UpdateBpmCommand;
    class SetMetaCommand;
    class ExternalMutationCommand;
    class UnreachableDivisionCommand;

    // BPM 去重：查找与 candidate 相同 beat 位置的已有 BPM 索引，返回 -1 表示无重复
    int findDuplicateBpmIndex(const BpmEntry &candidate) const;

    Chart m_chart;
    QUndoStack *m_undoStack;
    QString m_currentChartPath; // 当前加载的谱面文件路径
    QString m_lastOperationError; // 上一次操作的错误信息
};
