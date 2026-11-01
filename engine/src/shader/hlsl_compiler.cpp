#include "crimson/renderer/shader_compiler.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include "crimson/core/log.hpp"

namespace crimson
{
    namespace
    {
        // Includes resolve relative to the shader's folder
        class ShaderIncluder final : public glslang::TShader::Includer
        {
        public:
            explicit ShaderIncluder(std::filesystem::path directory) : m_directory(std::move(directory)) {}

            IncludeResult* includeLocal(const char* headerName, const char*, size_t) override
            {
                std::ifstream stream(m_directory / headerName, std::ios::binary);
                if (!stream)
                    return nullptr;

                std::stringstream contents;
                contents << stream.rdbuf();

                auto* source = new std::string(std::move(contents).str());
                return new IncludeResult(headerName, source->data(), source->size(), source);
            }

            IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t depth) override
            {
                return includeLocal(headerName, includerName, depth);
            }

            void releaseInclude(IncludeResult* result) override
            {
                if (result)
                {
                    delete static_cast<std::string*>(result->userData);
                    delete result;
                }
            }

        private:
            std::filesystem::path m_directory;
        };

        void InitializeGlslang()
        {
            // The process stays initialized for the whole run, as shaders can be compiled at any time
            [[maybe_unused]] static const bool initialized = glslang::InitializeProcess();
        }

        std::optional<std::vector<uint32_t>> CompileStage(const std::filesystem::path& file, const std::string& source, EShLanguage stage, const char* entryPoint)
        {
            const std::string fileName = file.generic_string();
            const char* sourceData = source.data();
            const int sourceLength = static_cast<int>(source.size());
            const char* sourceName = fileName.c_str();

            constexpr int kDefaultVersion = 100;
            constexpr EShMessages kMessages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);

            glslang::TShader shader(stage);
            shader.setStringsWithLengthsAndNames(&sourceData, &sourceLength, &sourceName, 1);
            shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, kDefaultVersion);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
            shader.setEntryPoint("main");
            shader.setSourceEntryPoint(entryPoint);
            shader.setAutoMapLocations(true);

            // StructuredBuffers use t registers but count as storage buffers here
            shader.setShiftBinding(glslang::EResTexture, kTextureBindingShift);
            shader.setShiftBinding(glslang::EResSsbo, kTextureBindingShift);
            shader.setShiftBinding(glslang::EResSampler, kSamplerBindingShift);
            shader.setShiftBinding(glslang::EResImage, kStorageBindingShift);
            shader.setShiftBinding(glslang::EResUav, kStorageBindingShift);

            ShaderIncluder includer(file.parent_path());

            if (!shader.parse(GetDefaultResources(), kDefaultVersion, false, kMessages, includer))
            {
                LOG_ERROR("[Shader] {}", shader.getInfoLog());
                return std::nullopt;
            }

            glslang::TProgram program;
            program.addShader(&shader);

            if (!program.link(kMessages) || !program.mapIO())
            {
                LOG_ERROR("[Shader] {}", program.getInfoLog());
                return std::nullopt;
            }

            std::vector<uint32_t> spirv;
            glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
            return spirv;
        }
    }

    std::optional<ShaderBinary> CompileHlslShader(const std::filesystem::path& file)
    {
        InitializeGlslang();

        std::ifstream stream(file, std::ios::binary);
        if (!stream)
        {
            LOG_ERROR("[Shader] Cannot open '{}'", file.string());
            return std::nullopt;
        }

        std::stringstream contents;
        contents << stream.rdbuf();
        const std::string source = std::move(contents).str();

        auto vertex = CompileStage(file, source, EShLangVertex, "VSMain");
        auto pixel = CompileStage(file, source, EShLangFragment, "PSMain");

        if (!vertex || !pixel)
            return std::nullopt;

        return ShaderBinary{ std::move(*vertex), std::move(*pixel) };
    }
}
