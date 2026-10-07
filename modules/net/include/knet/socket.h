#pragma once

// Sockets as coroutines await them: TCP streams (optionally TLS), TCP listeners and UDP sockets.
//
// Every operation that waits returns a kor::Task — `co_await` it in a coroutine, or Wait() and Take() it
// from ordinary code — whose result is a kor::Result: the value, or a kor::Error saying what went wrong
// (eNetwork, eTimedOut, eConnectionClosed, eProtocol). Nothing throws, and a cancelled task cancels the
// operation it is waiting for.
//
// The sockets themselves live on koral-net's own I/O thread; a coroutine resumes where kor::Tokens resume —
// the main thread if it was there, the background pool otherwise.
//
// @code
// kor::Task<void> Hello() {
//     auto stream = co_await knet::TcpStream::Connect("example.com", 443, {.tls = knet::TlsOptions{}});
//     if (!stream) { kor::log::Error("{}", stream.error().message); co_return; }
//     co_await stream->Write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
//     auto header = co_await stream->ReadUntil("\r\n\r\n");
// }
// @endcode

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <error.h>
#include <task.h>

#include "export.h"

namespace knet
{
    using Bytes = std::vector<std::byte>;
    using Duration = std::chrono::milliseconds;

    namespace detail { struct StreamState; struct ListenerState; struct UdpState; }

    /** @brief A numeric address and a port: what a name resolves to. */
    struct KNET_API Endpoint {
        std::string address;   ///< "127.0.0.1", "::1"
        std::uint16_t port = 0;

        [[nodiscard]] bool IsV6() const { return address.find(':') != std::string::npos; }
        /** @brief "127.0.0.1:80", "[::1]:80". */
        [[nodiscard]] std::string ToString() const;
        bool operator==(const Endpoint&) const = default;
    };

    /** @brief Every address @p host has, for @p port (a numeric address resolves to itself). */
    KNET_API kor::Task<kor::Result<std::vector<Endpoint>>> Resolve(std::string host, std::uint16_t port);

    /** @brief How a TLS client checks whom it talks to. */
    struct TlsOptions {
        /** Check the server's certificate against trusted roots and its name. Off only for testing. */
        bool verifyPeer = true;
        /** The name to send (SNI) and check the certificate for; empty: the host connected to. */
        std::string serverName;
        /** Extra trusted roots, PEM text — a private CA, a self-signed test server. The system's are trusted too. */
        std::string caPem;
    };

    /** @brief A TLS server's identity. */
    struct TlsServerOptions {
        std::string certificatePem;   ///< The certificate chain, PEM.
        std::string privateKeyPem;    ///< Its private key, PEM.
    };

    struct ConnectOptions {
        Duration timeout = Duration(10'000);   ///< For the whole connect (and TLS handshake); 0: none.
        std::optional<TlsOptions> tls;         ///< Speak TLS over the connection.
        bool noDelay = true;                   ///< Disable Nagle's algorithm: small writes go out at once.
    };

    /** @brief A connected byte stream: TCP, or TLS over TCP. Cheap to copy; copies are the same connection. */
    class KNET_API TcpStream {
    public:
        TcpStream() = default;

        /** @brief Connects to @p host (a name or a numeric address), trying each address it resolves to. */
        static kor::Task<kor::Result<TcpStream>> Connect(std::string host, std::uint16_t port, ConnectOptions options = {});

        /** @brief Reads what has arrived — at least a byte, at most `buffer.size()`. eConnectionClosed at the end of the stream. */
        kor::Task<kor::Result<std::size_t>> ReadSome(std::span<std::byte> buffer, Duration timeout = Duration::zero());
        /** @brief Reads exactly `buffer.size()` bytes. */
        kor::Task<kor::Result<void>> ReadExactly(std::span<std::byte> buffer, Duration timeout = Duration::zero());
        /** @brief Reads up to and including @p delimiter (at most @p limit bytes): a line, an HTTP header. */
        kor::Task<kor::Result<std::string>> ReadUntil(std::string delimiter, std::size_t limit = 64 * 1024, Duration timeout = Duration::zero());
        /** @brief Reads until the other side closes the stream (at most @p limit bytes). */
        kor::Task<kor::Result<Bytes>> ReadToEnd(std::size_t limit = 64 * 1024 * 1024, Duration timeout = Duration::zero());
        /** @brief Writes all of @p data. Writes from different coroutines go out one after another, never interleaved. */
        kor::Task<kor::Result<void>> Write(std::span<const std::byte> data, Duration timeout = Duration::zero());
        kor::Task<kor::Result<void>> Write(std::string_view text, Duration timeout = Duration::zero());

        /** @brief Closes the connection; what is waiting on it finishes with eConnectionClosed. */
        void Close();
        [[nodiscard]] bool Valid() const { return _state != nullptr; }
        [[nodiscard]] bool Secure() const;
        [[nodiscard]] Endpoint Local() const;
        [[nodiscard]] Endpoint Remote() const;

    private:
        friend class TcpListener;
        friend struct detail::StreamState;
        explicit TcpStream(std::shared_ptr<detail::StreamState> state) : _state(std::move(state)) {}
        std::shared_ptr<detail::StreamState> _state;
    };

    /** @brief A TCP port accepting connections — with TLS when given an identity. */
    class KNET_API TcpListener {
    public:
        TcpListener() = default;

        /** @brief Listens on @p port (0: any free one, see Port()) at @p address ("0.0.0.0" all IPv4, "::" all). */
        static kor::Result<TcpListener> Listen(std::uint16_t port, std::string address = "0.0.0.0",
                                               std::optional<TlsServerOptions> tls = std::nullopt);

        /** @brief The next connection (after its TLS handshake, for a TLS listener). */
        kor::Task<kor::Result<TcpStream>> Accept();

        [[nodiscard]] std::uint16_t Port() const;
        void Close();
        [[nodiscard]] bool Valid() const { return _state != nullptr; }

    private:
        std::shared_ptr<detail::ListenerState> _state;
    };

    /** @brief One UDP packet: who sent it, and what. */
    struct Datagram {
        Endpoint from;
        Bytes data;
    };

    /** @brief A UDP socket. Packets arrive whole or not at all, in any order. */
    class KNET_API UdpSocket {
    public:
        UdpSocket() = default;

        /** @brief A socket on @p port (0: any free one) at @p address ("0.0.0.0", or "::" for IPv6). */
        static kor::Result<UdpSocket> Bind(std::uint16_t port = 0, std::string address = "0.0.0.0");

        /** @brief The next packet (at most 64 KiB). */
        kor::Task<kor::Result<Datagram>> Receive(Duration timeout = Duration::zero());
        /** @brief Sends one packet. */
        kor::Task<kor::Result<void>> SendTo(Endpoint to, std::span<const std::byte> data);

        [[nodiscard]] std::uint16_t Port() const;
        void Close();
        [[nodiscard]] bool Valid() const { return _state != nullptr; }

    private:
        std::shared_ptr<detail::UdpState> _state;
    };

    /** @brief The bytes of a string, for Write and SendTo. */
    inline std::span<const std::byte> AsBytes(std::string_view text) { return std::as_bytes(std::span(text.data(), text.size())); }
    /** @brief Bytes as text. */
    inline std::string AsText(std::span<const std::byte> bytes) { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }
}
