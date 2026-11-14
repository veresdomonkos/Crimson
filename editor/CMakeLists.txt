set(EDITOR_HEADERS
    include/crimson_editor/app_mode.hpp
    include/crimson_editor/asset_edits.hpp
    include/crimson_editor/asset_thumbnails.hpp
    include/crimson_editor/editor_application.hpp
    include/crimson_editor/editor_context.hpp
    include/crimson_editor/editor_icons.hpp
    include/crimson_editor/editor_paths.hpp
    include/crimson_editor/editor_resources.hpp
    include/crimson_editor/editor_settings.hpp
    include/crimson_editor/file_dialog.hpp
    include/crimson_editor/frame_stats.hpp
    include/crimson_editor/project_manager_mode.hpp
    include/crimson_editor/project_session_mode.hpp
    include/crimson_editor/recent_projects.hpp
    include/crimson_editor/scene_camera_controller.hpp
    include/crimson_editor/script_editor.hpp
    include/crimson_editor/system_shell.hpp

    include/crimson_editor/ui/asset_browser_panel.hpp
    include/crimson_editor/ui/asset_drag.hpp
    include/crimson_editor/ui/asset_field.hpp
    include/crimson_editor/ui/asset_inspector.hpp
    include/crimson_editor/ui/asset_picker.hpp
    include/crimson_editor/ui/asset_tiles.hpp
    include/crimson_editor/ui/component_fields.hpp
    include/crimson_editor/ui/console_panel.hpp
    include/crimson_editor/ui/editor_theme.hpp
    include/crimson_editor/ui/editor_ui.hpp
    include/crimson_editor/ui/hierarchy_panel.hpp
    include/crimson_editor/ui/icons.hpp
    include/crimson_editor/ui/inspector_panel.hpp
    include/crimson_editor/ui/linear_color.hpp
    include/crimson_editor/ui/notifications.hpp
    include/crimson_editor/ui/panel.hpp
    include/crimson_editor/ui/preferences_panel.hpp
    include/crimson_editor/ui/property_fields.hpp
    include/crimson_editor/ui/render_settings_panel.hpp
    include/crimson_editor/ui/scene_viewport_panel.hpp
    include/crimson_editor/ui/stats_panel.hpp
    include/crimson_editor/ui/viewport_panel.hpp
)

set(EDITOR_SOURCES
    src/asset_edits.cpp
    src/asset_thumbnails.cpp
    src/editor_application.cpp
    src/editor_icons.cpp
    src/editor_paths.cpp
    src/editor_settings.cpp
    src/file_dialog.cpp
    src/project_manager_mode.cpp
    src/project_session_mode.cpp
    src/recent_projects.cpp
    src/scene_camera_controller.cpp
    src/script_editor.cpp
    src/system_shell.cpp

    src/ui/asset_browser_panel.cpp
    src/ui/asset_field.cpp
    src/ui/asset_inspector.cpp
    src/ui/asset_picker.cpp
    src/ui/asset_tiles.cpp
    src/ui/component_fields.cpp
    src/ui/console_panel.cpp
    src/ui/editor_theme.cpp
    src/ui/editor_ui.cpp
    src/ui/hierarchy_panel.cpp
    src/ui/inspector_panel.cpp
    src/ui/linear_color.cpp
    src/ui/notifications.cpp
    src/ui/preferences_panel.cpp
    src/ui/property_fields.cpp
    src/ui/render_settings_panel.cpp
    src/ui/scene_viewport_panel.cpp
    src/ui/stats_panel.cpp
    src/ui/viewport_panel.cpp
)

# Everything but main, so the tests can link and drive the editor
add_library(EditorCore STATIC ${EDITOR_HEADERS} ${EDITOR_SOURCES})

target_include_directories(EditorCore PUBLIC include)

crimson_embed_resources(EditorCore DIRECTORY resources NAMESPACE crimson::editor FUNCTION GetEditorResources)

target_link_libraries(EditorCore
    PUBLIC
        Engine
        imguizmo
    PRIVATE
        nfd
)

# Opening files in other programs and showing them in Explorer (system_shell.cpp)
if(WIN32)
    target_link_libraries(EditorCore PRIVATE shell32 ole32)
endif()

crimson_precompile_headers(EditorCore ${CRIMSON_PRECOMPILED_ENGINE_DEPS} <imgui.h>)

add_executable(Editor src/main.cpp)

target_link_libraries(Editor PRIVATE EditorCore)

crimson_link_statically(Editor)
