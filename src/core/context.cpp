//
// Created by radue on 2/26/2026.
//


#include <surface.h>
#include <framebuffer.h>
#include "context.h"
#include "paths.h"
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <GLFW/glfw3.h>

#include "scheduler.h"
#include "shader.h"
#include "window.h"
#include "resource.h"
#include "../executor/BackgroundExecutor.h"
#include "../executor/MainThreadExecutor.h"
#include "../backends/vulkan/device.h"
#include "../backends/vulkan/vulkanContext.h"

// initLibs.cpp — seeds GLFW's Vulkan loader; must run before glfwInit().
void initGlfwVulkanLoader();


namespace {
    // Resolve a relative path against a list of roots, first existing wins.
    //
    // The working directory is the last resort, after every root. Before the search roots existed a
    // relative path simply meant "relative to wherever you launched from", and some code still
    // passes one — this keeps those callers working rather than silently failing to find a file they
    // used to find. It goes last so it can never shadow a project's own directories.
    //
    // When nothing matches at all, the path is still joined onto the first root: the caller's own
    // "file not found" then names somewhere the user can actually go and look, instead of an empty
    // string or a bare file name.
    std::filesystem::path resolveAgainstRoots(const std::filesystem::path& relativePath,
                                              const std::vector<std::filesystem::path>& roots)
    {
        std::error_code ec;
        for (const auto& root : roots) {
            if (auto candidate = root / relativePath; std::filesystem::exists(candidate, ec))
                return candidate;
        }

        if (std::filesystem::exists(relativePath, ec)) return relativePath;

        return roots.empty() ? relativePath : roots.front() / relativePath;
    }

    // Process-wide, and deliberately not an inline static in the header: the executable and the
    // shared library would then each get their own copy, and a root registered by one would be
    // invisible to the other.
    std::vector<std::filesystem::path>& assetSearchPathsStorage()
    {
        // Seeded with wherever Koral's own assets/ actually landed — beside the installed library,
        // or in the source tree for a dev build. Projects prepend their own; see addAssetSearchPath.
        static std::vector<std::filesystem::path> paths =
            kor::detail::dataRoots("assets", "KORAL_ASSETS_DIR", ASSETS_PATH);
        return paths;
    }
}

void kor::addAssetSearchPath(const std::filesystem::path& dir, const bool front)
{
    auto& paths = assetSearchPathsStorage();
    if (dir.empty() || std::ranges::find(paths, dir) != paths.end()) return;
    if (front) paths.insert(paths.begin(), dir);
    else       paths.push_back(dir);
}

const std::vector<std::filesystem::path>& kor::assetSearchPaths() { return assetSearchPathsStorage(); }

std::filesystem::path kor::assetPath(const std::filesystem::path& relativePath)
{
    // An absolute path is an answer already, not a question — resolving it against a root would
    // only produce nonsense.
    if (relativePath.is_absolute()) return relativePath;

    return resolveAgainstRoots(relativePath, assetSearchPaths());
}

std::filesystem::path kor::shaderPath(const std::filesystem::path& relativePath)
{
    if (relativePath.is_absolute()) return relativePath;

    // The registered shader roots: the install roots, plus anything the project or its koral.json
    // added via Shader::addSearchPath.
    return resolveAgainstRoots(relativePath, Shader::searchPaths());
}

kor::Window& kor::Context::Window()
{
    if (_window == nullptr) {
        throw std::runtime_error("No window is linked to the current thread!");
    }
    return *_window;
}

const kor::Scheduler& kor::Context::Scheduler()
{
    if (_scheduler == nullptr) {
        throw std::runtime_error("No scheduler is linked to the current thread!");
    }
    return *_scheduler;
}

kor::ResourceRef<const kor::Framebuffer> kor::Context::DefaultFramebuffer()
{
    if (_window == nullptr)
    {
        throw std::runtime_error("There is no default framebuffer!");
    }
    return _window->getFramebuffer();
}

ImGuiContext* kor::Context::GetCurrentImGuiContext()
{
    if (_imguiContext == nullptr) {
        throw std::runtime_error("ImGui context is not initialized for this thread");
    }
    return _imguiContext;
}

kor::SwitchAwaiter kor::Context::SwitchToMainThread() {
    if (!_mainThreadExecutor) {
        throw std::runtime_error("Main thread executor is not initialized for this thread!");
    }
    return _mainThreadExecutor->SwitchToMainThread();
}

kor::SwitchAwaiter kor::Context::SwitchToBackgroundThread() {
    if (!_backgroundExecutor) {
        throw std::runtime_error("Background thread executor is not initialized for this thread!");
    }
    return _backgroundExecutor->SwitchToBackground();
}

void kor::Context::DrainMainThread() {
    if (!_mainThreadExecutor) {
        throw std::runtime_error("Main thread executor is not initialized for this thread!");
    }
    _mainThreadExecutor->Drain();
}

kor::Repository & kor::Context::Repository() {
    if (!_repository) {
        throw std::runtime_error("Resource repository is not initialized for this thread!");
    }
    return *_repository;
}

kor::API kor::Context::activeAPI()
{
    return _activeAPI;
}

bool kor::Context::IsHeadless()
{
    return _headless;
}

bool kor::Context::SupportsRayTracing()
{
    if (_activeAPI != API::eVulkan) return false;
    if (_window == nullptr && !_headless) return false;
    return kor::vk::Context::Device().supportsRayTracing();
}

void kor::Context::InitHeadless(const API api)
{
    if (_window != nullptr)
        throw std::runtime_error("Context::InitHeadless: a window is already active; headless mode is mutually exclusive with a window.");
    if (_headless)
        throw std::runtime_error("Context::InitHeadless: a headless context is already active.");
    if (api != API::eVulkan)
        throw std::runtime_error("Context::InitHeadless: only the Vulkan backend supports headless (device-only) mode.");

    _activeAPI = api;

    // GLFW is initialized so the Vulkan runtime can query platform instance
    // extensions; no window is created. A failure here is non-fatal — a
    // compute/transfer-only instance does not require the WSI surface extensions.
    initGlfwVulkanLoader(); // init hint — must precede glfwInit() (see initLibs.cpp)
    glfwInit();

    kor::vk::Context::Init();            // instance + physical/logical device + allocator + descriptor pool

    // Same async machinery as a window: a Job's Initialize() returns a Task and
    // may co_await background work, so the executors must exist even headless.
    _mainThreadExecutor = new MainThreadExecutor();
    _backgroundExecutor = new BackgroundExecutor();

    _repository = new kor::Repository(); // pipelines register themselves here on build
                                         // (qualified: Context::Repository() the method shadows the type here)
    _headless = true;
}

void kor::Context::ShutdownHeadless()
{
    if (!_headless) return;

    // Destroy the repository first: its destructor stops the FileWatcher and waits
    // for that coroutine to finish, which requires the background executor to still
    // be alive so the worker can run the watcher to completion.
    delete _repository;
    _repository = nullptr;

    delete _mainThreadExecutor;
    _mainThreadExecutor = nullptr;
    delete _backgroundExecutor;
    _backgroundExecutor = nullptr;

    kor::vk::Context::Destroy();
    glfwTerminate();
    _headless = false;
}
