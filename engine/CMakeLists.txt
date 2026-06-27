find_package(Vulkan REQUIRED)

set(ENGINE_PUBLIC_HEADERS
		include/crimson/core/application.hpp
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
)

set(ENGINE_IMPLEMENTATION
		src/core/application.cpp
		src/platform/glfw/glfw_window.hpp
		src/platform/glfw/glfw_window.cpp
        src/core/log.cpp
        src/platform/opengl/opengl_resource_manager.hpp
        src/platform/opengl/opengl_resource_manager.cpp
        src/platform/opengl/opengl_renderer.hpp
        src/platform/opengl/opengl_renderer_glfw.cpp
        src/renderer/renderer.cpp
		src/platform/vulkan/vulkan_renderer.cpp
		src/platform/vulkan/vulkan_renderer.hpp
		src/platform/vulkan/vulkan_resource_manager.cpp
		src/platform/vulkan/vulkan_resource_manager.hpp
		src/platform/vulkan/vulkan_device.cpp
		src/platform/vulkan/vulkan_device.hpp
        src/platform/opengl/opengl_resources.hpp
        src/platform/vulkan/vulkan_resources.hpp
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
    PRIVATE
        glfw
        glad
		fmt::fmt
		Vulkan::Vulkan
)