#include "crimson/asset_manager/importers/material_importer.hpp"

#include <array>

#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    static constexpr std::string_view kDefaultShaderPath = "engine/shaders/S_Lit.hlsl";

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

    // What the material is made from, for it to be updated when one changes
    static void AddUses(const MaterialData& data, ImportContext& ctx)
    {
        if (data.Shader)
            ctx.Uses.push_back(data.Shader);

        for (const auto& [slot, texture] : data.Textures)
            ctx.Uses.push_back(texture);
    }

    TextureHandle MaterialImporter::EmptySlotTexture(ImportContext& ctx, ShaderHandle shader, const std::string& slot)
    {
        const auto& textures = ctx.Gpu.GetShaderInterface(shader).Textures;
        const auto it = textures.find(slot);

        if (it == textures.end() || it->second.Default != ShaderTextureDefault::Normal)
            return TextureHandle::Invalid();

        if (!m_flatNormal)
        {
            // Pointing straight out of the surface
            constexpr std::array<unsigned char, 4> pixel = { 128, 128, 255, 255 };
            m_flatNormal = ctx.Gpu.CreateTexture({ .Width = 1, .Height = 1, .Format = TextureFormat::RGBA8, .Usage = TextureUsage::Sampled }, pixel.data());
        }

        return m_flatNormal;
    }

    MaterialHandle MaterialImporter::CreateMaterialFromData(const MaterialData& data, ImportContext& ctx, std::string_view name)
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
            if (const TextureHandle empty = EmptySlotTexture(ctx, shader, slot))
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

    std::optional<std::string> MaterialImporter::NewFile(const AssetManager& assets) const
    {
        MaterialData data;
        data.Shader = assets.Find<AssetType::Shader>(kDefaultShaderPath);
        data.Properties["Color"] = { 1.0f, 1.0f, 1.0f, 1.0f };

        return WriteMaterialData(data, assets);
    }

    std::optional<std::string> MaterialImporter::UpdateReferences(const AssetSource& source, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets) const
    {
        const std::optional<std::string> text = source.Read();
        return text ? UpdateMaterialReferences(*text, moved, assets) : std::nullopt;
    }

    void MaterialImporter::ReleaseShared(GpuResourceManager& gpu)
    {
        if (m_flatNormal)
        {
            gpu.DestroyTexture(m_flatNormal);
            m_flatNormal = TextureHandle::Invalid();
        }
    }

    std::optional<MaterialHandle> MaterialImporter::Load(const AssetSource& source, ImportContext& ctx)
    {
        const auto text = source.Read();
        if (!text)
            return {};

        const auto data = ParseMaterialData(*text, ctx.Assets, source.Info.Path);
        if (!data)
            return {};

        // Even if it fails, for it to be tried again when what it uses changes
        AddUses(*data, ctx);

        const MaterialHandle material = CreateMaterialFromData(*data, ctx, source.Info.Path);
        if (!material)
            return {};

        return material;
    }

    void MaterialImporter::Unload(MaterialHandle& material, GpuResourceManager& gpu)
    {
        gpu.DestroyMaterial(material);
    }

    std::optional<MaterialHandle> MaterialImporter::Create(const MaterialData& data, const AssetSource& source, ImportContext& ctx)
    {
        AddUses(data, ctx);

        const MaterialHandle material = CreateMaterialFromData(data, ctx, source.Info.Path);
        if (!material)
            return {};

        return material;
    }

    bool MaterialImporter::Update(MaterialHandle& material, const MaterialData& from, const MaterialData& to, ImportContext& ctx)
    {
        // A material's GPU resources are laid out for its shader, so another shader needs a new material
        if (from.Shader != to.Shader)
            return false;

        // Only what changed: replacing a texture has to wait for the GPU, and a dragged color slider gets here every frame
        // The defaults first, for a property that is no longer in the data to go back to its default
        const ShaderHandle shader = ctx.Assets.Load(to.Shader);
        ApplyShaderPropertyDefaults(shader, material, ctx);

        MaterialData values = to;
        values.Textures.clear();
        ApplyMaterialValues(values, material, ctx);

        for (const auto& [name, texture] : to.Textures)
        {
            const auto previous = from.Textures.find(name);
            if (previous != from.Textures.end() && previous->second == texture)
                continue;

            if (const TextureHandle handle = ctx.Assets.Load(texture))
                ctx.Gpu.SetMaterialTexture(material, name, handle);
        }

        for (const auto& [name, texture] : from.Textures)
        {
            if (!to.Textures.contains(name))
                ctx.Gpu.SetMaterialTexture(material, name, EmptySlotTexture(ctx, shader, name));
        }

        AddUses(to, ctx);
        return true;
    }

    bool MaterialImporter::UpdateUse(MaterialHandle& material, const AssetInfo& info, AssetID used, ImportContext& ctx)
    {
        const MaterialData* data = ctx.Assets.GetData(ctx.Assets.As<AssetType::Material>(info.Id));
        if (!data || used == data->Shader)
            return false;

        const ShaderHandle shader = ctx.Assets.Load(data->Shader);

        for (const auto& [slot, texture] : data->Textures)
        {
            if (used != texture)
                continue;

            // Invalid when the texture was removed, which leaves it as a missing one in the data, for it to work again
            // if the file comes back
            TextureHandle handle = ctx.Assets.Load(texture);
            if (!handle)
                handle = EmptySlotTexture(ctx, shader, slot);

            ctx.Gpu.SetMaterialTexture(material, slot, handle);
        }

        return true;
    }
}
