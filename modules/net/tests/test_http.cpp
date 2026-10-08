// HTTP and WebSockets against servers in this process: plain and TLS, chunked and gzipped bodies, redirects,
// fragmented and control frames, the closing handshake.

#include <gtest/gtest.h>

#include <format>
#include <functional>
#include <map>

#include <zlib.h>

#include <knet/http.h>

#include "tls_identity.h"

using namespace knet;
using namespace std::chrono_literals;

namespace {
    template<class T>
    T Await(kor::Task<T> task) {
        task.Wait();
        return std::move(task.Take()).value();
    }

    std::string Gzip(std::string_view text) {
        z_stream z{};
        deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        std::string out(compressBound(uLong(text.size())) + 32, '\0');
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
        z.avail_in = uInt(text.size());
        z.next_out = reinterpret_cast<Bytef*>(out.data());
        z.avail_out = uInt(out.size());
        deflate(&z, Z_FINISH);
        out.resize(z.total_out);
        deflateEnd(&z);
        return out;
    }

    // A server answering each request with whatever `answer` makes of its request line, header and body.
    struct Server {
        TcpListener listener;
        kor::Task<void> loop;
        std::vector<std::string> seen;

        explicit Server(std::function<std::string(const std::string& head, const std::string& body)> answer,
                        std::optional<TlsServerOptions> tls = std::nullopt) {
            listener = TcpListener::Listen(0, "127.0.0.1", tls).value();
            loop = [](Server* self, auto answer) -> kor::Task<void> {
                for (;;) {
                    auto client = co_await self->listener.Accept();
                    if (!client) co_return;
                    auto head = co_await client->ReadUntil("\r\n\r\n");
                    if (!head) continue;
                    std::string body;
                    if (const auto at = head->find("Content-Length: "); at != std::string::npos) {
                        std::string n(std::stoul(head->substr(at + 16)), '\0');
                        if (!n.empty()) co_await client->ReadExactly(std::as_writable_bytes(std::span(n)));
                        body = n;
                    }
                    self->seen.push_back(*head);
                    co_await client->Write(answer(*head, body));
                    client->Close();
                }
            }(this, std::move(answer));
        }
        ~Server() { listener.Close(); loop.Wait(); }
        std::string Url(std::string_view path, bool secure = false) const { return std::format("{}://127.0.0.1:{}{}", secure ? "https" : "http", listener.Port(), path); }
    };

    std::string Reply(std::string_view status, std::string_view body, std::string_view extra = "") {
        return std::format("HTTP/1.1 {}\r\nContent-Length: {}\r\n{}\r\n{}", status, body.size(), extra, body);
    }
}

TEST(Http, UrlsParseAndResolve) {
    auto u = Url::Parse("https://user@Example.com:8443/a/b?x=1#frag");
    ASSERT_TRUE(u);
    EXPECT_EQ(u->scheme, "https");
    EXPECT_EQ(u->host, "Example.com");
    EXPECT_EQ(u->port, 8443);
    EXPECT_EQ(u->target, "/a/b?x=1");
    EXPECT_EQ(u->Authority(), "Example.com:8443");
    EXPECT_EQ(Url::Parse("http://h")->target, "/");
    EXPECT_EQ(Url::Parse("ws://[::1]:9/x")->host, "::1");
    EXPECT_EQ(Url::Parse("wss://h/")->port, 443);
    EXPECT_FALSE(Url::Parse("ftp://h"));
    EXPECT_FALSE(Url::Parse("nohost"));
    EXPECT_EQ(u->Resolve("/c")->ToString(), "https://Example.com:8443/c");
    EXPECT_EQ(u->Resolve("c")->ToString(), "https://Example.com:8443/a/c");
    EXPECT_EQ(u->Resolve("//other/x")->ToString(), "https://other/x");
    EXPECT_EQ(u->Resolve("http://z/")->ToString(), "http://z/");
}

TEST(Http, GetPostChunkedGzipAndRedirects) {
    const std::string big(100000, 'k');
    Server server([&](const std::string& head, const std::string& body) -> std::string {
        if (head.starts_with("GET /plain ")) return Reply("200 OK", "hello", "Content-Type: text/plain\r\n");
        if (head.starts_with("POST /echo ")) return Reply("201 Created", "got " + body);
        if (head.starts_with("GET /chunked "))
            return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n7;ext=1\r\n, world\r\n0\r\nX-Trailer: 1\r\n\r\n";
        if (head.starts_with("GET /gzip ")) return Reply("200 OK", Gzip(big), "Content-Encoding: gzip\r\n");
        if (head.starts_with("GET /moved ")) return Reply("302 Found", "", "Location: /plain\r\n");
        if (head.starts_with("POST /see-other ")) return Reply("303 See Other", "", "Location: /plain\r\n");
        if (head.starts_with("GET /loop ")) return Reply("302 Found", "", "Location: /loop\r\n");
        if (head.starts_with("GET /to-end ")) return "HTTP/1.1 200 OK\r\n\r\nuntil the end";
        return Reply("404 Not Found", "none");
    });

    auto plain = Await(HttpGet(server.Url("/plain")));
    ASSERT_TRUE(plain) << plain.error().message;
    EXPECT_EQ(plain->status, 200);
    EXPECT_EQ(plain->Text(), "hello");
    EXPECT_EQ(plain->Header("content-type"), "text/plain");
    EXPECT_NE(server.seen.back().find("Host: 127.0.0.1:"), std::string::npos);

    auto posted = Await(HttpPost(server.Url("/echo"), R"({"a":1})"));
    ASSERT_TRUE(posted);
    EXPECT_EQ(posted->status, 201);
    EXPECT_EQ(posted->Text(), R"(got {"a":1})");
    EXPECT_NE(server.seen.back().find("Content-Type: application/json"), std::string::npos);

    auto chunked = Await(HttpGet(server.Url("/chunked")));
    ASSERT_TRUE(chunked) << chunked.error().message;
    EXPECT_EQ(chunked->Text(), "hello, world");

    auto gzipped = Await(HttpGet(server.Url("/gzip")));
    ASSERT_TRUE(gzipped) << gzipped.error().message;
    EXPECT_EQ(gzipped->Text(), big);

    auto moved = Await(HttpGet(server.Url("/moved")));
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->Text(), "hello");
    EXPECT_EQ(moved->url, server.Url("/plain"));

    auto seeOther = Await(HttpPost(server.Url("/see-other"), "x"));
    ASSERT_TRUE(seeOther);
    EXPECT_EQ(seeOther->Text(), "hello");
    EXPECT_TRUE(server.seen.back().starts_with("GET /plain "));   // 303 turns a POST into a GET

    auto loop = Await(Fetch({.url = server.Url("/loop"), .maxRedirects = 3}));
    ASSERT_TRUE(loop);
    EXPECT_EQ(loop->status, 302);   // handed back once the redirects run out

    auto toEnd = Await(HttpGet(server.Url("/to-end")));
    ASSERT_TRUE(toEnd);
    EXPECT_EQ(toEnd->Text(), "until the end");

    auto missing = Await(HttpGet(server.Url("/nope")));
    ASSERT_TRUE(missing);   // an error status is still a response
    EXPECT_EQ(missing->status, 404);
    EXPECT_FALSE(missing->Ok());
}

TEST(Http, HttpsAndTimeouts) {
    const auto identity = test::MakeSelfSignedIdentity("localhost");
    Server server([](const std::string&, const std::string&) { return Reply("200 OK", "secret"); },
                  TlsServerOptions{identity.certificatePem, identity.privateKeyPem});
    auto refused = Await(HttpGet(server.Url("/", true)));
    EXPECT_FALSE(refused);   // self-signed, not trusted
    auto ok = Await(Fetch({.url = server.Url("/", true), .tls = {.serverName = "localhost", .caPem = identity.certificatePem}}));
    ASSERT_TRUE(ok) << ok.error().message;
    EXPECT_EQ(ok->Text(), "secret");

    // A server that never answers.
    auto silent = TcpListener::Listen(0, "127.0.0.1").value();
    auto accepted = silent.Accept();
    auto late = Await(Fetch({.url = std::format("http://127.0.0.1:{}/", silent.Port()), .timeout = 100ms}));
    ASSERT_FALSE(late);
    EXPECT_EQ(late.error().code, kor::ErrorCode::eTimedOut);
    silent.Close();
}

TEST(WebSockets, EchoTextBinaryPingAndClose) {
    auto listener = TcpListener::Listen(0, "127.0.0.1").value();
    auto server = [](TcpListener l) -> kor::Task<std::uint16_t> {
        auto stream = co_await l.Accept();
        auto socket = co_await WebSocket::Accept(*stream);
        if (!socket) co_return 0;
        for (;;) {
            auto message = co_await socket->Receive();
            if (!message) co_return socket->CloseCode();
            if (message->binary) co_await socket->Send(std::span<const std::byte>(message->data));
            else co_await socket->Send("echo " + message->Text());
        }
    }(listener);

    auto socket = Await(WebSocket::Connect(std::format("ws://127.0.0.1:{}/chat", listener.Port()), {.protocols = {"chat"}}));
    ASSERT_TRUE(socket) << socket.error().message;
    EXPECT_EQ(socket->Protocol(), "chat");
    ASSERT_TRUE(Await(socket->Send("hi")));
    auto text = Await(socket->Receive());
    ASSERT_TRUE(text) << text.error().message;
    EXPECT_FALSE(text->binary);
    EXPECT_EQ(text->Text(), "echo hi");

    Bytes blob(70000);   // a 64-bit length
    for (std::size_t i = 0; i < blob.size(); ++i) blob[i] = std::byte(i * 7);
    ASSERT_TRUE(Await(socket->Send(std::span<const std::byte>(blob))));
    auto binary = Await(socket->Receive());
    ASSERT_TRUE(binary);
    EXPECT_TRUE(binary->binary);
    EXPECT_EQ(binary->data, blob);

    ASSERT_TRUE(Await(socket->Close(4000, "bye")));
    auto after = Await(socket->Receive());   // the server's answering close
    ASSERT_FALSE(after);
    EXPECT_EQ(after.error().code, kor::ErrorCode::eConnectionClosed);
    EXPECT_EQ(socket->CloseCode(), 4000);
    server.Wait();
    EXPECT_EQ(std::move(server.Take()).value(), 4000);
}

TEST(WebSockets, FragmentsAndPingsFromARawPeer) {
    // A "server" written by hand, so it can send what our own never does: fragments, pings, a bad frame.
    auto listener = TcpListener::Listen(0, "127.0.0.1").value();
    // Fragmentation is exercised from the client side: two fragments and a ping between them, sent raw to Accept.
    auto acceptor = [](TcpListener l) -> kor::Task<std::string> {
        auto stream = co_await l.Accept();
        auto socket = co_await WebSocket::Accept(*stream);
        if (!socket) co_return "no socket";
        auto message = co_await socket->Receive();
        co_return message ? message->Text() : message.error().message;
    }(listener);

    auto raw = Await(TcpStream::Connect("127.0.0.1", listener.Port()));
    ASSERT_TRUE(raw);
    Await(raw->Write("GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"));
    auto answer = Await(raw->ReadUntil("\r\n\r\n"));
    ASSERT_TRUE(answer);
    EXPECT_NE(answer->find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), std::string::npos);   // RFC 6455's own example
    auto masked = [](std::uint8_t first, std::string_view payload) {
        std::string f;
        f += char(first);
        f += char(0x80 | payload.size());
        const char key[4] = {1, 2, 3, 4};
        f.append(key, 4);
        for (std::size_t i = 0; i < payload.size(); ++i) f += char(payload[i] ^ key[i & 3]);
        return f;
    };
    Await(raw->Write(masked(0x01, "frag") + masked(0x89, "p") + masked(0x80, "mented")));   // text (not fin), ping, continuation (fin)
    acceptor.Wait();
    EXPECT_EQ(std::move(acceptor.Take()).value(), "fragmented");
    // The ping was answered with a pong carrying its payload.
    std::byte pong[3];
    ASSERT_TRUE(Await(raw->ReadExactly(pong)));
    EXPECT_EQ(std::uint8_t(pong[0]), 0x8A);
    EXPECT_EQ(std::uint8_t(pong[2]), std::uint8_t('p'));
}
