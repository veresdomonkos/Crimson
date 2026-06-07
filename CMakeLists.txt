cmake_minimum_required(VERSION 3.10)
project(Crimson)

add_subdirectory(engine)
add_subdirectory(editor)

add_subdirectory(external/glfw)
add_subdirectory(external/imgui)