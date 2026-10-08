#pragma once

// HTTP/1.1 and WebSockets, client side (and a WebSocket server's handshake, for tools and tests): plain or
// over TLS ("https://", "wss://"), with chunked bodies, gzip and redirects.
//
// @code
// auto response = co_await knet::HttpGet("https://api.example.com/scores");
// if (response && response->Ok()) Show(response->Text());
//
// auto socket = co_await knet::WebSocket::Connect("wss://chat.example.com/room");
// co_await socket->Send("hello");
// while (auto message = co_await socket->Receive()) Print(message->Text());
// @endcode

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "socket.h"

namespace knet
{
    /** @brief A parsed http(s)/ws(s) URL. */
    struct KNET_API Url {
        std::string scheme;   ///< "http", "https", "ws", "wss" (lower case)
        std::string host;
        std::uint16_t port = 0;   ///< The scheme's default when the URL names none.
        std::string target = "/";  ///< Path and query: what the request line asks for.

        static kor::Result<Url> Parse(std::string_view text);
        [[nodiscard]] bool Secure() const { return scheme == "https" || scheme == "wss"; }
        /** @brief Host, with the port when it is not the scheme's default: the Host header. */
        [[nodiscard]] std::string Authority() const;
        [[nodiscard]] std::string ToString() const;
        /** @brief Where @p reference (absolute, "//host/x", "/x" or "x") points, seen from this URL. */
        [[nodiscard]] kor::Result<Url> Resolve(std::string_view reference) const;
    };

    struct HttpHeader {
        std::string name;
        std::string value;
    };

    struct HttpRequest {
        std::string method = "GET";
        std::string url;
        std::vector<HttpHeader> headers;
        Bytes body;
        Duration timeout = Duration(30'000);   ///< For the whole exchange, redirects included.
        int maxRedirects = 5;                  ///< 0: hand back the redirect itself.
        TlsOptions tls;                        ///< For https.
        std::size_t maxBody = 256 * 1024 * 1024;
    };

    struct KNET_API HttpResponse {
        int status = 0;
        std::string reason;
        std::vector<HttpHeader> headers;
        Bytes body;          ///< Decoded: chunks joined, gzip/deflate inflated.
        std::string url;     ///< Where it came from, after redirects.

        [[nodiscard]] bool Ok() const { return status >= 200 && status < 300; }
        /** @brief The first header of that name (case-insensitive), or empty. */
        [[nodiscard]] std::string_view Header(std::string_view name) const;
        [[nodiscard]] std::string Text() const { return AsText(body); }
    };

    /** @brief Sends @p request and reads the whole response. An HTTP error status is a response, not a failure. */
    KNET_API kor::Task<kor::Result<HttpResponse>> Fetch(HttpRequest request);
    KNET_API kor::Task<kor::Result<HttpResponse>> HttpGet(std::string url, std::vector<HttpHeader> headers = {});
    KNET_API kor::Task<kor::Result<HttpResponse>> HttpPost(std::string url, std::string body, std::string contentType = "application/json",
                                                           std::vector<HttpHeader> headers = {});

    // ---- WebSocket ----------------------------------------------------------------------------------------

    struct WebSocketMessage {
        bool binary = false;
        Bytes data;
        [[nodiscard]] std::string Text() const { return AsText(data); }
    };

    struct WebSocketOptions {
        std::vector<HttpHeader> headers;
        std::vector<std::string> protocols;   ///< Sec-WebSocket-Protocol to offer.
        Duration timeout = Duration(10'000);  ///< For connecting and the handshake.
        TlsOptions tls;                       ///< For wss.
        std::size_t maxMessage = 16 * 1024 * 1024;
    };

    namespace detail { struct WebSocketState; }

    /** @brief A WebSocket (RFC 6455). Pings are answered by itself; a close is answered and ends Receive. */
    class KNET_API WebSocket {
    public:
        WebSocket() = default;

        static kor::Task<kor::Result<WebSocket>> Connect(std::string url, WebSocketOptions options = {});
        /** @brief The server side: reads the upgrade request from @p stream and answers it. */
        static kor::Task<kor::Result<WebSocket>> Accept(TcpStream stream, std::size_t maxMessage = 16 * 1024 * 1024);

        kor::Task<kor::Result<void>> Send(std::string_view text);
        kor::Task<kor::Result<void>> Send(std::span<const std::byte> binary);
        /** @brief The next message; eConnectionClosed once the socket is closed (CloseCode() says how). */
        kor::Task<kor::Result<WebSocketMessage>> Receive();
        /** @brief Starts the closing handshake: the other side's close then ends Receive. */
        kor::Task<kor::Result<void>> Close(std::uint16_t code = 1000, std::string reason = {});

        [[nodiscard]] bool Valid() const { return _state != nullptr; }
        [[nodiscard]] bool Open() const;
        /** @brief The close code received (1000 normal, 1006 the connection just went), 0 while open. */
        [[nodiscard]] std::uint16_t CloseCode() const;
        /** @brief The subprotocol the server chose, if any. */
        [[nodiscard]] std::string Protocol() const;

    private:
        explicit WebSocket(std::shared_ptr<detail::WebSocketState> state) : _state(std::move(state)) {}
        std::shared_ptr<detail::WebSocketState> _state;
    };
}
