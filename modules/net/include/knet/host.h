#pragma once

// A game's connection protocol over UDP: knet::Host is both ends — a server that accepts peers, a client
// that connects to one, or both at once.
//
// Each connection has channels, chosen per host:
//  - eReliable: every message arrives, once, in the order sent — however large (it is split and joined);
//  - eUnreliable: may be lost or arrive out of order, never late; for what the next update replaces anyway;
//  - eUnreliableSequenced: may be lost, never arrives older than one already delivered — positions, inputs.
//
// Host is polled, not threaded: Update() reads what arrived, resends what was lost, sends what was queued and
// returns what happened — call it once per tick from one thread. Nothing runs between calls.
//
// @code
// auto server = knet::Host::Create({.port = 7777, .maxPeers = 16}).value();
// auto client = knet::Host::Create({}).value();
// const knet::PeerId toServer = client.Connect("127.0.0.1", 7777);
// for (;;) {
//     for (const auto& e : server.Update())
//         if (e.type == knet::EventType::eMessage) server.Broadcast(0, e.data);
//     ...
// }
// @endcode
//
// Connections are protected against spoofed sources by a challenge, not encrypted: send nothing secret.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <error.h>

#include "socket.h"

namespace knet
{
    enum class Delivery : std::uint8_t { eReliable, eUnreliable, eUnreliableSequenced };

    /** A connection, as this host knows it. 0 is none. */
    using PeerId = std::uint32_t;

    /** @brief A bad network on demand, applied to what this host sends: test a game as players will play it. */
    struct NetworkSimulation {
        Duration latency{0};        ///< Added to every packet.
        Duration jitter{0};         ///< Plus up to this much more, at random — so packets also arrive out of order.
        float loss = 0.f;           ///< The chance a packet is dropped.
        float duplicate = 0.f;      ///< The chance a packet arrives twice.
        [[nodiscard]] bool Enabled() const { return latency.count() > 0 || jitter.count() > 0 || loss > 0.f || duplicate > 0.f; }
    };

    struct HostOptions {
        std::uint16_t port = 0;                 ///< 0: any free port (a client's usual choice).
        std::string address = "0.0.0.0";        ///< "::" for IPv6 (and IPv4).
        std::uint32_t maxPeers = 32;            ///< Incoming connections accepted; 0: a client only.
        std::uint64_t protocolId = 0;           ///< Both ends must agree: a game and version of it.
        /** 0–2 for the game; 3 and 4 are knet::Replicator's by default (ReplicationOptions), idle when it is not used. */
        std::vector<Delivery> channels = {Delivery::eReliable, Delivery::eUnreliable, Delivery::eUnreliableSequenced,
                                          Delivery::eReliable, Delivery::eUnreliableSequenced};
        Duration timeout = Duration(10'000);    ///< Silence after which a peer is gone.
        Duration connectTimeout = Duration(5'000);
        Duration keepAlive = Duration(250);     ///< An empty packet when nothing else has gone out for this long.
        std::size_t maxMessage = 1 << 20;       ///< The largest reliable message.
        NetworkSimulation simulation;
    };

    enum class EventType : std::uint8_t { eConnected, eDisconnected, eMessage };

    enum class DisconnectReason : std::uint8_t {
        eNone,
        eLocal,       ///< This host called Disconnect (or was destroyed).
        eRemote,      ///< The peer disconnected.
        eTimedOut,    ///< The peer went silent.
        eRefused,     ///< The server is full, or speaks another protocolId.
        eFailed,      ///< The address could not be resolved, or the connection never answered.
    };

    struct Event {
        EventType type = EventType::eMessage;
        PeerId peer = 0;
        std::uint8_t channel = 0;
        Bytes data;
        DisconnectReason reason = DisconnectReason::eNone;
    };

    struct PeerStats {
        float rtt = 0.f;                  ///< Round-trip time, smoothed, in milliseconds.
        float packetLoss = 0.f;           ///< The share of packets not acknowledged, smoothed.
        float sentBytesPerSecond = 0.f;
        float receivedBytesPerSecond = 0.f;
        std::uint64_t packetsSent = 0;
        std::uint64_t packetsReceived = 0;
        std::uint64_t packetsLost = 0;
    };

    namespace detail { struct HostImpl; }

    class KNET_API Host {
    public:
        static kor::Result<Host> Create(HostOptions options = {});

        Host(Host&&) noexcept;
        Host& operator=(Host&&) noexcept;
        /** @brief Says goodbye to every peer. */
        ~Host();

        /** @brief Starts connecting to a server; eConnected (or eDisconnected with eFailed/eRefused) follows from Update. */
        PeerId Connect(const std::string& host, std::uint16_t port);
        /** @brief Queues @p message on @p channel; false when it cannot be sent (no such peer or channel, too large). */
        bool Send(PeerId peer, std::uint8_t channel, std::span<const std::byte> message);
        /** @brief Send to every connected peer but @p except. */
        void Broadcast(std::uint8_t channel, std::span<const std::byte> message, PeerId except = 0);
        void Disconnect(PeerId peer);

        /** @brief Receives, resends, sends, and returns what happened since the last call. */
        std::vector<Event> Update();
        /** @brief Sends what is queued now, without receiving. */
        void Flush();

        [[nodiscard]] std::vector<PeerId> Peers() const;
        [[nodiscard]] bool Connected(PeerId peer) const;
        [[nodiscard]] Endpoint Address(PeerId peer) const;
        [[nodiscard]] PeerStats Stats(PeerId peer) const;
        [[nodiscard]] std::uint16_t Port() const;
        void SetSimulation(const NetworkSimulation& simulation);

    private:
        explicit Host(std::unique_ptr<detail::HostImpl> impl);
        std::unique_ptr<detail::HostImpl> _impl;
    };
}
