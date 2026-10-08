// koralNet_c.h over knet. An operation is a coroutine started on the I/O thread (so nothing it waits for needs
// the main thread to resume it); its handle keeps what it produced for the getters.

#include "koralNet_c.h"

#include <capiInterop.h>

#include <cstring>
#include <limits>

#include <knet/knet.h>

#include "io.h"

using namespace knet;

struct KnetStream { TcpStream stream; std::string text; };
struct KnetListener { TcpListener listener; };
struct KnetUdp { UdpSocket socket; };
struct KnetWebSocket { WebSocket socket; std::string protocol; };
struct KnetBitWriter { BitWriter writer; };
struct KnetBitReader { Bytes data; BitReader reader{data}; std::string text; Bytes bytes; explicit KnetBitReader(Bytes d) : data(std::move(d)), reader(data) {} };
struct KnetHost { Host host; std::vector<Event> events; std::vector<PeerId> peers; std::string text; };
struct KnetReplicator { Replicator replicator; };
struct KnetReplicaSet { ReplicaSet replicas; std::vector<ReplicaChange> changes; std::vector<NetId> ids; };

struct KnetOp {
    kor::Task<void> task;
    std::optional<kor::Error> error;
    std::string errorText;
    Bytes bytes;
    std::vector<Endpoint> endpoints;
    std::optional<TcpStream> stream;
    std::optional<WebSocket> socket;
    std::optional<HttpResponse> response;
    bool binary = false;
};

namespace
{
    thread_local std::string scratch;

    template<class T, class Make, class Store>
    KnetOp* Launch(Make make, Store store, KoralToken** done) {
        auto* op = new KnetOp;
        op->task = [](KnetOp* op, Make make, Store store) -> kor::Task<void> {
            co_await detail::ToIoThread();
            auto result = co_await make();
            if (!result) op->error = std::move(result.error());
            else if constexpr (std::is_void_v<T>) store(*op);
            else store(*op, std::move(*result));
        }(op, std::move(make), std::move(store));
        if (done) *done = kor::capi::MakeToken(op->task.Completion());
        return op;
    }

    Duration Ms(uint32_t ms) { return Duration(ms); }

    TlsOptions Tls(const KnetTlsOptions& o) {
        return {o.verify_peer, o.server_name ? o.server_name : "", o.ca_pem ? o.ca_pem : ""};
    }

    std::vector<HttpHeader> Headers(const char* const* names, const char* const* values, size_t count) {
        std::vector<HttpHeader> headers;
        for (size_t i = 0; i < count; ++i) headers.push_back({names[i], values[i]});
        return headers;
    }

    Event ToEvent(const KnetEvent& e) {
        return {EventType(e.type), e.peer, uint8_t(e.channel), Bytes(reinterpret_cast<const std::byte*>(e.data), reinterpret_cast<const std::byte*>(e.data) + e.size),
                DisconnectReason(e.reason)};
    }

    std::span<const std::byte> Span(const uint8_t* data, size_t size) { return {reinterpret_cast<const std::byte*>(data), size}; }
}

extern "C" {

// ---- operations -------------------------------------------------------------------------------------

bool knet_op_ready(const KnetOp* op) { return op && op->task.Done(); }
bool knet_op_failed(const KnetOp* op) { return op && op->task.Done() && (op->error || op->task.Exception()); }
uint32_t knet_op_error_code(const KnetOp* op) { return op && op->error ? uint32_t(op->error->code) : 0; }
const char* knet_op_error(const KnetOp* op) {
    if (!op || !op->task.Done()) return "";
    auto* mutable_op = const_cast<KnetOp*>(op);
    if (op->error) mutable_op->errorText = op->error->message;
    else if (op->task.IsCancelled()) mutable_op->errorText = "cancelled";
    else if (op->task.Exception()) mutable_op->errorText = "failed";
    else mutable_op->errorText.clear();
    return op->errorText.c_str();
}
void knet_op_destroy(KnetOp* op) {
    if (!op) return;
    if (!op->task.Done()) {
        op->task.Cancel();
        op->task.Wait();
    }
    delete op;
}

const uint8_t* knet_op_bytes(const KnetOp* op, size_t* size) {
    if (!op) { if (size) *size = 0; return nullptr; }
    const Bytes& b = op->response ? op->response->body : op->bytes;
    if (size) *size = b.size();
    return reinterpret_cast<const uint8_t*>(b.data());
}
KnetStream* knet_op_take_stream(KnetOp* op) {
    if (!op || !op->stream) return nullptr;
    auto* s = new KnetStream{std::move(*op->stream), {}};
    op->stream.reset();
    return s;
}
KnetWebSocket* knet_op_take_websocket(KnetOp* op) {
    if (!op || !op->socket) return nullptr;
    auto* s = new KnetWebSocket{std::move(*op->socket), {}};
    op->socket.reset();
    return s;
}
const char* knet_op_address(const KnetOp* op, size_t index, uint16_t* port) {
    if (!op || index >= op->endpoints.size()) { if (port) *port = 0; return ""; }
    if (port) *port = op->endpoints[index].port;
    return op->endpoints[index].address.c_str();
}
size_t knet_op_count(const KnetOp* op) { return op ? op->endpoints.size() : 0; }
bool knet_op_binary(const KnetOp* op) { return op && op->binary; }
int32_t knet_op_status(const KnetOp* op) { return op && op->response ? op->response->status : 0; }
const char* knet_op_url(const KnetOp* op) { return op && op->response ? op->response->url.c_str() : ""; }
size_t knet_op_header_count(const KnetOp* op) { return op && op->response ? op->response->headers.size() : 0; }
const char* knet_op_header_name(const KnetOp* op, size_t i) { return i < knet_op_header_count(op) ? op->response->headers[i].name.c_str() : ""; }
const char* knet_op_header_value(const KnetOp* op, size_t i) { return i < knet_op_header_count(op) ? op->response->headers[i].value.c_str() : ""; }
const char* knet_op_header(const KnetOp* op, const char* name) {
    if (!op || !op->response || !name) return "";
    scratch = std::string(op->response->Header(name));
    return scratch.c_str();
}

// ---- sockets ----------------------------------------------------------------------------------------

KnetTlsOptions knet_tls_options_default(void) { return {true, nullptr, nullptr}; }

KnetOp* knet_resolve(const char* host, uint16_t port, KoralToken** done) {
    return Launch<std::vector<Endpoint>>([h = std::string(host ? host : ""), port] { return Resolve(h, port); },
                                         [](KnetOp& op, std::vector<Endpoint> e) { op.endpoints = std::move(e); }, done);
}

KnetOp* knet_tcp_connect(const char* host, uint16_t port, uint32_t timeout_ms, const KnetTlsOptions* tls, KoralToken** done) {
    ConnectOptions options{.timeout = Ms(timeout_ms)};
    if (tls) options.tls = Tls(*tls);
    return Launch<TcpStream>([h = std::string(host ? host : ""), port, options] { return TcpStream::Connect(h, port, options); },
                             [](KnetOp& op, TcpStream s) { op.stream = std::move(s); }, done);
}

KnetOp* knet_stream_read_some(KnetStream* stream, size_t max_bytes, uint32_t timeout_ms, KoralToken** done) {
    auto buffer = std::make_shared<Bytes>(max_bytes);
    return Launch<size_t>([s = stream->stream, buffer, timeout_ms]() mutable { return s.ReadSome(*buffer, Ms(timeout_ms)); },
                          [buffer](KnetOp& op, size_t n) { buffer->resize(n); op.bytes = std::move(*buffer); }, done);
}

KnetOp* knet_stream_read_exactly(KnetStream* stream, size_t bytes, uint32_t timeout_ms, KoralToken** done) {
    auto buffer = std::make_shared<Bytes>(bytes);
    return Launch<void>([s = stream->stream, buffer, timeout_ms]() mutable { return s.ReadExactly(*buffer, Ms(timeout_ms)); },
                        [buffer](KnetOp& op) { op.bytes = std::move(*buffer); }, done);
}

KnetOp* knet_stream_read_until(KnetStream* stream, const char* delimiter, size_t limit, uint32_t timeout_ms, KoralToken** done) {
    return Launch<std::string>([s = stream->stream, d = std::string(delimiter ? delimiter : "\n"), limit, timeout_ms]() mutable { return s.ReadUntil(d, limit, Ms(timeout_ms)); },
                               [](KnetOp& op, std::string text) { auto b = AsBytes(text); op.bytes.assign(b.begin(), b.end()); }, done);
}

KnetOp* knet_stream_read_to_end(KnetStream* stream, size_t limit, uint32_t timeout_ms, KoralToken** done) {
    return Launch<Bytes>([s = stream->stream, limit, timeout_ms]() mutable { return s.ReadToEnd(limit, Ms(timeout_ms)); },
                         [](KnetOp& op, Bytes b) { op.bytes = std::move(b); }, done);
}

KnetOp* knet_stream_write(KnetStream* stream, const uint8_t* data, size_t size, uint32_t timeout_ms, KoralToken** done) {
    auto copy = std::make_shared<Bytes>(Span(data, size).begin(), Span(data, size).end());
    return Launch<void>([s = stream->stream, copy, timeout_ms]() mutable { return s.Write(*copy, Ms(timeout_ms)); }, [](KnetOp&) {}, done);
}

void knet_stream_close(KnetStream* stream) { if (stream) stream->stream.Close(); }
bool knet_stream_secure(const KnetStream* stream) { return stream && stream->stream.Secure(); }
const char* knet_stream_local(const KnetStream* stream, uint16_t* port) {
    if (!stream) return "";
    const Endpoint e = stream->stream.Local();
    if (port) *port = e.port;
    const_cast<KnetStream*>(stream)->text = e.address;
    return stream->text.c_str();
}
const char* knet_stream_remote(const KnetStream* stream, uint16_t* port) {
    if (!stream) return "";
    const Endpoint e = stream->stream.Remote();
    if (port) *port = e.port;
    const_cast<KnetStream*>(stream)->text = e.address;
    return stream->text.c_str();
}
void knet_stream_destroy(KnetStream* stream) { delete stream; }

KnetListener* knet_listener_listen(uint16_t port, const char* address, const char* certificate_pem, const char* private_key_pem) {
    std::optional<TlsServerOptions> tls;
    if (certificate_pem && private_key_pem) tls = TlsServerOptions{certificate_pem, private_key_pem};
    auto listener = TcpListener::Listen(port, address ? address : "0.0.0.0", tls);
    if (!listener) { koral_set_last_error(listener.error().message.c_str()); return nullptr; }
    return new KnetListener{std::move(*listener)};
}
KnetOp* knet_listener_accept(KnetListener* listener, KoralToken** done) {
    return Launch<TcpStream>([l = listener->listener]() mutable { return l.Accept(); }, [](KnetOp& op, TcpStream s) { op.stream = std::move(s); }, done);
}
uint16_t knet_listener_port(const KnetListener* listener) { return listener ? listener->listener.Port() : 0; }
void knet_listener_close(KnetListener* listener) { if (listener) listener->listener.Close(); }
void knet_listener_destroy(KnetListener* listener) { delete listener; }

KnetUdp* knet_udp_bind(uint16_t port, const char* address) {
    auto socket = UdpSocket::Bind(port, address ? address : "0.0.0.0");
    if (!socket) { koral_set_last_error(socket.error().message.c_str()); return nullptr; }
    return new KnetUdp{std::move(*socket)};
}
KnetOp* knet_udp_receive(KnetUdp* socket, uint32_t timeout_ms, KoralToken** done) {
    return Launch<Datagram>([s = socket->socket, timeout_ms]() mutable { return s.Receive(Ms(timeout_ms)); },
                            [](KnetOp& op, Datagram d) { op.bytes = std::move(d.data); op.endpoints = {d.from}; }, done);
}
KnetOp* knet_udp_send_to(KnetUdp* socket, const char* address, uint16_t port, const uint8_t* data, size_t size, KoralToken** done) {
    auto copy = std::make_shared<Bytes>(Span(data, size).begin(), Span(data, size).end());
    return Launch<void>([s = socket->socket, to = Endpoint{address ? address : "", port}, copy]() mutable { return s.SendTo(to, *copy); }, [](KnetOp&) {}, done);
}
uint16_t knet_udp_port(const KnetUdp* socket) { return socket ? socket->socket.Port() : 0; }
void knet_udp_close(KnetUdp* socket) { if (socket) socket->socket.Close(); }
void knet_udp_destroy(KnetUdp* socket) { delete socket; }

// ---- HTTP and WebSockets ----------------------------------------------------------------------------

KnetHttpRequest knet_http_request_default(const char* url) {
    const HttpRequest d;
    return {nullptr, url, nullptr, nullptr, 0, nullptr, 0, uint32_t(d.timeout.count()), d.maxRedirects, knet_tls_options_default()};
}

KnetOp* knet_http_fetch(const KnetHttpRequest* r, KoralToken** done) {
    HttpRequest request;
    request.method = r->method ? r->method : "GET";
    request.url = r->url ? r->url : "";
    request.headers = Headers(r->header_names, r->header_values, r->header_count);
    request.body.assign(Span(r->body, r->body_size).begin(), Span(r->body, r->body_size).end());
    request.timeout = Ms(r->timeout_ms);
    request.maxRedirects = r->max_redirects;
    request.tls = Tls(r->tls);
    return Launch<HttpResponse>([request]() { return Fetch(request); }, [](KnetOp& op, HttpResponse response) { op.response = std::move(response); }, done);
}

KnetWebSocketOptions knet_websocket_options_default(void) {
    const WebSocketOptions d;
    return {nullptr, nullptr, 0, nullptr, 0, uint32_t(d.timeout.count()), knet_tls_options_default(), d.maxMessage};
}

KnetOp* knet_websocket_connect(const char* url, const KnetWebSocketOptions* o, KoralToken** done) {
    const KnetWebSocketOptions defaults = knet_websocket_options_default();
    if (!o) o = &defaults;
    WebSocketOptions options;
    options.headers = Headers(o->header_names, o->header_values, o->header_count);
    for (size_t i = 0; i < o->protocol_count; ++i) options.protocols.emplace_back(o->protocols[i]);
    options.timeout = Ms(o->timeout_ms);
    options.tls = Tls(o->tls);
    options.maxMessage = o->max_message;
    return Launch<WebSocket>([u = std::string(url ? url : ""), options] { return WebSocket::Connect(u, options); },
                             [](KnetOp& op, WebSocket s) { op.socket = std::move(s); }, done);
}

KnetOp* knet_websocket_accept(KnetStream* stream, size_t max_message, KoralToken** done) {
    TcpStream s = stream->stream;
    delete stream;
    return Launch<WebSocket>([s, max_message] { return WebSocket::Accept(s, max_message); }, [](KnetOp& op, WebSocket w) { op.socket = std::move(w); }, done);
}

KnetOp* knet_websocket_send_text(KnetWebSocket* socket, const char* text, KoralToken** done) {
    return Launch<void>([s = socket->socket, t = std::string(text ? text : "")]() mutable { return s.Send(std::string_view(t)); }, [](KnetOp&) {}, done);
}
KnetOp* knet_websocket_send_binary(KnetWebSocket* socket, const uint8_t* data, size_t size, KoralToken** done) {
    auto copy = std::make_shared<Bytes>(Span(data, size).begin(), Span(data, size).end());
    return Launch<void>([s = socket->socket, copy]() mutable { return s.Send(std::span<const std::byte>(*copy)); }, [](KnetOp&) {}, done);
}
KnetOp* knet_websocket_receive(KnetWebSocket* socket, KoralToken** done) {
    return Launch<WebSocketMessage>([s = socket->socket]() mutable { return s.Receive(); },
                                    [](KnetOp& op, WebSocketMessage m) { op.binary = m.binary; op.bytes = std::move(m.data); }, done);
}
KnetOp* knet_websocket_close(KnetWebSocket* socket, uint16_t code, const char* reason, KoralToken** done) {
    return Launch<void>([s = socket->socket, code, r = std::string(reason ? reason : "")]() mutable { return s.Close(code, r); }, [](KnetOp&) {}, done);
}
bool knet_websocket_open(const KnetWebSocket* socket) { return socket && socket->socket.Open(); }
uint16_t knet_websocket_close_code(const KnetWebSocket* socket) { return socket ? socket->socket.CloseCode() : 0; }
const char* knet_websocket_protocol(const KnetWebSocket* socket) {
    if (!socket) return "";
    const_cast<KnetWebSocket*>(socket)->protocol = socket->socket.Protocol();
    return socket->protocol.c_str();
}
void knet_websocket_destroy(KnetWebSocket* socket) { delete socket; }

// ---- bits -------------------------------------------------------------------------------------------

KnetBitWriter* knet_bit_writer_create(void) { return new KnetBitWriter; }
void knet_bit_writer_destroy(KnetBitWriter* w) { delete w; }
void knet_bit_writer_write_bits(KnetBitWriter* w, uint64_t value, int32_t bits) { w->writer.WriteBits(value, bits); }
void knet_bit_writer_write_var_uint(KnetBitWriter* w, uint64_t value) { w->writer.WriteVarUInt(value); }
void knet_bit_writer_write_var_int(KnetBitWriter* w, int64_t value) { w->writer.WriteVarInt(value); }
void knet_bit_writer_write_float(KnetBitWriter* w, float value) { w->writer.WriteFloat(value); }
void knet_bit_writer_write_double(KnetBitWriter* w, double value) { w->writer.WriteDouble(value); }
void knet_bit_writer_write_quantized(KnetBitWriter* w, float value, float min, float max, int32_t bits) { w->writer.WriteQuantized(value, min, max, bits); }
void knet_bit_writer_write_string(KnetBitWriter* w, const char* text) { w->writer.WriteString(text ? text : ""); }
void knet_bit_writer_write_bytes(KnetBitWriter* w, const uint8_t* data, size_t size) { w->writer.WriteBytes(Span(data, size)); }
void knet_bit_writer_write_quat(KnetBitWriter* w, const float q[4], int32_t bits) { w->writer.WriteQuat(kor::Quat(q[0], q[1], q[2], q[3]), bits); }
void knet_bit_writer_align(KnetBitWriter* w) { w->writer.Align(); }
const uint8_t* knet_bit_writer_data(const KnetBitWriter* w, size_t* size) {
    if (size) *size = w->writer.Data().size();
    return reinterpret_cast<const uint8_t*>(w->writer.Data().data());
}
size_t knet_bit_writer_bit_count(const KnetBitWriter* w) { return w->writer.BitCount(); }
void knet_bit_writer_clear(KnetBitWriter* w) { w->writer.Clear(); }

KnetBitReader* knet_bit_reader_create(const uint8_t* data, size_t size) { return new KnetBitReader(Bytes(Span(data, size).begin(), Span(data, size).end())); }
void knet_bit_reader_destroy(KnetBitReader* r) { delete r; }
uint64_t knet_bit_reader_read_bits(KnetBitReader* r, int32_t bits) { return r->reader.ReadBits(bits); }
uint64_t knet_bit_reader_read_var_uint(KnetBitReader* r) { return r->reader.ReadVarUInt(); }
int64_t knet_bit_reader_read_var_int(KnetBitReader* r) { return r->reader.ReadVarInt(); }
float knet_bit_reader_read_float(KnetBitReader* r) { return r->reader.ReadFloat(); }
double knet_bit_reader_read_double(KnetBitReader* r) { return r->reader.ReadDouble(); }
float knet_bit_reader_read_quantized(KnetBitReader* r, float min, float max, int32_t bits) { return r->reader.ReadQuantized(min, max, bits); }
const char* knet_bit_reader_read_string(KnetBitReader* r) { r->text = r->reader.ReadString(); return r->text.c_str(); }
const uint8_t* knet_bit_reader_read_bytes(KnetBitReader* r, size_t* size) {
    r->bytes = r->reader.ReadBytes();
    if (size) *size = r->bytes.size();
    return reinterpret_cast<const uint8_t*>(r->bytes.data());
}
void knet_bit_reader_read_quat(KnetBitReader* r, float q[4], int32_t bits) {
    const kor::Quat v = r->reader.ReadQuat(bits);
    q[0] = v.x; q[1] = v.y; q[2] = v.z; q[3] = v.w;
}
void knet_bit_reader_align(KnetBitReader* r) { r->reader.Align(); }
bool knet_bit_reader_failed(const KnetBitReader* r) { return r->reader.Failed(); }
size_t knet_bit_reader_bits_left(const KnetBitReader* r) { return r->reader.BitsLeft(); }

size_t knet_encode_delta(const uint8_t* base, size_t base_size, const uint8_t* state, size_t state_size, uint8_t* out, size_t capacity) {
    const Bytes delta = EncodeDelta(Span(base, base_size), Span(state, state_size));
    if (out && capacity >= delta.size()) std::memcpy(out, delta.data(), delta.size());
    return delta.size();
}
size_t knet_decode_delta(const uint8_t* base, size_t base_size, const uint8_t* delta, size_t delta_size, uint8_t* out, size_t capacity) {
    const auto state = DecodeDelta(Span(base, base_size), Span(delta, delta_size));
    if (!state) return std::numeric_limits<size_t>::max();
    if (out && capacity >= state->size()) std::memcpy(out, state->data(), state->size());
    return state->size();
}

// ---- the game protocol ------------------------------------------------------------------------------

KnetHostOptions knet_host_options_default(void) {
    const HostOptions d;
    return {d.port, nullptr, d.maxPeers, d.protocolId, nullptr, 0, uint32_t(d.timeout.count()), uint32_t(d.connectTimeout.count()),
            uint32_t(d.keepAlive.count()), d.maxMessage, {0, 0, 0.f, 0.f}};
}

KnetHost* knet_host_create(const KnetHostOptions* o) {
    const KnetHostOptions defaults = knet_host_options_default();
    if (!o) o = &defaults;
    HostOptions options;
    options.port = o->port;
    if (o->address) options.address = o->address;
    options.maxPeers = o->max_peers;
    options.protocolId = o->protocol_id;
    if (o->channels && o->channel_count) {
        options.channels.clear();
        for (size_t i = 0; i < o->channel_count; ++i) options.channels.push_back(Delivery(o->channels[i]));
    }
    options.timeout = Ms(o->timeout_ms);
    options.connectTimeout = Ms(o->connect_timeout_ms);
    options.keepAlive = Ms(o->keep_alive_ms);
    options.maxMessage = o->max_message;
    options.simulation = {Ms(o->simulation.latency_ms), Ms(o->simulation.jitter_ms), o->simulation.loss, o->simulation.duplicate};
    auto host = Host::Create(std::move(options));
    if (!host) { koral_set_last_error(host.error().message.c_str()); return nullptr; }
    return new KnetHost{std::move(*host), {}, {}, {}};
}
void knet_host_destroy(KnetHost* host) { delete host; }
uint32_t knet_host_connect(KnetHost* host, const char* address, uint16_t port) { return host->host.Connect(address ? address : "", port); }
bool knet_host_send(KnetHost* host, uint32_t peer, uint8_t channel, const uint8_t* data, size_t size) { return host->host.Send(peer, channel, Span(data, size)); }
void knet_host_broadcast(KnetHost* host, uint8_t channel, const uint8_t* data, size_t size, uint32_t except) { host->host.Broadcast(channel, Span(data, size), except); }
void knet_host_disconnect(KnetHost* host, uint32_t peer) { host->host.Disconnect(peer); }
size_t knet_host_update(KnetHost* host) {
    host->events = host->host.Update();
    return host->events.size();
}
bool knet_host_event(const KnetHost* host, size_t index, KnetEvent* event) {
    if (!host || index >= host->events.size() || !event) return false;
    const Event& e = host->events[index];
    *event = {uint32_t(e.type), e.peer, e.channel, uint32_t(e.reason), reinterpret_cast<const uint8_t*>(e.data.data()), e.data.size()};
    return true;
}
void knet_host_flush(KnetHost* host) { host->host.Flush(); }
size_t knet_host_peer_count(const KnetHost* host) {
    const_cast<KnetHost*>(host)->peers = host->host.Peers();
    return host->peers.size();
}
uint32_t knet_host_peer_at(const KnetHost* host, size_t index) {
    const auto peers = host->host.Peers();
    return index < peers.size() ? peers[index] : 0;
}
bool knet_host_connected(const KnetHost* host, uint32_t peer) { return host->host.Connected(peer); }
const char* knet_host_address(const KnetHost* host, uint32_t peer, uint16_t* port) {
    const Endpoint e = host->host.Address(peer);
    if (port) *port = e.port;
    const_cast<KnetHost*>(host)->text = e.address;
    return host->text.c_str();
}
KnetPeerStats knet_host_stats(const KnetHost* host, uint32_t peer) {
    const PeerStats s = host->host.Stats(peer);
    return {s.rtt, s.packetLoss, s.sentBytesPerSecond, s.receivedBytesPerSecond, s.packetsSent, s.packetsReceived, s.packetsLost};
}
uint16_t knet_host_port(const KnetHost* host) { return host->host.Port(); }
void knet_host_set_simulation(KnetHost* host, const KnetNetworkSimulation* s) {
    host->host.SetSimulation({Ms(s->latency_ms), Ms(s->jitter_ms), s->loss, s->duplicate});
}

// ---- replication ------------------------------------------------------------------------------------

KnetReplicator* knet_replicator_create(KnetHost* host, uint8_t control, uint8_t snapshot) {
    return new KnetReplicator{Replicator(host->host, {control, snapshot})};
}
void knet_replicator_destroy(KnetReplicator* r) { delete r; }
uint32_t knet_replicator_spawn(KnetReplicator* r, const char* type, const uint8_t* state, size_t size, uint32_t owner) {
    return r->replicator.Spawn(type ? type : "", Span(state, size), owner);
}
void knet_replicator_set_state(KnetReplicator* r, uint32_t id, const uint8_t* state, size_t size) { r->replicator.SetState(id, Span(state, size)); }
void knet_replicator_despawn(KnetReplicator* r, uint32_t id) { r->replicator.Despawn(id); }
size_t knet_replicator_count(const KnetReplicator* r) { return r->replicator.Count(); }
void knet_replicator_send_snapshot(KnetReplicator* r, uint32_t tick) { r->replicator.SendSnapshot(tick); }
bool knet_replicator_handle(KnetReplicator* r, const KnetEvent* event) { return event && r->replicator.Handle(ToEvent(*event)); }
size_t knet_replicator_last_snapshot_bytes(const KnetReplicator* r, uint32_t peer) { return r->replicator.LastSnapshotBytes(peer); }

KnetReplicaSet* knet_replica_set_create(KnetHost* host, uint32_t server, uint8_t control, uint8_t snapshot) {
    return new KnetReplicaSet{ReplicaSet(host->host, server, {control, snapshot}), {}, {}};
}
void knet_replica_set_destroy(KnetReplicaSet* r) { delete r; }
bool knet_replica_set_handle(KnetReplicaSet* r, const KnetEvent* event) { return event && r->replicas.Handle(ToEvent(*event)); }
size_t knet_replica_set_take_changes(KnetReplicaSet* r) {
    r->changes = r->replicas.TakeChanges();
    return r->changes.size();
}
bool knet_replica_set_change(const KnetReplicaSet* r, size_t index, uint32_t* kind, uint32_t* id, const char** type) {
    if (index >= r->changes.size()) return false;
    const auto& c = r->changes[index];
    if (kind) *kind = uint32_t(c.kind);
    if (id) *id = c.id;
    if (type) *type = c.type.c_str();
    return true;
}
size_t knet_replica_set_count(const KnetReplicaSet* r) {
    const_cast<KnetReplicaSet*>(r)->ids = r->replicas.Ids();
    return r->ids.size();
}
uint32_t knet_replica_set_id_at(const KnetReplicaSet* r, size_t index) {
    const auto ids = r->replicas.Ids();
    return index < ids.size() ? ids[index] : 0;
}
bool knet_replica_set_find(const KnetReplicaSet* r, uint32_t id, const uint8_t** state, size_t* size, uint32_t* tick, bool* owned, const char** type) {
    const Replica* replica = r->replicas.Find(id);
    if (!replica) return false;
    if (state) *state = reinterpret_cast<const uint8_t*>(replica->state.data());
    if (size) *size = replica->state.size();
    if (tick) *tick = replica->tick;
    if (owned) *owned = replica->owned;
    if (type) *type = replica->type.c_str();
    return true;
}
uint32_t knet_replica_set_latest_tick(const KnetReplicaSet* r) { return r->replicas.LatestTick(); }
bool knet_replica_set_states_around(const KnetReplicaSet* r, uint32_t id, float tick, const uint8_t** from, size_t* from_size,
                                    const uint8_t** to, size_t* to_size, float* alpha) {
    const auto around = r->replicas.StatesAround(id, tick);
    if (!around) return false;
    if (from) *from = reinterpret_cast<const uint8_t*>(around->from->data());
    if (from_size) *from_size = around->from->size();
    if (to) *to = reinterpret_cast<const uint8_t*>(around->to->data());
    if (to_size) *to_size = around->to->size();
    if (alpha) *alpha = around->alpha;
    return true;
}

}
