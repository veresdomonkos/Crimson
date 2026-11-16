#include "crimson_editor/asset_edits.hpp"

namespace crimson::editor
{
    AssetEdits::TableBase* AssetEdits::TableOf(AssetID id) const
    {
        const AssetInfo* info = m_assets.GetInfo(id);
        return info ? m_tables[static_cast<size_t>(info->Type)].get() : nullptr;
    }

    bool AssetEdits::IsDirty(AssetID id) const
    {
        const TableBase* table = TableOf(id);
        return table && table->IsDirty(id);
    }

    bool AssetEdits::HasUnsavedChanges() const
    {
        return std::ranges::any_of(m_tables, [](const auto& table) { return table && table->HasUnsavedChanges(); });
    }

    bool AssetEdits::Save(AssetID id)
    {
        TableBase* table = TableOf(id);
        return !table || table->Save(id);
    }

    bool AssetEdits::SaveAll()
    {
        bool saved = true;

        for (const auto& table : m_tables)
        {
            if (table)
                saved = table->SaveAll() && saved;
        }

        return saved;
    }

    void AssetEdits::DiscardAll()
    {
        for (const auto& table : m_tables)
        {
            if (table)
                table->DiscardAll();
        }
    }

    void AssetEdits::Forget(AssetID id)
    {
        for (const auto& table : m_tables)
        {
            if (table)
                table->Forget(id);
        }
    }

    bool AssetEdits::Apply()
    {
        bool replaced = false;

        for (const auto& table : m_tables)
        {
            if (table)
                replaced = table->Apply() || replaced;
        }

        return replaced;
    }

    void AssetEdits::Collect(std::vector<std::unique_ptr<EditChange>>& changes)
    {
        for (const auto& table : m_tables)
        {
            if (table)
                table->Collect(changes);
        }
    }

    void AssetEdits::Discard()
    {
        for (const auto& table : m_tables)
        {
            if (table)
                table->Discard();
        }
    }
}
