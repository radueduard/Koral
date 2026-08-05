// What each Buffer::Type costs the CPU to write and to read.
//
// Buffer::Type is a placement policy, and the placement decides three different things that pull
// against each other: how fast the GPU reaches the memory, how fast the CPU writes it, and how fast
// the CPU reads it back. No single memory type wins all three on a discrete GPU, so the choice is a
// trade and this prints the terms of it.
//
// It matters most on a system with a resizable BAR, where a DEVICE_LOCAL | HOST_VISIBLE memory type
// exists: the GPU reaches it at full speed and the CPU can write it directly, but it is
// write-combined, so CPU *reads* from it are uncached and very slow. Which of those properties a
// Buffer::Type gets is decided by the VMA usage and host-access flags in
// src/backends/vulkan/buffer.cpp.
//
// A measurement, not an assertion: it prints and passes. The numbers are the point.

#include "gpu_fixture.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <span>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "shader.h"

using kor::Buffer;
using kor::CommandBuffer;
using kor::ComputePipeline;
using kor::Descriptor;
using kor::DescriptorSet;
using kor::ResourceRef;
using kor::Shader;

namespace {

constexpr glm::u64 kElements = 4u << 20;   // 16 MiB of uint32
constexpr int kRepeats = 5;

// Reduced by minimum over repeats: a transfer can only be *slowed* by something else on the
// machine, so the fastest run is the one least polluted by it.
template<typename Func>
double fastest(Func&& body) {
    double best = 1e9;
    for (int repeat = 0; repeat < kRepeats; ++repeat) {
        const auto start = std::chrono::steady_clock::now();
        body();
        best = std::min(best, std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    return best;
}

const char* name(const Buffer::Type type) {
    switch (type) {
        case Buffer::Type::eDeviceLocal: return "eDeviceLocal";
        case Buffer::Type::eStaging:     return "eStaging";
        case Buffer::Type::eReadback:    return "eReadback";
        case Buffer::Type::eDynamic:     return "eDynamic";
        case Buffer::Type::eDeviceDynamic: return "eDeviceDynamic";
    }
    return "?";
}

TEST_F(GpuTest, MeasureHostAccessCostPerBufferType) {
    std::vector<std::uint32_t> source(kElements);
    std::iota(source.begin(), source.end(), 0u);

    const double megabytes = static_cast<double>(kElements * sizeof(std::uint32_t)) / (1024.0 * 1024.0);

    // The other side of the trade: how fast the *GPU* reaches the same memory. Doubles every
    // element in place, so it both reads and writes the whole buffer once per dispatch.
    Shader::Builder shaderBuilder;
    shaderBuilder.setPath("doubleValues.comp.glsl");
    auto shader = shaderBuilder.build();
    ASSERT_TRUE(shader.valid());

    ComputePipeline::Builder pipeBuilder;
    pipeBuilder.setComputeShader(shader);
    auto pipeline = pipeBuilder.build();
    ASSERT_TRUE(pipeline.valid());

    constexpr std::uint32_t kLocalSize = 64;
    constexpr int kDispatches = 8;

    for (const auto type : { Buffer::Type::eDeviceLocal, Buffer::Type::eStaging, Buffer::Type::eReadback, Buffer::Type::eDynamic, Buffer::Type::eDeviceDynamic }) {
        Buffer::RawBuilder builder;
        builder.setRawSize(static_cast<glm::i64>(kElements * sizeof(std::uint32_t)))
               .addUsage(Buffer::Usage::eStorage)
               .addUsage(Buffer::Usage::eTransferSrc)
               .addUsage(Buffer::Usage::eTransferDst)
               .setType(type);
        auto buffer = builder.build();
        ASSERT_TRUE(buffer.valid()) << "could not allocate a " << name(type) << " buffer";

        // eDeviceLocal is not host-visible, so it is measured for GPU throughput only — it is the
        // reference the host-visible types are being compared against.
        const bool hostVisible = type != Buffer::Type::eDeviceLocal;

        double write = 0.0, read = 0.0;
        if (hostVisible) {
            write = fastest([&] {
                buffer->Write(std::span<const std::uint32_t>(source), 0);
            });

            // The read is what punishes write-combined memory, and the one an engine is most
            // likely to do without realising what it costs.
            // One read, not the usual best-of-five: on write-combined memory a single pass takes
            // most of a second, and repeating it would dominate the suite's runtime to no purpose.
            std::vector<std::uint32_t> destination;
            if (type == Buffer::Type::eDeviceDynamic) {
                const auto start = std::chrono::steady_clock::now();
                destination = buffer->Read<std::uint32_t>();
                read = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            } else {
                read = fastest([&] { destination = buffer->Read<std::uint32_t>(); });
            }
            ASSERT_EQ(destination.size(), source.size());
        }

        auto descriptorSet =
            DescriptorSet::Builder(ResourceRef<const kor::Pipeline>(pipeline), 0)
                .write(0, Descriptor(ResourceRef<const Buffer>(buffer)))
                .build();
        ASSERT_TRUE(descriptorSet.valid());

        const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
        double gpu = 0.0;
        if (cb->supportsTimers()) {
            cb->Begin();
            cb->BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
            cb->BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
            cb->BeginTimer("pass");
            for (int i = 0; i < kDispatches; ++i)
                cb->Dispatch(static_cast<glm::u32>(kElements / kLocalSize), 1, 1);
            cb->EndTimer();
            cb->End();
            ASSERT_TRUE(cb->Submit());
            cb->WaitForFence();
            if (const auto measured = cb->CollectTimer("pass")) gpu = *measured;
        }
        // Each dispatch reads and writes every element once.
        const double gpuGiBs = gpu > 0.0 ? megabytes * 2.0 * kDispatches / gpu * 1000.0 / 1024.0 : 0.0;

        std::cout << "[ MEASURE  ] " << name(type) << ": ";
        if (hostVisible) {
            std::cout << "host write " << megabytes / write * 1000.0 / 1024.0 << " GiB/s"
                      << ", host read " << megabytes / read * 1000.0 / 1024.0 << " GiB/s, ";
        } else {
            std::cout << "host write -, host read -, ";
        }
        std::cout << "GPU " << gpuGiBs << " GiB/s (" << gpu << " ms for "
                  << kDispatches << " passes)" << std::endl;
    }

    SUCCEED();
}

// eDeviceDynamic is the newest placement and the one with a fallback path, so this pins the part
// that has to hold everywhere: whatever memory it lands in, a CPU write must be visible to the GPU
// without an explicit transfer, and the contents must survive a round trip. Correctness only — the
// throughput it is *for* is what the measurement above reports.
TEST_F(GpuTest, DeviceDynamicIsHostWritableAndGpuVisible) {
    constexpr std::uint32_t kCount = 256;
    constexpr std::uint32_t kLocalSize = 64;

    std::vector<std::uint32_t> source(kCount);
    std::iota(source.begin(), source.end(), 1u);

    Buffer::Builder<std::uint32_t> builder;
    builder.setData(source);
    builder.addUsage(Buffer::Usage::eStorage);
    builder.addUsage(Buffer::Usage::eTransferSrc);
    builder.setType(Buffer::Type::eDeviceDynamic);
    auto buffer = builder.build();
    ASSERT_TRUE(buffer.valid());
    EXPECT_TRUE(buffer->isHostVisible()) << "eDeviceDynamic must stay mappable on every path";

    Shader::Builder shaderBuilder;
    shaderBuilder.setPath("doubleValues.comp.glsl");
    auto shader = shaderBuilder.build();
    ASSERT_TRUE(shader.valid());

    ComputePipeline::Builder pipeBuilder;
    pipeBuilder.setComputeShader(shader);
    auto pipeline = pipeBuilder.build();
    ASSERT_TRUE(pipeline.valid());

    auto descriptorSet =
        DescriptorSet::Builder(ResourceRef<const kor::Pipeline>(pipeline), 0)
            .write(0, Descriptor(ResourceRef<const Buffer>(buffer)))
            .build();
    ASSERT_TRUE(descriptorSet.valid());

    // The initial data went in through a host write at build time. If the GPU sees it, the write
    // reached device memory without anyone staging a copy — which is the whole promise of the type.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        cb.Dispatch(kCount / kLocalSize, 1, 1);
    }, CommandBuffer::Usage::eCompute);

    // Reading it back is exactly what the type warns against, and it is done here on purpose: the
    // warning is about speed, not correctness, and correctness is what this asserts.
    const auto result = buffer->Read<std::uint32_t>();
    ASSERT_EQ(result.size(), source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        EXPECT_EQ(result[i], source[i] * 2u) << "at index " << i;
    }
}

} // namespace
