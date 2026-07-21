# x64-windows-static-md, with ImGui (and its GLFW backend dependency) forced to a shared build.
#
# Identical to vcpkg's built-in x64-windows-static-md — dynamic CRT, everything else a static
# archive — apart from the per-port override at the bottom, which is the entire reason this file
# exists. It shadows the built-in because vcpkg-overlay-triplets is on VCPKG_OVERLAY_TRIPLETS (see
# the top of the root CMakeLists), and overlay triplets win over the stock ones of the same name.
#
# Why ImGui must be a DLL on Windows:
#
# ImGui keeps process-global state — the current-context pointer (GImGui) and the allocator
# function pointers it routes every allocation through. Linux and macOS share a single instance of
# these across the libKoral <-> consumer-EXE boundary for free, because the ELF/Mach-O loaders
# interpose symbols: the first definition loaded wins for the whole process. Windows does no such
# thing — each module binds to its own statically linked copy at link time. So a *static* ImGui
# gives libKoral.dll and the EXE each their own GImGui and their own allocator: two contexts (hence
# the SetCurrentContext-every-frame workaround) and, worse, objects allocated on one side and freed
# on the other — heap corruption that shows up as a hang a frame or two in. Building ImGui as a DLL
# restores the single shared instance the other two platforms already have.
#
# glfw3 comes along for the ride, and must. ImGui's GLFW backend (imgui_impl_glfw) is compiled into
# the ImGui library and calls GLFW directly, so an ImGui DLL links GLFW. Were GLFW left static,
# ImGui.dll would embed its own copy of GLFW's global state — separate from, and never glfwInit()'d
# like, the one the engine drives. Input would silently break and ImGui_ImplGlfw_NewFrame would read
# an uninitialised library. A single glfw3.dll shared by both keeps the backend and the engine on
# the same GLFW instance. (The Vulkan loader is already a DLL, so the Vulkan backend is fine.)
#
# The two DLLs land next to Koral_Runtime.exe automatically: libKoral now depends on them, so the
# GET_RUNTIME_DEPENDENCIES install step in the root CMakeLists bundles them like slang/vulkan. Note
# this does change the SDK-vendoring story for third-party consumers — see KORAL_VENDOR_PUBLIC_DEPS.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT MATCHES "^(imgui|glfw3)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()