#pragma once

namespace crimson::editor::ui
{
    class Panel
    {
    public:
        virtual ~Panel() = default;
        virtual void OnImGui() = 0;
    };
}