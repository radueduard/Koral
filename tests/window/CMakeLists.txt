# A scene library the windowed tests load, open by name, unload and reload (sceneLibrary.h).
add_library(koral_test_scenes SHARED test_scene_library.cpp)
target_link_libraries(koral_test_scenes PRIVATE Koral glm::glm imgui::imgui spdlog::spdlog fmt::fmt)
target_include_directories(koral_test_scenes PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
set_target_properties(koral_test_scenes PROPERTIES CXX_VISIBILITY_PRESET hidden)

# Windowed integration tests — boot a real window + swap chain and render frames.
# Separate executable from koral_integration_tests: it creates a windowed Vulkan
# context, which cannot coexist with that binary's headless global environment.
add_executable(koral_window_tests
        test_windowed.cpp
)

target_link_libraries(koral_window_tests PRIVATE
        Koral
        # Vertex formats come from the mesh module, linked here the way a project links it.
        koral-mesh
        # And the GUI extras, whose viewport and panels are drawn from the test scene's RenderUI —
        # the only place a real ImGui backend is behind them.
        koral-gui-extras
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

add_dependencies(koral_window_tests koral_test_scenes)
target_compile_definitions(koral_window_tests PRIVATE
        KORAL_TEST_SCENE_LIBRARY="$<TARGET_FILE:koral_test_scenes>")

include(GoogleTest)
# DISCOVERY_MODE PRE_TEST: enumerate at run time so a build host without a display
# never has to launch the binary just to list tests.
#
# No loader override: the vcpkg Vulkan-Loader is now built with WSI (Wayland/XCB/
# Xlib), so windowed surface creation works against it directly. The bundled
# validation layers are found via VK_ADD_LAYER_PATH set by the runtime itself.
gtest_discover_tests(koral_window_tests
        DISCOVERY_MODE PRE_TEST)

# An application with no windowing system, running offscreen scenes only. Its own executable: an
# application is one per process. Needs a Vulkan device but no display.
add_executable(koral_headless_app_tests test_headless_app.cpp)
target_link_libraries(koral_headless_app_tests PRIVATE Koral GTest::gtest GTest::gtest_main glm::glm spdlog::spdlog imgui::imgui)
target_include_directories(koral_headless_app_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR})
gtest_discover_tests(koral_headless_app_tests DISCOVERY_MODE PRE_TEST)

# The C interface, driven from C (which is also the check that koral_c.h is C). No display needed.
add_executable(koral_capi_test test_capi.c)
set_target_properties(koral_capi_test PROPERTIES C_STANDARD 11 LINKER_LANGUAGE CXX)
target_link_libraries(koral_capi_test PRIVATE Koral)
target_include_directories(koral_capi_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
add_test(NAME CApi.DrivesASceneFromC COMMAND koral_capi_test)
