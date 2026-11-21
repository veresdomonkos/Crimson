# The editor's UI layer: the theme, the widgets, the panel framework and the ImGui glue. Only this library knows ImGui.
set(EDITOR_UI_HEADERS
    include/crimson_editor/ui/commands.hpp
    include/crimson_editor/ui/drag_drop.hpp
    include/crimson_editor/ui/editor_theme.hpp
    include/crimson_editor/ui/gizmo.hpp
    include/crimson_editor/ui/host.hpp
    include/crimson_editor/ui/icons.hpp
    include/crimson_editor/ui/input.hpp
    include/crimson_editor/ui/layout.hpp
    include/crimson_editor/ui/linear_color.hpp
    include/crimson_editor/ui/menus.hpp
    include/crimson_editor/ui/painter.hpp
    include/crimson_editor/ui/panel.hpp
    include/crimson_editor/ui/property_fields.hpp
    include/crimson_editor/ui/types.hpp
    include/crimson_editor/ui/widgets.hpp
    include/crimson_editor/ui/window.hpp
    include/crimson_editor/ui/workspace.hpp

    src/command_menu.hpp
    src/imgui_convert.hpp
    src/imgui_platform.hpp
    src/imgui_renderer.hpp
    src/placement.hpp
    src/theme_style.hpp
    src/ui_resources.hpp
)

set(EDITOR_UI_SOURCES
    src/command_menu.cpp
    src/commands.cpp
    src/drag_drop.cpp
    src/editor_theme.cpp
    src/gizmo.cpp
    src/host.cpp
    src/imgui_convert.cpp
    src/imgui_platform.cpp
    src/imgui_renderer.cpp
    src/input.cpp
    src/layout.cpp
    src/linear_color.cpp
    src/menus.cpp
    src/painter.cpp
    src/panel.cpp
    src/property_fields.cpp
    src/widgets.cpp
    src/window.cpp
    src/workspace.cpp
)

add_library(EditorUI STATIC ${EDITOR_UI_HEADERS} ${EDITOR_UI_SOURCES})

target_include_directories(EditorUI PUBLIC include PRIVATE src)

crimson_embed_resources(EditorUI DIRECTORY resources NAMESPACE crimson::editor::ui FUNCTION GetUiResources)

# ImGui and ImGuizmo are private, so nothing outside this library can draw with them
target_link_libraries(EditorUI
    PUBLIC
        Engine
    PRIVATE
        imgui
        imguizmo
)

crimson_precompile_headers(EditorUI ${CRIMSON_PRECOMPILED_ENGINE_DEPS} <imgui.h>)
