#pragma once

// NoteChainHistory.h — 128 级撤销/重做历史栈（对应 Python 的 _push_history / _restore_snapshot）

#include "NoteChainState.h"

namespace NoteChain {

class NoteChainHistory
{
public:
    NoteChainHistory();

    /// 记录当前状态（在操作前调用）
    void push(const NoteChainState &state);

    /// 撤销：返回 true 并填充 restored 状态
    bool undo(NoteChainState &restored);

    /// 重做：返回 true 并填充 restored 状态
    bool redo(NoteChainState &restored);

    /// 是否可以撤销
    bool canUndo() const;

    /// 是否可以重做
    bool canRedo() const;

    /// 清空历史
    void clear();

    /// 当前历史栈深度
    int depth() const { return m_stack.size(); }
    int currentIndex() const { return m_currentIndex; }

private:
    QVector<NoteChainState> m_stack;
    int m_currentIndex = -1; // 指向当前状态在栈中的索引，-1 表示空
};

} // namespace NoteChain