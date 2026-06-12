cmake_minimum_required(VERSION 3.10)
project(Crimson LANGUAGES CXX C)

# GLFW
add_subdirectory(external/glfw)

# imgui
add_subdirectory(external/imgui)

# glad
add_subdirectory(external/glad)

#glm
set(GLM_BUILD_LIBRARY OFF CACHE INTERNAL "")
set(GLM_BUILD_TESTS OFF CACHE INTERNAL "")
add_subdirectory(external/glm)

add_subdirectory(engine)
add_subdirectory(editor)