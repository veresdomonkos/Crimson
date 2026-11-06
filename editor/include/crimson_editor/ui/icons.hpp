#pragma once
#include <crimson/asset_manager/asset_types.hpp>

// Icons from the Lucide font (editor/resources/fonts/lucide.ttf, version 1.61.0), merged into the editor's fonts, so they
// can go straight into a label: ImGui::Button(kIconMove). Only the icons the editor uses are here. To add one, find its
// codepoint in the codepoints.json of the same Lucide release, and write it as UTF-8.

namespace crimson::editor::ui
{
    inline constexpr const char* kIconFolder = "\xEE\x83\x97"; // folder, U+E0D7
    inline constexpr const char* kIconFolderOpen = "\xEE\x89\x87"; // folder-open, U+E247
    inline constexpr const char* kIconMesh = "\xEE\x81\xA1"; // box, U+E061
    inline constexpr const char* kIconMaterial = "\xEE\x87\x9D"; // palette, U+E1DD
    inline constexpr const char* kIconShader = "\xEE\x83\x83"; // file-code, U+E0C3
    inline constexpr const char* kIconTexture = "\xEE\x83\xB6"; // image, U+E0F6
    inline constexpr const char* kIconScene = "\xEE\x8A\x9B"; // clapperboard, U+E29B
    inline constexpr const char* kIconFile = "\xEE\x83\x80"; // file, U+E0C0
    inline constexpr const char* kIconEntity = "\xEE\x92\xB0"; // circle-dashed, U+E4B0
    inline constexpr const char* kIconDirectionalLight = "\xEE\x85\xB8"; // sun, U+E178
    inline constexpr const char* kIconPointLight = "\xEE\x87\x82"; // lightbulb, U+E1C2
    inline constexpr const char* kIconSpotLight = "\xEE\x94\xA3"; // cone, U+E523
    inline constexpr const char* kIconSceneInstance = "\xEE\x84\xA9"; // package, U+E129
    inline constexpr const char* kIconMove = "\xEE\x84\xA1"; // move, U+E121
    inline constexpr const char* kIconRotate = "\xEE\x8B\xAA"; // rotate-3d, U+E2EA
    inline constexpr const char* kIconScale = "\xEE\x8B\xAC"; // scaling, U+E2EC
    inline constexpr const char* kIconLocalSpace = "\xEE\x8B\xBE"; // axis-3d, U+E2FE
    inline constexpr const char* kIconWorldSpace = "\xEE\x83\xA8"; // globe, U+E0E8
    inline constexpr const char* kIconTransform = "\xEE\x8B\xA5"; // move-3d, U+E2E5
    inline constexpr const char* kIconSearch = "\xEE\x85\x91"; // search, U+E151
    inline constexpr const char* kIconError = "\xEE\x81\xB7"; // circle-alert, U+E077
    inline constexpr const char* kIconWarning = "\xEE\x86\x93"; // triangle-alert, U+E193
    inline constexpr const char* kIconSuccess = "\xEE\x88\xA6"; // circle-check, U+E226
    inline constexpr const char* kIconInfo = "\xEE\x83\xB9"; // info, U+E0F9

    inline const char* AssetIcon(AssetType type)
    {
        switch (type)
        {
            case AssetType::Mesh:     return kIconMesh;
            case AssetType::Material: return kIconMaterial;
            case AssetType::Shader:   return kIconShader;
            case AssetType::Texture:  return kIconTexture;
            case AssetType::Scene:    return kIconScene;
            default:                  return kIconFile;
        }
    }
}
