// koral-ui: the canvas's shapes drawn by a real GPU into an offscreen scene and read back pixel by
// pixel, plus the CPU-side pieces (paths, tessellation, text layout) on their own.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <numbers>

#include <app.h>
#include <buffer.h>
#include <commandBuffer.h>
#include <context.h>
#include <frameGraph.h>
#include <image.h>
#include <scene.h>
#include <scheduler.h>

#include <koralUI.h>

namespace {
    constexpr int Size = 64;

    class ClearPass final : public kor::RenderPass {
    public:
        ClearPass() : RenderPass("Clear") {}
        void Setup(kor::PassBuilder& b) override { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); }
        void Initialize(const kor::PassResources& r) override { _screen = r.ImageNamed(kor::FrameGraph::Screen); }
        void Record(kor::CommandBuffer& cb) const override { cb.ClearColorImage(_screen, glm::vec4(0.f, 0.f, 0.f, 1.f)); }
    private:
        kor::ResourceRef<const kor::Image> _screen;
    };

    class ReadPass final : public kor::RenderPass {
    public:
        explicit ReadPass(kor::ResourceRef<const kor::Buffer> out) : RenderPass("Read"), _out(std::move(out)) {}
        void Setup(kor::PassBuilder& b) override { b.Read(kor::FrameGraph::Screen, kor::Image::Usage::eTransferSrc).SideEffect(); }
        void Initialize(const kor::PassResources& r) override { _screen = r.ImageNamed(kor::FrameGraph::Screen); }
        void Record(kor::CommandBuffer& cb) const override { cb.CopyImageToBuffer(_screen, _out); }
    private:
        kor::ResourceRef<const kor::Buffer> _out;
        kor::ResourceRef<const kor::Image> _screen;
    };

    class Canvas final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(Size * Size * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            ui.SetRoot(root);
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        [[nodiscard]] glm::u8vec4 At(const int x, const int y) const {
            const auto pixels = readback->Read<glm::u8vec4>(Size * Size);
            return pixels[static_cast<std::size_t>(y) * Size + x];
        }
        kui::Renderer ui;
        std::shared_ptr<kui::Layer> root = kui::Layer::Create();
        kor::Resource<kor::Buffer> readback;
    };

    std::unique_ptr<kor::App> s_app;
    std::string s_reason = "no device";

    class Environment final : public ::testing::Environment {
    public:
        void SetUp() override {
            try { s_app = std::make_unique<kor::App>(kor::AppSettings{ .platform = kor::WindowPlatform::eNone }); }
            catch (const std::exception& e) { s_reason = e.what(); s_app.reset(); }
        }
        void TearDown() override { s_app.reset(); }
    };
    const auto* const s_environment = ::testing::AddGlobalTestEnvironment(new Environment);

    void settle() {
        s_app->Frame();
        kor::Context::Scheduler().WaitIdle();
    }

    class Gpu : public ::testing::Test {
    protected:
        void SetUp() override {
            if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
            scene = s_app->OpenOffscreen<Canvas>({ .title = "kui", .extent = { Size, Size } });
            ASSERT_NE(scene, nullptr);
        }
        void TearDown() override {
            if (scene) { s_app->Close(*scene); settle(); }
        }
        /** @brief Records with @p draw into the root layer and shows it. */
        void Draw(const std::function<void(kui::Canvas&)>& draw) {
            kui::Canvas canvas;
            draw(canvas);
            scene->root->SetPicture(canvas.Finish());
            settle();
            settle();   // the readback copies what the frame before it drew
        }
        Canvas* scene = nullptr;
    };

    constexpr kui::Color Red { 1.f, 0.f, 0.f, 1.f };
    constexpr kui::Color Green { 0.f, 1.f, 0.f, 1.f };
    constexpr kui::Color Blue { 0.f, 0.f, 1.f, 1.f };

    bool IsRed(const glm::u8vec4 p) { return p.r > 240 && p.g < 15 && p.b < 15; }
    bool IsGreen(const glm::u8vec4 p) { return p.g > 240 && p.r < 15 && p.b < 15; }
    bool IsBlue(const glm::u8vec4 p) { return p.b > 240 && p.r < 15 && p.g < 15; }
    bool IsBlack(const glm::u8vec4 p) { return p.r < 5 && p.g < 5 && p.b < 5; }
}

// ---- drawn ----------------------------------------------------------------------------------------------

TEST_F(Gpu, ARectangleFillsExactlyItsPixels) {
    Draw([](kui::Canvas& c) { c.DrawRect(kui::Rect::LTRB(8, 8, 24, 24), kui::Paint::Fill(Red)); });
    EXPECT_TRUE(IsRed(scene->At(8, 8)));
    EXPECT_TRUE(IsRed(scene->At(23, 23)));
    EXPECT_TRUE(IsBlack(scene->At(24, 24)));
    EXPECT_TRUE(IsBlack(scene->At(7, 16)));
}

TEST_F(Gpu, RoundedCornersAndCirclesLeaveTheirCornersOut) {
    Draw([](kui::Canvas& c) {
        c.DrawRRect({ kui::Rect::LTRB(0, 0, 32, 32), 12.f }, kui::Paint::Fill(Red));
        c.DrawCircle({ 48, 48 }, 10.3f, kui::Paint::Fill(Green));   // its edge crosses pixel 58 at x = 58.3
    });
    EXPECT_TRUE(IsBlack(scene->At(0, 0))) << "the rounded corner";
    EXPECT_TRUE(IsRed(scene->At(16, 16)));
    EXPECT_TRUE(IsRed(scene->At(16, 0)));
    EXPECT_TRUE(IsGreen(scene->At(48, 48)));
    EXPECT_TRUE(IsBlack(scene->At(39, 39))) << "outside the circle, inside its square";
    const auto edge = scene->At(58, 48);   // the circle's edge runs through this pixel
    EXPECT_GT(edge.g, 40);
    EXPECT_LT(edge.g, 215) << "anti-aliased";
}

TEST_F(Gpu, AStrokeOutlinesWithoutFilling) {
    Draw([](kui::Canvas& c) { c.DrawRect(kui::Rect::LTRB(10, 10, 50, 50), kui::Paint::Stroked(Blue, 4.f)); });
    EXPECT_TRUE(IsBlue(scene->At(10, 30)));
    EXPECT_TRUE(IsBlue(scene->At(11, 30)));
    EXPECT_TRUE(IsBlack(scene->At(30, 30)));
    EXPECT_TRUE(IsBlack(scene->At(5, 30)));
}

TEST_F(Gpu, FillAndStrokeTogether) {
    Draw([](kui::Canvas& c) { c.DrawCircle({ 32, 32 }, 20.f, kui::Paint::Fill(Red).SetStroke(4.f, Blue)); });
    EXPECT_TRUE(IsRed(scene->At(32, 32)));
    EXPECT_TRUE(IsBlue(scene->At(32, 12)));
}

TEST_F(Gpu, LinesTrianglesArcsAndCurves) {
    Draw([](kui::Canvas& c) {
        c.DrawLine({ 2, 4 }, { 62, 4 }, kui::Paint::Stroked(Red, 3.f));
        c.DrawTriangle({ 4, 60 }, { 28, 60 }, { 16, 40 }, kui::Paint::Fill(Green));
        c.DrawArc({ 48, 48 }, 12.f, 0.f, std::numbers::pi_v<float> * 0.5f, true, kui::Paint::Fill(Blue));   // the lower-right quarter
        c.DrawQuadraticBezier({ 4, 20 }, { 32, 36 }, { 60, 20 }, kui::Paint::Stroked(Red, 3.f).SetStroke({ .width = 3.f, .color = Red, .cap = kui::StrokeCap::eRound }));
    });
    EXPECT_TRUE(IsRed(scene->At(32, 4)));
    EXPECT_TRUE(IsBlack(scene->At(32, 9)));
    EXPECT_TRUE(IsGreen(scene->At(16, 55)));
    EXPECT_TRUE(IsBlack(scene->At(5, 41)));
    EXPECT_TRUE(IsBlue(scene->At(52, 52)));
    EXPECT_TRUE(IsBlack(scene->At(44, 44))) << "the pie is the quarter below and right of the centre";
    EXPECT_TRUE(IsRed(scene->At(32, 28))) << "the curve's lowest point is halfway to its control";
}

TEST_F(Gpu, APathWithAHoleFillsByItsRule) {
    Draw([](kui::Canvas& c) {
        kui::Path path;
        path.AddRect(kui::Rect::LTRB(8, 8, 56, 56)).AddRect(kui::Rect::LTRB(24, 24, 40, 40)).SetFillRule(kui::FillRule::eEvenOdd);
        c.DrawPath(path, kui::Paint::Fill(Green));
    });
    EXPECT_TRUE(IsGreen(scene->At(12, 32)));
    EXPECT_TRUE(IsBlack(scene->At(32, 32))) << "the hole";
    EXPECT_TRUE(IsBlack(scene->At(4, 4)));
}

TEST_F(Gpu, AStrokedPathIsOneShapeEvenWhereItsPiecesOverlap) {
    Draw([](kui::Canvas& c) {
        kui::Path path;
        path.MoveTo({ 8, 32 }).LineTo({ 56, 32 }).LineTo({ 32, 8 }).LineTo({ 32, 56 });
        c.DrawPath(path, kui::Paint::Stroked(Red.WithAlpha(0.5f), 6.f).SetStroke({ .width = 6.f, .color = Red.WithAlpha(0.5f), .join = kui::StrokeJoin::eRound }));
    });
    const auto crossing = scene->At(32, 32);   // two segments cross here
    const auto single = scene->At(16, 32);
    EXPECT_NEAR(crossing.r, single.r, 3) << "a translucent stroke does not darken where it crosses itself";
    EXPECT_GT(single.r, 100);
    EXPECT_LT(single.r, 160);
}

TEST_F(Gpu, ThePenDrawsAPathASegmentAtATimeAndFillsThenStrokesIt) {
    Draw([](kui::Canvas& c) {
        c.BeginPath()
         .MoveTo({ 8, 8 })
         .DrawLineTo({ 40, 8 })
         .DrawArcTo({ 56, 8 }, { 56, 24 }, 16.f)   // the top-right corner, rounded
         .DrawLineTo({ 56, 56 })
         .DrawLineTo({ 8, 56 })
         .ClosePath()
         .Fill(kui::Paint::Fill(Green))
         .Stroke(kui::Paint::Stroked(Red, 2.f));
    });
    EXPECT_TRUE(IsGreen(scene->At(30, 30)));
    EXPECT_TRUE(IsRed(scene->At(8, 30))) << "stroked along the left edge";
    EXPECT_TRUE(IsBlack(scene->At(54, 10))) << "the corner is rounded off";
    EXPECT_TRUE(IsGreen(scene->At(48, 16))) << "inside the curve";
}

TEST_F(Gpu, AGradientRunsFromItsStartToItsEnd) {
    Draw([](kui::Canvas& c) {
        c.DrawRect(kui::Rect::LTRB(0, 0, 64, 64), kui::Paint{}.SetGradient(kui::Gradient::Linear({ 0, 0 }, { 64, 0 }, Red, Blue)));
    });
    const auto left = scene->At(1, 32), right = scene->At(62, 32), mid = scene->At(32, 32);
    EXPECT_GT(left.r, 240);
    EXPECT_GT(right.b, 240);
    EXPECT_NEAR(mid.r, 127, 6);
    EXPECT_NEAR(mid.b, 127, 6);
}

TEST_F(Gpu, NothingIsDrawnOutsideAClip) {
    Draw([](kui::Canvas& c) {
        c.Save();
        c.ClipRRect({ kui::Rect::LTRB(16, 16, 48, 48), 8.f });
        c.DrawRect(kui::Rect::LTRB(0, 0, 64, 64), kui::Paint::Fill(Red));
        c.Restore();
        c.DrawRect(kui::Rect::LTRB(0, 60, 4, 64), kui::Paint::Fill(Green));   // after Restore: unclipped
    });
    EXPECT_TRUE(IsRed(scene->At(32, 32)));
    EXPECT_TRUE(IsBlack(scene->At(8, 32)));
    EXPECT_TRUE(IsBlack(scene->At(16, 16))) << "the clip's rounded corner";
    EXPECT_TRUE(IsGreen(scene->At(1, 62)));
}

TEST_F(Gpu, TransformsMoveRotateAndScale) {
    Draw([](kui::Canvas& c) {
        c.Translate({ 32, 32 });
        c.Rotate(std::numbers::pi_v<float> * 0.25f);
        c.DrawRect(kui::Rect::LTRB(-4, -20, 4, 20), kui::Paint::Fill(Red));   // a bar, turned to the diagonal
    });
    EXPECT_TRUE(IsRed(scene->At(32, 32)));
    EXPECT_TRUE(IsRed(scene->At(22, 42))) << "clockwise a quarter turn from vertical points down-left";
    EXPECT_TRUE(IsBlack(scene->At(42, 42)));
}

TEST_F(Gpu, MovingALayerReRecordsNothingAndUploadsLittle) {
    auto child = kui::Layer::Create();
    {
        kui::Canvas c;
        c.DrawRect(kui::Rect::LTRB(0, 0, 8, 8), kui::Paint::Fill(Green));
        child->SetPicture(c.Finish());
    }
    Draw([&](kui::Canvas& c) {
        c.DrawRect(kui::Rect::LTRB(40, 40, 64, 64), kui::Paint::Fill(Red));
        c.DrawLayer(child);
    });
    EXPECT_TRUE(IsGreen(scene->At(4, 4)));

    settle();
    EXPECT_FALSE(scene->ui.Stats().recomposed);
    EXPECT_EQ(scene->ui.Stats().uploadedBytes, 0u) << "an unchanged frame uploads nothing";

    child->SetTransform(kui::Transform::Translation({ 20, 0 }));
    settle();
    EXPECT_FALSE(scene->ui.Stats().recomposed) << "a moved layer is not a changed picture";
    EXPECT_LE(scene->ui.Stats().uploadedBytes, 2u * sizeof(float) * 8) << "only the layer table";
    settle();
    EXPECT_TRUE(IsBlack(scene->At(4, 4)));
    EXPECT_TRUE(IsGreen(scene->At(24, 4)));
    EXPECT_TRUE(IsRed(scene->At(50, 50)));
}

TEST_F(Gpu, ALayerIsClippedWhereItWasDrawn) {
    auto content = kui::Layer::Create();
    {
        kui::Canvas c;
        c.DrawRect(kui::Rect::LTRB(0, 0, 64, 64), kui::Paint::Fill(Blue));
        content->SetPicture(c.Finish());
    }
    Draw([&](kui::Canvas& c) {
        c.ClipRect(kui::Rect::LTRB(0, 0, 32, 64));
        c.DrawLayer(content);
    });
    EXPECT_TRUE(IsBlue(scene->At(10, 10)));
    EXPECT_TRUE(IsBlack(scene->At(40, 10)));
}

TEST_F(Gpu, TextIsDrawnFromTheAtlas) {
    if (!kui::Font::Default()) GTEST_SKIP() << "Koral's default font is not where assets are looked for";
    Draw([](kui::Canvas& c) { c.DrawText("I", { 20, 0 }, { .size = 60.f, .color = kui::colors::White }); });
    int lit = 0;
    for (int y = 0; y < Size; ++y)
        for (int x = 0; x < Size; ++x) lit += scene->At(x, y).r > 128;
    EXPECT_GT(lit, 60) << "a capital I at 60 units is a solid bar";
    EXPECT_LT(lit, 1200);
}

TEST_F(Gpu, AnElementShaderFillsItsRectangleWithItsParameters) {
    const auto shader = kui::ElementShader::Load(std::filesystem::path(KUI_TEST_SHADERS) / "testElement.frag.glsl");
    ASSERT_TRUE(shader->Valid());
    struct Params { glm::vec4 left, right; };
    Draw([&](kui::Canvas& c) {
        c.DrawElement(shader, kui::Rect::LTRB(0, 0, 64, 32), Params{ { 1, 0, 0, 1 }, { 0, 1, 0, 1 } });
        c.DrawElement(shader, kui::Rect::LTRB(0, 32, 64, 64), Params{ { 0, 0, 1, 1 }, { 1, 0, 0, 1 } }, 0.f);
    });
    EXPECT_TRUE(IsRed(scene->At(10, 10)));
    EXPECT_TRUE(IsGreen(scene->At(50, 10)));
    EXPECT_TRUE(IsBlue(scene->At(10, 50)));
    EXPECT_TRUE(IsRed(scene->At(50, 50)));
    EXPECT_EQ(scene->ui.Stats().draws, 1u) << "both elements in one instanced draw";
}

TEST_F(Gpu, AnElementShaderCanBeWrittenInSlang) {
    const auto shader = kui::ElementShader::Load(std::filesystem::path(KUI_TEST_SHADERS) / "testElement.slang", "fragmentMain");
    ASSERT_TRUE(shader->Valid());
    struct Params { glm::vec4 left, right; };
    Draw([&](kui::Canvas& c) {
        c.ClipRect(kui::Rect::LTRB(0, 0, 64, 48));
        c.DrawElement(shader, kui::Rect::LTRB(0, 0, 64, 64), Params{ { 0, 0, 1, 1 }, { 0, 1, 0, 1 } }, 0.f);
    });
    EXPECT_TRUE(IsBlue(scene->At(10, 10)));
    EXPECT_TRUE(IsGreen(scene->At(50, 10)));
    EXPECT_TRUE(IsBlack(scene->At(50, 56))) << "clipped like anything else";
}

TEST_F(Gpu, AShadowFadesOutwards) {
    Draw([](kui::Canvas& c) { c.DrawShadow({ kui::Rect::LTRB(16, 16, 48, 48), 4.f }, kui::colors::White, 4.f); });
    const auto inside = scene->At(32, 32), edge = scene->At(48, 32), far = scene->At(60, 32);
    EXPECT_GT(inside.r, 240);
    EXPECT_GT(edge.r, 80);
    EXPECT_LT(edge.r, 180);
    EXPECT_LT(far.r, 10);
}

TEST_F(Gpu, ShapesAreOneDrawUntilAMeshInterrupts) {
    Draw([](kui::Canvas& c) {
        for (int i = 0; i < 100; ++i) c.DrawCircle({ static_cast<float>(i % 10) * 6.f + 3.f, static_cast<float>(i / 10) * 6.f + 3.f }, 2.f, kui::Paint::Fill(Red));
    });
    EXPECT_EQ(scene->ui.Stats().draws, 1u);
    EXPECT_EQ(scene->ui.Stats().instances, 100u);
}

// ---- on the CPU -----------------------------------------------------------------------------------------

TEST(Path, CurvesFlattenWithinTheirTolerance) {
    kui::Path path;
    path.AddCircle({ 0, 0 }, 100.f);
    const auto contours = path.Flatten(0.1f);
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_TRUE(contours[0].closed);
    for (std::size_t i = 0; i < contours[0].points.size(); ++i) {
        const glm::vec2 a = contours[0].points[i], b = contours[0].points[(i + 1) % contours[0].points.size()];
        EXPECT_NEAR(glm::length(a), 100.f, 0.05f);
        EXPECT_GT(glm::length((a + b) * 0.5f), 100.f - 0.2f) << "each chord stays near the arc";
    }
}

TEST(Paragraph, WrapsAtWordsAndMeasuresItsLines) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    kui::Paragraph one("hello world", { .size = 20.f });
    EXPECT_EQ(one.LineCount(), 1u);
    const float full = one.Size().x;
    EXPECT_GT(full, 50.f);
    EXPECT_FLOAT_EQ(one.MaxIntrinsicWidth(), full);

    kui::Paragraph wrapped("hello world", { .size = 20.f }, full * 0.7f);
    EXPECT_EQ(wrapped.LineCount(), 2u);
    EXPECT_FLOAT_EQ(wrapped.Size().y, 2.f * 20.f * 1.25f);
    EXPECT_LT(wrapped.Size().x, full * 0.7f);

    kui::Paragraph breaks("a\nb\nc", { .size = 10.f });
    EXPECT_EQ(breaks.LineCount(), 3u);

    // The caret before 'w' is where "hello " ends, and a click there finds it again.
    const glm::vec2 caret = one.CaretPosition(6);
    EXPECT_GT(caret.x, 0.f);
    EXPECT_EQ(one.IndexAt(caret + glm::vec2(0.5f, 2.f)), 6u);
}

TEST(Transform, ComposesLikeMatrices) {
    const auto t = kui::Transform::Translation({ 10, 0 }) * kui::Transform::Rotation(std::numbers::pi_v<float> * 0.5f);
    const glm::vec2 p = t.Apply({ 1, 0 });
    EXPECT_NEAR(p.x, 10.f, 1e-5f);
    EXPECT_NEAR(p.y, 1.f, 1e-5f);
    const glm::vec2 back = t.Inverse().Apply(p);
    EXPECT_NEAR(back.x, 1.f, 1e-5f);
    EXPECT_NEAR(back.y, 0.f, 1e-5f);
}

// ---- widgets --------------------------------------------------------------------------------------------

namespace {
    class Widgets final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(Size * Size * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        void Update() override { ui.Update(); }
        [[nodiscard]] glm::u8vec4 At(const int x, const int y) const {
            return readback->Read<glm::u8vec4>(Size * Size)[static_cast<std::size_t>(y) * Size + x];
        }
        kui::Ui ui;
        kor::Resource<kor::Buffer> readback;
    };

    class WidgetTest : public ::testing::Test {
    protected:
        void SetUp() override {
            if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
            scene = s_app->OpenOffscreen<Widgets>({ .title = "kui widgets", .extent = { Size, Size } });
            ASSERT_NE(scene, nullptr);
        }
        void TearDown() override {
            if (scene) { s_app->Close(*scene); settle(); }
        }
        void Show(kui::Widget root) { scene->ui.SetRoot(std::move(root)); settle(); settle(); }
        /** @brief A click at @p p: the pointer moved there, then the button down, then up, a frame each. */
        void Click(const glm::vec2 p) {
            auto& input = scene->SceneInput();
            input.FeedMousePosition(p);
            settle();
            input.FeedMouseButton(kor::MouseButton::eLeft, true);
            settle();
            input.FeedMouseButton(kor::MouseButton::eLeft, false);
            settle();
        }
        Widgets* scene = nullptr;
    };

    /** @brief A box that reports the size it was given each time it paints. */
    kui::Widget Probe(glm::vec2& size, const kui::Color color = Red) {
        return kui::CustomPaint([&size, color](kui::Canvas& c, const glm::vec2 s) {
            size = s;
            c.DrawRect(kui::Rect::FromSize(s), kui::Paint::Fill(color));
        });
    }
}

TEST_F(WidgetTest, RowsShareWhatIsLeftAmongTheirExpandedChildren) {
    glm::vec2 a {}, b {};
    Show(kui::Row({
        kui::SizedBox(10.f, 20.f),
        kui::Expanded(Probe(a)),
        kui::Expanded(Probe(b, Green), 2.f),
    }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStretch }));
    EXPECT_FLOAT_EQ(a.x, 18.f);
    EXPECT_FLOAT_EQ(b.x, 36.f);
    EXPECT_FLOAT_EQ(a.y, 64.f) << "stretched across";
    EXPECT_TRUE(IsRed(scene->At(15, 32)));
    EXPECT_TRUE(IsGreen(scene->At(40, 32)));
    EXPECT_TRUE(IsBlack(scene->At(5, 32)));
}

TEST_F(WidgetTest, PaddingAlignmentAndDecorationPlaceABox) {
    Show(kui::Container({ .padding = kui::EdgeInsets::All(8.f), .decoration = { .color = Blue } },
                        kui::Align(kui::Alignment::BottomRight(), kui::SizedBox(10.f, 10.f, kui::DecoratedBox({ .color = Red }, {})))));
    EXPECT_TRUE(IsBlue(scene->At(2, 2)));
    EXPECT_TRUE(IsRed(scene->At(50, 50))) << "in the bottom-right corner, inside the padding";
    EXPECT_TRUE(IsBlue(scene->At(58, 58)));
}

namespace {
    struct Counter final : kui::StatefulWidget {
        int n = 0;
        int* builds;
        explicit Counter(int* b) : builds(b) {}
        kui::Widget Build() override {
            ++*builds;
            return kui::GestureDetector({ .onTap = [this] { SetState([this] { ++n; }); } },
                                        kui::SizedBox(64.f, 32.f, kui::DecoratedBox({ .color = n % 2 ? Green : Red }, {})));
        }
    };
    struct Label final : kui::StatelessWidget {
        int* builds;
        explicit Label(int* b) : builds(b) {}
        kui::Widget Build() const override { ++*builds; return kui::SizedBox(64.f, 32.f); }
    };
}

TEST_F(WidgetTest, SetStateRebuildsTheWidgetAndNothingElse) {
    int counterBuilds = 0, labelBuilds = 0;
    Show(kui::Column({ kui::Make<Counter>(&counterBuilds), kui::Make<Label>(&labelBuilds) }));
    EXPECT_EQ(counterBuilds, 1);
    EXPECT_EQ(labelBuilds, 1);
    EXPECT_TRUE(IsRed(scene->At(10, 10)));

    Click({ 10.f, 10.f });
    settle();
    EXPECT_EQ(counterBuilds, 2) << "the tap set state once";
    EXPECT_EQ(labelBuilds, 1) << "its sibling was not built again";
    EXPECT_TRUE(IsGreen(scene->At(10, 10)));

    settle();
    EXPECT_EQ(scene->ui.Stats().builds, 0u);
    EXPECT_EQ(scene->ui.Stats().layouts, 0u);
    EXPECT_EQ(scene->ui.Stats().paints, 0u) << "an idle frame does nothing";
}

TEST_F(WidgetTest, ReassemblingBuildsEverythingAgainAndKeepsState) {
    int builds = 0;
    Show(kui::Column({ kui::Make<Counter>(&builds) }));
    Click({ 10.f, 10.f });
    settle();
    EXPECT_TRUE(IsGreen(scene->At(10, 10))) << "n is 1";
    const int before = builds;
    scene->ui.Reassemble();   // what a hot reload does
    settle();
    EXPECT_GT(builds, before) << "built again";
    EXPECT_TRUE(IsGreen(scene->At(10, 10))) << "with its state";
}

namespace {
    /** @brief Remembers its own colour; keyed, it keeps it wherever its parent puts it. */
    struct Swatch final : kui::StatefulWidget {
        kui::Color color;
        explicit Swatch(const kui::Color c) : color(c) {}
        kui::Widget Build() override { return kui::SizedBox(64.f, 32.f, kui::DecoratedBox({ .color = color }, {})); }
    };
}

TEST_F(WidgetTest, KeyedChildrenKeepTheirStateWhenReordered) {
    Show(kui::Column({ kui::Make<Swatch>(Red).Key("a"), kui::Make<Swatch>(Green).Key("b") }));
    EXPECT_TRUE(IsRed(scene->At(10, 10)));
    // Rebuilt in the other order, with colours the kept state ignores: each stays itself.
    Show(kui::Column({ kui::Make<Swatch>(Blue).Key("b"), kui::Make<Swatch>(Blue).Key("a") }));
    EXPECT_TRUE(IsGreen(scene->At(10, 10)));
    EXPECT_TRUE(IsRed(scene->At(10, 40)));
}

TEST_F(WidgetTest, AButtonCallsBackWhenClickedAndClaimsThePointer) {
    int pressed = 0;
    Show(kui::Center(kui::Button("Go", [&] { ++pressed; })));
    Click({ 32.f, 32.f });
    EXPECT_EQ(pressed, 1);
    EXPECT_TRUE(scene->SceneInput().InterfaceWantsMouse()) << "the pointer is over the button";
    Click({ 1.f, 1.f });
    EXPECT_EQ(pressed, 1) << "a click beside it is not a click on it";
    settle();
    EXPECT_FALSE(scene->SceneInput().InterfaceWantsMouse());
}

TEST_F(WidgetTest, AnyWidgetCanBeAButton) {
    int pressed = 0;
    // A plain button: the child alone, clickable — here a box painted in the top-left quarter.
    Show(kui::Align(kui::Alignment::TopLeft(),
        kui::Button(kui::SizedBox(32.f, 32.f, kui::DecoratedBox({ .color = Green }, {})), [&] { ++pressed; },
                    { .style = kui::ButtonStyle::ePlain })));
    EXPECT_TRUE(IsGreen(scene->At(16, 16))) << "the child is what shows";
    Click({ 16.f, 16.f });
    EXPECT_EQ(pressed, 1) << "and clicking it presses the button";
    Click({ 48.f, 48.f });
    EXPECT_EQ(pressed, 1) << "outside it, nothing";
}

TEST_F(WidgetTest, ScrollingMovesALayerAndRepaintsNothing) {
    std::vector<kui::Widget> rows;
    for (int i = 0; i < 10; ++i) rows.push_back(kui::SizedBox(64.f, 16.f, kui::DecoratedBox({ .color = i % 2 ? Green : Red }, {})));
    Show(kui::ScrollView(kui::Column(std::move(rows))));
    EXPECT_TRUE(IsRed(scene->At(10, 4)));

    scene->SceneInput().FeedMousePosition({ 10.f, 10.f });
    settle();
    scene->SceneInput().FeedScroll({ 0.f, -1.f });   // one notch down: 48 units
    settle();
    EXPECT_EQ(scene->ui.Stats().paints, 0u) << "the content kept its picture";
    EXPECT_FALSE(scene->ui.GetRenderer().Stats().recomposed) << "only a layer moved";
    settle();
    EXPECT_TRUE(IsGreen(scene->At(10, 4))) << "row 3 (green) is at the top now";
}

TEST_F(WidgetTest, ATextFieldTakesTypedTextOnceClicked) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    std::string text, submitted;
    Show(kui::TextField({ .placeholder = "name", .onChanged = [&](const std::string& t) { text = t; },
                          .onSubmitted = [&](const std::string& t) { submitted = t; } }));
    auto& input = scene->SceneInput();
    input.FeedText(U"ignored");
    settle();
    EXPECT_EQ(text, "") << "not focused yet";

    Click({ 20.f, 10.f });
    EXPECT_TRUE(input.InterfaceWantsKeyboard());
    input.FeedText(U"héllo");
    settle();
    EXPECT_EQ(text, "héllo");
    input.FeedKey(kor::Key::eBackspace, true);
    settle();
    input.FeedKey(kor::Key::eBackspace, false);
    input.FeedKey(kor::Key::eEnter, true);
    settle();
    EXPECT_EQ(text, "héll");
    EXPECT_EQ(submitted, "héll");
}

TEST_F(WidgetTest, AVirtualListBuildsOnlyWhatIsNearTheView) {
    std::size_t built = 0;
    Show(kui::ListView(1'000'000, 16.f, [&](const std::size_t i) {
        ++built;
        return kui::DecoratedBox({ .color = i % 2 ? Green : Red }, {});
    }));
    settle();   // the first frame builds a guess; the list then asks for what is near its view
    EXPECT_LT(built, 200u) << "a million rows, a few screens of them built";
    EXPECT_TRUE(IsRed(scene->At(10, 4)));
    EXPECT_TRUE(IsGreen(scene->At(10, 20)));

    built = 0;
    scene->SceneInput().FeedMousePosition({ 10.f, 10.f });
    settle();
    scene->SceneInput().FeedScroll({ 0.f, -1.f });   // 48 units: three rows
    settle();
    settle();
    EXPECT_TRUE(IsGreen(scene->At(10, 4))) << "row 3 at the top";
    EXPECT_EQ(built, 3u) << "items already built stay built; the three scrolled into range join them";
}

TEST_F(WidgetTest, AListGivesEachItemALayerOfItsOwn) {
    int builds = 0;
    std::vector<kui::Widget> items;
    for (int i = 0; i < 50; ++i) items.push_back(kui::Make<Counter>(&builds));
    Show(kui::ListView(std::move(items)));
    Click({ 10.f, 10.f });
    settle();
    EXPECT_EQ(scene->ui.Stats().paints, 0u);
    EXPECT_LT(scene->ui.GetRenderer().Stats().uploadedBytes, 1024u * 4) << "one item changed: about one item's worth of bytes";
}

// ---- a showcase, written to a PNG when KUI_SHOWCASE names one --------------------------------------------

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)
#include <stb_image_write.h>

namespace {
    constexpr int ShowW = 720, ShowH = 480;

    struct Showcase final : kui::StatefulWidget {
        bool checked = true, toggled = true;
        float volume = 0.6f;
        kui::Widget Build() override {
            const auto& t = kui::Theme::Current();
            kui::TextStyle title = t.textStyle;
            title.size = 28.f;
            title.font = kui::Font::Load("fonts/Inter_28pt-Bold.ttf");
            kui::TextStyle muted = t.textStyle;
            muted.color = t.textMuted;

            auto shapes = kui::CustomPaint([](kui::Canvas& c, const glm::vec2 size) {
                c.DrawRRect({ kui::Rect::FromSize(size), 10.f }, kui::Paint{}.SetGradient(
                    kui::Gradient::Linear({ 0, 0 }, { size.x, size.y }, kui::Color::Hex(0x2B2F6B), kui::Color::Hex(0x6B2B5E))));
                c.DrawCircle({ 50, 60 }, 30.f, kui::Paint::Fill(kui::Color::Hex(0xFFB74D)).SetStroke(3.f, kui::colors::White));
                c.DrawArc({ 130, 60 }, 30.f, -1.57f, 4.2f, false, kui::Paint::Stroked(kui::Color::Hex(0x4DD0E1), 6.f)
                    .SetStroke({ .width = 6.f, .color = kui::Color::Hex(0x4DD0E1), .cap = kui::StrokeCap::eRound }));
                c.DrawTriangle({ 180, 90 }, { 240, 90 }, { 210, 30 }, kui::Paint::Fill(kui::Color::Hex(0x81C784)));
                kui::Path star;
                for (int i = 0; i < 10; ++i) {
                    const float a = -1.5708f + i * 3.14159f / 5.f, r = i % 2 ? 12.f : 30.f;
                    const glm::vec2 p { 290 + r * std::cos(a), 62 + r * std::sin(a) };
                    if (i == 0) star.MoveTo(p); else star.LineTo(p);
                }
                star.Close();
                c.DrawPath(star, kui::Paint::Fill(kui::Color::Hex(0xFFF176)).SetStroke({ .width = 2.f, .color = kui::Color::Hex(0xF57F17), .join = kui::StrokeJoin::eRound }));
                c.DrawCubicBezier({ 20, 120 }, { 100, 80 }, { 200, 160 }, { 320, 110 }, kui::Paint::Stroked(kui::colors::White.WithAlpha(0.8f), 3.f));
                c.DrawShadow({ kui::Rect::XYWH(20, 140, 120, 30), 8.f }, kui::colors::Black.WithAlpha(0.6f), 6.f, { 0, 4 });
                c.DrawRRect({ kui::Rect::XYWH(20, 140, 120, 30), 8.f }, kui::Paint::Fill(kui::Color::Hex(0xECEFF1)));
                c.DrawText("Canvas text", { 32, 146 }, { .size = 14.f, .color = kui::Color::Hex(0x263238) });
            }, { 340.f, 190.f });

            return kui::Container({ .padding = kui::EdgeInsets::All(24.f), .decoration = { .color = t.background } },
                kui::Row({
                    kui::Column({
                        kui::Text("koral-ui", title),
                        kui::Text("Retained widgets over a signed-distance canvas — one instanced draw.", muted),
                        kui::SizedBox(-1.f, 8.f),
                        shapes,
                    }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStart, .gap = 6.f }),
                    kui::SizedBox(24.f, 0.f),
                    kui::Expanded(kui::Container({ .padding = kui::EdgeInsets::All(16.f),
                                                   .decoration = { .color = t.surface, .borderWidth = 1.f, .borderColor = t.border, .radius = 12.f,
                                                                   .shadowColor = kui::colors::Black.WithAlpha(0.5f), .shadowBlur = 12.f, .shadowOffset = { 0, 6 } } },
                        kui::Column({
                            kui::Text("Settings", t.textStyle),
                            kui::TextField({ .placeholder = "Player name" }),
                            kui::Checkbox(checked, [this](bool v) { SetState([&] { checked = v; }); }, "Show subtitles"),
                            kui::Row({ kui::Text("Fullscreen", t.textStyle), kui::Switch(toggled, [this](bool v) { SetState([&] { toggled = v; }); }) },
                                     { .mainAxisAlignment = kui::MainAxisAlignment::eSpaceBetween }),
                            kui::Text("Volume", muted),
                            kui::Slider(volume, [this](float v) { SetState([&] { volume = v; }); }),
                            kui::ProgressBar(0.42f),
                            kui::Row({ kui::Button("Cancel", [] {}, { .style = kui::ButtonStyle::eSecondary }), kui::Button("Apply", [] {}) },
                                     { .mainAxisAlignment = kui::MainAxisAlignment::eEnd, .gap = 8.f }),
                        }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStretch, .gap = 12.f }))),
                }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStart }));
        }
    };

    class ShowcaseScene final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(ShowW * ShowH * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            ui.SetRoot(kui::Make<Showcase>());
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
        kor::Resource<kor::Buffer> readback;
    };
}

TEST(Showcase, Renders) {
    const char* out = std::getenv("KUI_SHOWCASE");
    if (!s_app || !out) GTEST_SKIP() << "set KUI_SHOWCASE to a .png path to render the showcase";
    auto* scene = s_app->OpenOffscreen<ShowcaseScene>({ .title = "showcase", .extent = { ShowW, ShowH }, .format = kor::Window::Format::eRGBA8_SRGB });
    ASSERT_NE(scene, nullptr);
    for (int i = 0; i < 4; ++i) settle();
    const auto pixels = scene->readback->Read<glm::u8vec4>(ShowW * ShowH);
    stbi_write_png(out, ShowW, ShowH, 4, pixels.data(), ShowW * 4);
    s_app->Close(*scene);
    settle();
}

// A stroked curve is solid wherever it should be, at any sub-pixel position: its fill and its
// anti-aliasing fringe share their vertices, so no pixel centre can fall between them. (It once could:
// a hairline crack between the two showed as a dark pixel in the middle of a stroke.)
TEST_F(Gpu, AStrokedCurveHasNoHolesAtAnyOffset) {
    for (float fy : { 0.f, 0.25f, 0.5f, 0.75f }) {
        for (float fx : { 0.f, 0.5f }) {
            Draw([&](kui::Canvas& c) {
                c.Translate({ -63.f + fx, -76.f + fy });
                kui::Path star;
                for (int i = 0; i < 10; ++i) {
                    const float a = -1.5708f + i * 3.14159f / 5.f, r = i % 2 ? 12.f : 30.f;
                    const glm::vec2 q { 290 + r * std::cos(a), 62 + r * std::sin(a) };
                    if (i == 0) star.MoveTo(q); else star.LineTo(q);
                }
                star.Close();
                c.DrawPath(star, kui::Paint::Fill(kui::Color::Hex(0xFFF176)).SetStroke({ .width = 2.f, .color = kui::Color::Hex(0xF57F17), .join = kui::StrokeJoin::eRound }));
                c.DrawCubicBezier({ 20, 120 }, { 100, 80 }, { 200, 160 }, { 320, 110 }, kui::Paint::Stroked(kui::colors::White.WithAlpha(0.8f), 3.f));
            });
            // Every pixel whose centre is well inside the stroke must be lit.
            kui::Path path;
            path.MoveTo({ 20, 120 }).CubicTo({ 100, 80 }, { 200, 160 }, { 320, 110 });
            const auto line = path.Flatten(0.01f)[0].points;
            for (int y = 0; y < Size; ++y) for (int x = 0; x < Size; ++x) {
                const glm::vec2 p { x + 0.5f + 63.f - fx, y + 0.5f + 76.f - fy };
                float d = 1e9f;
                for (std::size_t i = 0; i + 1 < line.size(); ++i) {
                    const glm::vec2 a = line[i], b = line[i + 1];
                    const float h = std::clamp(glm::dot(p - a, b - a) / glm::dot(b - a, b - a), 0.f, 1.f);
                    d = std::min(d, glm::length(p - a - (b - a) * h));
                }
                if (d < 0.9f) EXPECT_GT(scene->At(x, y).r, 190) << "pixel " << x << "," << y << " at offset " << fx << "," << fy << " d=" << d;
            }
        }
    }
}

// ---- how fast, when KUI_BENCH is set ----------------------------------------------------------------------

namespace {
    struct Row final : kui::StatefulWidget {
        int index;
        bool on = false;
        std::function<void(Row*)> registered;
        Row(const int i, std::function<void(Row*)> r) : index(i), registered(std::move(r)) {}
        void InitState() override { if (registered) registered(this); }
        void Toggle() { SetState([this] { on = !on; }); }
        kui::Widget Build() override {
            const auto& t = kui::Theme::Current();
            return kui::Container({ .height = 28.f, .padding = kui::EdgeInsets::Symmetric(10.f, 4.f),
                                    .decoration = { .color = on ? t.primary : (index % 2 ? t.surface : t.background), .radius = 4.f } },
                kui::Row({ kui::Text(std::format("Row {} — the quick brown fox", index), t.textStyle, kui::TextAlign::eStart, false),
                           kui::Checkbox(on, [](bool) {}) }, { .mainAxisAlignment = kui::MainAxisAlignment::eSpaceBetween }));
        }
    };

    struct BenchScene final : kor::Scene {
        bool virtualList = false;
        void Initialize() override {
            if (virtualList) {
                ui.SetRoot(kui::ListView(100'000, 30.f, [this](const std::size_t i) {
                    return kui::Make<Row>(static_cast<int>(i), [this](Row* r) { all.push_back(r); });
                }));
            } else {
                std::vector<kui::Widget> rows;
                for (int i = 0; i < 2000; ++i) rows.push_back(kui::Make<Row>(i, [this](Row* r) { all.push_back(r); }));
                ui.SetRoot(kui::ListView(std::move(rows), kui::Axis::eVertical, 2.f));
            }
            Graph().Add<kui::UiPass>(ui);
        }
        void Update() override {
            const auto begin = std::chrono::steady_clock::now();
            ui.Update();
            updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        }
        kui::Ui ui;
        std::vector<Row*> all;
        double updateMs = 0.0;
    };
}

TEST(Bench, ListsOfThousandsOfRows) {
    if (!s_app || !std::getenv("KUI_BENCH")) GTEST_SKIP() << "set KUI_BENCH to measure";
    for (const bool virtualList : { false, true }) {
        std::printf("%s\n", virtualList ? "-- ListView(100000, builder): only what is near the view exists" : "-- ListView of 2000 rows, each in a layer of its own");
        auto scene = std::make_unique<BenchScene>();
        scene->virtualList = virtualList;
        auto* bench = static_cast<BenchScene*>(s_app->OpenOffscreen("bench", std::move(scene), { .title = "bench", .extent = { 1280, 720 } }));
        ASSERT_NE(bench, nullptr);
        const auto report = [&](const char* what) {
            const auto& v = bench->ui.Stats();
            const auto& r = bench->ui.GetRenderer().Stats();
            std::printf("%-20s update %8.2f ms [build %.2f layout %.2f paint %.2f] (builds %4zu layouts %5zu paints %4zu)  compose %5.2f ms  uploaded %8zu B  instances %6zu  layers %5zu  draws %zu\n",
                        what, bench->updateMs, v.buildMs, v.layoutMs, v.paintMs, v.builds, v.layouts, v.paints, r.composeMs, r.uploadedBytes, r.instances, r.layers, r.draws);
        };
        settle(); report("first frame");
        settle(); report("settled");
        settle(); report("idle");
        bench->all[5]->Toggle();
        settle(); report("one row's SetState");
        bench->SceneInput().FeedMousePosition({ 600.f, 300.f });
        settle();
        bench->SceneInput().FeedScroll({ 0.f, -3.f });
        settle(); report("scroll");
        settle(); report("after scroll");
        settle(); report("idle");
        s_app->Close(*bench);
        settle();
    }
}

// ---- modifiers and drag and drop -------------------------------------------------------------------------

TEST_F(WidgetTest, ModifiersWrapAWidgetInsideOut) {
    int taps = 0;
    // A red 16x16 box, 8 of blue around it, in the top-left corner; clicking it counts.
    Show(kui::SizedBox(16.f, 16.f).Background(Red).Padding(8.f).Background(Blue).OnTap([&] { ++taps; }).Align(kui::Alignment::TopLeft()));
    EXPECT_TRUE(IsBlue(scene->At(3, 3))) << "the padding, outside the red";
    EXPECT_TRUE(IsRed(scene->At(16, 16)));
    EXPECT_TRUE(IsBlack(scene->At(40, 40))) << "and nothing beyond";
    Click({ 16.f, 16.f });
    EXPECT_EQ(taps, 1);
}

TEST_F(WidgetTest, ADragIsDroppedOnTheTargetThatAcceptsIt) {
    std::string dropped, ended;
    int entered = 0, left = 0;
    Show(kui::Row({
        kui::SizedBox(20.f, 64.f).Background(Red).Draggable({ "word", std::string("hello") },
            kui::DraggableOptions {}.OnDragEnd([&](const bool ok) { ended = ok ? "accepted" : "cancelled"; })),
        kui::SizedBox(20.f, 64.f).Background(Blue).OnDrop("number", [&](const kui::DragData&) { dropped = "wrong target"; }),
        kui::SizedBox(24.f, 64.f).Background(Green).DropTarget(kui::DropTargetOptions {}.AcceptsType("word")
            .OnEnter([&](const kui::DragData&) { ++entered; }).OnLeave([&] { ++left; })
            .OnDrop([&](const kui::DragData& d, glm::vec2) { dropped = *d.As<std::string>(); })),
    }));
    auto& input = scene->SceneInput();
    const auto frame = [&](const glm::vec2 p) { input.FeedMousePosition(p); settle(); };

    frame({ 10.f, 30.f });
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    settle();
    frame({ 20.f, 30.f });   // past the slop: the drag begins
    frame({ 30.f, 30.f });   // over the target for numbers, which does not take words
    EXPECT_EQ(entered, 0);
    EXPECT_TRUE(scene->ui.WantsPointer());
    frame({ 50.f, 30.f });
    EXPECT_EQ(entered, 1) << "over the target that accepts it";
    EXPECT_FALSE(IsGreen(scene->At(46, 26))) << "the ghost follows the pointer, over the target";
    input.FeedMouseButton(kor::MouseButton::eLeft, false);
    settle();
    EXPECT_EQ(dropped, "hello");
    EXPECT_EQ(ended, "accepted");
    EXPECT_EQ(left, 1) << "told the drag is over";
    settle();
    EXPECT_TRUE(IsGreen(scene->At(46, 26))) << "the ghost is gone";

    // Let go over nothing that takes it: cancelled.
    dropped.clear();
    frame({ 10.f, 30.f });
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    settle();
    frame({ 20.f, 30.f });
    frame({ 30.f, 30.f });
    input.FeedMouseButton(kor::MouseButton::eLeft, false);
    settle();
    EXPECT_EQ(ended, "cancelled");
    EXPECT_TRUE(dropped.empty());
}

// ---- docking -----------------------------------------------------------------------------------------------

namespace {
    class DockScene final : public kor::Scene {
    public:
        void Initialize() override {
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
    };

    /** @brief A panel that fills its space, counts its taps in its own state, and says how big it is. */
    struct Tapped final : kui::StatefulWidget {
        int n = 0;
        int* out;
        glm::vec2* size;
        Tapped(int* o, glm::vec2* s) : out(o), size(s) {}
        kui::Widget Build() override {
            return kui::CustomPaint([s = size](kui::Canvas&, const glm::vec2 given) { if (s) *s = given; })
                .OnTap([this] { SetState([&] { ++n; }); if (out) *out = n; });
        }
    };

    class DockTest : public ::testing::Test {
    protected:
        void SetUp() override {
            if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
            scene = s_app->OpenOffscreen<DockScene>({ .title = "kui dock", .extent = { 240, 160 } });
            ASSERT_NE(scene, nullptr);
            layout = std::make_shared<kui::DockLayout>();
            layout->Dock("scene").Dock("inspector", kui::DockSide::eRight, "scene", 0.25f).Dock("log", kui::DockSide::eBottom, "scene", 0.3f);
            // Inspector first: it waits for the scene it goes beside.
            scene->ui.SetRoot(kui::DockSpace(layout, {
                { "inspector", "Inspector", kui::Make<Tapped>(nullptr, &inspector) },
                { "scene", "Scene", kui::Make<Tapped>(nullptr, &view) },
                { "log", "Log", kui::Make<Tapped>(&taps, &log) },
            }, kui::DockOptions {}.OnChanged([this] { ++changes; }).OnClosed([this](const std::string& id) { closed = id; })));
            settle(); settle();
        }
        void TearDown() override {
            if (scene) { s_app->Close(*scene); settle(); }
        }
        void Move(const glm::vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); }
        void Button(const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); }
        void Click(const glm::vec2 p) { Move(p); Button(true); Button(false); }
        void Drag(const glm::vec2 from, const glm::vec2 to) {
            Move(from); Button(true);
            Move(from + (to - from) * 0.5f); Move(to); Move(to);
            Button(false); settle();
        }
        DockScene* scene = nullptr;
        std::shared_ptr<kui::DockLayout> layout;
        glm::vec2 view {}, inspector {}, log {};
        int taps = 0, changes = 0;
        std::string closed;
    };
}

TEST_F(DockTest, PanelsGoWhereTheLayoutSaysAndFillTheirShare) {
    // 240 wide: a quarter (of what the 4-wide line leaves) on the right; 160 tall: 30% at the bottom, bars of 28.
    EXPECT_FLOAT_EQ(inspector.x, 59.f);
    EXPECT_FLOAT_EQ(inspector.y, 132.f);
    EXPECT_FLOAT_EQ(view.x, 177.f);
    EXPECT_FLOAT_EQ(view.y, 81.f);
    EXPECT_FLOAT_EQ(log.x, 177.f);
    EXPECT_FLOAT_EQ(log.y, 19.f);
    Click({ 20.f, 150.f });
    EXPECT_EQ(taps, 1) << "a click in a panel reaches the panel";
    EXPECT_TRUE(scene->ui.WantsPointer());
}

TEST_F(DockTest, ATabDraggedToAnotherGroupKeepsItsState) {
    Click({ 20.f, 150.f });
    ASSERT_EQ(taps, 1);
    Drag({ 12.f, 127.f }, { 200.f, 14.f });   // the Log tab, onto the Inspector's bar
    EXPECT_FLOAT_EQ(view.y, 132.f) << "the scene has the whole column";
    EXPECT_FLOAT_EQ(log.x, 59.f) << "the log is where the inspector was, and in front";
    EXPECT_GT(changes, 0);
    Click({ 210.f, 100.f });
    EXPECT_EQ(taps, 2) << "the same panel, with what it counted before";

    // And to an edge: the left of the scene, split in two.
    const std::string before = layout->Save();
    layout->Dock("log", kui::DockSide::eLeft, "scene", 0.5f);
    settle(); settle();
    EXPECT_NE(layout->Save(), before);
    EXPECT_FLOAT_EQ(log.y, 132.f);
    Click({ 20.f, 100.f });
    EXPECT_EQ(taps, 3);
}

TEST_F(DockTest, TheLineBetweenGroupsResizesThem) {
    Drag({ 179.f, 80.f }, { 139.f, 80.f });
    EXPECT_FLOAT_EQ(view.x, 137.f);
    EXPECT_FLOAT_EQ(inspector.x, 99.f);
}

TEST_F(DockTest, DroppedInTheMiddleItFloatsAndTheLayoutSurvivesSaving) {
    EXPECT_FALSE(layout->IsFloating("inspector"));
    Drag({ 190.f, 14.f }, { 80.f, 60.f });   // the Inspector's tab, into the middle of the scene
    EXPECT_TRUE(layout->IsFloating("inspector"));
    EXPECT_FLOAT_EQ(view.x, 240.f) << "what it left closes up";

    const std::string saved = layout->Save();
    auto other = std::make_shared<kui::DockLayout>();
    ASSERT_TRUE(other->Load(saved));
    EXPECT_EQ(other->Save(), saved);
    EXPECT_TRUE(other->IsFloating("inspector"));
    EXPECT_FALSE(other->Load("not a layout"));

    layout->Close("inspector");
    settle();
    EXPECT_FALSE(layout->IsOpen("inspector"));
    layout->Open("inspector");
    layout->Dock("inspector", kui::DockSide::eRight, {}, 0.5f);
    settle(); settle();
    EXPECT_FALSE(layout->IsFloating("inspector"));
    EXPECT_FLOAT_EQ(inspector.x, 118.f);
}

TEST_F(DockTest, TheRimOfTheSpaceDocksBesideEverything) {
    Drag({ 12.f, 127.f }, { 236.f, 80.f });   // the Log tab, to the right rim of the space
    EXPECT_FLOAT_EQ(log.y, 132.f) << "the whole height, beside everything else";
    EXPECT_FLOAT_EQ(log.x, 59.f) << "a quarter of the space";
    EXPECT_FLOAT_EQ(view.y, 132.f);
    EXPECT_FALSE(layout->IsFloating("log"));
}

namespace {
    /** @brief Two interfaces in one window, one drawn over the other: a drag begun in one lands in the other. */
    class TwoUis final : public kor::Scene {
    public:
        void Initialize() override {
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(below);
            Graph().Add<kui::UiPass>(above);
        }
        void Update() override { below.Update(); above.Update(); }
        kui::Ui below, above;
    };
}

TEST(DragAcrossUis, ADragFromOneUiLandsInAnother) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<TwoUis>({ .title = "two uis", .extent = { 64, 64 } });
    ASSERT_NE(scene, nullptr);
    std::string dropped;
    bool accepted = false;
    // The target fills the lower view; the source is a box in the corner of the upper one.
    scene->below.SetRoot(kui::SizedBox(64.f, 64.f).OnDrop("word", [&](const kui::DragData& d) { dropped = *d.As<std::string>(); }));
    scene->above.SetRoot(kui::SizedBox(20.f, 20.f).Background(Red)
        .Draggable({ "word", std::string("across") }, kui::DraggableOptions {}.OnDragEnd([&](const bool ok) { accepted = ok; }))
        .Align(kui::Alignment::TopLeft()));
    settle(); settle();
    auto& input = scene->SceneInput();
    const auto frame = [&](const glm::vec2 p) { input.FeedMousePosition(p); settle(); };
    frame({ 10.f, 10.f });
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    settle();
    frame({ 20.f, 20.f });
    frame({ 50.f, 50.f });
    input.FeedMouseButton(kor::MouseButton::eLeft, false);
    settle();
    EXPECT_EQ(dropped, "across");
    EXPECT_TRUE(accepted);
    s_app->Close(*scene);
    settle();
}
