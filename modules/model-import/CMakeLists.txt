# ---------------------------------------------------------------------------
# The model import module: koral.model.import
#
# Reading a 3D model file — glTF, FBX, OBJ, everything Assimp reads — into plain
# CPU-side arrays, plus the bridge that pours that geometry into one of the mesh
# module's vertex formats.
#
# Assimp is here rather than in the engine: this module is what took it, and its
# whole tree of format readers, off Koral's link line.
#
# A consumer links koral-model-import exactly as it links Koral.
# ---------------------------------------------------------------------------

add_library(koral-model-import SHARED
        src/export.cpp
        src/importer.cpp
)

target_include_directories(koral-model-import PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-model-import>)

# Picks the export half of KIMG_API on Windows; everywhere else visibility does
# the same job without needing to know which side of the boundary it is on.
target_compile_definitions(koral-model-import PRIVATE KORAL_MODEL_IMPORT_EXPORTS)

# Koral is PUBLIC, and it is the only one: koralModelImport.h is built on kor::Resource and kor::Task,
# so anything that links this module links Koral too and inherits its include directories.
#
# koral-mesh is deliberately *not* here. The bridge to it, koralModelMesh.h, is templates only — they
# compile into the consumer, not into this library, so this library neither calls kmesh nor needs its
# headers. A project that wants the bridge links koral-mesh as well and the header finds it; one that
# only wants the imported CPU-side arrays pays nothing for a mesh module it never asked for. Naming
# koral-mesh here would take that choice away from every consumer.
target_link_libraries(koral-model-import PUBLIC Koral)

# Everything else is PRIVATE, exactly as for Koral itself — a consumer gets glm through Koral::Koral
# (in an installed SDK, through the vendored copies KoralConfig.cmake attaches to it) and never sees
# Assimp at all.
target_link_libraries(koral-model-import PRIVATE
        glm::glm
        spdlog::spdlog
        # Assimp, and only here: the engine no longer links it at all.
        assimp::assimp
)

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-model-import PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one level up.
if(APPLE)
    set_target_properties(koral-model-import PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-model-import PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# Ship with the SDK, in the same export set as Koral so a project can link it by name.
install(TARGETS koral-model-import
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-model-import
        FILES_MATCHING PATTERN "*.h")
