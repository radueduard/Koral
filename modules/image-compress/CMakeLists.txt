# ---------------------------------------------------------------------------
# The image compression module: koral.image.compress
#
# Turning an ordinary image into a GPU-ready KTX2: decode, build a mip chain,
# hand it to libktx's Basis or ASTC encoder, write the container. No GPU is
# involved, which is what makes it usable as a build step.
#
# It reads its input through koral-image-import rather than opening files itself,
# so it links that module and declares the dependency in its descriptor.
#
# A consumer links koral-image-compress exactly as it links Koral.
# ---------------------------------------------------------------------------

add_library(koral-image-compress SHARED
        src/export.cpp
        src/compress.cpp
)

target_include_directories(koral-image-compress PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-image-compress>)

# Picks the export half of KIMG_API on Windows; everywhere else visibility does
# the same job without needing to know which side of the boundary it is on.
target_compile_definitions(koral-image-compress PRIVATE KORAL_IMAGE_COMPRESS_EXPORTS)

# Koral is PUBLIC: koralImage.h is built on kor::Image, kor::Resource and kor::Task, so anything
# that links this module links Koral too, and inherits its include directories.
# Koral and the import module are both PUBLIC: koralImageCompress.h is built on kor::Result and on
# kimg::CpuImage, so anything that links this links both and inherits their include directories.
target_link_libraries(koral-image-compress PUBLIC Koral koral-image-import)

# Everything else is PRIVATE, exactly as for Koral itself — a consumer gets glm through Koral::Koral
# (in an installed SDK, through the vendored copies KoralConfig.cmake attaches to it) and never sees
# libktx at all.
target_link_libraries(koral-image-compress PRIVATE
        glm::glm
        spdlog::spdlog
        KTX::ktx
        # Vulkan for the VkFormat enum a KTX2 names its format with, as constants only: this module
        # talks to the device exclusively through Koral.
        Vulkan::Vulkan
)

# Inherited through koral-image-import, which links OpenImageIO: its header-only fmt collides with
# spdlog's compiled one at link time. @see modules/image-import/CMakeLists.txt
if(UNIX AND NOT APPLE)
    target_link_options(koral-image-compress PRIVATE -Wl,--allow-multiple-definition)
endif()
if(MSVC)
    target_link_options(koral-image-compress PRIVATE /FORCE:MULTIPLE /IGNORE:4006 /IGNORE:4088)
endif()

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-image-compress PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one level up.
if(APPLE)
    set_target_properties(koral-image-compress PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-image-compress PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# Ship with the SDK, in the same export set as Koral so a project can link it by name.
install(TARGETS koral-image-compress
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-image-compress
        FILES_MATCHING PATTERN "*.h")
