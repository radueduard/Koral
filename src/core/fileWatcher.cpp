//
// Created by radue on 21.06.2026.
//

#include "fileWatcher.h"

#include <thread>

#include "context.h"

kor::Task<void> kor::FileWatcher::Run()
{
    co_await kor::Context::SwitchToBackgroundThread();
    co_return []
    {
        while (!_stopped)
        {
            // Snapshot the tracked paths under the lock, then poll and dispatch without holding
            // it: a reload callback may recompile a shader and TrackFile/UntrackFile new
            // dependencies, which would deadlock or invalidate iterators if done under the lock.
            std::vector<std::filesystem::path> paths;
            {
                std::scoped_lock lock(_mutex);
                paths.assign(_trackedFiles.begin(), _trackedFiles.end());
            }

            for (const auto& filePath : paths)
            {
                // error_code overload: a file being saved may briefly not exist (atomic
                // rename); skip this tick rather than throwing.
                std::error_code ec;
                const auto lastWriteTime = std::filesystem::last_write_time(filePath, ec);
                if (ec) continue;

                std::vector<std::function<void()>> due;
                {
                    std::scoped_lock lock(_mutex);
                    if (!_trackedFiles.contains(filePath)) continue; // untracked since the snapshot
                    if (lastWriteTime == _lastWriteTimes[filePath]) continue;
                    _lastWriteTimes[filePath] = lastWriteTime;
                    due = _callbacks[filePath]; // copy so callbacks may mutate the map safely
                }
                for (const auto& callback : due) callback();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    } ();
}

void kor::FileWatcher::Stop()
{
    _stopped = true;
    for (const auto trackedFiles = _trackedFiles; const auto& filePath : trackedFiles)
    {
        UntrackFile(filePath);
    }
}

void kor::FileWatcher::TrackFile(const std::filesystem::path& filePath, const std::function<void()>& callback)
{
    std::error_code ec;
    const auto lastWriteTime = std::filesystem::last_write_time(filePath, ec);

    std::scoped_lock lock(_mutex);
    _trackedFiles.insert(filePath);
    _lastWriteTimes[filePath] = lastWriteTime; // ec -> epoch time; a first change re-syncs it
    _callbacks[filePath].push_back(callback);
}

void kor::FileWatcher::UntrackFile(const std::filesystem::path& filePath)
{
    std::scoped_lock lock(_mutex);
    _trackedFiles.erase(filePath);
    _lastWriteTimes.erase(filePath);
    _callbacks.erase(filePath);
}
