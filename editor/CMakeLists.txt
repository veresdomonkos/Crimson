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

add_executable(Editor src/main.cpp)

target_link_libraries(Editor PRIVATE EditorCore)

crimson_link_statically(Editor)
