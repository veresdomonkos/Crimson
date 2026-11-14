# Precompiled headers hold only standard and third-party headers, never crimson/ or crimson_editor/ ones,
# because an edit to a header in the list rebuilds the whole target. Files keep their own includes.
# Never add GLFW or glad (opengl_device.hpp defines GLFW_INCLUDE_NONE first), windows.h (main.cpp sets macros before it)
# or vk_mem_alloc.h (VMA_IMPLEMENTATION).

# The standard headers most files include
set(CRIMSON_PRECOMPILED_STANDARD
    <algorithm> <array> <cstdint> <filesystem> <functional> <memory> <optional> <span> <string> <string_view>
    <unordered_map> <utility> <vector>
)

# The Engine's third-party dependencies that most files include
set(CRIMSON_PRECOMPILED_ENGINE_DEPS
    <nlohmann/json.hpp> <fmt/format.h> <fmt/color.h> <glm/glm.hpp> <entt/entity/registry.hpp>
)

# CMake adds -Winvalid-pch with a precompiled header on GCC and Clang, so GCC doesn't quietly parse the headers again
# when it rejects a .gch
function(crimson_precompile_headers target)
    target_precompile_headers(${target} PRIVATE ${CRIMSON_PRECOMPILED_STANDARD} ${ARGN})
endfunction()
