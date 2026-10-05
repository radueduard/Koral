# x64-windows-static-md, with GLFW forced to a shared build.
#
# Identical to vcpkg's built-in x64-windows-static-md — dynamic CRT, everything else a static
# archive — apart from the per-port override at the bottom, which is the entire reason this file
# exists. It shadows the built-in because vcpkg-overlay-triplets is on VCPKG_OVERLAY_TRIPLETS (see
# the top of the root CMakeLists), and overlay triplets win over the stock ones of the same name.
#
# Why GLFW must be a DLL on Windows:
#
# GLFW keeps process-global state — whether glfwInit() has run, its windows, its callbacks. Linux
# and macOS share a single instance of it across module boundaries for free, because the ELF/Mach-O
# loaders interpose symbols: the first definition loaded wins for the whole process. Windows does no
# such thing — each module binds to its own statically linked copy at link time. libKoral.dll and
# Koral_Runtime.exe both call GLFW, so a *static* GLFW would give each its own copy, and the one the
# runtime asks about would never have been initialised by the one the engine drives. A single
# glfw3.dll shared by both restores the one instance the other two platforms already have.
#
# The DLL lands next to Koral_Runtime.exe automatically: libKoral depends on it, so the
# GET_RUNTIME_DEPENDENCIES install step in the root CMakeLists bundles it like slang and Vulkan.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT STREQUAL "glfw3")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
