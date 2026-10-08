// Sockets over loopback: TCP both ways, TLS with a self-signed certificate made here, UDP, timeouts, cancel.

#include <gtest/gtest.h>

#include <knet/socket.h>

#include "tls_identity.h"

using namespace knet;
using namespace std::chrono_literals;

namespace {
    // Runs a task to completion from a plain test thread and hands back what it produced.
    template<class T>
    T Await(kor::Task<T> task) {
        task.Wait();
        return std::move(task.Take()).value();
    }
}

TEST(Sockets, TcpEchoesBothWays) {
    auto listener = TcpListener::Listen(0, "127.0.0.1");
    ASSERT_TRUE(listener) << listener.error().message;
    const std::uint16_t port = listener->Port();
    ASSERT_NE(port, 0);

    auto server = [](TcpListener l) -> kor::Task<void> {
        auto client = co_await l.Accept();
        if (!client) co_return;
        auto line = co_await client->ReadUntil("\n");
        if (line) co_await client->Write("echo: " + *line);
        std::byte rest[5];
        if (co_await client->ReadExactly(rest)) co_await client->Write(std::span<const std::byte>(rest));
    }(*listener);

    auto stream = Await(TcpStream::Connect("127.0.0.1", port));
    ASSERT_TRUE(stream) << stream.error().message;
    EXPECT_FALSE(stream->Secure());
    EXPECT_EQ(stream->Remote().port, port);
    ASSERT_TRUE(Await(stream->Write("hello\n")));
    auto reply = Await(stream->ReadUntil("\n"));
    ASSERT_TRUE(reply) << reply.error().message;
    EXPECT_EQ(*reply, "echo: hello\n");

    ASSERT_TRUE(Await(stream->Write(AsBytes("12345"))));
    std::byte back[5];
    ASSERT_TRUE(Await(stream->ReadExactly(back)));
    EXPECT_EQ(AsText(back), "12345");
    server.Wait();

    // The server's end went away: the next read says so.
    std::byte one[1];
    auto closed = Await(stream->ReadSome(one));
    ASSERT_FALSE(closed);
    EXPECT_EQ(closed.error().code, kor::ErrorCode::eConnectionClosed);
}

TEST(Sockets, ConnectingToNothingFails) {
    auto listener = TcpListener::Listen(0, "127.0.0.1");
    const std::uint16_t port = listener->Port();
    listener->Close();
    std::this_thread::sleep_for(50ms);
    auto refused = Await(TcpStream::Connect("127.0.0.1", port));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, kor::ErrorCode::eNetwork);

    auto unknown = Await(Resolve("no-such-host.invalid", 80));
    EXPECT_FALSE(unknown);
}

TEST(Sockets, ReadsTimeOutAndCanBeCancelled) {
    auto listener = TcpListener::Listen(0, "127.0.0.1");
    auto accepted = listener->Accept();
    auto stream = Await(TcpStream::Connect("127.0.0.1", listener->Port()));
    ASSERT_TRUE(stream);
    accepted.Wait();

    std::byte b[1];
    auto timed = Await(stream->ReadSome(b, 50ms));
    ASSERT_FALSE(timed);
    EXPECT_EQ(timed.error().code, kor::ErrorCode::eTimedOut);

    auto pending = stream->ReadSome(b);
    std::this_thread::sleep_for(20ms);
    pending.Cancel();
    pending.Wait();
    EXPECT_TRUE(pending.IsCancelled());

    // Closing our own end fails what waits with eConnectionClosed.
    auto waiting = stream->ReadSome(b);
    stream->Close();
    waiting.Wait();
    auto result = std::move(waiting.Take()).value();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kor::ErrorCode::eConnectionClosed);
}

TEST(Sockets, TlsWithASelfSignedCertificate) {
    const auto identity = test::MakeSelfSignedIdentity("localhost");
    auto listener = TcpListener::Listen(0, "127.0.0.1", TlsServerOptions{identity.certificatePem, identity.privateKeyPem});
    ASSERT_TRUE(listener) << listener.error().message;

    auto server = [](TcpListener l) -> kor::Task<void> {
        // The untrusting client's failed handshake is skipped: one Accept is the trusting client.
        auto client = co_await l.Accept();
        if (!client) co_return;
        auto line = co_await client->ReadUntil("\n");
        if (line) co_await client->Write("secure " + *line);
    }(*listener);

    // Not trusted: the handshake fails.
    auto untrusted = Await(TcpStream::Connect("127.0.0.1", listener->Port(), {.tls = TlsOptions{.serverName = "localhost"}}));
    EXPECT_FALSE(untrusted);

    // Trusted as a root, and checked by name.
    auto stream = Await(TcpStream::Connect("127.0.0.1", listener->Port(),
                                           {.tls = TlsOptions{.serverName = "localhost", .caPem = identity.certificatePem}}));
    ASSERT_TRUE(stream) << stream.error().message;
    EXPECT_TRUE(stream->Secure());
    ASSERT_TRUE(Await(stream->Write("hi\n")));
    auto reply = Await(stream->ReadUntil("\n"));
    ASSERT_TRUE(reply) << reply.error().message;
    EXPECT_EQ(*reply, "secure hi\n");
    server.Wait();
}

TEST(Sockets, UdpPacketsArriveWhole) {
    auto a = UdpSocket::Bind(0, "127.0.0.1");
    auto b = UdpSocket::Bind(0, "127.0.0.1");
    ASSERT_TRUE(a && b);
    auto received = b->Receive(2000ms);
    ASSERT_TRUE(Await(a->SendTo({"127.0.0.1", b->Port()}, AsBytes("datagram"))));
    received.Wait();
    auto packet = std::move(received.Take()).value();
    ASSERT_TRUE(packet) << packet.error().message;
    EXPECT_EQ(AsText(packet->data), "datagram");
    EXPECT_EQ(packet->from.port, a->Port());

    auto nothing = Await(b->Receive(30ms));
    ASSERT_FALSE(nothing);
    EXPECT_EQ(nothing.error().code, kor::ErrorCode::eTimedOut);
}
