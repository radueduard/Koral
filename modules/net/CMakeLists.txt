# ---------------------------------------------------------------------------
# The networking module: koral.net (namespace knet)
#
# Sockets a coroutine awaits (TCP, TLS, UDP), HTTP and WebSocket clients, and a game protocol over UDP with
# reliable and unreliable channels, replication and prediction. Linked by whoever uses it, like every module;
# a project that does not link it pays nothing for it — not even a thread.
# ---------------------------------------------------------------------------

add_library(koral-net SHARED
        src/export.cpp
        src/io.cpp
        src/socket.cpp
        src/http.cpp
        src/websocket.cpp
        src/bits.cpp
        src/host.cpp
        src/replication.cpp
        src/capi.cpp
)
target_include_directories(koral-net PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(koral-net PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-net>)
target_compile_definitions(koral-net PRIVATE KORAL_NET_EXPORTS)
target_link_libraries(koral-net PUBLIC Koral)

# asio does the sockets, OpenSSL the TLS, zlib the gzip of HTTP bodies: all static and private, so none of
# them shows through the module's headers or reaches a consumer's link line.
find_package(asio CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(koral-net PRIVATE asio::asio OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB spdlog::spdlog)
if(WIN32)
    target_link_libraries(koral-net PRIVATE ws2_32 crypt32)
endif()

set_target_properties(koral-net PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

if(MSVC)
    # As koral-ui: no manifest of its own, so C# binds its import of Koral.dll to the engine (see that module).
    target_link_options(koral-net PRIVATE /MANIFEST:NO)
endif()

if(APPLE)
    set_target_properties(koral-net PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-net PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

install(TARGETS koral-net
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-net
        FILES_MATCHING PATTERN "*.h")
