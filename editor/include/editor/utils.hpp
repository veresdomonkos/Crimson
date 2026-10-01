#pragma once
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

#include "process.hpp"

namespace crimson::editor::utils
{
    static std::vector<uint32_t> CompileGLSLToSPIRV(std::string_view source, const std::string& stage)
    {
        if (source.empty()) return {};

        std::string command = "glslc -fshader-stage=" + stage;

        //command += RendererAPI::GetType() == RendererAPIType::OpenGL ? " --target-env=opengl" : " --target-env=vulkan1.3";

        command += " -o - -";

        std::vector<char> spirvRawBytes;
        std::string compilerErrors;

        TinyProcessLib::Process process(
            command,
            "",
            [&spirvRawBytes](const char* bytes, size_t n) {
                spirvRawBytes.insert(spirvRawBytes.end(), bytes, bytes + n);
            },
            [&compilerErrors](const char* bytes, size_t n) {
                compilerErrors.append(bytes, n);
            },
            true
        );

        process.write(source.data(), source.size());
        process.close_stdin();

        int exitCode = process.get_exit_status();

        if (exitCode != 0) {
            std::cerr << "[Crimson Shader Compiler] COMPILE ERROR (" << stage << " shader, " << "):\n";
            std::cerr << compilerErrors << "\n";
            return {};
        }

        std::vector<uint32_t> spirv(spirvRawBytes.size() / 4);
        std::memcpy(spirv.data(), spirvRawBytes.data(), spirvRawBytes.size());

        return spirv;
    }

    static void DrawTextureViewport(const char* windowName, ImTextureID texture, float aspect)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            windowName,
            nullptr,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        );
        ImGui::PopStyleVar();

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();

        if (avail.x > 1.0f && avail.y > 1.0f)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                origin,
                ImVec2(origin.x + avail.x, origin.y + avail.y),
                IM_COL32(0, 0, 0, 255));

            ImVec2 size;
            if (avail.x / avail.y > aspect)
                size = ImVec2(avail.y * aspect, avail.y);
            else
                size = ImVec2(avail.x, avail.x / aspect);

            const ImVec2 pos(
                origin.x + (avail.x - size.x) * 0.5f,
                origin.y + (avail.y - size.y) * 0.5f);

            ImGui::SetCursorScreenPos(pos);

            ImGui::Image(texture, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }

        ImGui::End();
    }
}
