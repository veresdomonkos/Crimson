#pragma once
#include <unordered_map>

#include <crimson/asset_manager/asset_manager.hpp>

namespace crimson::editor
{
    // Unsaved changes to materials. An edit shows in the scene right away, and only reaches the file on Save.
    //
    // Everything here can be called from a panel, except Apply, which changes GPU resources and has to run between frames.
    class AssetEdits
    {
    public:
        explicit AssetEdits(AssetManager& assets) : m_assets(assets) {}

        // The material as it is being edited. Null if it can't be read.
        [[nodiscard]] const MaterialData* GetMaterial(MaterialID id);
        void SetMaterial(MaterialID id, const MaterialData& data);

        [[nodiscard]] bool IsDirty(MaterialID id) const;
        [[nodiscard]] bool HasUnsavedChanges() const;

        bool Save(MaterialID id);
        bool SaveAll();
        void Revert(MaterialID id);
        void DiscardAll();
        void Forget(AssetID id); // any asset, as it is called for whatever was removed

        // Shows the edits made since the last call. Returns true if a material got a new handle, which happens when
        // its shader changes, so whatever holds the old one has to get it again. An edit that can't be shown is undone.
        bool Apply();
    private:
        struct Edit
        {
            MaterialData Working;
            MaterialData Saved;
            bool Pending = false;
        };

        Edit* GetOrCreateEdit(MaterialID id);

        AssetManager& m_assets;
        std::unordered_map<MaterialID, Edit> m_edits;
    };
}
