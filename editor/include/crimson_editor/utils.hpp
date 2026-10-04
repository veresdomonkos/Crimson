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
}
