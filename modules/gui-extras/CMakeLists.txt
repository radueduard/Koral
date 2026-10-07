# ---------------------------------------------------------------------------
# The GUI extras module: koral.gui.extras
#
# Panels built on koral-ui — a log, statistics, an inspector for reflected objects, a viewport, another
# scene in a panel, a frame graph's own panels, a file browser — that are useful without being the
# engine's business. The camera panel is a header-only seam (koralCameraPanel.h), so this library does
# not link the camera module.
#
# A consumer links koral-gui-extras exactly as it links Koral. Nothing has to be named in koral.json.
# ---------------------------------------------------------------------------

add_library(koral-gui-extras SHARED
        src/export.cpp
        src/logPanel.cpp
        src/statsPanel.cpp
        src/inspector.cpp
        src/viewport.cpp
        src/graphPanels.cpp
        src/fileBrowser.cpp
)

target_include_directories(koral-gui-extras PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-gui-extras>)

target_compile_definitions(koral-gui-extras PRIVATE KORAL_GUI_EXTRAS_EXPORTS)

# koral-ui is PUBLIC: every panel here is a kui::Widget, so anything that links this module links
# koral-ui too — and Koral through it — and inherits their include directories.
target_link_libraries(koral-gui-extras PUBLIC koral-ui Koral)

# PRIVATE, exactly as for Koral itself: spdlog appears in Koral's public headers, and a
# consumer gets it from Koral::Koral — in an installed SDK through the vendored copies
# KoralConfig.cmake attaches to it. Naming it PUBLIC here would put `spdlog::spdlog` in the exported
# link interface, where a consumer's CMake has no such target and find_package(Koral) fails.
target_link_libraries(koral-gui-extras PRIVATE
        spdlog::spdlog
)

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-gui-extras PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

if(MSVC)
    # No manifest of its own, for the same reason as koral-ui's: see modules/ui/CMakeLists.txt.
    target_link_options(koral-gui-extras PRIVATE /MANIFEST:NO)
endif()

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
