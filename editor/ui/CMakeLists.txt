# The editor's UI layer: the theme, the widgets and the ImGui glue. Only this library knows ImGui.
set(EDITOR_UI_HEADERS
    include/crimson_editor/ui/commands.hpp
    include/crimson_editor/ui/editor_theme.hpp
    include/crimson_editor/ui/icons.hpp
    include/crimson_editor/ui/imgui_platform.hpp
    include/crimson_editor/ui/imgui_renderer.hpp
    include/crimson_editor/ui/linear_color.hpp
    include/crimson_editor/ui/panel.hpp
    include/crimson_editor/ui/property_fields.hpp
    include/crimson_editor/ui/workspace.hpp

    src/command_menu.hpp
    src/imgui_convert.hpp
    src/ui_resources.hpp
)

set(EDITOR_UI_SOURCES
    src/command_menu.cpp
    src/commands.cpp
    src/editor_theme.cpp
    src/imgui_convert.cpp
    src/imgui_platform.cpp
    src/imgui_renderer.cpp
    src/linear_color.cpp
    src/panel.cpp
    src/property_fields.cpp
    src/workspace.cpp
)

add_library(EditorUI STATIC ${EDITOR_UI_HEADERS} ${EDITOR_UI_SOURCES})

target_include_directories(EditorUI PUBLIC include PRIVATE src)

crimson_embed_resources(EditorUI DIRECTORY resources NAMESPACE crimson::editor::ui FUNCTION GetUiResources)

target_link_libraries(EditorUI
    PUBLIC
        Engine
        imgui
        imguizmo
)

crimson_precompile_headers(EditorUI ${CRIMSON_PRECOMPILED_ENGINE_DEPS} <imgui.h>)
