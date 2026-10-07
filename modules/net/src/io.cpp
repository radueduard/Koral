#include "io.h"

#include <format>

namespace knet::detail
{
    Io& Io::Get() {
        static Io io;
        return io;
    }

    Io::Io() : _work(asio::make_work_guard(_context)) {
        _thread = std::thread([this] {
            for (;;) {
                try {
                    _context.run();
                    return;
                } catch (...) {
                    // A completion handler threw: nothing of ours does, so it is a bug elsewhere — but the
                    // thread every socket depends on must not die of it.
                }
            }
        });
    }

    Io::~Io() {
        _work.reset();
        _context.stop();
        if (_thread.joinable()) _thread.join();
    }

    kor::Error FromAsio(const asio::error_code& error, std::string_view what, kor::ErrorCode code) {
        if (error == asio::error::operation_aborted) code = kor::ErrorCode::eNetwork;
        if (error == asio::error::eof || error == asio::error::connection_reset || error == asio::error::broken_pipe
            || error == asio::error::connection_aborted)
            code = kor::ErrorCode::eConnectionClosed;
        if (error == asio::error::timed_out) code = kor::ErrorCode::eTimedOut;
        return kor::Error{code, std::format("{}: {}", what, error.message())};
    }

    kor::Error MakeError(kor::ErrorCode code, std::string message) { return kor::Error{code, std::move(message)}; }
}
