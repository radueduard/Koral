# ---------------------------------------------------------------------------
# GFX unit tests
#
# These are pure-logic, GPU-independent tests: they exercise the CPU-side
# algorithms and value types of the framework (the allocator, flag bitsets,
# the functional error model, vertex layout math, resource ownership) without
# needing a Vulkan device, window or swap chain.
#
# They link against the Koral shared library for the handful of symbols
# that are defined out-of-line (e.g. kor::describe / kor::Error::toString), and
# pull the public headers + generated api.h through its INTERFACE include dirs.
# ---------------------------------------------------------------------------
find_package(GTest CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(koral_tests
        test_tlsf_allocator.cpp
        test_flags.cpp
        test_error.cpp
        test_log_repeat.cpp
        test_structs.cpp
        test_mesh_layout.cpp
        test_resource.cpp
        test_builder_recoverable.cpp
        test_project_config.cpp
)

target_link_libraries(koral_tests PRIVATE
        Koral
        GTest::gtest
        GTest::gtest_main
        magic_enum::magic_enum
        glm::glm
        # Public GFX headers (mesh.h, buffer.h, context.h, log.h, ...) expose these
        # dependencies' headers directly, so consumers — including this test target —
        # need their include directories, exactly like Koral_Runtime does.
        spdlog::spdlog
        imgui::imgui
        Vulkan::Vulkan
        glfw
)

# The public headers reach into a few private headers via relative paths
# (e.g. resource.h -> ../src/core/fileWatcher.h), so expose the source tree too.
target_include_directories(koral_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(koral_tests)

# ---------------------------------------------------------------------------
# Integration tests: these boot a real Vulkan device (through a window) and
# exercise GPU code paths. They need a GPU + display, so they live in a separate
# binary and skip gracefully at runtime when a device cannot be created.
# ---------------------------------------------------------------------------
option(KORAL_BUILD_INTEGRATION_TESTS "Build the GFX GPU integration tests" ON)
if(KORAL_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()

# Windowed tests boot a real window + swap chain (needs a display). Separate opt-in
# so headless CI can build the rest without pulling in the presentation stack.
option(KORAL_BUILD_WINDOW_TESTS "Build the GFX windowed integration tests" ON)
if(KORAL_BUILD_WINDOW_TESTS)
    add_subdirectory(window)
endif()
