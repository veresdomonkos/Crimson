set(ENGINE_PUBLIC_HEADERS
		include/crimson/core/application.hpp
		include/crimson/core/core.hpp
		include/crimson/core/window.hpp
		include/crimson/core/events.hpp
		include/crimson/renderer/renderer_backend.hpp
        include/crimson/renderer/render_command_queue.hpp
        include/crimson/renderer/render_commands.hpp
        include/crimson/renderer/renderer_api.hpp
		include/crimson/core/log.hpp
)

set(ENGINE_IMPLEMENTATION
		src/core/application.cpp
		src/platform/glfw/glfw_window.hpp
		src/platform/glfw/glfw_window.cpp
        src/platform/opengl/opengl_backend_glfw.cpp
        src/platform/opengl/opengl_backend.hpp
        src/renderer/renderer_backend.cpp
		src/core/log.cpp
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