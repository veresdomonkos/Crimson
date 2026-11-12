#include "shader_reflection.hpp"

#include <span>
#include <vector>

#include <spirv_reflect.h>

#include "crimson/core/log.hpp"
#include "crimson/renderer/frame_gpu_data.hpp"

namespace crimson
{
    namespace
    {
        // Storage buffers report their element size, blocks their total size. Zero means no size to check.
        struct StandardResource
        {
            const char* Name;
            uint32_t Space;
            uint32_t Binding;
            SpvReflectDescriptorType Type;
            std::size_t Size;
        };

        constexpr StandardResource kStandardResources[] = {
            { "FrameBlock",    kFrameSpace, kFrameBlockBinding,    SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sizeof(FrameBlock) },
            { "Lights",        kFrameSpace, kLightsBinding,        SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, sizeof(GpuLight) },
            { "Shadows",       kFrameSpace, kShadowsBinding,       SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, sizeof(GpuShadow) },
            { "Objects",       kFrameSpace, kObjectsBinding,       SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, sizeof(GpuObject) },
            { "ShadowAtlas",   kFrameSpace, kShadowAtlasBinding,   SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  0 },
            { "Views",         kFrameSpace, kViewsBinding,         SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, sizeof(GpuView) },
            { "LinearWrap",    kFrameSpace, kLinearWrapBinding,    SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,        0 },
            { "LinearClamp",   kFrameSpace, kLinearClampBinding,   SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,        0 },
            { "PointClamp",    kFrameSpace, kPointClampBinding,    SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,        0 },
            { "ShadowCompare", kFrameSpace, kShadowCompareBinding, SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,        0 },
        };

        std::size_t ReflectedSize(const SpvReflectDescriptorBinding& binding)
        {
            if (binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                return binding.block.size;

            // A StructuredBuffer is a struct holding one runtime array
            const SpvReflectTypeDescription* type = binding.type_description;
            return type && type->member_count > 0 ? type->members[0].traits.array.stride : 0;
        }

        // A cbuffer has no variable name in the SPIR-V, only its type does
        const char* GetResourceName(const SpvReflectDescriptorBinding& binding)
        {
            if (binding.name && binding.name[0] != '\0')
                return binding.name;

            return binding.type_description && binding.type_description->type_name ? binding.type_description->type_name : "<unnamed>";
        }

        bool CheckStandardResource(const SpvReflectDescriptorBinding& binding)
        {
            for (const StandardResource& resource : kStandardResources)
            {
                if (resource.Space != binding.set || resource.Binding != binding.binding)
                    continue;

                if (resource.Type != binding.descriptor_type)
                {
                    LOG_ERROR("[Shader] '{}' in space{} has the wrong resource type for '{}'", GetResourceName(binding), binding.set, resource.Name);
                    return false;
                }

                const std::size_t size = ReflectedSize(binding);
                if (resource.Size != 0 && size != resource.Size)
                {
                    LOG_ERROR("[Shader] '{}' is {} bytes in the shader but {} in the engine", resource.Name, size, resource.Size);
                    return false;
                }

                return true;
            }

            LOG_ERROR("[Shader] '{}' uses binding {} of space{}, which the engine doesn't provide", GetResourceName(binding), binding.binding, binding.set);
            return false;
        }

        bool AddMaterialResource(const SpvReflectDescriptorBinding& binding, bool vertex, ShaderInterface& result)
        {
            switch (binding.descriptor_type)
            {
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                {
                    if (result.MaterialBlockSize > 0 && result.MaterialBlockBinding != binding.binding)
                    {
                        LOG_ERROR("[Shader] A shader can have only one constant buffer in space{}, '{}' is a second one", kMaterialSpace, GetResourceName(binding));
                        return false;
                    }

                    result.MaterialBlockSize = binding.block.size;
                    result.MaterialBlockBinding = binding.binding;
                    (vertex ? result.MaterialBlockInVertex : result.MaterialBlockInPixel) = true;

                    for (uint32_t i = 0; i < binding.block.member_count; ++i)
                    {
                        const SpvReflectBlockVariable& member = binding.block.members[i];
                        result.Properties[member.name] = ShaderPropertyInfo{ .Offset = member.offset, .Size = member.size };
                    }

                    return true;
                }
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                {
                    ShaderMaterialTexture& texture = result.Textures[binding.name];
                    texture.Binding = binding.binding;
                    (vertex ? texture.Vertex : texture.Pixel) = true;
                    return true;
                }
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                    LOG_ERROR("[Shader] Sampler '{}' can't be in space{}, use one of the shared samplers from crimson/bindings.hlsli", GetResourceName(binding), kMaterialSpace);
                    return false;
                default:
                    LOG_ERROR("[Shader] '{}' in space{} has a resource type materials don't support", GetResourceName(binding), kMaterialSpace);
                    return false;
            }
        }

        bool ReflectStage(std::span<const uint32_t> spirv, bool vertex, ShaderInterface& result)
        {
            SpvReflectShaderModule module;
            if (spvReflectCreateShaderModule(spirv.size_bytes(), spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
            {
                LOG_ERROR("[Shader] Failed to reflect a shader stage");
                return false;
            }

            uint32_t count = 0;
            spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
            std::vector<SpvReflectDescriptorBinding*> bindings(count);
            spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

            bool valid = true;

            for (const SpvReflectDescriptorBinding* binding : bindings)
            {
                if (binding->set == kMaterialSpace)
                {
                    valid = AddMaterialResource(*binding, vertex, result) && valid;
                }
                else if (binding->set == kFrameSpace)
                {
                    valid = CheckStandardResource(*binding) && valid;
                }
                else if (binding->set == kUnusedSpace)
                {
                    LOG_ERROR("[Shader] '{}' is in space{}, which is empty. The camera comes from GetView() in crimson/bindings.hlsli", GetResourceName(*binding), binding->set);
                    valid = false;
                }
                else
                {
                    LOG_ERROR("[Shader] '{}' is in space{}, only spaces 0 to {} exist", GetResourceName(*binding), binding->set, kMaterialSpace);
                    valid = false;
                }
            }

            uint32_t pushConstantCount = 0;
            spvReflectEnumeratePushConstantBlocks(&module, &pushConstantCount, nullptr);
            std::vector<SpvReflectBlockVariable*> pushConstants(pushConstantCount);
            spvReflectEnumeratePushConstantBlocks(&module, &pushConstantCount, pushConstants.data());

            for (const SpvReflectBlockVariable* block : pushConstants)
            {
                if (block->size != sizeof(DrawData))
                {
                    LOG_ERROR("[Shader] The push constant is {} bytes in the shader but {} in the engine", block->size, sizeof(DrawData));
                    valid = false;
                }
            }

            spvReflectDestroyShaderModule(&module);
            return valid;
        }
    }

    std::optional<ShaderInterface> ReflectShaderInterface(const ShaderBinary& binary)
    {
        ShaderInterface result;

        if (!ReflectStage(binary.Vertex, true, result) || !ReflectStage(binary.Pixel, false, result))
            return std::nullopt;

        // What only PSDepth uses has to be in the material's layout too
        if (!binary.PixelDepth.empty() && !ReflectStage(binary.PixelDepth, false, result))
            return std::nullopt;

        for (const auto& [name, annotation] : binary.Annotations)
        {
            if (const auto texture = result.Textures.find(name); texture != result.Textures.end())
            {
                texture->second.Default = annotation.Texture;
                continue;
            }

            const auto property = result.Properties.find(name);
            if (property == result.Properties.end())
                continue;

            property->second.Range = annotation.Range;

            if (annotation.Default.size() * sizeof(float) == property->second.Size)
                property->second.Default = annotation.Default;
            else if (!annotation.Default.empty())
                LOG_WARN("[Shader] The default of '{}' has {} values, which doesn't fit its {} bytes", name, annotation.Default.size(), property->second.Size);
        }

        return result;
    }
}
