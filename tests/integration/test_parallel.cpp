// ParallelFor on the real background pool, and the uploads that no longer make the CPU wait: what is
// read back — by a one-off after them, or awaited — must still be what was written.

#include "gpu_fixture.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "image.h"
#include "parallel.h"
#include "task.h"

using namespace kor;

namespace {

class ParallelTest : public GpuTest {};

void DrainUntil(const auto& done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        Context::DrainMainThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

TEST_F(ParallelTest, EveryIndexRunsExactlyOnce) {
    constexpr std::size_t count = 100'000;
    std::vector<std::atomic<int>> hits(count);
    ParallelFor(0, count, [&](const std::size_t i) { hits[i].fetch_add(1); }).Wait();
    for (std::size_t i = 0; i < count; ++i) ASSERT_EQ(hits[i].load(), 1) << "index " << i;
}

TEST_F(ParallelTest, RunsOnSeveralThreadsAndTheWaiterHelps) {
    if (std::thread::hardware_concurrency() < 2) GTEST_SKIP() << "one core";
    std::mutex mutex;
    std::set<std::thread::id> threads;
    ParallelFor(0, 64, [&](std::size_t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::lock_guard lock(mutex);
        threads.insert(std::this_thread::get_id());
    }, 1).Wait();
    EXPECT_GT(threads.size(), 1u) << "the parts ran on more than one thread";
}

TEST_F(ParallelTest, RangesCoverTheWholeRangeWithoutOverlap) {
    std::vector<int> covered(1000, 0);
    ParallelForRanges(10, 1000, [&](const std::size_t first, const std::size_t last) {
        for (std::size_t i = first; i < last; ++i) ++covered[i];
    }, 37).Wait();
    for (std::size_t i = 0; i < 10; ++i) EXPECT_EQ(covered[i], 0);
    for (std::size_t i = 10; i < 1000; ++i) ASSERT_EQ(covered[i], 1) << i;
}

TEST_F(ParallelTest, AnEmptyRangeIsDoneAtOnce) {
    auto work = ParallelFor(5, 5, [](std::size_t) { FAIL() << "nothing to run"; });
    EXPECT_TRUE(work.Ready());
    work.Wait();
}

TEST_F(ParallelTest, WhatTheBodyThrowsIsThrownWhereItIsWaitedFor) {
    auto work = ParallelFor(0, 100, [](const std::size_t i) {
        if (i == 57) throw std::runtime_error("fifty-seven");
    });
    EXPECT_THROW(work.Wait(), std::runtime_error);
}

TEST_F(ParallelTest, NestedLoopsDoNotDeadlock) {
    std::atomic<int> total{0};
    ParallelFor(0, 16, [&](std::size_t) {
        ParallelFor(0, 16, [&](std::size_t) { total.fetch_add(1); }).Wait();
    }, 1).Wait();
    EXPECT_EQ(total.load(), 256);
}

Task<void> SumAsync(const std::vector<int>& values, std::atomic<long long>& sum, std::thread::id& resumedOn) {
    co_await ParallelFor(0, values.size(), [&](const std::size_t i) { sum.fetch_add(values[i]); });
    resumedOn = std::this_thread::get_id();
}

TEST_F(ParallelTest, AnAwaitingCoroutineResumesWhereItWas) {
    std::vector<int> values(10'000);
    std::iota(values.begin(), values.end(), 1);
    std::atomic<long long> sum{0};
    std::thread::id resumedOn;
    auto task = SumAsync(values, sum, resumedOn);
    DrainUntil([&] { return task.Done(); });
    ASSERT_TRUE(task.Done());
    EXPECT_EQ(sum.load(), 10'000LL * 10'001 / 2);
    EXPECT_EQ(resumedOn, std::this_thread::get_id()) << "started on the main thread, resumed there";
}

Task<void> ThrowsAsync(bool& caught) {
    try {
        co_await ParallelFor(0, 10, [](std::size_t) { throw std::logic_error("no"); });
    } catch (const std::logic_error&) {
        caught = true;
    }
}

TEST_F(ParallelTest, AnAwaitRethrowsToo) {
    bool caught = false;
    auto task = ThrowsAsync(caught);
    DrainUntil([&] { return task.Done(); });
    EXPECT_TRUE(caught);
}

// ---- uploads the CPU does not wait for ----------------------------------------------------------

TEST_F(ParallelTest, AReadAfterWritesSeesEveryWrite) {
    auto buffer = Buffer::Builder<std::uint32_t>()
        .SetInstanceCount(256)
        .SetType(Buffer::Type::eDeviceLocal)
        .Build();
    ASSERT_TRUE(buffer.Valid());
    // Several writes in a row, none waited for, then a read: the read is a one-off after them.
    for (std::uint32_t round = 0; round < 4; ++round) {
        std::vector<std::uint32_t> values(256);
        std::iota(values.begin(), values.end(), round * 1000);
        buffer->Write(values);
    }
    const auto read = buffer->Read<std::uint32_t>();
    ASSERT_EQ(read.size(), 256u);
    for (std::uint32_t i = 0; i < 256; ++i) ASSERT_EQ(read[i], 3000 + i) << i;
}

TEST_F(ParallelTest, InitialDataIsThereForTheFirstRead) {
    std::vector<float> values(1024);
    std::iota(values.begin(), values.end(), 0.5f);
    auto buffer = Buffer::Builder<float>().SetData(values).SetType(Buffer::Type::eDeviceLocal).Build();
    EXPECT_EQ(buffer->Read<float>(), values);
    EXPECT_EQ(buffer->ReadAt<float>(700), 700.5f);
}

TEST_F(ParallelTest, AnImagesPixelsAreThereForTheFirstCopy) {
    std::vector<std::uint32_t> pixels(16 * 16);
    std::iota(pixels.begin(), pixels.end(), 1u);
    auto image = Image::Builder()
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(glm::uvec2(16, 16))
        .SetData(pixels)
        .SetMipLevels(3)
        .Build();
    auto readback = Buffer::Builder<std::uint32_t>()
        .SetInstanceCount(16 * 16)
        .SetUsage(Buffer::Usage::eTransferDst)
        .SetType(Buffer::Type::eReadback)
        .Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) { cb.CopyImageToBuffer(image, readback); }).Wait();
    EXPECT_EQ(readback->Read<std::uint32_t>(), pixels);
}

Task<void> ReadLater(const Buffer& buffer, std::vector<int>& into) {
    into = co_await buffer.ReadAsync<int>();
}

TEST_F(ParallelTest, ReadAsyncResumesWithTheData) {
    std::vector<int> values(512);
    std::iota(values.begin(), values.end(), -100);
    auto buffer = Buffer::Builder<int>().SetData(values).SetType(Buffer::Type::eDeviceLocal).Build();
    std::vector<int> read;
    auto task = ReadLater(*buffer, read);
    DrainUntil([&] { return task.Done(); });
    ASSERT_TRUE(task.Done());
    EXPECT_EQ(read, values);
}

} // namespace
