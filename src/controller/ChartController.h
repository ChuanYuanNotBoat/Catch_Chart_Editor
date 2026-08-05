// src/controller/ChartController.h
#pragma once

#include <QObject>
#include <QUndoStack>
#include <QVector>
#include "model/Chart.h"

/**
 * @brief 谱面编辑控制器，负责所有修改操作，并管理撤销/重做栈。
 *
 * 线程安全：所有方法必须在主线程调用。
 * 修改信号：任何数据变化都会发送 chartChanged() 信号。
 *
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

signals:
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
    class AddBpmCommand;
    class RemoveBpmCommand;
    class UpdateBpmCommand;
    class SetMetaCommand;
    class ExternalMutationCommand;

    Chart m_chart;
    QUndoStack *m_undoStack;
    QString m_currentChartPath; // 当前加载的谱面文件路径
};
