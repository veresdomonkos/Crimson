add_library(Engine STATIC 
    src/Window.cpp
)

target_include_directories(Engine PUBLIC include)

target_link_libraries(Engine PRIVATE glfw imgui)