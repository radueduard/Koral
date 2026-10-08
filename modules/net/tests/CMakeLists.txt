# koral-net's tests: everything over loopback — a server and its client in one process, TLS with a certificate
# made on the spot, and the game protocol through its own simulator of a bad network. No outside network.
add_executable(koral_net_tests test_sockets.cpp test_http.cpp test_host.cpp test_replication.cpp)
target_link_libraries(koral_net_tests PRIVATE koral-net Koral GTest::gtest GTest::gtest_main spdlog::spdlog OpenSSL::Crypto ZLIB::ZLIB)
target_include_directories(koral_net_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
include(GoogleTest)
gtest_discover_tests(koral_net_tests DISCOVERY_MODE PRE_TEST)

# The C interface, from C: also the check that koralNet_c.h is C.
add_executable(koral_net_capi_test test_capi.c)
set_target_properties(koral_net_capi_test PROPERTIES C_STANDARD 11 LINKER_LANGUAGE CXX)
target_link_libraries(koral_net_capi_test PRIVATE koral-net Koral)
target_include_directories(koral_net_capi_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
add_test(NAME Net.CInterface COMMAND koral_net_capi_test)
