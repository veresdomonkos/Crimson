add_subdirectory(ui)

set(EDITOR_HEADERS
    include/crimson_editor/app_mode.hpp
    include/crimson_editor/asset_edits.hpp
    include/crimson_editor/asset_thumbnails.hpp
    include/crimson_editor/edit_history.hpp
    include/crimson_editor/editor_application.hpp
    include/crimson_editor/editor_context.hpp
    include/crimson_editor/editor_icons.hpp
    include/crimson_editor/editor_paths.hpp
    include/crimson_editor/editor_resources.hpp
    include/crimson_editor/editor_settings.hpp
    include/crimson_editor/file_dialog.hpp
    include/crimson_editor/frame_stats.hpp
    include/crimson_editor/platform.hpp
    include/crimson_editor/project_manager_mode.hpp
    include/crimson_editor/project_session.hpp
    include/crimson_editor/project_session_mode.hpp
    include/crimson_editor/recent_projects.hpp
    include/crimson_editor/recovery_files.hpp
    include/crimson_editor/scene_edits.hpp
    include/crimson_editor/scene_view.hpp
    include/crimson_editor/scene_camera_controller.hpp
    include/crimson_editor/script_editor.hpp
    include/crimson_editor/system_shell.hpp

    include/crimson_editor/panels/asset_look.hpp

    include/crimson_editor/panels/asset_browser_panel.hpp
    include/crimson_editor/panels/asset_drag.hpp
    include/crimson_editor/panels/asset_field.hpp
    include/crimson_editor/panels/asset_inspector.hpp
    include/crimson_editor/panels/asset_picker.hpp
    include/crimson_editor/panels/asset_tiles.hpp
    include/crimson_editor/panels/component_fields.hpp
    include/crimson_editor/panels/console_panel.hpp
    include/crimson_editor/panels/hierarchy_panel.hpp
    include/crimson_editor/panels/inspector_panel.hpp
    include/crimson_editor/panels/notifications.hpp
    include/crimson_editor/panels/preferences_panel.hpp
    include/crimson_editor/panels/scene_settings_panel.hpp
    include/crimson_editor/panels/scene_viewport_panel.hpp
    include/crimson_editor/panels/stats_panel.hpp
    include/crimson_editor/panels/viewport_panel.hpp
)

set(EDITOR_SOURCES
    src/asset_edits.cpp
    src/asset_thumbnails.cpp
    src/edit_history.cpp
    src/editor_application.cpp
    src/editor_icons.cpp
    src/editor_paths.cpp
    src/editor_settings.cpp
    src/file_dialog.cpp
    src/project_manager_mode.cpp
    src/project_session.cpp
    src/project_session_mode.cpp
    src/recent_projects.cpp
    src/recovery_files.cpp
    src/scene_edits.cpp
    src/scene_view.cpp
    src/scene_camera_controller.cpp
    src/script_editor.cpp
    src/system_shell.cpp

    src/panels/asset_look.cpp

    src/panels/asset_browser_panel.cpp
    src/panels/asset_field.cpp
    src/panels/asset_inspector.cpp
    src/panels/asset_picker.cpp
    src/panels/asset_tiles.cpp
    src/panels/component_fields.cpp
    src/panels/console_panel.cpp
    src/panels/hierarchy_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/notifications.cpp
    src/panels/preferences_panel.cpp
    src/panels/scene_settings_panel.cpp
    src/panels/scene_viewport_panel.cpp
    src/panels/stats_panel.cpp
    src/panels/viewport_panel.cpp
)

# Everything but main, so the tests can link and drive the editor
add_library(EditorCore STATIC ${EDITOR_HEADERS} ${EDITOR_SOURCES})

target_include_directories(EditorCore PUBLIC include)

crimson_embed_resources(EditorCore DIRECTORY resources NAMESPACE crimson::editor FUNCTION GetEditorResources)

target_link_libraries(EditorCore
    PUBLIC
        Engine
        EditorUI
    PRIVATE
        nfd
)

# What differs between the operating systems, one file for each (platform.hpp, system_shell.hpp)
if(WIN32)
    target_sources(EditorCore PRIVATE
        src/platform/windows/windows_platform.cpp
        src/platform/windows/windows_system_shell.cpp
    )
    # The command line, and opening files in other programs and showing them in Explorer
    target_link_libraries(EditorCore PRIVATE shell32 ole32)
else()
    target_sources(EditorCore PRIVATE
        src/platform/posix/posix_platform.cpp
        src/platform/posix/posix_system_shell.cpp
    )
endif()

crimson_precompile_headers(EditorCore ${CRIMSON_PRECOMPILED_ENGINE_DEPS})

add_executable(Editor src/main.cpp)

target_link_libraries(Editor PRIVATE EditorCore)

crimson_link_statically(Editor)
