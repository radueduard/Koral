#include "knet/http.h"

#include <algorithm>
#include <charconv>
#include <format>

#include <zlib.h>

#include "io.h"

namespace knet
{
    using kor::ErrorCode;
    using Clock = std::chrono::steady_clock;

    namespace
    {
        bool EqualsNoCase(std::string_view a, std::string_view b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
        }
        std::string Lower(std::string_view s) {
            std::string out(s);
            for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }
        std::string_view Trim(std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
            return s;
        }
        kor::Error Protocol(std::string message) { return detail::MakeError(ErrorCode::eProtocol, std::move(message)); }

        /** What is left of the time a request was given; 1 ms rather than none once it has run out, so the next wait times out. */
        Duration Left(Clock::time_point deadline) {
            if (deadline == Clock::time_point::max()) return Duration::zero();
            const auto left = std::chrono::duration_cast<Duration>(deadline - Clock::now());
            return left > Duration::zero() ? left : Duration(1);
        }

        kor::Result<Bytes> Inflate(const Bytes& compressed, bool gzip, std::size_t limit) {
            z_stream z{};
            // 16 + MAX_WBITS: a gzip wrapper; MAX_WBITS: zlib's; deflate bodies are meant to be zlib but some
            // servers send raw deflate, which -MAX_WBITS reads — tried when zlib's header is refused.
            for (const int bits : gzip ? std::initializer_list<int>{16 + MAX_WBITS} : std::initializer_list<int>{MAX_WBITS, -MAX_WBITS}) {
                z = {};
                if (inflateInit2(&z, bits) != Z_OK) return std::unexpected(Protocol("zlib could not start"));
                Bytes out;
                z.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(compressed.data()));
                z.avail_in = uInt(compressed.size());
                int status = Z_OK;
                while (status == Z_OK) {
                    std::byte chunk[16384];
                    z.next_out = reinterpret_cast<Bytef*>(chunk);
                    z.avail_out = sizeof(chunk);
                    status = inflate(&z, Z_NO_FLUSH);
                    out.insert(out.end(), chunk, chunk + (sizeof(chunk) - z.avail_out));
                    if (out.size() > limit) { inflateEnd(&z); return std::unexpected(Protocol("the decompressed body is too large")); }
                }
                inflateEnd(&z);
                if (status == Z_STREAM_END) return out;
            }
            return std::unexpected(Protocol("the body could not be decompressed"));
        }

        kor::Task<kor::Result<Bytes>> ReadChunked(TcpStream stream, Clock::time_point deadline, std::size_t limit) {
            Bytes body;
            for (;;) {
                auto line = co_await stream.ReadUntil("\r\n", 4096, Left(deadline));
                if (!line) co_return std::unexpected(std::move(line.error()));
                std::size_t size = 0;
                const std::string_view digits = Trim(std::string_view(*line).substr(0, line->find(';')));
                if (std::from_chars(digits.data(), digits.data() + digits.size(), size, 16).ec != std::errc{} || digits.empty())
                    co_return std::unexpected(Protocol(std::format("a malformed chunk size '{}'", digits)));
                if (size == 0) {   // the last chunk, then trailers until an empty line
                    for (;;) {
                        auto trailer = co_await stream.ReadUntil("\r\n", 8192, Left(deadline));
                        if (!trailer) co_return std::unexpected(std::move(trailer.error()));
                        if (*trailer == "\r\n") co_return body;
                    }
                }
                if (body.size() + size > limit) co_return std::unexpected(Protocol("the body is too large"));
                const std::size_t at = body.size();
                body.resize(at + size);
                if (auto read = co_await stream.ReadExactly(std::span(body).subspan(at), Left(deadline)); !read)
                    co_return std::unexpected(std::move(read.error()));
                std::byte crlf[2];
                if (auto read = co_await stream.ReadExactly(crlf, Left(deadline)); !read) co_return std::unexpected(std::move(read.error()));
            }
        }

        /** One request on one connection, no redirects. */
        kor::Task<kor::Result<HttpResponse>> Exchange(const HttpRequest& request, const Url& url, Clock::time_point deadline) {
            ConnectOptions connect{.timeout = Left(deadline)};
            if (url.Secure()) connect.tls = request.tls;
            auto stream = co_await TcpStream::Connect(url.host, url.port, connect);
            if (!stream) co_return std::unexpected(std::move(stream.error()));

            std::string head = std::format("{} {} HTTP/1.1\r\nHost: {}\r\n", request.method, url.target, url.Authority());
            auto has = [&](std::string_view name) {
                return std::ranges::any_of(request.headers, [&](const HttpHeader& h) { return EqualsNoCase(h.name, name); });
            };
            if (!has("User-Agent")) head += "User-Agent: koral-net/1\r\n";
            if (!has("Accept")) head += "Accept: */*\r\n";
            if (!has("Accept-Encoding")) head += "Accept-Encoding: gzip, deflate\r\n";
            head += "Connection: close\r\n";
            if (!request.body.empty() || request.method == "POST" || request.method == "PUT" || request.method == "PATCH")
                if (!has("Content-Length")) head += std::format("Content-Length: {}\r\n", request.body.size());
            for (const auto& h : request.headers) head += std::format("{}: {}\r\n", h.name, h.value);
            head += "\r\n";
            if (auto w = co_await stream->Write(head, Left(deadline)); !w) co_return std::unexpected(std::move(w.error()));
            if (!request.body.empty())
                if (auto w = co_await stream->Write(request.body, Left(deadline)); !w) co_return std::unexpected(std::move(w.error()));

            HttpResponse response;
            for (;;) {   // 1xx responses (100 Continue) come before the real one
                auto header = co_await stream->ReadUntil("\r\n\r\n", 64 * 1024, Left(deadline));
                if (!header) co_return std::unexpected(std::move(header.error()));
                std::string_view rest = *header;
                const auto lineEnd = rest.find("\r\n");
                const std::string_view statusLine = rest.substr(0, lineEnd);
                rest.remove_prefix(lineEnd + 2);
                // HTTP/1.1 200 OK
                if (!statusLine.starts_with("HTTP/") || statusLine.size() < 12) co_return std::unexpected(Protocol(std::format("not an HTTP response: '{}'", statusLine)));
                const auto space = statusLine.find(' ');
                const std::string_view code = statusLine.substr(space + 1, 3);
                if (std::from_chars(code.data(), code.data() + 3, response.status).ec != std::errc{})
                    co_return std::unexpected(Protocol(std::format("a malformed status line '{}'", statusLine)));
                response.reason = statusLine.size() > space + 5 ? std::string(statusLine.substr(space + 5)) : "";
                response.headers.clear();
                while (!rest.empty()) {
                    const auto end = rest.find("\r\n");
                    const std::string_view line = rest.substr(0, end);
                    rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 2);
                    if (line.empty()) continue;
                    const auto colon = line.find(':');
                    if (colon == std::string_view::npos) co_return std::unexpected(Protocol(std::format("a malformed header '{}'", line)));
                    response.headers.push_back({std::string(Trim(line.substr(0, colon))), std::string(Trim(line.substr(colon + 1)))});
                }
                if (response.status >= 200 || response.status == 101) break;
            }

            const bool noBody = request.method == "HEAD" || response.status == 204 || response.status == 304 || response.status < 200;
            if (!noBody) {
                if (Lower(response.Header("Transfer-Encoding")).find("chunked") != std::string::npos) {
                    auto body = co_await ReadChunked(*stream, deadline, request.maxBody);
                    if (!body) co_return std::unexpected(std::move(body.error()));
                    response.body = std::move(*body);
                } else if (const auto length = response.Header("Content-Length"); !length.empty()) {
                    std::size_t size = 0;
                    if (std::from_chars(length.data(), length.data() + length.size(), size).ec != std::errc{})
                        co_return std::unexpected(Protocol(std::format("a malformed Content-Length '{}'", length)));
                    if (size > request.maxBody) co_return std::unexpected(Protocol("the body is too large"));
                    response.body.resize(size);
                    if (auto read = co_await stream->ReadExactly(response.body, Left(deadline)); !read) co_return std::unexpected(std::move(read.error()));
                } else {
                    auto body = co_await stream->ReadToEnd(request.maxBody, Left(deadline));
                    if (!body) co_return std::unexpected(std::move(body.error()));
                    response.body = std::move(*body);
                }
                const std::string encoding = Lower(response.Header("Content-Encoding"));
                if ((encoding == "gzip" || encoding == "x-gzip" || encoding == "deflate") && !response.body.empty()) {
                    auto inflated = Inflate(response.body, encoding != "deflate", request.maxBody);
                    if (!inflated) co_return std::unexpected(std::move(inflated.error()));
                    response.body = std::move(*inflated);
                }
            }
            stream->Close();
            response.url = url.ToString();
            co_return response;
        }
    }

    // ---- Url ----------------------------------------------------------------------------------------------

    kor::Result<Url> Url::Parse(std::string_view text) {
        Url url;
        const auto schemeEnd = text.find("://");
        if (schemeEnd == std::string_view::npos) return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("'{}' has no scheme", text)));
        url.scheme = Lower(text.substr(0, schemeEnd));
        std::uint16_t defaultPort = 0;
        if (url.scheme == "http" || url.scheme == "ws") defaultPort = 80;
        else if (url.scheme == "https" || url.scheme == "wss") defaultPort = 443;
        else return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("'{}' is not http(s) or ws(s)", url.scheme)));
        std::string_view rest = text.substr(schemeEnd + 3);
        const auto pathStart = rest.find_first_of("/?#");
        std::string_view authority = rest.substr(0, pathStart);
        if (const auto at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);   // user info: not sent
        std::string_view portText;
        if (authority.starts_with('[')) {   // [::1]:8080
            const auto close = authority.find(']');
            if (close == std::string_view::npos) return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("'{}' has a broken IPv6 address", text)));
            url.host = std::string(authority.substr(1, close - 1));
            if (close + 1 < authority.size() && authority[close + 1] == ':') portText = authority.substr(close + 2);
        } else if (const auto colon = authority.rfind(':'); colon != std::string_view::npos) {
            url.host = std::string(authority.substr(0, colon));
            portText = authority.substr(colon + 1);
        } else {
            url.host = std::string(authority);
        }
        if (url.host.empty()) return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("'{}' has no host", text)));
        url.port = defaultPort;
        if (!portText.empty() && std::from_chars(portText.data(), portText.data() + portText.size(), url.port).ec != std::errc{})
            return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("'{}' has a malformed port", text)));
        std::string_view target = pathStart == std::string_view::npos ? std::string_view("/") : rest.substr(pathStart);
        if (const auto hash = target.find('#'); hash != std::string_view::npos) target = target.substr(0, hash);   // fragments stay here
        url.target = target.empty() || target.front() != '/' ? "/" + std::string(target) : std::string(target);
        return url;
    }

    std::string Url::Authority() const {
        const bool defaultPort = ((scheme == "http" || scheme == "ws") && port == 80) || ((scheme == "https" || scheme == "wss") && port == 443);
        const std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
        return defaultPort ? h : std::format("{}:{}", h, port);
    }

    std::string Url::ToString() const { return std::format("{}://{}{}", scheme, Authority(), target); }

    kor::Result<Url> Url::Resolve(std::string_view reference) const {
        if (reference.find("://") != std::string_view::npos) return Parse(reference);
        if (reference.starts_with("//")) return Parse(scheme + ":" + std::string(reference));
        Url out = *this;
        if (reference.starts_with('/')) {
            out.target = std::string(reference);
        } else {
            std::string base = target.substr(0, target.find('?'));
            base = base.substr(0, base.rfind('/') + 1);
            out.target = base + std::string(reference);
        }
        return out;
    }

    std::string_view HttpResponse::Header(std::string_view name) const {
        for (const auto& h : headers)
            if (EqualsNoCase(h.name, name)) return h.value;
        return {};
    }

    // ---- Fetch --------------------------------------------------------------------------------------------

    kor::Task<kor::Result<HttpResponse>> Fetch(HttpRequest request) {
        const auto deadline = request.timeout > Duration::zero() ? Clock::now() + request.timeout : Clock::time_point::max();
        auto url = Url::Parse(request.url);
        if (!url) co_return std::unexpected(std::move(url.error()));
        if (url->scheme != "http" && url->scheme != "https")
            co_return std::unexpected(detail::MakeError(ErrorCode::eInvalidArgument, std::format("Fetch takes http(s), not '{}'", url->scheme)));
        for (int redirects = 0;; ++redirects) {
            auto response = co_await Exchange(request, *url, deadline);
            if (!response) co_return response;
            const int s = response->status;
            const bool redirect = s == 301 || s == 302 || s == 303 || s == 307 || s == 308;
            if (!redirect || redirects >= request.maxRedirects || response->Header("Location").empty()) co_return response;
            auto next = url->Resolve(response->Header("Location"));
            if (!next) co_return std::unexpected(Protocol(std::format("a redirect to '{}' that is not a URL", response->Header("Location"))));
            // 303 always, and 301/302 after a POST as browsers do, follow with a GET and no body.
            if (s == 303 || ((s == 301 || s == 302) && request.method == "POST")) {
                request.method = "GET";
                request.body.clear();
                std::erase_if(request.headers, [](const HttpHeader& h) { return EqualsNoCase(h.name, "Content-Type") || EqualsNoCase(h.name, "Content-Length"); });
            }
            // Credentials are not handed to another host.
            if (next->host != url->host) std::erase_if(request.headers, [](const HttpHeader& h) { return EqualsNoCase(h.name, "Authorization") || EqualsNoCase(h.name, "Cookie"); });
            url = std::move(next);
        }
    }

    kor::Task<kor::Result<HttpResponse>> HttpGet(std::string url, std::vector<HttpHeader> headers) {
        return Fetch(HttpRequest{.url = std::move(url), .headers = std::move(headers)});
    }

    kor::Task<kor::Result<HttpResponse>> HttpPost(std::string url, std::string body, std::string contentType, std::vector<HttpHeader> headers) {
        headers.push_back({"Content-Type", std::move(contentType)});
        const auto bytes = AsBytes(body);
        return Fetch(HttpRequest{.method = "POST", .url = std::move(url), .headers = std::move(headers), .body = Bytes(bytes.begin(), bytes.end())});
    }
}
