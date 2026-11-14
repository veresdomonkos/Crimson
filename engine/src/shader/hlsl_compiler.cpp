#include "crimson/renderer/shader_compiler.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

#include <SPIRV/GlslangToSpv.h>
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include "crimson/core/engine_resources.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    namespace
    {
        // Includes resolve relative to the shader's folder, then to the engine's shaders.
        // A shader without a folder only sees the engine's. The files read from the folder are added to included.
        class ShaderIncluder final : public glslang::TShader::Includer
        {
        public:
            ShaderIncluder(std::filesystem::path directory, std::vector<std::filesystem::path>& included)
                : m_directory(std::move(directory)), m_included(included) {}

            IncludeResult* includeLocal(const char* headerName, const char*, size_t) override
            {
                if (!m_directory.empty())
                {
                    const std::filesystem::path file = m_directory / headerName;

                    if (std::ifstream stream(file, std::ios::binary); stream)
                    {
                        if (std::ranges::find(m_included, file) == m_included.end())
                            m_included.push_back(file);

                        std::stringstream contents;
                        contents << stream.rdbuf();
                        return MakeResult(headerName, std::move(contents).str());
                    }
                }

                const std::string enginePath = std::string("shaders/") + headerName;
                if (const EmbeddedFile* file = FindEmbeddedFile(GetEngineResources(), enginePath))
                    return MakeResult(headerName, std::string(file->Text()));

                return nullptr;
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
            static IncludeResult* MakeResult(const char* headerName, std::string contents)
            {
                auto* source = new std::string(std::move(contents));
                return new IncludeResult(headerName, source->data(), source->size(), source);
            }

            std::filesystem::path m_directory;
            std::vector<std::filesystem::path>& m_included;
        };

        void InitializeGlslang()
        {
            // The process stays initialized for the whole run, as shaders can be compiled at any time
            [[maybe_unused]] static const bool initialized = glslang::InitializeProcess();
        }

        // What a stage compiles to: the SPIR-V as glslang emits it, and the same legalized and validated for running
        struct CompiledStage
        {
            std::vector<uint32_t> Interface;
            std::vector<uint32_t> Code;
        };

        // glslang copies a texture passed to a function into a local variable, which Vulkan doesn't allow and which
        // SPIRV-Cross then can't name after the texture. Inlining and copy propagation remove those copies.
        std::optional<std::vector<uint32_t>> Legalize(const std::string& name, const std::vector<uint32_t>& spirv)
        {
            constexpr spv_target_env kTarget = SPV_ENV_VULKAN_1_3;

            spvtools::Optimizer optimizer(kTarget);
            optimizer.SetMessageConsumer([&name](spv_message_level_t level, const char*, const spv_position_t&, const char* message)
            {
                if (level <= SPV_MSG_ERROR)
                    LOG_ERROR("[Shader] {}: {}", name, message);
            });
            optimizer.RegisterLegalizationPasses();

            // The input is what is invalid, so only the result is validated, below
            spvtools::OptimizerOptions options;
            options.set_run_validator(false);

            std::vector<uint32_t> legalized;
            if (!optimizer.Run(spirv.data(), spirv.size(), &legalized, options))
            {
                LOG_ERROR("[Shader] Failed to legalize the SPIR-V of '{}'", name);
                return std::nullopt;
            }

            spvtools::SpirvTools tools(kTarget);
            tools.SetMessageConsumer([&name](spv_message_level_t, const char*, const spv_position_t&, const char* message)
            {
                LOG_ERROR("[Shader] {}: {}", name, message);
            });

            if (!tools.Validate(legalized))
            {
                LOG_ERROR("[Shader] The SPIR-V of '{}' is not valid for Vulkan", name);
                return std::nullopt;
            }

            return legalized;
        }

        std::optional<CompiledStage> CompileStage(const std::string& name, const std::string& source, const std::filesystem::path& includeDirectory, std::vector<std::filesystem::path>& included, EShLanguage stage, const char* entryPoint)
        {
            const char* sourceData = source.data();
            const int sourceLength = static_cast<int>(source.size());
            const char* sourceName = name.c_str();

            constexpr int kDefaultVersion = 100;
            constexpr EShMessages kMessages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);

            glslang::TShader shader(stage);
            shader.setStringsWithLengthsAndNames(&sourceData, &sourceLength, &sourceName, 1);
            shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, kDefaultVersion);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
            // Not 1.6, where discard and clip become a demote to a helper invocation, which GLSL for OpenGL can't do
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
            shader.setEntryPoint("main");
            shader.setSourceEntryPoint(entryPoint);
            shader.setAutoMapLocations(true);

            // StructuredBuffers use t registers but count as storage buffers here
            shader.setShiftBinding(glslang::EResTexture, kTextureBindingShift);
            shader.setShiftBinding(glslang::EResSsbo, kTextureBindingShift);
            shader.setShiftBinding(glslang::EResSampler, kSamplerBindingShift);
            shader.setShiftBinding(glslang::EResImage, kStorageBindingShift);
            shader.setShiftBinding(glslang::EResUav, kStorageBindingShift);

            ShaderIncluder includer(includeDirectory, included);

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

            CompiledStage compiled;
            glslang::GlslangToSpv(*program.getIntermediate(stage), compiled.Interface);

            auto legalized = Legalize(name, compiled.Interface);
            if (!legalized)
                return std::nullopt;

            compiled.Code = std::move(*legalized);
            return compiled;
        }
    }

    std::optional<ShaderBinary> CompileHlslShader(const std::string& name, const std::string& source, const std::filesystem::path& includeDirectory, std::vector<std::filesystem::path>* includedFiles)
    {
        InitializeGlslang();

        std::vector<std::filesystem::path> included;
        auto vertex = CompileStage(name, source, includeDirectory, included, EShLangVertex, "VSMain");
        auto pixel = CompileStage(name, source, includeDirectory, included, EShLangFragment, "PSMain");

        // Only when it is there, as glslang reports a missing entry point as an error
        static const std::regex kDepthEntryPoint(R"(\bvoid\s+PSDepth\s*\()");
        std::optional<CompiledStage> pixelDepth = CompiledStage();
        if (std::regex_search(source, kDepthEntryPoint))
            pixelDepth = CompileStage(name, source, includeDirectory, included, EShLangFragment, "PSDepth");

        if (includedFiles)
            *includedFiles = std::move(included);

        if (!vertex || !pixel || !pixelDepth)
            return std::nullopt;

        return ShaderBinary{
            std::move(vertex->Code), std::move(pixel->Code), ParseShaderAnnotations(source), std::move(pixelDepth->Code),
            std::move(vertex->Interface), std::move(pixel->Interface), std::move(pixelDepth->Interface)
        };
    }
}
