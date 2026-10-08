#pragma once

// Replication: a server's objects, mirrored on its clients. The server owns the state; each tick it sends
// every client what changed since the last state that client acknowledged — per object, as a byte-level
// delta — so a quiet world costs nothing and a lost packet only delays an update.
//
// The state is bytes: any language serializes its objects its own way. In C++, a reflected type
// (KORAL_REFLECT) is serialized for you.
//
// @code
// // server, each tick
// replicator.SetState(id, player);                    // or Spawn(...) / Despawn(...)
// replicator.SendSnapshot(tick);
// for (auto& e : host.Update()) if (!replicator.Handle(e)) { /* the game's own */ }
//
// // client, each frame
// for (auto& e : host.Update()) if (!replicas.Handle(e)) { ... }
// for (auto& change : replicas.TakeChanges()) ...;   // spawned / despawned
// Player p; replicas.Interpolate(id, renderTick, p);  // between the two states around renderTick
// @endcode

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <reflect.h>

#include "bits.h"
#include "host.h"

namespace knet
{
    using NetId = std::uint32_t;

    struct ReplicationOptions {
        std::uint8_t controlChannel = 3;    ///< A reliable channel: spawns, despawns, states too large for a packet.
        std::uint8_t snapshotChannel = 4;   ///< An unreliable (sequenced) one: deltas, and the client's acknowledgements.
    };

    namespace detail { struct ReplicatorImpl; struct ReplicaSetImpl; }

    /** @brief The server side: the objects, and what each client has of them. */
    class KNET_API Replicator {
    public:
        explicit Replicator(Host& host, ReplicationOptions options = {});
        ~Replicator();
        Replicator(Replicator&&) noexcept;
        Replicator& operator=(Replicator&&) noexcept;

        /** @brief A new object of @p type (a name both sides agree on), owned by @p owner (0: the server). */
        NetId Spawn(std::string type, std::span<const std::byte> state, PeerId owner = 0);
        void SetState(NetId id, std::span<const std::byte> state);
        void Despawn(NetId id);
        [[nodiscard]] std::size_t Count() const;

        /** @brief A reflected object, its type named by its reflection. */
        template<class T> requires (!std::is_convertible_v<const T&, std::span<const std::byte>>)
        NetId Spawn(const T& object, PeerId owner = 0) { const Bytes state = Serialize(object); return Spawn(kor::TypeOf<T>().name, std::span<const std::byte>(state), owner); }
        template<class T> requires (!std::is_convertible_v<const T&, std::span<const std::byte>>)
        void SetState(NetId id, const T& object) { const Bytes state = Serialize(object); SetState(id, std::span<const std::byte>(state)); }

        /** @brief Sends every connected peer what it lacks of the world at @p tick. */
        void SendSnapshot(std::uint32_t tick);
        /** @brief Takes the replication's own messages (true); anything else is the game's (false). */
        bool Handle(const Event& event);
        /** @brief Bytes of snapshot sent to @p peer by the last SendSnapshot: what replication costs. */
        [[nodiscard]] std::size_t LastSnapshotBytes(PeerId peer) const;

    private:
        std::unique_ptr<detail::ReplicatorImpl> _impl;
    };

    /** @brief A replicated object, as a client has it. */
    struct Replica {
        NetId id = 0;
        std::string type;
        bool owned = false;          ///< This client's own (the server spawned it with this client as owner).
        Bytes state;                 ///< The latest.
        std::uint32_t tick = 0;      ///< The tick of the latest.
    };

    struct ReplicaChange {
        enum class Kind : std::uint8_t { eSpawned, eDespawned } kind;
        NetId id;
        std::string type;
    };

    /** @brief The client side: the server's objects, as they arrive. */
    class KNET_API ReplicaSet {
    public:
        ReplicaSet(Host& host, PeerId server, ReplicationOptions options = {});
        ~ReplicaSet();
        ReplicaSet(ReplicaSet&&) noexcept;
        ReplicaSet& operator=(ReplicaSet&&) noexcept;

        /** @brief Takes the replication's own messages (true); anything else is the game's (false). */
        bool Handle(const Event& event);
        /** @brief What was spawned and despawned since the last call. */
        std::vector<ReplicaChange> TakeChanges();

        [[nodiscard]] const Replica* Find(NetId id) const;
        [[nodiscard]] std::vector<NetId> Ids() const;
        /** @brief The newest tick any state has arrived for. */
        [[nodiscard]] std::uint32_t LatestTick() const;
        /**
         * @brief The two states around @p tick — the newest at or before it, the oldest after it — and how far
         *        between them it lies (0..1). Past the newest: the newest twice, 0.
         */
        struct Bracket { const Bytes* from; const Bytes* to; float alpha; };
        [[nodiscard]] std::optional<Bracket> StatesAround(NetId id, float tick) const;

        /** @brief The latest state of a reflected type. */
        template<class T> bool Get(NetId id, T& out) const {
            const Replica* r = Find(id);
            return r && Deserialize(r->state, out);
        }
        /** @brief A reflected object at @p tick: numbers, vectors and matrices lerped, quaternions slerped, the rest the earlier's. */
        template<class T> bool Interpolate(NetId id, float tick, T& out) const {
            const auto around = StatesAround(id, tick);
            if (!around) return false;
            T to;
            if (!Deserialize(*around->from, out) || !Deserialize(*around->to, to)) return false;
            Blend(kor::TypeOf<T>(), &out, &to, around->alpha);
            return true;
        }

        /** @brief out = blend of out and to by alpha, field by field (what Interpolate uses). */
        static void Blend(const kor::TypeInfo& type, void* out, const void* to, float alpha);

    private:
        std::unique_ptr<detail::ReplicaSetImpl> _impl;
    };

    /** @brief A delta of @p state against @p base: XOR, run-length coded. Exposed for the bindings' tests. */
    KNET_API Bytes EncodeDelta(std::span<const std::byte> base, std::span<const std::byte> state);
    KNET_API std::optional<Bytes> DecodeDelta(std::span<const std::byte> base, std::span<const std::byte> delta);
}
