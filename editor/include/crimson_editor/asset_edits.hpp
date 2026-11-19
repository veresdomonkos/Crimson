#pragma once
#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/edit_history.hpp"

namespace crimson::editor
{
    // Unsaved changes to assets, which take part in the edit history. An edit shows in the scene right away, and only
    // reaches the file on Save. Each asset type has its own table of edits, made when the first one of that type is
    // used, so a type that can be edited needs only an Inspector that calls Get and Set.
    //
    // The state of an asset from before a step is recorded by Set itself, and the history's Commit takes it as a change.
    // Undoing and redoing a change shows it on the next Apply, like an edit.
    //
    // Everything here can be called from a panel, except Apply, which changes GPU resources and has to run between frames.
    class AssetEdits final : public EditSource
    {
    public:
        explicit AssetEdits(AssetManager& assets) : m_assets(assets) {}

        // The asset as it is being edited. Null if it can't be read.
        template <typename T>
        [[nodiscard]] const AssetData<T>* Get(TypedAssetID<T> id) { return TableFor<T>().Get(id); }

        template <typename T>
        void Set(TypedAssetID<T> id, const AssetData<T>& data) { TableFor<T>().Set(id, data); }

        [[nodiscard]] bool IsDirty(AssetID id) const;
        [[nodiscard]] bool HasUnsavedChanges() const;

        bool Save(AssetID id);
        bool SaveAll();
        void DiscardAll();
        void Forget(AssetID id); // any asset, as it is called for whatever was removed

        // Shows the edits made since the last call. Returns true if an asset got a new handle, which happens when
        // a material's shader changes, so whatever holds the old one has to get it again. An edit that can't be shown is undone.
        bool Apply();

        void Collect(std::vector<std::unique_ptr<EditChange>>& changes) override;
        void Discard() override;
    private:
        class TableBase
        {
        public:
            virtual ~TableBase() = default;

            [[nodiscard]] virtual bool IsDirty(AssetID id) const = 0;
            [[nodiscard]] virtual bool HasUnsavedChanges() const = 0;
            virtual bool Save(AssetID id) = 0;
            virtual bool SaveAll() = 0;
            virtual void DiscardAll() = 0;
            virtual void Forget(AssetID id) = 0;
            virtual bool Apply() = 0;
            virtual void Collect(std::vector<std::unique_ptr<EditChange>>& changes) = 0;
            virtual void Discard() = 0;
        };

        // The edits to the assets of one type
        template <typename T>
        class Table final : public TableBase
        {
        public:
            explicit Table(AssetManager& assets) : m_assets(assets) {}

            [[nodiscard]] const AssetData<T>* Get(TypedAssetID<T> id)
            {
                const Edit* edit = GetOrCreateEdit(id);
                return edit ? &edit->Working : nullptr;
            }

            void Set(TypedAssetID<T> id, const AssetData<T>& data)
            {
                const AssetInfo* info = m_assets.GetInfo(id);
                if (!info || info->ReadOnly)
                    return;

                if (Edit* edit = GetOrCreateEdit(id))
                {
                    // Only the first state in a step counts, so a drag keeps what it started from
                    m_before.try_emplace(id, edit->Working);

                    edit->Working = data;
                    edit->Pending = true;
                }
            }

            [[nodiscard]] bool IsDirty(AssetID id) const override
            {
                const auto it = m_edits.find(m_assets.As<T>(id));
                return it != m_edits.end() && it->second.Working != it->second.Saved;
            }

            [[nodiscard]] bool HasUnsavedChanges() const override
            {
                return std::ranges::any_of(m_edits, [](const auto& entry) { return entry.second.Working != entry.second.Saved; });
            }

            bool Save(AssetID id) override { return SaveEdit(m_assets.As<T>(id)); }

            bool SaveAll() override
            {
                bool saved = true;

                for (const auto& [id, edit] : m_edits)
                {
                    if (edit.Working != edit.Saved)
                        saved = SaveEdit(id) && saved;
                }

                return saved;
            }

            void DiscardAll() override
            {
                for (auto& [id, edit] : m_edits)
                {
                    if (edit.Working != edit.Saved)
                    {
                        edit.Working = edit.Saved;
                        edit.Pending = true;
                    }
                }
            }

            void Forget(AssetID id) override
            {
                std::erase_if(m_edits, [id](const auto& entry) { return AssetID(entry.first) == id; });
                std::erase_if(m_before, [id](const auto& entry) { return AssetID(entry.first) == id; });
            }

            bool Apply() override
            {
                bool replaced = false;

                for (auto& [id, edit] : m_edits)
                {
                    if (!edit.Pending)
                        continue;

                    edit.Pending = false;

                    switch (m_assets.Apply(id, edit.Working))
                    {
                        case AssetUpdate::Replaced:
                            replaced = true;
                            break;

                        case AssetUpdate::Updated:
                            break;

                        case AssetUpdate::Failed:
                            if (const AssetData<T>* live = m_assets.GetData(id))
                                edit.Working = *live;
                            break;
                    }
                }

                return replaced;
            }

            void Collect(std::vector<std::unique_ptr<EditChange>>& changes) override
            {
                std::vector<Entry> entries;

                for (auto& [id, before] : m_before)
                {
                    const auto it = m_edits.find(id);
                    if (it != m_edits.end() && it->second.Working != before)
                        entries.push_back({ id, std::move(before), it->second.Working });
                }

                m_before.clear();

                if (!entries.empty())
                    changes.push_back(std::make_unique<Change>(*this, std::move(entries)));
            }

            void Discard() override { m_before.clear(); }
        private:
            struct Edit
            {
                AssetData<T> Working;
                AssetData<T> Saved;
                bool Pending = false;
            };

            // One asset as a step found it and left it
            struct Entry
            {
                TypedAssetID<T> Id;
                AssetData<T> Before;
                AssetData<T> After;
            };

            // What one commit took from this table
            class Change final : public EditChange
            {
            public:
                Change(Table& table, std::vector<Entry> entries) : m_table(table), m_entries(std::move(entries)) {}

                void Undo() override
                {
                    for (const Entry& entry : std::views::reverse(m_entries))
                        m_table.Restore(entry.Id, entry.Before);
                }

                void Redo() override
                {
                    for (const Entry& entry : m_entries)
                        m_table.Restore(entry.Id, entry.After);
                }
            private:
                Table& m_table;
                std::vector<Entry> m_entries;
            };

            Edit* GetOrCreateEdit(TypedAssetID<T> id)
            {
                if (const auto it = m_edits.find(id); it != m_edits.end())
                    return &it->second;

                const AssetData<T>* live = m_assets.GetData(id);
                if (!live)
                    return nullptr;

                const auto saved = m_assets.Read(id);

                return &m_edits.emplace(id, Edit{ .Working = *live, .Saved = saved.value_or(*live) }).first->second;
            }

            bool SaveEdit(TypedAssetID<T> id)
            {
                const auto it = m_edits.find(id);
                if (it == m_edits.end())
                    return true;

                if (!m_assets.Save(id, it->second.Working))
                    return false;

                it->second.Saved = it->second.Working;
                return true;
            }

            // Undo and redo go through Apply like an edit, and aren't recorded again. An asset that is gone is skipped.
            void Restore(TypedAssetID<T> id, const AssetData<T>& data)
            {
                if (const auto it = m_edits.find(id); it != m_edits.end())
                {
                    it->second.Working = data;
                    it->second.Pending = true;
                }
            }

            AssetManager& m_assets;
            std::unordered_map<TypedAssetID<T>, Edit> m_edits;
            std::unordered_map<TypedAssetID<T>, AssetData<T>> m_before; // as it was when the step's first Set came
        };

        template <typename T>
        Table<T>& TableFor()
        {
            std::unique_ptr<TableBase>& table = m_tables[static_cast<size_t>(T::Tag)];
            if (!table)
                table = std::make_unique<Table<T>>(m_assets);

            return static_cast<Table<T>&>(*table);
        }

        // The table of the asset's type, null if there are no edits of that type
        [[nodiscard]] TableBase* TableOf(AssetID id) const;

        AssetManager& m_assets;
        std::array<std::unique_ptr<TableBase>, kAssetTypeCount> m_tables;
    };
}
