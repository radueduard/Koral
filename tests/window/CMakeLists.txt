# Windowed integration tests — boot a real window + swap chain and render frames.
# Separate executable from koral_integration_tests: it creates a windowed Vulkan
# context, which cannot coexist with that binary's headless global environment.
add_executable(koral_window_tests
        test_windowed.cpp
)

target_link_libraries(koral_window_tests PRIVATE
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

target_include_directories(koral_window_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR})

include(GoogleTest)
# DISCOVERY_MODE PRE_TEST: enumerate at run time so a build host without a display
# never has to launch the binary just to list tests.
#
# No loader override: the vcpkg Vulkan-Loader is now built with WSI (Wayland/XCB/
# Xlib), so windowed surface creation works against it directly. The bundled
# validation layers are found via VK_ADD_LAYER_PATH set by the runtime itself.
gtest_discover_tests(koral_window_tests
        DISCOVERY_MODE PRE_TEST)

# OpenGL windowed parity tests. Separate executable: one windowed context per
# process (same constraint as the Vulkan windowed test), and the two backends
# cannot share a process anyway.
add_executable(koral_window_tests_gl
        test_windowed_gl.cpp
)

target_link_libraries(koral_window_tests_gl PRIVATE
        Koral
        GTest::gtest
        GTest::gtest_main
        magic_enum::magic_enum
        glm::glm
        spdlog::spdlog
        imgui::imgui
        glfw
)

target_include_directories(koral_window_tests_gl PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR})

# No special environment: kor::Window pins OpenGL windows to the X11 platform
# itself (GLEW needs GLX), and GL needs no Vulkan loader. The test skips
# gracefully when no display is available.
gtest_discover_tests(koral_window_tests_gl
        DISCOVERY_MODE PRE_TEST)
