/*
 * koral-net's C interface: knet, object for object, for bindings to other languages.
 *
 * Named as koral_c.h names Koral's: knet_<class>_<member> in snake case — knet_stream_read_until is
 * knet::TcpStream::ReadUntil, knet_host_update is knet::Host::Update — and the C++ headers are its
 * documentation. Its conventions are koral_c.h's, plus:
 *
 *  - An operation that waits (connect, read, accept, fetch, ...) returns a KnetOp at once and the caller's
 *    KoralToken in *done, signalled when it has finished: wait for it (koral_token_wait), await it, or poll
 *    knet_op_ready. Then knet_op_failed / knet_op_error say whether it worked, and the knet_op_* getters for
 *    its kind give what it produced. Free both with knet_op_destroy and koral_token_destroy. Destroying an
 *    unfinished op cancels it.
 *  - What a getter returns that is a pointer (bytes, strings, a new handle) stays valid until the op is
 *    destroyed — except new handles (streams, WebSockets), which are the caller's from then on, to destroy.
 *  - Functions that do not wait report failure as koral_c.h's do: a null handle or false, the reason in
 *    koral_last_error().
 *  - A KnetHost (the game protocol) is used from one thread at a time; everything else from any thread.
 *  - knet::Predictor, InputBuffer and TickClock are logic, not I/O: the bindings carry their own copies.
 */

#ifndef KORAL_NET_C_H
#define KORAL_NET_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "koral_c.h"
#include "knet/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KnetOp KnetOp;
typedef struct KnetStream KnetStream;
typedef struct KnetListener KnetListener;
typedef struct KnetUdp KnetUdp;
typedef struct KnetWebSocket KnetWebSocket;
typedef struct KnetHost KnetHost;
typedef struct KnetBitWriter KnetBitWriter;
typedef struct KnetBitReader KnetBitReader;
typedef struct KnetReplicator KnetReplicator;
typedef struct KnetReplicaSet KnetReplicaSet;

/* ==== operations ===================================================================================== */

KNET_API bool knet_op_ready(const KnetOp* op);
/** True once finished with an error, or cancelled. */
KNET_API bool knet_op_failed(const KnetOp* op);
/** The kor::ErrorCode it failed with (0: none). */
KNET_API uint32_t knet_op_error_code(const KnetOp* op);
KNET_API const char* knet_op_error(const KnetOp* op);
/** Cancels it if it has not finished, and frees it. */
KNET_API void knet_op_destroy(KnetOp* op);

/** A read's, a receive's, a fetch's or a WebSocket message's bytes. */
KNET_API const uint8_t* knet_op_bytes(const KnetOp* op, size_t* size);
/** A connect's, an accept's: the new stream, the caller's. Null once taken. */
KNET_API KnetStream* knet_op_take_stream(KnetOp* op);
/** A WebSocket connect's or accept's: the new socket, the caller's. Null once taken. */
KNET_API KnetWebSocket* knet_op_take_websocket(KnetOp* op);
/** A UDP receive's sender; a resolve's addresses (index < knet_op_count). */
KNET_API const char* knet_op_address(const KnetOp* op, size_t index, uint16_t* port);
KNET_API size_t knet_op_count(const KnetOp* op);
/** A WebSocket message's kind. */
KNET_API bool knet_op_binary(const KnetOp* op);
/** A fetch's response. */
KNET_API int32_t knet_op_status(const KnetOp* op);
KNET_API const char* knet_op_url(const KnetOp* op);
KNET_API size_t knet_op_header_count(const KnetOp* op);
KNET_API const char* knet_op_header_name(const KnetOp* op, size_t index);
KNET_API const char* knet_op_header_value(const KnetOp* op, size_t index);
/** The first header of that name, case-insensitively; empty when there is none. */
KNET_API const char* knet_op_header(const KnetOp* op, const char* name);

/* ==== sockets ======================================================================================== */

typedef struct KnetTlsOptions {
    bool verify_peer;            /* true unless testing */
    const char* server_name;     /* null: the host */
    const char* ca_pem;          /* extra trusted roots, or null */
} KnetTlsOptions;
/** knet::TlsOptions{}: verify, no name, no extra roots. */
KNET_API KnetTlsOptions knet_tls_options_default(void);

KNET_API KnetOp* knet_resolve(const char* host, uint16_t port, KoralToken** done);
/** @p tls null: plain TCP. @p timeout_ms 0: none. */
KNET_API KnetOp* knet_tcp_connect(const char* host, uint16_t port, uint32_t timeout_ms, const KnetTlsOptions* tls, KoralToken** done);
KNET_API KnetOp* knet_stream_read_some(KnetStream* stream, size_t max_bytes, uint32_t timeout_ms, KoralToken** done);
KNET_API KnetOp* knet_stream_read_exactly(KnetStream* stream, size_t bytes, uint32_t timeout_ms, KoralToken** done);
KNET_API KnetOp* knet_stream_read_until(KnetStream* stream, const char* delimiter, size_t limit, uint32_t timeout_ms, KoralToken** done);
KNET_API KnetOp* knet_stream_read_to_end(KnetStream* stream, size_t limit, uint32_t timeout_ms, KoralToken** done);
KNET_API KnetOp* knet_stream_write(KnetStream* stream, const uint8_t* data, size_t size, uint32_t timeout_ms, KoralToken** done);
KNET_API void knet_stream_close(KnetStream* stream);
KNET_API bool knet_stream_secure(const KnetStream* stream);
/** The local or (remote) endpoint's address; its port in *port. */
KNET_API const char* knet_stream_local(const KnetStream* stream, uint16_t* port);
KNET_API const char* knet_stream_remote(const KnetStream* stream, uint16_t* port);
KNET_API void knet_stream_destroy(KnetStream* stream);

/** A TLS listener when both PEMs are given. */
KNET_API KnetListener* knet_listener_listen(uint16_t port, const char* address, const char* certificate_pem, const char* private_key_pem);
KNET_API KnetOp* knet_listener_accept(KnetListener* listener, KoralToken** done);
KNET_API uint16_t knet_listener_port(const KnetListener* listener);
KNET_API void knet_listener_close(KnetListener* listener);
KNET_API void knet_listener_destroy(KnetListener* listener);

KNET_API KnetUdp* knet_udp_bind(uint16_t port, const char* address);
KNET_API KnetOp* knet_udp_receive(KnetUdp* socket, uint32_t timeout_ms, KoralToken** done);
KNET_API KnetOp* knet_udp_send_to(KnetUdp* socket, const char* address, uint16_t port, const uint8_t* data, size_t size, KoralToken** done);
KNET_API uint16_t knet_udp_port(const KnetUdp* socket);
KNET_API void knet_udp_close(KnetUdp* socket);
KNET_API void knet_udp_destroy(KnetUdp* socket);

/* ==== HTTP and WebSockets ============================================================================ */

typedef struct KnetHttpRequest {
    const char* method;            /* null: GET */
    const char* url;
    const char* const* header_names;
    const char* const* header_values;
    size_t header_count;
    const uint8_t* body;
    size_t body_size;
    uint32_t timeout_ms;           /* 0: none */
    int32_t max_redirects;
    KnetTlsOptions tls;
} KnetHttpRequest;
/** knet::HttpRequest{}'s defaults, for @p url. */
KNET_API KnetHttpRequest knet_http_request_default(const char* url);
KNET_API KnetOp* knet_http_fetch(const KnetHttpRequest* request, KoralToken** done);

typedef struct KnetWebSocketOptions {
    const char* const* header_names;
    const char* const* header_values;
    size_t header_count;
    const char* const* protocols;
    size_t protocol_count;
    uint32_t timeout_ms;
    KnetTlsOptions tls;
    size_t max_message;
} KnetWebSocketOptions;
KNET_API KnetWebSocketOptions knet_websocket_options_default(void);
KNET_API KnetOp* knet_websocket_connect(const char* url, const KnetWebSocketOptions* options, KoralToken** done);
/** The server side, on an accepted stream (which the op takes over). */
KNET_API KnetOp* knet_websocket_accept(KnetStream* stream, size_t max_message, KoralToken** done);
KNET_API KnetOp* knet_websocket_send_text(KnetWebSocket* socket, const char* text, KoralToken** done);
KNET_API KnetOp* knet_websocket_send_binary(KnetWebSocket* socket, const uint8_t* data, size_t size, KoralToken** done);
KNET_API KnetOp* knet_websocket_receive(KnetWebSocket* socket, KoralToken** done);
KNET_API KnetOp* knet_websocket_close(KnetWebSocket* socket, uint16_t code, const char* reason, KoralToken** done);
KNET_API bool knet_websocket_open(const KnetWebSocket* socket);
KNET_API uint16_t knet_websocket_close_code(const KnetWebSocket* socket);
KNET_API const char* knet_websocket_protocol(const KnetWebSocket* socket);
KNET_API void knet_websocket_destroy(KnetWebSocket* socket);

/* ==== bits =========================================================================================== */

KNET_API KnetBitWriter* knet_bit_writer_create(void);
KNET_API void knet_bit_writer_destroy(KnetBitWriter* writer);
KNET_API void knet_bit_writer_write_bits(KnetBitWriter* writer, uint64_t value, int32_t bits);
KNET_API void knet_bit_writer_write_var_uint(KnetBitWriter* writer, uint64_t value);
KNET_API void knet_bit_writer_write_var_int(KnetBitWriter* writer, int64_t value);
KNET_API void knet_bit_writer_write_float(KnetBitWriter* writer, float value);
KNET_API void knet_bit_writer_write_double(KnetBitWriter* writer, double value);
KNET_API void knet_bit_writer_write_quantized(KnetBitWriter* writer, float value, float min, float max, int32_t bits);
KNET_API void knet_bit_writer_write_string(KnetBitWriter* writer, const char* text);
KNET_API void knet_bit_writer_write_bytes(KnetBitWriter* writer, const uint8_t* data, size_t size);
/** x, y, z, w. */
KNET_API void knet_bit_writer_write_quat(KnetBitWriter* writer, const float q[4], int32_t bits_per_component);
KNET_API void knet_bit_writer_align(KnetBitWriter* writer);
KNET_API const uint8_t* knet_bit_writer_data(const KnetBitWriter* writer, size_t* size);
KNET_API size_t knet_bit_writer_bit_count(const KnetBitWriter* writer);
KNET_API void knet_bit_writer_clear(KnetBitWriter* writer);

/** Reads @p size bytes at @p data, copied. */
KNET_API KnetBitReader* knet_bit_reader_create(const uint8_t* data, size_t size);
KNET_API void knet_bit_reader_destroy(KnetBitReader* reader);
KNET_API uint64_t knet_bit_reader_read_bits(KnetBitReader* reader, int32_t bits);
KNET_API uint64_t knet_bit_reader_read_var_uint(KnetBitReader* reader);
KNET_API int64_t knet_bit_reader_read_var_int(KnetBitReader* reader);
KNET_API float knet_bit_reader_read_float(KnetBitReader* reader);
KNET_API double knet_bit_reader_read_double(KnetBitReader* reader);
KNET_API float knet_bit_reader_read_quantized(KnetBitReader* reader, float min, float max, int32_t bits);
/** Valid until the next read from this reader. */
KNET_API const char* knet_bit_reader_read_string(KnetBitReader* reader);
KNET_API const uint8_t* knet_bit_reader_read_bytes(KnetBitReader* reader, size_t* size);
KNET_API void knet_bit_reader_read_quat(KnetBitReader* reader, float q[4], int32_t bits_per_component);
KNET_API void knet_bit_reader_align(KnetBitReader* reader);
KNET_API bool knet_bit_reader_failed(const KnetBitReader* reader);
KNET_API size_t knet_bit_reader_bits_left(const KnetBitReader* reader);

/** Writes the delta into @p out when @p capacity allows; returns its size either way. */
KNET_API size_t knet_encode_delta(const uint8_t* base, size_t base_size, const uint8_t* state, size_t state_size, uint8_t* out, size_t capacity);
/** Returns the state's size (written to @p out when @p capacity allows), or SIZE_MAX for a delta that does not fit @p base. */
KNET_API size_t knet_decode_delta(const uint8_t* base, size_t base_size, const uint8_t* delta, size_t delta_size, uint8_t* out, size_t capacity);

/* ==== the game protocol ============================================================================= */

typedef struct KnetNetworkSimulation { uint32_t latency_ms, jitter_ms; float loss, duplicate; } KnetNetworkSimulation;

typedef struct KnetHostOptions {
    uint16_t port;
    const char* address;           /* null: "0.0.0.0" */
    uint32_t max_peers;
    uint64_t protocol_id;
    const uint8_t* channels;       /* knet::Delivery values: 0 reliable, 1 unreliable, 2 sequenced; null: the defaults */
    size_t channel_count;
    uint32_t timeout_ms, connect_timeout_ms, keep_alive_ms;
    size_t max_message;
    KnetNetworkSimulation simulation;
} KnetHostOptions;
KNET_API KnetHostOptions knet_host_options_default(void);

/** knet::Event; data valid until the next knet_host_update. type: 0 connected, 1 disconnected, 2 message. */
typedef struct KnetEvent {
    uint32_t type;
    uint32_t peer;
    uint32_t channel;
    uint32_t reason;               /* knet::DisconnectReason */
    const uint8_t* data;
    size_t size;
} KnetEvent;

typedef struct KnetPeerStats {
    float rtt, packet_loss, sent_bytes_per_second, received_bytes_per_second;
    uint64_t packets_sent, packets_received, packets_lost;
} KnetPeerStats;

KNET_API KnetHost* knet_host_create(const KnetHostOptions* options);
KNET_API void knet_host_destroy(KnetHost* host);
KNET_API uint32_t knet_host_connect(KnetHost* host, const char* address, uint16_t port);
KNET_API bool knet_host_send(KnetHost* host, uint32_t peer, uint8_t channel, const uint8_t* data, size_t size);
KNET_API void knet_host_broadcast(KnetHost* host, uint8_t channel, const uint8_t* data, size_t size, uint32_t except);
KNET_API void knet_host_disconnect(KnetHost* host, uint32_t peer);
/** Returns how many events happened; read them with knet_host_event. */
KNET_API size_t knet_host_update(KnetHost* host);
KNET_API bool knet_host_event(const KnetHost* host, size_t index, KnetEvent* event);
KNET_API void knet_host_flush(KnetHost* host);
KNET_API size_t knet_host_peer_count(const KnetHost* host);
KNET_API uint32_t knet_host_peer_at(const KnetHost* host, size_t index);
KNET_API bool knet_host_connected(const KnetHost* host, uint32_t peer);
KNET_API const char* knet_host_address(const KnetHost* host, uint32_t peer, uint16_t* port);
KNET_API KnetPeerStats knet_host_stats(const KnetHost* host, uint32_t peer);
KNET_API uint16_t knet_host_port(const KnetHost* host);
KNET_API void knet_host_set_simulation(KnetHost* host, const KnetNetworkSimulation* simulation);

/* ==== replication ==================================================================================== */

KNET_API KnetReplicator* knet_replicator_create(KnetHost* host, uint8_t control_channel, uint8_t snapshot_channel);
KNET_API void knet_replicator_destroy(KnetReplicator* replicator);
KNET_API uint32_t knet_replicator_spawn(KnetReplicator* replicator, const char* type, const uint8_t* state, size_t size, uint32_t owner);
KNET_API void knet_replicator_set_state(KnetReplicator* replicator, uint32_t id, const uint8_t* state, size_t size);
KNET_API void knet_replicator_despawn(KnetReplicator* replicator, uint32_t id);
KNET_API size_t knet_replicator_count(const KnetReplicator* replicator);
KNET_API void knet_replicator_send_snapshot(KnetReplicator* replicator, uint32_t tick);
/** The replication's own messages: true. Pass every event of knet_host_update. */
KNET_API bool knet_replicator_handle(KnetReplicator* replicator, const KnetEvent* event);
KNET_API size_t knet_replicator_last_snapshot_bytes(const KnetReplicator* replicator, uint32_t peer);

KNET_API KnetReplicaSet* knet_replica_set_create(KnetHost* host, uint32_t server, uint8_t control_channel, uint8_t snapshot_channel);
KNET_API void knet_replica_set_destroy(KnetReplicaSet* replicas);
KNET_API bool knet_replica_set_handle(KnetReplicaSet* replicas, const KnetEvent* event);
/** Takes the changes since the last call; read them with knet_replica_set_change (0 spawned, 1 despawned). */
KNET_API size_t knet_replica_set_take_changes(KnetReplicaSet* replicas);
KNET_API bool knet_replica_set_change(const KnetReplicaSet* replicas, size_t index, uint32_t* kind, uint32_t* id, const char** type);
KNET_API size_t knet_replica_set_count(const KnetReplicaSet* replicas);
KNET_API uint32_t knet_replica_set_id_at(const KnetReplicaSet* replicas, size_t index);
/** The latest state of @p id (valid until the next handle), false when there is no such replica. */
KNET_API bool knet_replica_set_find(const KnetReplicaSet* replicas, uint32_t id, const uint8_t** state, size_t* size, uint32_t* tick,
                                    bool* owned, const char** type);
KNET_API uint32_t knet_replica_set_latest_tick(const KnetReplicaSet* replicas);
KNET_API bool knet_replica_set_states_around(const KnetReplicaSet* replicas, uint32_t id, float tick, const uint8_t** from, size_t* from_size,
                                             const uint8_t** to, size_t* to_size, float* alpha);

#ifdef __cplusplus
}
#endif

#endif /* KORAL_NET_C_H */
