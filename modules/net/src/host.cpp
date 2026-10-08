// knet::Host: connections, acknowledgements and channels over one UDP socket, all on the caller's thread.
//
// Packets (little-endian), first byte the type:
//   Request    protocolId u64, clientSalt u64, padding   — padded so answering it never sends more than it got
//   Challenge  clientSalt u64, serverSalt u64
//   Response   key u64 (= clientSalt ^ serverSalt), padding
//   Accepted   key u64
//   Denied     clientSalt u64, reason u8
//   Payload    key u64, sequence u16, ack u16, ackBits u32, then messages to the end
//   Disconnect key u64
// A message: channel u8, then by the channel's delivery
//   reliable   id u16, last u8 (the last fragment of a message), length varuint, bytes
//   unreliable length varuint, bytes
//   sequenced  sequence u16, length varuint, bytes

#include "knet/host.h"

#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <map>
#include <random>

#include <asio.hpp>

#include <kmath/random.h>
#include <log.h>

#include "io.h"

namespace knet
{
    namespace detail
    {
        using asio::ip::udp;
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        constexpr std::size_t MaxPacket = 1200;       // stays under every path's MTU
        constexpr std::size_t FragmentSize = 1024;
        constexpr std::size_t SentHistory = 1024;
        constexpr std::uint16_t ReliableWindow = 512;  // messages in flight per channel
        constexpr std::size_t HandshakePadding = 100;
        constexpr auto HandshakeInterval = std::chrono::milliseconds(100);

        enum PacketType : std::uint8_t { eRequest = 1, eChallenge, eResponse, eAccepted, eDenied, ePayload, eDisconnectPacket };
        enum DenyReason : std::uint8_t { eFull = 1, eWrongProtocol = 2 };

        // Wrap-around order of 16-bit sequence numbers.
        bool Newer(std::uint16_t a, std::uint16_t b) { return std::uint16_t(a - b) != 0 && std::uint16_t(a - b) < 32768; }

        struct Out {
            Bytes bytes;
            void U8(std::uint8_t v) { bytes.push_back(std::byte(v)); }
            void U16(std::uint16_t v) { for (int i = 0; i < 2; ++i) bytes.push_back(std::byte(v >> (8 * i))); }
            void U32(std::uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(std::byte(v >> (8 * i))); }
            void U64(std::uint64_t v) { for (int i = 0; i < 8; ++i) bytes.push_back(std::byte(v >> (8 * i))); }
            void Var(std::uint64_t v) { do { const std::uint8_t g = v & 0x7f; v >>= 7; U8(g | (v ? 0x80 : 0)); } while (v); }
            void Raw(std::span<const std::byte> b) { bytes.insert(bytes.end(), b.begin(), b.end()); }
            void PadTo(std::size_t n) { if (bytes.size() < n) bytes.resize(n); }
        };
        constexpr std::size_t VarSize(std::uint64_t v) { std::size_t n = 1; while (v >>= 7) ++n; return n; }

        struct In {
            std::span<const std::byte> bytes;
            std::size_t at = 0;
            bool failed = false;
            bool Has(std::size_t n) { if (at + n > bytes.size()) failed = true; return !failed; }
            std::uint64_t Le(int n) { if (!Has(std::size_t(n))) return 0; std::uint64_t v = 0; for (int i = 0; i < n; ++i) v |= std::uint64_t(bytes[at + i]) << (8 * i); at += std::size_t(n); return v; }
            std::uint8_t U8() { return std::uint8_t(Le(1)); }
            std::uint16_t U16() { return std::uint16_t(Le(2)); }
            std::uint32_t U32() { return std::uint32_t(Le(4)); }
            std::uint64_t U64() { return Le(8); }
            std::uint64_t Var() {
                std::uint64_t v = 0;
                for (int shift = 0; shift < 64; shift += 7) { const std::uint8_t g = U8(); if (failed) return 0; v |= std::uint64_t(g & 0x7f) << shift; if (!(g & 0x80)) return v; }
                failed = true;
                return 0;
            }
            std::span<const std::byte> Take(std::size_t n) { if (!Has(n)) return {}; auto s = bytes.subspan(at, n); at += n; return s; }
            [[nodiscard]] bool Done() const { return at >= bytes.size(); }
        };

        struct ReliableOut {
            std::uint16_t id = 0;
            Bytes data;
            bool last = true;
            bool sent = false;
            bool acked = false;
            TimePoint sentAt{};
        };
        struct ReliableIn {
            Bytes data;
            bool last = true;
        };

        struct Channel {
            Delivery delivery;
            std::uint16_t nextId = 0;
            std::deque<ReliableOut> queue;
            std::uint16_t expected = 0;
            std::map<std::uint16_t, ReliableIn> early;   // by id: what arrived before its turn
            Bytes assembling;
            std::uint16_t nextSequence = 0;
            std::uint16_t lastSequence = 0;
            bool anySequence = false;
            std::vector<std::pair<std::uint16_t, Bytes>> unreliable;   // sequence (sequenced only), bytes
        };

        struct SentPacket {
            bool valid = false;
            bool acked = false;
            bool lost = false;
            std::uint16_t sequence = 0;
            TimePoint time{};
            std::vector<std::pair<std::uint8_t, std::uint16_t>> reliables;
        };

        struct Peer {
            enum class State { eConnecting, eChallenged, eConnected } state = State::eConnecting;
            PeerId id = 0;
            udp::endpoint endpoint;
            bool resolved = true;
            std::uint64_t clientSalt = 0, key = 0;
            TimePoint started{}, lastReceived{}, lastSent{}, lastHandshake{};
            std::uint16_t localSequence = 0;
            std::uint16_t remoteSequence = 0;
            std::uint32_t remoteBits = 0;
            bool anyRemote = false;
            bool ackPending = false;
            std::array<SentPacket, SentHistory> sent{};
            std::vector<Channel> channels;
            PeerStats stats;
            bool rttSampled = false;
            std::uint64_t windowSent = 0, windowReceived = 0;
            TimePoint windowStart{};
        };

        struct Pending {
            std::uint64_t clientSalt = 0, serverSalt = 0;
            TimePoint time{};
        };

        struct Delayed {
            TimePoint due;
            udp::endpoint to;
            Bytes bytes;
        };

        struct HostImpl {
            HostOptions options;
            asio::io_context context;   // never run: the socket is used synchronously, only by the caller's thread
            udp::socket socket{context};
            std::map<PeerId, Peer> peers;
            std::map<udp::endpoint, PeerId> byEndpoint;
            std::map<udp::endpoint, Pending> pending;
            std::vector<Event> events;
            PeerId nextId = 1;
            kor::Random random = kor::Random::FromEntropy();
            std::vector<Delayed> delayed;

            std::uint64_t Salt() { std::uint64_t s = 0; while (s == 0) s = random.NextU64(); return s; }

            void SendRaw(const udp::endpoint& to, Bytes bytes) {
                const auto& sim = options.simulation;
                if (!sim.Enabled()) return Transmit(to, bytes);
                if (sim.loss > 0.f && random.NextFloat() < sim.loss) return;
                const int copies = sim.duplicate > 0.f && random.NextFloat() < sim.duplicate ? 2 : 1;
                for (int c = 0; c < copies; ++c) {
                    auto delay = sim.latency + Duration(sim.jitter.count() > 0 ? random.NextInt(0, int(sim.jitter.count()) + 1) : 0);
                    delayed.push_back({Clock::now() + delay, to, bytes});
                }
            }

            void Transmit(const udp::endpoint& to, std::span<const std::byte> bytes) {
                asio::error_code ec;
                socket.send_to(asio::buffer(bytes.data(), bytes.size()), to, 0, ec);   // a full buffer drops it, as the network would
            }

            void ReleaseDelayed() {
                const auto now = Clock::now();
                std::vector<Delayed> later;
                for (auto& d : delayed) {
                    if (d.due <= now) Transmit(d.to, d.bytes);
                    else later.push_back(std::move(d));
                }
                delayed = std::move(later);
            }

            Peer& AddPeer(const udp::endpoint& endpoint) {
                const PeerId id = nextId++;
                Peer& peer = peers[id];
                peer.id = id;
                peer.endpoint = endpoint;
                peer.started = peer.lastReceived = peer.lastSent = peer.windowStart = Clock::now();
                for (const Delivery d : options.channels) peer.channels.push_back(Channel{d});
                byEndpoint[endpoint] = id;
                return peer;
            }

            void RemovePeer(PeerId id, DisconnectReason reason, bool notify) {
                const auto it = peers.find(id);
                if (it == peers.end()) return;
                if (notify && it->second.state == Peer::State::eConnected) {
                    Out out;
                    out.U8(eDisconnectPacket);
                    out.U64(it->second.key);
                    for (int i = 0; i < 3; ++i) Transmit(it->second.endpoint, out.bytes);   // no simulation: a goodbye must get there
                }
                if (const auto e = byEndpoint.find(it->second.endpoint); e != byEndpoint.end() && e->second == id) byEndpoint.erase(e);
                peers.erase(it);
                events.push_back({EventType::eDisconnected, id, 0, {}, reason});
            }

            void Connected(Peer& peer) {
                peer.state = Peer::State::eConnected;
                peer.lastReceived = Clock::now();
                events.push_back({EventType::eConnected, peer.id});
            }

            // ---- receiving ----

            void Receive() {
                std::array<std::byte, 2048> buffer;
                for (;;) {
                    udp::endpoint from;
                    asio::error_code ec;
                    const std::size_t n = socket.receive_from(asio::buffer(buffer), from, 0, ec);
                    if (ec == asio::error::would_block || ec == asio::error::try_again) return;
                    if (ec) continue;   // an ICMP "port unreachable" for an earlier send, on some systems: ignore
                    // An IPv6 socket sees IPv4 peers as v4-mapped: keep them as one form.
                    if (from.address().is_v6() && from.address().to_v6().is_v4_mapped())
                        from = udp::endpoint(asio::ip::make_address_v4(asio::ip::v4_mapped, from.address().to_v6()), from.port());
                    Handle(from, std::span(buffer.data(), n));
                }
            }

            Peer* PeerAt(const udp::endpoint& endpoint) {
                const auto it = byEndpoint.find(endpoint);
                return it == byEndpoint.end() ? nullptr : &peers.at(it->second);
            }

            void Handle(const udp::endpoint& from, std::span<const std::byte> packet) {
                In in{packet};
                const std::uint8_t type = in.U8();
                switch (type) {
                    case eRequest: {
                        const std::uint64_t protocol = in.U64(), clientSalt = in.U64();
                        if (in.failed || packet.size() < HandshakePadding || options.maxPeers == 0) return;
                        if (PeerAt(from)) return;   // already connected: a late duplicate
                        auto deny = [&](std::uint8_t why) {
                            Out out;
                            out.U8(eDenied);
                            out.U64(clientSalt);
                            out.U8(why);
                            SendRaw(from, std::move(out.bytes));
                        };
                        if (protocol != options.protocolId) return deny(eWrongProtocol);
                        if (peers.size() >= options.maxPeers) return deny(eFull);
                        if (pending.size() > 4096) return;   // a flood: let the challenges age out
                        auto& p = pending[from];
                        if (p.clientSalt != clientSalt) p = {clientSalt, Salt(), Clock::now()};
                        Out out;
                        out.U8(eChallenge);
                        out.U64(p.clientSalt);
                        out.U64(p.serverSalt);
                        SendRaw(from, std::move(out.bytes));
                        return;
                    }
                    case eChallenge: {
                        const std::uint64_t clientSalt = in.U64(), serverSalt = in.U64();
                        Peer* peer = PeerAt(from);
                        if (in.failed || !peer || peer->state != Peer::State::eConnecting || peer->clientSalt != clientSalt) return;
                        peer->state = Peer::State::eChallenged;
                        peer->key = clientSalt ^ serverSalt;
                        peer->lastHandshake = {};   // answer at once
                        return;
                    }
                    case eResponse: {
                        const std::uint64_t key = in.U64();
                        if (in.failed || packet.size() < HandshakePadding) return;
                        if (Peer* peer = PeerAt(from)) {
                            if (peer->key == key) SendAccepted(*peer);   // our Accepted was lost
                            return;
                        }
                        const auto it = pending.find(from);
                        if (it == pending.end() || (it->second.clientSalt ^ it->second.serverSalt) != key) return;
                        pending.erase(it);
                        if (peers.size() >= options.maxPeers) return;
                        Peer& peer = AddPeer(from);
                        peer.key = key;
                        Connected(peer);
                        SendAccepted(peer);
                        return;
                    }
                    case eAccepted: {
                        const std::uint64_t key = in.U64();
                        Peer* peer = PeerAt(from);
                        if (in.failed || !peer || peer->key != key || peer->state != Peer::State::eChallenged) return;
                        Connected(*peer);
                        return;
                    }
                    case eDenied: {
                        const std::uint64_t clientSalt = in.U64();
                        in.U8();
                        Peer* peer = PeerAt(from);
                        if (in.failed || !peer || peer->state == Peer::State::eConnected || peer->clientSalt != clientSalt) return;
                        const PeerId id = peer->id;
                        events.push_back({EventType::eDisconnected, id, 0, {}, DisconnectReason::eRefused});
                        byEndpoint.erase(from);
                        peers.erase(id);
                        return;
                    }
                    case eDisconnectPacket: {
                        const std::uint64_t key = in.U64();
                        Peer* peer = PeerAt(from);
                        if (!in.failed && peer && peer->key == key) RemovePeer(peer->id, DisconnectReason::eRemote, false);
                        return;
                    }
                    case ePayload: {
                        const std::uint64_t key = in.U64();
                        Peer* peer = PeerAt(from);
                        if (in.failed || !peer || peer->key != key) return;
                        if (peer->state == Peer::State::eChallenged) Connected(*peer);   // the Accepted was lost; this proves it
                        if (peer->state != Peer::State::eConnected) return;
                        Payload(*peer, in, packet.size());
                        return;
                    }
                    default:
                        return;
                }
            }

            void SendAccepted(Peer& peer) {
                Out out;
                out.U8(eAccepted);
                out.U64(peer.key);
                SendRaw(peer.endpoint, std::move(out.bytes));
            }

            void Payload(Peer& peer, In& in, std::size_t size) {
                const std::uint16_t sequence = in.U16(), ack = in.U16();
                const std::uint32_t ackBits = in.U32();
                if (in.failed) return;
                const auto now = Clock::now();
                peer.lastReceived = now;
                ++peer.stats.packetsReceived;
                peer.windowReceived += size;
                peer.ackPending = true;

                // Which of ours they have.
                Acknowledge(peer, ack, now);
                for (int i = 0; i < 32; ++i)
                    if (ackBits & (1u << i)) Acknowledge(peer, std::uint16_t(ack - 1 - i), now);
                // The packet that just left the window their acks cover was never acknowledged: it was lost.
                if (SentPacket& old = peer.sent[std::uint16_t(ack - 33) % SentHistory];
                    old.valid && old.sequence == std::uint16_t(ack - 33) && !old.acked && !old.lost) {
                    old.lost = true;
                    ++peer.stats.packetsLost;
                    peer.stats.packetLoss = peer.stats.packetLoss * 0.99f + 0.01f;
                }

                // Which of theirs we have; a duplicate packet's messages are not delivered twice.
                bool duplicate = false;
                if (!peer.anyRemote) {
                    peer.anyRemote = true;
                    peer.remoteSequence = sequence;
                    peer.remoteBits = 0;
                } else if (Newer(sequence, peer.remoteSequence)) {
                    const std::uint16_t shift = std::uint16_t(sequence - peer.remoteSequence);
                    peer.remoteBits = shift >= 32 ? 0 : peer.remoteBits << shift;
                    if (shift <= 32) peer.remoteBits |= 1u << (shift - 1);
                    peer.remoteSequence = sequence;
                } else {
                    const std::uint16_t back = std::uint16_t(peer.remoteSequence - sequence);
                    if (back == 0) duplicate = true;
                    else if (back <= 32) {
                        duplicate = (peer.remoteBits & (1u << (back - 1))) != 0;
                        peer.remoteBits |= 1u << (back - 1);
                    }
                }
                if (duplicate) return;

                while (!in.Done()) {
                    const std::uint8_t index = in.U8();
                    if (in.failed || index >= peer.channels.size()) return;
                    Channel& channel = peer.channels[index];
                    switch (channel.delivery) {
                        case Delivery::eReliable: {
                            const std::uint16_t id = in.U16();
                            const bool last = in.U8() != 0;
                            auto data = in.Take(std::size_t(in.Var()));
                            if (in.failed) return;
                            if (Newer(channel.expected, id) || std::uint16_t(id - channel.expected) >= ReliableWindow * 2) break;   // old, or absurd
                            channel.early.try_emplace(id, ReliableIn{Bytes(data.begin(), data.end()), last});
                            for (auto it = channel.early.find(channel.expected); it != channel.early.end(); it = channel.early.find(channel.expected)) {
                                channel.assembling.insert(channel.assembling.end(), it->second.data.begin(), it->second.data.end());
                                const bool complete = it->second.last;
                                channel.early.erase(it);
                                ++channel.expected;
                                if (complete) {
                                    events.push_back({EventType::eMessage, peer.id, index, std::move(channel.assembling)});
                                    channel.assembling.clear();
                                } else if (channel.assembling.size() > options.maxMessage) {
                                    return RemovePeer(peer.id, DisconnectReason::eRemote, true);   // a message larger than allowed: not a well-behaved peer
                                }
                            }
                            break;
                        }
                        case Delivery::eUnreliable: {
                            auto data = in.Take(std::size_t(in.Var()));
                            if (in.failed) return;
                            events.push_back({EventType::eMessage, peer.id, index, Bytes(data.begin(), data.end())});
                            break;
                        }
                        case Delivery::eUnreliableSequenced: {
                            const std::uint16_t seq = in.U16();
                            auto data = in.Take(std::size_t(in.Var()));
                            if (in.failed) return;
                            if (channel.anySequence && !Newer(seq, channel.lastSequence)) break;   // older than one delivered
                            channel.anySequence = true;
                            channel.lastSequence = seq;
                            events.push_back({EventType::eMessage, peer.id, index, Bytes(data.begin(), data.end())});
                            break;
                        }
                    }
                }
            }

            void Acknowledge(Peer& peer, std::uint16_t sequence, TimePoint now) {
                SentPacket& sent = peer.sent[sequence % SentHistory];
                if (!sent.valid || sent.sequence != sequence || sent.acked) return;
                sent.acked = true;
                const float sample = std::chrono::duration<float, std::milli>(now - sent.time).count();
                peer.stats.rtt = peer.rttSampled ? peer.stats.rtt + (sample - peer.stats.rtt) * 0.1f : sample;
                peer.rttSampled = true;
                peer.stats.packetLoss *= 0.99f;
                for (const auto& [index, id] : sent.reliables) {
                    auto& queue = peer.channels[index].queue;
                    for (auto& m : queue)
                        if (m.id == id) { m.acked = true; break; }
                    while (!queue.empty() && queue.front().acked) queue.pop_front();
                }
            }

            // ---- sending ----

            void Handshakes(TimePoint now) {
                std::vector<PeerId> failed;
                for (auto& [id, peer] : peers) {
                    if (peer.state == Peer::State::eConnected) continue;
                    if (!peer.resolved || now - peer.started > options.connectTimeout) { failed.push_back(id); continue; }
                    if (now - peer.lastHandshake < HandshakeInterval) continue;
                    peer.lastHandshake = now;
                    Out out;
                    if (peer.state == Peer::State::eConnecting) {
                        out.U8(eRequest);
                        out.U64(options.protocolId);
                        out.U64(peer.clientSalt);
                    } else {
                        out.U8(eResponse);
                        out.U64(peer.key);
                    }
                    out.PadTo(HandshakePadding);
                    SendRaw(peer.endpoint, std::move(out.bytes));
                }
                for (const PeerId id : failed) {
                    const auto endpoint = peers.at(id).endpoint;
                    if (const auto e = byEndpoint.find(endpoint); e != byEndpoint.end() && e->second == id) byEndpoint.erase(e);
                    peers.erase(id);
                    events.push_back({EventType::eDisconnected, id, 0, {}, DisconnectReason::eFailed});
                }
                std::erase_if(pending, [&](const auto& p) { return now - p.second.time > options.connectTimeout; });
            }

            void Packets(Peer& peer, TimePoint now) {
                const Duration resend = peer.rttSampled
                    ? std::clamp(Duration(std::int64_t(peer.stats.rtt * 1.25f) + 10), Duration(20), Duration(1000))
                    : Duration(100);
                Out packet;
                SentPacket record;
                bool any = false;
                int packetsThisUpdate = 0;

                auto begin = [&] {
                    packet = {};
                    packet.U8(ePayload);
                    packet.U64(peer.key);
                    packet.U16(peer.localSequence);
                    packet.U16(peer.remoteSequence);
                    packet.U32(peer.remoteBits);
                    record = {true, false, false, peer.localSequence, now, {}};
                };
                auto finish = [&] {
                    SentPacket& slot = peer.sent[peer.localSequence % SentHistory];
                    slot = std::move(record);
                    ++peer.localSequence;
                    ++peer.stats.packetsSent;
                    peer.windowSent += packet.bytes.size();
                    peer.lastSent = now;
                    peer.ackPending = false;
                    SendRaw(peer.endpoint, std::move(packet.bytes));
                    ++packetsThisUpdate;
                    any = false;
                };
                begin();
                auto room = [&](std::size_t n) {
                    if (packet.bytes.size() + n <= MaxPacket) return true;
                    finish();
                    begin();
                    return packetsThisUpdate < 256;   // a burst limit: the rest goes next update
                };

                for (std::size_t index = 0; index < peer.channels.size(); ++index) {
                    Channel& channel = peer.channels[index];
                    if (channel.delivery == Delivery::eReliable) {
                        const std::uint16_t first = channel.queue.empty() ? 0 : channel.queue.front().id;
                        for (auto& m : channel.queue) {
                            if (std::uint16_t(m.id - first) >= ReliableWindow) break;
                            if (m.acked || (m.sent && now - m.sentAt < resend)) continue;
                            const std::size_t size = 1 + 2 + 1 + VarSize(m.data.size()) + m.data.size();
                            if (!room(size)) return;
                            packet.U8(std::uint8_t(index));
                            packet.U16(m.id);
                            packet.U8(m.last ? 1 : 0);
                            packet.Var(m.data.size());
                            packet.Raw(m.data);
                            m.sent = true;
                            m.sentAt = now;
                            record.reliables.emplace_back(std::uint8_t(index), m.id);
                            any = true;
                        }
                    } else {
                        for (auto& [seq, data] : channel.unreliable) {
                            const bool sequenced = channel.delivery == Delivery::eUnreliableSequenced;
                            const std::size_t size = 1 + (sequenced ? 2 : 0) + VarSize(data.size()) + data.size();
                            if (!room(size)) break;
                            packet.U8(std::uint8_t(index));
                            if (sequenced) packet.U16(seq);
                            packet.Var(data.size());
                            packet.Raw(data);
                            any = true;
                        }
                        channel.unreliable.clear();
                    }
                }
                if (any || peer.ackPending || now - peer.lastSent >= options.keepAlive) finish();
            }

            void Flush() {
                const auto now = Clock::now();
                Handshakes(now);
                for (auto& [id, peer] : peers)
                    if (peer.state == Peer::State::eConnected) Packets(peer, now);
                ReleaseDelayed();
            }

            void Timeouts() {
                const auto now = Clock::now();
                std::vector<PeerId> gone;
                for (auto& [id, peer] : peers) {
                    if (peer.state == Peer::State::eConnected && now - peer.lastReceived > options.timeout) gone.push_back(id);
                    const float seconds = std::chrono::duration<float>(now - peer.windowStart).count();
                    if (seconds >= 1.f) {
                        peer.stats.sentBytesPerSecond = float(peer.windowSent) / seconds;
                        peer.stats.receivedBytesPerSecond = float(peer.windowReceived) / seconds;
                        peer.windowSent = peer.windowReceived = 0;
                        peer.windowStart = now;
                    }
                }
                for (const PeerId id : gone) RemovePeer(id, DisconnectReason::eTimedOut, false);
            }
        };
    }

    using detail::HostImpl;

    kor::Result<Host> Host::Create(HostOptions options) {
        auto impl = std::make_unique<HostImpl>();
        if (options.channels.empty() || options.channels.size() > 255)
            return std::unexpected(detail::MakeError(kor::ErrorCode::eInvalidArgument, "a host needs between 1 and 255 channels"));
        asio::error_code ec;
        const auto address = asio::ip::make_address(options.address, ec);
        if (ec) return std::unexpected(detail::MakeError(kor::ErrorCode::eInvalidArgument, std::format("'{}' is not a numeric address", options.address)));
        const asio::ip::udp::endpoint endpoint(address, options.port);
        impl->socket.open(endpoint.protocol(), ec);
        if (!ec && address.is_v6()) impl->socket.set_option(asio::ip::v6_only(false), ec);
        if (!ec) impl->socket.bind(endpoint, ec);
        if (!ec) impl->socket.non_blocking(true, ec);
        if (ec) return std::unexpected(detail::FromAsio(ec, std::format("binding {}", Endpoint{options.address, options.port}.ToString())));
        // Room for bursts: a server receiving from many clients between two updates.
        impl->socket.set_option(asio::socket_base::receive_buffer_size(1 << 21), ec);
        impl->socket.set_option(asio::socket_base::send_buffer_size(1 << 21), ec);
        impl->options = std::move(options);
        return Host(std::move(impl));
    }

    Host::Host(std::unique_ptr<HostImpl> impl) : _impl(std::move(impl)) {}
    Host::Host(Host&&) noexcept = default;
    Host& Host::operator=(Host&&) noexcept = default;

    Host::~Host() {
        if (!_impl) return;
        std::vector<PeerId> all;
        for (const auto& [id, peer] : _impl->peers) all.push_back(id);
        for (const PeerId id : all) _impl->RemovePeer(id, DisconnectReason::eLocal, true);
    }

    PeerId Host::Connect(const std::string& host, std::uint16_t port) {
        asio::ip::udp::resolver resolver(_impl->context);
        asio::error_code ec;
        const auto results = resolver.resolve(host, std::to_string(port), ec);
        asio::ip::udp::endpoint endpoint;
        bool found = false;
        if (!ec)
            for (const auto& entry : results) {
                // A socket bound to IPv4 reaches IPv4 addresses only; an IPv6 one, both (v4-mapped).
                const bool v6Socket = _impl->socket.local_endpoint().address().is_v6();
                auto a = entry.endpoint().address();
                if (a.is_v6() && !v6Socket) continue;
                endpoint = asio::ip::udp::endpoint(a, port);
                found = true;
                break;
            }
        detail::Peer& peer = _impl->AddPeer(found ? endpoint : asio::ip::udp::endpoint());
        peer.resolved = found;
        peer.clientSalt = _impl->Salt();
        if (!found) _impl->byEndpoint.erase(peer.endpoint);
        return peer.id;
    }

    bool Host::Send(PeerId id, std::uint8_t index, std::span<const std::byte> message) {
        const auto it = _impl->peers.find(id);
        if (it == _impl->peers.end() || it->second.state != detail::Peer::State::eConnected || index >= it->second.channels.size()) return false;
        detail::Channel& channel = it->second.channels[index];
        if (channel.delivery == Delivery::eReliable) {
            if (message.size() > _impl->options.maxMessage) return false;
            std::size_t at = 0;
            do {
                const std::size_t n = std::min(detail::FragmentSize, message.size() - at);
                channel.queue.push_back({channel.nextId++, Bytes(message.begin() + at, message.begin() + at + n), at + n == message.size()});
                at += n;
            } while (at < message.size());
            return true;
        }
        if (message.size() > detail::MaxPacket - 32) return false;   // an unreliable message must fit one packet
        channel.unreliable.emplace_back(channel.nextSequence++, Bytes(message.begin(), message.end()));
        return true;
    }

    void Host::Broadcast(std::uint8_t channel, std::span<const std::byte> message, PeerId except) {
        for (const PeerId id : Peers())
            if (id != except) Send(id, channel, message);
    }

    void Host::Disconnect(PeerId id) {
        if (!_impl->peers.contains(id)) return;
        _impl->Flush();
        _impl->RemovePeer(id, DisconnectReason::eLocal, true);
    }

    std::vector<Event> Host::Update() {
        _impl->Receive();
        _impl->Timeouts();
        _impl->Flush();
        return std::exchange(_impl->events, {});
    }

    void Host::Flush() { _impl->Flush(); }

    std::vector<PeerId> Host::Peers() const {
        std::vector<PeerId> ids;
        for (const auto& [id, peer] : _impl->peers)
            if (peer.state == detail::Peer::State::eConnected) ids.push_back(id);
        return ids;
    }

    bool Host::Connected(PeerId id) const {
        const auto it = _impl->peers.find(id);
        return it != _impl->peers.end() && it->second.state == detail::Peer::State::eConnected;
    }

    Endpoint Host::Address(PeerId id) const {
        const auto it = _impl->peers.find(id);
        if (it == _impl->peers.end()) return {};
        return {it->second.endpoint.address().to_string(), it->second.endpoint.port()};
    }

    PeerStats Host::Stats(PeerId id) const {
        const auto it = _impl->peers.find(id);
        return it == _impl->peers.end() ? PeerStats{} : it->second.stats;
    }

    std::uint16_t Host::Port() const {
        asio::error_code ec;
        return _impl->socket.local_endpoint(ec).port();
    }

    void Host::SetSimulation(const NetworkSimulation& simulation) { _impl->options.simulation = simulation; }
}
