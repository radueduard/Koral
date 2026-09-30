// An application with no windowing system at all (WindowPlatform::eNone): only offscreen scenes, on
// a process that needs no display — a server, a batch render, a test machine. Its own executable,
// because an application is one per process and the windowed suite already has one.

#include <gtest/gtest.h>

#include <memory>

#include <app.h>
#include <buffer.h>
#include <commandBuffer.h>
#include <frameGraph.h>
#include <image.h>
#include <scene.h>

namespace {
    // Clears the screen to a colour, and copies one texel out to read back.
    class ClearPass final : public kor::RenderPass {
    public:
        ClearPass(kor::ResourceRef<const kor::Buffer> readback, const glm::vec4 color)
            : RenderPass("Clear"), _readback(std::move(readback)), _color(color) {}
        void Setup(kor::PassBuilder& b) override {
            b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst | kor::Image::Usage::eTransferSrc).SideEffect();
        }
        void Initialize(const kor::PassResources& r) override { _screen = r.ImageNamed(kor::FrameGraph::Screen); }
        void Record(kor::CommandBuffer& cb) const override {
            cb.ClearColorImage(_screen, _color);
            cb.CopyImageToBuffer(_screen, _readback, kor::Copy{ .imageExtent = glm::ivec3(1, 1, 1) });
        }
    private:
        kor::ResourceRef<const kor::Buffer> _readback;
        kor::ResourceRef<const kor::Image> _screen;
        glm::vec4 _color;
    };

    class Batch final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            Graph().Add<ClearPass>(kor::ResourceRef<const kor::Buffer>(readback), glm::vec4(0.f, 1.f, 0.f, 1.f));
        }
        void Update() override { ++frames; }
        kor::Resource<kor::Buffer> readback;
        int frames = 0;
    };
}

TEST(HeadlessApp, RunsOffscreenScenesWithNoWindowingSystem) {
    std::unique_ptr<kor::App> app;
    try {
        app = std::make_unique<kor::App>(kor::AppSettings{ .platform = kor::WindowPlatform::eNone });
    } catch (const std::exception& e) {
        GTEST_SKIP() << "no Vulkan device: " << e.what();
    }

    EXPECT_EQ(app->Open<Batch>({.title = "In a window"}), nullptr) << "there is no windowing system to open one on";

    auto* scene = app->OpenOffscreen<Batch>({.title = "Batch", .extent = {32, 32}});
    ASSERT_NE(scene, nullptr);
    for (int frame = 0; frame < 3; ++frame) ASSERT_TRUE(app->Frame());
    kor::Context::Scheduler().WaitIdle();

    EXPECT_EQ(scene->frames, 3);
    const auto texel = scene->readback->Read<glm::u8>(4);
    EXPECT_EQ(texel[1], 255) << "the scene drew into its image";
    EXPECT_EQ(texel[0], 0);

    app->Close(*scene);
    EXPECT_FALSE(app->Frame()) << "no scene left: the application is done";
}
