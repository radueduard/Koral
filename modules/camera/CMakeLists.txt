# ---------------------------------------------------------------------------
# The camera module: koral.camera
#
# One target, linked by whoever uses it. A consumer (a scene, or a module built
# on this one) links koral-camera exactly as it links Koral: the header declares
# the API, the library defines it, and loading the library is what registers the
# module with the runtime. Nothing has to be named in koral.json.
#
# Inside the library the split mirrors a project's: export.cpp holds the
# declaration the loader needs and nothing else, while the rest is the module's
# own business — the camera objects, the controllers, the builders that hand a
# camera to the runtime.
# ---------------------------------------------------------------------------

add_library(koral-camera SHARED
        src/export.cpp
        src/builders.cpp
        src/camera.cpp
        src/controller.cpp
)

target_include_directories(koral-camera PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-camera>)

# Picks the export half of KCAM_API on Windows; everywhere else visibility does
# the same job without needing to know which side of the boundary it is on.
target_compile_definitions(koral-camera PRIVATE KORAL_CAMERA_EXPORTS)


# Koral is PUBLIC: koralCamera.h is built on kor::Resource and ::Builder, so anything
# that links this module links Koral too, and inherits its include directories.
target_link_libraries(koral-camera PUBLIC Koral)

# Everything else is PRIVATE, exactly as for Koral itself. glm and spdlog do appear in
# this module's public header, but a consumer gets them from Koral::Koral — in an
# installed SDK through the vendored copies KoralConfig.cmake attaches to it. Naming
# them PUBLIC here would put `glm::glm` in the exported link interface, where a
# consumer's CMake has no such target and find_package(Koral) fails outright.
#
# ImGui is deliberately absent. The controller does ask whether the interface is using the pointer,
# but through kor::GUI::WantsMouse — so a camera never becomes an ImGui client, and this module has
# no interface of its own to draw. @see koralCameraPanel.h in koral-gui-extras
target_link_libraries(koral-camera PRIVATE
        glm::glm
        spdlog::spdlog
)

# Into <libKoral's dir>/modules, where the loader looks without configuration.
# RUNTIME_OUTPUT_DIRECTORY is the same thing for Windows, where a .dll is a
# "runtime" artifact.
set_target_properties(koral-camera PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one
# level up. Without this the installed .so carries no RPATH at all and `ldd` reports
# "libKoral.so => not found". Windows has no RPATH; there the DLL sits beside the
# runtime's own DLLs, which is how it is found.
if(APPLE)
    set_target_properties(koral-camera PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-camera PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# Ship with the SDK, in the same export set as Koral so a project can link it by
# name: the library into the modules/ dir beside libKoral, the header where
# consumers can include it.
install(TARGETS koral-camera
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-camera
        FILES_MATCHING PATTERN "*.h")

# camera.slang declares the [camera(...)] attribute a Slang shader annotates with. It goes into
# Koral's own shaders/ directory, which is already a search root, so `import camera;` resolves for
# any project without a path being registered anywhere.
install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/shaders)

