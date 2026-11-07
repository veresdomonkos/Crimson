#include "crimson/asset_manager/importers/material_importer.hpp"

#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    void ApplyMaterialProperty(GpuResourceManager& gpu, MaterialHandle material, const std::string& name, const std::vector<float>& v)
    {
        switch (v.size())
        {
            case 1: gpu.SetMaterialPropertyByName(material, name, v[0]); return;
            case 2: gpu.SetMaterialPropertyByName(material, name, glm::vec2(v[0], v[1])); return;
            case 3: gpu.SetMaterialPropertyByName(material, name, glm::vec3(v[0], v[1], v[2])); return;
            case 4: gpu.SetMaterialPropertyByName(material, name, glm::vec4(v[0], v[1], v[2], v[3])); return;
            default:
                LOG_WARN("[MaterialImporter] Property '{}': unsupported size {}", name, v.size());
        }
    }

    void ApplyMaterialValues(const MaterialData& data, MaterialHandle material, ImportContext& ctx)
    {
        ctx.Gpu.SetMaterialSettings(material, data.Settings);

        for (const auto& [name, values] : data.Properties)
            ApplyMaterialProperty(ctx.Gpu, material, name, values);

        for (const auto& [slot, id] : data.Textures)
        {
            const TextureHandle texture = ctx.Assets.Load(id);

            // A missing one has been warned about when it was read
            if (!texture)
            {
                if (!ctx.Assets.IsMissing(id))
                    LOG_WARN("[MaterialImporter] Texture '{}' failed to load", slot);

                continue;
            }

            ctx.Gpu.SetMaterialTexture(material, slot, texture);
        }

        // Only a cutout material cuts, so the shader's AlphaCutoff is 0 for any other, which nothing is under
        if (data.Settings.RenderMode != MaterialRenderMode::Cutout)
        {
            const ShaderHandle shader = ctx.Assets.Load(data.Shader);
            if (shader && ctx.Gpu.GetShaderInterface(shader).Properties.contains("AlphaCutoff"))
                ctx.Gpu.SetMaterialPropertyByName(material, "AlphaCutoff", 0.0f);
        }
    }

    void ApplyShaderPropertyDefaults(ShaderHandle shader, MaterialHandle material, ImportContext& ctx)
    {
        for (const auto& [name, property] : ctx.Gpu.GetShaderInterface(shader).Properties)
        {
            if (!property.Default.empty())
                ApplyMaterialProperty(ctx.Gpu, material, name, property.Default);
        }
    }

    MaterialHandle CreateMaterialFromData(const MaterialData& data, ImportContext& ctx, std::string_view name)
    {
        const ShaderHandle shader = ctx.Assets.Load(data.Shader);
        if (!shader)
        {
            LOG_ERROR("[MaterialImporter] '{}': shader not found or failed to load", name);
            return MaterialHandle::Invalid();
        }

        const MaterialHandle material = ctx.Gpu.CreateMaterial(shader, data.Settings);
        if (!material)
        {
            LOG_ERROR("[MaterialImporter] CreateMaterial failed for '{}'", name);
            return MaterialHandle::Invalid();
        }

        ApplyShaderPropertyDefaults(shader, material, ctx);

        for (const auto& [slot, texture] : ctx.Gpu.GetShaderInterface(shader).Textures)
        {
            if (const TextureHandle empty = ctx.Assets.EmptySlotTexture(shader, slot))
                ctx.Gpu.SetMaterialTexture(material, slot, empty);
        }

        ApplyMaterialValues(data, material, ctx);
        return material;
    }

    std::span<const std::string_view> MaterialImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".mat" };
        return exts;
    }

    nlohmann::json MaterialImporter::DefaultSettings() const
    {
        return nlohmann::json::object();
    }

    std::optional<MaterialHandle> MaterialImporter::Load(const AssetSource& source, ImportContext& ctx)
    {
        const auto text = source.Read();
        if (!text)
            return {};

        const auto data = ParseMaterialData(*text, ctx.Assets, source.Info.Path);
        if (!data)
            return {};

        const MaterialHandle material = CreateMaterialFromData(*data, ctx, source.Info.Path);
        if (!material)
            return {};

        return material;
    }

    void MaterialImporter::Unload(MaterialHandle& material, GpuResourceManager& gpu)
    {
        gpu.DestroyMaterial(material);
    }
}
