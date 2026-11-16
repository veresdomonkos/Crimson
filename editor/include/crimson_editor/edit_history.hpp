#pragma once
#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

namespace crimson::editor
{
    // One source's part of a step, which can be undone and redone
    class EditChange
    {
    public:
        virtual ~EditChange() = default;

        virtual void Undo() = 0;
        virtual void Redo() = 0;
    };

    // Something editable that takes part in the history, like the scene or the assets. It records what it is about to
    // change in its own way, and hands over what did change when the step ends.
    class EditSource
    {
    public:
        virtual ~EditSource() = default;

        // Adds what changed since the last call, which may be nothing or several changes, and starts recording the next step
        virtual void Collect(std::vector<std::unique_ptr<EditChange>>& changes) = 0;

        // Forgets what was recorded since the last call
        virtual void Discard() = 0;
    };

    // Undo and redo for everything the sources hold. One step is what the sources changed between two commits, and
    // undoing applies its changes in reverse, so the sources' edits are undone in the order they were made.
    //
    // The sources, and anything a change refers to, have to outlive the history.
    class EditHistory
    {
    public:
        static constexpr size_t kMaxSteps = 100;

        // Sources are collected from in the order they were added
        void AddSource(EditSource& source) { m_sources.push_back(&source); }

        // Ends the step that was being recorded, which is kept if any source changed. Returns whether it was.
        bool Commit();

        // Put everything back to before the last step, or after the next one. They don't commit what is being recorded.
        bool Undo();
        bool Redo();

        [[nodiscard]] bool CanUndo() const { return m_done > 0; }
        [[nodiscard]] bool CanRedo() const { return m_done < m_steps.size(); }

        // Forgets every step and what the sources recorded, like when another scene is opened
        void Clear();
    private:
        struct Step
        {
            std::vector<std::unique_ptr<EditChange>> Changes;
        };

        std::vector<EditSource*> m_sources;
        std::deque<Step> m_steps;
        size_t m_done = 0; // the steps before this are done, the rest can be redone
    };
}
