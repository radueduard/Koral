# ---------------------------------------------------------------------------
# The image import module: koral.image.import
#
# Everything that turns a file into a kor::Image — OpenImageIO for the ordinary
# formats, libktx for KTX/KTX2 (including transcoding a universal texture into
# whatever block format the device has) — plus the two cubemap paths.
#
# Writing images is koral-image-export's job, and producing a compressed KTX2 is
# koral-image-compress's. This is also where the engine's image IO went when it
# became a module, which is why Koral itself no longer links OpenImageIO or KTX.
#
# A consumer links koral-image-import exactly as it links Koral. Nothing has to
# be named in koral.json.
# ---------------------------------------------------------------------------

add_library(koral-image-import SHARED
        src/export.cpp
        src/decode.cpp
        src/loader.cpp
        src/cubemap.cpp
)

target_include_directories(koral-image-import PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Installed into a directory of this module's own, not the SDK's shared include/ —
        # a module is a separate project, and its headers reach only the consumers that
        # link it. Linking is what puts this directory on their include path.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-image-import>)

# Picks the export half of KIMG_API on Windows; everywhere else visibility does
# the same job without needing to know which side of the boundary it is on.
target_compile_definitions(koral-image-import PRIVATE KORAL_IMAGE_IMPORT_EXPORTS)

# Koral is PUBLIC: koralImage.h is built on kor::Image, kor::Resource and kor::Task, so anything
# that links this module links Koral too, and inherits its include directories.
target_link_libraries(koral-image-import PUBLIC Koral)

# Everything else is PRIVATE, exactly as for Koral itself — a consumer gets glm through
# Koral::Koral (in an installed SDK, through the vendored copies KoralConfig.cmake attaches to it),
# and never sees OpenImageIO or KTX at all.
target_link_libraries(koral-image-import PRIVATE
        glm::glm
        spdlog::spdlog
        OpenImageIO::OpenImageIO
        KTX::ktx
        # KTX1 files name their format with a GL enum, so reading one means knowing the GL
        # constants. Headers only — no context is created here.
        GLEW::GLEW
        # Vulkan for the VkFormat enum a KTX2 file names its format with, again as constants:
        # the module talks to the device only through Koral.
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
    target_link_options(koral-image-import PRIVATE -Wl,--allow-multiple-definition)
endif()
if(MSVC)
    target_link_options(koral-image-import PRIVATE /FORCE:MULTIPLE /IGNORE:4006 /IGNORE:4088)
endif()

# Into <libKoral's dir>/modules, where the loader looks without configuration.
set_target_properties(koral-image-import PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# An installed module sits in <libdir>/modules and needs libKoral from <libdir>, one level up.
if(APPLE)
    set_target_properties(koral-image-import PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-image-import PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# The projection shader has to be findable at runtime, in a *dev* build as much as an installed one.
# The install rule below covers the second (share/Koral/shaders is a search root); this covers the
# first, because <libKoral's dir>/shaders is a search root too.
#
# That directory is asked of the Koral target rather than assumed to be the top of the build tree.
# The two are the same only under a single-config generator: Visual Studio and Xcode put the library
# in a per-configuration subdirectory, so staging to ${CMAKE_BINARY_DIR}/shaders left the file one
# level above the only place the search root looks — and every cubemap test failed with "cannot open
# file 'equirectToCube.slang'" on Windows while passing on Linux and macOS.
add_custom_command(TARGET koral-image-import POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:Koral>/shaders
        COMMENT "Staging the image module's shaders where the shader search roots look")

# Ship with the SDK, in the same export set as Koral so a project can link it by name.
install(TARGETS koral-image-import
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-image-import
        FILES_MATCHING PATTERN "*.h")

# equirectToCube.slang goes into Koral's own shaders/ directory, which is already a search root, so
# the module resolves it with no path registered anywhere.
install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/shaders)
