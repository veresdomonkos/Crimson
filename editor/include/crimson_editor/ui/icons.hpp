#pragma once
#include <string_view>

#include <crimson/asset_manager/asset_types.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/mesh_asset.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

// Icons from the Lucide font (editor/resources/fonts/lucide.ttf, version 1.61.0), merged into the editor's fonts, so they
// can go straight into a label: ImGui::Button(icons::kMove). Only the icons the editor uses are here. To add one, find its
// codepoint in the codepoints.json of the same Lucide release, and write it as UTF-8.

namespace crimson::editor::ui::icons
{
    inline constexpr const char* kFolder = "\xEE\x83\x97"; // folder, U+E0D7
    inline constexpr const char* kFolderOpen = "\xEE\x89\x87"; // folder-open, U+E247
    inline constexpr const char* kMesh = "\xEE\x81\xA1"; // box, U+E061
    inline constexpr const char* kMaterial = "\xEE\x87\x9D"; // palette, U+E1DD
    inline constexpr const char* kShader = "\xEE\x83\x83"; // file-code, U+E0C3
    inline constexpr const char* kTexture = "\xEE\x83\xB6"; // image, U+E0F6
    inline constexpr const char* kScene = "\xEE\x8A\x9B"; // clapperboard, U+E29B
    inline constexpr const char* kFile = "\xEE\x83\x80"; // file, U+E0C0
    inline constexpr const char* kEntity = "\xEE\x92\xB0"; // circle-dashed, U+E4B0
    inline constexpr const char* kDirectionalLight = "\xEE\x85\xB8"; // sun, U+E178
    inline constexpr const char* kPointLight = "\xEE\x87\x82"; // lightbulb, U+E1C2
    inline constexpr const char* kSpotLight = "\xEE\x94\xA3"; // cone, U+E523
    inline constexpr const char* kCamera = "\xEE\x86\xA5"; // video, U+E1A5
    inline constexpr const char* kSceneInstance = "\xEE\x84\xA9"; // package, U+E129
    inline constexpr const char* kMove = "\xEE\x84\xA1"; // move, U+E121
    inline constexpr const char* kRotate = "\xEE\x8B\xAA"; // rotate-3d, U+E2EA
    inline constexpr const char* kScale = "\xEE\x8B\xAC"; // scaling, U+E2EC
    inline constexpr const char* kLocalSpace = "\xEE\x8B\xBE"; // axis-3d, U+E2FE
    inline constexpr const char* kWorldSpace = "\xEE\x83\xA8"; // globe, U+E0E8
    inline constexpr const char* kTransform = "\xEE\x8B\xA5"; // move-3d, U+E2E5
    inline constexpr const char* kSearch = "\xEE\x85\x91"; // search, U+E151
    inline constexpr const char* kError = "\xEE\x81\xB7"; // circle-alert, U+E077
    inline constexpr const char* kWarning = "\xEE\x86\x93"; // triangle-alert, U+E193
    inline constexpr const char* kSuccess = "\xEE\x88\xA6"; // circle-check, U+E226
    inline constexpr const char* kInfo = "\xEE\x83\xB9"; // info, U+E0F9
    inline constexpr const char* kPicker = "\xEE\x8D\x85"; // circle-dot, U+E345

    inline const char* ForAsset(AssetType type)
    {
        if (type.Is<MeshAsset>())     return kMesh;
        if (type.Is<MaterialAsset>()) return kMaterial;
        if (type.Is<ShaderAsset>())   return kShader;
        if (type.Is<TextureAsset>())  return kTexture;
        if (type.Is<SceneAsset>())    return kScene;

        return kFile;
    }

    // An icon by its Lucide name, which is how a component registers its own. Null for a name that isn't here.
    inline const char* Named(std::string_view name)
    {
        struct Entry
        {
            std::string_view Name;
            const char* Icon;
        };

        static constexpr Entry kEntries[] = {
            { "folder", kFolder }, { "folder-open", kFolderOpen }, { "box", kMesh }, { "palette", kMaterial },
            { "file-code", kShader }, { "image", kTexture }, { "clapperboard", kScene }, { "file", kFile },
            { "circle-dashed", kEntity }, { "sun", kDirectionalLight }, { "lightbulb", kPointLight },
            { "cone", kSpotLight }, { "video", kCamera }, { "package", kSceneInstance }, { "move", kMove }, { "rotate-3d", kRotate },
            { "scaling", kScale }, { "axis-3d", kLocalSpace }, { "globe", kWorldSpace }, { "move-3d", kTransform },
            { "search", kSearch }, { "circle-alert", kError }, { "triangle-alert", kWarning },
            { "circle-check", kSuccess }, { "info", kInfo }, { "circle-dot", kPicker },
        };

        for (const Entry& entry : kEntries)
        {
            if (entry.Name == name)
                return entry.Icon;
        }

        return nullptr;
    }
}
