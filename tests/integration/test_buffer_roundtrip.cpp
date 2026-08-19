// Integration tests for kor::Buffer against a real Vulkan device. These exercise
// VMA allocation, staging buffers, the transfer queue and SingleTimeCommand
// submit/fence-wait — i.e. the actual GPU upload/readback paths.

#include "gpu_fixture.h"

#include <array>
#include <numeric>
#include <ranges>
#include <vector>

#include "buffer.h"

using kor::Buffer;

namespace {

std::vector<int> iotaVec(std::size_t n, int start = 0) {
    std::vector<int> v(n);
    std::iota(v.begin(), v.end(), start);
    return v;
}

// Device-local buffers can't be mapped: build() uploads through a staging buffer
// and a transfer-queue copy, and Read() copies back the same way. A correct
// round-trip proves that whole path works.
TEST_F(GpuTest, DeviceLocalRoundTrip) {
    const std::vector<int> src = iotaVec(256);

    Buffer::Builder<int> b;
    b.setData(src);
    b.addUsage(Buffer::Usage::eStorage);
    b.addUsage(Buffer::Usage::eTransferSrc);
    b.addUsage(Buffer::Usage::eTransferDst);
    b.setType(Buffer::Type::eDeviceLocal);

    auto buf = b.build();
    ASSERT_TRUE(static_cast<bool>(buf));
    EXPECT_EQ(buf->getType(), Buffer::Type::eDeviceLocal);
    EXPECT_EQ(buf->getSize(), src.size() * sizeof(int));

    const std::vector<int> out = buf->Read<int>();
    EXPECT_EQ(out, src);
}

// Host-visible (staging) buffers upload/read through a persistent mapping.
TEST_F(GpuTest, StagingRoundTrip) {
    const std::vector<float> src = {1.5f, -2.0f, 3.25f, 42.0f, 0.0f};

    Buffer::Builder<float> b;
    b.setData(src);
    b.addUsage(Buffer::Usage::eTransferSrc);
    b.addUsage(Buffer::Usage::eTransferDst);
    b.setType(Buffer::Type::eStaging);

    auto buf = b.build();
    const std::vector<float> out = buf->Read<float>();
    ASSERT_EQ(out.size(), src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        EXPECT_FLOAT_EQ(out[i], src[i]) << "at index " << i;
    }
}

// Single-element write/read at an offset on a device-local buffer (each op goes
// through its own staging buffer + transfer submit).
TEST_F(GpuTest, DeviceLocalWriteAtReadAt) {
    Buffer::Builder<int> b;
    b.setData(iotaVec(16));
    b.addUsage(Buffer::Usage::eStorage);
    b.addUsage(Buffer::Usage::eTransferSrc);
    b.addUsage(Buffer::Usage::eTransferDst);
    b.setType(Buffer::Type::eDeviceLocal);
    auto buf = b.build();

    buf->WriteAt<int>(7, 12345);
    EXPECT_EQ(buf->ReadAt<int>(7), 12345);
    EXPECT_EQ(buf->ReadAt<int>(0), 0); // untouched neighbour
    EXPECT_EQ(buf->ReadAt<int>(8), 8);
}

// Partial reads with count/offset return the right sub-range.
TEST_F(GpuTest, PartialRead) {
    const std::vector<int> src = iotaVec(100);

    Buffer::Builder<int> b;
    b.setData(src);
    b.addUsage(Buffer::Usage::eTransferSrc);
    b.addUsage(Buffer::Usage::eTransferDst);
    b.setType(Buffer::Type::eStaging);
    auto buf = b.build();

    const std::vector<int> mid = buf->Read<int>(/*count*/10, /*offset*/50);
    ASSERT_EQ(mid.size(), 10u);
    EXPECT_EQ(mid.front(), 50);
    EXPECT_EQ(mid.back(), 59);
}

// A too-large read is rejected before touching the GPU.
TEST_F(GpuTest, OutOfRangeReadThrows) {
    Buffer::Builder<int> b;
    b.setData(iotaVec(8));
    b.addUsage(Buffer::Usage::eTransferSrc);
    b.addUsage(Buffer::Usage::eTransferDst);
    b.setType(Buffer::Type::eStaging);
    auto buf = b.build();

    EXPECT_THROW((void)buf->Read<int>(/*count*/4, /*offset*/6), std::out_of_range);
}


// Anything a range can be, an upload can take: a vector, an array, a span, or a view that has no
// memory behind it at all. The last is the case a span-only API could not express — the elements
// do not exist until they are walked, so they are gathered into a temporary on the way to the GPU.
TEST_F(GpuTest, UploadsTakeAnyRange) {
    // A lazy view: 16 squares, computed as they are read.
    auto squares = std::views::iota(0, 16) | std::views::transform([](const int i) { return i * i; });
    static_assert(!std::ranges::contiguous_range<decltype(squares)>, "the interesting case is the one with no buffer");

    Buffer::Builder<int> fromView;
    fromView.setData(squares);
    fromView.addUsage(Buffer::Usage::eTransferSrc);
    fromView.addUsage(Buffer::Usage::eTransferDst);
    fromView.setType(Buffer::Type::eStaging);
    const auto viewBuffer = fromView.build();
    ASSERT_TRUE(viewBuffer.valid()) << (viewBuffer.error() ? viewBuffer.error()->history() : "");

    const auto readBack = viewBuffer->Read<int>();
    ASSERT_EQ(readBack.size(), 16u);
    EXPECT_EQ(readBack[3], 9);
    EXPECT_EQ(readBack.back(), 225);

    // And the contiguous spellings, none of which need a std::span written around them.
    const std::vector vector{1, 2, 3, 4};
    const std::array array{5, 6, 7, 8};

    Buffer::Builder<int> fromVector;
    fromVector.setData(vector);
    fromVector.addUsage(Buffer::Usage::eTransferSrc);
    fromVector.addUsage(Buffer::Usage::eTransferDst);
    fromVector.setType(Buffer::Type::eStaging);
    const auto vectorBuffer = fromVector.build();
    EXPECT_EQ(vectorBuffer->Read<int>(), vector);

    Buffer::Builder<int> fromArray;
    fromArray.setDataView(array);          // viewed where it lies, not copied until build()
    fromArray.addUsage(Buffer::Usage::eTransferSrc);
    fromArray.addUsage(Buffer::Usage::eTransferDst);
    fromArray.setType(Buffer::Type::eStaging);
    const auto arrayBuffer = fromArray.build();
    ASSERT_TRUE(arrayBuffer.valid());
    EXPECT_EQ(arrayBuffer->Read<int>()[2], 7);

    // Write takes the same variety, including the view.
    arrayBuffer->Write(std::vector{9, 9, 9, 9});
    EXPECT_EQ(arrayBuffer->Read<int>()[0], 9);
    arrayBuffer->Write(std::views::iota(0, 4) | std::views::transform([](const int i) { return i + 100; }));
    EXPECT_EQ(arrayBuffer->Read<int>()[3], 103);
}

} // namespace
