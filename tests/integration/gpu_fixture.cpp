#include "gpu_fixture.h"

#include <exception>

#include "context.h"
#include "log.h"

namespace {
    bool g_ready = false;
}

void GpuEnvironment::SetUp() {
    // Bring up a device-only (headless) context: no window, surface or swap chain,
    // just enough to allocate buffers and run compute/transfer commands. This
    // works anywhere a Vulkan device exists — no display required.
    try {
        kor::Context::InitHeadless(kor::API::eVulkan);
        g_ready = true;
    } catch (const std::exception& e) {
        kor::log::error("GPU integration harness: headless device init failed: {}", e.what());
        g_ready = false;
    } catch (...) {
        kor::log::error("GPU integration harness: headless device init failed (unknown exception)");
        g_ready = false;
    }
}

void GpuEnvironment::TearDown() {
    if (g_ready) {
        kor::Context::ShutdownHeadless();
        g_ready = false;
    }
}

bool GpuEnvironment::ready() { return g_ready; }

// Registered before RUN_ALL_TESTS runs (compatible with gtest_main). gtest owns
// and deletes the environment.
static ::testing::Environment* const kGpuEnv =
    ::testing::AddGlobalTestEnvironment(new GpuEnvironment);
