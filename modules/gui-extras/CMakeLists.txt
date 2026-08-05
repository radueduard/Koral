# ---------------------------------------------------------------------------
# The GUI extras module: koral.gui.extras
#
# ImGui widgets built on the engine's GUI — a gradient editor, a file browser —
# that are useful without being the engine's business. Header-only, so the
# library holds only the module declaration; what a consumer links it for is the
# headers and the identity.
#
# A consumer links koral-gui-extras exactly as it links Koral. Nothing has to be
# named in koral.json.
# ---------------------------------------------------------------------------

add_library(koral-gui-extras SHARED
        src/export.cpp
        src/imguizmoBinding.cpp
)

target_include_directories(koral-gui-extras PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-gui-extras>)

# Koral is PUBLIC: the widgets are drawn inside a Koral GUI frame and read Koral's log, timing and
# images, so anything that links this module links Koral too and inherits its include directories.
target_link_libraries(koral-gui-extras PUBLIC Koral)

# ImGuizmo, linked PRIVATE on purpose even though koralGizmo.h calls straight into it from a header.
# Its objects come in here and their symbols carry default visibility, so this shared library
# re-exports them and a consumer resolves ImGuizmo::Manipulate through it — no archive of its own to
# link. Naming it PUBLIC would instead put `imguizmo::imguizmo` in the exported link interface, where
# a consumer's CMake has no such target and find_package(Koral) fails outright: the same trap as glm.
# The *header* still has to reach the consumer, which is what the SDK vendoring does. @see the root
# CMakeLists' KORAL_VENDOR_PUBLIC_DEPS block, and src/imguizmoBinding.cpp for the ImGui context.
find_package(imguizmo CONFIG REQUIRED)
target_link_libraries(koral-gui-extras PRIVATE imguizmo::imguizmo)

# PRIVATE, exactly as for Koral itself: glm appears in this module's public headers, but a
# consumer gets it from Koral::Koral — in an installed SDK through the vendored copies
# KoralConfig.cmake attaches to it. Naming it PUBLIC here would put `glm::glm` in the exported
# link interface, where a consumer's CMake has no such target and find_package(Koral) fails.
#
# imgui is the same story: these headers *are* ImGui code, so a consumer has to compile against
# imgui.h — and gets it from Koral::Koral, which vendors it in an installed SDK.
target_link_libraries(koral-gui-extras PRIVATE
        glm::glm
        spdlog::spdlog
        imgui::imgui
)

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-gui-extras PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one level up.
# Without this the installed .so carries no RPATH at all. Windows has no RPATH; there the DLL sits
# beside the runtime's own DLLs, which is how it is found.
if(APPLE)
    set_target_properties(koral-gui-extras PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-gui-extras PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# Ship with the SDK, in the same export set as Koral so a project can link it by name.
install(TARGETS koral-gui-extras
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-gui-extras
        FILES_MATCHING PATTERN "*.h")
