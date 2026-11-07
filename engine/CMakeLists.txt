find_package(Vulkan REQUIRED)

set(ENGINE_PUBLIC_HEADERS
		include/crimson/core/core.hpp
		include/crimson/core/window.hpp
		include/crimson/core/events.hpp
		include/crimson/core/embedded_resource.hpp
		include/crimson/core/engine_resources.hpp
        include/crimson/renderer/render_commands.hpp
        include/crimson/core/log.hpp
        include/crimson/core/input.hpp
        include/crimson/renderer/renderer_api.hpp
        include/crimson/renderer/resource_manager.hpp
        include/crimson/renderer/renderer.hpp
        include/crimson/renderer/frame_context.hpp
        include/crimson/renderer/draw_sort.hpp
        include/crimson/core/command_buffer.hpp
        include/crimson/renderer/handle_registry.hpp
        include/crimson/renderer/resource_handles.hpp
        include/crimson/renderer/core.hpp
        include/crimson/renderer/resource_manager_base.hpp
        include/crimson/renderer/buffer_layout.hpp
        include/crimson/renderer/buffers.hpp
        include/crimson/renderer/camera_data.hpp
        include/crimson/renderer/shader_property_info.hpp
        include/crimson/renderer/shader_interface.hpp
        include/crimson/renderer/shader_compiler.hpp
        include/crimson/renderer/shadow_atlas.hpp
        include/crimson/renderer/light.hpp
        include/crimson/renderer/frame_data.hpp
		include/crimson/graphics/graphincs_backend.hpp
		include/crimson/graphics/graphics_device.hpp
        include/crimson/renderer/resource_tags.hpp
        include/crimson/asset_manager/asset_id.hpp
        include/crimson/asset_manager/asset_types.hpp
        include/crimson/asset_manager/meta_file.hpp
        include/crimson/asset_manager/asset_manager.hpp
        include/crimson/asset_manager/material_data.hpp
        include/crimson/asset_manager/scene_data.hpp
        include/crimson/asset_manager/importers/texture_importer.hpp
        include/crimson/asset_manager/importers/material_importer.hpp
        include/crimson/asset_manager/importers/shader_importer.hpp
        include/crimson/asset_manager/importers/scene_importer.hpp
        include/crimson/renderer/mesh.hpp
        include/crimson/renderer/shader_pass.hpp
        include/crimson/asset_manager/importers/mesh_importer.hpp
        include/crimson/core/profiler.hpp
        include/crimson/renderer/render_stats.hpp
        include/crimson/renderer/material_settings.hpp
        include/crimson/scene/components.hpp
        include/crimson/scene/entity.hpp
        include/crimson/scene/scene.hpp
        include/crimson/scene/scene_renderer.hpp
        include/crimson/scene/scene_serializer.hpp
        include/crimson/scene/tone_mapper.hpp
        include/crimson/project/project.hpp
        include/crimson/project/shader_includes.hpp
)

set(ENGINE_IMPLEMENTATION
		src/platform/glfw/glfw_window.hpp
		src/platform/glfw/glfw_window.cpp
        src/core/log.cpp
        src/core/input.cpp
        src/platform/opengl/opengl_resource_manager.hpp
        src/platform/opengl/opengl_resource_manager.cpp
        src/platform/opengl/opengl_renderer.hpp
        src/platform/opengl/opengl_renderer_glfw.cpp
		src/platform/opengl/opengl_resources.hpp
        src/platform/vulkan/vulkan_resources.hpp
        src/platform/vulkan/vulkan_allocator.hpp
        src/platform/vulkan/vulkan_allocator.cpp
        src/platform/vulkan/vulkan_include.hpp
        src/platform/vulkan/vulkan_pipeline_cache.hpp
        src/platform/opengl/utils.hpp
        src/platform/opengl/vertex_array_cache.hpp
        src/platform/opengl/opengl_state_cache.hpp
        src/platform/opengl/spirv_to_glsl.hpp
        src/platform/opengl/spirv_to_glsl.cpp
        src/shader/hlsl_compiler.cpp
        src/shader/shader_annotations.cpp
        src/shader/shader_reflection.hpp
        src/shader/shader_reflection.cpp
        include/crimson/renderer/texture.hpp
        src/platform/vulkan/utils.hpp
        src/platform/opengl/opengl_imgui_backend.hpp
        src/platform/opengl/opengl_imgui_backend.cpp
        src/platform/vulkan/vulkan_imgui_backend.hpp
        src/platform/vulkan/vulkan_imgui_backend.cpp
		src/platform/vulkan/vulkan_device.hpp
		src/platform/vulkan/vulkan_device.cpp
		src/platform/vulkan/vulkan_resource_manager.hpp
		src/platform/vulkan/vulkan_resource_manager.cpp
		src/platform/vulkan/vulkan_renderer.hpp
		src/platform/vulkan/vulkan_renderer.cpp
		src/graphics/graphics_backend.cpp
		src/platform/opengl/opengl_device.hpp
		src/platform/opengl/opengl_device.cpp
        src/asset_manager/asset_id.cpp
        src/asset_manager/meta_file.cpp
        include/crimson/asset_manager/asset_importer.hpp
        src/asset_manager/asset_manager.cpp
        src/asset_manager/material_data.cpp
        src/asset_manager/scene_data.cpp
        src/asset_manager/asset_ref.hpp
        src/asset_manager/asset_ref.cpp
        src/asset_manager/mtl_materials.hpp
        src/asset_manager/mtl_materials.cpp
        include/crimson/asset_manager/importers/texture_importer.hpp
        src/asset_manager/importers/texture_importer.cpp
        src/asset_manager/importers/material_importer.cpp
        src/asset_manager/importers/mesh_importer.cpp
        src/asset_manager/importers/shader_importer.cpp
        src/asset_manager/importers/scene_importer.cpp
        src/scene/scene.cpp
        src/scene/scene_renderer.cpp
        src/scene/scene_serializer.cpp
        src/project/project.cpp
        src/project/shader_includes.cpp
)

add_library(Engine STATIC
        ${ENGINE_PUBLIC_HEADERS}
        ${ENGINE_IMPLEMENTATION}
)

target_compile_features(Engine PUBLIC cxx_std_23)
set_target_properties(Engine PROPERTIES
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

target_include_directories(Engine PUBLIC include PRIVATE src)

crimson_embed_resources(Engine DIRECTORY resources NAMESPACE crimson FUNCTION GetEngineResources)

# vulkan_raii.hpp is very large, so only the Vulkan backend pays for it
set(ENGINE_VULKAN_SOURCES ${ENGINE_IMPLEMENTATION})
list(FILTER ENGINE_VULKAN_SOURCES INCLUDE REGEX "src/platform/vulkan/.*\.cpp$")
list(APPEND ENGINE_VULKAN_SOURCES src/graphics/graphics_backend.cpp)

set(ENGINE_OTHER_SOURCES ${ENGINE_IMPLEMENTATION})
list(FILTER ENGINE_OTHER_SOURCES INCLUDE REGEX "\.cpp$")
list(REMOVE_ITEM ENGINE_OTHER_SOURCES ${ENGINE_VULKAN_SOURCES})

target_precompile_headers(Engine PRIVATE src/platform/vulkan/vulkan_include.hpp)
set_source_files_properties(${ENGINE_OTHER_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(MINGW)
    target_compile_options(Engine PRIVATE -Wa,-mbig-obj)
endif()

target_link_libraries(Engine
    PUBLIC
        glm::glm-header-only
        imgui
        fmt::fmt
        nlohmann_json::nlohmann_json
        EnTT::EnTT
        Tracy::TracyClient
    PRIVATE
        glfw
        glad
		Vulkan::Vulkan
        spirv-reflect-static
        spirv-cross-glsl
        glslang::glslang
        glslang::SPIRV
        glslang::glslang-default-resource-limits
        stb_image
        tinyobjloader
        GPUOpen::VulkanMemoryAllocator
)