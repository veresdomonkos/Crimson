#include "crimson_editor/ui/editor_ui.hpp"
#include <imgui.h>

namespace crimson::editor::ui
{
    EditorUI::EditorUI(ImGuiBackend& backend)
        : m_backend(backend)
    {
        ApplyEditorStyle();
    }

    EditorUI::~EditorUI()
    {
        for (auto panel : m_panels)
            delete panel;
    }

    void EditorUI::Draw(const NativeFrameHandles& handles)
    {
        DrawDockSpace();
        for (auto panel : m_panels)
            panel->OnImGui();

        ImGui::Render();
        m_backend.RenderDrawData(ImGui::GetDrawData(), handles);
    }

    void EditorUI::ApplyEditorStyle()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.Fonts->AddFontFromFileTTF("assets/fonts/Inter_18pt-Regular.ttf", 18.0f);

        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        style.WindowRounding    = 4.0f;
        style.FrameRounding     = 3.0f;
        style.GrabRounding      = 3.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.TabRounding       = 3.0f;

        style.WindowPadding     = ImVec2(10.0f, 10.0f);
        style.FramePadding      = ImVec2(8.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
        style.IndentSpacing     = 20.0f;
        style.ScrollbarSize     = 14.0f;
        style.GrabMinSize       = 10.0f;

        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.PopupBorderSize   = 1.0f;

        colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]              = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        colors[ImGuiCol_ChildBg]               = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        colors[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
        colors[ImGuiCol_Border]                = ImVec4(0.25f, 0.25f, 0.27f, 0.60f);
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg]               = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.26f, 0.27f, 0.29f, 1.00f);
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.30f, 0.31f, 0.33f, 1.00f);

        colors[ImGuiCol_TitleBg]               = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.11f, 0.11f, 0.12f, 0.75f);

        colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.38f, 0.38f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.46f, 0.46f, 0.50f, 1.00f);

        constexpr ImVec4 kAccent         = ImVec4(0.75f, 0.20f, 0.25f, 1.00f);
        constexpr ImVec4 kAccentHover    = ImVec4(0.85f, 0.28f, 0.32f, 1.00f);
        constexpr ImVec4 kAccentActive   = ImVec4(0.65f, 0.15f, 0.20f, 1.00f);
        constexpr ImVec4 kAccentSubtle   = ImVec4(0.75f, 0.20f, 0.25f, 0.35f);

        colors[ImGuiCol_CheckMark]             = kAccent;
        colors[ImGuiCol_SliderGrab]            = kAccent;
        colors[ImGuiCol_SliderGrabActive]      = kAccentHover;

        colors[ImGuiCol_Button]                = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
        colors[ImGuiCol_ButtonHovered]         = kAccent;
        colors[ImGuiCol_ButtonActive]          = kAccentActive;

        colors[ImGuiCol_Header]                = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered]         = ImVec4(0.75f, 0.20f, 0.25f, 0.55f);
        colors[ImGuiCol_HeaderActive]          = kAccent;

        colors[ImGuiCol_Separator]             = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.75f, 0.20f, 0.25f, 0.65f);
        colors[ImGuiCol_SeparatorActive]       = kAccent;

        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.75f, 0.20f, 0.25f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.75f, 0.20f, 0.25f, 0.55f);
        colors[ImGuiCol_ResizeGripActive]      = kAccent;

        colors[ImGuiCol_Tab]                   = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered]            = ImVec4(0.75f, 0.20f, 0.25f, 0.60f);
        colors[ImGuiCol_TabActive]             = ImVec4(0.55f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_TabUnfocused]          = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);

        colors[ImGuiCol_TabSelectedOverline]    = kAccent;
        colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.75f, 0.20f, 0.25f, 0.50f);

        colors[ImGuiCol_DockingPreview]        = ImVec4(0.75f, 0.20f, 0.25f, 0.60f);
        colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);

        colors[ImGuiCol_PlotLines]             = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]      = kAccentHover;
        colors[ImGuiCol_PlotHistogram]         = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

        colors[ImGuiCol_TextSelectedBg]        = kAccentSubtle;
        colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);

        colors[ImGuiCol_NavHighlight]           = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    void EditorUI::DrawDockSpace()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        ImGui::Begin("DockSpaceHost", nullptr, flags);
        ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
    }
}