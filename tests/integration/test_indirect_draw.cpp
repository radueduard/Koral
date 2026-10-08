// Indirect draws on a real device: several commands read from one buffer with the stride left at 0 ("packed"),
// a count read from a buffer (DrawIndirectCount / DrawIndexedIndirectCount), and a device-local buffer whose
// exact usage set leaves out the transfer its initial data arrives by.
//
// The screen is split along its diagonal into two triangles, drawn by two indirect commands: the top-left one
// covers texel (0, 0), the bottom-right one texel (W-1, H-1), so the picture says which of them ran.

#include "gpu_fixture.h"

#include <cstdint>
#include <string>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "deviceFeatures.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "imageView.h"
#include "log.h"
#include "mesh.h"
#include "shader.h"
#include <koralMesh.h>

// Optional, so the suite still runs on a GPU without it; the count tests skip there.
KORAL_REQUEST_FEATURES(kor::Feature::eDrawIndirectCount);

namespace {

using kor::Buffer;
using kor::CommandBuffer;
using kor::IndirectDrawCommand;
using kor::IndirectDrawIndexedCommand;

using Pixel = kor::U8Vec4;
using PosVertex = kmesh::ParamVertex<kmesh::Position>;
using PosMesh = kmesh::ParamMesh<PosVertex>;

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

struct Target {
    kor::Resource<kor::Image> image;
    kor::Resource<kor::ImageView> view;   // the framebuffer only refers to it
    kor::Resource<kor::Framebuffer> framebuffer;
    kor::Resource<kor::GraphicsPipeline> pipeline;
    kor::Resource<PosMesh> mesh;

    Target()
    {
        // Top-left triangle (indices 0..2), then bottom-right (3..5); clip y = -1 is the top row.
        const std::vector<PosVertex> vertices = {
            PosVertex{ kor::Vec3{ -1.f, -1.f, 0.f } }, PosVertex{ kor::Vec3{ 1.f, -1.f, 0.f } },
            PosVertex{ kor::Vec3{ -1.f, 1.f, 0.f } },  PosVertex{ kor::Vec3{ 1.f, 1.f, 0.f } },
        };
        const std::vector<std::uint32_t> indices = { 0, 1, 2, 1, 3, 2 };
        mesh = PosMesh::Create(vertices, indices);

        image = kor::Image::Builder{}
                    .SetType(kor::Image::Type::e2D)
                    .SetFormat(kor::Image::Format::eRGBA8_UNORM)
                    .SetExtent(kor::UVec2{ kW, kH })
                    .SetUsage(kor::Image::Usage::eColorAttachment | kor::Image::Usage::eTransferSrc)
                    .Build();
        view = kor::ImageView::Builder(image).Build();
        framebuffer = kor::Framebuffer::Builder{}.AddColor({ .view = view, .clear = kor::Vec4{ 0.f, 0.f, 0.f, 1.f } }).Build();

        const kor::ResourceRef<const kor::Shader> vert =
            kor::Shader::Builder{}.SetLang<kor::Shader::Lang::eGLSL>().SetStage(kor::Shader::Stage::eVertex)
                .SetPath(kor::ShaderPath("meshTriangle.vert.glsl")).GetOrBuild("test.meshTriangle.vert");
        const kor::ResourceRef<const kor::Shader> frag =
            kor::Shader::Builder{}.SetLang<kor::Shader::Lang::eGLSL>().SetStage(kor::Shader::Stage::eFragment)
                .SetPath(kor::ShaderPath("flatTriangle.frag.glsl")).GetOrBuild("test.flatTriangle.frag");
        pipeline = kor::GraphicsPipeline::Builder{}
                       .SetVertexShader(vert, PosMesh::Layout())
                       .SetFragmentShader(frag)
                       .SetFramebuffer(framebuffer)
                       .Build();
    }

    /** Draws with @p draw inside a render pass over a cleared target, and says whether each corner came out green. */
    template <typename F>
    std::pair<bool, bool> Corners(F&& draw)
    {
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(framebuffer);
            cb.BindGraphicsPipeline(pipeline);
            cb.BindMesh(mesh);
            draw(cb);
            cb.EndRendering();
        }, CommandBuffer::Usage::eGraphics).Wait();

        auto readback = Buffer::RawBuilder{}
                            .SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
                            .SetUsage(Buffer::Usage::eTransferDst)
                            .SetType(Buffer::Type::eReadback)
                            .Build();
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) { cb.CopyImageToBuffer(image, readback); },
                                         CommandBuffer::Usage::eTransfer).Wait();
        const auto pixels = readback->Read<Pixel>();
        return { pixels.front().y == 255, pixels.back().y == 255 };
    }
};

/** Draw commands for each of the two triangles, in a device-local buffer that names its roles exactly. */
kor::Resource<Buffer> Commands()
{
    return Buffer::Builder<IndirectDrawIndexedCommand>{}
        .SetData(std::vector<IndirectDrawIndexedCommand>{ { 3, 1, 0, 0, 0 }, { 3, 1, 3, 0, 0 } })
        .SetUsage(Buffer::Usage::eIndirect)   // no eTransferDst: the builder adds the one its upload needs
        .SetType(Buffer::Type::eDeviceLocal)
        .Build();
}

kor::Resource<Buffer> Count(const std::uint32_t count)
{
    return Buffer::Builder<std::uint32_t>{}.SetData(count).SetUsage(Buffer::Usage::eIndirect).SetType(Buffer::Type::eDeviceLocal).Build();
}

void ExpectNoValidationErrors(const std::uint64_t since)
{
    for (const auto& record : kor::log::HistorySince(since))
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
}

TEST_F(GpuTest, IndirectCommandsPackedWithStrideZeroAllDraw) {
    Target target;
    const auto since = kor::log::LastSequence();
    const auto commands = Commands();
    const auto [topLeft, bottomRight] = target.Corners([&](CommandBuffer& cb) { cb.DrawIndexedIndirect(commands, 0, 2); });
    EXPECT_TRUE(topLeft);
    EXPECT_TRUE(bottomRight) << "the second command was not read";
    ExpectNoValidationErrors(since);
}

TEST_F(GpuTest, DrawIndexedIndirectCountDrawsAsManyAsTheBufferSays) {
    if (!kor::Context::Supports(kor::Feature::eDrawIndirectCount)) GTEST_SKIP() << "the GPU has no indirect counts";
    Target target;
    const auto since = kor::log::LastSequence();
    const auto commands = Commands();

    const auto one = Count(1);
    const auto [oneTopLeft, oneBottomRight] = target.Corners([&](CommandBuffer& cb) { cb.DrawIndexedIndirectCount(commands, 0, one, 0, 2); });
    EXPECT_TRUE(oneTopLeft);
    EXPECT_FALSE(oneBottomRight) << "a count of 1 drew the second command";

    const auto five = Count(5);   // more than there are: maxDrawCount caps it
    const auto [allTopLeft, allBottomRight] = target.Corners([&](CommandBuffer& cb) { cb.DrawIndexedIndirectCount(commands, 0, five, 0, 2); });
    EXPECT_TRUE(allTopLeft);
    EXPECT_TRUE(allBottomRight);
    ExpectNoValidationErrors(since);
}

TEST_F(GpuTest, DrawIndirectCountDrawsWithoutAMesh) {
    if (!kor::Context::Supports(kor::Feature::eDrawIndirectCount)) GTEST_SKIP() << "the GPU has no indirect counts";
    Target target;
    const auto since = kor::log::LastSequence();
    // The same two triangles, non-indexed: the mesh's vertex buffer is still bound, read as 0,1,2 then 1,2,3.
    const auto commands = Buffer::Builder<IndirectDrawCommand>{}
                              .SetData(std::vector<IndirectDrawCommand>{ { 3, 1, 0, 0 }, { 3, 1, 1, 0 } })
                              .SetUsage(Buffer::Usage::eIndirect)
                              .SetType(Buffer::Type::eDeviceLocal)
                              .Build();
    const auto count = Count(2);
    const auto [topLeft, bottomRight] = target.Corners([&](CommandBuffer& cb) { cb.DrawIndirectCount(commands, 0, count, 0, 2); });
    EXPECT_TRUE(topLeft);
    EXPECT_TRUE(bottomRight);
    ExpectNoValidationErrors(since);
}

TEST_F(GpuTest, AnIndirectStrideThatCannotStepIsRefused) {
    Target target;
    const auto commands = Commands();
    const auto count = Count(2);

    const auto record = [&](auto&& draw) {
        auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
        cb->Begin();
        cb->BeginRendering(target.framebuffer);
        cb->BindGraphicsPipeline(target.pipeline);
        cb->BindMesh(target.mesh);
        draw(*cb);
        cb->EndRendering();
        cb->End();
        return cb->Errors();
    };

    const auto shortStride = record([&](CommandBuffer& cb) { cb.DrawIndexedIndirect(commands, 0, 2, 8); });
    ASSERT_FALSE(shortStride.empty());
    EXPECT_EQ(shortStride.front().code, kor::ErrorCode::eInvalidArgument);
    EXPECT_NE(shortStride.front().message.find("stride of 8"), std::string::npos) << shortStride.front().message;

    // One draw steps nowhere: any stride will do.
    EXPECT_TRUE(record([&](CommandBuffer& cb) { cb.DrawIndexedIndirect(commands, 0, 1, 7); }).empty());

    if (kor::Context::Supports(kor::Feature::eDrawIndirectCount)) {
        const auto misaligned = record([&](CommandBuffer& cb) { cb.DrawIndexedIndirectCount(commands, 0, count, 2, 2); });
        ASSERT_FALSE(misaligned.empty());
        EXPECT_EQ(misaligned.front().code, kor::ErrorCode::eInvalidArgument);
    }
}

}
