# The GUI extras' tests: each panel shown in a koral-ui interface over an offscreen scene, and read back.
# Their own executable, as an application is one per process.
add_executable(koral_gui_extras_tests test_gui_extras.cpp)
target_link_libraries(koral_gui_extras_tests PRIVATE koral-gui-extras koral-camera koral-ui Koral
        GTest::gtest GTest::gtest_main glm::glm spdlog::spdlog)
target_include_directories(koral_gui_extras_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
include(GoogleTest)
gtest_discover_tests(koral_gui_extras_tests DISCOVERY_MODE PRE_TEST)
