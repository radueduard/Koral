// Shared, backend-agnostic body for the raster-vs-compute Y-orientation parity
// tests. Included by both windowed executables (Vulkan: test_windowed.cpp,
// OpenGL: test_windowed_gl.cpp) so the exact same checks run on both backends.
//
// What it proves: a triangle/quad drawn by the RASTERIZER and the same pattern
// written by a COMPUTE shader's imageStore must land identically. On Vulkan that
// is trivially true. On OpenGL it is only true because imageStore is Y-flipped at
// transpile (injectStorageImageYFlip in backends/open_gl/shader.cpp) to match the
// glClipControl'd rasterizer — remove that flip and the compute test fails on GL.
//
// Each test reads its offscreen target back and checks it against a backend-aware
// reference rather than comparing the two backends' buffers directly: GL's
// CopyImageToBuffer (glGetTextureSubImage) returns rows bottom-up while Vulkan's
// returns them top-down, so the raw buffers are vertically mirrored for identical
// content. Asserting each against the correct half for its backend isolates the
// image orientation (what the fix touches) from that readback quirk. Both tests
// also blit their image to the screen, exercising the present path.

#pragma once

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
        .setLang<kor::Shader::Lang::eGLSL>()
        .setStage(stage)
        .setPath(kor::shaderPath(file))
        .getOrBuild(key);
}

// Copy an image back to host memory. Rows come out top-down on Vulkan and
// bottom-up on OpenGL (see the file header) — expectHalfSplit accounts for that.
inline std::vector<Pixel> readback(const kor::Resource<kor::Image>& image) {
    kor::Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(kor::Buffer::Usage::eTransferDst)
      .setType(kor::Buffer::Type::eReadback);
    auto buf = rb.build();
    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.CopyImageToBuffer(kor::ResourceRef<const kor::Image>(image),
                             kor::ResourceRef<const kor::Buffer>(buf));
    }, kor::CommandBuffer::Usage::eTransfer);
    return buf->Read<Pixel>();
}

// Blit the image to the default framebuffer (the swap chain) and present one
// frame. Secondary to the readback assertion — it exercises the blit-to-screen
// path the user actually cares about, on both backends.
inline void blitToScreen(const kor::Resource<kor::Image>& image) {
    glfwPollEvents();
    kor::Context::DrainMainThread();
    kor::Context::Scheduler().Draw([&](kor::CommandBuffer& cb) {
        cb.Blit(kor::ResourceRef<const kor::Image>(image));
    });
}

// Draw the top-half quad through a graphics pipeline into an offscreen RGBA8
// image and read it back.
inline Result rasterTopHalf() {
    auto image = kor::Image::Builder{}
                     .setType(kor::Image::Type::e2D)
                     .setFormat(kor::Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .addUsage(kor::Image::Usage::eColorAttachment)
                     .addUsage(kor::Image::Usage::eTransferSrc)
                     .build();
    auto view = kor::ImageView::Builder(kor::ResourceRef<const kor::Image>(image)).build();
    auto fb = kor::Framebuffer::Builder{}
                  .addColorAttachment(kor::ResourceRef<const kor::ImageView>(view), glm::vec4{0.f, 0.f, 0.f, 1.f})
                  .build();

    const auto vert = loadShader("topHalfQuad.vert.glsl", kor::Shader::Stage::eVertex, "orient.tophalf.vert");
    const auto frag = loadShader("flatTriangle.frag.glsl", kor::Shader::Stage::eFragment, "orient.flat.frag");
    auto pipeline = kor::GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(kor::ResourceRef<kor::Framebuffer>(fb))
                        .build();

    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.BeginRendering(kor::ResourceRef<const kor::Framebuffer>(fb));
        cb.BindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline>(pipeline));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(6); // two triangles covering the top half of clip space
        cb.EndRendering();
    }, kor::CommandBuffer::Usage::eGraphics);

    auto px = readback(image);
    return Result{ std::move(image), std::move(px) };
}

// Write the same top-half pattern into a storage image with a compute imageStore
// and read it back.
inline Result computeTopHalf() {
    auto image = kor::Image::Builder{}
                     .setType(kor::Image::Type::e2D)
                     .setFormat(kor::Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .addUsage(kor::Image::Usage::eStorage)
                     .addUsage(kor::Image::Usage::eTransferSrc)
                     .build();
    auto view = kor::ImageView::Builder(kor::ResourceRef<const kor::Image>(image)).build();

    const auto shader = loadShader("topHalfImage.comp.glsl", kor::Shader::Stage::eCompute, "orient.tophalf.comp");
    auto pipeline = kor::ComputePipeline::Builder{}.setComputeShader(shader).build();
    auto set = kor::DescriptorSet::Builder(kor::ResourceRef<const kor::Pipeline>(pipeline), 0)
                   .write(0, kor::Descriptor(kor::ResourceRef<const kor::ImageView>(view)))
                   .build();

    const kor::ResourceRef<const kor::Image> imgRef(image);
    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.BindComputePipeline(kor::ResourceRef<const kor::ComputePipeline>(pipeline));
        cb.BindDescriptorSet(0, kor::ResourceRef<const kor::DescriptorSet>(set));
        cb.ImageBarrier(kor::ImageBarrier(imgRef, kor::ResourceAccess::ComputeWrite));
        cb.Dispatch((kW + 7) / 8, (kH + 7) / 8, 1);
        cb.ImageBarrier(kor::ImageBarrier(imgRef, kor::ResourceAccess::TransferSrc));
    }, kor::CommandBuffer::Usage::eCompute);

    auto px = readback(image);
    return Result{ std::move(image), std::move(px) };
}

// Assert the readback is the top-half-green / bottom-half-black pattern, in the
// half appropriate to this backend's readback row order (see the file header).
// Both the raster and the compute readback must satisfy this identical check, so
// passing it on both means the two are pixel-identical.
inline void expectHalfSplit(const std::vector<Pixel>& px) {
    ASSERT_EQ(px.size(), static_cast<std::size_t>(kW) * kH);

    // Vulkan reads back top-down (green half = low rows); GL reads back bottom-up
    // (green half = high rows). The rendered image is the same either way.
    const bool greenInLowRows = (kor::Context::activeAPI() == kor::API::eVulkan);

    int greenRows = 0, blackRows = 0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        // Skip the two rows straddling the split to stay robust to the rasterizer's
        // exact edge coverage at the clip-space boundary.
        if (y == kH / 2 - 1 || y == kH / 2) continue;

        const bool expectGreen = greenInLowRows ? (y < kH / 2) : (y >= kH / 2);
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
