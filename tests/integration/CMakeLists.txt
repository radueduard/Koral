# GPU integration tests — boot a real Vulkan device and exercise GPU paths.
add_executable(koral_integration_tests
        gpu_fixture.cpp
        test_buffer_roundtrip.cpp
        test_compute_dispatch.cpp
        test_gpu_timers.cpp
        test_barrier_resolution_cost.cpp
        test_buffer_memory_cost.cpp
        test_image_roundtrip.cpp
        test_offscreen_render.cpp
        test_vulkan_coverage.cpp
        test_raytracing.cpp
        test_importer.cpp
        test_image_module.cpp
        test_image_export.cpp
        test_image_compress.cpp
        test_gui_viewport.cpp
        test_camera_aspect.cpp
        test_camera_panel.cpp
        test_slang_shader.cpp
        test_poison.cpp
)

target_link_libraries(koral_integration_tests PRIVATE
        Koral
        # Vertex formats come from the mesh module, and image IO from the two image modules — all
        # linked here the way a project links them.
        koral-mesh
        koral-image-import
        koral-image-export
        koral-image-compress
        koral-model-import
        # The viewport and gizmo need a real image, so they are covered here rather than in the unit
        # suite: same headless ImGui context, but with a device behind it.
        koral-gui-extras
        # A camera's aspect source is a real image or framebuffer, so it is covered with a device.
        koral-camera
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
