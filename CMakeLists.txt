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

set (SOURCE_FILES
        src/core/buffer.cpp
        src/core/commandBuffer.cpp
        src/core/computePipeline.cpp
        src/core/context.cpp
        src/core/descriptorSet.cpp
        src/core/descriptorSetLayout.cpp
        src/core/framebuffer.cpp
        src/core/graphicsPipeline.cpp
        src/core/image.cpp
        src/core/imageView.cpp
        src/core/importer.cpp
        src/core/initLibs.cpp
        src/core/sampler.cpp
        src/core/scheduler.cpp
        src/core/shader.cpp
        src/core/surface.cpp

        src/io/input.cpp
        src/io/manager.cpp
        src/io/time.cpp
        src/io/window.cpp

        src/backends/open_gl/buffer.cpp
        src/backends/open_gl/commandBuffer.cpp
        src/backends/open_gl/computePipeline.cpp
        src/backends/open_gl/descriptorSet.cpp
        src/backends/open_gl/framebuffer.cpp
        src/backends/open_gl/graphicsPipeline.cpp
        src/backends/open_gl/image.cpp
        src/backends/open_gl/imageView.cpp
        src/backends/open_gl/sampler.cpp
        src/backends/open_gl/scheduler.cpp
        src/backends/open_gl/shader.cpp

        src/backends/vulkan/allocator.cpp
        src/backends/vulkan/buffer.cpp
        src/backends/vulkan/commandBuffer.cpp
        src/backends/vulkan/computePipeline.cpp
        src/backends/vulkan/descriptorPool.cpp
        src/backends/vulkan/descriptorSet.cpp
        src/backends/vulkan/descriptorSetLayout.cpp
        src/backends/vulkan/device.cpp
        src/backends/vulkan/framebuffer.cpp
        src/backends/vulkan/graphicsPipeline.cpp
        src/backends/vulkan/image.cpp
        src/backends/vulkan/imageView.cpp
        src/backends/vulkan/physicalDevice.cpp
        src/backends/vulkan/runtime.cpp
        src/backends/vulkan/sampler.cpp
        src/backends/vulkan/scheduler.cpp
        src/backends/vulkan/shader.cpp
        src/backends/vulkan/surface.cpp
        src/backends/vulkan/swapChain.cpp
        src/backends/vulkan/vulkanContext.cpp
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

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} OpenGL::GLX)
endif()

target_link_libraries(${PROJECT_NAME} glad::glad)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/"
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/include)


add_executable(Gfx_Runtime
        src/main.cpp
        src/engine.cpp
        src/lab/LabMesh/Lab01.cpp)

target_include_directories(Gfx_Runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
# link the GFX_RELOADED dll
target_link_libraries(Gfx_Runtime PRIVATE ${PROJECT_NAME})