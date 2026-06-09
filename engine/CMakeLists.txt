add_library(Engine STATIC
	"include/crimson/core/application.hpp"
	"include/crimson/core/core.hpp"
	"include/crimson/core/window.hpp"
	"include/crimson/core/events.hpp"
	"src/core/application.cpp" 
	"src/platform/windows/windows_window.hpp"
	"src/platform/windows/windows_window.cpp" 
)

target_include_directories(Engine PUBLIC include)
target_link_libraries(Engine PRIVATE glfw imgui glad)