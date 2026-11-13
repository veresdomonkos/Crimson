#pragma once
#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    // What can be done to a store without knowing its type, for what goes over every type of asset
    class AssetStoreBase
    {
    public:
        virtual ~AssetStoreBase() = default;

        [[nodiscard]] virtual bool IsLoaded(AssetID id) const = 0;

        // Unloads it, and drops what it kept
        virtual void Forget(AssetID id, GpuResourceManager& gpu) = 0;
        virtual void UnloadAll(GpuResourceManager& gpu) = 0;

        // The assets that were tried and use this one
        [[nodiscard]] virtual std::vector<AssetID> UsersOf(AssetID used) const = 0;

        // Tried, and its file or one it included changed since. A file that can't be looked at counts as unchanged,
        // as it may be in the middle of being saved.
        [[nodiscard]] virtual bool HasChangedOnDisk(AssetID id) const = 0;

        // Imports it again. One that fails keeps the version it had. On success the users are updated before the old
        // version is freed, as they may still be using it.
        virtual bool Reload(AssetID id, const AssetSource& source, ImportContext& ctx, const std::function<void()>& updateUsers) = 0;

        // Updates a user of an asset that was loaded again or, if removed, is gone. Returns whether its handle changed.
        virtual bool UpdateUser(AssetID user, AssetID used, bool removed, const AssetSource& source, ImportContext& ctx) = 0;

        // Reads what a loaded asset is using, if its type has data and that wasn't read yet
        virtual void ReadData(AssetID id, const AssetSource& source, const AssetManager& assets) = 0;

        // Lets one that failed be tried again
        virtual void Retry(AssetID id) = 0;
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

            std::optional<AssetData<T>> Data; // what it is using, which edits may have made different from its file
            std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> Files; // its own file, then the ones it included, as of its last import
            std::vector<AssetID> Uses;

            [[nodiscard]] bool Tried() const { return Loaded || Failed; }

            void Unload(GpuResourceManager& gpu)
            {
                if (Loaded)
                {
                    Importer->Unload(*Loaded, gpu);
                    Loaded.reset();
                }
            }

            // Records what it was made from even when that fails, so a failed one isn't tried again until it changes
            std::optional<LoadedAsset<T>> Import(const AssetSource& source, ImportContext& ctx)
            {
                std::error_code error;
                Files.clear();

                // Before the import, for a save during it to count as a change
                if (!source.File.empty())
                    Files.emplace_back(source.File, std::filesystem::last_write_time(source.File, error));

                std::optional<LoadedAsset<T>> loaded = Importer->Load(source, ctx);

                for (std::filesystem::path& dependency : ctx.Dependencies)
                {
                    const std::filesystem::file_time_type time = std::filesystem::last_write_time(dependency, error);
                    Files.emplace_back(std::move(dependency), time);
                }

                Uses = ctx.Uses;
                return loaded;
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

        [[nodiscard]] std::vector<AssetID> UsersOf(AssetID used) const override
        {
            std::vector<AssetID> users;

            for (const auto& [id, slot] : m_slots)
            {
                if (slot.Tried() && std::ranges::find(slot.Uses, used) != slot.Uses.end())
                    users.push_back(id);
            }

            return users;
        }

        [[nodiscard]] bool HasChangedOnDisk(AssetID id) const override
        {
            const Slot* slot = Find(id);
            if (!slot || !slot->Tried())
                return false;

            return std::ranges::any_of(slot->Files, [](const auto& file)
            {
                std::error_code error;
                const std::filesystem::file_time_type now = std::filesystem::last_write_time(file.first, error);
                return !error && now != file.second;
            });
        }

        bool Reload(AssetID id, const AssetSource& source, ImportContext& ctx, const std::function<void()>& updateUsers) override
        {
            Slot* slot = Find(id);
            if (!slot)
                return false;

            std::optional<LoadedAsset<T>> replacement = slot->Import(source, ctx);
            if (!replacement)
                return false;

            // In place before the users are updated, as they get what they use from here
            std::optional<LoadedAsset<T>> previous = std::exchange(slot->Loaded, std::move(replacement));
            slot->Failed = false;

            updateUsers();

            if (previous)
                slot->Importer->Unload(*previous, ctx.Gpu);

            return true;
        }

        bool UpdateUser(AssetID user, AssetID used, bool removed, const AssetSource& source, ImportContext& ctx) override
        {
            Slot* slot = Find(user);
            if (!slot)
                return false;

            // It may have failed because of what it uses, so it gets another go on its next load
            if (slot->Failed && !removed)
            {
                slot->Failed = false;
                return false;
            }

            if (!slot->Loaded)
                return false;

            if (slot->Importer->UpdateUse(*slot->Loaded, source.Info, used, ctx))
                return false;

            if (removed)
            {
                slot->Unload(ctx.Gpu);
                slot->Data.reset();
                return true;
            }

            // Unsaved edits survive, as it is made again from what it is using
            ctx.Uses.clear();
            std::optional<LoadedAsset<T>> recreated = slot->Data
                ? slot->Importer->Create(*slot->Data, source, ctx)
                : slot->Import(source, ctx);

            // The old one can't stay, as what it used is about to go
            slot->Unload(ctx.Gpu);
            slot->Failed = !recreated;

            if (recreated)
            {
                slot->Loaded = std::move(recreated);
                slot->Uses = ctx.Uses;
            }

            return true;
        }

        void ReadData(AssetID id, const AssetSource& source, const AssetManager& assets) override
        {
            if constexpr (!std::is_same_v<AssetData<T>, NoAssetData>)
            {
                Slot* slot = Find(id);
                if (!slot || !slot->Loaded || slot->Data)
                    return;

                if (const std::optional<std::string> text = source.Read())
                    slot->Data = AssetTraits<T>::Parse(*text, assets, source.Info.Path);
            }
        }

        void Retry(AssetID id) override
        {
            if (Slot* slot = Find(id))
                slot->Failed = false;
        }

        // Iterates (AssetID, Slot) pairs. Nothing may be added or removed meanwhile.
        auto begin() { return m_slots.begin(); }
        auto end() { return m_slots.end(); }
    private:
        // Node based, so a slot stays where it is while loading one asset loads others
        std::unordered_map<AssetID, Slot> m_slots;
    };
}
