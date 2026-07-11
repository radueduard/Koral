cmake_minimum_required(VERSION 3.28)
project(GFX_RELOADED VERSION 0.0.1 LANGUAGES C CXX)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_OSX_SYSROOT ${CMAKE_OSX_SYSROOT} CACHE PATH "macOS SDK path")
endif()

find_package(TIFF REQUIRED)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(Jasper REQUIRED)

find_package(glfw3 CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(assimp REQUIRED)

# Pin find_package(Vulkan) to the vcpkg-provided headers + loader instead of a
# system SDK. Without this, CMake's FindVulkan picks up /usr/{include,lib}; we then
# link the system libvulkan while the vcpkg loader also sits in the runtime search
# path, so CMake strips the vcpkg dir from the RPATH "for safety" and the build
# silently runs the system loader — defeating the bundled-WSI loader and emitting
# "Cannot generate a safe runtime search path" warnings. Pointing FindVulkan at the
# vcpkg tree makes link-time and runtime agree (one loader), which removes both.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(GFX_VCPKG_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(EXISTS "${GFX_VCPKG_PREFIX}/include/vulkan/vulkan.h")
        set(Vulkan_INCLUDE_DIR "${GFX_VCPKG_PREFIX}/include" CACHE PATH "vcpkg Vulkan headers" FORCE)
        # Loader lib: release tree serves both configs (the C loader is config-agnostic
        # and its debug/release copies are ABI-identical). Drop any stale cached path
        # (e.g. a prior /usr/lib pick) so the vcpkg-only search below actually applies.
        unset(Vulkan_LIBRARY CACHE)
        find_library(Vulkan_LIBRARY NAMES vulkan vulkan-1
                PATHS "${GFX_VCPKG_PREFIX}/lib" NO_DEFAULT_PATH)
    endif()
endif()

find_package(Vulkan REQUIRED)
message(STATUS "Using Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Using Vulkan loader:  ${Vulkan_LIBRARIES}")

# ---------------------------------------------------------------------------
# Runtime Vulkan loader.
#
# We link and ship the vcpkg-built Vulkan-Loader. The manifest requests its WSI
# features (Wayland + XCB + Xlib on Linux, Win32 is always built, Metal on macOS
# via molten-vk), so this one loader creates surfaces on every platform and still
# discovers the user's installed ICDs/drivers and layers through the standard
# search paths + env vars. No system loader or LD_LIBRARY_PATH override needed.
#
# Bundled layers/ICD manifest dirs (from vcpkg) are handed to the runtime as
# compile definitions below so it can add them to the loader's search paths at
# startup — this makes validation (and MoltenVK on macOS) work with no system SDK.
# ---------------------------------------------------------------------------
set(GFX_VK_SHARE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

find_package(Ktx CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

find_package(asio CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)



file(GLOB_RECURSE SOURCE_FILES
        src/core/*.cpp
        src/backends/*.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

generate_export_header(${PROJECT_NAME}
        BASE_NAME GFX
        EXPORT_MACRO_NAME GFX_API
        EXPORT_FILE_NAME include/api.h
)

target_compile_definitions(${PROJECT_NAME}
        PRIVATE GFX_RELOADED_EXPORTS
        INTERFACE GFX_RELOADED_IMPORTS)

if(UNIX AND NOT APPLE)
    set(GLFW_USE_WAYLAND ON)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE magic_enum::magic_enum)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
target_link_libraries(${PROJECT_NAME} PRIVATE GLEW::GLEW OpenGL::GL)
target_link_libraries(${PROJECT_NAME} PRIVATE assimp::assimp)

target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
target_link_libraries(${PROJECT_NAME} PRIVATE GPUOpen::VulkanMemoryAllocator)

# Hand the vcpkg-bundled layer/ICD manifest dirs to the runtime so it can add them
# to the loader's search paths at startup (see runtime.cpp). This makes validation
# available with no system Vulkan SDK; on macOS it also points the loader at the
# molten-vk ICD. Additive (VK_ADD_*), so a user's own layers/drivers still load.
# The manifest layout differs per platform (share/vulkan/... on Linux/macOS, bin/
# on Windows), so glob for the actual file rather than hardcoding one path.
file(GLOB_RECURSE GFX_VK_LAYER_MANIFEST
        "${GFX_VK_SHARE_DIR}/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
        "${GFX_VK_SHARE_DIR}/bin/VkLayer_khronos_validation.json")
if(GFX_VK_LAYER_MANIFEST)
    list(GET GFX_VK_LAYER_MANIFEST 0 GFX_VK_LAYER_MANIFEST0)
    get_filename_component(GFX_VK_LAYER_DIR "${GFX_VK_LAYER_MANIFEST0}" DIRECTORY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GFX_VK_LAYER_PATH="${GFX_VK_LAYER_DIR}")
    message(STATUS "Bundled Vulkan validation layers: ${GFX_VK_LAYER_DIR}")
else()
    message(WARNING "vcpkg validation-layer manifest not found under ${GFX_VK_SHARE_DIR}; "
            "portable validation will be unavailable (loader still finds a system SDK if present).")
endif()

if(APPLE)
    file(GLOB_RECURSE GFX_VK_ICD_MANIFEST
            "${GFX_VK_SHARE_DIR}/share/vulkan/icd.d/MoltenVK_icd.json"
            "${GFX_VK_SHARE_DIR}/bin/MoltenVK_icd.json")
    if(GFX_VK_ICD_MANIFEST)
        list(GET GFX_VK_ICD_MANIFEST 0 GFX_VK_ICD_MANIFEST0)
        target_compile_definitions(${PROJECT_NAME} PRIVATE GFX_VK_ICD_PATH="${GFX_VK_ICD_MANIFEST0}")
        message(STATUS "Bundled MoltenVK ICD: ${GFX_VK_ICD_MANIFEST0}")
    else()
        message(WARNING "molten-vk ICD manifest not found under ${GFX_VK_SHARE_DIR}.")
    endif()
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
        glslang::glslang
        glslang::MachineIndependent
        glslang::OSDependent
        glslang::SPIRV
        glslang::glslang-default-resource-limits
)

target_link_libraries(${PROJECT_NAME} PRIVATE spirv-cross-core)
target_link_libraries(${PROJECT_NAME} PRIVATE spirv-cross-glsl)
target_link_libraries(${PROJECT_NAME} PRIVATE slang::slang)
target_link_libraries(${PROJECT_NAME} PRIVATE reflectcpp::reflectcpp)

target_link_libraries(${PROJECT_NAME} PRIVATE ${TIFF_LIBRARIES} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES} ${PNG_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${JASPER_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE asio::asio)

target_link_libraries(${PROJECT_NAME} PRIVATE KTX::ktx)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenImageIO::OpenImageIO)

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GLX)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--allow-multiple-definition)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE glad::glad)

# std::stacktrace support (GCC libstdc++). fail()/warn() in builder.h capture a
# stacktrace; the symbols live in libstdc++exp. PUBLIC so consumers that
# instantiate the header-inline diagnostics also link it.
target_link_libraries(${PROJECT_NAME} PUBLIC $<$<CXX_COMPILER_ID:GNU>:stdc++exp>)

# The source tree is a *fallback* for locating shaders and assets, used only when they were not
# found beside the installed library (see src/core/paths.cpp). Baking it in is right for a dev build
# and wrong for a shipped one: it is an absolute path on the machine that compiled the binary, which
# on a user's machine names nothing. Release/CI builds turn it off, and then the only way gfx finds
# its data is the install layout — which is exactly what we want to be testing.
option(GFX_EMBED_SOURCE_PATHS
       "Bake the source-tree shader/asset paths in as a development fallback (OFF for distribution)" ON)

if(GFX_EMBED_SOURCE_PATHS)
    set(GFX_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets/")
    set(GFX_SHADERS_PATH "${CMAKE_SOURCE_DIR}/shaders/")
else()
    set(GFX_ASSETS_PATH "")
    set(GFX_SHADERS_PATH "")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
        ASSETS_PATH="${GFX_ASSETS_PATH}"
        SHADERS_PATH="${GFX_SHADERS_PATH}"
        PROJECT_ROOT="${CMAKE_SOURCE_DIR}/"
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
)

file(GLOB_RECURSE RUNTIME_SOURCE_FILES
        src/main.cpp
        src/engine.cpp
        src/lab/*.cpp
        src/project/*.cpp
)

add_executable(Gfx_Runtime ${RUNTIME_SOURCE_FILES})

target_include_directories(Gfx_Runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)

target_link_libraries(Gfx_Runtime PRIVATE ${PROJECT_NAME})

# Gfx_Runtime includes public headers from GFX that expose these deps directly
target_link_libraries(Gfx_Runtime PRIVATE
        imgui::imgui
        glm::glm
        spdlog::spdlog
        Vulkan::Vulkan
        glfw
)

string(REPLACE "\\" "/" VCPKG_TOOLCHAIN_FWD "${CMAKE_TOOLCHAIN_FILE}")
target_compile_definitions(Gfx_Runtime PRIVATE
        PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
        VCPKG_ROOT="${VCPKG_TOOLCHAIN_FWD}"
        ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
        SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/")

# ---------------------------------------------------------------------------
# Export & install — generates GFX_RELOADEDConfig.cmake into cmake/
# ---------------------------------------------------------------------------

set_target_properties(Gfx_Runtime PROPERTIES EXPORT_NAME Gfx_Runtime)

# Where the loader looks for the bundled dependencies below. Without this, an installed binary
# resolves libslang/libvulkan from the *build* machine's vcpkg tree (CMake strips the build RUNPATH
# on install), so it links here and fails to start anywhere else. $ORIGIN is resolved by the loader
# at load time, which is what keeps the archive relocatable.
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "@loader_path")
    set_target_properties(Gfx_Runtime PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN")
    set_target_properties(Gfx_Runtime PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

# Declare the export set via install() — export(EXPORT) requires this to exist
install(TARGETS ${PROJECT_NAME} Gfx_Runtime
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# The public headers. Without these the installed library is unusable as an SDK.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

# api.h is generated (generate_export_header), so it lives in the build tree, not include/ — and
# every public header includes it. Miss it and the installed SDK fails on the consumer's first
# #include, which is exactly how this was found.
install(FILES ${CMAKE_BINARY_DIR}/include/api.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# gfx's own shaders and assets, into the layout src/core/paths.cpp looks for
# (<libdir>/../share/GFX_RELOADED/<kind>). Shipping the binary without these gives a library that
# builds and then cannot find a single shader at runtime.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/GFX_RELOADED/shaders)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/GFX_RELOADED/assets)

# ---------------------------------------------------------------------------
# Bundled runtime dependencies
#
# Most of gfx's dependencies are static, but not all: slang and the Vulkan loader are shared
# objects living in the build machine's vcpkg tree. CMake strips the build RUNPATH on install, so
# an installed binary asks the loader for "libslang-compiler.so" with nowhere to find it — it
# neither links nor starts on any machine but the one that built it.
#
# The closure is computed at install time rather than listed by hand: the list differs per platform
# (DLLs on Windows, dylibs on macOS) and per vcpkg feature set, and a hand-written one is a list
# that silently goes stale. Anything resolving to a system path is *excluded* — libX11, libGL and
# libstdc++ are the user's, and shipping our own copies is how you break their machine.
# ---------------------------------------------------------------------------
# Where the bundled libraries have to land differs by platform, and getting it wrong produces a
# package that installs cleanly and then cannot start. The Windows loader searches the directory of
# the *executable* and never a sibling lib/, so DLLs go next to Gfx_Runtime; ELF/Mach-O resolve via
# the RPATH set above, so they go beside the library that names them.
if(WIN32)
    set(GFX_DEP_DESTDIR ${CMAKE_INSTALL_BINDIR})
else()
    set(GFX_DEP_DESTDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(CODE "
    set(GFX_DEP_SEARCH_DIRS \"${GFX_VCPKG_PREFIX}/lib\" \"${GFX_VCPKG_PREFIX}/bin\")
    set(GFX_DEP_DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${GFX_DEP_DESTDIR}\")
")
install(CODE [[
    file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES "$<TARGET_FILE:GFX_RELOADED>"
            EXECUTABLES "$<TARGET_FILE:Gfx_Runtime>"
            RESOLVED_DEPENDENCIES_VAR GFX_RESOLVED_DEPS
            UNRESOLVED_DEPENDENCIES_VAR GFX_UNRESOLVED_DEPS
            DIRECTORIES ${GFX_DEP_SEARCH_DIRS}
            PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
            POST_EXCLUDE_REGEXES
                "^/(usr|lib|lib32|lib64)/"          # Linux system libraries
                "^/System/Library/" "^/usr/lib/"    # macOS system libraries
                "[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\]" # Windows system DLLs
    )

    # Gfx_Runtime links libGFX_RELOADED, so gfx resolves as a dependency of itself. install(TARGETS)
    # has already placed it — with its RPATH rewritten to $ORIGIN — and copying the build-tree
    # binary over that would restore the build machine's RUNPATH and undo the relocatability. (It
    # currently survives only because file(INSTALL) skips a newer destination, which is a timestamp
    # race, not a guarantee.) Ours is not a dependency to bundle; it *is* the thing being installed.
    set(GFX_OWN_BINARIES "$<TARGET_FILE_NAME:GFX_RELOADED>" "$<TARGET_FILE_NAME:Gfx_Runtime>")

    foreach(dep IN LISTS GFX_RESOLVED_DEPS)
        get_filename_component(depName "${dep}" NAME)
        if(depName IN_LIST GFX_OWN_BINARIES)
            continue()
        endif()
        message(STATUS "Bundling runtime dependency: ${depName}")
        file(INSTALL
                DESTINATION "${GFX_DEP_DESTINATION}"
                TYPE SHARED_LIBRARY
                FOLLOW_SYMLINK_CHAIN
                FILES "${dep}")
    endforeach()

    # Unresolved is the normal case for system libraries the loader will supply. It is only a
    # problem if something we *built against* shows up here, so say what they were.
    foreach(dep IN LISTS GFX_UNRESOLVED_DEPS)
        message(STATUS "Leaving to the system loader: ${dep}")
    endforeach()
]])

# ---------------------------------------------------------------------------
# Vendored public dependencies
#
# gfx's public headers expose three third-party libraries: glm (in ~20 headers — vectors and
# matrices are part of the API's vocabulary), spdlog (log.h, whose inline templates take
# spdlog::format_string_t) and imgui (gui.h). A consumer therefore cannot compile against gfx
# without their headers, nor link against it without their objects — spdlog and imgui are static
# libs whose symbols do not escape libGFX_RELOADED.
#
# Rather than making that the consumer's problem (they would need vcpkg with matching versions,
# which for a launcher-generated project is an absurd amount of setup), the SDK *ships* them. A
# project needs nothing but the downloaded archive.
#
# This is a property of the package, not a promise to the user: consumers link only
# GFX_RELOADED::GFX_RELOADED. When these types stop appearing in gfx's public headers, delete this
# block and the vendored copies simply stop shipping — no consumer CMake changes.
# ---------------------------------------------------------------------------
option(GFX_VENDOR_PUBLIC_DEPS
       "Ship glm/spdlog/fmt/imgui inside the SDK so consumers need no package manager" ON)

if(GFX_VENDOR_PUBLIC_DEPS AND GFX_VCPKG_PREFIX)
    set(GFX_VENDOR_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/gfx-vendor)
    set(GFX_VENDOR_LIBDIR ${CMAKE_INSTALL_LIBDIR}/gfx-vendor)

    # Headers. glm and fmt are header-only from the consumer's point of view; fmt comes along
    # because spdlog's headers include it.
    install(DIRECTORY
            "${GFX_VCPKG_PREFIX}/include/glm"
            "${GFX_VCPKG_PREFIX}/include/spdlog"
            "${GFX_VCPKG_PREFIX}/include/fmt"
            DESTINATION ${GFX_VENDOR_INCLUDEDIR})

    # imgui's headers are loose files at the top of include/, not a directory.
    file(GLOB GFX_VENDOR_IMGUI_HEADERS
            "${GFX_VCPKG_PREFIX}/include/imgui*.h"
            "${GFX_VCPKG_PREFIX}/include/imconfig.h"
            "${GFX_VCPKG_PREFIX}/include/imstb_*.h")
    install(FILES ${GFX_VENDOR_IMGUI_HEADERS} DESTINATION ${GFX_VENDOR_INCLUDEDIR})

    # The static archives the consumer must actually link. Order matters for GNU ld: a static lib
    # only resolves symbols demanded by something *earlier* on the command line, and spdlog's
    # headers call into fmt — so spdlog precedes fmt. The Config.cmake below preserves this order.
    set(GFX_VENDOR_LIBS "")
    foreach(dep spdlog fmt imgui)
        file(GLOB found "${GFX_VCPKG_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${dep}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        if(found)
            list(APPEND GFX_VENDOR_LIBS ${found})
        else()
            message(WARNING "Vendoring: no static ${dep} under ${GFX_VCPKG_PREFIX}/lib — "
                            "the installed SDK will not link.")
        endif()
    endforeach()
    install(FILES ${GFX_VENDOR_LIBS} DESTINATION ${GFX_VENDOR_LIBDIR})
endif()

# The CMake package, so consumers can find_package(GFX_RELOADED).
install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE GFX_RELOADED::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

# The install-tree config. It attaches the vendored dependencies to the gfx target's *interface*,
# so `target_link_libraries(app PRIVATE GFX_RELOADED::GFX_RELOADED)` is the whole of what a
# consumer writes. Paths are derived from the config file's own location, never baked in, so the
# archive can be unpacked anywhere.
set(GFX_INSTALL_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/install/${PROJECT_NAME}Config.cmake")
file(WRITE "${GFX_INSTALL_CONFIG}"
"include(CMakeFindDependencyMacro)
include(\"\${CMAKE_CURRENT_LIST_DIR}/${PROJECT_NAME}Targets.cmake\")

# lib/cmake/GFX_RELOADED/ -> the install prefix
get_filename_component(GFX_RELOADED_PREFIX \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)
")

if(GFX_VENDOR_PUBLIC_DEPS AND GFX_VCPKG_PREFIX)
    # Names only: the paths are rebuilt relative to the unpacked prefix at find_package() time.
    set(GFX_VENDOR_LIB_NAMES "")
    foreach(lib ${GFX_VENDOR_LIBS})
        get_filename_component(name "${lib}" NAME)
        list(APPEND GFX_VENDOR_LIB_NAMES "${name}")
    endforeach()

    file(APPEND "${GFX_INSTALL_CONFIG}"
"
# gfx's public headers expose glm, spdlog and imgui, so the SDK ships them; see GFX_VENDOR_PUBLIC_DEPS
# in the top-level CMakeLists. Consumers do not name these — they are pulled in by linking gfx.
if(NOT TARGET GFX_RELOADED::vendored)
    add_library(GFX_RELOADED::vendored INTERFACE IMPORTED)
    set_target_properties(GFX_RELOADED::vendored PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES \"\${GFX_RELOADED_PREFIX}/${GFX_VENDOR_INCLUDEDIR}\")

    set(GFX_RELOADED_VENDORED_LIBS \"\")
    foreach(name ${GFX_VENDOR_LIB_NAMES})
        list(APPEND GFX_RELOADED_VENDORED_LIBS \"\${GFX_RELOADED_PREFIX}/${GFX_VENDOR_LIBDIR}/\${name}\")
    endforeach()
    if(GFX_RELOADED_VENDORED_LIBS)
        set_target_properties(GFX_RELOADED::vendored PROPERTIES
                INTERFACE_LINK_LIBRARIES \"\${GFX_RELOADED_VENDORED_LIBS}\")
    endif()
endif()

# APPEND rather than target_link_libraries: the gfx target is IMPORTED here.
set_property(TARGET GFX_RELOADED::${PROJECT_NAME} APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES GFX_RELOADED::vendored)
")
endif()

install(FILES
        "${GFX_INSTALL_CONFIG}"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# Write config files directly at configure time (no build step needed).
# Each build configuration exports into its own subdirectory so that single-config
# generators (Ninja) don't clobber each other's exports in a shared folder. A
# consumer then selects the subdir matching its own build type, giving
# debug->debug / release->release linkage.
set(GFX_CMAKE_OUTPUT_BASE_DIR "${CMAKE_SOURCE_DIR}/cmake" CACHE PATH "Base directory where per-config GFX_RELOADED cmake config files are written")
set(GFX_CMAKE_OUTPUT_DIR "${GFX_CMAKE_OUTPUT_BASE_DIR}/${CMAKE_BUILD_TYPE}")
file(MAKE_DIRECTORY "${GFX_CMAKE_OUTPUT_DIR}")

# Export build-tree targets directly into the output dir
export(EXPORT ${PROJECT_NAME}Targets
        FILE "${GFX_CMAKE_OUTPUT_DIR}/GFX_RELOADEDTargets.cmake"
        NAMESPACE GFX_RELOADED::)

# Config file that pulls in the targets
file(WRITE "${GFX_CMAKE_OUTPUT_DIR}/GFX_RELOADEDConfig.cmake"
        "include(CMakeFindDependencyMacro)
include(\"\${CMAKE_CURRENT_LIST_DIR}/GFX_RELOADEDTargets.cmake\")
")

# Re-export after each build to keep target file paths up to date
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DEXPORT_SET=${PROJECT_NAME}Targets
            -DOUTPUT_FILE="${GFX_CMAKE_OUTPUT_DIR}/GFX_RELOADEDTargets.cmake"
            -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_export_targets.cmake"
        COMMENT "Refreshing GFX_RELOADED cmake config in ${GFX_CMAKE_OUTPUT_DIR}"
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_export_targets.cmake"
        "include(\"${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Export/${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/${PROJECT_NAME}Targets.cmake\" OPTIONAL)
")

# ---------------------------------------------------------------------------
# Unit tests (gtest). Enable with -DGFX_BUILD_TESTS=ON (default ON).
# ---------------------------------------------------------------------------
option(GFX_BUILD_TESTS "Build the GFX unit test suite" ON)
if(GFX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
