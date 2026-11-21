#pragma once
#include <string_view>

#include <glm/vec2.hpp>

#include <crimson/renderer/resource_handles.hpp>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    enum class Corners
    {
        All,
        Right
    };

    // Draws shapes and text on the window being drawn, in screen space, over what the items drew before
    class Painter
    {
    public:
        Painter();

        void FillRect(const Rect& rect, Color color, float rounding = 0.0f, Corners corners = Corners::All);
        void StrokeRect(const Rect& rect, Color color, float rounding, float thickness);

        // A size of 0 is the current one, in pixels
        void Text(glm::vec2 position, Color color, std::string_view text, Font font = Font::Regular, float size = 0.0f);

        // A name centered in the width, shortened with "..." if it is wider. The tooltip has the whole name.
        void ClippedText(glm::vec2 position, float width, std::string_view text, Color color);

        // An icon as big as fits in the rect, centered by what it draws rather than by its place in the font, which leaves
        // room around icons that isn't the same on every side
        void Icon(const Rect& rect, std::string_view icon, Color color);

        void Image(TextureHandle texture, const Rect& rect);
    private:
        void* m_list;
    };

    // What is drawn by the painter while it lives is cut off at the rect
    class ClipScope
    {
    public:
        explicit ClipScope(const Rect& rect);
        ~ClipScope();

        ClipScope(const ClipScope&) = delete;
        ClipScope& operator=(const ClipScope&) = delete;
    };
}
