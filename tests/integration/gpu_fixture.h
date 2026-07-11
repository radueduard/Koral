// Shared harness for GPU integration tests.
//
// A single Vulkan device is booted once for the whole binary (through a small
// window + empty scene, since the framework has no headless init path). Tests
// derive from GpuTest, which skips them if the device could not be created
// (e.g. no GPU or no display) so the suite stays safe to run anywhere.
#pragma once

#include <gtest/gtest.h>

class GpuEnvironment : public ::testing::Environment {
public:
    void SetUp() override;
    void TearDown() override;

    // True once a Vulkan device has been successfully created.
    static bool ready();
};

class GpuTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!GpuEnvironment::ready()) {
            GTEST_SKIP() << "No Vulkan device/display available; skipping GPU test.";
        }
    }
};
