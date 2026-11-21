#pragma once
#include <imgui.h>

namespace crimson::tests
{
    // An ImGui context for what runs frames without a GPU: docking is on as in the editor, no renderer is expected to
    // upload the font texture, and events are all taken at once
    struct ImGuiContextGuard
    {
        ImGuiContextGuard()
        {
            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO();
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigInputTrickleEventQueue = false;
            io.IniFilename = nullptr;
        }

        ~ImGuiContextGuard() { ImGui::DestroyContext(); }
        ImGuiContextGuard(const ImGuiContextGuard&) = delete;
        ImGuiContextGuard& operator=(const ImGuiContextGuard&) = delete;
    };
}
