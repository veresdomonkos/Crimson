#pragma once
#include <glm/vec2.hpp>
#include <imgui.h>

#include <crimson/core/keys.hpp>
#include <crimson/renderer/resource_handles.hpp>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    // What ImGui calls a key, or ImGuiKey_None for one it has no name for
    ImGuiKey ToImGuiKey(Key key);

    [[nodiscard]] inline ImVec2 ToImVec2(glm::vec2 value) { return ImVec2(value.x, value.y); }
    [[nodiscard]] inline glm::vec2 FromImVec2(ImVec2 value) { return glm::vec2(value.x, value.y); }
    [[nodiscard]] inline ImVec4 ToImVec4(Color color) { return ImVec4(color.R, color.G, color.B, color.A); }
    // With the style's alpha applied, as ImGui's own widgets have it, so what is drawn by hand fades and dims with them
    [[nodiscard]] inline ImU32 ToImU32(Color color) { return ImGui::GetColorU32(ToImVec4(color)); }

    [[nodiscard]] inline ImGuiMouseButton ToImGuiMouseButton(MouseButton button) { return static_cast<ImGuiMouseButton>(button); }

    // An ImGui texture id holds the engine's texture handle, so drawing an image takes no registering and a texture can be
    // destroyed without telling ImGui first. The invalid handle is ImGui's no texture, as both are 0.
    [[nodiscard]] inline ImTextureID ToImTextureID(TextureHandle texture) { return static_cast<ImTextureID>(texture.GetRaw()); }
    [[nodiscard]] inline TextureHandle FromImTextureID(ImTextureID id) { return TextureHandle::FromRaw(id); }
}
