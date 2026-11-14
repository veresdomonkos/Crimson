#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "crimson_editor/ui/editor_theme.hpp"

using namespace crimson::editor::ui;

namespace
{
    // ApplyEditorTheme needs a context, but no window or GPU
    struct ImGuiContextGuard
    {
        ImGuiContextGuard() { ImGui::CreateContext(); }
        ~ImGuiContextGuard() { ImGui::DestroyContext(); }
        ImGuiContextGuard(const ImGuiContextGuard&) = delete;
        ImGuiContextGuard& operator=(const ImGuiContextGuard&) = delete;
    };

    bool IsGrey(const ImVec4& color)
    {
        const float lowest = std::min({ color.x, color.y, color.z });
        const float highest = std::max({ color.x, color.y, color.z });
        return highest - lowest <= 0.03f;
    }

    bool SameRgb(const ImVec4& a, const ImVec4& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
}

TEST_CASE("The theme's hovers and presses are grey", "[editor][theme]")
{
    const ImGuiContextGuard context;
    ApplyEditorTheme();

    const ImVec4* colors = ImGui::GetStyle().Colors;

    for (const ImGuiCol slot : {
             ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_TabHovered,
             ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SeparatorHovered, ImGuiCol_SeparatorActive,
             ImGuiCol_ResizeGripHovered, ImGuiCol_ResizeGripActive })
    {
        INFO(ImGui::GetStyleColorName(slot));
        CHECK(IsGrey(colors[slot]));
    }
}

TEST_CASE("The accent appears only on selection, focus, the active tab line and drop targets", "[editor][theme]")
{
    const ImGuiContextGuard context;
    ApplyEditorTheme();

    const ImVec4* colors = ImGui::GetStyle().Colors;

    const std::vector<ImGuiCol> allowed = {
        ImGuiCol_Header, ImGuiCol_TabSelectedOverline, ImGuiCol_NavCursor, ImGuiCol_TextSelectedBg,
        ImGuiCol_DragDropTarget, ImGuiCol_DragDropTargetBg, ImGuiCol_DockingPreview
    };

    const ImVec4 accents[] = { theme::Accent, theme::AccentHover, theme::AccentPressed, theme::Selection, theme::TextSelection };

    for (int slot = 0; slot < ImGuiCol_COUNT; ++slot)
    {
        if (std::find(allowed.begin(), allowed.end(), slot) != allowed.end())
            continue;

        INFO(ImGui::GetStyleColorName(slot));
        for (const ImVec4& accent : accents)
            CHECK_FALSE(SameRgb(colors[slot], accent));
    }
}

TEST_CASE("Body text is 13 to 14 px and follows the monitor's scale", "[editor][theme]")
{
    SECTION("At 100%")
    {
        const ImGuiContextGuard context;
        ApplyEditorTheme(1.0f);

        const ImGuiStyle& style = ImGui::GetStyle();
        CHECK(style.FontSizeBase >= 13.0f);
        CHECK(style.FontSizeBase <= 14.0f);
        CHECK(style.FontScaleDpi == 1.0f);

        REQUIRE(theme::RegularFont() != nullptr);
        REQUIRE(theme::BoldFont() != nullptr);
        CHECK(theme::RegularFont() != theme::BoldFont());
    }

    SECTION("At 200% the text is drawn larger rather than stretched")
    {
        const ImGuiContextGuard context;
        ApplyEditorTheme(2.0f);

        const ImGuiStyle& style = ImGui::GetStyle();
        CHECK(style.FontScaleDpi == 2.0f);
        CHECK(style.FontSizeBase == theme::FontBody);
        CHECK(theme::Scaled(theme::SpaceS) == theme::SpaceS * 2.0f);
    }
}

TEST_CASE("Every corner has the one radius and every border is 1 px", "[editor][theme]")
{
    const auto roundings = [](const ImGuiStyle& style)
    {
        return std::vector<float>{ style.WindowRounding, style.ChildRounding, style.FrameRounding, style.PopupRounding,
            style.GrabRounding, style.ScrollbarRounding, style.TabRounding };
    };

    SECTION("At 100%")
    {
        const ImGuiContextGuard context;
        ApplyEditorTheme(1.0f);

        const ImGuiStyle& style = ImGui::GetStyle();
        for (const float rounding : roundings(style))
            CHECK(rounding == theme::Radius);

        // A highlight takes the shape of what it is on, and the rounded targets draw their own
        CHECK(style.DragDropTargetRounding == 0.0f);
        CHECK(style.WindowBorderSize == theme::BorderSize);
        CHECK(style.ChildBorderSize == theme::BorderSize);
        CHECK(style.PopupBorderSize == theme::BorderSize);
        CHECK(style.DockingSeparatorSize == theme::BorderSize);
        CHECK(style.TabBarBorderSize == theme::BorderSize);
        CHECK(style.TabBarOverlineSize == theme::AccentLineSize);
    }

    SECTION("At 200% the corners grow with the screen")
    {
        const ImGuiContextGuard context;
        ApplyEditorTheme(2.0f);

        for (const float rounding : roundings(ImGui::GetStyle()))
            CHECK(rounding == theme::Radius * 2.0f);
    }
}

TEST_CASE("Panels write out no colors, radii or font sizes", "[editor][theme]")
{
    const std::filesystem::path folder = std::filesystem::path(CRIMSON_SOURCE_DIR) / "editor" / "src" / "ui";
    REQUIRE(std::filesystem::is_directory(folder));

    const std::regex forbidden(
        R"(ImVec4\s*\(\s*-?[0-9.]|IM_COL32|ImColor\s*\(|PushStyleVar\s*\(\s*ImGuiStyleVar_\w*Rounding|Rounding\s*\*|PushFont\s*\([^,)]*,\s*-?[0-9.])");

    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != ".cpp" || entry.path().filename() == "editor_theme.cpp")
            continue;

        std::ifstream stream(entry.path(), std::ios::binary);
        REQUIRE(stream);
        std::stringstream text;
        text << stream.rdbuf();
        const std::string source = text.str();

        INFO(entry.path().filename().string());
        CHECK_FALSE(std::regex_search(source, forbidden));
        ++checked;
    }

    CHECK(checked > 10);
}
