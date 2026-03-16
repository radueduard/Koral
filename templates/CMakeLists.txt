cmake_minimum_required(VERSION 3.28)
project(@NAME@ VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glm CONFIG REQUIRED)
find_package(assimp REQUIRED)

file (GLOB_RECURSE SOURCE_FILES "src/*.cpp" "src/*.c")
file (GLOB_RECURSE HEADER_FILES "src/*.h" "src/*.hpp")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${HEADER_FILES})

set(GFX_RELOADED_INCLUDE_DIRS
        "@GFX_PATH@/include"
        "@GFX_PATH@/cmake-build-debug/include"
)

find_library(GFX_RELOADED_LIBRARY
        NAMES GFX_RELOADED
        HINTS "@GFX_PATH@/cmake-build-debug/"
)

if(NOT GFX_RELOADED_LIBRARY)
    message(FATAL_ERROR "GFX_RELOADED library not found!")
endif()

message(STATUS "GFX_RELOADED library found at: ${GFX_RELOADED_LIBRARY}")

target_include_directories(${PROJECT_NAME} PUBLIC
    ${GFX_RELOADED_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PUBLIC ${GFX_RELOADED_LIBRARY})
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm assimp::assimp)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        ASSETS_PATH="@PATH@/assets/"
        SHADERS_PATH="@PATH@/shaders/"
)

set(OUTPUT_DLL_DIR "@GFX_PATH@/scenes" CACHE PATH "Where to copy built DLL")

if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DLL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${OUTPUT_DLL_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>
            "${OUTPUT_DLL_DIR}"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying ${PROJECT_NAME} and runtime DLL dependencies to ${OUTPUT_DLL_DIR}"
    )
endif()
