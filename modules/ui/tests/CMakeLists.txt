# koral-ui's tests: what the canvas records, checked on the CPU, and what it draws, read back from a real
# GPU through an offscreen scene — its own executable, as an application is one per process.
add_executable(koral_ui_tests test_ui.cpp)
target_link_libraries(koral_ui_tests PRIVATE koral-ui Koral GTest::gtest GTest::gtest_main glm::glm spdlog::spdlog)
target_include_directories(koral_ui_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
target_compile_definitions(koral_ui_tests PRIVATE KUI_TEST_SHADERS="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
include(GoogleTest)
gtest_discover_tests(koral_ui_tests DISCOVERY_MODE PRE_TEST)

# The C interface, from C: also the check that koralUI_c.h is C.
add_executable(koral_ui_capi_test test_capi.c)
set_target_properties(koral_ui_capi_test PROPERTIES C_STANDARD 11 LINKER_LANGUAGE CXX)
target_link_libraries(koral_ui_capi_test PRIVATE koral-ui Koral)
target_include_directories(koral_ui_capi_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
add_test(NAME UI.CInterface COMMAND koral_ui_capi_test)

# Docking, drag and drop and modifier chains, in a window: a sample to try them by hand.
add_executable(koral_ui_docking ../samples/docking.cpp)
target_link_libraries(koral_ui_docking PRIVATE koral-ui Koral glm::glm spdlog::spdlog)
target_include_directories(koral_ui_docking PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)

# koral-ui against Dear ImGui, the same grid in each: what a frame costs. Built with the tests, run by hand —
# from a Release build, or it measures the debugging.
add_executable(koral_ui_bench ../bench/bench.cpp)
target_link_libraries(koral_ui_bench PRIVATE koral-ui Koral imgui::imgui glm::glm spdlog::spdlog)
target_include_directories(koral_ui_bench PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
