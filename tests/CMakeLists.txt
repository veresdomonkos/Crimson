add_executable(CrimsonTests
        support/fake_gpu.hpp
        support/temp_folder.hpp

        engine/test_asset_manager.cpp
        engine/test_asset_editing.cpp
        engine/test_frame.cpp
        engine/test_handle_registry.cpp
        engine/test_log.cpp
        engine/test_mesh_tangents.cpp
        engine/test_project.cpp
        engine/test_scene.cpp
        engine/test_scene_data.cpp
        engine/test_component_registry.cpp
        engine/test_scene_renderer.cpp
        engine/test_shaders.cpp
        engine/test_shadow_atlas.cpp

        editor/test_linear_color.cpp
        editor/test_recent_projects.cpp
        editor/test_asset_edits.cpp
        editor/test_asset_thumbnails.cpp
        editor/test_asset_browser.cpp
        editor/test_asset_picker.cpp
        editor/test_asset_inspector_prune.cpp
        editor/test_component_fields.cpp
        editor/test_system_shell.cpp
        editor/test_script_editor.cpp
        editor/test_editor_theme.cpp
)

# The engine's src folder is for the parts of the backends that run without a GPU, such as GLSL translation
target_include_directories(CrimsonTests PRIVATE . ${CMAKE_SOURCE_DIR}/engine/src)

# For the tests that read the sample projects
target_compile_definitions(CrimsonTests PRIVATE CRIMSON_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(CrimsonTests
        PRIVATE
            Engine
            EditorCore
            Catch2::Catch2WithMain
)

crimson_link_statically(CrimsonTests)

add_test(NAME CrimsonTests COMMAND CrimsonTests)

# The real backends. They need Vulkan 1.3 and OpenGL 4.5, from a GPU or a software renderer, and they open a window.
add_executable(CrimsonGpuTests
        gpu/main.cpp
        gpu/gpu_test.hpp
        gpu/gpu_test.cpp
        gpu/test_rendering.cpp
        gpu/test_lighting.cpp
        gpu/test_asset_editing.cpp
        gpu/test_editor.cpp
        gpu/test_input.cpp
)

target_include_directories(CrimsonGpuTests PRIVATE .)

target_link_libraries(CrimsonGpuTests
        PRIVATE
            Engine
            EditorCore
            Catch2::Catch2
)

crimson_link_statically(CrimsonGpuTests)

foreach(backend gl vk)
    add_test(NAME CrimsonGpuTests.${backend} COMMAND CrimsonGpuTests --backend ${backend})
    set_tests_properties(CrimsonGpuTests.${backend} PROPERTIES LABELS gpu TIMEOUT 600)
endforeach()
