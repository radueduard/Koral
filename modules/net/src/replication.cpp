// Replication over a Host's two channels.
//
// Control (reliable, server → client): kind u8, then
//   1 Spawn      id, type, owned u8, tick u32, state
//   2 Despawn    id
//   4 State      id, tick u32, state                 — one too large for a snapshot packet
// Snapshot (unreliable, server → client):
//   3 Snapshot   packet u32, tick u32, count, then per object: id, baseTick u32 (0: none), delta
// Acknowledgement (unreliable, client → server):
//   5 Ack        count, then per packet: packet u32, count, the ids it could not apply
// Ids and counts are varuints, states and deltas length-prefixed byte strings (BitWriter's forms, byte-aligned).
//
// The server keeps, per client, the last state of each object the client acknowledged, and sends a delta
// against it; the client keeps a few recent states per object to undo deltas against, and acknowledges each
// snapshot packet (several times over, the last few at once, since acks travel unreliably) — naming what it
// could not apply (an object it has no spawn for yet, a baseline it no longer has), so that is never taken as
// a baseline.

#include "knet/replication.h"

#include <algorithm>
#include <deque>
#include <set>

namespace knet
{
    namespace
    {
        enum Kind : std::uint8_t { eSpawn = 1, eDespawn = 2, eSnapshot = 3, eState = 4, eAck = 5 };
        constexpr std::size_t SnapshotBudget = 1100;   // what fits one unreliable message
        constexpr std::size_t History = 64;            // states kept per object, client side
        constexpr std::size_t AckRedundancy = 8;
    }

    Bytes EncodeDelta(std::span<const std::byte> base, std::span<const std::byte> state) {
        BitWriter w;
        w.WriteVarUInt(state.size());
        std::size_t i = 0;
        auto x = [&](std::size_t k) { return state[k] ^ (k < base.size() ? base[k] : std::byte{0}); };
        const std::size_t n = state.size();
        while (i < n) {
            std::size_t zeros = 0;
            while (i + zeros < n && x(i + zeros) == std::byte{0}) ++zeros;
            if (i + zeros == n) break;   // trailing zeros: implied
            const std::size_t start = i + zeros;
            std::size_t j = start;
            // A literal run goes on through short stretches of zeros (cheaper kept than split), and ends at 3 or more.
            while (j < n) {
                if (x(j) != std::byte{0}) { ++j; continue; }
                std::size_t z = 0;
                while (j + z < n && x(j + z) == std::byte{0}) ++z;
                if (z >= 3 || j + z == n) break;
                j += z;
            }
            w.WriteVarUInt(zeros);
            w.WriteVarUInt(j - start);
            for (std::size_t k = start; k < j; ++k) w.WriteU8(std::uint8_t(x(k)));
            i = j;
        }
        return w.Data();
    }

    std::optional<Bytes> DecodeDelta(std::span<const std::byte> base, std::span<const std::byte> delta) {
        BitReader r(delta);
        const std::uint64_t size = r.ReadVarUInt();
        if (r.Failed() || size > (std::uint64_t(1) << 26)) return std::nullopt;
        Bytes out(size);
        for (std::size_t k = 0; k < out.size() && k < base.size(); ++k) out[k] = base[k];
        std::size_t i = 0;
        while (r.BitsLeft() >= 8) {
            const std::uint64_t zeros = r.ReadVarUInt(), literal = r.ReadVarUInt();
            if (r.Failed() || i + zeros + literal > size) return std::nullopt;
            i += zeros;
            for (std::uint64_t k = 0; k < literal; ++k, ++i) out[i] ^= std::byte(r.ReadU8());
        }
        if (r.Failed()) return std::nullopt;
        return out;
    }

    // ---- server -------------------------------------------------------------------------------------

    namespace detail
    {
        struct ReplicatorImpl {
            struct Object {
                std::string type;
                PeerId owner = 0;
                Bytes state;
            };
            struct Sent {
                NetId id;
                std::uint32_t tick;
                Bytes state;
            };
            struct View {
                std::set<NetId> spawned;
                std::map<NetId, std::pair<std::uint32_t, Bytes>> acked;   // the baseline: tick, state
                std::map<std::uint32_t, std::vector<Sent>> inFlight;      // by snapshot packet
                std::uint32_t nextPacket = 1;
                std::size_t lastBytes = 0;
            };

            Host* host;
            ReplicationOptions options;
            std::map<NetId, Object> objects;
            std::map<PeerId, View> views;
            NetId nextId = 1;

            void Control(PeerId peer, const BitWriter& w) { host->Send(peer, options.controlChannel, w.Data()); }
        };
    }

    Replicator::Replicator(Host& host, ReplicationOptions options) : _impl(std::make_unique<detail::ReplicatorImpl>()) {
        _impl->host = &host;
        _impl->options = options;
    }
    Replicator::~Replicator() = default;
    Replicator::Replicator(Replicator&&) noexcept = default;
    Replicator& Replicator::operator=(Replicator&&) noexcept = default;

    NetId Replicator::Spawn(std::string type, std::span<const std::byte> state, PeerId owner) {
        const NetId id = _impl->nextId++;
        _impl->objects[id] = {std::move(type), owner, Bytes(state.begin(), state.end())};
        return id;
    }

    void Replicator::SetState(NetId id, std::span<const std::byte> state) {
        if (const auto it = _impl->objects.find(id); it != _impl->objects.end()) it->second.state.assign(state.begin(), state.end());
    }

    void Replicator::Despawn(NetId id) {
        if (!_impl->objects.erase(id)) return;
        for (auto& [peer, view] : _impl->views) {
            if (!view.spawned.erase(id)) continue;
            view.acked.erase(id);
            BitWriter w;
            w.WriteU8(eDespawn);
            w.WriteVarUInt(id);
            _impl->Control(peer, w);
        }
    }

    std::size_t Replicator::Count() const { return _impl->objects.size(); }

    void Replicator::SendSnapshot(std::uint32_t tick) {
        auto& impl = *_impl;
        for (const PeerId peer : impl.host->Peers()) {
            auto& view = impl.views[peer];
            view.lastBytes = 0;
            // Spawns first, on the reliable channel; their snapshot entries may still overtake them, and are then
            // reported as not applied.
            for (const auto& [id, object] : impl.objects) {
                if (view.spawned.contains(id)) continue;
                BitWriter w;
                w.WriteU8(eSpawn);
                w.WriteVarUInt(id);
                w.WriteString(object.type);
                w.WriteU8(object.owner == peer ? 1 : 0);
                w.WriteU32(tick);
                w.WriteBytes(object.state);
                impl.Control(peer, w);
                view.spawned.insert(id);
                view.lastBytes += w.Data().size();
            }

            BitWriter packet;
            std::vector<detail::ReplicatorImpl::Sent> contents;
            std::size_t count = 0;
            BitWriter entries;
            auto flush = [&] {
                if (count == 0) return;
                const std::uint32_t number = view.nextPacket++;
                BitWriter w;
                w.WriteU8(eSnapshot);
                w.WriteU32(number);
                w.WriteU32(tick);
                w.WriteVarUInt(count);
                Bytes message = w.Data();
                message.insert(message.end(), entries.Data().begin(), entries.Data().end());
                impl.host->Send(peer, impl.options.snapshotChannel, message);
                view.lastBytes += message.size();
                view.inFlight[number] = std::move(contents);
                contents.clear();
                entries.Clear();
                count = 0;
                while (!view.inFlight.empty() && view.inFlight.begin()->first + 256 < number) view.inFlight.erase(view.inFlight.begin());
            };

            for (const auto& [id, object] : impl.objects) {
                const auto base = view.acked.find(id);
                if (base != view.acked.end() && base->second.second == object.state) continue;   // they have it
                const std::span<const std::byte> baseState = base != view.acked.end() ? std::span<const std::byte>(base->second.second) : std::span<const std::byte>();
                const Bytes delta = EncodeDelta(baseState, object.state);
                BitWriter entry;
                entry.WriteVarUInt(id);
                entry.WriteU32(base != view.acked.end() ? base->second.first : 0);
                entry.WriteBytes(delta);
                if (entry.Data().size() + 16 > SnapshotBudget) {   // too large for any packet: the reliable channel takes it
                    BitWriter w;
                    w.WriteU8(eState);
                    w.WriteVarUInt(id);
                    w.WriteU32(tick);
                    w.WriteBytes(object.state);
                    impl.Control(peer, w);
                    view.lastBytes += w.Data().size();
                    continue;
                }
                if (entries.Data().size() + entry.Data().size() + 16 > SnapshotBudget) flush();
                for (const std::byte b : entry.Data()) entries.WriteU8(std::uint8_t(b));
                contents.push_back({id, tick, object.state});
                ++count;
            }
            flush();
        }
    }

    bool Replicator::Handle(const Event& event) {
        auto& impl = *_impl;
        if (event.type == EventType::eDisconnected) { impl.views.erase(event.peer); return false; }
        if (event.type != EventType::eMessage) return false;
        if (event.channel != impl.options.snapshotChannel && event.channel != impl.options.controlChannel) return false;
        BitReader r(event.data);
        if (r.ReadU8() != eAck) return true;
        auto& view = impl.views[event.peer];
        const std::uint64_t packets = r.ReadVarUInt();
        for (std::uint64_t p = 0; p < packets && !r.Failed(); ++p) {
            const std::uint32_t number = r.ReadU32();
            std::set<NetId> skipped;
            const std::uint64_t n = r.ReadVarUInt();
            for (std::uint64_t k = 0; k < n && !r.Failed(); ++k) skipped.insert(NetId(r.ReadVarUInt()));
            const auto it = view.inFlight.find(number);
            if (r.Failed() || it == view.inFlight.end()) continue;
            for (auto& sent : it->second) {
                if (skipped.contains(sent.id) || !impl.objects.contains(sent.id)) continue;
                auto& base = view.acked[sent.id];
                if (base.first <= sent.tick) base = {sent.tick, std::move(sent.state)};
            }
            view.inFlight.erase(it);
        }
        return true;
    }

    std::size_t Replicator::LastSnapshotBytes(PeerId peer) const {
        const auto it = _impl->views.find(peer);
        return it == _impl->views.end() ? 0 : it->second.lastBytes;
    }

    // ---- client -------------------------------------------------------------------------------------

    namespace detail
    {
        struct ReplicaSetImpl {
            struct Entry {
                Replica replica;
                std::map<std::uint32_t, Bytes> history;
            };
            Host* host;
            PeerId server;
            ReplicationOptions options;
            std::map<NetId, Entry> entries;
            std::vector<ReplicaChange> changes;
            std::deque<std::pair<std::uint32_t, std::vector<NetId>>> recent;   // acknowledged packets, resent
            std::uint32_t latestTick = 0;

            void Store(Entry& e, std::uint32_t tick, Bytes state) {
                if (tick >= e.replica.tick) { e.replica.tick = tick; e.replica.state = state; }
                e.history[tick] = std::move(state);
                while (e.history.size() > History) e.history.erase(e.history.begin());
                latestTick = std::max(latestTick, tick);
            }
        };
    }

    ReplicaSet::ReplicaSet(Host& host, PeerId server, ReplicationOptions options) : _impl(std::make_unique<detail::ReplicaSetImpl>()) {
        _impl->host = &host;
        _impl->server = server;
        _impl->options = options;
    }
    ReplicaSet::~ReplicaSet() = default;
    ReplicaSet::ReplicaSet(ReplicaSet&&) noexcept = default;
    ReplicaSet& ReplicaSet::operator=(ReplicaSet&&) noexcept = default;

    bool ReplicaSet::Handle(const Event& event) {
        auto& impl = *_impl;
        if (event.peer != impl.server || event.type != EventType::eMessage) return false;
        if (event.channel != impl.options.snapshotChannel && event.channel != impl.options.controlChannel) return false;
        BitReader r(event.data);
        switch (r.ReadU8()) {
            case eSpawn: {
                const NetId id = NetId(r.ReadVarUInt());
                std::string type = r.ReadString();
                const bool owned = r.ReadU8() != 0;
                const std::uint32_t tick = r.ReadU32();
                Bytes state = r.ReadBytes();
                if (r.Failed() || impl.entries.contains(id)) break;
                auto& e = impl.entries[id];
                e.replica = {id, type, owned, {}, 0};
                impl.Store(e, tick, std::move(state));
                impl.changes.push_back({ReplicaChange::Kind::eSpawned, id, std::move(type)});
                break;
            }
            case eDespawn: {
                const NetId id = NetId(r.ReadVarUInt());
                if (const auto it = impl.entries.find(id); !r.Failed() && it != impl.entries.end()) {
                    impl.changes.push_back({ReplicaChange::Kind::eDespawned, id, it->second.replica.type});
                    impl.entries.erase(it);
                }
                break;
            }
            case eState: {
                const NetId id = NetId(r.ReadVarUInt());
                const std::uint32_t tick = r.ReadU32();
                Bytes state = r.ReadBytes();
                if (const auto it = impl.entries.find(id); !r.Failed() && it != impl.entries.end()) impl.Store(it->second, tick, std::move(state));
                break;
            }
            case eSnapshot: {
                const std::uint32_t number = r.ReadU32(), tick = r.ReadU32();
                const std::uint64_t count = r.ReadVarUInt();
                std::vector<NetId> skipped;
                for (std::uint64_t k = 0; k < count && !r.Failed(); ++k) {
                    const NetId id = NetId(r.ReadVarUInt());
                    const std::uint32_t baseTick = r.ReadU32();
                    const Bytes delta = r.ReadBytes();
                    if (r.Failed()) break;
                    const auto it = impl.entries.find(id);
                    if (it == impl.entries.end()) { skipped.push_back(id); continue; }
                    std::span<const std::byte> base;
                    if (baseTick != 0) {
                        const auto b = it->second.history.find(baseTick);
                        if (b == it->second.history.end()) { skipped.push_back(id); continue; }
                        base = b->second;
                    }
                    auto state = DecodeDelta(base, delta);
                    if (!state) { skipped.push_back(id); continue; }
                    impl.Store(it->second, tick, std::move(*state));
                }
                if (r.Failed()) break;
                impl.recent.emplace_back(number, std::move(skipped));
                while (impl.recent.size() > AckRedundancy) impl.recent.pop_front();
                BitWriter ack;
                ack.WriteU8(eAck);
                ack.WriteVarUInt(impl.recent.size());
                for (const auto& [n, ids] : impl.recent) {
                    ack.WriteU32(n);
                    ack.WriteVarUInt(ids.size());
                    for (const NetId id : ids) ack.WriteVarUInt(id);
                }
                impl.host->Send(impl.server, impl.options.snapshotChannel, ack.Data());
                break;
            }
            default:
                break;
        }
        return true;
    }

    std::vector<ReplicaChange> ReplicaSet::TakeChanges() { return std::exchange(_impl->changes, {}); }

    const Replica* ReplicaSet::Find(NetId id) const {
        const auto it = _impl->entries.find(id);
        return it == _impl->entries.end() ? nullptr : &it->second.replica;
    }

    std::vector<NetId> ReplicaSet::Ids() const {
        std::vector<NetId> ids;
        for (const auto& [id, e] : _impl->entries) ids.push_back(id);
        return ids;
    }

    std::uint32_t ReplicaSet::LatestTick() const { return _impl->latestTick; }

    std::optional<ReplicaSet::Bracket> ReplicaSet::StatesAround(NetId id, float tick) const {
        const auto it = _impl->entries.find(id);
        if (it == _impl->entries.end() || it->second.history.empty()) return std::nullopt;
        const auto& h = it->second.history;
        auto after = h.upper_bound(std::uint32_t(std::max(0.f, std::floor(tick))));
        if (after == h.begin()) return Bracket{&after->second, &after->second, 0.f};   // before all of them: the oldest
        auto before = std::prev(after);
        if (after == h.end()) return Bracket{&before->second, &before->second, 0.f};
        const float span = float(after->first - before->first);
        return Bracket{&before->second, &after->second, std::clamp((tick - float(before->first)) / span, 0.f, 1.f)};
    }

    void ReplicaSet::Blend(const kor::TypeInfo& type, void* out, const void* to, float t) {
        using kor::TypeKind;
        auto floats = [&](int n) {
            float* a = static_cast<float*>(out);
            const float* b = static_cast<const float*>(to);
            for (int i = 0; i < n; ++i) a[i] = kor::Lerp(a[i], b[i], t);
        };
        switch (type.kind) {
            case TypeKind::eFloat: floats(1); break;
            case TypeKind::eDouble: { auto* a = static_cast<double*>(out); *a = *a + (*static_cast<const double*>(to) - *a) * double(t); break; }
            case TypeKind::eVec2: floats(2); break;
            case TypeKind::eVec3: floats(3); break;
            case TypeKind::eVec4: floats(4); break;
            case TypeKind::eMat4: floats(16); break;
            case TypeKind::eQuat: { auto* q = static_cast<kor::Quat*>(out); *q = kor::Slerp(*q, *static_cast<const kor::Quat*>(to), t); break; }
            case TypeKind::eStruct:
                for (const auto& field : type.fields)
                    Blend(*field.type, field.address(out), field.address(const_cast<void*>(to)), t);
                break;
            case TypeKind::eArray: {
                const std::size_t n = type.arraySize(out);
                if (n != type.arraySize(to)) break;
                for (std::size_t i = 0; i < n; ++i) Blend(*type.element, type.arrayAt(out, i), type.arrayAt(const_cast<void*>(to), i), t);
                break;
            }
            default:
                break;   // integers, strings, enums, booleans: the earlier state's
        }
    }
}
