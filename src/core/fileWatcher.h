//
// Created by radue on 21.06.2026.
//

#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>

#include "task.h"

namespace gfx
{

    class FileWatcher
    {
        inline static std::unordered_set<std::filesystem::path> _trackedFiles {};
        inline static std::unordered_map<std::filesystem::path, std::filesystem::file_time_type> _lastWriteTimes {};
        // Multiple callbacks per file: several shaders may share an #include/import, and each
        // must be reloaded when that shared file changes.
        inline static std::unordered_map<std::filesystem::path, std::vector<std::function<void()>>> _callbacks {};
        inline static bool _stopped = false;

        // Guards the maps above: TrackFile/UntrackFile run on the main thread while Run() polls
        // on a background thread, and reload callbacks (invoked by Run) may themselves TrackFile
        // newly discovered dependencies.
        inline static std::mutex _mutex {};

    public:
        static Task<void> Run();
        static void Stop();
        static void TrackFile(const std::filesystem::path& filePath, const std::function<void()>& callback);
        static void UntrackFile(const std::filesystem::path& filePath);
    };
}