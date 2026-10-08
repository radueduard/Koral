#include "knet/socket.h"

#include <deque>
#include <filesystem>
#include <format>

#include <asio/ssl.hpp>

#if defined(_WIN32)
#  include <wincrypt.h>
#  pragma comment(lib, "crypt32.lib")
#endif

#include "io.h"
#include "stream.h"

namespace knet
{
    using asio::ip::tcp;
    using asio::ip::udp;
    using kor::ErrorCode;

    std::string Endpoint::ToString() const { return IsV6() ? std::format("[{}]:{}", address, port) : std::format("{}:{}", address, port); }

    namespace
    {
        template<class AsioEndpoint>
        Endpoint ToEndpoint(const AsioEndpoint& e) {
            auto address = e.address();
            if (address.is_v6() && address.to_v6().is_v4_mapped()) address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
            return {address.to_string(), e.port()};
        }

        kor::Result<asio::ip::address> ParseAddress(const std::string& text) {
            asio::error_code ec;
            const auto address = asio::ip::make_address(text, ec);
            if (ec) return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("'{}' is not a numeric address", text)));
            return address;
        }

        /** Arms a timer that, should it fire first, cancels the op and finishes it with eTimedOut. */
        template<class T>
        std::shared_ptr<asio::steady_timer> Deadline(const std::shared_ptr<detail::Op<T>>& op, Duration timeout, std::string what) {
            if (timeout <= Duration::zero()) return nullptr;
            auto timer = std::make_shared<asio::steady_timer>(detail::Io::Get().Context(), timeout);
            timer->async_wait([op, what = std::move(what)](const asio::error_code& ec) {
                if (ec) return;   // the operation finished first and cancelled the timer
                op->cancel.emit(asio::cancellation_type::all);
                op->Finish(std::unexpected(detail::MakeError(ErrorCode::eTimedOut, what + " timed out")));
            });
            return timer;
        }

        // Trusted roots: OpenSSL's own default paths, then wherever this system keeps its bundle — a vcpkg-built
        // OpenSSL looks in its own prefix, where there is none.
        void LoadSystemRoots(asio::ssl::context& context) {
            asio::error_code ignored;
            context.set_default_verify_paths(ignored);
#if defined(_WIN32)
            if (HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT")) {
                X509_STORE* x509 = SSL_CTX_get_cert_store(context.native_handle());
                for (PCCERT_CONTEXT cert = nullptr; (cert = CertEnumCertificatesInStore(store, cert));) {
                    const unsigned char* data = cert->pbCertEncoded;
                    if (X509* x = d2i_X509(nullptr, &data, static_cast<long>(cert->cbCertEncoded))) {
                        X509_STORE_add_cert(x509, x);
                        X509_free(x);
                    }
                }
                CertCloseStore(store, 0);
            }
#else
            for (const char* bundle : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem",
                                       "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", "/etc/ssl/cert.pem", "/usr/local/etc/openssl/cert.pem"}) {
                std::error_code exists;
                if (std::filesystem::exists(bundle, exists)) SSL_CTX_load_verify_locations(context.native_handle(), bundle, nullptr);
            }
#endif
        }
    }

    namespace detail
    {
        StreamState::StreamState(tcp::socket s) : socket(std::move(s)) {}

        // The last copy goes when no operation holds it any more (each holds one), so nothing is in flight on the
        // socket and it may be closed from whichever thread lets go — the reactor's bookkeeping is thread-safe.
        StreamState::~StreamState() { ssl.reset(); }

        kor::Result<void> StreamState::MakeClientTls(const TlsOptions& options, const std::string& host) {
            try {
                sslContext = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
                sslContext->set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3);
                const std::string name = options.serverName.empty() ? host : options.serverName;
                if (options.verifyPeer) {
                    sslContext->set_verify_mode(asio::ssl::verify_peer);
                    LoadSystemRoots(*sslContext);
                    if (!options.caPem.empty()) sslContext->add_certificate_authority(asio::buffer(options.caPem));
                } else {
                    sslContext->set_verify_mode(asio::ssl::verify_none);
                }
                ssl = std::make_unique<asio::ssl::stream<tcp::socket&>>(socket, *sslContext);
                SSL_set_tlsext_host_name(ssl->native_handle(), name.c_str());
                if (options.verifyPeer) ssl->set_verify_callback(asio::ssl::host_name_verification(name));
                return {};
            } catch (const std::exception& e) {
                return std::unexpected(MakeError(ErrorCode::eNetwork, std::format("TLS could not be set up: {}", e.what())));
            }
        }

        void StreamState::StartWrite() {
            if (writing || writes.empty()) return;
            writing = true;
            auto self = shared_from_this();
            auto& job = writes.front();
            auto handler = asio::bind_cancellation_slot(job.op->cancel.slot(), [self](const asio::error_code& ec, std::size_t) {
                auto job = std::move(self->writes.front());
                self->writes.pop_front();
                self->writing = false;
                if (job.timer) job.timer->cancel();
                if (ec) job.op->Finish(std::unexpected(self->Failure(ec, "write")));
                else job.op->Finish({});
                self->StartWrite();
            });
            if (ssl) asio::async_write(*ssl, asio::buffer(job.data), std::move(handler));
            else asio::async_write(socket, asio::buffer(job.data), std::move(handler));
        }

        kor::Error StreamState::Failure(const asio::error_code& ec, std::string_view what) const {
            if (closed) return MakeError(ErrorCode::eConnectionClosed, std::format("{}: the connection was closed", what));
            // A TLS peer that closes without a close_notify is still a closed connection.
            if (ec == asio::ssl::error::stream_truncated) return MakeError(ErrorCode::eConnectionClosed, std::format("{}: the connection was closed", what));
            return FromAsio(ec, what);
        }

        kor::Task<kor::Result<std::size_t>> RawReadSome(std::shared_ptr<StreamState> state, std::span<std::byte> buffer, Duration timeout) {
            return Run<std::size_t>([state, buffer, timeout](auto op) {
                auto timer = Deadline(op, timeout, "read");
                auto handler = asio::bind_cancellation_slot(op->cancel.slot(), [state, op, timer](const asio::error_code& ec, std::size_t n) {
                    if (timer) timer->cancel();
                    if (ec) op->Finish(std::unexpected(state->Failure(ec, "read")));
                    else op->Finish(n);
                });
                if (state->closed) return op->Finish(std::unexpected(state->Failure(asio::error::operation_aborted, "read")));
                if (state->ssl) state->ssl->async_read_some(asio::buffer(buffer.data(), buffer.size()), std::move(handler));
                else state->socket.async_read_some(asio::buffer(buffer.data(), buffer.size()), std::move(handler));
            });
        }
    }

    // ---- resolving --------------------------------------------------------------------------------------

    kor::Task<kor::Result<std::vector<Endpoint>>> Resolve(std::string host, std::uint16_t port) {
        return detail::Run<std::vector<Endpoint>>([host = std::move(host), port](auto op) {
            auto resolver = std::make_shared<tcp::resolver>(detail::Io::Get().Context());
            resolver->async_resolve(host, std::to_string(port), asio::bind_cancellation_slot(op->cancel.slot(),
                [op, resolver, host](const asio::error_code& ec, const tcp::resolver::results_type& results) {
                    if (ec) return op->Finish(std::unexpected(detail::FromAsio(ec, std::format("resolving '{}'", host))));
                    std::vector<Endpoint> endpoints;
                    for (const auto& entry : results) endpoints.push_back(ToEndpoint(entry.endpoint()));
                    op->Finish(std::move(endpoints));
                }));
        });
    }

    // ---- TcpStream --------------------------------------------------------------------------------------

    kor::Task<kor::Result<TcpStream>> TcpStream::Connect(std::string host, std::uint16_t port, ConnectOptions options) {
        auto state = std::make_shared<detail::StreamState>(tcp::socket(detail::Io::Get().Context()));
        if (options.tls)
            if (auto made = state->MakeClientTls(*options.tls, host); !made) co_return std::unexpected(std::move(made.error()));

        auto connected = co_await detail::Run<TcpStream>([state, host, port, options](auto op) {
            auto timer = Deadline(op, options.timeout, std::format("connecting to {}:{}", host, port));
            auto resolver = std::make_shared<tcp::resolver>(detail::Io::Get().Context());
            resolver->async_resolve(host, std::to_string(port), asio::bind_cancellation_slot(op->cancel.slot(),
                [state, op, timer, resolver, host, port, options](const asio::error_code& ec, const tcp::resolver::results_type& results) {
                    if (ec) {
                        if (timer) timer->cancel();
                        return op->Finish(std::unexpected(detail::FromAsio(ec, std::format("resolving '{}'", host))));
                    }
                    asio::async_connect(state->socket, results, asio::bind_cancellation_slot(op->cancel.slot(),
                        [state, op, timer, host, port, options](const asio::error_code& ec, const tcp::endpoint&) {
                            if (ec) {
                                if (timer) timer->cancel();
                                return op->Finish(std::unexpected(detail::FromAsio(ec, std::format("connecting to {}:{}", host, port))));
                            }
                            asio::error_code ignored;
                            state->socket.set_option(tcp::no_delay(options.noDelay), ignored);
                            if (!state->ssl) {
                                if (timer) timer->cancel();
                                return op->Finish(TcpStream(state));
                            }
                            state->ssl->async_handshake(asio::ssl::stream_base::client, asio::bind_cancellation_slot(op->cancel.slot(),
                                [state, op, timer, host](const asio::error_code& ec) {
                                    if (timer) timer->cancel();
                                    if (ec) return op->Finish(std::unexpected(detail::FromAsio(ec, std::format("TLS handshake with '{}'", host))));
                                    op->Finish(TcpStream(state));
                                }));
                        }));
                }));
        });
        co_return connected;
    }

    kor::Task<kor::Result<std::size_t>> TcpStream::ReadSome(std::span<std::byte> buffer, Duration timeout) {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "read from a stream that is not connected"));
        if (buffer.empty()) co_return std::size_t{0};
        auto state = _state;
        if (!state->buffer.empty()) {   // what an earlier ReadUntil read past its delimiter comes first
            const std::size_t n = std::min(buffer.size(), state->buffer.size());
            std::memcpy(buffer.data(), state->buffer.data(), n);
            state->buffer.erase(0, n);
            co_return n;
        }
        co_return co_await detail::RawReadSome(state, buffer, timeout);
    }

    kor::Task<kor::Result<void>> TcpStream::ReadExactly(std::span<std::byte> buffer, Duration timeout) {
        TcpStream self = *this;   // the caller's object may go while this waits
        std::size_t done = 0;
        while (done < buffer.size()) {
            auto n = co_await self.ReadSome(buffer.subspan(done), timeout);
            if (!n) co_return std::unexpected(std::move(n.error()));
            done += *n;
        }
        co_return kor::Result<void>{};
    }

    kor::Task<kor::Result<std::string>> TcpStream::ReadUntil(std::string delimiter, std::size_t limit, Duration timeout) {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "read from a stream that is not connected"));
        auto state = _state;
        std::size_t searched = 0;
        for (;;) {
            const auto at = state->buffer.find(delimiter, searched > delimiter.size() ? searched - delimiter.size() : 0);
            if (at != std::string::npos) {
                std::string line = state->buffer.substr(0, at + delimiter.size());
                state->buffer.erase(0, at + delimiter.size());
                co_return line;
            }
            searched = state->buffer.size();
            if (state->buffer.size() >= limit)
                co_return std::unexpected(detail::MakeError(ErrorCode::eProtocol, std::format("no '{}' within {} bytes", delimiter == "\r\n" ? "line end" : delimiter, limit)));
            std::byte chunk[4096];
            auto n = co_await detail::RawReadSome(state, chunk, timeout);
            if (!n) co_return std::unexpected(std::move(n.error()));
            state->buffer.append(reinterpret_cast<const char*>(chunk), *n);
        }
    }

    kor::Task<kor::Result<Bytes>> TcpStream::ReadToEnd(std::size_t limit, Duration timeout) {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "read from a stream that is not connected"));
        auto state = _state;
        Bytes all(reinterpret_cast<const std::byte*>(state->buffer.data()), reinterpret_cast<const std::byte*>(state->buffer.data()) + state->buffer.size());
        state->buffer.clear();
        for (;;) {
            std::byte chunk[16384];
            auto n = co_await detail::RawReadSome(state, chunk, timeout);
            if (!n) {
                if (n.error().code == ErrorCode::eConnectionClosed) co_return all;
                co_return std::unexpected(std::move(n.error()));
            }
            all.insert(all.end(), chunk, chunk + *n);
            if (all.size() > limit) co_return std::unexpected(detail::MakeError(ErrorCode::eProtocol, std::format("more than {} bytes", limit)));
        }
    }

    kor::Task<kor::Result<void>> TcpStream::Write(std::span<const std::byte> data, Duration timeout) {
        if (!_state) return []() -> kor::Task<kor::Result<void>> {
            co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "write to a stream that is not connected"));
        }();
        return detail::Run<void>([state = _state, copy = Bytes(data.begin(), data.end()), timeout](auto op) mutable {
            if (state->closed) return op->Finish(std::unexpected(state->Failure(asio::error::operation_aborted, "write")));
            state->writes.push_back({std::move(copy), op, Deadline(op, timeout, "write")});
            state->StartWrite();
        });
    }

    kor::Task<kor::Result<void>> TcpStream::Write(std::string_view text, Duration timeout) { return Write(AsBytes(text), timeout); }

    void TcpStream::Close() {
        if (!_state) return;
        asio::post(detail::Io::Get().Context(), [state = _state] {
            state->closed = true;
            asio::error_code ignored;
            state->socket.shutdown(tcp::socket::shutdown_both, ignored);
            state->socket.close(ignored);
        });
    }

    bool TcpStream::Secure() const { return _state && _state->ssl != nullptr; }

    Endpoint TcpStream::Local() const {
        if (!_state) return {};
        asio::error_code ec;
        const auto e = _state->socket.local_endpoint(ec);
        return ec ? Endpoint{} : ToEndpoint(e);
    }

    Endpoint TcpStream::Remote() const {
        if (!_state) return {};
        asio::error_code ec;
        const auto e = _state->socket.remote_endpoint(ec);
        return ec ? Endpoint{} : ToEndpoint(e);
    }

    // ---- TcpListener ------------------------------------------------------------------------------------

    namespace detail
    {
        struct ListenerState {
            tcp::acceptor acceptor;
            std::shared_ptr<asio::ssl::context> sslContext;
            std::uint16_t port = 0;
            explicit ListenerState(asio::io_context& context) : acceptor(context) {}
        };
    }

    kor::Result<TcpListener> TcpListener::Listen(std::uint16_t port, std::string address, std::optional<TlsServerOptions> tls) {
        auto ip = ParseAddress(address);
        if (!ip) return std::unexpected(std::move(ip.error()));
        auto state = std::make_shared<detail::ListenerState>(detail::Io::Get().Context());
        const tcp::endpoint endpoint(*ip, port);
        asio::error_code ec;
        state->acceptor.open(endpoint.protocol(), ec);
        if (!ec) state->acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
        if (!ec && ip->is_v6()) state->acceptor.set_option(asio::ip::v6_only(false), ec);
        if (!ec) state->acceptor.bind(endpoint, ec);
        if (!ec) state->acceptor.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) return std::unexpected(detail::FromAsio(ec, std::format("listening on {}", Endpoint{address, port}.ToString())));
        state->port = state->acceptor.local_endpoint(ec).port();
        if (tls) {
            try {
                state->sslContext = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
                state->sslContext->set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3);
                state->sslContext->use_certificate_chain(asio::buffer(tls->certificatePem));
                state->sslContext->use_private_key(asio::buffer(tls->privateKeyPem), asio::ssl::context::pem);
            } catch (const std::exception& e) {
                return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("the TLS identity was refused: {}", e.what())));
            }
        }
        TcpListener listener;
        listener._state = std::move(state);
        return listener;
    }

    kor::Task<kor::Result<TcpStream>> TcpListener::Accept() {
        if (!_state) co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "accept on a listener that is not listening"));
        auto state = _state;
        for (;;) {
            auto accepted = co_await detail::Run<TcpStream>([state](auto op) {
                state->acceptor.async_accept(asio::bind_cancellation_slot(op->cancel.slot(), [op](const asio::error_code& ec, tcp::socket socket) {
                    if (ec) return op->Finish(std::unexpected(detail::FromAsio(ec, "accept")));
                    asio::error_code ignored;
                    socket.set_option(tcp::no_delay(true), ignored);
                    op->Finish(TcpStream(std::make_shared<detail::StreamState>(std::move(socket))));
                }));
            });
            if (!accepted || !state->sslContext) co_return accepted;

            // A client whose handshake fails is that client's problem, not the listener's: it is dropped, and
            // the next one waited for, so one bad client cannot end a server's accept loop.
            auto stream = accepted->_state;
            stream->sslContext = state->sslContext;
            stream->ssl = std::make_unique<asio::ssl::stream<tcp::socket&>>(stream->socket, *stream->sslContext);
            auto handshake = co_await detail::Run<void>([stream](auto op) {
                auto timer = Deadline(op, Duration(10'000), "TLS handshake");
                stream->ssl->async_handshake(asio::ssl::stream_base::server, asio::bind_cancellation_slot(op->cancel.slot(),
                    [op, timer](const asio::error_code& ec) {
                        if (timer) timer->cancel();
                        if (ec) op->Finish(std::unexpected(detail::FromAsio(ec, "TLS handshake")));
                        else op->Finish({});
                    }));
            });
            if (handshake) co_return accepted;
            accepted->Close();
        }
    }

    std::uint16_t TcpListener::Port() const { return _state ? _state->port : 0; }

    void TcpListener::Close() {
        if (!_state) return;
        asio::post(detail::Io::Get().Context(), [state = _state] {
            asio::error_code ignored;
            state->acceptor.close(ignored);
        });
    }

    // ---- UdpSocket --------------------------------------------------------------------------------------

    namespace detail
    {
        struct UdpState {
            udp::socket socket;
            std::uint16_t port = 0;
            explicit UdpState(asio::io_context& context) : socket(context) {}
        };
    }

    kor::Result<UdpSocket> UdpSocket::Bind(std::uint16_t port, std::string address) {
        auto ip = ParseAddress(address);
        if (!ip) return std::unexpected(std::move(ip.error()));
        auto state = std::make_shared<detail::UdpState>(detail::Io::Get().Context());
        const udp::endpoint endpoint(*ip, port);
        asio::error_code ec;
        state->socket.open(endpoint.protocol(), ec);
        if (!ec && ip->is_v6()) state->socket.set_option(asio::ip::v6_only(false), ec);
        if (!ec) state->socket.bind(endpoint, ec);
        if (ec) return std::unexpected(detail::FromAsio(ec, std::format("binding {}", Endpoint{address, port}.ToString())));
        state->port = state->socket.local_endpoint(ec).port();
        UdpSocket socket;
        socket._state = std::move(state);
        return socket;
    }

    kor::Task<kor::Result<Datagram>> UdpSocket::Receive(Duration timeout) {
        if (!_state) return []() -> kor::Task<kor::Result<Datagram>> {
            co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "receive on a socket that is not bound"));
        }();
        return detail::Run<Datagram>([state = _state, timeout](auto op) {
            auto timer = Deadline(op, timeout, "receive");
            auto buffer = std::make_shared<Bytes>(65536);
            auto from = std::make_shared<udp::endpoint>();
            state->socket.async_receive_from(asio::buffer(*buffer), *from, asio::bind_cancellation_slot(op->cancel.slot(),
                [state, op, timer, buffer, from](const asio::error_code& ec, std::size_t n) {
                    if (timer) timer->cancel();
                    if (ec) return op->Finish(std::unexpected(detail::FromAsio(ec, "receive")));
                    buffer->resize(n);
                    op->Finish(Datagram{ToEndpoint(*from), std::move(*buffer)});
                }));
        });
    }

    kor::Task<kor::Result<void>> UdpSocket::SendTo(Endpoint to, std::span<const std::byte> data) {
        if (!_state) return []() -> kor::Task<kor::Result<void>> {
            co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, "send on a socket that is not bound"));
        }();
        auto ip = ParseAddress(to.address);
        if (!ip) return [e = std::move(ip.error())]() -> kor::Task<kor::Result<void>> { co_return std::unexpected(e); }();
        // An IPv6 socket reaches IPv4 peers as v4-mapped addresses.
        asio::ip::address target = *ip;
        if (_state->socket.local_endpoint().address().is_v6() && target.is_v4())
            target = asio::ip::make_address_v6(asio::ip::v4_mapped, target.to_v4());
        return detail::Run<void>([state = _state, copy = Bytes(data.begin(), data.end()), endpoint = udp::endpoint(target, to.port)](auto op) mutable {
            auto payload = std::make_shared<Bytes>(std::move(copy));
            state->socket.async_send_to(asio::buffer(*payload), endpoint, [op, payload](const asio::error_code& ec, std::size_t) {
                if (ec) op->Finish(std::unexpected(detail::FromAsio(ec, "send")));
                else op->Finish({});
            });
        });
    }

    std::uint16_t UdpSocket::Port() const { return _state ? _state->port : 0; }

    void UdpSocket::Close() {
        if (!_state) return;
        asio::post(detail::Io::Get().Context(), [state = _state] {
            asio::error_code ignored;
            state->socket.close(ignored);
        });
    }
}
