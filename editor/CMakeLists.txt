add_executable(Editor
        include/editor/editor_application.hpp

        src/main.cpp
        src/editor_application.cpp
        include/editor/utils.hpp
        include/editor/ui/Panel.hpp
        include/editor/ui/viewport_panel.hpp
        src/ui/viewport_panel.cpp
        include/editor/ui/performance_panel.hpp
        include/editor/frame_stats.hpp
        src/ui/performance_panel.cpp
        include/editor/ui/editor_ui.hpp
        src/ui/editor_ui.cpp
        include/editor/editor_resources.hpp
        include/editor/ui/console_panel.hpp
        include/editor/ui/hierarchy_panel.hpp
        include/editor/ui/inspector_panel.hpp
        include/editor/ui/asset_browser_panel.hpp
        src/ui/console_panel.cpp
)

target_include_directories(Editor PUBLIC include)

target_link_libraries(Editor
        PRIVATE
            Engine
            tiny-process-library
)

file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*")

add_custom_command(
        TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Editor>/assets"
        COMMENT "Copying assets to output directory"
)