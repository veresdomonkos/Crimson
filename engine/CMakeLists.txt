file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS "include/*.h" "include/*.hpp")

add_library(Engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

source_group("Source Files" FILES ${ENGINE_SOURCES})
source_group("Header Files" FILES ${ENGINE_HEADERS})

target_include_directories(Engine PUBLIC include)
target_link_libraries(Engine PRIVATE glfw imgui glad)