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

    LoadedAsset ShaderImporter::Load(const AssetMetadata&, const std::filesystem::path& path, ImportContext& ctx)
    {
        const auto binary = CompileHlslShader(path);
        if (!binary)
        {
            LOG_ERROR("[ShaderImporter] Compilation failed for '{}'", path.string());
            return {};
        }

        const ShaderHandle handle = ctx.Gpu.CreateShader(*binary);
        if (!handle)
        {
            LOG_ERROR("[ShaderImporter] CreateShader failed for '{}'", path.string());
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
