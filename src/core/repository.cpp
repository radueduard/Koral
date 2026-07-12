//
// Created by radue on 11.07.2026.
//

#include "resource.h"

#include <chrono>
#include <thread>

#include "fileWatcher.h"

namespace kor
{
    // Out of line so that resource.h — a public header — does not have to include fileWatcher.h,
    // which is internal. A shipped SDK has no src/ tree, so a public header that reaches into one
    // simply does not compile for the consumer.
    Repository::Repository()
    {
        _fileWatcherTask = FileWatcher::Run();
    }

    Repository::~Repository()
    {
        // Signal the watcher loop to exit, then wait for the coroutine to actually reach its final
        // suspend before the Task frees the frame. Destroying the handle while a background worker
        // is still executing inside it (e.g. mid sleep_for) frees the coroutine frame out from under
        // that thread and corrupts the heap. take() is non-blocking, so poll done() here — this runs
        // while the background executor is still alive, so the worker can finish.
        FileWatcher::Stop();
        while (!_fileWatcherTask.done())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto result = _fileWatcherTask.take();
    }
}
