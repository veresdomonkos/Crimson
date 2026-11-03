#include "crimson/asset_manager/importers/shader_importer.hpp"

#include "crimson/core/log.hpp"
#include "crimson/renderer/shader_compiler.hpp"

namespace crimson
{
    std::span<const std::string_view> ShaderImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".hlsl" };
        return exts;
    }

    nlohmann::json ShaderImporter::DefaultSettings() const
    {
        return nlohmann::json::object();
    }

    LoadedAsset ShaderImporter::Load(const AssetSource& source, ImportContext& ctx)
    {
        const auto text = source.Read();
        if (!text)
            return {};

        const auto binary = CompileHlslShader(source.Info.Path, *text, source.File.parent_path(), &ctx.Dependencies);
        if (!binary)
        {
            LOG_ERROR("[ShaderImporter] Compilation failed for '{}'", source.Info.Path);
            return {};
        }

        const ShaderHandle handle = ctx.Gpu.CreateShader(*binary);
        if (!handle)
        {
            LOG_ERROR("[ShaderImporter] CreateShader failed for '{}'", source.Info.Path);
            return {};
        }

        return handle;
    }

    void ShaderImporter::Unload(const LoadedAsset& asset, GpuResourceManager& gpu)
    {
        if (const auto* shader = std::get_if<ShaderHandle>(&asset))
            gpu.DestroyShader(*shader);
    }
}
