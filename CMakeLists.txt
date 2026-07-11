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

target_compile_definitions(${PROJECT_NAME} PRIVATE
        ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
        SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/"
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

# Declare the export set via install() — export(EXPORT) requires this to exist
install(TARGETS ${PROJECT_NAME} Gfx_Runtime
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Write config files directly at configure time (no build step needed).
# Each build configuration exports into its own subdirectory so that single-config
# generators (Ninja) don't clobber each other's exports in a shared folder. A
# consumer then selects the subdir matching its own build type, giving
# debug->debug / release->release linkage.
set(GFX_CMAKE_OUTPUT_BASE_DIR "/mnt/w/CLionProjects/GFX/cmake" CACHE PATH "Base directory where per-config GFX_RELOADED cmake config files are written")
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
