cmake_minimum_required(VERSION 3.22)
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

set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "Build static library" FORCE)
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/spirv-reflect)

#spirv-cross
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/SPIRV-Cross)

#glslang, the HLSL compiler built into the engine
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glslang)

#entt
add_subdirectory(external/entt)

#json
add_subdirectory(external/json)

#stb-image
add_subdirectory(external/stb_image)

add_subdirectory(external/tinyobjloader)

#tracy
option(CRIMSON_ENABLE_PROFILER "Enable Tracy profiling instrumentation" ON)
set(TRACY_ENABLE ${CRIMSON_ENABLE_PROFILER} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
add_subdirectory(external/tracy)

#vulkan memory allocator
add_subdirectory(external/VulkanMemoryAllocator)

add_subdirectory(engine)
add_subdirectory(editor)