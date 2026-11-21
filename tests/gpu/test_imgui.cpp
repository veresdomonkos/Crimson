#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <imgui.h>

#include "imgui_convert.hpp"
#include "imgui_renderer.hpp"
#include "gpu/gpu_test.hpp"

using namespace crimson;
using namespace crimson::editor::ui;
using namespace crimson::tests;

namespace
{
    constexpr uint32_t kSize = 64;

    constexpr glm::ivec3 kBlack(0, 0, 0);
    constexpr glm::ivec3 kRed(255, 0, 0);

    struct ImGuiContextGuard
    {
        ImGuiContextGuard()
        {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
        }

        ~ImGuiContextGuard() { ImGui::DestroyContext(); }
        ImGuiContextGuard(const ImGuiContextGuard&) = delete;
        ImGuiContextGuard& operator=(const ImGuiContextGuard&) = delete;
    };
}

TEST_CASE("ImGui draws through the engine, with its own font texture", "[imgui]")
{
    // The renderer goes before the context, as it destroys ImGui's textures
    const ImGuiContextGuard context;
    ImGuiRenderer renderer(*Gpu().GPUResources);

    OffscreenTarget target(kSize, kSize);

    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(kSize), static_cast<float>(kSize));

    const auto drawFrame = [&]
    {
        ImGui::NewFrame();

        // A red rect near the top left, and some text, which needs the font texture
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        list->AddRectFilled(ImVec2(8.0f, 8.0f), ImVec2(40.0f, 40.0f), IM_COL32(255, 0, 0, 255));
        list->AddText(ImVec2(8.0f, 44.0f), IM_COL32(255, 255, 255, 255), "Crimson");

        ImGui::Render();

        RenderOneFrame([&](FrameRecorder& frame)
        {
            renderer.Record(frame, *ImGui::GetDrawData(), PassInfo{
                .Name = "UI",
                .Target = target.Handle(),
                .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) }
            });
        });

        return target.Read();
    };

    const Image first = drawFrame();

    // Inside the rect, and outside it: the top of the display is the top of the image
    CHECK(IsNear(first.AtClip(-0.25f, 0.25f), kRed));
    CHECK(IsNear(first.AtClip(0.75f, -0.75f), kBlack));
    CHECK(IsNear(first.AtClip(-0.25f, -0.75f), kBlack));

    // The text is drawn, from the font texture the renderer made
    bool anyText = false;
    for (uint32_t x = 8; x < 56 && !anyText; ++x)
    {
        for (uint32_t row = 4; row < 22; ++row)
            anyText = anyText || first.At(x, row).r > 128;
    }
    CHECK(anyText);

    const ImTextureData* fonts = ImGui::GetIO().Fonts->TexData;
    REQUIRE(fonts);
    CHECK(fonts->Status == ImTextureStatus_OK);
    CHECK(FromImTextureID(fonts->GetTexID()).IsValid());

    // The second frame has nothing to create, and the same buffers to update
    const Image second = drawFrame();
    CHECK(IsNear(second.AtClip(-0.25f, 0.25f), kRed));
    CHECK(IsNear(second.AtClip(0.75f, -0.75f), kBlack));
}
