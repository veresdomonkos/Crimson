#pragma once
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/material_data.hpp"

namespace crimson
{
    // What an asset of a type keeps besides what it loads as
    template <AssetType T>
    struct AssetState {};

    template <>
    struct AssetState<AssetType::Shader>
    {
        // When its file and the files it included last changed, as of its last load
        std::filesystem::file_time_type FileTime;
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> Dependencies;
    };

    template <>
    struct AssetState<AssetType::Material>
    {
        std::optional<MaterialData> Data; // what it is using, which edits may have made different from its file
    };

    // What can be done to a store without knowing its type, for what goes over every type of asset
    class AssetStoreBase
    {
    public:
        virtual ~AssetStoreBase() = default;

        [[nodiscard]] virtual bool IsLoaded(AssetID id) const = 0;

        // Unloads it, and drops what it kept
        virtual void Forget(AssetID id, GpuResourceManager& gpu) = 0;
        virtual void UnloadAll(GpuResourceManager& gpu) = 0;
    };

    // The assets of one type that were loaded or asked about
    template <AssetType T>
    class AssetStore final : public AssetStoreBase
    {
    public:
        struct Slot
        {
            AssetImporter<T>* Importer = nullptr;
            std::optional<LoadedAsset<T>> Loaded;
            bool Failed = false; // not tried again until something about it changes
            AssetState<T> State;

            [[nodiscard]] bool Tried() const { return Loaded || Failed; }

            void Unload(GpuResourceManager& gpu)
            {
                if (Loaded)
                {
                    Importer->Unload(*Loaded, gpu);
                    Loaded.reset();
                }
            }
        };

        Slot& GetOrCreate(AssetID id, AssetImporter<T>& importer)
        {
            Slot& slot = m_slots[id];
            slot.Importer = &importer;
            return slot;
        }

        [[nodiscard]] Slot* Find(AssetID id)
        {
            const auto it = m_slots.find(id);
            return it != m_slots.end() ? &it->second : nullptr;
        }

        [[nodiscard]] const Slot* Find(AssetID id) const
        {
            const auto it = m_slots.find(id);
            return it != m_slots.end() ? &it->second : nullptr;
        }

        // Iterates (AssetID, Slot) pairs. Nothing may be added or removed meanwhile.
        auto begin() { return m_slots.begin(); }
        auto end() { return m_slots.end(); }

        [[nodiscard]] bool IsLoaded(AssetID id) const override
        {
            const Slot* slot = Find(id);
            return slot && slot->Loaded;
        }

        void Forget(AssetID id, GpuResourceManager& gpu) override
        {
            if (Slot* slot = Find(id))
            {
                slot->Unload(gpu);
                m_slots.erase(id);
            }
        }

        void UnloadAll(GpuResourceManager& gpu) override
        {
            for (auto& [id, slot] : m_slots)
                slot.Unload(gpu);
        }
    private:
        // Node based, so a slot stays where it is while loading one asset loads others
        std::unordered_map<AssetID, Slot> m_slots;
    };
}
