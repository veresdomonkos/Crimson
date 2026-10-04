find_package(Vulkan REQUIRED)

set(ENGINE_PUBLIC_HEADERS
		include/crimson/core/core.hpp
		include/crimson/core/window.hpp
		include/crimson/core/events.hpp
        include/crimson/renderer/render_commands.hpp
        include/crimson/core/log.hpp
        include/crimson/renderer/renderer_api.hpp
        include/crimson/renderer/resource_manager.hpp
        include/crimson/renderer/renderer.hpp
        include/crimson/renderer/frame_context.hpp
        include/crimson/core/command_buffer.hpp
        include/crimson/renderer/handle_registry.hpp
        include/crimson/renderer/resource_handles.hpp
        include/crimson/renderer/core.hpp
        include/crimson/renderer/resource_manager_base.hpp
        include/crimson/renderer/buffer_layout.hpp
        include/crimson/renderer/buffers.hpp
        include/crimson/renderer/graphics_pipeline_cache.hpp
        include/crimson/renderer/camera_data.hpp
        include/crimson/renderer/shader_property_info.hpp
        include/crimson/renderer/binding_conventions.hpp
        include/crimson/renderer/light.hpp
        include/crimson/renderer/frame_data.hpp
		include/crimson/graphics/graphincs_backend.hpp
		include/crimson/graphics/graphics_device.hpp
        include/crimson/renderer/resource_tags.hpp
        include/crimson/asset_manager/asset_id.hpp
        include/crimson/asset_manager/asset_types.hpp
        include/crimson/asset_manager/meta_file.hpp
        include/crimson/asset_manager/asset_manager.hpp
        include/crimson/asset_manager/importers/texture_importer.hpp
        include/crimson/asset_manager/importers/material_importer.hpp
        include/crimson/renderer/mesh.hpp
        include/crimson/asset_manager/importers/mesh_importer.hpp
)

set(ENGINE_IMPLEMENTATION
		src/platform/glfw/glfw_window.hpp
		src/platform/glfw/glfw_window.cpp
        src/core/log.cpp
        src/platform/opengl/opengl_resource_manager.hpp
        src/platform/opengl/opengl_resource_manager.cpp
        src/platform/opengl/opengl_renderer.hpp
        src/platform/opengl/opengl_renderer_glfw.cpp
		src/platform/opengl/opengl_resources.hpp
        src/platform/vulkan/vulkan_resources.hpp
        src/platform/opengl/utils.hpp
        src/platform/opengl/vertex_array_cache.hpp
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
        include/crimson/asset_manager/importers/texture_importer.hpp
        src/asset_manager/importers/texture_importer.cpp
        src/asset_manager/importers/material_importer.cpp
        src/asset_manager/importers/mesh_importer.cpp
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

target_include_directories(Engine PUBLIC include)

target_link_libraries(Engine
    PUBLIC
        glm::glm-header-only
        imgui
        fmt::fmt
        nlohmann_json::nlohmann_json
    PRIVATE
        glfw
        glad
		Vulkan::Vulkan
        spirv-reflect-static
        stb_image
        tinyobjloader
)