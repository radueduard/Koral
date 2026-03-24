cmake_minimum_required(VERSION 3.28)
project(GFX_RELOADED VERSION 0.0.1 LANGUAGES C CXX)
include(GenerateExportHeader)

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

find_package(glfw3 CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
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

find_package(imgui CONFIG REQUIRED)

file (GLOB_RECURSE SOURCE_FILES
        src/core/*.cpp
        src/io/*.cpp
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

if (UNIX AND NOT APPLE)
    set(GLFW_USE_WAYLAND ON)
endif()

target_link_libraries(${PROJECT_NAME} glfw)
target_link_libraries(${PROJECT_NAME} magic_enum::magic_enum)
target_link_libraries(${PROJECT_NAME} glm::glm)
target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} GLEW::GLEW OpenGL::GL)
target_link_libraries(${PROJECT_NAME} assimp::assimp)

target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan)
target_link_libraries(${PROJECT_NAME} GPUOpen::VulkanMemoryAllocator)

target_link_libraries(${PROJECT_NAME}
        glslang::glslang
        glslang::MachineIndependent
        glslang::OSDependent
        glslang::SPIRV
        glslang::glslang-default-resource-limits
)

target_link_libraries(${PROJECT_NAME} spirv-cross-core)
target_link_libraries(${PROJECT_NAME} spirv-cross-glsl)
target_link_libraries(${PROJECT_NAME} reflectcpp::reflectcpp)
target_link_libraries(${PROJECT_NAME} imgui::imgui)

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} OpenGL::GLX)
endif()

target_link_libraries(${PROJECT_NAME} glad::glad)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/"
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}/"
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/include)

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

string(REPLACE "\\" "/" VCPKG_TOOLCHAIN_FWD "${CMAKE_TOOLCHAIN_FILE}")
target_compile_definitions(Gfx_Runtime PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    VCPKG_ROOT="${VCPKG_TOOLCHAIN_FWD}"
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/")