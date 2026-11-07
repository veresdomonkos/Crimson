#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/material_data.hpp"
#include "crimson/asset_manager/scene_data.hpp"

namespace crimson
{
    enum class MaterialApplyResult
    {
        Failed,
        Updated,  // the material's handle is the same
        Replaced  // the material has a new handle, so anything holding on to the old one has to get it again
    };

    // A reference to an asset that isn't there, with what it was written as, for a save to write it back the same way
    struct MissingAssetRef
    {
        std::string Path;
        bool PathOnly = false; // written by hand without a guid, so its id was made up from the path
    };

    // Knows every asset under one folder and loads them on first use. Destroying it frees what it loaded.
    // The engine's own shaders and materials are always available too, read-only, under "engine/".
    //
    // Asset paths are relative to the folder, with forward slashes. A load that fails returns an invalid
    // handle or null, logs once and isn't tried again. An invalid id does the same without logging, so a
    // component's empty asset slot needs no check.
    //
    // What changes or removes GPU resources has to run between frames: Refresh, Delete, ApplyMaterial, ReimportTexture
    // and ReloadChangedShaders.
    // Handles that were copied elsewhere stop working when these remove what they refer to.
    class AssetManager
    {
    public:
        AssetManager(GpuResourceManager& gpuResources, std::filesystem::path root);
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        // What exists. None of these load anything.
        [[nodiscard]] const AssetInfo* GetInfo(AssetID id) const;
        [[nodiscard]] AssetID FindId(std::string_view path) const;
        [[nodiscard]] std::vector<const AssetInfo*> List(AssetType type) const; // sorted by path
        [[nodiscard]] std::vector<const AssetInfo*> ListAll() const; // sorted by path
        [[nodiscard]] const std::filesystem::path& GetRoot() const { return m_root; }
        [[nodiscard]] std::filesystem::path GetFilePath(AssetID id) const; // empty for an engine asset
        [[nodiscard]] bool IsLoaded(AssetID id) const;

        // A reference that was read while its asset wasn't there keeps its id, for it to be saved and work again once the
        // file is back. Resolving a reference is only a read of the project, so remembering one counts as const.
        [[nodiscard]] bool IsMissing(AssetID id) const { return id && !GetInfo(id); }
        [[nodiscard]] const MissingAssetRef* GetMissing(AssetID id) const; // null unless IsMissing
        bool RememberMissing(AssetID id, MissingAssetRef ref) const; // whether it is new, for a warning only the first time

        [[nodiscard]] AssetID DefaultMaterial() const;

        // The material made for one of a mesh's slots: materials/<slot>.mat, next to the mesh. Invalid if there is none.
        [[nodiscard]] AssetID FindSlotMaterial(AssetID mesh, std::string_view slot) const;

        // What a loaded shader's materials can set
        [[nodiscard]] const ShaderInterface& GetShaderInterface(ShaderHandle shader) const { return m_gpuResources.GetShaderInterface(shader); }

        TextureHandle LoadTexture(AssetID id);
        ShaderHandle LoadShader(AssetID id);
        MaterialHandle LoadMaterial(AssetID id);
        const Mesh* LoadMesh(AssetID id); // stays valid as long as the manager

        TextureHandle LoadTexture(std::string_view path);
        ShaderHandle LoadShader(std::string_view path);
        MaterialHandle LoadMaterial(std::string_view path);
        const Mesh* LoadMesh(std::string_view path);

        // A separate copy of a texture for showing as it is stored, as UI that isn't sRGB encoded would show an sRGB
        // texture too dark. The caller owns it, and gives it back to DestroyTexturePreview.
        [[nodiscard]] TextureHandle CreateTexturePreview(AssetID id) const;
        void DestroyTexturePreview(TextureHandle preview) const { m_gpuResources.DestroyTexture(preview); }

        // What a material's texture slot uses when nothing is assigned to it. Invalid means the backend's white texture.
        [[nodiscard]] TextureHandle EmptySlotTexture(ShaderHandle shader, const std::string& slot);

        // Looks at the folder again: new files appear, files moved along with their meta keep their id,
        // and the assets whose files are gone are removed. Returns the ids of those.
        std::vector<AssetID> Refresh();

        // These report failures in the log. Engine assets can't be changed, and project assets can't go under "engine/".
        AssetID CreateMaterial(std::string_view path);
        AssetID CreateScene(std::string_view path); // with a light, for it not to start out black
        AssetID CreateShader(std::string_view path); // a simple lit one, to start from
        bool Rename(AssetID id, std::string_view newPath);
        bool Delete(AssetID id);

        // What the material is using right now, which edits may have made different from its file
        [[nodiscard]] const MaterialData* GetMaterialData(AssetID id);
        [[nodiscard]] std::optional<MaterialData> ReadMaterialFile(AssetID id) const;

        // Shows the data on the material, leaving its file alone
        MaterialApplyResult ApplyMaterial(AssetID id, const MaterialData& data);
        bool SaveMaterial(AssetID id, const MaterialData& data);

        [[nodiscard]] std::optional<SceneData> ReadSceneFile(AssetID id) const;
        bool SaveScene(AssetID id, const SceneData& data);

        // Stores the settings in the texture's meta file, and if it is loaded, recreates it for the materials using it
        bool ReimportTexture(AssetID id, nlohmann::json settings);

        // Compiles the project's shaders again whose file or included files changed since they were loaded, and
        // recreates their materials. One that no longer compiles keeps the old version. Returns the ids of the
        // shaders and materials that were replaced, whose handles have to be gotten again.
        std::vector<AssetID> ReloadChangedShaders();
    private:
        struct Entry
        {
            AssetInfo Info;
            AssetImporter* Importer = nullptr;
            const EmbeddedFile* Embedded = nullptr;
            LoadedAsset Loaded;
            bool Failed = false;
            std::optional<MaterialData> Material; // what a material is using, read from its file when first needed

            // For a project's shader: when its file and the files it included last changed, as of its last load
            std::filesystem::file_time_type FileTime;
            std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> Dependencies;
        };

        void AddImporter(std::unique_ptr<AssetImporter> importer);
        void RegisterEngineResources();
        void Scan(std::unordered_set<AssetID>& seen);
        void RegisterFile(const std::filesystem::path& file, std::unordered_set<AssetID>& seen);
        void RemoveAssets(std::span<const AssetID> ids);
        void UpdateReferencesTo(const std::unordered_map<std::string, AssetID>& moved); // old path to id
        AssetID CreateAssetFile(std::string_view path, std::string_view extension, const std::string& contents);

        const LoadedAsset& Load(AssetID id);
        LoadedAsset Import(Entry& entry);
        std::vector<AssetID> ReloadShader(AssetID id, Entry& entry);
        AssetID FindIdOrWarn(std::string_view path) const;
        void Unload(Entry& entry);

        template <typename T>
        const T* LoadAs(AssetID id);

        GpuResourceManager& m_gpuResources;
        std::filesystem::path m_root;

        std::vector<std::unique_ptr<AssetImporter>> m_importers;
        std::unordered_map<std::string, AssetImporter*> m_importersByExtension;

        std::unordered_map<AssetID, Entry> m_assets;
        std::unordered_map<std::string, AssetID> m_idsByPath;
        mutable std::unordered_map<AssetID, MissingAssetRef> m_missing;

        TextureHandle m_flatNormal; // made when a shader first needs it
    };
}
