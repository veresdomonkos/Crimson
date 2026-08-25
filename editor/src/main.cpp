#include <cstring>
#include <iostream>
#include <process.hpp>

#include "editor/editor_application.hpp"

int main(int argc, char *argv[])
{
    crimson::editor::EditorApplication app;
    app.Run();
}