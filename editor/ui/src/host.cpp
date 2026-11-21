#include "crimson_editor/ui/host.hpp"

#include <string>

#include <imgui.h>
#include <ImGuizmo.h>

#include "crimson_editor/ui/editor_theme.hpp"
#include "imgui_platform.hpp"
#include "imgui_renderer.hpp"
#include "theme_style.hpp"

namespace crimson::editor::ui
{
    struct Host::Impl
    {
        // Owns the ImGui context
        struct ContextOwner
        {
            ContextOwner()
            {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
            }

            ~ContextOwner() { ImGui::DestroyContext(); }
            ContextOwner(const ContextOwner&) = delete;
            ContextOwner& operator=(const ContextOwner&) = delete;
        };

        Impl(Window& window, GpuResourceManager& gpu, const std::filesystem::path& layoutFile)
            : IniPath(layoutFile.string()), Platform(window), Renderer(gpu)
        {
            ImGui::GetIO().IniFilename = IniPath.c_str();
            theme::Apply(window.GetContentScale());
        }

        // ImGui only keeps a pointer to this, so it has to outlive the context
        std::string IniPath;

        // The renderer goes first, while the GPU and the context are there, and the context goes last
        ContextOwner Context;
        ImGuiPlatform Platform;
        ImGuiRenderer Renderer;
    };

    Host::Host(Window& window, GpuResourceManager& gpu, const std::filesystem::path& layoutFile)
        : m_impl(std::make_unique<Impl>(window, gpu, layoutFile))
    {
    }

    Host::~Host() = default;

    void Host::OnEvent(Event& event)
    {
        m_impl->Platform.OnEvent(event);
    }

    void Host::Build(float deltaTime, const std::function<void()>& draw)
    {
        m_impl->Platform.NewFrame(deltaTime);
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        draw();
        ImGui::Render();
    }

    void Host::RecordPass(FrameRecorder& frame)
    {
        // Everything the draw data holds is copied while recording
        m_impl->Renderer.Record(frame, *ImGui::GetDrawData(), PassInfo{
            .Name = "UI",
            .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth,
                       .Color = glm::vec4(theme::AppBackground.R, theme::AppBackground.G, theme::AppBackground.B, 1.0f) }
        });
    }
}
