# Getting started

Three pages, in order:

1. [Building the SDK](building.md): Koral from source, and the SDK a program builds against.
2. [A first program](first-program.md): a window with a triangle in it, in thirty lines of C++.
3. [How Koral fits together](overview.md): the application, scenes, resources, command buffers and the frame graph,
   and which guide says more about each.

What a program needs:

- A GPU with Vulkan 1.3 and its driver.
- A C++23 compiler and CMake 3.28.
- [vcpkg](https://vcpkg.io), which fetches and builds Koral's dependencies.
- For the C# bindings, the .NET 10 SDK; for Kotlin, JDK 25. Without them, the build leaves those bindings out.
