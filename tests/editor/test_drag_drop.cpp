#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <entt/entt.hpp>
#include <imgui.h>

#include <crimson/asset_manager/asset_id.hpp>

#include "crimson_editor/ui/drag_drop.hpp"
#include "support/imgui_context.hpp"

using namespace crimson;
namespace drag_drop = crimson::editor::ui::drag_drop;

namespace
{
    constexpr ImVec2 kSourceCentre(60.0f, 25.0f);
    constexpr ImVec2 kTargetCentre(60.0f, 75.0f);

    // A source and a target button in a window that doesn't move, so a drag between them is a drag of the editor
    template <typename T>
    struct DragRig
    {
        crimson::tests::ImGuiContextGuard Context;
        T Value;
        std::function<bool(const T&)> Accepts = [](const T&) { return true; };
        std::vector<std::optional<T>> Drops;

        explicit DragRig(const T& value) : Value(value)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(400.0f, 300.0f);
            io.DeltaTime = 1.0f / 60.0f;
        }

        void Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
            ImGui::Begin("Rig", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
            ImGui::Button("Source", ImVec2(100.0f, 30.0f));
            drag_drop::Source("TEST", Value, "label");

            ImGui::SetCursorPos(ImVec2(10.0f, 60.0f));
            ImGui::Button("Target", ImVec2(100.0f, 30.0f));
            Drops.push_back(drag_drop::Accept<T>("TEST", [this](const T& value) { return Accepts(value); }));

            ImGui::End();
            ImGui::EndFrame();
        }

        void MoveMouse(const ImVec2& position)
        {
            ImGui::GetIO().AddMousePosEvent(position.x, position.y);
        }

        void Press() { ImGui::GetIO().AddMouseButtonEvent(0, true); }
        void Release() { ImGui::GetIO().AddMouseButtonEvent(0, false); }

        // Holds the source, carries it over the target, and stops short of letting go
        void DragOverTarget()
        {
            MoveMouse(kSourceCentre);
            Frame();
            Frame();
            Press();
            Frame();

            for (int step = 1; step <= 10; ++step)
            {
                const float t = static_cast<float>(step) / 10.0f;
                MoveMouse(ImVec2(kSourceCentre.x + (kTargetCentre.x - kSourceCentre.x) * t, kSourceCentre.y + (kTargetCentre.y - kSourceCentre.y) * t));
                Frame();
            }
        }

        void Drop()
        {
            Release();
            Frame();
            Frame();
            Frame();
        }

        [[nodiscard]] std::vector<T> Delivered(std::size_t from = 0) const
        {
            std::vector<T> delivered;
            for (std::size_t i = from; i < Drops.size(); ++i)
            {
                if (Drops[i])
                    delivered.push_back(*Drops[i]);
            }
            return delivered;
        }
    };
}

TEST_CASE("A drop gives what was dragged", "[editor][drag drop]")
{
    SECTION("an entity")
    {
        const entt::entity entity{ 5 };
        DragRig<entt::entity> rig(entity);

        rig.DragOverTarget();
        rig.Drop();

        const std::vector<entt::entity> delivered = rig.Delivered();
        REQUIRE(delivered.size() == 1);
        CHECK(delivered[0] == entity);
    }

    SECTION("an asset")
    {
        const AssetID id{ 0x0123456789abcdefull };
        DragRig<AssetID> rig(id);

        rig.DragOverTarget();
        rig.Drop();

        const std::vector<AssetID> delivered = rig.Delivered();
        REQUIRE(delivered.size() == 1);
        CHECK(delivered[0] == id);
    }
}

TEST_CASE("A drop the predicate refuses gives nothing", "[editor][drag drop]")
{
    const entt::entity entity{ 5 };
    DragRig<entt::entity> rig(entity);

    std::vector<entt::entity> seen;
    rig.Accepts = [&seen](const entt::entity& value)
    {
        seen.push_back(value);
        return false;
    };

    rig.DragOverTarget();
    rig.Drop();

    CHECK(rig.Delivered().empty());
    REQUIRE_FALSE(seen.empty());
    for (const entt::entity value : seen)
        CHECK(value == entity);
}

TEST_CASE("Nothing is dropped before the mouse is released", "[editor][drag drop]")
{
    DragRig<entt::entity> rig(entt::entity{ 5 });

    rig.DragOverTarget();
    rig.Frame();

    CHECK(rig.Delivered().empty());
}
