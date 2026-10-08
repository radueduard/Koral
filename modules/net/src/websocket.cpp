#include "knet/http.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "io.h"

namespace knet
{
    using kor::ErrorCode;

    namespace detail
    {
        struct WebSocketState {
            TcpStream stream;
            bool client = true;   // a client masks what it sends; a server must not
            std::size_t maxMessage = 16 * 1024 * 1024;
            std::string protocol;
            std::atomic<std::uint16_t> closeCode = 0;
            std::atomic<bool> closeSent = false;
        };
    }

    namespace
    {
        enum Opcode : std::uint8_t { eContinuation = 0, eText = 1, eBinary = 2, eClose = 8, ePing = 9, ePong = 10 };
        constexpr std::string_view Guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        kor::Error ProtocolError(std::string message) { return detail::MakeError(ErrorCode::eProtocol, std::move(message)); }

        std::string Base64(std::span<const unsigned char> data) {
            std::string out(4 * ((data.size() + 2) / 3), '\0');
            const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data.data(), int(data.size()));
            out.resize(std::size_t(n));
            return out;
        }

        std::string AcceptKey(std::string_view key) {
            const std::string input = std::string(key) + std::string(Guid);
            unsigned char digest[SHA_DIGEST_LENGTH];
            SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
            return Base64(digest);
        }

        Bytes Frame(Opcode opcode, std::span<const std::byte> payload, bool mask) {
            Bytes frame;
            frame.reserve(payload.size() + 14);
            frame.push_back(std::byte(0x80 | opcode));
            const std::byte maskBit{std::uint8_t(mask ? 0x80 : 0)};
            if (payload.size() < 126) {
                frame.push_back(maskBit | std::byte(payload.size()));
            } else if (payload.size() <= 0xffff) {
                frame.push_back(maskBit | std::byte{126});
                frame.push_back(std::byte(payload.size() >> 8));
                frame.push_back(std::byte(payload.size()));
            } else {
                frame.push_back(maskBit | std::byte{127});
                for (int i = 7; i >= 0; --i) frame.push_back(std::byte(std::uint64_t(payload.size()) >> (8 * i)));
            }
            std::byte key[4]{};
            if (mask) {
                RAND_bytes(reinterpret_cast<unsigned char*>(key), 4);
                frame.insert(frame.end(), key, key + 4);
            }
            const std::size_t at = frame.size();
            frame.insert(frame.end(), payload.begin(), payload.end());
            if (mask)
                for (std::size_t i = 0; i < payload.size(); ++i) frame[at + i] ^= key[i & 3];
            return frame;
        }

        kor::Task<kor::Result<void>> SendFrame(std::shared_ptr<detail::WebSocketState> state, Opcode opcode, std::span<const std::byte> payload) {
            co_return co_await state->stream.Write(Frame(opcode, payload, state->client));
        }

        struct RawFrame {
            bool fin;
            Opcode opcode;
            Bytes payload;
        };

        kor::Task<kor::Result<RawFrame>> ReadFrame(std::shared_ptr<detail::WebSocketState> state) {
            std::byte head[2];
            if (auto r = co_await state->stream.ReadExactly(head); !r) co_return std::unexpected(std::move(r.error()));
            RawFrame frame{(std::uint8_t(head[0]) & 0x80) != 0, Opcode(std::uint8_t(head[0]) & 0x0f), {}};
            if ((std::uint8_t(head[0]) & 0x70) != 0) co_return std::unexpected(ProtocolError("a frame with reserved bits set (no extension was agreed)"));
            const bool masked = (std::uint8_t(head[1]) & 0x80) != 0;
            if (masked == state->client) co_return std::unexpected(ProtocolError(state->client ? "the server masked a frame" : "the client sent an unmasked frame"));
            std::uint64_t length = std::uint8_t(head[1]) & 0x7f;
            if (length == 126 || length == 127) {
                std::byte ext[8];
                const std::size_t n = length == 126 ? 2 : 8;
                if (auto r = co_await state->stream.ReadExactly(std::span(ext, n)); !r) co_return std::unexpected(std::move(r.error()));
                length = 0;
                for (std::size_t i = 0; i < n; ++i) length = (length << 8) | std::uint8_t(ext[i]);
            }
            if (length > state->maxMessage) co_return std::unexpected(ProtocolError(std::format("a frame of {} bytes, more than the {} allowed", length, state->maxMessage)));
            if (frame.opcode >= eClose && (length > 125 || !frame.fin)) co_return std::unexpected(ProtocolError("a control frame too long or fragmented"));
            std::byte key[4]{};
            if (masked)
                if (auto r = co_await state->stream.ReadExactly(key); !r) co_return std::unexpected(std::move(r.error()));
            frame.payload.resize(length);
            if (length)
                if (auto r = co_await state->stream.ReadExactly(frame.payload); !r) co_return std::unexpected(std::move(r.error()));
            if (masked)
                for (std::size_t i = 0; i < frame.payload.size(); ++i) frame.payload[i] ^= key[i & 3];
            co_return frame;
        }

        bool HeaderHas(std::string_view value, std::string_view token) {
            std::string lower(value);
            for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
            return lower.find(token) != std::string::npos;
        }
    }

    kor::Task<kor::Result<WebSocket>> WebSocket::Connect(std::string text, WebSocketOptions options) {
        auto url = Url::Parse(text);
        if (!url) co_return std::unexpected(std::move(url.error()));
        if (url->scheme != "ws" && url->scheme != "wss")
            co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("WebSocket::Connect takes ws(s), not '{}'", url->scheme)));
        ConnectOptions connect{.timeout = options.timeout};
        if (url->Secure()) connect.tls = options.tls;
        auto stream = co_await TcpStream::Connect(url->host, url->port, connect);
        if (!stream) co_return std::unexpected(std::move(stream.error()));

        unsigned char nonce[16];
        RAND_bytes(nonce, sizeof(nonce));
        const std::string key = Base64(nonce);
        std::string request = std::format("GET {} HTTP/1.1\r\nHost: {}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                          "Sec-WebSocket-Key: {}\r\nSec-WebSocket-Version: 13\r\n", url->target, url->Authority(), key);
        if (!options.protocols.empty()) {
            request += "Sec-WebSocket-Protocol: ";
            for (std::size_t i = 0; i < options.protocols.size(); ++i) request += (i ? ", " : "") + options.protocols[i];
            request += "\r\n";
        }
        for (const auto& h : options.headers) request += std::format("{}: {}\r\n", h.name, h.value);
        request += "\r\n";
        if (auto w = co_await stream->Write(request, options.timeout); !w) co_return std::unexpected(std::move(w.error()));

        auto header = co_await stream->ReadUntil("\r\n\r\n", 64 * 1024, options.timeout);
        if (!header) co_return std::unexpected(std::move(header.error()));
        if (!header->starts_with("HTTP/1.1 101"))
            co_return std::unexpected(ProtocolError(std::format("the server refused the upgrade: '{}'", header->substr(0, header->find("\r\n")))));
        auto headerValue = [&](std::string_view name) -> std::string {
            std::string_view rest = *header;
            while (!rest.empty()) {
                const auto end = rest.find("\r\n");
                const std::string_view line = rest.substr(0, end);
                rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 2);
                const auto colon = line.find(':');
                if (colon == std::string_view::npos || colon != name.size()) continue;
                if (std::equal(name.begin(), name.end(), line.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                    std::string_view v = line.substr(colon + 1);
                    while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
                    return std::string(v);
                }
            }
            return {};
        };
        if (headerValue("Sec-WebSocket-Accept") != AcceptKey(key)) co_return std::unexpected(ProtocolError("the server's Sec-WebSocket-Accept is wrong"));
        auto state = std::make_shared<detail::WebSocketState>();
        state->stream = *stream;
        state->client = true;
        state->maxMessage = options.maxMessage;
        state->protocol = headerValue("Sec-WebSocket-Protocol");
        co_return WebSocket(state);
    }

    kor::Task<kor::Result<WebSocket>> WebSocket::Accept(TcpStream stream, std::size_t maxMessage) {
        auto header = co_await stream.ReadUntil("\r\n\r\n", 64 * 1024, Duration(10'000));
        if (!header) co_return std::unexpected(std::move(header.error()));
        std::string key, protocols;
        bool upgrade = false;
        std::string_view rest = *header;
        rest.remove_prefix(rest.find("\r\n") + 2);
        while (!rest.empty()) {
            const auto end = rest.find("\r\n");
            const std::string_view line = rest.substr(0, end);
            rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 2);
            const auto colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            std::string name(line.substr(0, colon));
            for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
            std::string_view value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
            if (name == "sec-websocket-key") key = std::string(value);
            else if (name == "upgrade") upgrade = HeaderHas(value, "websocket");
            else if (name == "sec-websocket-protocol") protocols = std::string(value);
        }
        if (!header->starts_with("GET ") || !upgrade || key.empty()) {
            co_await stream.Write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            stream.Close();
            co_return std::unexpected(ProtocolError("not a WebSocket upgrade request"));
        }
        auto state = std::make_shared<detail::WebSocketState>();
        state->stream = stream;
        state->client = false;
        state->maxMessage = maxMessage;
        state->protocol = protocols.substr(0, protocols.find(','));   // the first offered
        std::string response = std::format("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {}\r\n", AcceptKey(key));
        if (!state->protocol.empty()) response += std::format("Sec-WebSocket-Protocol: {}\r\n", state->protocol);
        response += "\r\n";
        if (auto w = co_await stream.Write(response); !w) co_return std::unexpected(std::move(w.error()));
        co_return WebSocket(state);
    }

    kor::Task<kor::Result<void>> WebSocket::Send(std::string_view text) {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "send on a WebSocket that is not connected"));
        auto state = _state;
        if (state->closeSent) co_return std::unexpected(detail::MakeError(ErrorCode::eConnectionClosed, "send after close"));
        co_return co_await SendFrame(state, eText, AsBytes(text));
    }

    kor::Task<kor::Result<void>> WebSocket::Send(std::span<const std::byte> binary) {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "send on a WebSocket that is not connected"));
        auto state = _state;
        if (state->closeSent) co_return std::unexpected(detail::MakeError(ErrorCode::eConnectionClosed, "send after close"));
        co_return co_await SendFrame(state, eBinary, binary);
    }

    kor::Task<kor::Result<WebSocketMessage>> WebSocket::Receive() {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "receive on a WebSocket that is not connected"));
        auto state = _state;
        auto closed = [&](std::string why) {
            if (state->closeCode == 0) state->closeCode = 1006;
            return std::unexpected(detail::MakeError(ErrorCode::eConnectionClosed, std::move(why)));
        };
        if (state->closeCode != 0) co_return closed("the WebSocket is closed");
        WebSocketMessage message;
        bool started = false;
        for (;;) {
            auto frame = co_await ReadFrame(state);
            if (!frame) {
                if (frame.error().code == ErrorCode::eProtocol) {   // RFC 6455 7.4.1: 1002 protocol error, then close
                    const std::byte code[2]{std::byte(1002 >> 8), std::byte(1002 & 0xff)};
                    if (!state->closeSent.exchange(true)) co_await SendFrame(state, eClose, code);
                    state->closeCode = 1002;
                    state->stream.Close();
                    co_return std::unexpected(std::move(frame.error()));
                }
                co_return closed(frame.error().message);
            }
            switch (frame->opcode) {
                case ePing:
                    co_await SendFrame(state, ePong, frame->payload);
                    continue;
                case ePong:
                    continue;
                case eClose: {
                    std::uint16_t code = 1005;   // none given
                    if (frame->payload.size() >= 2) code = std::uint16_t((std::uint16_t(frame->payload[0]) << 8) | std::uint16_t(frame->payload[1]));
                    state->closeCode = code;
                    if (!state->closeSent.exchange(true)) {   // they began it: answer with the same code
                        const std::byte echo[2]{std::byte(code >> 8), std::byte(code & 0xff)};
                        co_await SendFrame(state, eClose, code == 1005 ? std::span<const std::byte>() : std::span<const std::byte>(echo));
                    }
                    state->stream.Close();
                    co_return std::unexpected(detail::MakeError(ErrorCode::eConnectionClosed, std::format("the WebSocket was closed ({})", code)));
                }
                case eText:
                case eBinary:
                    if (started) co_return std::unexpected(ProtocolError("a new message before the last one finished"));
                    started = true;
                    message.binary = frame->opcode == eBinary;
                    break;
                case eContinuation:
                    if (!started) co_return std::unexpected(ProtocolError("a continuation frame with no message"));
                    break;
                default:
                    co_return std::unexpected(ProtocolError(std::format("an unknown opcode {}", int(frame->opcode))));
            }
            if (message.data.size() + frame->payload.size() > state->maxMessage) co_return std::unexpected(ProtocolError("a message larger than allowed"));
            message.data.insert(message.data.end(), frame->payload.begin(), frame->payload.end());
            if (frame->fin) co_return message;
        }
    }

    kor::Task<kor::Result<void>> WebSocket::Close(std::uint16_t code, std::string reason) {
        if (!_state) co_return kor::Result<void>{};
        auto state = _state;
        if (state->closeSent.exchange(true)) co_return kor::Result<void>{};
        Bytes payload{std::byte(code >> 8), std::byte(code & 0xff)};
        reason.resize(std::min<std::size_t>(reason.size(), 123));
        const auto bytes = AsBytes(reason);
        payload.insert(payload.end(), bytes.begin(), bytes.end());
        co_return co_await SendFrame(state, eClose, payload);
    }

    bool WebSocket::Open() const { return _state && _state->closeCode == 0 && !_state->closeSent; }
    std::uint16_t WebSocket::CloseCode() const { return _state ? _state->closeCode.load() : 0; }
    std::string WebSocket::Protocol() const { return _state ? _state->protocol : std::string(); }
}
