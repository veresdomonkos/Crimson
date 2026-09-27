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

        if (RendererAPI::GetType() == RendererAPIType::OpenGL)
        {
            command += " -DCRIMSON_FLIP_SHADOW_Y=0";
        }

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
            std::cerr << "[Crimson Shader Compiler] COMPILE ERROR (" << stage << " shader, "
                       << (RendererAPI::GetType() == RendererAPIType::OpenGL ? "OpenGL" : "Vulkan") << "):\n";
            std::cerr << compilerErrors << "\n";
            return {};
        }

        std::vector<uint32_t> spirv(spirvRawBytes.size() / 4);
        std::memcpy(spirv.data(), spirvRawBytes.data(), spirvRawBytes.size());

        return spirv;
    }

    static void DrawTextureViewport(
    const char* windowName,
    ImTextureID texture,
    float aspect)
    {
        ImGui::Begin(
            windowName,
            nullptr,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        );

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        if (avail.x > 0.0f && avail.y > 0.0f)
        {
            const float windowAspect = avail.x / avail.y;

            float visibleU = 1.0f;
            float visibleV = 1.0f;

            if (windowAspect < aspect)
            {
                // Keskenyebb ablak:
                // oldalakat cropolunk.
                visibleU = windowAspect / aspect;
            }
            else if (windowAspect > aspect)
            {
                // Szélesebb ablak:
                // tetejét/alját cropoljuk.
                visibleV = aspect / windowAspect;
            }

            const float uCrop = (1.0f - visibleU) * 0.5f;
            const float vCrop = (1.0f - visibleV) * 0.5f;

            // Y-flip
            const ImVec2 uv0(
                uCrop,
                1.0f - vCrop
            );

            const ImVec2 uv1(
                1.0f - uCrop,
                vCrop
            );

            ImGui::Image(
                texture,
                avail,
                uv0,
                uv1
            );
        }

        ImGui::End();
    }
}
