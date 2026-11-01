#include "spirv_to_glsl.hpp"

#include <algorithm>
#include <exception>

#include <spirv_glsl.hpp>

#include "crimson/core/log.hpp"
#include "crimson/renderer/shader_interface.hpp"

namespace crimson::opengl
{
    namespace
    {
        uint32_t GetUniformBlockSlot(uint32_t space)
        {
            switch (space)
            {
                case kFrameSpace: return kFrameBlockSlot;
                case kViewSpace:  return kViewBlockSlot;
                default:          return kMaterialBlockSlot;
            }
        }

        uint32_t GetStorageBufferSlot(uint32_t binding)
        {
            switch (binding)
            {
                case kLightsBinding:  return kLightsSlot;
                case kShadowsBinding: return kShadowsSlot;
                default:              return kObjectsSlot;
            }
        }

        uint32_t GetTextureUnit(std::vector<GlslTextureUnit>& units, const std::string& texture, const std::string& sampler)
        {
            const auto existing = std::ranges::find_if(units, [&](const GlslTextureUnit& unit)
            {
                return unit.Texture == texture && unit.Sampler == sampler;
            });

            if (existing != units.end())
                return existing->Unit;

            const uint32_t unit = static_cast<uint32_t>(units.size());
            units.push_back({ texture, sampler, unit });
            return unit;
        }
    }

    std::optional<std::string> TranslateSpirvToGlsl(std::span<const uint32_t> spirv, std::vector<GlslTextureUnit>& units)
    {
        try
        {
            spirv_cross::CompilerGLSL compiler(spirv.data(), spirv.size());

            spirv_cross::CompilerGLSL::Options options;
            options.version = 450;
            options.es = false;
            compiler.set_common_options(options);

            // A texture read without a sampler, like Load, still needs a combined sampler in GLSL
            const spirv_cross::VariableID dummySampler = compiler.build_dummy_sampler_for_combined_images();

            compiler.build_combined_image_samplers();

            for (const spirv_cross::CombinedImageSampler& combined : compiler.get_combined_image_samplers())
            {
                const std::string sampler = combined.sampler_id == dummySampler ? "LinearClamp" : compiler.get_name(combined.sampler_id);
                compiler.set_decoration(combined.combined_id, spv::DecorationBinding, GetTextureUnit(units, compiler.get_name(combined.image_id), sampler));
            }

            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

            for (const spirv_cross::Resource& block : resources.uniform_buffers)
            {
                const uint32_t space = compiler.get_decoration(block.id, spv::DecorationDescriptorSet);
                compiler.set_decoration(block.id, spv::DecorationBinding, GetUniformBlockSlot(space));
                compiler.unset_decoration(block.id, spv::DecorationDescriptorSet);
            }

            for (const spirv_cross::Resource& buffer : resources.storage_buffers)
            {
                const uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
                compiler.set_decoration(buffer.id, spv::DecorationBinding, GetStorageBufferSlot(binding));
                compiler.unset_decoration(buffer.id, spv::DecorationDescriptorSet);
            }

            // OpenGL finds the push constant's uniform by name, which the compiler doesn't keep as written
            for (const spirv_cross::Resource& block : resources.push_constant_buffers)
                compiler.set_name(block.id, kDrawUniformName);

            return compiler.compile();
        }
        catch (const std::exception& error)
        {
            LOG_ERROR("[OpenGL] Failed to translate a shader to GLSL: {}", error.what());
            return std::nullopt;
        }
    }
}
