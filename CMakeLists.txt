cmake_minimum_required(VERSION 3.30)

# The `import std` experimental gate UUID is bumped between CMake releases. Pick the one matching
# the active cmake so both the CLI (4.3.x) and CLion's bundled toolchain (4.2.x) can configure.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.3)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
else()
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
endif()

project(GFX_RELOADED VERSION 0.0.1 LANGUAGES C CXX)
include(GenerateExportHeader)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C++23 named modules + `import std`. Scanning is required so the build orders BMI generation;
# CXX_MODULE_STD makes the standard-library module available to any cxx_std_23 target.
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_MODULE_STD ON)

# Installed binaries must find the co-installed framework shared lib (bin/ -> ../lib) without
# the user setting LD_LIBRARY_PATH/DYLD. No-op on Windows, which uses PATH/same-dir lookup.
if (APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR};@executable_path/../${CMAKE_INSTALL_LIBDIR}")
elseif (UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR};$ORIGIN")
endif()

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
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

find_package(Ktx CONFIG REQUIRED)
#find_package(directxtex CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

find_package(imgui CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)



file (GLOB_RECURSE SOURCE_FILES
        src/core/*.cpp
        src/io/*.cpp
        src/backends/*.cpp
)

file(GLOB_RECURSE PUBLIC_MODULE_FILES
        include/*.ixx
        src/executor/*.ixx
)

file(GLOB_RECURSE PRIVATE_MODULE_FILES
        src/backends/*.ixx
        src/core/*.ixx
)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

target_sources(${PROJECT_NAME}
    PUBLIC  FILE_SET gfx_public_modules  TYPE CXX_MODULES FILES ${PUBLIC_MODULE_FILES}
    PRIVATE FILE_SET gfx_private_modules TYPE CXX_MODULES FILES ${PRIVATE_MODULE_FILES}
)

target_link_options(${PROJECT_NAME} PRIVATE
    "LINKER:--allow-multiple-definition"
)

generate_export_header(${PROJECT_NAME}
        BASE_NAME GFX
        EXPORT_MACRO_NAME GFX_API
        EXPORT_FILE_NAME include/api.h
)

target_compile_definitions(${PROJECT_NAME}
        PRIVATE GFX_RELOADED_EXPORTS
        INTERFACE GFX_RELOADED_IMPORTS)

if (UNIX AND NOT APPLE)
    set(GLFW_USE_WAYLAND ON)
endif()

# Public usage requirements: glm and imgui appear in GFX_RELOADED's public headers, and spdlog
# is deliberately re-exported so downstream projects can log through the framework's stack.
# These propagate to anything that links GFX_RELOADED::GFX_RELOADED.
target_link_libraries(${PROJECT_NAME} PUBLIC
        glm::glm
        imgui::imgui
        spdlog::spdlog)

# Implementation details. Because GFX_RELOADED is a SHARED library these are fully resolved into
# the .dll and are NOT propagated to consumers (so projects don't need these vcpkg ports).
target_link_libraries(${PROJECT_NAME} PRIVATE
        glfw
        magic_enum::magic_enum
        GLEW::GLEW OpenGL::GL
        assimp::assimp
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        glslang::glslang
        glslang::MachineIndependent
        glslang::OSDependent
        glslang::SPIRV
        glslang::glslang-default-resource-limits
        spirv-cross-core
        spirv-cross-glsl
        reflectcpp::reflectcpp
        ${TIFF_LIBRARIES} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES} ${PNG_LIBRARIES}
        ${JASPER_LIBRARIES}
        KTX::ktx
        #Microsoft::DirectXTex
        OpenImageIO::OpenImageIO
        glad::glad)

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GLX)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/"
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}/"
)

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

file (GLOB_RECURSE RUNTIME_SOURCE_FILES
        src/main.cpp
        src/engine.cpp
        src/lab/*.cpp
        src/project/*.cpp
)

add_executable(Gfx_Runtime ${RUNTIME_SOURCE_FILES})

target_include_directories(Gfx_Runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
# link the GFX_RELOADED dll
target_link_libraries(Gfx_Runtime PRIVATE ${PROJECT_NAME})

# Namespaced aliases so an in-tree super-build (e.g. the Hub added via add_subdirectory when
# packaging) links the same GFX_RELOADED::* names that installed/exported consumers use.
add_library(GFX_RELOADED::GFX_RELOADED ALIAS ${PROJECT_NAME})
add_executable(GFX_RELOADED::Gfx_Runtime ALIAS Gfx_Runtime)

string(REPLACE "\\" "/" VCPKG_TOOLCHAIN_FWD "${CMAKE_TOOLCHAIN_FILE}")
target_compile_definitions(Gfx_Runtime PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    VCPKG_ROOT="${VCPKG_TOOLCHAIN_FWD}"
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/")

include(CMakePackageConfigHelpers)

set(GFX_CMAKE_CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GFX_RELOADED")

write_basic_package_version_file(
        "${CMAKE_BINARY_DIR}/GFX_RELOADEDConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

# ---------------------------------------------------------------------------
# Build-tree package export.
# Lets external projects consume the framework as a CMake target without an
# install step:
#   find_package(GFX_RELOADED CONFIG REQUIRED
#       PATHS "<checkout>/cmake-build-debug")
#   target_link_libraries(<proj> PRIVATE GFX_RELOADED::GFX_RELOADED)
# The exported target carries the public include dirs (BUILD_INTERFACE) plus
# the glm/imgui/spdlog usage requirements. Paths point into this build tree,
# so the package is valid on this machine/build only.
# ---------------------------------------------------------------------------
export(TARGETS ${PROJECT_NAME} Gfx_Runtime
        NAMESPACE GFX_RELOADED::
        FILE "${CMAKE_BINARY_DIR}/GFX_RELOADEDTargets.cmake")

# @PACKAGE_INIT@ expands only under configure_package_config_file (install case);
# a plain configure_file leaves it empty, which is correct for the build tree.
configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/GFX_RELOADEDConfig.cmake.in"
        "${CMAKE_BINARY_DIR}/GFX_RELOADEDConfig.cmake"
        @ONLY)

# ---------------------------------------------------------------------------
# Installed SDK.
# Produces a relocatable prefix that a consumer finds via the CMake package
# registry (see Phase 3) — no env var, no vcpkg path to the framework:
#   find_package(GFX_RELOADED CONFIG REQUIRED)
#   target_link_libraries(<proj> PRIVATE GFX_RELOADED::GFX_RELOADED)
# The public glm/imgui/spdlog usage requirements still resolve through the
# consumer's vcpkg toolchain (pinned to the same baseline this SDK was built
# against), so user projects can add extra ports (e.g. imguizmo) freely.
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} Gfx_Runtime
        EXPORT GFX_RELOADEDTargets
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILE_SET gfx_public_modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Public headers: the hand-written API in include/ plus the generated export header.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(FILES "${CMAKE_BINARY_DIR}/include/api.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Runtime data the framework/runtime load relative to the install prefix (see Phase 2).
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/gfx/assets)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/gfx/shaders)

install(EXPORT GFX_RELOADEDTargets
        NAMESPACE GFX_RELOADED::
        DESTINATION ${GFX_CMAKE_CONFIG_DESTINATION})

configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/GFX_RELOADEDConfig.cmake.in"
        "${CMAKE_BINARY_DIR}/install/GFX_RELOADEDConfig.cmake"
        INSTALL_DESTINATION ${GFX_CMAKE_CONFIG_DESTINATION})

install(FILES
        "${CMAKE_BINARY_DIR}/install/GFX_RELOADEDConfig.cmake"
        "${CMAKE_BINARY_DIR}/GFX_RELOADEDConfigVersion.cmake"
        DESTINATION ${GFX_CMAKE_CONFIG_DESTINATION})

# Stage the framework's private vcpkg dependencies (assimp/glfw/vulkan/...) next to the
# installed binaries so the runtime is self-contained. Off by default: resolution is slow and
# noisy, and proper system-lib excludes are tuned in the packaging phase (Phase 5). Consumers
# linking GFX_RELOADED don't need this — these libs are sealed inside the shared lib.
option(GFX_BUNDLE_RUNTIME_DEPS "Install the framework's private runtime dependencies into the SDK" OFF)
if (GFX_BUNDLE_RUNTIME_DEPS)
    install(TARGETS ${PROJECT_NAME} Gfx_Runtime
            RUNTIME_DEPENDENCY_SET gfx_runtime_deps
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(RUNTIME_DEPENDENCY_SET gfx_runtime_deps
            PRE_EXCLUDE_REGEXES
                "api-ms-" "ext-ms-" "^hvsifiletrust" "^pdmutilities"   # Windows API sets
            POST_EXCLUDE_REGEXES
                "[Ss]ystem32" "^/lib" "^/usr/lib"                      # OS-provided libraries
            DIRECTORIES $<TARGET_FILE_DIR:${PROJECT_NAME}>
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Register the installed SDK in the per-user CMake package registry so a bare
# find_package(GFX_RELOADED) resolves it with no CMAKE_PREFIX_PATH / env var. Runs during
# `cmake --install`. Native installers (Phase 5) build packages with this OFF and run the same
# RegisterCMakePackage.cmake from a post-install step on the END user's machine instead — an
# install(CODE) registry write would otherwise land on the packaging machine, not the user's.
option(GFX_REGISTER_PACKAGE "Register the installed SDK in the CMake user package registry at install time" ON)
if(GFX_REGISTER_PACKAGE)
    install(CODE "
        set(PACKAGE_NAME GFX_RELOADED)
        set(CONFIG_DIR \"\${CMAKE_INSTALL_PREFIX}/${GFX_CMAKE_CONFIG_DESTINATION}\")
        include(\"${CMAKE_SOURCE_DIR}/cmake/RegisterCMakePackage.cmake\")
    ")
endif()

# Optional: register THIS build tree for local framework development. Off by default — it would
# otherwise shadow an installed SDK in find_package. Enable when iterating on the framework.
option(GFX_EXPORT_BUILD_TREE_PACKAGE "Register the build tree in the CMake user package registry" OFF)
if(GFX_EXPORT_BUILD_TREE_PACKAGE)
    cmake_policy(SET CMP0090 NEW)
    set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
    export(PACKAGE GFX_RELOADED)
endif()

# ---------------------------------------------------------------------------
# Packaging (CPack). Active only when GFX_RELOADED is the top-level project, so a consumer that
# add_subdirectory()'s the framework doesn't inherit its packaging.
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    # Optionally fold the Hub launcher into the same package (super-build). The Hub binds to the
    # in-tree GFX_RELOADED::* targets, so it performs no registry/checkout lookup here; its
    # install rules (bin/ + share/gfx-hub) then contribute to the SDK package.
    option(GFX_BUILD_HUB "Build and package the Hub launcher with the SDK" OFF)
    set(GFX_HUB_SOURCE_DIR "" CACHE PATH "Path to the Hub source tree (used when GFX_BUILD_HUB is ON)")
    if(GFX_BUILD_HUB)
        if(NOT GFX_HUB_SOURCE_DIR)
            message(FATAL_ERROR "GFX_BUILD_HUB is ON but GFX_HUB_SOURCE_DIR is not set.")
        endif()
        add_subdirectory("${GFX_HUB_SOURCE_DIR}" hub)
    endif()

    set(CPACK_PACKAGE_NAME "GfxReloaded")
    set(CPACK_PACKAGE_VENDOR "Gfx Reloaded")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Gfx Reloaded SDK: framework, runtime and project Hub")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "GfxReloaded")

    # Per-OS generators. Native installers (WiX/NSIS/DragNDrop/DEB) need their tooling and are
    # produced in CI; ZIP/TGZ always work and yield a relocatable archive of the SDK. Each native
    # installer registers the SDK in the user's CMake package registry from a post-install step,
    # so packages are built with -DGFX_REGISTER_PACKAGE=OFF (the install(CODE) path would
    # otherwise register the packaging machine instead of the end user's).
    if(WIN32)
        set(CPACK_GENERATOR "ZIP" CACHE STRING "CPack generators (add WIX / NSIS in CI)")
        set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
            "WriteRegStr HKCU \\\"Software\\\\Kitware\\\\CMake\\\\Packages\\\\GFX_RELOADED\\\" \\\"gfxsdk\\\" \\\"$INSTDIR\\\\${CMAKE_INSTALL_LIBDIR}\\\\cmake\\\\GFX_RELOADED\\\"")
        set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
            "DeleteRegValue HKCU \\\"Software\\\\Kitware\\\\CMake\\\\Packages\\\\GFX_RELOADED\\\" \\\"gfxsdk\\\"")
    elseif(APPLE)
        set(CPACK_GENERATOR "DragNDrop;TGZ" CACHE STRING "CPack generators")
    else()
        set(CPACK_GENERATOR "TGZ" CACHE STRING "CPack generators (add DEB in CI)")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "radue")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    endif()

    include(CPack)
endif()