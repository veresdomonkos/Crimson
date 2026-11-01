#pragma once

// Precompiled for the Vulkan backend, see engine/CMakeLists.txt.
// Out of date swapchains are a normal resize event, so they come back as a result instead of an exception.
#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
#include <vulkan/vulkan_raii.hpp>
