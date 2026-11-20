#include "crimson_editor/asset_edits.hpp"

namespace crimson::editor
{
    AssetEdits::TableBase* AssetEdits::TableOf(AssetID id) const
    {
        const AssetInfo* info = m_assets.GetInfo(id);
        if (!info)
            return nullptr;

        const auto table = m_tables.find(info->Type);
        return table != m_tables.end() ? table->second.get() : nullptr;
    }

    bool AssetEdits::IsDirty(AssetID id) const
    {
        const TableBase* table = TableOf(id);
        return table && table->IsDirty(id);
    }

    bool AssetEdits::HasUnsavedChanges() const
    {
        return std::ranges::any_of(m_tables, [](const auto& table) { return table.second->HasUnsavedChanges(); });
    }

    bool AssetEdits::Save(AssetID id)
    {
        TableBase* table = TableOf(id);
        return !table || table->Save(id);
    }

    bool AssetEdits::SaveAll()
    {
        bool saved = true;

        for (const auto& [type, table] : m_tables)
            saved = table->SaveAll() && saved;

        return saved;
    }

    void AssetEdits::DiscardAll()
    {
        for (const auto& [type, table] : m_tables)
            table->DiscardAll();
    }

    void AssetEdits::Forget(AssetID id)
    {
        for (const auto& [type, table] : m_tables)
            table->Forget(id);
    }

    void AssetEdits::Apply()
    {
        for (const auto& [type, table] : m_tables)
            table->Apply();
    }

    void AssetEdits::Collect(std::vector<std::unique_ptr<EditChange>>& changes)
    {
        for (const auto& [type, table] : m_tables)
            table->Collect(changes);
    }

    void AssetEdits::Discard()
    {
        for (const auto& [type, table] : m_tables)
            table->Discard();
    }
}
