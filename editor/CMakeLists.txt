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
