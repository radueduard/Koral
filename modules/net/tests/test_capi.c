/* koral-net from C: a TCP echo through ops and tokens, and a game-protocol exchange. Also the check that
   koralNet_c.h is C. */

#include <stdio.h>
#include <string.h>

#include <koralNet_c.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAILED: %s (%s:%d)\n", what, __FILE__, __LINE__); ++failures; } } while (0)

/* Waits for an op, and frees its token. (The token is read after the call that made it: C does not order
   the evaluation of a call's arguments, so it cannot be passed beside the call.) */
static KnetOp* finish(KnetOp* op, KoralToken** done) {
    koral_token_wait(*done);
    koral_token_destroy(*done);
    *done = NULL;
    return op;
}

int main(void) {
    KoralToken* done = NULL;
    KnetListener* listener = knet_listener_listen(0, "127.0.0.1", NULL, NULL);
    CHECK(listener != NULL, "listen");
    KoralToken* acceptedToken = NULL;
    KnetOp* accepting = knet_listener_accept(listener, &acceptedToken);

    KnetOp* connecting = finish(knet_tcp_connect("127.0.0.1", knet_listener_port(listener), 5000, NULL, &done), &done);
    CHECK(!knet_op_failed(connecting), knet_op_error(connecting));
    KnetStream* client = knet_op_take_stream(connecting);
    knet_op_destroy(connecting);

    finish(accepting, &acceptedToken);
    KnetStream* server = knet_op_take_stream(accepting);
    knet_op_destroy(accepting);
    CHECK(client && server, "both ends");

    KnetOp* writing = finish(knet_stream_write(client, (const uint8_t*)"ping\n", 5, 0, &done), &done);
    CHECK(!knet_op_failed(writing), "write");
    knet_op_destroy(writing);
    KnetOp* reading = finish(knet_stream_read_until(server, "\n", 1024, 2000, &done), &done);
    size_t size = 0;
    const uint8_t* bytes = knet_op_bytes(reading, &size);
    CHECK(size == 5 && memcmp(bytes, "ping\n", 5) == 0, "read until");
    knet_op_destroy(reading);

    /* A read that times out. */
    KnetOp* late = finish(knet_stream_read_some(client, 16, 30, &done), &done);
    CHECK(knet_op_failed(late), "a read times out");
    knet_op_destroy(late);
    /* An op destroyed before it finishes is cancelled. */
    knet_op_destroy(knet_stream_read_some(client, 16, 0, &done));
    koral_token_destroy(done);

    knet_stream_destroy(client);
    knet_stream_destroy(server);
    knet_listener_destroy(listener);

    /* The game protocol. */
    KnetHostOptions serverOptions = knet_host_options_default();
    serverOptions.address = "127.0.0.1";
    KnetHost* host = knet_host_create(&serverOptions);
    KnetHostOptions clientOptions = knet_host_options_default();
    clientOptions.address = "127.0.0.1";
    clientOptions.max_peers = 0;
    KnetHost* guest = knet_host_create(&clientOptions);
    uint32_t toServer = knet_host_connect(guest, "127.0.0.1", knet_host_port(host));
    int received = 0;
    for (int i = 0; i < 2000 && !received; ++i) {
        size_t n = knet_host_update(host);
        for (size_t k = 0; k < n; ++k) {
            KnetEvent e;
            knet_host_event(host, k, &e);
            if (e.type == 2 && e.size == 5 && memcmp(e.data, "hello", 5) == 0) received = 1;
        }
        knet_host_update(guest);
        if (knet_host_connected(guest, toServer)) knet_host_send(guest, toServer, 0, (const uint8_t*)"hello", 5);
    }
    CHECK(received, "a reliable message arrives");
    knet_host_destroy(guest);
    knet_host_destroy(host);

    /* Bits, as every binding writes them. */
    KnetBitWriter* w = knet_bit_writer_create();
    knet_bit_writer_write_bits(w, 5, 3);
    knet_bit_writer_write_var_int(w, -300);
    knet_bit_writer_write_string(w, "abc");
    size_t n = 0;
    const uint8_t* data = knet_bit_writer_data(w, &n);
    KnetBitReader* r = knet_bit_reader_create(data, n);
    CHECK(knet_bit_reader_read_bits(r, 3) == 5, "bits");
    CHECK(knet_bit_reader_read_var_int(r) == -300, "varint");
    CHECK(strcmp(knet_bit_reader_read_string(r), "abc") == 0, "string");
    CHECK(!knet_bit_reader_failed(r), "not failed");
    knet_bit_reader_destroy(r);
    knet_bit_writer_destroy(w);

    if (failures == 0) printf("koral-net from C: all good\n");
    return failures == 0 ? 0 : 1;
}
