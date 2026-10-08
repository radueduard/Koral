# Networking

`koral-net` (namespace `knet`, `#include <knet/knet.h>`) is a linked module: link `Koral::koral-net` and it is
there; a project that doesn't pays nothing for it — not even a thread. It has four layers:

| Header | What |
|---|---|
| `knet/socket.h` | `Resolve`, `TcpStream` (TCP, or TLS over it), `TcpListener`, `UdpSocket` |
| `knet/http.h` | `Url`, `Fetch` / `HttpGet` / `HttpPost`, `WebSocket` (client, and a server's handshake) |
| `knet/bits.h`, `knet/host.h` | `BitWriter`/`BitReader`; `Host`, a game's connection protocol over UDP |
| `knet/replication.h`, `knet/prediction.h` | `Replicator`/`ReplicaSet`; `TickClock`, `Predictor`, `InputBuffer` |

The same API is in C (`koralNet_c.h`), C# (`Koral.Net`) and Kotlin (`koral.net`, the `koral-net` artifact).

## Sockets, HTTP and WebSockets: awaited

Everything that waits returns a `kor::Task<kor::Result<T>>`: `co_await` it in a coroutine, or `Wait()` and
`Take()` it from ordinary code. A failure is a `kor::Error` — `eNetwork`, `eTimedOut`, `eConnectionClosed`,
`eProtocol` — never an exception. Cancelling the task (`Task::Cancel`) cancels the operation.

```cpp
kor::Task<void> Scores() {
    auto response = co_await knet::HttpGet("https://example.com/api/scores");
    if (!response) { kor::log::Error("{}", response.error().message); co_return; }
    if (response->Ok()) Show(response->Text());
}

kor::Task<void> Chat() {
    auto socket = co_await knet::WebSocket::Connect("wss://example.com/chat");
    if (!socket) co_return;
    co_await socket->Send("hello");
    while (auto message = co_await socket->Receive()) Print(message->Text());
    // Receive failed: closed (socket->CloseCode() says how) or broken.
}
```

The sockets live on koral-net's own I/O thread. A coroutine resumes where tokens resume it: on the main thread
(next frame) if it was there, on the background pool otherwise. Without an application, it resumes on the I/O
thread itself — so don't block (`Wait()`) inside a networking coroutine.

- TLS uses the system's trusted roots (and `TlsOptions::caPem` for a private CA); the name connected to is
  checked. `verifyPeer = false` is for testing only.
- `TcpListener::Listen(port, address, TlsServerOptions{cert, key})` accepts TLS; a client whose handshake fails
  is skipped, not returned as an error, so one bad client can't end an accept loop.
- `TcpStream` writes from several coroutines go out one after another, never interleaved.
- HTTP: one connection per request; chunked bodies, `gzip`/`deflate`, redirects (303, and 301/302 after a POST,
  become GETs; credentials aren't forwarded to another host); `HttpRequest::timeout` is for the whole exchange.

C#: `await TcpStream.Connect(...)`, `await Http.Get(url)`; failures throw `NetException` (its `Code`).
Kotlin: `suspend` functions — `TcpStream.connect(...)`, `Http.get(url)` — throwing `NetException`.

## The game protocol: `knet::Host`

```cpp
auto server = knet::Host::Create({.port = 7777, .maxPeers = 16, .protocolId = 0xC0FFEE}).value();
auto client = knet::Host::Create({.maxPeers = 0, .protocolId = 0xC0FFEE}).value();
const knet::PeerId toServer = client.Connect("game.example.com", 7777);

// every tick, on one thread:
for (const knet::Event& e : client.Update()) {
    switch (e.type) {
        case knet::EventType::eConnected:    break;
        case knet::EventType::eMessage:      Handle(e.channel, e.data); break;
        case knet::EventType::eDisconnected: Lost(e.reason); break;   // eTimedOut, eRefused, eRemote...
    }
}
client.Send(toServer, 1, input);   // goes out with the next Update (or Flush)
```

A `Host` is both ends: a server (`maxPeers` > 0), a client, or both. It is polled, not threaded — `Update()`
receives, resends what was lost, sends what was queued and returns what happened. Channels are chosen per host:

| Delivery | Guarantee | For |
|---|---|---|
| `eReliable` | arrives once, in order; any size (split and joined) | chat, spawns, RPCs |
| `eUnreliable` | may be lost or reordered, never late | effects, sounds |
| `eUnreliableSequenced` | may be lost, never older than one delivered | positions, inputs |

The defaults are 0 reliable, 1 unreliable, 2 sequenced — and 3 (reliable) and 4 (sequenced), which replication
uses unless told otherwise.

Connections are protected by a challenge (a spoofed address can't open one), not encrypted — send nothing secret
through a `Host`; use TLS for that. `HostOptions::simulation` (or `SetSimulation`) adds latency, jitter, loss and
duplication to what a host sends: test the game as players will play it. `Stats(peer)` gives round-trip time,
loss and bandwidth.

`BitWriter`/`BitReader` pack messages tightly (`WriteQuantized`, `WriteQuat` in 32 bits, varints). Their bytes are
the same in every language. In C++, `knet::Serialize(value)` / `Deserialize(bytes, value)` do a whole reflected
type (`KORAL_REFLECT`). A reader never reads past its data: malformed input makes it `Failed()`.

## Replication and prediction

The server owns the world; clients mirror it. Each object is a type name and state bytes; each tick, every client
is sent what changed since the last state it acknowledged — a byte-level delta per object — so a still world
costs nothing and a lost packet only delays an update.

```cpp
// server
knet::Replicator replicator(server);
const knet::NetId ship = replicator.Spawn(playerShip, /*owner*/ peer);   // reflected: serialized for you
replicator.SetState(ship, playerShip);
replicator.SendSnapshot(tick);
for (auto& e : server.Update()) if (!replicator.Handle(e)) { /* the game's own */ }

// client
knet::ReplicaSet replicas(client, toServer);
for (auto& e : client.Update()) if (!replicas.Handle(e)) { ... }
for (const auto& change : replicas.TakeChanges()) ...;              // spawned / despawned
Ship drawn;
replicas.Interpolate(ship, renderTick, drawn);                       // between the states around renderTick
```

`Interpolate` lerps numbers, vectors and matrices and slerps quaternions of a reflected type; C# and Kotlin get
the two states and the fraction (`StatesAround`) and blend their own.

For the player's own character, waiting a round trip for the server feels like lag. `Predictor<State, Input>`
applies each input at once, keeps the ones the server hasn't confirmed (`Pending`: send them every tick — they
are small, and packets get lost), and `Reconcile(tick, serverState)` rewinds to the server's state and replays
the rest. On the server, `InputBuffer<Input>` hands each client's inputs out in tick order and repeats the last
one when an input never arrived. `TickClock` runs the simulation at a fixed rate and gives the rendering's
interpolation fraction.
