#include "crimson/asset_manager/asset_manager.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>
#include <utility>
#include <glm/gtc/quaternion.hpp>
#include "crimson/asset_manager/importers/material_importer.hpp"
#include "crimson/asset_manager/importers/mesh_importer.hpp"
#include "crimson/asset_manager/importers/scene_importer.hpp"
#include "crimson/asset_manager/importers/shader_importer.hpp"
#include "crimson/asset_manager/importers/texture_importer.hpp"
#include "crimson/asset_manager/meta_file.hpp"
#include "asset_store.hpp"
#include "crimson/core/engine_resources.hpp"
#include "crimson/core/log.hpp"

#include "engine_renames.hpp"
#include "mtl_materials.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static constexpr std::string_view kEnginePathPrefix = "engine/";
    static constexpr std::string_view kDefaultMaterialPath = "engine/materials/M_Default.mat";
    static constexpr std::string_view kDefaultShaderPath = "engine/shaders/S_Lit.hlsl";

    // What a new shader starts as: lit like the default one, with fewer slots, to have something to change
    static constexpr std::string_view kNewShaderSource = R"(// VSMain and PSMain are the entry points. The material's properties go in the one cbuffer in space2,
// and its textures in space2 too. A comment like "@default 0.5 @range 0 1" sets how a property starts.
// Time and DeltaTime, in seconds, come from crimson/bindings.hlsli. crimson/lighting.hlsli also brings
// crimson/math.hlsli (PI, TAU, EPSILON, ...) and crimson/transforms.hlsli (ObjectToWorldPosition, WorldToClip, ...).
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color; // @default 1 1 1 1
    float Metallic; // @default 0 @range 0 1
    float Roughness; // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space2);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const float3 worldPosition = ObjectToWorldPosition(input.Position);

    VertexOutput output;
    output.ClipPosition = WorldToClip(worldPosition);
    output.Normal = ObjectToWorldNormal(input.Normal);
    output.WorldPosition = worldPosition;
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float3 normal = normalize(input.Normal);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = normal;
    surface.GeometricNormal = normal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic;
    surface.Roughness = Roughness;
    surface.Occlusion = 1.0;

    return float4(ComputeLighting(surface), base.a);
}
)";

    // FNV-1a. An engine resource has no meta to store an id in, and scenes must find it again in any project.
    static AssetID EngineResourceId(std::string_view path)
    {
        uint64_t hash = 0xcbf29ce484222325ull;

        for (const unsigned char c : path)
        {
            hash ^= c;
            hash *= 0x100000001b3ull;
        }

        return AssetID{ hash };
    }

    static std::string NormalizeExt(const fs::path& p)
    {
        std::string e = p.extension().string();
        std::ranges::transform(e, e.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    std::optional<std::string> AssetSource::Read() const
    {
        if (Embedded)
            return std::string(Embedded->Text());

        std::ifstream file(File, std::ios::binary);
        if (!file)
        {
            LOG_ERROR("[Assets] Cannot open '{}'", File.string());
            return std::nullopt;
        }

        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    AssetManager::AssetManager(GpuResourceManager& gpuResources, fs::path root)
        : m_gpuResources(gpuResources), m_root(fs::absolute(std::move(root)).lexically_normal())
    {
        AddImporter(std::make_unique<TextureImporter>());
        AddImporter(std::make_unique<ShaderImporter>());
        AddImporter(std::make_unique<MaterialImporter>());
        AddImporter(std::make_unique<MeshImporter>());
        AddImporter(std::make_unique<SceneImporter>());

        m_stores[static_cast<size_t>(AssetType::Texture)] = std::make_unique<AssetStore<AssetType::Texture>>();
        m_stores[static_cast<size_t>(AssetType::Shader)] = std::make_unique<AssetStore<AssetType::Shader>>();
        m_stores[static_cast<size_t>(AssetType::Material)] = std::make_unique<AssetStore<AssetType::Material>>();
        m_stores[static_cast<size_t>(AssetType::Mesh)] = std::make_unique<AssetStore<AssetType::Mesh>>();

        RegisterEngineResources();

        std::unordered_set<AssetID> seen;
        Scan(seen);

        // For the project's files to stop needing the old names
        UpdateReferencesTo(m_renamedPaths);
    }

    AssetManager::~AssetManager()
    {
        // Each one can be used by the ones before it
        for (const AssetType type : { AssetType::Material, AssetType::Mesh, AssetType::Texture, AssetType::Shader })
            m_stores[static_cast<size_t>(type)]->UnloadAll(m_gpuResources);

        if (m_flatNormal)
            m_gpuResources.DestroyTexture(m_flatNormal);
    }

    TextureHandle AssetManager::EmptySlotTexture(ShaderHandle shader, const std::string& slot)
    {
        const auto& textures = m_gpuResources.GetShaderInterface(shader).Textures;
        const auto it = textures.find(slot);

        if (it == textures.end() || it->second.Default != ShaderTextureDefault::Normal)
            return TextureHandle::Invalid();

        if (!m_flatNormal)
        {
            // Pointing straight out of the surface
            constexpr std::array<unsigned char, 4> pixel = { 128, 128, 255, 255 };
            m_flatNormal = m_gpuResources.CreateTexture({ .Width = 1, .Height = 1, .Format = TextureFormat::RGBA8, .Usage = TextureUsage::Sampled }, pixel.data());
        }

        return m_flatNormal;
    }

    void AssetManager::AddImporter(std::unique_ptr<AssetImporterBase> importer)
    {
        for (std::string_view ext : importer->Extensions())
            m_importersByExtension[std::string(ext)] = importer.get();

        m_importers.push_back(std::move(importer));
    }

    void AssetManager::RegisterEngineResources()
    {
        for (const EmbeddedFile& file : GetEngineResources())
        {
            // Shader includes have no importer, the shader compiler finds those itself
            const auto importer = m_importersByExtension.find(NormalizeExt(file.Path));
            if (importer == m_importersByExtension.end())
                continue;

            AssetInfo info;
            info.Path = std::string(kEnginePathPrefix) + std::string(file.Path);
            info.Id = EngineResourceId(info.Path);
            info.Type = importer->second->Type();
            info.Settings = importer->second->DefaultSettings();
            info.ReadOnly = true;

            const AssetID id = info.Id;
            m_idsByPath[info.Path] = id;
            m_assets.emplace(id, Record{ .Info = std::move(info), .Importer = importer->second, .Embedded = &file });
        }

        const auto exists = [this](std::string_view path) { return m_idsByPath.contains(std::string(path)); };

        for (const auto& [oldPath, newPath] : kRenamedEngineAssets)
        {
            const std::string_view now = FollowRenames(kRenamedEngineAssets, newPath, exists);
            if (now.empty())
            {
                LOG_ERROR("[Assets] {} was renamed to {}, which doesn't exist", oldPath, newPath);
                continue;
            }

            const AssetID id = m_idsByPath.at(std::string(now));

            m_renamedPaths.emplace(oldPath, id);
            m_renamedIds.emplace(EngineResourceId(oldPath), id);
        }
    }

    void AssetManager::Scan(std::unordered_set<AssetID>& seen)
    {
        LOG_INFO("[Assets] Scanning {}", m_root.string());

        // An unreadable folder must not take the editor down, so errors are checked instead of thrown
        std::error_code error;
        fs::recursive_directory_iterator it(m_root, fs::directory_options::skip_permission_denied, error);

        if (error)
        {
            LOG_WARN("[Assets] Can't read {}: {}", m_root.string(), error.message());
            return;
        }

        for (const fs::recursive_directory_iterator end; it != end; it.increment(error))
        {
            if (error)
            {
                LOG_WARN("[Assets] Stopped scanning {}: {}", m_root.string(), error.message());
                break;
            }

            if (it->is_regular_file(error))
                RegisterFile(it->path(), seen);
        }

        LOG_INFO("[Assets] Scan done: {} assets", m_assets.size());
    }

    void AssetManager::RegisterFile(const fs::path& file, std::unordered_set<AssetID>& seen)
    {
        const std::string extension = NormalizeExt(file);
        if (extension == ".meta" || extension == ".hlsli")
            return;

        std::string path = fs::relative(file, m_root).generic_string();

        if (path.starts_with(kEnginePathPrefix))
        {
            LOG_WARN("[Assets] Skipped {}: the engine/ folder is reserved for the engine's own assets", path);
            return;
        }

        const auto importer = m_importersByExtension.find(extension);
        if (importer == m_importersByExtension.end())
        {
            LOG_WARN("[Assets] Skipped (no importer): {}", path);
            return;
        }

        const fs::path metaPath = meta::MetaPathFor(file);
        std::optional<AssetInfo> existing = meta::Read(metaPath);

        // Already known from an earlier scan. If only its meta is gone, it keeps the id it has.
        if (const AssetID known = FindId(path); known && (!existing || existing->Id == known))
        {
            if (!existing && !meta::Write(metaPath, m_assets.at(known).Info))
                LOG_ERROR("[Assets] Failed to write {}", metaPath.string());

            seen.insert(known);
            return;
        }

        bool duplicate = false;

        if (existing)
        {
            if (const auto other = m_assets.find(existing->Id); other != m_assets.end())
            {
                std::error_code error;
                const bool originalStillThere = !other->second.Info.ReadOnly && fs::exists(m_root / fs::path(other->second.Info.Path), error);

                // Moved along with its meta, so nothing using it notices
                if (!originalStillThere && !other->second.Info.ReadOnly)
                {
                    m_idsByPath.erase(other->second.Info.Path);
                    other->second.Info.Path = path;
                    m_idsByPath[path] = existing->Id;
                    seen.insert(existing->Id);
                    return;
                }

                // A file copied along with its meta would otherwise replace the original's entry
                duplicate = true;
            }
        }

        AssetInfo info;

        if (duplicate)
            LOG_WARN("[Assets] {} has the same id as {}, giving it a new one", path, m_assets.at(existing->Id).Info.Path);

        if (existing)
        {
            info = std::move(*existing);
        }
        else
        {
            info.Settings = importer->second->DefaultSettings();
        }

        info.Type = importer->second->Type();
        info.Path = std::move(path);

        if (!existing || duplicate)
        {
            info.Id = AssetID::Generate();

            if (!meta::Write(metaPath, info))
                LOG_ERROR("[Assets] Failed to write {}", metaPath.string());
        }

        LOG_INFO("[Assets] {} [{}] id={}", info.Path, ToString(info.Type), info.Id.ToString());

        m_idsByPath[info.Path] = info.Id;
        const AssetID id = info.Id;
        m_assets.emplace(id, Record{ .Info = std::move(info), .Importer = importer->second });
        seen.insert(id);
    }

    const AssetInfo* AssetManager::GetInfo(AssetID id) const
    {
        const auto it = m_assets.find(id);
        return it != m_assets.end() ? &it->second.Info : nullptr;
    }

    const MissingAssetRef* AssetManager::GetMissing(AssetID id) const
    {
        if (!IsMissing(id))
            return nullptr;

        const auto it = m_missing.find(id);
        return it != m_missing.end() ? &it->second : nullptr;
    }

    bool AssetManager::RememberMissing(AssetID id, MissingAssetRef ref) const
    {
        return id && m_missing.insert_or_assign(id, std::move(ref)).second;
    }

    AssetID AssetManager::FindId(std::string_view path) const
    {
        if (const auto it = m_idsByPath.find(std::string(path)); it != m_idsByPath.end())
            return it->second;

        const auto renamed = m_renamedPaths.find(std::string(path));
        return renamed != m_renamedPaths.end() ? renamed->second : AssetID{};
    }

    AssetID AssetManager::Redirect(AssetID id) const
    {
        if (GetInfo(id))
            return id;

        const auto renamed = m_renamedIds.find(id);
        return renamed != m_renamedIds.end() ? renamed->second : id;
    }

    AssetID AssetManager::FindIdOrWarn(std::string_view path) const
    {
        const AssetID id = FindId(path);
        if (!id)
            LOG_WARN("[Assets] No asset at '{}'", path);
        return id;
    }

    std::vector<const AssetInfo*> AssetManager::List(AssetType type) const
    {
        std::vector<const AssetInfo*> assets;

        for (const auto& [id, record] : m_assets)
            if (record.Info.Type == type)
                assets.push_back(&record.Info);

        std::ranges::sort(assets, {}, &AssetInfo::Path);
        return assets;
    }

    std::vector<const AssetInfo*> AssetManager::ListAll() const
    {
        std::vector<const AssetInfo*> assets;
        assets.reserve(m_assets.size());

        for (const auto& [id, record] : m_assets)
            assets.push_back(&record.Info);

        std::ranges::sort(assets, {}, &AssetInfo::Path);
        return assets;
    }

    bool AssetManager::IsLoaded(AssetID id) const
    {
        const AssetInfo* info = GetInfo(id);
        const AssetStoreBase* store = info ? m_stores[static_cast<size_t>(info->Type)].get() : nullptr;
        return store && store->IsLoaded(id);
    }

    MaterialID AssetManager::DefaultMaterial() const
    {
        return Find<AssetType::Material>(kDefaultMaterialPath);
    }

    template <AssetType T>
    AssetStore<T>& AssetManager::Store()
    {
        // Each one is made for its own place, in the constructor
        return static_cast<AssetStore<T>&>(*m_stores[static_cast<size_t>(T)]);
    }

    template <AssetType T>
    typename AssetStore<T>::Slot* AssetManager::SlotFor(AssetID id)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != T)
            return nullptr;

        return &Store<T>().GetOrCreate(id, static_cast<AssetImporter<T>&>(*it->second.Importer));
    }

    AssetSource AssetManager::SourceOf(const Record& record) const
    {
        return AssetSource{ record.Info, record.Embedded ? fs::path() : m_root / fs::path(record.Info.Path), record.Embedded };
    }

    template <AssetType T>
    std::optional<LoadedAsset<T>> AssetManager::Import(const Record& record, typename AssetStore<T>::Slot& slot)
    {
        ImportContext ctx{ m_gpuResources, *this };
        const AssetSource source = SourceOf(record);

        // Before the import, for a save during it to count as a change
        std::error_code error;
        if constexpr (T == AssetType::Shader)
        {
            if (!record.Embedded)
                slot.State.FileTime = fs::last_write_time(source.File, error);
        }

        std::optional<LoadedAsset<T>> loaded = slot.Importer->Load(source, ctx);

        if constexpr (T == AssetType::Shader)
        {
            slot.State.Dependencies.clear();

            for (fs::path& dependency : ctx.Dependencies)
            {
                const fs::file_time_type time = fs::last_write_time(dependency, error);
                slot.State.Dependencies.emplace_back(std::move(dependency), time);
            }
        }

        return loaded;
    }

    template <AssetType T>
    LoadResult<T> AssetManager::Load(TypedAssetID<T> id)
    {
        auto* slot = SlotFor<T>(id);

        if (slot && !slot->Tried())
        {
            slot->Loaded = Import<T>(m_assets.at(id), *slot);
            slot->Failed = !slot->Loaded;

            if (slot->Failed)
                LOG_ERROR("[Assets] Failed to load {}", GetInfo(id)->Path);
        }

        const LoadedAsset<T>* loaded = slot && slot->Loaded ? &*slot->Loaded : nullptr;

        if constexpr (std::is_pointer_v<LoadResult<T>>)
            return loaded;
        else
            return loaded ? *loaded : LoadedAsset<T>::Invalid();
    }

    template TextureHandle AssetManager::Load(TextureID);
    template ShaderHandle AssetManager::Load(ShaderID);
    template MaterialHandle AssetManager::Load(MaterialID);
    template const Mesh* AssetManager::Load(MeshID);

    void AssetManager::WarnWrongType(std::string_view path, AssetType wanted) const
    {
        LOG_WARN("[Assets] '{}' isn't a {}", path, ToString(wanted));
    }

    fs::path AssetManager::GetFilePath(AssetID id) const
    {
        const AssetInfo* info = GetInfo(id);
        return info && !info->ReadOnly ? m_root / fs::path(info->Path) : fs::path();
    }

    std::vector<AssetID> AssetManager::Refresh()
    {
        std::unordered_map<std::string, AssetID> moved;

        for (const auto& [id, record] : m_assets)
            if (!record.Info.ReadOnly)
                moved.emplace(record.Info.Path, id);

        std::unordered_set<AssetID> seen;
        Scan(seen);

        std::vector<AssetID> removed;

        for (const auto& [id, record] : m_assets)
            if (!record.Info.ReadOnly && !seen.contains(id))
                removed.push_back(id);

        RemoveAssets(removed);

        // Only now, as a renamed folder's materials don't know where they are until the scan has been past them
        std::erase_if(moved, [this](const auto& asset)
        {
            const AssetInfo* info = GetInfo(asset.second);
            return !info || info->Path == asset.first;
        });

        UpdateReferencesTo(moved);
        return removed;
    }

    void AssetManager::UpdateReferencesTo(const std::unordered_map<std::string, AssetID>& moved)
    {
        if (moved.empty())
            return;

        for (const auto& [id, record] : m_assets)
        {
            const bool isMaterial = record.Info.Type == AssetType::Material;
            if ((!isMaterial && record.Info.Type != AssetType::Scene) || record.Info.ReadOnly)
                continue;

            const fs::path file = m_root / fs::path(record.Info.Path);
            const std::optional<std::string> text = AssetSource{ record.Info, file, nullptr }.Read();
            if (!text)
                continue;

            const std::optional<std::string> updated = isMaterial
                ? UpdateMaterialReferences(*text, moved, *this)
                : UpdateSceneReferences(*text, moved, *this);
            if (!updated)
                continue;

            if (std::ofstream stream(file, std::ios::binary); !(stream << *updated))
                LOG_ERROR("[Assets] Can't write {}", record.Info.Path);
            else
                LOG_INFO("[Assets] Updated the references in {}", record.Info.Path);
        }
    }

    void AssetManager::RemoveAssets(std::span<const AssetID> ids)
    {
        const auto isRemoved = [ids](AssetID id) { return std::ranges::find(ids, id) != ids.end(); };

        // Materials first, as they can use the textures and shaders being removed
        for (auto& [id, material] : Store<AssetType::Material>())
        {
            // Read from its file if nothing asked for it yet. Its slot is there already, so none is added meanwhile.
            if (isRemoved(id) || !material.Loaded || !GetMaterialData(MaterialID(id)))
                continue;

            MaterialData& data = *material.State.Data;

            if (isRemoved(data.Shader))
            {
                material.Unload(m_gpuResources);
                material.State.Data.reset();
                continue;
            }

            // The texture stays in the data as a missing one, for it to work again if the file comes back
            for (const auto& [slot, texture] : data.Textures)
            {
                if (isRemoved(texture))
                    m_gpuResources.SetMaterialTexture(*material.Loaded, slot, EmptySlotTexture(Load(data.Shader), slot));
            }
        }

        for (const AssetType type : { AssetType::Scene, AssetType::Material, AssetType::Mesh, AssetType::Texture, AssetType::Shader })
        {
            for (const AssetID id : ids)
            {
                const auto it = m_assets.find(id);
                if (it == m_assets.end() || it->second.Info.Type != type)
                    continue;

                RememberMissing(id, { .Path = it->second.Info.Path, .Type = it->second.Info.Type });

                if (AssetStoreBase* store = m_stores[static_cast<size_t>(type)].get())
                    store->Forget(id, m_gpuResources);

                m_idsByPath.erase(it->second.Info.Path);
                m_assets.erase(it);
            }
        }
    }

    static bool IsUsableAssetPath(std::string_view path)
    {
        const fs::path parsed(path);

        if (path.empty() || parsed.is_absolute() || !parsed.has_filename() || path.starts_with(kEnginePathPrefix))
            return false;

        return std::ranges::none_of(parsed, [](const fs::path& part) { return part == ".."; });
    }

    MaterialID AssetManager::CreateMaterial(std::string_view path)
    {
        MaterialData data;
        data.Shader = Find<AssetType::Shader>(kDefaultShaderPath);
        data.Properties["Color"] = { 1.0f, 1.0f, 1.0f, 1.0f };

        return As<AssetType::Material>(CreateAssetFile(path, ".mat", WriteMaterialData(data, *this)));
    }

    SceneID AssetManager::CreateScene(std::string_view path)
    {
        SceneNode sun;
        sun.Id = AssetID::Generate();
        sun.Name = "Sun";
        sun.Position = glm::vec3(0.0f, 8.0f, 0.0f);
        sun.RotationDegrees = glm::degrees(glm::eulerAngles(glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), glm::vec3(0.0f, 1.0f, 0.0f))));
        sun.Light = LightComponent{ .Type = LightType::Directional, .Intensity = 1.2f, .CastsShadow = true };

        SceneData data;
        data.Roots.push_back(std::move(sun));

        return As<AssetType::Scene>(CreateAssetFile(path, ".scene", WriteSceneData(data, *this)));
    }

    ShaderID AssetManager::CreateShader(std::string_view path)
    {
        return As<AssetType::Shader>(CreateAssetFile(path, ".hlsl", std::string(kNewShaderSource)));
    }

    // The folder an asset is in, with a slash at the end, or empty at the top
    static std::string FolderOf(const std::string& path)
    {
        const size_t folderEnd = path.rfind('/');
        return folderEnd == std::string::npos ? std::string{} : path.substr(0, folderEnd + 1);
    }

    // Where the material for one of a mesh's slots goes
    static std::string SlotMaterialPath(const std::string& meshPath, std::string_view slot)
    {
        return FolderOf(meshPath) + "materials/" + std::string(slot) + ".mat";
    }

    SlotMaterials AssetManager::CreateSlotMaterials(MeshID mesh)
    {
        SlotMaterials result;

        const AssetInfo* info = GetInfo(mesh);
        if (!info || info->Type != AssetType::Mesh || info->ReadOnly || NormalizeExt(fs::path(info->Path)) != ".obj")
        {
            LOG_ERROR("[Assets] Only a project's .obj meshes can get materials from their .mtl");
            return result;
        }

        // Copied, as creating files adds assets, which can move the info
        const std::string meshPath = info->Path;
        std::vector<TextureID> dataTextures;

        for (const tinyobj::material_t& mtl : ReadMtlMaterials(m_root / fs::path(meshPath)))
        {
            const std::string path = SlotMaterialPath(meshPath, mtl.name);

            std::error_code error;
            if (FindId(path) || fs::exists(m_root / fs::path(path), error))
            {
                ++result.Kept;
                continue;
            }

            MtlMaterial material = MaterialFromMtl(mtl, FolderOf(meshPath), *this);

            if (const MaterialID created = As<AssetType::Material>(CreateAssetFile(path, ".mat", WriteMaterialData(material.Data, *this))))
            {
                result.Created.push_back(created);
                std::ranges::move(material.DataTextures, std::back_inserter(dataTextures));
            }
        }

        // Normal maps hold directions, which sRGB decoding would bend
        for (const TextureID texture : dataTextures)
        {
            const AssetInfo* textureInfo = GetInfo(texture);
            if (!textureInfo || textureInfo->ReadOnly || !textureInfo->Settings.value("srgb", true))
                continue;

            nlohmann::json settings = textureInfo->Settings;
            settings["srgb"] = false;
            ReimportTexture(texture, std::move(settings));
        }

        LOG_INFO("[Assets] {}: made {} slot materials, and kept {} that were there", meshPath, result.Created.size(), result.Kept);
        return result;
    }

    AssetID AssetManager::CreateAssetFile(std::string_view path, std::string_view extension, const std::string& contents)
    {
        if (!IsUsableAssetPath(path) || NormalizeExt(fs::path(path)) != extension)
        {
            LOG_ERROR("[Assets] Can't create a {} file at '{}'", extension, path);
            return {};
        }

        std::error_code error;
        const fs::path file = m_root / fs::path(path);

        if (fs::exists(file, error))
        {
            LOG_ERROR("[Assets] '{}' already exists", path);
            return {};
        }

        fs::create_directories(file.parent_path(), error);

        if (std::ofstream stream(file, std::ios::binary); !(stream << contents))
        {
            LOG_ERROR("[Assets] Can't write '{}'", path);
            return {};
        }

        std::unordered_set<AssetID> seen;
        RegisterFile(file, seen);
        return FindId(path);
    }

    bool AssetManager::Rename(AssetID id, std::string_view newPath)
    {
        const auto it = m_assets.find(id);

        if (it == m_assets.end() || it->second.Info.ReadOnly || !IsUsableAssetPath(newPath))
        {
            LOG_ERROR("[Assets] Can't rename an asset to '{}'", newPath);
            return false;
        }

        Record& record = it->second;

        // The extension decides what the file is, and an asset can't change what it is
        if (NormalizeExt(fs::path(newPath)) != NormalizeExt(fs::path(record.Info.Path)))
        {
            LOG_ERROR("[Assets] Renaming to '{}' would change the type of {}", newPath, record.Info.Path);
            return false;
        }

        std::error_code error;
        const fs::path from = m_root / fs::path(record.Info.Path);
        const fs::path to = m_root / fs::path(newPath);

        if (fs::exists(to, error))
        {
            LOG_ERROR("[Assets] '{}' already exists", newPath);
            return false;
        }

        fs::create_directories(to.parent_path(), error);
        fs::rename(from, to, error);

        if (error)
        {
            LOG_ERROR("[Assets] Can't rename {}: {}", record.Info.Path, error.message());
            return false;
        }

        fs::rename(meta::MetaPathFor(from), meta::MetaPathFor(to), error);
        if (error)
            LOG_WARN("[Assets] {} lost its meta file: {}", newPath, error.message());

        const std::string oldPath = std::exchange(record.Info.Path, std::string(newPath));

        m_idsByPath.erase(oldPath);
        m_idsByPath[record.Info.Path] = id;

        UpdateReferencesTo({ { oldPath, id } });

        return true;
    }

    bool AssetManager::Delete(AssetID id)
    {
        const auto it = m_assets.find(id);

        if (it == m_assets.end() || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This asset can't be deleted");
            return false;
        }

        std::error_code error;
        const fs::path file = m_root / fs::path(it->second.Info.Path);

        fs::remove(file, error);

        if (error)
        {
            LOG_ERROR("[Assets] Can't delete {}: {}", it->second.Info.Path, error.message());
            return false;
        }

        fs::remove(meta::MetaPathFor(file), error);

        const std::array removed = { id };
        RemoveAssets(removed);
        return true;
    }

    MaterialID AssetManager::FindSlotMaterial(MeshID mesh, std::string_view slot) const
    {
        const AssetInfo* info = GetInfo(mesh);
        if (!info || info->Type != AssetType::Mesh)
            return {};

        return Find<AssetType::Material>(SlotMaterialPath(info->Path, slot));
    }

    TextureHandle AssetManager::CreateTexturePreview(TextureID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Texture)
            return TextureHandle::Invalid();

        const AssetSource source = SourceOf(it->second);

        const std::optional<std::string> file = source.Read();
        if (!file)
            return TextureHandle::Invalid();

        const TextureLoadOptions options{ .GenerateMips = true, .FlipY = source.Info.Settings.value("flipY", false) };
        return CreateTextureFromMemory(m_gpuResources, std::as_bytes(std::span(*file)), options);
    }

    std::optional<MaterialData> AssetManager::ReadMaterialFile(MaterialID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Material)
            return std::nullopt;

        const AssetSource source = SourceOf(it->second);

        const std::optional<std::string> text = source.Read();
        return text ? ParseMaterialData(*text, *this, source.Info.Path) : std::nullopt;
    }

    std::optional<SceneData> AssetManager::ReadSceneFile(SceneID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Scene)
            return std::nullopt;

        const AssetSource source = SourceOf(it->second);

        const std::optional<std::string> text = source.Read();
        return text ? ParseSceneData(*text, *this, source.Info.Path) : std::nullopt;
    }

    const MaterialData* AssetManager::GetMaterialData(MaterialID id)
    {
        auto* slot = SlotFor<AssetType::Material>(id);
        if (!slot)
            return nullptr;

        if (!slot->State.Data)
            slot->State.Data = ReadMaterialFile(id);

        return slot->State.Data ? &*slot->State.Data : nullptr;
    }

    MaterialApplyResult AssetManager::ApplyMaterial(MaterialID id, const MaterialData& data)
    {
        const AssetInfo* info = GetInfo(id);
        if (!info || info->ReadOnly)
            return MaterialApplyResult::Failed;

        const MaterialHandle current = Load(id);
        const MaterialData* currentData = GetMaterialData(id);

        auto& slot = *SlotFor<AssetType::Material>(id);
        ImportContext ctx{ m_gpuResources, *this };

        // A material's GPU resources are laid out for its shader, so another shader needs a new material
        if (!current || !currentData || currentData->Shader != data.Shader)
        {
            const MaterialHandle replacement = CreateMaterialFromData(data, ctx, info->Path);
            if (!replacement)
                return MaterialApplyResult::Failed;

            slot.Unload(m_gpuResources);
            slot.Loaded = replacement;
            slot.Failed = false;
            slot.State.Data = data;
            return MaterialApplyResult::Replaced;
        }

        // Only what changed: replacing a texture has to wait for the GPU, and a dragged color slider gets here every frame
        // The defaults first, for a property that is no longer in the data to go back to its default
        const ShaderHandle shader = Load(data.Shader);
        ApplyShaderPropertyDefaults(shader, current, ctx);

        MaterialData values = data;
        values.Textures.clear();
        ApplyMaterialValues(values, current, ctx);

        for (const auto& [name, texture] : data.Textures)
        {
            const auto previous = currentData->Textures.find(name);
            if (previous != currentData->Textures.end() && previous->second == texture)
                continue;

            if (const TextureHandle handle = Load(texture))
                m_gpuResources.SetMaterialTexture(current, name, handle);
        }

        for (const auto& [name, texture] : currentData->Textures)
        {
            if (!data.Textures.contains(name))
                m_gpuResources.SetMaterialTexture(current, name, EmptySlotTexture(shader, name));
        }

        slot.State.Data = data;
        return MaterialApplyResult::Updated;
    }

    bool AssetManager::SaveMaterial(MaterialID id, const MaterialData& data)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Material || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This material can't be saved");
            return false;
        }

        const fs::path file = m_root / fs::path(it->second.Info.Path);

        if (std::ofstream stream(file, std::ios::binary); !(stream << WriteMaterialData(data, *this)))
        {
            LOG_ERROR("[Assets] Can't write {}", it->second.Info.Path);
            return false;
        }

        return true;
    }

    bool AssetManager::SaveScene(SceneID id, const SceneData& data)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Scene)
        {
            LOG_ERROR("[Assets] This scene can't be saved");
            return false;
        }

        // A scene is a lot of work, so a write that fails halfway shouldn't take the old file with it
        const fs::path file = m_root / fs::path(it->second.Info.Path);
        fs::path temporary = file;
        temporary += ".saving";

        {
            std::ofstream stream(temporary, std::ios::binary);
            stream << WriteSceneData(data, *this);
            stream.close();

            if (!stream)
            {
                LOG_ERROR("[Assets] Can't write {}", it->second.Info.Path);
                return false;
            }
        }

        std::error_code error;
        fs::rename(temporary, file, error);

        if (error)
        {
            LOG_ERROR("[Assets] Can't write {}: {}", it->second.Info.Path, error.message());
            fs::remove(temporary, error);
            return false;
        }

        return true;
    }

    bool AssetManager::ReimportTexture(TextureID id, nlohmann::json settings)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This texture can't be reimported");
            return false;
        }

        Record& record = it->second;
        record.Info.Settings = std::move(settings);

        if (!meta::Write(meta::MetaPathFor(m_root / fs::path(record.Info.Path)), record.Info))
            LOG_ERROR("[Assets] Failed to write the meta file of {}", record.Info.Path);

        auto& slot = *SlotFor<AssetType::Texture>(id);

        // Not in use yet, so the next load picks the settings up
        if (!slot.Loaded)
        {
            slot.Failed = false;
            return true;
        }

        const std::optional<TextureHandle> replacement = Import<AssetType::Texture>(record, slot);
        if (!replacement)
        {
            LOG_ERROR("[Assets] Failed to reimport {}", record.Info.Path);
            return false;
        }

        for (auto& [materialId, material] : Store<AssetType::Material>())
        {
            if (!material.Loaded)
                continue;

            const MaterialData* data = GetMaterialData(MaterialID(materialId));
            if (!data)
                continue;

            for (const auto& [name, used] : data->Textures)
                if (used == id)
                    m_gpuResources.SetMaterialTexture(*material.Loaded, name, *replacement);
        }

        slot.Unload(m_gpuResources);
        slot.Loaded = replacement;
        return true;
    }

    // A file that can't be looked at counts as unchanged, as it may be in the middle of being saved
    static bool HasChanged(const fs::path& file, fs::file_time_type time)
    {
        std::error_code error;
        const fs::file_time_type now = fs::last_write_time(file, error);
        return !error && now != time;
    }

    std::vector<AssetID> AssetManager::ReloadChangedShaders()
    {
        std::vector<AssetID> reloaded;

        for (auto& [id, shader] : Store<AssetType::Shader>())
        {
            // One that was never loaded gets the new version when it is
            const Record& record = m_assets.at(id);
            if (record.Info.ReadOnly || !shader.Tried())
                continue;

            const bool changed = HasChanged(m_root / fs::path(record.Info.Path), shader.State.FileTime)
                || std::ranges::any_of(shader.State.Dependencies, [](const auto& dependency) { return HasChanged(dependency.first, dependency.second); });

            if (changed)
                std::ranges::move(ReloadShader(id, record), std::back_inserter(reloaded));
        }

        return reloaded;
    }

    std::vector<AssetID> AssetManager::ReloadShader(AssetID id, const Record& record)
    {
        auto& shader = *SlotFor<AssetType::Shader>(id);
        const std::optional<ShaderHandle> replacement = Import<AssetType::Shader>(record, shader);

        if (!replacement)
        {
            LOG_ERROR("[Assets] {} changed but doesn't compile, so the last version that did stays", record.Info.Path);
            return {};
        }

        // In place before the materials are made again, as they get their shader from here
        std::optional<ShaderHandle> previous = std::exchange(shader.Loaded, replacement);
        shader.Failed = false;

        std::vector<AssetID> reloaded = { id };
        ImportContext ctx{ m_gpuResources, *this };

        for (auto& [materialId, material] : Store<AssetType::Material>())
        {
            if (!material.Tried())
                continue;

            // What the material is using, unsaved edits included
            const MaterialData* data = GetMaterialData(MaterialID(materialId));
            if (!data || data->Shader != id)
                continue;

            // It may have failed because of the shader, so it gets another go on its next load
            if (material.Failed)
            {
                material.Failed = false;
                continue;
            }

            const MaterialHandle recreated = CreateMaterialFromData(*data, ctx, m_assets.at(materialId).Info.Path);

            // The old one can't stay, as its shader is about to go
            material.Unload(m_gpuResources);
            if (recreated)
                material.Loaded = recreated;
            material.Failed = !recreated;

            reloaded.push_back(materialId);
        }

        if (previous)
            shader.Importer->Unload(*previous, m_gpuResources);

        LOG_INFO("[Assets] Reloaded {}", record.Info.Path);
        return reloaded;
    }
}
