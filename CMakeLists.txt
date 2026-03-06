cmake_minimum_required(VERSION 3.28)
project(GFX_RELOADED VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(assimp REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

file (GLOB_RECURSE SOURCE_FILES "src/*.cpp" "src/*.c")
file (GLOB_RECURSE HEADER_FILES "src/*.h" "src/*.hpp")

add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

if (UNIX AND NOT APPLE)
    set(GLFW_USE_WAYLAND ON)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE magic_enum::magic_enum)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE GLEW::GLEW OpenGL::GL)
target_link_libraries(${PROJECT_NAME} PRIVATE assimp::assimp)

target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

target_link_libraries(${PROJECT_NAME} PRIVATE
        glslang::glslang
        glslang::MachineIndependent
        glslang::OSDependent
        glslang::SPIRV
        glslang::glslang-default-resource-limits
)

target_link_libraries(${PROJECT_NAME} PRIVATE spirv-cross-core)
target_link_libraries(${PROJECT_NAME} PRIVATE spirv-cross-glsl)
target_link_libraries(${PROJECT_NAME} PRIVATE reflectcpp::reflectcpp)

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GLX)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE glad::glad)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SHADERS_PATH="${CMAKE_SOURCE_DIR}/shaders/"
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)