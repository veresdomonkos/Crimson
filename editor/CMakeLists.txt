add_executable(Editor
        include/crimson_editor/editor_application.hpp

        src/main.cpp
        src/editor_application.cpp
        include/crimson_editor/ui/Panel.hpp
        include/crimson_editor/ui/viewport_panel.hpp
        src/ui/viewport_panel.cpp
        include/crimson_editor/frame_stats.hpp
        include/crimson_editor/ui/stats_panel.hpp
        src/ui/stats_panel.cpp
        include/crimson_editor/ui/editor_ui.hpp
        src/ui/editor_ui.cpp
        include/crimson_editor/editor_resources.hpp
        include/crimson_editor/ui/console_panel.hpp
        include/crimson_editor/ui/hierarchy_panel.hpp
        include/crimson_editor/ui/inspector_panel.hpp
        include/crimson_editor/ui/asset_browser_panel.hpp
        src/ui/console_panel.cpp
)

target_include_directories(Editor PUBLIC include)

target_link_libraries(Editor
        PRIVATE
            Engine
)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
endif()

# A custom target runs on every build, so editing only a shader still reaches the output directory
add_custom_target(EditorAssets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Editor>/assets"
        COMMENT "Copying assets to output directory"
)
add_dependencies(Editor EditorAssets)