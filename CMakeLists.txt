cmake_minimum_required(VERSION 3.10)
project(Crimson)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# GLFW
add_subdirectory(external/glfw)

set_target_properties(uninstall PROPERTIES FOLDER "External/GLFW3")
set_target_properties(glfw PROPERTIES FOLDER "External/GLFW3")

if(TARGET update_mappings)
    set_target_properties(update_mappings PROPERTIES FOLDER "External/GLFW3")
endif()

# imgui
add_subdirectory(external/imgui)
set_target_properties(imgui PROPERTIES FOLDER "External")

# glad
add_subdirectory(external/glad)
set_target_properties(glad PROPERTIES FOLDER "External")

add_subdirectory(engine)
add_subdirectory(editor)