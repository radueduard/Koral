# koral-net's tests: everything over loopback — a server and its client in one process, TLS with a certificate
# made on the spot, and the game protocol through its own simulator of a bad network. No outside network.
add_executable(koral_net_tests test_sockets.cpp)
target_link_libraries(koral_net_tests PRIVATE koral-net Koral GTest::gtest GTest::gtest_main spdlog::spdlog OpenSSL::Crypto)
target_include_directories(koral_net_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
include(GoogleTest)
gtest_discover_tests(koral_net_tests DISCOVERY_MODE PRE_TEST)
