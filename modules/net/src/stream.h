//
// What a TcpStream is underneath, shared with the HTTP and WebSocket code that reads and writes it.
//

#pragma once

#include <deque>
#include <memory>
#include <string>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "knet/socket.h"
#include "io.h"

namespace knet::detail
{
    struct StreamState : std::enable_shared_from_this<StreamState> {
        struct WriteJob {
            Bytes data;
            std::shared_ptr<Op<void>> op;
            std::shared_ptr<asio::steady_timer> timer;
        };

        asio::ip::tcp::socket socket;
        std::shared_ptr<asio::ssl::context> sslContext;
        std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket&>> ssl;
        /** Bytes read past what a ReadUntil asked for, handed out before the socket is read again. */
        std::string buffer;
        std::deque<WriteJob> writes;
        bool writing = false;
        bool closed = false;   // Close() was called: what fails from now on failed because of it

        explicit StreamState(asio::ip::tcp::socket socket);
        ~StreamState();

        kor::Result<void> MakeClientTls(const TlsOptions& options, const std::string& host);
        /** On the I/O thread: begins the next queued write, if none is running. */
        void StartWrite();
        [[nodiscard]] kor::Error Failure(const asio::error_code& ec, std::string_view what) const;
    };

    /** One read straight from the socket (or TLS), ignoring the read-ahead buffer. */
    kor::Task<kor::Result<std::size_t>> RawReadSome(std::shared_ptr<StreamState> state, std::span<std::byte> buffer, Duration timeout);
}
