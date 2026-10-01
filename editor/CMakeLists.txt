add_executable(Editor
        include/editor/editor_application.hpp

        src/main.cpp
        src/editor_application.cpp
        include/editor/utils.hpp
        include/editor/editor_ui.hpp
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