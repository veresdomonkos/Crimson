#include "crimson_editor/asset_edits.hpp"

#include <algorithm>

namespace crimson::editor
{
    AssetEdits::Edit* AssetEdits::GetOrCreateEdit(MaterialID id)
    {
        if (const auto it = m_edits.find(id); it != m_edits.end())
            return &it->second;

        const MaterialData* live = m_assets.GetMaterialData(id);
        if (!live)
            return nullptr;

        const auto saved = m_assets.ReadMaterialFile(id);

        return &m_edits.emplace(id, Edit{ .Working = *live, .Saved = saved.value_or(*live) }).first->second;
    }

    const MaterialData* AssetEdits::GetMaterial(MaterialID id)
    {
        const Edit* edit = GetOrCreateEdit(id);
        return edit ? &edit->Working : nullptr;
    }

    void AssetEdits::SetMaterial(MaterialID id, const MaterialData& data)
    {
        const AssetInfo* info = m_assets.GetInfo(id);
        if (!info || info->ReadOnly)
            return;

        if (Edit* edit = GetOrCreateEdit(id))
        {
            edit->Working = data;
            edit->Pending = true;
        }
    }

    bool AssetEdits::IsDirty(MaterialID id) const
    {
        const auto it = m_edits.find(id);
        return it != m_edits.end() && it->second.Working != it->second.Saved;
    }

    bool AssetEdits::HasUnsavedChanges() const
    {
        return std::ranges::any_of(m_edits, [](const auto& entry) { return entry.second.Working != entry.second.Saved; });
    }

    bool AssetEdits::Save(MaterialID id)
    {
        const auto it = m_edits.find(id);
        if (it == m_edits.end())
            return true;

        if (!m_assets.SaveMaterial(id, it->second.Working))
            return false;

        it->second.Saved = it->second.Working;
        return true;
    }

    bool AssetEdits::SaveAll()
    {
        bool saved = true;

        for (auto& [id, edit] : m_edits)
        {
            if (edit.Working != edit.Saved)
                saved = Save(id) && saved;
        }

        return saved;
    }

    void AssetEdits::Revert(MaterialID id)
    {
        if (const auto it = m_edits.find(id); it != m_edits.end() && it->second.Working != it->second.Saved)
        {
            it->second.Working = it->second.Saved;
            it->second.Pending = true;
        }
    }

    void AssetEdits::DiscardAll()
    {
        for (auto& [id, edit] : m_edits)
            Revert(id);
    }

    void AssetEdits::Forget(AssetID id)
    {
        std::erase_if(m_edits, [id](const auto& entry) { return AssetID(entry.first) == id; });
    }

    bool AssetEdits::Apply()
    {
        bool replaced = false;

        for (auto& [id, edit] : m_edits)
        {
            if (!edit.Pending)
                continue;

            edit.Pending = false;

            switch (m_assets.ApplyMaterial(id, edit.Working))
            {
                case MaterialApplyResult::Replaced:
                    replaced = true;
                    break;

                case MaterialApplyResult::Updated:
                    break;

                case MaterialApplyResult::Failed:
                    if (const MaterialData* live = m_assets.GetMaterialData(id))
                        edit.Working = *live;
                    break;
            }
        }

        return replaced;
    }
}
