# ---------------------------------------------------------------------------
# The image export module: koral.image.export
#
# Writing a kor::Image, or any subimage of one, to a file: OpenImageIO for the
# ordinary containers, libktx for the one that can hold a whole texture — every
# mip of every layer, block-compressed or not.
#
# Reading files is koral-image-import's job. The two are separate libraries
# because most projects only ever do one of them: a game reads textures, a tool
# writes them, and neither should carry the other's dependencies.
#
# A consumer links koral-image-export exactly as it links Koral. Nothing has to
# be named in koral.json.
# ---------------------------------------------------------------------------

add_library(koral-image-export SHARED
        src/export.cpp
        src/writer.cpp
        src/ktxWriter.cpp
)

target_include_directories(koral-image-export PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-image-export>)

# Picks the export half of KIMG_API on Windows; everywhere else visibility does
# the same job without needing to know which side of the boundary it is on.
target_compile_definitions(koral-image-export PRIVATE KORAL_IMAGE_EXPORT_EXPORTS)

# Koral is PUBLIC: koralImage.h is built on kor::Image, kor::Resource and kor::Task, so anything
# that links this module links Koral too, and inherits its include directories.
target_link_libraries(koral-image-export PUBLIC Koral)

# Everything else is PRIVATE, exactly as for Koral itself — a consumer gets glm through
# Koral::Koral (in an installed SDK, through the vendored copies KoralConfig.cmake attaches to it),
# and never sees OpenImageIO or KTX at all.
target_link_libraries(koral-image-export PRIVATE
        glm::glm
        spdlog::spdlog
        OpenImageIO::OpenImageIO
        KTX::ktx
        # Vulkan for the VkFormat enum a KTX2 names its format with, as constants only: this module
        # talks to the device exclusively through Koral.
        Vulkan::Vulkan
        ${TIFF_LIBRARIES} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES} ${PNG_LIBRARIES} ${JASPER_LIBRARIES}
)

# Moved here with OpenImageIO, and for the same reason it was needed in the engine: OpenImageIO
# links fmt::fmt-header-only, which bakes inline fmt definitions into our objects, while
# spdlog::spdlog links the *compiled* fmt whose library exports the same symbols as strong
# definitions. GNU ld is told to tolerate the duplicates; MSVC otherwise fails the link with a wall
# of LNK2005. The duplicate definitions are identical code, so picking the first is safe in
# practice — the real fix is to stop mixing header-only and compiled fmt.
if(UNIX AND NOT APPLE)
    target_link_options(koral-image-export PRIVATE -Wl,--allow-multiple-definition)
endif()
if(MSVC)
    target_link_options(koral-image-export PRIVATE /FORCE:MULTIPLE /IGNORE:4006 /IGNORE:4088)
endif()

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-image-export PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one level up.
if(APPLE)
    set_target_properties(koral-image-export PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-image-export PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# Ship with the SDK, in the same export set as Koral so a project can link it by name.
install(TARGETS koral-image-export
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-image-export
        FILES_MATCHING PATTERN "*.h")
