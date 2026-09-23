cmake_minimum_required(VERSION 3.28)

# vcpkg-overlay-ports/kubazip renames the mz_* symbol namespace of kubazip's vendored copy of
# miniz.h, so it no longer collides with minizip-ng (pulled in transitively via OpenColorIO) at
# link time. Must be set before project() -- that is when the vcpkg toolchain reads it.
list(APPEND VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-overlay-ports")

# vcpkg-overlay-triplets/arm64-osx.cmake adds VCPKG_OSX_DEPLOYMENT_TARGET to the stock arm64-osx
# triplet. Setting CMAKE_OSX_DEPLOYMENT_TARGET below only covers Koral's own binaries; vcpkg builds
# each port in its own CMake invocation that never sees it, so without this the bundled dylibs are
# built against the build machine's OS and drag the whole artifact's floor up with them. Same
# before-project() rule as the overlay ports above.
list(APPEND VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-overlay-triplets")

# Oldest macOS a shipped binary must run on.
#
# Like VCPKG_OVERLAY_PORTS above, this MUST be set before project(): that is when CMake fixes the
# deployment target, and setting it afterwards is silently ignored. It was set after project() to
# begin with, and the resulting build declared minos 26.0 — the runner's own OS — which is exactly
# the failure this is here to prevent. If you move it, move it up, never down.
#
# 15.0, and neither of the two things forcing it upwards is ours to choose:
#
#   13.3 is the floor imposed by the toolchain. Apple's libc++ marks the floating-point
#   std::to_chars overloads as availability("introduced in macOS 13.3"), and std::format of any
#   float goes straight through them, so 12.0 — MoltenVK's own minimum, and the obvious first
#   guess — fails the build outright with "'to_chars' is unavailable".
#
#   15.0 is the floor imposed by Slang, and it is the binding one. vcpkg's shader-slang port does
#   not build from source; it downloads Slang's official release binaries. So
#   VCPKG_OSX_DEPLOYMENT_TARGET has no effect on libslang-compiler.dylib, which ships built for
#   macOS 15.0, and a runtime archive bundling it needs 15.0 no matter what everything beside it
#   says. This was found by check-macos-portability.sh, which failed the build rather than let a
#   green job ship an artifact that would not launch.
#
# Lowering this below 15.0 therefore requires building Slang from source via an overlay port, not
# just editing this number — the check will simply fail again.
#
# VCPKG_OSX_DEPLOYMENT_TARGET matters just as much as the CMake one. Without it vcpkg builds every
# dependency against the build machine's OS, and a binary is only as portable as the newest thing
# it links.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
    if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endif()

project(Koral VERSION 0.1.0 LANGUAGES C CXX)
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
# Assimp is found here but linked only by modules/model-import — the engine itself no longer reads
# model files. Kept at this level so the module (a subdirectory) sees the imported target.
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
# We link the vcpkg-built Vulkan-Loader. The manifest requests its WSI features
# (Wayland + XCB + Xlib on Linux, Win32 is always built; macOS needs no loader
# feature — MoltenVK supplies its own Metal surface extension as an ICD), so builds
# and tests create surfaces on every platform with no system SDK installed.
#
# Whether it gets *shipped* depends on which archive, and the two answers differ:
#
#   SDK      — yes. libKoral.so carries DT_NEEDED libvulkan.so.1, so anyone linking
#              against it needs the loader resolvable at link time. Dropping it from
#              the install tree broke the standalone-consumer check outright: the
#              release container has no system loader, so ld had nothing to resolve
#              vkCreateInstance and its ~40 friends against.
#
#   runtime  — no, on Linux and Windows. A loader has to match the machine it runs on:
#              its ICD manifests, its driver interface version, and above all its
#              *implicit* layers (Steam overlay, gamescope WSI, …), which were built
#              against whatever loader that machine ships. Ours, given priority by the
#              $ORIGIN RPATH, is how a package that runs perfectly on the build box
#              segfaults inside libvulkan on someone else's desktop. The Package step
#              in .github/workflows/release.yml strips it from the runtime archive;
#              both loaders have SONAME libvulkan.so.1 and the loader ABI is stable, so
#              the binary then resolves the system one like every other Vulkan app.
#              macOS is exempt — there is no system loader there, so the runtime archive
#              keeps it alongside the MoltenVK ICD installed below.
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
# assimp is deliberately absent: model importing lives in modules/model-import, which links it.

target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
target_link_libraries(${PROJECT_NAME} PRIVATE GPUOpen::VulkanMemoryAllocator)

# Declared here rather than beside the shader/asset paths it was written for, because the
# validation-layer path just below is the same kind of dev-only absolute path and needs to test
# it. An option() has to precede every use: read before its declaration it is simply empty, which
# would silently take the distribution branch in a development build.
option(KORAL_EMBED_SOURCE_PATHS
       "Bake the source-tree shader/asset paths in as a development fallback (OFF for distribution)" ON)

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
    # Dev builds only — same reasoning as KORAL_EMBED_SOURCE_PATHS below, and the same failure
    # mode: this is an absolute path into *this* machine's build tree, which on a user's machine
    # names nothing. Harmless there (VK_ADD_LAYER_PATH pointing at a missing directory is
    # ignored), but a shipped binary should not carry the build machine's filesystem layout, and
    # a distribution build gets its validation layers from the user's own Vulkan SDK if they
    # have one.
    if(KORAL_EMBED_SOURCE_PATHS)
        target_compile_definitions(${PROJECT_NAME} PRIVATE KORAL_VK_LAYER_PATH="${KORAL_VK_LAYER_DIR}")
        message(STATUS "Bundled Vulkan validation layers: ${KORAL_VK_LAYER_DIR}")
    else()
        message(STATUS "Distribution build: not baking in the bundled validation-layer path")
    endif()
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

# ZLIB only: the rest of the codec stack (TIFF/JPEG/PNG/Jasper) came in with OpenImageIO, which
# moved to the image module along with the code that used it.
target_link_libraries(${PROJECT_NAME} PRIVATE ${ZLIB_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE asio::asio)

# KTX and OpenImageIO are deliberately absent: image IO lives in modules/image, which links them.

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GLX)
endif()

# The duplicate-fmt band-aid that used to live here (-Wl,--allow-multiple-definition, and MSVC's
# /FORCE:MULTIPLE beside it) moved to modules/image along with OpenImageIO, which is what dragged a
# second copy of fmt into the link. Koral no longer links it, so it no longer needs the flag.

# <windows.h> defines min() and max() as function-like macros, which collide with
# std::numeric_limits<T>::max() and friends and produce a baffling C2589 rather than anything that
# names the real problem. NOMINMAX suppresses them; WIN32_LEAN_AND_MEAN keeps the rest of the Win32
# surface out of every translation unit while we are here.
#
# PUBLIC so a consumer building against the SDK gets the same treatment. That is a convenience, not
# the fix: the public headers themselves spell max as (std::numeric_limits<T>::max)(), which blocks
# the macro whatever the consumer defines. Anything added to include/ should keep doing that.
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# /utf-8 is PUBLIC because it is a requirement we impose on consumers, not a preference of ours.
#
# fmt hard-fails with static_assert("Unicode support requires compiling with /utf-8") unless MSVC is
# told its sources are UTF-8, and fmt reaches consumers through our public headers (log.h pulls in
# spdlog, which pulls in fmt). Our own build never noticed: the vcpkg toolchain passes /utf-8
# already. A project doing find_package(Koral) with no vcpkg gets no such help and fails to compile
# against a perfectly good SDK.
#
# Caught by the "Verify the SDK links standalone" step, which builds exactly that project. Worth
# keeping in mind that anything leaking out of the public headers becomes a consumer's problem —
# the same reason the SDK vendors glm/spdlog/fmt/imgui in the first place.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC /utf-8)
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
        # list(FIND) rather than the tidier `if(depName IN_LIST ...)`, because this runs inside the
        # generated cmake_install.cmake, which carries no cmake_policy() call and therefore executes
        # with whatever the *running* cmake defaults to. if(IN_LIST) needs CMP0057 NEW; under CMake
        # 4.x it works because the OLD behaviour of policies that old was removed outright, but
        # under 3.x it is unset and the install dies with "Unknown arguments specified". list(FIND)
        # depends on no policy and behaves the same everywhere.
        list(FIND KORAL_OWN_BINARIES "${depName}" KORAL_OWN_INDEX)
        if(NOT KORAL_OWN_INDEX EQUAL -1)
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

    # imgui's headers are loose files at the top of include/, not a directory. ImGuizmo's header comes
    # along because the GUI extras module's koralGizmo.h includes it: its *objects* reach a consumer
    # through that module's shared library, which re-exports them, but the declarations have to be
    # compiled and so have to be here.
    file(GLOB KORAL_VENDOR_IMGUI_HEADERS
            "${KORAL_VCPKG_PREFIX}/include/imgui*.h"
            "${KORAL_VCPKG_PREFIX}/include/imconfig.h"
            "${KORAL_VCPKG_PREFIX}/include/imstb_*.h"
            "${KORAL_VCPKG_PREFIX}/include/ImGuizmo.h")
    install(FILES ${KORAL_VENDOR_IMGUI_HEADERS} DESTINATION ${KORAL_VENDOR_INCLUDEDIR})

    # The static archives the consumer must actually link. Order matters for GNU ld: a static lib
    # only resolves symbols demanded by something *earlier* on the command line, and spdlog's
    # headers call into fmt — so spdlog precedes fmt. The Config.cmake below preserves this order.
    #
    # Collected per configuration, because vcpkg keeps two trees and they are not interchangeable.
    # This used to read ${KORAL_VCPKG_PREFIX}/lib unconditionally — the *release* tree — for every
    # configuration, which nobody noticed while CI only ever packaged Release. A Debug install then
    # shipped release import libraries: a Debug consumer linked fmt.lib, bound to fmt.dll, and got
    # a bin/ containing fmtd.dll. It compiled, it linked, and it failed to load with "fmt.dll: cannot
    # open shared object file" the first time anyone ran it.
    #
    # The `d`-suffix fallback is there because the debug name is not the same everywhere: vcpkg's
    # MSVC builds carry CMake's DEBUG_POSTFIX (fmtd.lib, spdlogd.lib, imguid.lib) while the same
    # ports on Linux and macOS keep the release name inside debug/lib (libfmt.a). Trying the plain
    # name first and the suffixed one second covers both without hard-coding a platform rule.
    set(KORAL_VENDOR_LIBS "")
    set(KORAL_VENDOR_LIBS_DEBUG "")
    foreach(dep spdlog fmt imgui)
        file(GLOB found "${KORAL_VCPKG_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${dep}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        if(found)
            list(APPEND KORAL_VENDOR_LIBS ${found})
        else()
            message(WARNING "Vendoring: no static ${dep} under ${KORAL_VCPKG_PREFIX}/lib — "
                            "the installed SDK will not link.")
        endif()

        set(foundDebug "")
        foreach(candidate "${dep}" "${dep}d")
            file(GLOB foundDebug "${KORAL_VCPKG_PREFIX}/debug/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${candidate}${CMAKE_STATIC_LIBRARY_SUFFIX}")
            if(foundDebug)
                break()
            endif()
        endforeach()
        if(foundDebug)
            list(APPEND KORAL_VENDOR_LIBS_DEBUG ${foundDebug})
        else()
            message(WARNING "Vendoring: no static ${dep} under ${KORAL_VCPKG_PREFIX}/debug/lib — "
                            "a Debug install of the SDK will not link.")
        endif()
    endforeach()

    # Both sets go into the same directory, and on Windows — the only platform where a Debug SDK is
    # actually built — the DEBUG_POSTFIX keeps their names apart, so installing both configurations
    # into one prefix is lossless. Where the two names collide (Linux, macOS) the CONFIGURATIONS
    # guards mean only the configuration being installed is written, so a single-config install tree
    # is still correct; it is only the both-into-one-prefix case that cannot be represented there,
    # and no platform but Windows builds one.
    install(FILES ${KORAL_VENDOR_LIBS} DESTINATION ${KORAL_VENDOR_LIBDIR}
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel)
    install(FILES ${KORAL_VENDOR_LIBS_DEBUG} DESTINATION ${KORAL_VENDOR_LIBDIR}
            CONFIGURATIONS Debug)

    # A single-config generator with no CMAKE_BUILD_TYPE matches neither guard above and would
    # install no vendored libraries at all. Treat that as Release, which is what the rest of the
    # build already assumes when the type is unset.
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        install(FILES ${KORAL_VENDOR_LIBS} DESTINATION ${KORAL_VENDOR_LIBDIR})
    endif()
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
    set(KORAL_VENDOR_LIB_NAMES_DEBUG "")
    foreach(lib ${KORAL_VENDOR_LIBS_DEBUG})
        get_filename_component(name "${lib}" NAME)
        list(APPEND KORAL_VENDOR_LIB_NAMES_DEBUG "${name}")
    endforeach()

    file(APPEND "${KORAL_INSTALL_CONFIG}"
"
# Koral's public headers expose glm, spdlog and imgui, so the SDK ships them; see KORAL_VENDOR_PUBLIC_DEPS
# in the top-level CMakeLists. Consumers do not name these — they are pulled in by linking Koral.
#
# Which set to link is decided here rather than baked in, because the same config file is installed
# into three shapes of tree and all three have to work:
#
#   release only — what the release archives contain, and what a consumer building in Debug against
#                  a downloaded SDK has always linked. Must keep working: there is no debug set to
#                  fall back to, and refusing to link one would break every published SDK.
#   debug only   — a local `cmake --install --config Debug --prefix …`.
#   both         — Windows, where the DEBUG_POSTFIX keeps the two sets' filenames apart so they can
#                  share one prefix. Only here is the choice per-configuration, and only here does
#                  the generator expression below actually decide anything.
#
# EXISTS rather than trusting the lists: the names come from the machine that built the SDK, and
# whether the file is present is a property of the tree the consumer unpacked.
if(NOT TARGET Koral::vendored)
    add_library(Koral::vendored INTERFACE IMPORTED)
    set_target_properties(Koral::vendored PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES \"\${KORAL_RELOADED_PREFIX}/${KORAL_VENDOR_INCLUDEDIR}\")

    set(KORAL_RELOADED_VENDORED_RELEASE \"\")
    foreach(name ${KORAL_VENDOR_LIB_NAMES})
        set(_koral_lib \"\${KORAL_RELOADED_PREFIX}/${KORAL_VENDOR_LIBDIR}/\${name}\")
        if(EXISTS \"\${_koral_lib}\")
            list(APPEND KORAL_RELOADED_VENDORED_RELEASE \"\${_koral_lib}\")
        endif()
    endforeach()

    set(KORAL_RELOADED_VENDORED_DEBUG \"\")
    foreach(name ${KORAL_VENDOR_LIB_NAMES_DEBUG})
        set(_koral_lib \"\${KORAL_RELOADED_PREFIX}/${KORAL_VENDOR_LIBDIR}/\${name}\")
        if(EXISTS \"\${_koral_lib}\")
            list(APPEND KORAL_RELOADED_VENDORED_DEBUG \"\${_koral_lib}\")
        endif()
    endforeach()
    unset(_koral_lib)

    set(KORAL_RELOADED_VENDORED_LIBS \"\")
    if(KORAL_RELOADED_VENDORED_RELEASE AND KORAL_RELOADED_VENDORED_DEBUG)
        # Release first, then debug: only one group survives a given configuration, and within each
        # the spdlog-before-fmt order the install block established is preserved — which is what
        # GNU ld needs, since spdlog's headers call into fmt.
        foreach(lib IN LISTS KORAL_RELOADED_VENDORED_RELEASE)
            list(APPEND KORAL_RELOADED_VENDORED_LIBS \"\$<\$<NOT:\$<CONFIG:Debug>>:\${lib}>\")
        endforeach()
        foreach(lib IN LISTS KORAL_RELOADED_VENDORED_DEBUG)
            list(APPEND KORAL_RELOADED_VENDORED_LIBS \"\$<\$<CONFIG:Debug>:\${lib}>\")
        endforeach()
    elseif(KORAL_RELOADED_VENDORED_DEBUG)
        set(KORAL_RELOADED_VENDORED_LIBS \${KORAL_RELOADED_VENDORED_DEBUG})
    else()
        set(KORAL_RELOADED_VENDORED_LIBS \${KORAL_RELOADED_VENDORED_RELEASE})
    endif()

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

# Runtime DLL placement for consumers (Windows).
#
# Windows has no RPATH: an executable resolves its DLLs from its own directory or PATH, nowhere
# else. Koral is a DLL and pulls in more that must sit beside the consumer's binary at load time —
# the Vulkan loader, slang, and (because they carry process-global state that cannot be shared
# across a static link on Windows, see the overlay triplet) imgui and glfw3. All are installed into
# <prefix>/bin by the GET_RUNTIME_DEPENDENCIES step above. A consumer's freshly built .exe sits in
# its own build tree with none of them beside it, so it links against the import libs and then
# fails to start. In our own build vcpkg's applocal deploy copies these automatically; a consumer
# using the packaged SDK with no vcpkg toolchain gets no such help, which is what this is for.
#
# This is deliberately a function the consumer calls, not something bolted onto Koral::Koral's
# interface: copying DLLs into every target that merely links Koral would be surprising, and a
# consumer may want them somewhere other than each target's own directory. A no-op off Windows,
# where the installed libraries carry an $ORIGIN / @loader_path RPATH and find each other unaided.
file(APPEND "${KORAL_INSTALL_CONFIG}"
"
function(koral_copy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    file(GLOB _koral_runtime_dlls \"\${KORAL_RELOADED_PREFIX}/${CMAKE_INSTALL_BINDIR}/*.dll\")
    foreach(_dll IN LISTS _koral_runtime_dlls)
        add_custom_command(TARGET \${target} POST_BUILD
                COMMAND \${CMAKE_COMMAND} -E copy_if_different \"\${_dll}\" \"\$<TARGET_FILE_DIR:\${target}>\"
                VERBATIM)
    endforeach()
endfunction()
")

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
# Modules: optional engine features a project links against (see include/module.h).
#
# The projects that demonstrate them are not built here: they are ordinary Koral
# projects that consume the *installed* SDK, and they live outside this tree
# (CameraDemo is a Hub project). Building them from here would test a build
# layout no user has.
# ---------------------------------------------------------------------------
option(KORAL_BUILD_MODULES "Build the modules that ship with Koral" ON)
if(KORAL_BUILD_MODULES)
    add_subdirectory(modules)

    # The modules' own third-party dependencies.
    #
    # The GET_RUNTIME_DEPENDENCIES pass further up scans Koral and Koral_Runtime, and nothing else —
    # so assimp, OpenImageIO and ktx, which only the modules link, were never bundled. The build tree
    # hides this: vcpkg's applocal deploy drops them next to each module as a post-build step, so a
    # dev build loads every module perfectly while the installed SDK cannot load model-import at all.
    # It fails as ERROR_MOD_NOT_FOUND from LoadLibrary — error 126, which names the module it was
    # asked for and not the dependency that is actually missing, so it reads like a broken module.
    #
    # Same destination as Koral's own bundled dependencies, deliberately. It is where the loader
    # already looks (beside the executable on Windows; via the $ORIGIN/.. that modules/*/CMakeLists
    # sets on ELF and Mach-O), and it means a library both a module and the engine need — spdlog,
    # zlib — is installed once rather than once per consumer.
    set(KORAL_MODULE_TARGETS
            koral-camera koral-gui-extras koral-mesh
            koral-image-import koral-image-export koral-image-compress
            koral-model-import)

    set(KORAL_MODULE_BINARIES "")
    foreach(target IN LISTS KORAL_MODULE_TARGETS)
        list(APPEND KORAL_MODULE_BINARIES "$<TARGET_FILE:${target}>")
    endforeach()

    install(CODE "
        set(KORAL_MODULE_DEP_SEARCH_DIRS \"${KORAL_VCPKG_PREFIX}/lib\" \"${KORAL_VCPKG_PREFIX}/bin\")
        set(KORAL_MODULE_DEP_DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${KORAL_DEP_DESTDIR}\")
        set(KORAL_MODULE_BINARIES \"${KORAL_MODULE_BINARIES}\")
    ")
    install(CODE [[
        file(GET_RUNTIME_DEPENDENCIES
                LIBRARIES ${KORAL_MODULE_BINARIES}
                RESOLVED_DEPENDENCIES_VAR KORAL_MODULE_RESOLVED_DEPS
                UNRESOLVED_DEPENDENCIES_VAR KORAL_MODULE_UNRESOLVED_DEPS
                DIRECTORIES ${KORAL_MODULE_DEP_SEARCH_DIRS}
                PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
                POST_EXCLUDE_REGEXES
                    "^/(usr|lib|lib32|lib64)/"          # Linux system libraries
                    "^/System/Library/" "^/usr/lib/"    # macOS system libraries
                    "[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\]" # Windows system DLLs
        )

        # Koral resolves as a dependency of every module, and each module as a dependency of the
        # ones built on it. Both are already installed, with their RPATHs rewritten; copying the
        # build-tree binary over either would undo that. Skipped by name, the same way the engine's
        # own pass skips Koral and Koral_Runtime.
        set(KORAL_MODULE_OWN_BINARIES "")
        foreach(binary IN LISTS KORAL_MODULE_BINARIES)
            get_filename_component(binaryName "${binary}" NAME)
            list(APPEND KORAL_MODULE_OWN_BINARIES "${binaryName}")
        endforeach()

        foreach(dep IN LISTS KORAL_MODULE_RESOLVED_DEPS)
            get_filename_component(depName "${dep}" NAME)
            list(FIND KORAL_MODULE_OWN_BINARIES "${depName}" KORAL_MODULE_OWN_INDEX)
            if(NOT KORAL_MODULE_OWN_INDEX EQUAL -1)
                continue()
            endif()
            if(EXISTS "${KORAL_MODULE_DEP_DESTINATION}/${depName}")
                continue()   # the engine's pass already bundled it
            endif()
            message(STATUS "Bundling module dependency: ${depName}")
            file(INSTALL
                    DESTINATION "${KORAL_MODULE_DEP_DESTINATION}"
                    TYPE SHARED_LIBRARY
                    FOLLOW_SYMLINK_CHAIN
                    FILES "${dep}")
        endforeach()

        foreach(dep IN LISTS KORAL_MODULE_UNRESOLVED_DEPS)
            message(STATUS "Leaving to the system loader: ${dep}")
        endforeach()
    ]])
endif()

# ---------------------------------------------------------------------------
# Staging the SDK: `cmake --build <dir> --target stage --config <cfg>`, or one click from the IDE.
#
# `--prefix` rather than CMAKE_INSTALL_PREFIX, because the prefix has to differ per configuration
# and the cache variable cannot: it is one value for the whole build tree, so a multi-config
# generator would stage Debug and Release on top of each other. They must not share a prefix —
# on Windows a Debug SDK is /MDd and a Release one /MD, and a project linking the wrong flavour
# gets a different std::string layout than the engine, which segfaults rather than failing to link.
#
# RelWithDebInfo and MinSizeRel stage as release. That is not a shortcut: they link the release
# CRT, so the release tree is genuinely the one they belong in.
#
# The dependencies are exactly the targets something installs. Naming them rather than relying on
# ALL has two effects worth keeping: a click builds before it stages, so the tree can never hold
# binaries older than the source; and the test targets stay out, which matters while
# koral_integration_tests does not link in Release.
# ---------------------------------------------------------------------------
add_custom_target(stage
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                --config $<CONFIG>
                --prefix "${CMAKE_SOURCE_DIR}/stage/$<IF:$<CONFIG:Debug>,debug,release>"
        USES_TERMINAL
        COMMENT "Staging the $<CONFIG> SDK into stage/$<IF:$<CONFIG:Debug>,debug,release>")

add_dependencies(stage ${PROJECT_NAME} Koral_Runtime)
if(KORAL_BUILD_MODULES)
    add_dependencies(stage
            koral-camera koral-gui-extras koral-mesh
            koral-image-import koral-image-export koral-image-compress koral-model-import)
endif()

# ---------------------------------------------------------------------------
# Unit tests (gtest). Enable with -DGFX_BUILD_TESTS=ON (default ON).
# ---------------------------------------------------------------------------
option(KORAL_BUILD_TESTS "Build the GFX unit test suite" ON)
if(KORAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
