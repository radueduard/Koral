// Replication between a server and a client host in this process, also through simulated loss; deltas;
// interpolation; and prediction with server reconciliation.

#include <gtest/gtest.h>

#include <functional>
#include <thread>

#include <kmath/random.h>
#include <knet/knet.h>

using namespace knet;
using namespace std::chrono_literals;

namespace {
    struct Ship {
        std::string name;
        kor::Vec3 position;
        kor::Quat rotation;
        float health = 100.f;
        std::int32_t kills = 0;
    };
    KORAL_REFLECT(Ship, name, position, rotation, health, kills)

    bool PumpUntil(std::vector<std::function<void()>> steps, const std::function<bool()>& done, std::chrono::milliseconds limit = 10'000ms) {
        const auto end = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < end) {
            for (auto& s : steps) s();
            if (done()) return true;
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
}

TEST(Replication, DeltasRoundTrip) {
    kor::Random random(5);
    for (int trial = 0; trial < 500; ++trial) {
        Bytes base(random.NextU32(300)), state(random.NextU32(300));
        for (auto& b : base) b = std::byte(random.NextU32(4) == 0 ? random.NextU32(256) : 0);
        state = base;
        state.resize(random.NextU32(300));
        for (std::uint32_t k = random.NextU32(20); k > 0 && !state.empty(); --k) state[random.NextU32(std::uint32_t(state.size()))] = std::byte(random.NextU32(256));
        const Bytes delta = EncodeDelta(base, state);
        const auto back = DecodeDelta(base, delta);
        ASSERT_TRUE(back);
        ASSERT_EQ(*back, state) << trial;
    }
    const Bytes big(1000, std::byte{7});
    EXPECT_LE(EncodeDelta(big, big).size(), 2u);   // nothing changed: a length and nothing else
    EXPECT_FALSE(DecodeDelta({}, Bytes{std::byte{5}, std::byte{0}, std::byte{9}}));   // runs past the end
}

TEST(Replication, MirrorsTheServersObjects) {
    const NetworkSimulation lossy{.latency = 10ms, .jitter = 10ms, .loss = 0.15f};
    auto serverHost = Host::Create({.address = "127.0.0.1", .simulation = lossy}).value();
    auto clientHost = Host::Create({.address = "127.0.0.1", .maxPeers = 0, .simulation = lossy}).value();
    const PeerId toServer = clientHost.Connect("127.0.0.1", serverHost.Port());
    Replicator replicator(serverHost);
    ReplicaSet replicas(clientHost, toServer);

    std::vector<Event> serverEvents;
    auto serverStep = [&] { for (auto& e : serverHost.Update()) if (!replicator.Handle(e)) serverEvents.push_back(e); };
    auto clientStep = [&] { for (auto& e : clientHost.Update()) replicas.Handle(e); };
    ASSERT_TRUE(PumpUntil({serverStep, clientStep}, [&] { return clientHost.Connected(toServer) && !serverHost.Peers().empty(); }));
    const PeerId client = serverHost.Peers().front();

    std::vector<Ship> ships(40);
    std::vector<NetId> ids;
    for (std::size_t i = 0; i < ships.size(); ++i) {
        ships[i].name = "ship " + std::to_string(i);
        ids.push_back(replicator.Spawn(ships[i], i == 3 ? client : 0));
    }
    std::uint32_t tick = 0;
    auto simulate = [&] {
        ++tick;
        for (std::size_t i = 0; i < ships.size(); i += 2) {   // half of them move
            ships[i].position.x += 1.f;
            replicator.SetState(ids[i], ships[i]);
        }
        replicator.SendSnapshot(tick);
    };
    ASSERT_TRUE(PumpUntil({simulate, serverStep, clientStep}, [&] { return tick >= 100; }, 20'000ms));

    // Stop moving; the client converges on the final state despite the loss.
    auto quiet = [&] { replicator.SendSnapshot(++tick); };
    ASSERT_TRUE(PumpUntil({quiet, serverStep, clientStep}, [&] {
        for (std::size_t i = 0; i < ships.size(); ++i) {
            Ship s;
            if (!replicas.Get(ids[i], s) || s.position != ships[i].position) return false;
        }
        return true;
    }));
    EXPECT_EQ(replicas.Ids().size(), ships.size());
    EXPECT_TRUE(replicas.Find(ids[3])->owned);
    EXPECT_FALSE(replicas.Find(ids[4])->owned);
    EXPECT_EQ(replicas.Find(ids[5])->type, "Ship");

    // Once the client has acknowledged everything, a still world costs nothing.
    ASSERT_TRUE(PumpUntil({quiet, serverStep, clientStep}, [&] { return replicator.LastSnapshotBytes(client) == 0; }));

    // A state too large for a packet goes on the reliable channel.
    ships[1].name = std::string(5000, 'x');
    replicator.SetState(ids[1], ships[1]);
    ASSERT_TRUE(PumpUntil({quiet, serverStep, clientStep}, [&] { Ship s; return replicas.Get(ids[1], s) && s.name.size() == 5000; }));

    // Despawns reach the client as changes.
    replicas.TakeChanges();
    replicator.Despawn(ids[7]);
    ASSERT_TRUE(PumpUntil({quiet, serverStep, clientStep}, [&] { return replicas.Find(ids[7]) == nullptr; }));
    const auto changes = replicas.TakeChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].kind, ReplicaChange::Kind::eDespawned);
    EXPECT_EQ(changes[0].id, ids[7]);
}

TEST(Replication, InterpolatesBetweenStates) {
    Ship a{"a", {0, 0, 0}, kor::Quat(), 100.f, 1};
    Ship b{"b", {10, 0, 0}, kor::Quat::AngleAxis(1.f, kor::Vec3::Up()), 50.f, 2};
    Ship out = a;
    ReplicaSet::Blend(kor::TypeOf<Ship>(), &out, &b, 0.25f);
    EXPECT_FLOAT_EQ(out.position.x, 2.5f);
    EXPECT_FLOAT_EQ(out.health, 87.5f);
    EXPECT_EQ(out.kills, 1);       // integers keep the earlier
    EXPECT_EQ(out.name, "a");
    EXPECT_TRUE(kor::SameRotation(out.rotation, kor::Quat::AngleAxis(0.25f, kor::Vec3::Up()), 1e-5f));
}

namespace {
    struct Body { float x = 0, v = 0; bool operator==(const Body&) const = default; };
    struct Input { float push = 0; };
    void Step(Body& b, const Input& in) { b.v += in.push; b.v *= 0.9f; b.x += b.v; }
}

TEST(Prediction, ReconcilesWithTheServer) {
    // The client predicts; the server, two ticks of "network" behind, applies the same inputs from its
    // buffer and answers with its state; the client's corrected state matches the server's every time.
    Predictor<Body, Input> client(Body{}, Step);
    InputBuffer<Input> buffer;
    Body server;
    std::deque<std::pair<std::uint32_t, Input>> wire;          // client → server, two ticks late
    std::deque<std::pair<std::uint32_t, Body>> replies;        // server → client
    kor::Random random(3);
    Body lastReply;
    for (std::uint32_t t = 1; t <= 200; ++t) {
        const Input in{random.NextFloat(-1.f, 1.f)};
        client.Apply(in);
        for (const auto& p : client.Pending()) wire.push_back(p);   // all unconfirmed, every tick
        // Deliver what is two ticks old, some of it twice, some lost.
        while (!wire.empty() && wire.front().first + 2 <= t) {
            if (random.NextFloat() > 0.2f) buffer.Add(wire.front().first, wire.front().second);
            wire.pop_front();
        }
        if (t > 2) {
            const std::uint32_t serverTick = t - 2;
            if (auto input = buffer.Take(serverTick)) Step(server, *input);
            replies.emplace_back(serverTick, server);
        }
        if (replies.size() > 1) {
            client.Reconcile(replies.front().first, replies.front().second);
            lastReply = replies.front().second;
            replies.pop_front();
        }
    }
    EXPECT_GT(client.Reconciliations(), 100u);
    EXPECT_LT(client.Pending().size(), 10u);
    EXPECT_GT(buffer.Received(), 150u);
    // The client is the server's last answer with its unconfirmed inputs replayed on it — wherever the server
    // had to repeat an input it lost, that is what the client now has too.
    Body replay = lastReply;
    for (const auto& [tick, in] : client.Pending()) Step(replay, in);
    EXPECT_FLOAT_EQ(replay.x, client.Current().x);
}

TEST(Prediction, TickClock) {
    TickClock clock(50.0);   // 20 ms
    EXPECT_EQ(clock.Advance(0.019), 0);
    EXPECT_EQ(clock.Advance(0.002), 1);
    EXPECT_NEAR(clock.Alpha(), 0.05f, 1e-3f);
    EXPECT_EQ(clock.Advance(0.1), 5);
    EXPECT_EQ(clock.Tick(), 6u);
    EXPECT_EQ(clock.Advance(10.0), 8);   // a stall: capped, not replayed
    EXPECT_EQ(clock.Alpha(), 0.f);
}
