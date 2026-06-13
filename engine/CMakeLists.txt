set(ENGINE_PUBLIC_HEADERS
		include/crimson/core/application.hpp
		include/crimson/core/core.hpp
		include/crimson/core/window.hpp
		include/crimson/core/events.hpp
		include/crimson/renderer/renderer_backend.hpp
        include/crimson/renderer/render_command_queue.hpp
        include/crimson/renderer/render_commands.hpp
        include/crimson/core/log.hpp
        include/crimson/renderer/renderer.hpp
        include/crimson/renderer/rendering_context.hpp
        include/crimson/renderer/render_core.hpp
        include/crimson/renderer/buffers/vertex_buffer.hpp
        include/crimson/renderer/buffers/index_buffer.hpp
)

set(ENGINE_IMPLEMENTATION
		src/core/application.cpp
		src/platform/glfw/glfw_window.hpp
		src/platform/glfw/glfw_window.cpp
        src/platform/opengl/opengl_backend.cpp
        src/platform/opengl/opengl_backend.hpp
        src/renderer/renderer_backend.cpp
		src/core/log.cpp
        src/renderer/renderer.cpp
        src/platform/opengl/opengl_context_glfw.cpp
        src/platform/opengl/opengl_context.hpp
        src/renderer/rendering_context.cpp
        src/renderer/buffers/vertex_buffer.cpp
        src/platform/opengl/opengl_vertex_buffer.cpp
        src/platform/opengl/opengl_vertex_buffer.hpp
        src/renderer/buffers/index_buffer.cpp
        src/platform/opengl/opengl_index_buffer.hpp
        src/platform/opengl/opengl_index_buffer.cpp
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
)