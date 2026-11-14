cmake_minimum_required(VERSION 3.22)
project(Crimson LANGUAGES CXX C)

# Turns on the git hooks that check the conventions in CONTRIBUTING.md, unless the checkout already has other hooks set
find_package(Git QUIET)
if (PROJECT_IS_TOP_LEVEL AND GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" config core.hooksPath
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE CRIMSON_HOOKS_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if (CRIMSON_HOOKS_PATH STREQUAL "")
        execute_process(COMMAND "${GIT_EXECUTABLE}" config core.hooksPath .githooks WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        message(STATUS "Git hooks: turned on the ones in .githooks")
    endif()
endif()

# Caches compiler output with ccache when it is installed, so files that were built before come from the cache
option(CRIMSON_USE_CCACHE "Use ccache when it is installed" ON)
if (PROJECT_IS_TOP_LEVEL AND CRIMSON_USE_CCACHE AND NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CRIMSON_CCACHE ccache)
    if (CRIMSON_CCACHE)
        # Precompiled headers only come from the cache with this sloppiness
        set(CMAKE_C_COMPILER_LAUNCHER "${CRIMSON_CCACHE}" sloppiness=pch_defines,time_macros)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CRIMSON_CCACHE}" sloppiness=pch_defines,time_macros)
        message(STATUS "ccache: ${CRIMSON_CCACHE}")
    endif()
endif()

# Links with lld when the compiler can use it, which is several times faster than GNU ld on the large Debug builds
option(CRIMSON_USE_LLD "Link with lld when it is installed" ON)
if (PROJECT_IS_TOP_LEVEL AND CRIMSON_USE_LLD)
    include(CheckLinkerFlag)
    check_linker_flag(CXX -fuse-ld=lld CRIMSON_HAS_LLD)
    if (CRIMSON_HAS_LLD)
        add_link_options(-fuse-ld=lld)
        message(STATUS "Linker: lld")
    endif()
endif()

# Adds a library from external/ with only line tables as debug info. Nobody steps through them, and full debug info
# makes their archives and every executable they link into hundreds of megabytes bigger.
function(crimson_add_external dir)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        foreach (lang C CXX)
            foreach (config DEBUG RELWITHDEBINFO)
                set(CMAKE_${lang}_FLAGS_${config} "${CMAKE_${lang}_FLAGS_${config}} -g1")
            endforeach()
        endforeach()
    endif()
    add_subdirectory(${dir} ${ARGN})
endfunction()

# GLFW
crimson_add_external(external/glfw)

# imgui
crimson_add_external(external/imgui)

# glad
crimson_add_external(external/glad)

#glm
set(GLM_BUILD_LIBRARY OFF CACHE INTERNAL "")
set(GLM_BUILD_TESTS OFF CACHE INTERNAL "")
crimson_add_external(external/glm)

#fmt
crimson_add_external(external/fmt)

set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "Build static library" FORCE)
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
crimson_add_external(external/spirv-reflect)

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
crimson_add_external(external/SPIRV-Cross)

#glslang, the HLSL compiler built into the engine
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
crimson_add_external(external/glslang)

#imguizmo, without the rest of its project's widgets
add_library(imguizmo STATIC external/ImGuizmo/src/ImGuizmo.cpp)
target_include_directories(imguizmo PUBLIC external/ImGuizmo/src)
target_link_libraries(imguizmo PUBLIC imgui)

#entt
crimson_add_external(external/entt)

#json
crimson_add_external(external/json)

#stb-image
crimson_add_external(external/stb_image)

crimson_add_external(external/tinyobjloader)

#mikktspace, the tangents normal maps are baked against
add_library(mikktspace STATIC external/MikkTSpace/mikktspace.c)
target_include_directories(mikktspace PUBLIC external/MikkTSpace)

#tracy
option(CRIMSON_ENABLE_PROFILER "Enable Tracy profiling instrumentation" ON)
set(TRACY_ENABLE ${CRIMSON_ENABLE_PROFILER} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
crimson_add_external(external/tracy)

#vulkan memory allocator
crimson_add_external(external/VulkanMemoryAllocator)

#native file dialogs
crimson_add_external(external/nativefiledialog-extended)

include(cmake/EmbedResources.cmake)
include(cmake/LinkStatically.cmake)

add_subdirectory(engine)
add_subdirectory(editor)

option(CRIMSON_BUILD_TESTS "Build the tests" ON)
if(CRIMSON_BUILD_TESTS)
    enable_testing()
    crimson_add_external(external/Catch2)
    add_subdirectory(tests)
endif()
