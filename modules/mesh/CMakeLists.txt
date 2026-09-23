# ---------------------------------------------------------------------------
# The mesh module: koral.mesh
#
# Vertex formats, the meshes built from them, the heap that packs many into one
# set of buffers, and the bridge that fills a format from an imported file. All
# of it templates, so the library itself holds only the module declaration —
# what a consumer links it for is the headers and the identity.
#
# A consumer links koral-mesh exactly as it links Koral. Nothing has to be named
# in koral.json.
# ---------------------------------------------------------------------------

add_library(koral-mesh SHARED
        src/export.cpp
)

target_include_directories(koral-mesh PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-mesh>)

# Koral is PUBLIC: koralMesh.h is built on kor::Mesh, kor::VertexLayout and kor::Resource, so
# anything that links this module links Koral too, and inherits its include directories.
target_link_libraries(koral-mesh PUBLIC Koral)

# PRIVATE, exactly as for Koral itself: glm appears in this module's public headers, but a
# consumer gets it from Koral::Koral — in an installed SDK through the vendored copies
# KoralConfig.cmake attaches to it. Naming it PUBLIC here would put `glm::glm` in the exported
# link interface, where a consumer's CMake has no such target and find_package(Koral) fails.
target_link_libraries(koral-mesh PRIVATE
        glm::glm
        spdlog::spdlog
)

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-mesh PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one level up.
# Without this the installed .so carries no RPATH at all. Windows has no RPATH; there the DLL sits
# beside the runtime's own DLLs, which is how it is found.
if(APPLE)
    set_target_properties(koral-mesh PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-mesh PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# Ship with the SDK, in the same export set as Koral so a project can link it by name.
install(TARGETS koral-mesh
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-mesh
        FILES_MATCHING PATTERN "*.h")
