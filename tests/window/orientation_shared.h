// The raster-vs-compute Y-orientation checks, used by the windowed suite
// (test_windowed.cpp).
//
// What it proves: a triangle/quad drawn by the RASTERIZER and the same pattern
// written by a COMPUTE shader's imageStore must land identically.
// Both tests also blit their image to the screen, exercising the present path.

#pragma once

#include <functional>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "imageView.h"
#include "pipeline.h"
#include "resource.h"
#include "scheduler.h"
#include "shader.h"
#include "window.h"

namespace orient {

using Pixel = glm::u8vec4; // RGBA8
inline constexpr std::uint32_t kW = 16;
inline constexpr std::uint32_t kH = 16;

// A rendered target plus its host-side readback. The image is returned so a test
// can blit it to the screen *after* asserting on the pixels — a swap-chain blit
// issue must not mask the orientation result.
struct Result {
    kor::Resource<kor::Image> image;
    std::vector<Pixel> pixels;
};

inline kor::ResourceRef<const kor::Shader> loadShader(const char* file, kor::Shader::Stage stage, const char* key) {
    return kor::Shader::Builder{}
        .SetLang<kor::Shader::Lang::eGLSL>()
        .SetStage(stage)
        .SetPath(kor::ShaderPath(file))
        .GetOrBuild(key);
}

// Copy an image back to host memory, rows top-down.
inline std::vector<Pixel> readback(const kor::Resource<kor::Image>& image) {
    kor::Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(kor::Buffer::Usage::eTransferDst)
      .SetType(kor::Buffer::Type::eReadback);
    auto buf = rb.Build();
    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.CopyImageToBuffer(image,
                             buf);
    }, kor::CommandBuffer::Usage::eTransfer).Wait();
    return buf->Read<Pixel>();
}

// Blit the image to the default framebuffer (the swap chain) and present one
// frame. Secondary to the readback assertion — it exercises the blit-to-screen
// path the user actually cares about, on both backends.
// One application frame in which the test's scene records @p record. Each suite defines it.
void drawSharedFrame(const std::function<void(kor::CommandBuffer&)>& record);

inline void blitToScreen(const kor::Resource<kor::Image>& image) {
    drawSharedFrame([&](kor::CommandBuffer& cb) {
        cb.BlitToScreen(image);
    });
}

// Draw the top-half quad through a graphics pipeline into an offscreen RGBA8
// image and read it back.
inline Result rasterTopHalf() {
    auto image = kor::Image::Builder{}
                     .SetType(kor::Image::Type::e2D)
                     .SetFormat(kor::Image::Format::eRGBA8_UNORM)
                     .SetExtent(glm::uvec2{kW, kH})
                     .SetUsage(kor::Image::Usage::eColorAttachment | kor::Image::Usage::eTransferSrc)
                     .Build();
    auto view = kor::ImageView::Builder(image).Build();
    auto fb = kor::Framebuffer::Builder{}
                  .AddColor({ .view = view, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                  .Build();

    const auto vert = loadShader("topHalfQuad.vert.glsl", kor::Shader::Stage::eVertex, "orient.tophalf.vert");
    const auto frag = loadShader("flatTriangle.frag.glsl", kor::Shader::Stage::eFragment, "orient.flat.frag");
    auto pipeline = kor::GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(fb)
                        .Build();

    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.BeginRendering(fb);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(6); // two triangles covering the top half of clip space
        cb.EndRendering();
    }, kor::CommandBuffer::Usage::eGraphics).Wait();

    auto px = readback(image);
    return Result{ std::move(image), std::move(px) };
}

// Write the same top-half pattern into a storage image with a compute imageStore
// and read it back.
inline Result computeTopHalf() {
    auto image = kor::Image::Builder{}
                     .SetType(kor::Image::Type::e2D)
                     .SetFormat(kor::Image::Format::eRGBA8_UNORM)
                     .SetExtent(glm::uvec2{kW, kH})
                     .SetUsage(kor::Image::Usage::eStorage | kor::Image::Usage::eTransferSrc)
                     .Build();
    auto view = kor::ImageView::Builder(image).Build();

    const auto shader = loadShader("topHalfImage.comp.glsl", kor::Shader::Stage::eCompute, "orient.tophalf.comp");
    auto pipeline = kor::ComputePipeline::Builder{}.SetComputeShader(shader).Build();
    auto set = kor::DescriptorSet::Builder(pipeline, 0)
                   .Write(0, view)
                   .Build();

    const kor::ResourceRef<const kor::Image> imgRef(image);
    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, set);
        cb.ImageBarrier(kor::ImageBarrier(imgRef, kor::ResourceAccess::eComputeWrite));
        cb.Dispatch((kW + 7) / 8, (kH + 7) / 8, 1);
        cb.ImageBarrier(kor::ImageBarrier(imgRef, kor::ResourceAccess::eTransferSrc));
    }, kor::CommandBuffer::Usage::eCompute).Wait();

    auto px = readback(image);
    return Result{ std::move(image), std::move(px) };
}

// Assert the readback is the top-half-green / bottom-half-black pattern (rows come
// back top-down, so green is in the low rows). Both the raster and the compute readback must satisfy this identical check, so
// passing it on both means the two are pixel-identical.
inline void expectHalfSplit(const std::vector<Pixel>& px) {
    ASSERT_EQ(px.size(), static_cast<std::size_t>(kW) * kH);

    int greenRows = 0, blackRows = 0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        // Skip the two rows straddling the split to stay robust to the rasterizer's
        // exact edge coverage at the clip-space boundary.
        if (y == kH / 2 - 1 || y == kH / 2) continue;

        const bool expectGreen = y < kH / 2;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Pixel& p = px[y * kW + x];
            if (expectGreen) {
                ASSERT_EQ(p.r, 0)   << "row " << y << " col " << x << " should be green";
                ASSERT_EQ(p.g, 255) << "row " << y << " col " << x << " should be green";
                ASSERT_EQ(p.b, 0)   << "row " << y << " col " << x << " should be green";
            } else {
                ASSERT_EQ(p.g, 0) << "row " << y << " col " << x << " should be black; "
                                     "green here means the image is vertically flipped";
            }
        }
        (expectGreen ? greenRows : blackRows)++;
    }
    // The pattern must actually be an asymmetric split — otherwise a flip would be undetectable.
    ASSERT_GT(greenRows, 0);
    ASSERT_GT(blackRows, 0);
}

} // namespace orient
