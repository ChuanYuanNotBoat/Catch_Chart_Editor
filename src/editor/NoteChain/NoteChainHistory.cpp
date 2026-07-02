// NoteChainHistory.cpp — 撤销/重做历史栈实现

#include "NoteChainHistory.h"

namespace NoteChain {

NoteChainHistory::NoteChainHistory() = default;

void NoteChainHistory::push(const NoteChainState &state)
{
    // 如果当前不在栈顶（即用户在撤销后进行了新操作），截断未来的重做记录
    if (m_currentIndex < m_stack.size() - 1)
        m_stack.resize(m_currentIndex + 1);

    m_stack.append(state.clone());

    // 限制最大深度
    if (m_stack.size() > kMaxHistoryDepth)
        m_stack.removeFirst();

    m_currentIndex = m_stack.size() - 1;
}

bool NoteChainHistory::undo(NoteChainState &restored)
{
    if (!canUndo())
        return false;

    m_currentIndex--;
    restored = m_stack.at(m_currentIndex).clone();
    return true;
}

bool NoteChainHistory::redo(NoteChainState &restored)
{
    if (!canRedo())
        return false;

    m_currentIndex++;
    restored = m_stack.at(m_currentIndex).clone();
    return true;
}

bool NoteChainHistory::canUndo() const
{
    return m_currentIndex > 0;
}

bool NoteChainHistory::canRedo() const
{
    return m_currentIndex < m_stack.size() - 1;
}

void NoteChainHistory::clear()
{
    m_stack.clear();
    m_currentIndex = -1;
}

} // namespace NoteChain