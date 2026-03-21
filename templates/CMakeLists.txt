cmake_minimum_required(VERSION 3.28)
project(@NAME@ VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glm CONFIG REQUIRED)
find_package(assimp REQUIRED)
find_package(imgui CONFIG REQUIRED)

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
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)

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
if (UNIX)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DLL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${OUTPUT_DLL_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Copying ${PROJECT_NAME} to ${OUTPUT_DLL_DIR}"
    )
endif()

if (WIN32)
    set(GFX_RUNTIME_EXECUTABLE "@GFX_PATH@/cmake-build-debug/Gfx_Runtime.exe" CACHE FILEPATH "Path to GFX Runtime executable")
elseif (UNIX)
    set(GFX_RUNTIME_EXECUTABLE "@GFX_PATH@/cmake-build-debug/Gfx_Runtime" CACHE FILEPATH "Path to GFX Runtime executable")
endif()

set(RUN_CONFIG_CONTENT
"<component name=\"ProjectRunConfigurationManager\">
  <configuration default=\"false\" name=\"${PROJECT_NAME}\" type=\"CMakeRunConfiguration\" factoryName=\"Application\"
    PROGRAM_PARAMS=\"$<TARGET_FILE:${PROJECT_NAME}>\"
    PROJECT_NAME=\"${PROJECT_NAME}\"
    TARGET_NAME=\"${PROJECT_NAME}\"
    CONFIG_NAME=\"debug\"
    RUN_PATH=\"${GFX_RUNTIME_EXECUTABLE}\">
    <method v=\"2\">
      <option name=\"com.jetbrains.cidr.execution.CidrBuildBeforeRunTaskProvider$BuildBeforeRunTask\" enabled=\"true\" />
    </method>
  </configuration>
</component>")

file(GENERATE OUTPUT "${CMAKE_SOURCE_DIR}/.idea/runConfigurations/${PROJECT_NAME}.xml" CONTENT "${RUN_CONFIG_CONTENT}")
