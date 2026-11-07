#pragma once
#include <array>
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

    // What draws a cutout material in the passes that only need its shape. Both are invalid for any other material.
    struct CutoutMaterials
    {
        MaterialHandle Shadow;
        MaterialHandle Mask; // for the selection outline
    };

    // What CreateSlotMaterials did
    struct SlotMaterials
    {
        std::vector<MaterialID> Created;
        size_t Kept = 0; // slots that had a material already, which is left as it is
    };

    // A reference to an asset that isn't there, with what it was written as, for a save to write it back the same way
    struct MissingAssetRef
    {
        std::string Path;
        bool PathOnly = false; // written by hand without a guid, so its id was made up from the path
        AssetType Type = AssetType::None; // what it was read as, so it keeps a typed id while its asset is away
    };

    class AssetStoreBase;
    template <AssetType T> class AssetStore;

    // Knows every asset under one folder and loads them on first use. Destroying it frees what it loaded.
    // The engine's own shaders and materials are always available too, read-only, under "engine/".
    //
    // Asset paths are relative to the folder, with forward slashes. A load that fails returns an invalid
    // handle or null, logs once and isn't tried again. An invalid id does the same without logging, so a
    // component's empty asset slot needs no check.
    //
    // What changes or removes GPU resources has to run between frames: Refresh, Delete, ApplyMaterial, ReimportTexture,
    // CreateSlotMaterials and ReloadChangedShaders.
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
        [[nodiscard]] AssetID FindId(std::string_view path) const; // an engine asset's old path finds it under its new one

        // The id an engine asset that was renamed has now, for one that was saved with its old id. Any other id stays.
        [[nodiscard]] AssetID Redirect(AssetID id) const;
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
        // The id with its type, or an empty one if there is no such asset or it is of another type. A missing one has the
        // type it was read as, so a material can keep a texture that isn't there, and save it.
        template <AssetType T>
        [[nodiscard]] TypedAssetID<T> As(AssetID id) const
        {
            if (const AssetInfo* info = GetInfo(id))
                return info->Type == T ? TypedAssetID<T>(id) : TypedAssetID<T>();

            const MissingAssetRef* missing = GetMissing(id);
            return missing && missing->Type == T ? TypedAssetID<T>(id) : TypedAssetID<T>();
        }

        template <AssetType T>
        [[nodiscard]] TypedAssetID<T> Find(std::string_view path) const { return As<T>(FindId(path)); }

        // An invalid handle or null if it can't be loaded
        template <AssetType T>
        LoadResult<T> Load(TypedAssetID<T> id);

        // A shortcut for code that knows its asset's path, which warns if nothing of that type is there
        template <AssetType T>
        LoadResult<T> Load(std::string_view path) { return Load(FindOrWarn<T>(path)); }

        [[nodiscard]] MaterialID DefaultMaterial() const;

        // The material made for one of a mesh's slots: materials/<slot>.mat, next to the mesh. Invalid if there is none.
        [[nodiscard]] MaterialID FindSlotMaterial(MeshID mesh, std::string_view slot) const;

        // What a loaded shader's materials can set
        [[nodiscard]] const ShaderInterface& GetShaderInterface(ShaderHandle shader) const { return m_gpuResources.GetShaderInterface(shader); }

        // A separate copy of a texture for showing as it is stored, as UI that isn't sRGB encoded would show an sRGB
        // texture too dark. The caller owns it, and gives it back to DestroyTexturePreview.
        [[nodiscard]] TextureHandle CreateTexturePreview(TextureID id) const;
        void DestroyTexturePreview(TextureHandle preview) const { m_gpuResources.DestroyTexture(preview); }

        // What a material's texture slot uses when nothing is assigned to it. Invalid means the backend's white texture.
        [[nodiscard]] TextureHandle EmptySlotTexture(ShaderHandle shader, const std::string& slot);

        // Looks at the folder again: new files appear, files moved along with their meta keep their id,
        // and the assets whose files are gone are removed. Returns the ids of those.
        std::vector<AssetID> Refresh();

        // These report failures in the log. Engine assets can't be changed, and project assets can't go under "engine/".
        MaterialID CreateMaterial(std::string_view path);
        SceneID CreateScene(std::string_view path); // with a light, for it not to start out black
        ShaderID CreateShader(std::string_view path); // a simple lit one, to start from

        // Makes the material each slot of an .obj looks for, materials/<slot>.mat next to it, from its .mtl: colors,
        // textures and shininess on the default shader, with sRGB turned off on its normal maps. A slot that has one
        // already keeps it, so running it again changes nothing.
        SlotMaterials CreateSlotMaterials(MeshID mesh);
        bool Rename(AssetID id, std::string_view newPath);
        bool Delete(AssetID id);

        // What the material is using right now, which edits may have made different from its file
        [[nodiscard]] const MaterialData* GetMaterialData(MaterialID id);
        [[nodiscard]] std::optional<MaterialData> ReadMaterialFile(MaterialID id) const;

        // Shows the data on the material, leaving its file alone
        MaterialApplyResult ApplyMaterial(MaterialID id, const MaterialData& data);
        bool SaveMaterial(MaterialID id, const MaterialData& data);

        // For a loaded cutout material, so the shadows and the outline have its holes too. They change when it is
        // replaced, or when it starts or stops being a cutout, which ApplyMaterial reports as Replaced.
        [[nodiscard]] CutoutMaterials GetCutoutMaterials(MaterialID id) const;

        [[nodiscard]] std::optional<SceneData> ReadSceneFile(SceneID id) const;
        bool SaveScene(SceneID id, const SceneData& data);

        // Stores the settings in the texture's meta file, and if it is loaded, recreates it for the materials using it
        bool ReimportTexture(TextureID id, nlohmann::json settings);

        // Compiles the project's shaders again whose file or included files changed since they were loaded, and
        // recreates their materials. One that no longer compiles keeps the old version. Returns the ids of the
        // shaders and materials that were replaced, whose handles have to be gotten again.
        std::vector<AssetID> ReloadChangedShaders();
    private:
        // What is known about every asset, loaded or not. What it loads as is in its type's store.
        struct Record
        {
            AssetInfo Info;
            AssetImporterBase* Importer = nullptr;
            const EmbeddedFile* Embedded = nullptr;
        };

        void AddImporter(std::unique_ptr<AssetImporterBase> importer);
        void RegisterEngineResources();
        void Scan(std::unordered_set<AssetID>& seen);
        void RegisterFile(const std::filesystem::path& file, std::unordered_set<AssetID>& seen);
        void RemoveAssets(std::span<const AssetID> ids);
        void UpdateReferencesTo(const std::unordered_map<std::string, AssetID>& moved); // old path to id
        AssetID CreateAssetFile(std::string_view path, std::string_view extension, const std::string& contents);

        template <AssetType T>
        AssetStore<T>& Store();

        // Where an asset of a type keeps what it loaded. Null if there is no such asset of that type.
        template <AssetType T>
        typename AssetStore<T>::Slot* SlotFor(AssetID id);

        template <AssetType T>
        std::optional<LoadedAsset<T>> Import(const Record& record, typename AssetStore<T>::Slot& slot);

        std::vector<AssetID> ReloadShader(AssetID id, const Record& record);

        // Makes, updates or drops a material's cutout companions to match its data. Returns whether they were made or dropped.
        bool SyncCutout(MaterialID id);
        [[nodiscard]] AssetSource SourceOf(const Record& record) const;
        AssetID FindIdOrWarn(std::string_view path) const;

        template <AssetType T>
        TypedAssetID<T> FindOrWarn(std::string_view path) const
        {
            const AssetID id = FindIdOrWarn(path);
            const TypedAssetID<T> typed = As<T>(id);
            if (id && !typed)
                WarnWrongType(path, T);
            return typed;
        }

        void WarnWrongType(std::string_view path, AssetType wanted) const;

        GpuResourceManager& m_gpuResources;
        std::filesystem::path m_root;

        std::vector<std::unique_ptr<AssetImporterBase>> m_importers;
        std::unordered_map<std::string, AssetImporterBase*> m_importersByExtension;

        std::unordered_map<AssetID, Record> m_assets;
        std::unordered_map<std::string, AssetID> m_idsByPath;
        std::unordered_map<std::string, AssetID> m_renamedPaths; // an engine asset's old path to its id
        std::unordered_map<AssetID, AssetID> m_renamedIds; // the id made from an engine asset's old path to its id
        mutable std::unordered_map<AssetID, MissingAssetRef> m_missing;

        // One per type that loads, by AssetType. Scenes have none.
        std::array<std::unique_ptr<AssetStoreBase>, kAssetTypeCount> m_stores;

        TextureHandle m_flatNormal; // made when a shader first needs it
    };
}
