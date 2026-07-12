# GPU integration tests — boot a real Vulkan device and exercise GPU paths.
add_executable(koral_integration_tests
        gpu_fixture.cpp
        test_buffer_roundtrip.cpp
        test_compute_dispatch.cpp
        test_image_roundtrip.cpp
        test_offscreen_render.cpp
        test_vulkan_coverage.cpp
        test_raytracing.cpp
        test_importer.cpp
        test_slang_shader.cpp
        test_poison.cpp
)

target_link_libraries(koral_integration_tests PRIVATE
        Koral
        GTest::gtest
        GTest::gtest_main
        magic_enum::magic_enum
        glm::glm
        spdlog::spdlog
        imgui::imgui
        Vulkan::Vulkan
        glfw
)

target_include_directories(koral_integration_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR})

include(GoogleTest)
# DISCOVERY_MODE PRE_TEST: enumerate tests at run time, not build time, so a build
# host without a GPU/display never has to launch the binary just to list tests.
gtest_discover_tests(koral_integration_tests DISCOVERY_MODE PRE_TEST)
