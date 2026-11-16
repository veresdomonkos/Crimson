#include "crimson_editor/edit_history.hpp"

#include <iterator>
#include <ranges>
#include <utility>

namespace crimson::editor
{
    bool EditHistory::Commit()
    {
        Step step;

        for (EditSource* source : m_sources)
            source->Collect(step.Changes);

        if (step.Changes.empty())
            return false;

        // What could be redone was undone before this, and goes
        m_steps.erase(m_steps.begin() + static_cast<std::ptrdiff_t>(m_done), m_steps.end());
        m_steps.push_back(std::move(step));

        if (m_steps.size() > kMaxSteps)
            m_steps.pop_front();

        m_done = m_steps.size();
        return true;
    }

    bool EditHistory::Undo()
    {
        if (!CanUndo())
            return false;

        for (const auto& change : std::views::reverse(m_steps[--m_done].Changes))
            change->Undo();

        return true;
    }

    bool EditHistory::Redo()
    {
        if (!CanRedo())
            return false;

        for (const auto& change : m_steps[m_done++].Changes)
            change->Redo();

        return true;
    }

    void EditHistory::Clear()
    {
        for (EditSource* source : m_sources)
            source->Discard();

        m_steps.clear();
        m_done = 0;
    }
}
