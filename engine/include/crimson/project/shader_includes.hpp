#pragma once
#include "crimson/project/project.hpp"

namespace crimson
{
    // The engine's shader headers are built in, so a code editor opening a project's shader can't find them.
    // This writes copies of them to the project's data folder, and adds that folder to the include
    // directories in the shadertoolsconfig.json at the project's root, which HLSL Tools reads.
    // Shaders still compile with the built-in headers, so the copies only have to be there for reading.
    //
    // Only what is out of date gets written, and copies of headers the engine no longer has are removed.
    // Failures go to the log, as the project works without them.
    void ExportShaderIncludes(const Project& project);
}
