#pragma once

namespace crimson::editor::ui
{
    class Panel
    {
    public:
        virtual ~Panel() = default;
        virtual void OnImGui() = 0;

        // Adds the panel's own menus to the main menu bar, between File and View. Called before OnImGui.
        virtual void OnMainMenuBar() {}
    };
}
