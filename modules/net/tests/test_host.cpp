// The bit packer, reflected serialization, and the game protocol between two hosts in this process — over
// loopback, and through the simulator's loss, delay, reordering and duplication.

#include <gtest/gtest.h>

#include <functional>
#include <thread>

#include <knet/bits.h>
#include <knet/host.h>

using namespace knet;
using namespace std::chrono_literals;

namespace {
    struct Player {
        std::string name;
        kor::Vec3 position;
        kor::Quat rotation;
        std::int32_t score = 0;
        std::vector<std::uint16_t> items;
        bool alive = true;
    };
    KORAL_REFLECT(Player, name, position, rotation, score, items, alive)

    Bytes Text(std::string_view s) { auto b = AsBytes(s); return {b.begin(), b.end()}; }

    // Updates every host until `done` says so, or `limit` passes; collects their events.
    struct Pump {
        std::vector<Host*> hosts;
        std::vector<std::vector<Event>> events;
        explicit Pump(std::vector<Host*> h) : hosts(std::move(h)), events(hosts.size()) {}
        bool Until(const std::function<bool()>& done, std::chrono::milliseconds limit = 5000ms) {
            const auto end = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < end) {
                for (std::size_t i = 0; i < hosts.size(); ++i)
                    for (auto& e : hosts[i]->Update()) events[i].push_back(std::move(e));
                if (done()) return true;
                std::this_thread::sleep_for(1ms);
            }
            return false;
        }
        std::vector<Event> Of(std::size_t host, EventType type) const {
            std::vector<Event> out;
            for (const auto& e : events[host]) if (e.type == type) out.push_back(e);
            return out;
        }
    };
}

TEST(Bits, RoundTrip) {
    BitWriter w;
    w.WriteBits(5, 3);
    w.WriteBool(true);
    w.WriteVarUInt(300);
    w.WriteVarInt(-70000);
    w.WriteFloat(3.25f);
    w.WriteQuantized(0.5f, 0.f, 1.f, 8);
    w.WriteString("héllo");
    w.WriteQuat(kor::Quat::AngleAxis(1.f, kor::Vec3(1, 2, 3)));
    w.WriteU64(0x0123456789abcdefull);
    EXPECT_LT(w.Data().size(), 40u);

    BitReader r(w.Data());
    EXPECT_EQ(r.ReadBits(3), 5u);
    EXPECT_TRUE(r.ReadBool());
    EXPECT_EQ(r.ReadVarUInt(), 300u);
    EXPECT_EQ(r.ReadVarInt(), -70000);
    EXPECT_EQ(r.ReadFloat(), 3.25f);
    EXPECT_NEAR(r.ReadQuantized(0.f, 1.f, 8), 0.5f, 1.f / 255.f);
    EXPECT_EQ(r.ReadString(), "héllo");
    EXPECT_TRUE(kor::SameRotation(r.ReadQuat(), kor::Quat::AngleAxis(1.f, kor::Vec3(1, 2, 3)), 2e-3f));
    EXPECT_EQ(r.ReadU64(), 0x0123456789abcdefull);
    EXPECT_FALSE(r.Failed());
    r.ReadBits(32);   // past the end
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.ReadU32(), 0u);
}

TEST(Bits, ReflectedValuesAndHostileInput) {
    Player p{"ann", {1, 2, 3}, kor::Quat::AngleAxis(0.3f, kor::Vec3::Up()), -42, {7, 8, 9}, false};
    const Bytes bytes = Serialize(p);
    Player q;
    ASSERT_TRUE(Deserialize(bytes, q));
    EXPECT_EQ(q.name, "ann");
    EXPECT_EQ(q.position, p.position);
    EXPECT_EQ(q.rotation, p.rotation);
    EXPECT_EQ(q.score, -42);
    EXPECT_EQ(q.items, p.items);
    EXPECT_FALSE(q.alive);

    // Truncated, or claiming a huge array: refused, never a crash or a giant allocation.
    Player r;
    EXPECT_FALSE(Deserialize(std::span(bytes).first(bytes.size() / 2), r));
    BitWriter evil;
    evil.WriteString("x");
    evil.WriteFloat(0); evil.WriteFloat(0); evil.WriteFloat(0);
    for (int i = 0; i < 4; ++i) evil.WriteFloat(0);
    evil.WriteVarInt(0);
    evil.WriteVarUInt(1ull << 40);   // items: a trillion of them
    EXPECT_FALSE(Deserialize(evil.Data(), r));
}

TEST(Host, ConnectsAndDeliversOnEveryChannel) {
    auto server = Host::Create({.port = 0, .address = "127.0.0.1", .maxPeers = 4, .protocolId = 77}).value();
    auto client = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .protocolId = 77}).value();
    const PeerId toServer = client.Connect("127.0.0.1", server.Port());
    Pump pump({&server, &client});
    ASSERT_TRUE(pump.Until([&] { return client.Connected(toServer) && server.Peers().size() == 1; }));
    const PeerId toClient = server.Peers().front();
    EXPECT_EQ(server.Address(toClient).port, client.Port());

    Bytes big(200'000);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = std::byte(i * 31 + 7);
    ASSERT_TRUE(client.Send(toServer, 0, Text("first")));
    ASSERT_TRUE(client.Send(toServer, 0, big));
    ASSERT_TRUE(client.Send(toServer, 0, Text("third")));
    ASSERT_TRUE(client.Send(toServer, 1, Text("unreliable")));
    ASSERT_TRUE(client.Send(toServer, 2, Text("sequenced")));
    EXPECT_FALSE(client.Send(toServer, 1, big));   // too large to go unreliably
    EXPECT_FALSE(client.Send(toServer, 9, Text("x")));   // no such channel
    ASSERT_TRUE(pump.Until([&] { return pump.Of(0, EventType::eMessage).size() >= 5; }));
    const auto messages = pump.Of(0, EventType::eMessage);
    std::vector<Bytes> reliable;
    for (const auto& m : messages) if (m.channel == 0) reliable.push_back(m.data);
    ASSERT_EQ(reliable.size(), 3u);
    EXPECT_EQ(AsText(reliable[0]), "first");
    EXPECT_EQ(reliable[1], big);
    EXPECT_EQ(AsText(reliable[2]), "third");

    server.Broadcast(0, Text("to all"));
    ASSERT_TRUE(pump.Until([&] { return !pump.Of(1, EventType::eMessage).empty(); }));
    EXPECT_EQ(AsText(pump.Of(1, EventType::eMessage).front().data), "to all");
    EXPECT_GT(client.Stats(toServer).rtt, 0.f);

    client.Disconnect(toServer);
    ASSERT_TRUE(pump.Until([&] { return !pump.Of(0, EventType::eDisconnected).empty(); }));
    EXPECT_EQ(pump.Of(0, EventType::eDisconnected).front().reason, DisconnectReason::eRemote);
    EXPECT_EQ(pump.Of(1, EventType::eDisconnected).front().reason, DisconnectReason::eLocal);
}

TEST(Host, ReliableSurvivesABadNetwork) {
    const NetworkSimulation bad{.latency = 20ms, .jitter = 30ms, .loss = 0.2f, .duplicate = 0.1f};
    auto server = Host::Create({.address = "127.0.0.1", .simulation = bad}).value();
    auto client = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .simulation = bad}).value();
    const PeerId toServer = client.Connect("127.0.0.1", server.Port());
    Pump pump({&server, &client});
    ASSERT_TRUE(pump.Until([&] { return client.Connected(toServer) && !server.Peers().empty(); }, 10'000ms));

    constexpr int Count = 300;
    for (int i = 0; i < Count; ++i) {
        BitWriter w;
        w.WriteVarUInt(std::uint64_t(i));
        client.Send(toServer, 0, w.Data());
        client.Send(toServer, 2, w.Data());   // sequenced
        if (i == 150) client.Send(toServer, 0, Bytes(50'000, std::byte{0x5a}));   // a large one in the middle
        // Sent a few at a time, as a game sends them: all at once they are two or three datagrams, and a bad
        // network sometimes loses every one, leaving nothing sequenced to check.
        if (i % 10 == 9) pump.Until([] { return false; }, 2ms);
    }
    ASSERT_TRUE(pump.Until([&] {
        int n = 0;
        for (const auto& e : pump.events[0]) n += e.type == EventType::eMessage && e.channel == 0;
        return n == Count + 1;
    }, 30'000ms));

    int expected = 0;
    std::int64_t lastSequenced = -1;
    int sequencedCount = 0;
    for (const auto& e : pump.Of(0, EventType::eMessage)) {
        if (e.channel == 0) {
            if (e.data.size() == 50'000) { EXPECT_EQ(expected, 151); continue; }
            BitReader r(e.data);
            EXPECT_EQ(r.ReadVarUInt(), std::uint64_t(expected)) << "reliable messages arrive once, in order";
            ++expected;
        } else if (e.channel == 2) {
            BitReader r(e.data);
            const auto v = std::int64_t(r.ReadVarUInt());
            EXPECT_GT(v, lastSequenced) << "sequenced messages never go back";
            lastSequenced = v;
            ++sequencedCount;
        }
    }
    EXPECT_EQ(expected, Count);
    EXPECT_GT(sequencedCount, 0);
    // Unreliable ones may be lost, and none can arrive twice. That some are lost is not for this to insist on: the
    // 300 travel in a handful of datagrams, and one run in five loses none of those. packetsLost, below, counts
    // every datagram of the run, and says the network was bad.
    EXPECT_LE(sequencedCount, Count);
    EXPECT_GT(client.Stats(toServer).packetsLost, 0u);
}

TEST(Host, RefusesWrongProtocolsAndFullServers) {
    auto server = Host::Create({.address = "127.0.0.1", .maxPeers = 1, .protocolId = 1}).value();
    auto stranger = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .protocolId = 2}).value();
    auto first = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .protocolId = 1}).value();
    auto second = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .protocolId = 1}).value();
    stranger.Connect("127.0.0.1", server.Port());
    const PeerId a = first.Connect("127.0.0.1", server.Port());
    Pump pump({&server, &stranger, &first});
    ASSERT_TRUE(pump.Until([&] { return first.Connected(a) && !pump.Of(1, EventType::eDisconnected).empty(); }));
    EXPECT_EQ(pump.Of(1, EventType::eDisconnected).front().reason, DisconnectReason::eRefused);

    second.Connect("127.0.0.1", server.Port());
    Pump more({&server, &first, &second});
    ASSERT_TRUE(more.Until([&] { return !more.Of(2, EventType::eDisconnected).empty(); }));
    EXPECT_EQ(more.Of(2, EventType::eDisconnected).front().reason, DisconnectReason::eRefused);
    EXPECT_EQ(server.Peers().size(), 1u);
}

TEST(Host, TimesOutSilentPeersAndUnansweredConnects) {
    auto server = Host::Create({.address = "127.0.0.1", .timeout = 300ms}).value();
    auto client = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .timeout = 300ms, .connectTimeout = 300ms}).value();
    const PeerId toServer = client.Connect("127.0.0.1", server.Port());
    Pump both({&server, &client});
    ASSERT_TRUE(both.Until([&] { return client.Connected(toServer) && !server.Peers().empty(); }));
    Pump serverOnly({&server});   // the client goes quiet
    ASSERT_TRUE(serverOnly.Until([&] { return !serverOnly.Of(0, EventType::eDisconnected).empty(); }, 2000ms));
    EXPECT_EQ(serverOnly.Of(0, EventType::eDisconnected).front().reason, DisconnectReason::eTimedOut);

    // Nobody listening there.
    auto lonely = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .connectTimeout = 300ms}).value();
    auto nobody = UdpSocket::Bind(0, "127.0.0.1").value();
    lonely.Connect("127.0.0.1", nobody.Port());
    Pump alone({&lonely});
    ASSERT_TRUE(alone.Until([&] { return !alone.Of(0, EventType::eDisconnected).empty(); }, 2000ms));
    EXPECT_EQ(alone.Of(0, EventType::eDisconnected).front().reason, DisconnectReason::eFailed);
}
