#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include <crimson_editor/edit_history.hpp>

using crimson::editor::EditChange;
using crimson::editor::EditHistory;
using crimson::editor::EditSource;

namespace
{
    using Log = std::vector<std::string>;

    // Holds a number, and makes a change of it going from the value at the last collect to the value now
    class FakeSource final : public EditSource
    {
    public:
        FakeSource(std::string name, Log& log) : m_name(std::move(name)), m_log(log) {}

        void Collect(std::vector<std::unique_ptr<EditChange>>& changes) override
        {
            if (Value != m_collected)
                changes.push_back(std::make_unique<Change>(m_name, m_log));

            m_collected = Value;
        }

        void Discard() override
        {
            ++Discarded;
            m_collected = Value;
        }

        int Value = 0;
        int Discarded = 0;
    private:
        class Change final : public EditChange
        {
        public:
            Change(const std::string& name, Log& log) : m_name(name), m_log(log) {}

            void Undo() override { m_log.push_back("undo " + m_name); }
            void Redo() override { m_log.push_back("redo " + m_name); }
        private:
            std::string m_name;
            Log& m_log;
        };

        std::string m_name;
        Log& m_log;
        int m_collected = 0;
    };

    struct Fixture
    {
        Log Entries;
        FakeSource First{ "first", Entries };
        FakeSource Second{ "second", Entries };
        EditHistory History;

        Fixture()
        {
            History.AddSource(First);
            History.AddSource(Second);
        }
    };
}

TEST_CASE_METHOD(Fixture, "Nothing changed is no step", "[edit history]")
{
    CHECK_FALSE(History.Commit());
    CHECK_FALSE(History.CanUndo());
    CHECK_FALSE(History.Undo());
}

TEST_CASE_METHOD(Fixture, "Changes from two sources form one step", "[edit history]")
{
    First.Value = 1;
    Second.Value = 1;
    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    CHECK(Entries == Log{ "undo second", "undo first" });
    CHECK_FALSE(History.CanUndo());

    Entries.clear();
    REQUIRE(History.Redo());
    CHECK(Entries == Log{ "redo first", "redo second" });
    CHECK_FALSE(History.CanRedo());
}

TEST_CASE_METHOD(Fixture, "Steps are undone in the order they were made", "[edit history]")
{
    First.Value = 1;
    History.Commit();
    Second.Value = 1;
    History.Commit();
    First.Value = 2;
    History.Commit();

    REQUIRE(History.Undo());
    REQUIRE(History.Undo());
    REQUIRE(History.Undo());
    CHECK(Entries == Log{ "undo first", "undo second", "undo first" });

    Entries.clear();
    REQUIRE(History.Redo());
    REQUIRE(History.Redo());
    REQUIRE(History.Redo());
    CHECK(Entries == Log{ "redo first", "redo second", "redo first" });
}

TEST_CASE_METHOD(Fixture, "A new step drops what could be redone", "[edit history]")
{
    First.Value = 1;
    History.Commit();
    REQUIRE(History.Undo());
    REQUIRE(History.CanRedo());

    Second.Value = 1;
    REQUIRE(History.Commit());

    CHECK_FALSE(History.CanRedo());
    CHECK_FALSE(History.Redo());
}

TEST_CASE_METHOD(Fixture, "Only the latest steps are kept", "[edit history]")
{
    for (size_t i = 0; i < EditHistory::kMaxSteps + 10; ++i)
    {
        ++First.Value;
        REQUIRE(History.Commit());
    }

    size_t undone = 0;
    while (History.Undo())
        ++undone;

    CHECK(undone == EditHistory::kMaxSteps);
}

TEST_CASE_METHOD(Fixture, "Clear forgets the steps and what the sources recorded", "[edit history]")
{
    First.Value = 1;
    History.Commit();
    Second.Value = 1;

    History.Clear();

    CHECK_FALSE(History.CanUndo());
    CHECK_FALSE(History.CanRedo());
    CHECK(First.Discarded == 1);
    CHECK(Second.Discarded == 1);
    CHECK_FALSE(History.Commit());
}
