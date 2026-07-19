cmake_minimum_required(VERSION 3.28)

# vcpkg-overlay-ports/kubazip renames the mz_* symbol namespace of kubazip's vendored copy of
# miniz.h, so it no longer collides with minizip-ng (pulled in transitively via OpenColorIO) at
# link time. Must be set before project() -- that is when the vcpkg toolchain reads it.
list(APPEND VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-overlay-ports")

project(Koral VERSION 0.0.4 LANGUAGES C CXX)
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

# Oldest macOS a shipped binary must run on. Without this the deployment target floats to whatever
# the build machine happens to be, so a release built on a freshly updated Mac silently refuses to
# launch on anything older. 12.0 is the floor for Apple Silicon in practice: 11.0 was the first
# arm64 release, but MoltenVK's own minimum is 12.0.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

# The x86-64 instruction-set floor for shipped binaries.
#
# This exists because a release built on a machine running CachyOS's znver4 package set picked up
# AVX-512 it never asked for: our own code compiles at GCC's default x86-64 baseline, but the
# *prebuilt static* libstdc++ archives on such a system are themselves -march=znver4, and linking
# them (stdc++exp, for std::stacktrace) drags unguarded AVX-512 into std::to_chars and friends.
# The binary then dies with SIGILL on anything without AVX-512 — which is every Intel CPU a normal
# person owns.
#
# Stating the floor here does not fix that on its own (the fix is building in a container with a
# stock toolchain — see docker/linux-release.Dockerfile), but it makes the contract explicit and
# stops a future -march=native or an optimised distro from quietly raising it.
#
# x86-64-v2 is SSE4.2/POPCNT — Nehalem (2008) and later, so it comfortably covers the Intel 8th gen
# floor including the low-end Coffee Lake/Gemini Lake parts that have no AVX2. Nothing here is
# CPU-SIMD bound (the work is on the GPU), so a higher level would buy nothing measurable while
# excluding real hardware.
option(KORAL_SET_ISA_BASELINE "Pin the x86-64 instruction-set baseline for portable binaries" ON)

if(KORAL_SET_ISA_BASELINE AND NOT APPLE)
    include(CheckCXXCompilerFlag)
    if(MSVC)
        # MSVC has no x86-64-v2 switch. Its default is already SSE2-only, and /arch:AVX or higher
        # is what would break us, so the correct action is simply not to raise it.
        set(KORAL_ISA_FLAG "")
    else()
        check_cxx_compiler_flag("-march=x86-64-v2" KORAL_HAS_MARCH_V2)
        if(KORAL_HAS_MARCH_V2)
            set(KORAL_ISA_FLAG "-march=x86-64-v2;-mtune=generic")
        endif()
    endif()
    if(KORAL_ISA_FLAG)
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${KORAL_ISA_FLAG}>")
        message(STATUS "ISA baseline: x86-64-v2 (SSE4.2/POPCNT, Nehalem and later)")
    endif()
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
    set(KORAL_VCPKG_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(EXISTS "${KORAL_VCPKG_PREFIX}/include/vulkan/vulkan.h")
        set(Vulkan_INCLUDE_DIR "${KORAL_VCPKG_PREFIX}/include" CACHE PATH "vcpkg Vulkan headers" FORCE)
        # Loader lib: release tree serves both configs (the C loader is config-agnostic
        # and its debug/release copies are ABI-identical). Drop any stale cached path
        # (e.g. a prior /usr/lib pick) so the vcpkg-only search below actually applies.
        unset(Vulkan_LIBRARY CACHE)
        find_library(Vulkan_LIBRARY NAMES vulkan vulkan-1
                PATHS "${KORAL_VCPKG_PREFIX}/lib" NO_DEFAULT_PATH)
    endif()
endif()

find_package(Vulkan REQUIRED)
message(STATUS "Using Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Using Vulkan loader:  ${Vulkan_LIBRARIES}")

# ---------------------------------------------------------------------------
# Runtime Vulkan loader.
#
# We link and ship the vcpkg-built Vulkan-Loader. The manifest requests its WSI
# features (Wayland + XCB + Xlib on Linux, Win32 is always built; macOS needs no
# loader feature — MoltenVK supplies its own Metal surface extension as an ICD),
# so this one loader creates surfaces on every platform and still discovers the
# user's installed ICDs/drivers and layers through the standard search paths +
# env vars. No system loader or LD_LIBRARY_PATH override needed.
#
# Bundled layer manifest dir (from vcpkg) is handed to the runtime as a compile
# definition below so it can add it to the loader's search paths at startup —
# this makes validation available with no system SDK.
# ---------------------------------------------------------------------------
set(KORAL_VK_SHARE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

# MoltenVK has no vcpkg port — it has never existed in the vcpkg registry (checked the
# full port list and history), which is what produced the "baseline does not contain an
# entry for port molten-vk" configure error this replaces. Homebrew is the source of
# truth instead: its formula ships the dylib alongside an ICD manifest whose library_path
# is already relative to itself, so both files can be copied as a pair with no rewriting.
if(APPLE)
    find_program(KORAL_BREW_EXECUTABLE brew)
    if(KORAL_BREW_EXECUTABLE)
        execute_process(
                COMMAND "${KORAL_BREW_EXECUTABLE}" --prefix molten-vk
                OUTPUT_VARIABLE KORAL_MOLTENVK_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE KORAL_BREW_PREFIX_RESULT)
    endif()

    set(KORAL_MOLTENVK_DYLIB "KORAL_MOLTENVK_DYLIB-NOTFOUND")
    set(KORAL_MOLTENVK_ICD_JSON "KORAL_MOLTENVK_ICD_JSON-NOTFOUND")
    if(KORAL_MOLTENVK_PREFIX AND KORAL_BREW_PREFIX_RESULT EQUAL 0)
        find_file(KORAL_MOLTENVK_DYLIB NAMES libMoltenVK.dylib
                PATHS "${KORAL_MOLTENVK_PREFIX}/lib" NO_DEFAULT_PATH)
        find_file(KORAL_MOLTENVK_ICD_JSON NAMES MoltenVK_icd.json
                PATHS "${KORAL_MOLTENVK_PREFIX}/etc/vulkan/icd.d" NO_DEFAULT_PATH)
    endif()

    if(KORAL_MOLTENVK_DYLIB AND KORAL_MOLTENVK_ICD_JSON)
        message(STATUS "Found MoltenVK via Homebrew: ${KORAL_MOLTENVK_DYLIB}")
    else()
        message(WARNING "MoltenVK not found ('brew --prefix molten-vk' failed or its files are "
                "missing). Vulkan will have no driver on macOS until you run "
                "'brew install molten-vk' and reconfigure.")
    endif()
endif()

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
        BASE_NAME KORAL
        EXPORT_MACRO_NAME KORAL_API
        EXPORT_FILE_NAME include/api.h
)

target_compile_definitions(${PROJECT_NAME}
        PRIVATE KORAL_RELOADED_EXPORTS
        INTERFACE KORAL_RELOADED_IMPORTS)

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
file(GLOB_RECURSE KORAL_VK_LAYER_MANIFEST
        "${KORAL_VK_SHARE_DIR}/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
        "${KORAL_VK_SHARE_DIR}/bin/VkLayer_khronos_validation.json")
if(KORAL_VK_LAYER_MANIFEST)
    list(GET KORAL_VK_LAYER_MANIFEST 0 KORAL_VK_LAYER_MANIFEST0)
    get_filename_component(KORAL_VK_LAYER_DIR "${KORAL_VK_LAYER_MANIFEST0}" DIRECTORY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE KORAL_VK_LAYER_PATH="${KORAL_VK_LAYER_DIR}")
    message(STATUS "Bundled Vulkan validation layers: ${KORAL_VK_LAYER_DIR}")
else()
    message(WARNING "vcpkg validation-layer manifest not found under ${KORAL_VK_SHARE_DIR}; "
            "portable validation will be unavailable (loader still finds a system SDK if present).")
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
# on a user's machine names nothing. Release/CI builds turn it off, and then the only way Koral finds
# its data is the install layout — which is exactly what we want to be testing.
option(KORAL_EMBED_SOURCE_PATHS
       "Bake the source-tree shader/asset paths in as a development fallback (OFF for distribution)" ON)

if(KORAL_EMBED_SOURCE_PATHS)
    set(KORAL_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets/")
    set(KORAL_SHADERS_PATH "${CMAKE_SOURCE_DIR}/shaders/")
else()
    set(KORAL_ASSETS_PATH "")
    set(KORAL_SHADERS_PATH "")
endif()

# Same dev-fallback deal as assets/shaders above, for the Homebrew MoltenVK ICD manifest
# found earlier: an absolute path into this machine's Homebrew cellar, so it's only baked
# in for dev builds. A distribution build instead relies on the copy installed alongside
# Koral (see the "Bundle MoltenVK" install step below), found at runtime via paths.cpp.
if(APPLE)
    if(KORAL_EMBED_SOURCE_PATHS AND KORAL_MOLTENVK_ICD_JSON)
        get_filename_component(KORAL_VK_ICD_DEV_PATH "${KORAL_MOLTENVK_ICD_JSON}" DIRECTORY)
    else()
        set(KORAL_VK_ICD_DEV_PATH "")
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE KORAL_VK_ICD_DEV_PATH="${KORAL_VK_ICD_DEV_PATH}")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
        ASSETS_PATH="${KORAL_ASSETS_PATH}"
        SHADERS_PATH="${KORAL_SHADERS_PATH}"
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

add_executable(Koral_Runtime ${RUNTIME_SOURCE_FILES})

target_include_directories(Koral_Runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)

target_link_libraries(Koral_Runtime PRIVATE ${PROJECT_NAME})

# Koral_Runtime includes public headers from GFX that expose these deps directly
target_link_libraries(Koral_Runtime PRIVATE
        imgui::imgui
        glm::glm
        spdlog::spdlog
        Vulkan::Vulkan
        glfw
)

string(REPLACE "\\" "/" VCPKG_TOOLCHAIN_FWD "${CMAKE_TOOLCHAIN_FILE}")
target_compile_definitions(Koral_Runtime PRIVATE
        PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
        VCPKG_ROOT="${VCPKG_TOOLCHAIN_FWD}"
        ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
        SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/")

# ---------------------------------------------------------------------------
# Export & install — generates KORAL_RELOADEDConfig.cmake into cmake/
# ---------------------------------------------------------------------------

set_target_properties(Koral_Runtime PROPERTIES EXPORT_NAME Koral_Runtime)

# Where the loader looks for the bundled dependencies below. Without this, an installed binary
# resolves libslang/libvulkan from the *build* machine's vcpkg tree (CMake strips the build RUNPATH
# on install), so it links here and fails to start anywhere else. $ORIGIN is resolved by the loader
# at load time, which is what keeps the archive relocatable.
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "@loader_path")
    set_target_properties(Koral_Runtime PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN")
    set_target_properties(Koral_Runtime PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

# Declare the export set via install() — export(EXPORT) requires this to exist
install(TARGETS ${PROJECT_NAME} Koral_Runtime
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

# Koral's own shaders and assets, into the layout src/core/paths.cpp looks for
# (<libdir>/../share/Koral/<kind>). Shipping the binary without these gives a library that
# builds and then cannot find a single shader at runtime.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/shaders)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/assets)

# Bundle MoltenVK itself — it's a runtime-loaded Vulkan driver (dlopen'd by the loader via
# its ICD manifest), not something Koral links, so file(GET_RUNTIME_DEPENDENCIES) below
# never sees it. Installed under the same share/Koral/<kind> layout paths.cpp already
# knows how to find, as a pair: the dylib plus its ICD manifest, exactly as Homebrew laid
# them out, since the manifest's library_path is relative to itself
# (../../../lib/libMoltenVK.dylib) and only resolves correctly if that relative structure
# is preserved.
if(APPLE AND KORAL_MOLTENVK_DYLIB AND KORAL_MOLTENVK_ICD_JSON)
    install(FILES "${KORAL_MOLTENVK_DYLIB}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/moltenvk/lib)
    install(FILES "${KORAL_MOLTENVK_ICD_JSON}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/moltenvk/etc/vulkan/icd.d)
endif()

# ---------------------------------------------------------------------------
# Bundled runtime dependencies
#
# Most of Koral's dependencies are static, but not all: slang and the Vulkan loader are shared
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
# the *executable* and never a sibling lib/, so DLLs go next to Koral_Runtime; ELF/Mach-O resolve via
# the RPATH set above, so they go beside the library that names them.
if(WIN32)
    set(KORAL_DEP_DESTDIR ${CMAKE_INSTALL_BINDIR})
else()
    set(KORAL_DEP_DESTDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(CODE "
    set(KORAL_DEP_SEARCH_DIRS \"${KORAL_VCPKG_PREFIX}/lib\" \"${KORAL_VCPKG_PREFIX}/bin\")
    set(KORAL_DEP_DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${KORAL_DEP_DESTDIR}\")
")
install(CODE [[
    file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES "$<TARGET_FILE:Koral>"
            EXECUTABLES "$<TARGET_FILE:Koral_Runtime>"
            RESOLVED_DEPENDENCIES_VAR KORAL_RESOLVED_DEPS
            UNRESOLVED_DEPENDENCIES_VAR KORAL_UNRESOLVED_DEPS
            DIRECTORIES ${KORAL_DEP_SEARCH_DIRS}
            PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
            POST_EXCLUDE_REGEXES
                "^/(usr|lib|lib32|lib64)/"          # Linux system libraries
                "^/System/Library/" "^/usr/lib/"    # macOS system libraries
                "[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\]" # Windows system DLLs
    )

    # Koral_Runtime links libKoral, so Koral resolves as a dependency of itself. install(TARGETS)
    # has already placed it — with its RPATH rewritten to $ORIGIN — and copying the build-tree
    # binary over that would restore the build machine's RUNPATH and undo the relocatability. (It
    # currently survives only because file(INSTALL) skips a newer destination, which is a timestamp
    # race, not a guarantee.) Ours is not a dependency to bundle; it *is* the thing being installed.
    set(KORAL_OWN_BINARIES "$<TARGET_FILE_NAME:Koral>" "$<TARGET_FILE_NAME:Koral_Runtime>")

    foreach(dep IN LISTS KORAL_RESOLVED_DEPS)
        get_filename_component(depName "${dep}" NAME)
        if(depName IN_LIST KORAL_OWN_BINARIES)
            continue()
        endif()
        message(STATUS "Bundling runtime dependency: ${depName}")
        file(INSTALL
                DESTINATION "${KORAL_DEP_DESTINATION}"
                TYPE SHARED_LIBRARY
                FOLLOW_SYMLINK_CHAIN
                FILES "${dep}")
    endforeach()

    # Unresolved is the normal case for system libraries the loader will supply. It is only a
    # problem if something we *built against* shows up here, so say what they were.
    foreach(dep IN LISTS KORAL_UNRESOLVED_DEPS)
        message(STATUS "Leaving to the system loader: ${dep}")
    endforeach()
]])

# ---------------------------------------------------------------------------
# Vendored public dependencies
#
# Koral's public headers expose three third-party libraries: glm (in ~20 headers — vectors and
# matrices are part of the API's vocabulary), spdlog (log.h, whose inline templates take
# spdlog::format_string_t) and imgui (gui.h). A consumer therefore cannot compile against Koral
# without their headers, nor link against it without their objects — spdlog and imgui are static
# libs whose symbols do not escape libKoral.
#
# Rather than making that the consumer's problem (they would need vcpkg with matching versions,
# which for a launcher-generated project is an absurd amount of setup), the SDK *ships* them. A
# project needs nothing but the downloaded archive.
#
# This is a property of the package, not a promise to the user: consumers link only
# Koral::Koral. When these types stop appearing in Koral's public headers, delete this
# block and the vendored copies simply stop shipping — no consumer CMake changes.
# ---------------------------------------------------------------------------
option(KORAL_VENDOR_PUBLIC_DEPS
       "Ship glm/spdlog/fmt/imgui inside the SDK so consumers need no package manager" ON)

if(KORAL_VENDOR_PUBLIC_DEPS AND KORAL_VCPKG_PREFIX)
    set(KORAL_VENDOR_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/koral-vendor)
    set(KORAL_VENDOR_LIBDIR ${CMAKE_INSTALL_LIBDIR}/koral-vendor)

    # Headers. glm and fmt are header-only from the consumer's point of view; fmt comes along
    # because spdlog's headers include it.
    install(DIRECTORY
            "${KORAL_VCPKG_PREFIX}/include/glm"
            "${KORAL_VCPKG_PREFIX}/include/spdlog"
            "${KORAL_VCPKG_PREFIX}/include/fmt"
            DESTINATION ${KORAL_VENDOR_INCLUDEDIR})

    # imgui's headers are loose files at the top of include/, not a directory.
    file(GLOB KORAL_VENDOR_IMGUI_HEADERS
            "${KORAL_VCPKG_PREFIX}/include/imgui*.h"
            "${KORAL_VCPKG_PREFIX}/include/imconfig.h"
            "${KORAL_VCPKG_PREFIX}/include/imstb_*.h")
    install(FILES ${KORAL_VENDOR_IMGUI_HEADERS} DESTINATION ${KORAL_VENDOR_INCLUDEDIR})

    # The static archives the consumer must actually link. Order matters for GNU ld: a static lib
    # only resolves symbols demanded by something *earlier* on the command line, and spdlog's
    # headers call into fmt — so spdlog precedes fmt. The Config.cmake below preserves this order.
    set(KORAL_VENDOR_LIBS "")
    foreach(dep spdlog fmt imgui)
        file(GLOB found "${KORAL_VCPKG_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${dep}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        if(found)
            list(APPEND KORAL_VENDOR_LIBS ${found})
        else()
            message(WARNING "Vendoring: no static ${dep} under ${KORAL_VCPKG_PREFIX}/lib — "
                            "the installed SDK will not link.")
        endif()
    endforeach()
    install(FILES ${KORAL_VENDOR_LIBS} DESTINATION ${KORAL_VENDOR_LIBDIR})
endif()

# The CMake package, so consumers can find_package(Koral).
install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE Koral::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

# The install-tree config. It attaches the vendored dependencies to the Koral target's *interface*,
# so `target_link_libraries(app PRIVATE Koral::Koral)` is the whole of what a
# consumer writes. Paths are derived from the config file's own location, never baked in, so the
# archive can be unpacked anywhere.
set(KORAL_INSTALL_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/install/${PROJECT_NAME}Config.cmake")
file(WRITE "${KORAL_INSTALL_CONFIG}"
"include(CMakeFindDependencyMacro)
include(\"\${CMAKE_CURRENT_LIST_DIR}/${PROJECT_NAME}Targets.cmake\")

# lib/cmake/Koral/ -> the install prefix
get_filename_component(KORAL_RELOADED_PREFIX \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)
")

if(KORAL_VENDOR_PUBLIC_DEPS AND KORAL_VCPKG_PREFIX)
    # Names only: the paths are rebuilt relative to the unpacked prefix at find_package() time.
    set(KORAL_VENDOR_LIB_NAMES "")
    foreach(lib ${KORAL_VENDOR_LIBS})
        get_filename_component(name "${lib}" NAME)
        list(APPEND KORAL_VENDOR_LIB_NAMES "${name}")
    endforeach()

    file(APPEND "${KORAL_INSTALL_CONFIG}"
"
# Koral's public headers expose glm, spdlog and imgui, so the SDK ships them; see KORAL_VENDOR_PUBLIC_DEPS
# in the top-level CMakeLists. Consumers do not name these — they are pulled in by linking Koral.
if(NOT TARGET Koral::vendored)
    add_library(Koral::vendored INTERFACE IMPORTED)
    set_target_properties(Koral::vendored PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES \"\${KORAL_RELOADED_PREFIX}/${KORAL_VENDOR_INCLUDEDIR}\")

    set(KORAL_RELOADED_VENDORED_LIBS \"\")
    foreach(name ${KORAL_VENDOR_LIB_NAMES})
        list(APPEND KORAL_RELOADED_VENDORED_LIBS \"\${KORAL_RELOADED_PREFIX}/${KORAL_VENDOR_LIBDIR}/\${name}\")
    endforeach()
    if(KORAL_RELOADED_VENDORED_LIBS)
        set_target_properties(Koral::vendored PROPERTIES
                INTERFACE_LINK_LIBRARIES \"\${KORAL_RELOADED_VENDORED_LIBS}\")
    endif()
endif()

# APPEND rather than target_link_libraries: the Koral target is IMPORTED here.
set_property(TARGET Koral::${PROJECT_NAME} APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES Koral::vendored)
")
endif()

install(FILES
        "${KORAL_INSTALL_CONFIG}"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# Write config files directly at configure time (no build step needed).
# Each build configuration exports into its own subdirectory so that single-config
# generators (Ninja) don't clobber each other's exports in a shared folder. A
# consumer then selects the subdir matching its own build type, giving
# debug->debug / release->release linkage.
set(KORAL_CMAKE_OUTPUT_BASE_DIR "${CMAKE_SOURCE_DIR}/cmake" CACHE PATH "Base directory where per-config Koral cmake config files are written")
set(KORAL_CMAKE_OUTPUT_DIR "${KORAL_CMAKE_OUTPUT_BASE_DIR}/${CMAKE_BUILD_TYPE}")
file(MAKE_DIRECTORY "${KORAL_CMAKE_OUTPUT_DIR}")

# Export build-tree targets directly into the output dir
export(EXPORT ${PROJECT_NAME}Targets
        FILE "${KORAL_CMAKE_OUTPUT_DIR}/KORAL_RELOADEDTargets.cmake"
        NAMESPACE Koral::)

# Config file that pulls in the targets
file(WRITE "${KORAL_CMAKE_OUTPUT_DIR}/KORAL_RELOADEDConfig.cmake"
        "include(CMakeFindDependencyMacro)
include(\"\${CMAKE_CURRENT_LIST_DIR}/KORAL_RELOADEDTargets.cmake\")
")

# Re-export after each build to keep target file paths up to date
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DEXPORT_SET=${PROJECT_NAME}Targets
            -DOUTPUT_FILE="${KORAL_CMAKE_OUTPUT_DIR}/KORAL_RELOADEDTargets.cmake"
            -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_export_targets.cmake"
        COMMENT "Refreshing Koral cmake config in ${KORAL_CMAKE_OUTPUT_DIR}"
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_export_targets.cmake"
        "include(\"${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Export/${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/${PROJECT_NAME}Targets.cmake\" OPTIONAL)
")

# ---------------------------------------------------------------------------
# Unit tests (gtest). Enable with -DGFX_BUILD_TESTS=ON (default ON).
# ---------------------------------------------------------------------------
option(KORAL_BUILD_TESTS "Build the GFX unit test suite" ON)
if(KORAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
