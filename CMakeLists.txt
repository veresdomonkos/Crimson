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

#fmt
add_subdirectory(external/fmt)

#tiny-process-library
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
add_subdirectory(external/tiny-process-library)

set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "Build static library" FORCE)
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/spirv-reflect)

#json
add_subdirectory(external/json)

#stb-image
add_subdirectory(external/stb_image)

add_subdirectory(external/tinyobjloader)

add_subdirectory(engine)
add_subdirectory(editor)