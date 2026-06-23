# Crimson Engine

Crimson is a high-performance, data-driven rendering engine architecture designed for scalability and robustness. The system leverages type-safe resource management and a command-buffer-based rendering pipeline to decouple CPU-side logic from GPU execution.

## Core Architecture

### 1. Command Processing
The engine utilizes a `RenderCommandBuffer` to batch GPU commands, enabling thread-safe command submission and improved cache locality.
* **Command Buffer**: Commands are serialized into a contiguous memory buffer.
* **Memory Alignment**: Custom `AlignUp` mechanisms and `static_assert` checks ensure that all commands meet memory alignment requirements, preventing undefined behavior and hardware performance penalties.
* **Extensibility**: Commands are defined as structures (e.g., `BeginRenderPassCommand`) and interpreted via a type-safe `CommandView` pattern.

### 2. Resource Management
To ensure platform-agnostic resource handling, the engine employs a `HandleRegistry` pattern.
* **Handle System**: Resources are accessed via opaque `Handle<Tag>` types containing an `Id` and `Generation` for invalidation safety, rather than raw pointers.
* **Registry**: The `HandleRegistry<Handle, T>` class provides a unified interface for registering, retrieving, and finding engine resources (Textures, Framebuffers, Surfaces).
* **Abstraction**: A `ResourceManager` base class provides a virtual interface for platform-specific implementations (e.g., OpenGL, Vulkan), effectively isolating the renderer from backend details.

### 3. Rendering Pipeline
* **Render Surfaces**: Represents windowing abstractions, storing native handles and associated backbuffer information.
* **Render Targets**: Encapsulates framebuffer objects, including dimensions and texture attachments.
* **Render Passes**: Managed through `BeginRenderPass` commands that encapsulate clear flags, clear colors, and viewport settings, providing a clean state for rendering commands.

## Key Components

| Component             | Responsibility |
|:----------------------| :--- |
| `RenderCommandBuffer` | Submits and stores commands for execution. |
| `HandleRegistry`      | Manages object lifetimes via ID/Generation handles. |
| `ResourceManager`     | Provides backend-specific resource access. |
| `Traits`              | Defines platform-specific types (e.g., `OpenglResourceTraits`). |

Markdown
## Build Requirements
- Compiler: C++23 compliant compiler (GCC 13+, Clang 16+, or MSVC 19.35+)
- Build System: CMake 3.20+

---
*Note: This documentation reflects the current internal structure of the Crimson engine, emphasizing modularity and memory safety.*