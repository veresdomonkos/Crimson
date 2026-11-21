#pragma once
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui::drag_drop
{
    enum class DropLook
    {
        Default,

        // The target draws no highlight, as it draws its own with Highlight
        None
    };

    namespace detail
    {
        void Source(std::string_view type, const void* data, std::size_t size, std::string_view label);

        // Copies what is dropped into out, on the frame it is dropped, if the function takes it. ImGui clears its copy as
        // the target ends, so nothing may point into it afterwards.
        bool Accept(std::string_view type, void* out, std::size_t size, DropLook look, const std::function<bool(const void*)>& accepts);

        // What is being dragged, if it is of the type
        const void* Peek(std::string_view type, std::size_t size);
    }

    // Makes the last item a thing to drag, which is shown as the label while dragged
    template <typename T>
    void Source(std::string_view type, const T& payload, std::string_view label)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        detail::Source(type, &payload, sizeof(T), label);
    }

    // Makes the last item accept what is dropped on it. Returns it on the frame it is dropped, if the predicate takes it.
    template <typename T, typename Accepts>
    [[nodiscard]] std::optional<T> Accept(std::string_view type, Accepts accepts, DropLook look = DropLook::Default)
    {
        static_assert(std::is_trivially_copyable_v<T>);

        T value;
        const bool dropped = detail::Accept(type, &value, sizeof(T), look, [&accepts](const void* payload)
        {
            T candidate;
            std::memcpy(&candidate, payload, sizeof(T));
            return accepts(candidate);
        });

        if (!dropped)
            return std::nullopt;

        return value;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> Accept(std::string_view type, DropLook look = DropLook::Default)
    {
        return Accept<T>(type, [](const T&) { return true; }, look);
    }

    // What is being dragged right now, if it is of the type, wherever the mouse is
    template <typename T>
    [[nodiscard]] std::optional<T> Dragged(std::string_view type)
    {
        static_assert(std::is_trivially_copyable_v<T>);

        const void* data = detail::Peek(type, sizeof(T));
        if (!data)
            return std::nullopt;

        T value;
        std::memcpy(&value, data, sizeof(T));
        return value;
    }

    // The highlight of a drop target with rounded corners, while the mouse is over it. ImGui's own is square, which suits
    // windows and rows, so a rounded target accepts with DropLook::None and draws this.
    void Highlight(const Rect& rect, float rounding);
}
