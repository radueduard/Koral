//
// koral-net's one I/O thread, and how an asynchronous operation on it becomes something a coroutine awaits.
//
// Every socket lives on this thread's io_context, and every operation on a socket is *started* there too
// (posted), so a socket is only ever touched by one thread — what asio requires of a shared object. An
// operation's completion fills an Op and signals its kor::Token; whoever awaits the token resumes where
// tokens resume (the main thread if it suspended there, the background pool otherwise, or right here on the
// I/O thread when there is no application). Nothing here blocks the I/O thread.
//

#pragma once

#include <memory>
#include <future>
#include <optional>
#include <thread>

#include <asio.hpp>

#include <error.h>
#include <task.h>
#include <token.h>

namespace knet::detail
{
    class Io {
    public:
        /** The process's I/O thread, started the first time anything asks for it. */
        static Io& Get();

        asio::io_context& Context() { return _context; }
        asio::any_io_executor Executor() { return _context.get_executor(); }
        /** Whether the caller is the I/O thread. */
        [[nodiscard]] bool OnIoThread() const { return std::this_thread::get_id() == _thread.get_id(); }

        ~Io();

    private:
        Io();
        asio::io_context _context;
        asio::executor_work_guard<asio::io_context::executor_type> _work;
        std::thread _thread;
    };

    /** The state one operation fills in on the I/O thread and its awaiter reads. */
    template<class T>
    struct Op {
        kor::Token done = kor::Token::Create();
        std::optional<kor::Result<T>> result;
        asio::cancellation_signal cancel;

        void Finish(kor::Result<T> value) {
            if (result) return;   // a timeout and the operation itself may both try
            result = std::move(value);
            done.Signal();
        }
    };

    /** An Error of `code` from a system error. */
    kor::Error FromAsio(const asio::error_code& error, std::string_view what, kor::ErrorCode code = kor::ErrorCode::eNetwork);
    kor::Error MakeError(kor::ErrorCode code, std::string message);

    /**
     * Starts `start(op)` on the I/O thread and awaits its result. A cancelled task (kor::Task::Cancel) cancels
     * the operation too, through the op's cancellation signal, which `start` binds to what it begins.
     */
    template<class T, class Start>
    kor::Task<kor::Result<T>> Run(Start start) {
        auto op = std::make_shared<Op<T>>();
        asio::post(Io::Get().Context(), [op, start = std::move(start)]() mutable { start(op); });
        try {
            co_await op->done;
        } catch (const kor::Cancelled&) {
            asio::post(Io::Get().Context(), [op] { op->cancel.emit(asio::cancellation_type::all); });
            throw;
        }
        co_return std::move(*op->result);
    }

    /**
     * `co_await ToIoThread()`: the rest of the coroutine runs on the I/O thread. What it then awaits resumes off the
     * main thread (on the background pool, or the I/O thread with no application) — for code that must be waited for
     * by blocking, on any thread, without a frame having to run.
     */
    struct ToIoThread {
        bool await_ready() const noexcept { return Io::Get().OnIoThread(); }
        void await_suspend(std::coroutine_handle<> h) const { asio::post(Io::Get().Context(), [h] { h.resume(); }); }
        void await_resume() const noexcept {}
    };

    /** Runs `work` on the I/O thread and waits for it: for the few synchronous calls that touch a socket. */
    template<class F>
    auto OnIo(F&& work) -> decltype(work()) {
        if (Io::Get().OnIoThread()) return work();
        using R = decltype(work());
        std::promise<R> promise;
        auto future = promise.get_future();
        asio::post(Io::Get().Context(), [&] {
            if constexpr (std::is_void_v<R>) { work(); promise.set_value(); }
            else promise.set_value(work());
        });
        return future.get();
    }
}
