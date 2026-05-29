#include "ChartController.h"
#include "file/ChartIO.h"
#include "file/BpmAuxFiles.h"
#include "logic/UnreachableDivisionManager.h"
#include "utils/Logger.h"
#include "utils/PerformanceTimer.h"
#include <QUndoCommand>
#include <QDebug>
#include <QList>
#include <QPair>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool bpmLess(const BpmEntry &a, const BpmEntry &b)
{
    if (a.beatNum != b.beatNum)
        return a.beatNum < b.beatNum;

    const double aPos = static_cast<double>(a.numerator) / a.denominator;
    const double bPos = static_cast<double>(b.numerator) / b.denominator;
    return aPos < bPos;
}

bool bpmExactEqual(const BpmEntry &a, const BpmEntry &b)
{
    return a.beatNum == b.beatNum &&
           a.numerator == b.numerator &&
           a.denominator == b.denominator &&
           std::abs(a.bpm - b.bpm) < 1e-9;
}

bool bpmPositionEqual(const BpmEntry &a, const BpmEntry &b)
{
    return a.beatNum == b.beatNum &&
           a.numerator == b.numerator &&
           a.denominator == b.denominator;
}

int findBpmExactIndex(const QVector<BpmEntry> &list, const BpmEntry &target)
{
    for (int i = 0; i < list.size(); ++i)
    {
        if (bpmExactEqual(list[i], target))
            return i;
    }
    return -1;
}

int findBpmIndexByPosition(const QVector<BpmEntry> &list, const BpmEntry &target)
{
    int bestIndex = -1;
    double bestDelta = std::numeric_limits<double>::max();
    for (int i = 0; i < list.size(); ++i)
    {
        if (!bpmPositionEqual(list[i], target))
            continue;
        const double delta = std::abs(list[i].bpm - target.bpm);
        if (delta < bestDelta)
        {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    return bestIndex;
}

void sortBpmList(QVector<BpmEntry> &list)
{
    std::sort(list.begin(), list.end(), bpmLess);
}

bool removeBpmByValue(Chart &chart, const BpmEntry &entry, int fallbackIndex)
{
    QVector<BpmEntry> &list = chart.bpmList();
    int idx = findBpmExactIndex(list, entry);
    if (idx < 0)
        idx = findBpmIndexByPosition(list, entry);
    if (idx < 0 && fallbackIndex >= 0 && fallbackIndex < list.size())
        idx = fallbackIndex;
    if (idx < 0 || idx >= list.size())
        return false;

    list.removeAt(idx);
    return true;
}

bool replaceBpmByValue(Chart &chart, const BpmEntry &from, const BpmEntry &to, int fallbackIndex)
{
    QVector<BpmEntry> &list = chart.bpmList();
    int idx = findBpmExactIndex(list, from);
    if (idx < 0)
        idx = findBpmIndexByPosition(list, from);
    if (idx < 0 && fallbackIndex >= 0 && fallbackIndex < list.size())
        idx = fallbackIndex;
    if (idx < 0 || idx >= list.size())
        return false;

    list[idx] = to;
    sortBpmList(list);
    return true;
}

QString noteSignature(const Note &note)
{
    return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12")
        .arg(note.id)
        .arg(static_cast<int>(note.type))
        .arg(note.beatNum)
        .arg(note.numerator)
        .arg(note.denominator)
        .arg(note.x)
        .arg(note.endBeatNum)
        .arg(note.endNumerator)
        .arg(note.endDenominator)
        .arg(note.sound)
        .arg(note.vol)
        .arg(note.offset);
}

bool isReferenceNoteValid(const Note &note)
{
    // Remove/move-from notes are references to existing data.
    // We still require basic timeline/lane validity to block malformed payloads.
    return note.isTimeValid() && note.isXValid();
}

bool isTargetNoteValid(const Note &note)
{
    // Add/move-to notes must be full valid notes before mutating chart data.
    return note.isValid() && note.isTimeValid() && note.isXValid();
}

QHash<QString, int> buildNoteInventory(const QVector<Note> &notes)
{
    QHash<QString, int> inventory;
    for (const Note &note : notes)
    {
        const QString key = noteSignature(note);
        inventory[key] = inventory.value(key, 0) + 1;
    }
    return inventory;
}

bool consumeFromInventory(QHash<QString, int> *inventory, const Note &note)
{
    if (!inventory)
        return false;
    const QString key = noteSignature(note);
    const int count = inventory->value(key, 0);
    if (count <= 0)
        return false;
    if (count == 1)
        inventory->remove(key);
    else
        (*inventory)[key] = count - 1;
    return true;
}

bool validateBatchEditPayload(const QVector<Note> &notesToAdd,
                              const QVector<Note> &notesToRemove,
                              const QList<QPair<Note, Note>> &notesToMove,
                              const Chart &currentChart,
                              QString *reason)
{
    auto fail = [reason](const QString &msg) -> bool
    {
        if (reason)
            *reason = msg;
        return false;
    };

    if (notesToAdd.isEmpty() && notesToRemove.isEmpty() && notesToMove.isEmpty())
        return fail("Batch edit is empty.");

    constexpr int kMaxBatchOperations = 20000;
    const int totalOps = notesToAdd.size() + notesToRemove.size() + notesToMove.size();
    if (totalOps > kMaxBatchOperations)
    {
        return fail(QString("Batch edit too large (%1 ops > %2 limit).")
                        .arg(totalOps)
                        .arg(kMaxBatchOperations));
    }

    QHash<QString, int> sourceInventory = buildNoteInventory(currentChart.notes());

    QSet<QString> removeKeys;
    for (const Note &note : notesToRemove)
    {
        if (!isReferenceNoteValid(note))
            return fail("Invalid remove note detected.");
        if (!consumeFromInventory(&sourceInventory, note))
            return fail("Remove note does not exist in current chart.");
        removeKeys.insert(noteSignature(note));
    }

    QSet<QString> moveFromKeys;
    for (const auto &mv : notesToMove)
    {
        const Note &from = mv.first;
        const Note &to = mv.second;
        if (!isReferenceNoteValid(from))
            return fail("Invalid move source note detected.");
        if (!isTargetNoteValid(to))
            return fail("Invalid move target note detected.");

        const QString fromKey = noteSignature(from);
        if (moveFromKeys.contains(fromKey))
            return fail("Duplicated move source note detected.");
        moveFromKeys.insert(fromKey);

        if (removeKeys.contains(fromKey))
            return fail("Conflicting remove + move source detected.");
        if (!consumeFromInventory(&sourceInventory, from))
            return fail("Move source note does not exist in current chart.");
    }

    for (const Note &note : notesToAdd)
    {
        if (!isTargetNoteValid(note))
            return fail("Invalid add note detected.");
    }

    return true;
}
} // namespace

// 撤销命令基类
class ChartController::ChartCommand : public QUndoCommand
{
public:
    ChartCommand(ChartController *controller, const QString &text) : QUndoCommand(text), m_controller(controller) {}

protected:
    ChartController *m_controller;
};

// 添加单个音符命令
class ChartController::AddNoteCommand : public ChartController::ChartCommand
{
public:
    AddNoteCommand(ChartController *controller, const Note &note) : ChartCommand(controller, "Add Note"), m_note(note) {}
    void undo() override
    {
        m_controller->m_chart.removeNote(m_note);
        m_controller->chartChanged();
    }
    void redo() override
    {
        m_controller->m_chart.addNote(m_note);
        m_controller->chartChanged();
    }

private:
    Note m_note;
};

// 批量添加音符命令（使用 QVector<Note>）
class ChartController::AddNotesCommand : public ChartController::ChartCommand
{
public:
    AddNotesCommand(ChartController *controller, const QVector<Note> &notes)
        : ChartCommand(controller, QString("Add %1 Notes").arg(notes.size())), m_notes(notes) {}
    void undo() override
    {
        for (const Note &note : m_notes)
            m_controller->m_chart.removeNote(note);
        m_controller->chartChanged();
    }
    void redo() override
    {
        for (const Note &note : m_notes)
            m_controller->m_chart.addNote(note);
        m_controller->chartChanged();
    }

private:
    QVector<Note> m_notes;
};

// 删除单个音符命令
class ChartController::RemoveNoteCommand : public ChartController::ChartCommand
{
public:
    RemoveNoteCommand(ChartController *controller, const Note &note) : ChartCommand(controller, "Remove Note"), m_note(note) {}
    void undo() override
    {
        m_controller->m_chart.addNote(m_note);
        m_controller->chartChanged();
    }
    void redo() override
    {
        m_controller->m_chart.removeNote(m_note);
        m_controller->chartChanged();
    }

private:
    Note m_note;
};

// 批量删除音符命令
class ChartController::RemoveNotesCommand : public ChartController::ChartCommand
{
public:
    RemoveNotesCommand(ChartController *controller, const QVector<Note> &notes)
        : ChartCommand(controller, QString("Remove %1 Notes").arg(notes.size())), m_notes(notes) {}
    void undo() override
    {
        for (const Note &note : m_notes)
            m_controller->m_chart.addNote(note);
        m_controller->chartChanged();
    }
    void redo() override
    {
        for (const Note &note : m_notes)
            m_controller->m_chart.removeNote(note);
        m_controller->chartChanged();
    }

private:
    QVector<Note> m_notes;
};

// 移动单个音符命令
class ChartController::MoveNoteCommand : public ChartController::ChartCommand
{
public:
    MoveNoteCommand(ChartController *controller, const Note &original, const Note &newNote)
        : ChartCommand(controller, "Move Note"), m_original(original), m_new(newNote) {}
    void undo() override
    {
        m_controller->m_chart.removeNote(m_new);
        m_controller->m_chart.addNote(m_original);
        m_controller->chartChanged();
    }
    void redo() override
    {
        m_controller->m_chart.removeNote(m_original);
        m_controller->m_chart.addNote(m_new);
        m_controller->chartChanged();
    }

private:
    Note m_original, m_new;
};

// 批量移动音符命令
class ChartController::MoveNotesCommand : public ChartController::ChartCommand
{
public:
    MoveNotesCommand(ChartController *controller, const QList<QPair<Note, Note>> &changes)
        : ChartCommand(controller, "Move Notes"), m_changes(changes) {}
    void undo() override
    {
        for (const auto &change : m_changes)
            m_controller->m_chart.removeNote(change.second);
        for (const auto &change : m_changes)
            m_controller->m_chart.addNote(change.first);
        m_controller->chartChanged();
    }
    void redo() override
    {
        for (const auto &change : m_changes)
            m_controller->m_chart.removeNote(change.first);
        for (const auto &change : m_changes)
            m_controller->m_chart.addNote(change.second);
        m_controller->chartChanged();
    }

private:
    QList<QPair<Note, Note>> m_changes;
};

// 添加 BPM 命令
class ChartController::AddBpmCommand : public ChartController::ChartCommand
{
public:
    AddBpmCommand(ChartController *controller, const BpmEntry &bpm) : ChartCommand(controller, "Add BPM"), m_bpm(bpm) {}
    void undo() override
    {
        removeBpmByValue(m_controller->m_chart, m_bpm, m_controller->m_chart.bpmList().size() - 1);
        m_controller->chartChanged();
    }
    void redo() override
    {
        m_controller->m_chart.addBpm(m_bpm);
        m_controller->chartChanged();
    }

private:
    BpmEntry m_bpm;
};

// 删除 BPM 命令
class ChartController::RemoveBpmCommand : public ChartController::ChartCommand
{
public:
    RemoveBpmCommand(ChartController *controller, int index, const BpmEntry &bpm)
        : ChartCommand(controller, "Remove BPM"), m_index(index), m_bpm(bpm) {}
    void undo() override
    {
        m_controller->m_chart.addBpm(m_bpm);
        m_controller->chartChanged();
    }
    void redo() override
    {
        removeBpmByValue(m_controller->m_chart, m_bpm, m_index);
        m_controller->chartChanged();
    }

private:
    int m_index;
    BpmEntry m_bpm;
};

// 更新 BPM 命令
class ChartController::UpdateBpmCommand : public ChartController::ChartCommand
{
public:
    UpdateBpmCommand(ChartController *controller, int index, const BpmEntry &oldBpm, const BpmEntry &newBpm)
        : ChartCommand(controller, "Update BPM"), m_index(index), m_old(oldBpm), m_new(newBpm) {}
    void undo() override
    {
        replaceBpmByValue(m_controller->m_chart, m_new, m_old, m_index);
        m_controller->chartChanged();
    }
    void redo() override
    {
        replaceBpmByValue(m_controller->m_chart, m_old, m_new, m_index);
        m_controller->chartChanged();
    }

private:
    int m_index;
    BpmEntry m_old, m_new;
};

// 设置元数据命令
class ChartController::SetMetaCommand : public ChartController::ChartCommand
{
public:
    SetMetaCommand(ChartController *controller, const MetaData &oldMeta, const MetaData &newMeta)
        : ChartCommand(controller, "Edit Meta"), m_old(oldMeta), m_new(newMeta) {}
    void undo() override
    {
        m_controller->m_chart.meta() = m_old;
        m_controller->chartChanged();
    }
    void redo() override
    {
        m_controller->m_chart.meta() = m_new;
        m_controller->chartChanged();
    }

private:
    MetaData m_old, m_new;
};

class ChartController::ExternalMutationCommand : public ChartController::ChartCommand
{
public:
    ExternalMutationCommand(ChartController *controller,
                            const QString &actionName,
                            const Chart &before,
                            const Chart &after,
                            const QString &chartPath)
        : ChartCommand(controller, actionName.isEmpty() ? "Plugin Mutation" : actionName),
          m_before(before),
          m_after(after),
          m_chartPath(chartPath)
    {
    }

    void undo() override
    {
        m_controller->m_chart = m_before;
        if (!m_chartPath.isEmpty())
            ChartIO::save(m_chartPath, m_controller->m_chart);
        m_controller->chartChanged();
    }

    void redo() override
    {
        m_controller->m_chart = m_after;
        if (!m_chartPath.isEmpty())
            ChartIO::save(m_chartPath, m_controller->m_chart);
        m_controller->chartChanged();
    }

private:
    Chart m_before;
    Chart m_after;
    QString m_chartPath;
};

// 不可达分度启用 — 原子撤销命令
// 将BPM插入 + Note修改 + 排除项更新封装为单条撤销记录
class ChartController::UnreachableDivisionCommand : public ChartController::ChartCommand
{
public:
    UnreachableDivisionCommand(ChartController *controller,
                               const QVector<BpmEntry> &newBpms,
                               const Note &originalNote,
                               const Note &replacementNote,
                               const BpmAuxFiles::BpmExcludesData &oldExcludes,
                               const BpmAuxFiles::BpmExcludesData &newExcludes,
                               const QString &chartPath)
        : ChartCommand(controller, QObject::tr("启用不可达分度")),
          m_newBpms(newBpms),
          m_originalNote(originalNote),
          m_replacementNote(replacementNote),
          m_oldExcludes(oldExcludes),
          m_newExcludes(newExcludes),
          m_chartPath(chartPath)
    {
    }

    void redo() override
    {
        // 1. 插入新BPM点
        for (const BpmEntry &bpm : m_newBpms)
            m_controller->m_chart.addBpm(bpm);

        // 2. 移动Note (remove old, add new)
        m_controller->m_chart.removeNote(m_originalNote);
        m_controller->m_chart.addNote(m_replacementNote);

        // 3. 保存排除项
        if (!m_chartPath.isEmpty())
            BpmAuxFiles::saveBpmExcludes(m_chartPath, m_newExcludes);

        m_controller->chartChanged();
    }

    void undo() override
    {
        // 1. 恢复Note (remove new, add old)
        m_controller->m_chart.removeNote(m_replacementNote);
        m_controller->m_chart.addNote(m_originalNote);

        // 2. 移除新插入的BPM点
        for (const BpmEntry &bpm : m_newBpms)
            removeBpmByValue(m_controller->m_chart, bpm, -1);

        // 3. 恢复排除项
        if (!m_chartPath.isEmpty())
            BpmAuxFiles::saveBpmExcludes(m_chartPath, m_oldExcludes);

        m_controller->chartChanged();
    }

private:
    QVector<BpmEntry> m_newBpms;
    Note m_originalNote;
    Note m_replacementNote;
    BpmAuxFiles::BpmExcludesData m_oldExcludes;
    BpmAuxFiles::BpmExcludesData m_newExcludes;
    QString m_chartPath;
};

// ---------- ChartController 实现 ----------
ChartController::ChartController(QObject *parent) : QObject(parent)
{
    m_undoStack = new QUndoStack(this);
}

ChartController::~ChartController()
{
}

void ChartController::addNote(const Note &note)
{
    m_undoStack->push(new AddNoteCommand(this, note));
}

void ChartController::addNotes(const QVector<Note> &notes)
{
    if (notes.isEmpty())
        return;
    m_undoStack->push(new AddNotesCommand(this, notes));
}

void ChartController::removeNote(const Note &note)
{
    int idx = m_chart.notes().indexOf(note);
    if (idx != -1)
        m_undoStack->push(new RemoveNoteCommand(this, note));
}

void ChartController::moveNote(const Note &original, const Note &newNote)
{
    if (original == newNote)
        return;
    m_undoStack->push(new MoveNoteCommand(this, original, newNote));
}

void ChartController::moveNotes(const QList<QPair<Note, Note>> &changes)
{
    if (changes.isEmpty())
        return;

    QString invalidReason;
    if (!validateBatchEditPayload(QVector<Note>{}, QVector<Note>{}, changes, m_chart, &invalidReason))
    {
        Logger::warn(QString("moveNotes rejected: %1").arg(invalidReason));
        return;
    }

    m_undoStack->push(new MoveNotesCommand(this, changes));
}

void ChartController::removeNotes(const QVector<Note> &notes)
{
    if (notes.isEmpty())
        return;
    qDebug() << "[ChartController] removeNotes: pushing command for" << notes.size() << "notes";
    m_undoStack->push(new RemoveNotesCommand(this, notes));
}

void ChartController::addBpm(const BpmEntry &bpm)
{
    m_undoStack->push(new AddBpmCommand(this, bpm));
}

void ChartController::removeBpm(int index)
{
    if (index >= 0 && index < m_chart.bpmList().size())
    {
        m_undoStack->push(new RemoveBpmCommand(this, index, m_chart.bpmList()[index]));
    }
}

void ChartController::updateBpm(int index, const BpmEntry &bpm)
{
    if (index >= 0 && index < m_chart.bpmList().size())
    {
        m_undoStack->push(new UpdateBpmCommand(this, index, m_chart.bpmList()[index], bpm));
    }
}

void ChartController::setMetaData(const MetaData &meta)
{
    m_undoStack->push(new SetMetaCommand(this, m_chart.meta(), meta));
}

void ChartController::undo()
{
    qDebug() << "ChartController::undo called";
    try
    {
        m_undoStack->undo();
        qDebug() << "ChartController::undo completed";
    }
    catch (const std::exception &e)
    {
        qCritical() << "ChartController::undo exception:" << e.what();
        throw;
    }
    catch (...)
    {
        qCritical() << "ChartController::undo unknown exception";
        throw;
    }
}

void ChartController::redo()
{
    qDebug() << "ChartController::redo called";
    try
    {
        m_undoStack->redo();
        qDebug() << "ChartController::redo completed";
    }
    catch (const std::exception &e)
    {
        qCritical() << "ChartController::redo exception:" << e.what();
        throw;
    }
    catch (...)
    {
        qCritical() << "ChartController::redo unknown exception";
        throw;
    }
}

bool ChartController::canUndo() const
{
    return m_undoStack->canUndo();
}

bool ChartController::canRedo() const
{
    return m_undoStack->canRedo();
}

QString ChartController::nextUndoActionText() const
{
    return m_undoStack ? m_undoStack->undoText() : QString();
}

QString ChartController::nextRedoActionText() const
{
    return m_undoStack ? m_undoStack->redoText() : QString();
}

bool ChartController::loadChart(const QString &path)
{
    PerformanceTimer loadTimer("ChartController::loadChart", "ui_operations");

    Logger::info(QString("ChartController::loadChart: Loading chart from %1").arg(path));
    try
    {
        Chart newChart;
        Logger::debug("ChartController::loadChart: Created new Chart object");

        if (ChartIO::load(path, newChart, false))
        {
            Logger::debug("ChartController::loadChart: ChartIO::load completed successfully");
            Logger::debug(QString("ChartController::loadChart: Chart has %1 notes").arg(newChart.notes().size()));
            return loadChartFromData(path, std::move(newChart));
        }
        Logger::error(QString("ChartController::loadChart: ChartIO::load failed for %1").arg(path));
        emit errorOccurred("Failed to load chart: " + path);
        return false;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::loadChart: Exception - %1").arg(e.what()));
        emit errorOccurred("Exception loading chart: " + QString::fromStdString(std::string(e.what())));
        return false;
    }
    catch (...)
    {
        Logger::error("ChartController::loadChart: Unknown exception");
        emit errorOccurred("Unknown exception loading chart");
        return false;
    }
}

bool ChartController::loadChartFromData(const QString &path, Chart loadedChart)
{
    PerformanceTimer loadTimer("ChartController::loadChartFromData", "ui_operations");

    Logger::info(QString("ChartController::loadChartFromData: Applying loaded chart for %1").arg(path));
    try
    {
        Logger::debug(QString("ChartController::loadChartFromData: Chart has %1 notes").arg(loadedChart.notes().size()));

        m_chart = std::move(loadedChart);
        m_currentChartPath = path;
        Logger::debug("ChartController::loadChartFromData: Chart assigned to m_chart, path saved");

        m_undoStack->clear();
        Logger::debug("ChartController::loadChartFromData: Undo stack cleared");

        emit chartChanged();
        emit chartLoaded();
        Logger::info("ChartController::loadChartFromData: Signals emitted");

        Logger::info(QString("ChartController::loadChartFromData: Successfully applied chart for %1").arg(path));
        return true;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::loadChartFromData: Exception - %1").arg(e.what()));
        emit errorOccurred("Exception applying loaded chart: " + QString::fromStdString(std::string(e.what())));
        return false;
    }
    catch (...)
    {
        Logger::error("ChartController::loadChartFromData: Unknown exception");
        emit errorOccurred("Unknown exception applying loaded chart");
        return false;
    }
}

void ChartController::applyUnreachableDivisionAtomic(
    const QVector<BpmEntry> &newBpms,
    const Note &originalNote,
    const Note &replacementNote,
    const BpmAuxFiles::BpmExcludesData &oldExcludes,
    const BpmAuxFiles::BpmExcludesData &newExcludes,
    const QString &chartPath)
{
    m_undoStack->push(new UnreachableDivisionCommand(
        this,
        newBpms,
        originalNote,
        replacementNote,
        oldExcludes,
        newExcludes,
        chartPath));
}

bool ChartController::applyUnreachableDivisionAtomic(
    const QVector<int> &noteIndices,
    int targetDenominator)
{
    m_lastOperationError.clear();
    if (!m_chart.notes().isEmpty() && !noteIndices.isEmpty())
    {
        // Use UnreachableDivisionManager for the full pipeline
        UnreachableDivisionManager mgr;
        bool ok = mgr.applyUnreachableDivision(
            this,
            m_currentChartPath,
            noteIndices,
            targetDenominator);
        if (!ok)
            m_lastOperationError = mgr.lastError();
        return ok;
    }
    m_lastOperationError = QStringLiteral("No notes selected");
    return false;
}

bool ChartController::saveChart(const QString &path)
{
    PerformanceTimer saveTimer("ChartController::saveChart", "ui_operations");

    Logger::info(QString("ChartController::saveChart: Saving chart to %1").arg(path));
    Logger::debug(QString("ChartController::saveChart: Chart has %1 notes").arg(m_chart.notes().size()));

    try
    {
        if (ChartIO::save(path, m_chart))
        {
            Logger::info(QString("ChartController::saveChart: Successfully saved chart to %1").arg(path));
            return true;
        }
        Logger::error(QString("ChartController::saveChart: ChartIO::save failed for %1").arg(path));
        emit errorOccurred("Failed to save chart: " + path);
        return false;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::saveChart: Exception - %1").arg(e.what()));
        emit errorOccurred("Exception saving chart: " + QString::fromStdString(std::string(e.what())));
        return false;
    }
    catch (...)
    {
        Logger::error("ChartController::saveChart: Unknown exception");
        emit errorOccurred("Unknown exception saving chart");
        return false;
    }
}

bool ChartController::applyExternalChartMutation(const QString &actionName, const Chart &mutatedChart)
{
    m_undoStack->push(new ExternalMutationCommand(this, actionName, m_chart, mutatedChart, m_currentChartPath));
    return true;
}

bool ChartController::applyBatchEdit(const QString &actionName,
                                     const QVector<Note> &notesToAdd,
                                     const QVector<Note> &notesToRemove,
                                     const QList<QPair<Note, Note>> &notesToMove)
{
    QString invalidReason;
    if (!validateBatchEditPayload(notesToAdd, notesToRemove, notesToMove, m_chart, &invalidReason))
    {
        Logger::warn(QString("applyBatchEdit rejected: %1").arg(invalidReason));
        return false;
    }

    Chart mutated = m_chart;
    for (const Note &note : notesToRemove)
        mutated.removeNote(note);
    for (const auto &mv : notesToMove)
    {
        mutated.removeNote(mv.first);
        mutated.addNote(mv.second);
    }
    for (const Note &note : notesToAdd)
        mutated.addNote(note);

    m_undoStack->push(new ExternalMutationCommand(
        this,
        actionName.isEmpty() ? "Plugin Batch Edit" : actionName,
        m_chart,
        mutated,
        m_currentChartPath));
    return true;
}
