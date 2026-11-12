# Everything but main, so the tests can link and drive the editor
add_library(EditorCore STATIC
        include/crimson_editor/editor_application.hpp
        src/editor_application.cpp
        include/crimson_editor/app_mode.hpp
        include/crimson_editor/project_session_mode.hpp
        src/project_session_mode.cpp
        include/crimson_editor/project_manager_mode.hpp
        src/project_manager_mode.cpp
        include/crimson_editor/recent_projects.hpp
        src/recent_projects.cpp
        include/crimson_editor/editor_icons.hpp
        src/editor_icons.cpp
        include/crimson_editor/editor_paths.hpp
        src/editor_paths.cpp
        include/crimson_editor/file_dialog.hpp
        src/file_dialog.cpp
        include/crimson_editor/system_shell.hpp
        src/system_shell.cpp
        include/crimson_editor/editor_settings.hpp
        src/editor_settings.cpp
        include/crimson_editor/script_editor.hpp
        src/script_editor.cpp
        include/crimson_editor/scene_camera_controller.hpp
        src/scene_camera_controller.cpp
        include/crimson_editor/ui/panel.hpp
        include/crimson_editor/ui/viewport_panel.hpp
        src/ui/viewport_panel.cpp
        include/crimson_editor/ui/scene_viewport_panel.hpp
        src/ui/scene_viewport_panel.cpp
        include/crimson_editor/frame_stats.hpp
        include/crimson_editor/ui/stats_panel.hpp
        src/ui/stats_panel.cpp
        include/crimson_editor/ui/render_settings_panel.hpp
        src/ui/render_settings_panel.cpp
        include/crimson_editor/ui/linear_color.hpp
        src/ui/linear_color.cpp
        include/crimson_editor/ui/property_fields.hpp
        src/ui/property_fields.cpp
        include/crimson_editor/ui/component_fields.hpp
        src/ui/component_fields.cpp
        include/crimson_editor/ui/notifications.hpp
        src/ui/notifications.cpp
        include/crimson_editor/ui/preferences_panel.hpp
        src/ui/preferences_panel.cpp
        include/crimson_editor/ui/editor_ui.hpp
        src/ui/editor_ui.cpp
        include/crimson_editor/ui/editor_theme.hpp
        src/ui/editor_theme.cpp
        include/crimson_editor/editor_resources.hpp
        include/crimson_editor/editor_context.hpp
        include/crimson_editor/ui/console_panel.hpp
        include/crimson_editor/ui/hierarchy_panel.hpp
        include/crimson_editor/ui/inspector_panel.hpp
        include/crimson_editor/ui/asset_browser_panel.hpp
        src/ui/asset_browser_panel.cpp
        include/crimson_editor/ui/asset_inspector.hpp
        src/ui/asset_inspector.cpp
        include/crimson_editor/ui/asset_drag.hpp
        include/crimson_editor/asset_edits.hpp
        src/asset_edits.cpp
        include/crimson_editor/asset_thumbnails.hpp
        src/asset_thumbnails.cpp
        src/ui/console_panel.cpp
        src/ui/hierarchy_panel.cpp
        src/ui/inspector_panel.cpp
)

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

add_executable(Editor src/main.cpp)

target_link_libraries(Editor PRIVATE EditorCore)

crimson_link_statically(Editor)
