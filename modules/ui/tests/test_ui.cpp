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

TEST_F(WidgetTest, WhatIsScrolledFarOutOfViewIsNotPainted) {
    // Two hundred rows of sixteen in a view of sixty-four: fifty views of content.
    std::vector<int> painted(200, 0);
    std::vector<kui::Widget> rows;
    for (int i = 0; i < 200; ++i) {
        rows.push_back(kui::CustomPaint([&painted, i](kui::Canvas& c, const glm::vec2 s) {
            ++painted[static_cast<std::size_t>(i)];
            c.DrawRect(kui::Rect::FromSize(s), kui::Paint::Fill(i % 2 ? Green : Red));
        }, { 64.f, 16.f }));
    }
    Show(kui::ScrollView(kui::Column(std::move(rows))));
    EXPECT_TRUE(IsRed(scene->At(10, 4)));
    EXPECT_GT(painted[0], 0);
    EXPECT_GT(painted[7], 0) << "within a view of what shows";
    EXPECT_EQ(painted[100], 0) << "far below: not in the picture at all";
    const std::size_t all = scene->ui.GetRenderer().Stats().instances;
    EXPECT_LT(all, 40u) << "and not handed to the GPU";

    // A notch at a time, well past what was painted: what comes into view is there when it does.
    scene->SceneInput().FeedMousePosition({ 10.f, 10.f });
    settle();
    for (int i = 0; i < 10; ++i) { scene->SceneInput().FeedScroll({ 0.f, -1.f }); settle(); }
    settle();
    // 480 units down: row 30 is at the top, and it is red.
    EXPECT_TRUE(IsRed(scene->At(10, 4)));
    EXPECT_TRUE(IsGreen(scene->At(10, 20)));
    EXPECT_GT(painted[30], 0);
    EXPECT_EQ(painted[100], 0);
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

TEST_F(WidgetTest, ATextFieldSelectsReplacesAndTakesSeveralLines) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    std::string text;
    auto& input = scene->SceneInput();
    const auto press = [&](const kor::Key key) { input.FeedKey(key, true); settle(); input.FeedKey(key, false); settle(); };
    Show(kui::Align(kui::Alignment::TopLeft(), kui::TextField({ .onChanged = [&](const std::string& t) { text = t; }, .width = 60.f })));
    Click({ 20.f, 10.f });
    input.FeedText(U"abc");
    settle();
    ASSERT_EQ(text, "abc");

    // Shift and Left, twice: the last two letters are selected, and what is typed takes their place.
    input.FeedKey(kor::Key::eLeftShift, true);
    press(kor::Key::eLeft);
    press(kor::Key::eLeft);
    input.FeedKey(kor::Key::eLeftShift, false);
    input.FeedText(U"X");
    settle();
    EXPECT_EQ(text, "aX");

    // Control and A: all of it.
    input.FeedKey(kor::Key::eLeftControl, true);
    press(kor::Key::eA);
    input.FeedKey(kor::Key::eLeftControl, false);
    settle();
    press(kor::Key::eBackspace);
    EXPECT_EQ(text, "");

    // Several lines: Enter starts another, and the field grows to hold it.
    glm::vec2 before = {};
    Show(kui::Align(kui::Alignment::TopLeft(), kui::TextField(kui::TextFieldOptions {}.SetWidth(60.f).SetMultiline(1, 3)
                                                                  .OnChanged([&](const std::string& t) { text = t; }))));
    Click({ 20.f, 10.f });
    input.FeedText(U"a");
    settle();
    EXPECT_TRUE(IsBlack(scene->At(30, 50))) << "one line tall";
    press(kor::Key::eEnter);
    input.FeedText(U"b");
    settle();
    press(kor::Key::eEnter);
    input.FeedText(U"c");
    settle(); settle();
    EXPECT_EQ(text, "a\nb\nc");
    EXPECT_FALSE(IsBlack(scene->At(30, 50))) << "three lines tall now";
    (void) before;
}

TEST_F(WidgetTest, TabGoesFromOneFieldToTheNext) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    std::string first, second;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Column({
        kui::TextField({ .onChanged = [&](const std::string& t) { first = t; }, .width = 60.f }),
        kui::TextField({ .onChanged = [&](const std::string& t) { second = t; }, .width = 60.f }),
    })));
    auto& input = scene->SceneInput();
    Click({ 20.f, 10.f });
    input.FeedText(U"a");
    settle();
    input.FeedKey(kor::Key::eTab, true); settle(); input.FeedKey(kor::Key::eTab, false); settle();
    input.FeedText(U"b");
    settle();
    EXPECT_EQ(first, "a");
    EXPECT_EQ(second, "b") << "the keyboard went to the field after";
    input.FeedKey(kor::Key::eTab, true); settle(); input.FeedKey(kor::Key::eTab, false); settle();
    input.FeedText(U"c");
    settle();
    EXPECT_EQ(first, "ac") << "and round to the first again";
}

TEST_F(WidgetTest, SharesRatiosTransformsAndLayoutsOfTheCallersOwn) {
    glm::vec2 size {};
    Show(kui::FractionallySizedBox(0.5f, 0.25f, Probe(size)));
    EXPECT_FLOAT_EQ(size.x, 32.f);
    EXPECT_FLOAT_EQ(size.y, 16.f);

    Show(kui::Align(kui::Alignment::TopLeft(), kui::AspectRatio(2.f, Probe(size))));
    EXPECT_FLOAT_EQ(size.x, 64.f);
    EXPECT_FLOAT_EQ(size.y, 32.f) << "half as tall as it is wide";

    // A rule of the caller's own: the second child ten square, at (40, 10); the whole as big as it may be.
    const auto dot = [](const kui::Color color) { return kui::DecoratedBox({ .color = color }, {}); };
    Show(kui::CustomLayout([](kui::LayoutContext& context, const kui::BoxConstraints& c) {
        context.measure(0, kui::BoxConstraints::Tight({ 6.f, 6.f }));
        context.place(0, { 0.f, 0.f });
        const glm::vec2 second = context.measure(1, kui::BoxConstraints::Loose({ 10.f, 10.f }).Tighten(10.f, 10.f));
        context.place(1, { 40.f, second.y });
        return c.Biggest();
    }, { dot(Red), dot(Green) }));
    EXPECT_TRUE(IsRed(scene->At(3, 3)));
    EXPECT_TRUE(IsGreen(scene->At(45, 15)));
    EXPECT_TRUE(IsBlack(scene->At(45, 5)));

    // Twice the size about its top-left corner: drawn there, and pressed there.
    int taps = 0;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::TransformBox(kui::Transform::Scaling({ 2.f, 2.f }),
        kui::GestureDetector(kui::GestureOptions {}.OnTap([&] { ++taps; }), kui::SizedBox(10.f, 10.f, dot(Red))), kui::Alignment::TopLeft())));
    EXPECT_TRUE(IsRed(scene->At(15, 15)));
    EXPECT_TRUE(IsBlack(scene->At(25, 15)));
    Click({ 15.f, 15.f });
    EXPECT_EQ(taps, 1) << "hit where it is drawn";
}

TEST_F(WidgetTest, APopupIsShownUnderWhatItIsInAndSaysWhenItGoes) {
    int dismissed = 0;
    const auto dot = [](const kui::Color color, const float size) { return kui::SizedBox(size, size, kui::DecoratedBox({ .color = color }, {})); };
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Stack({ dot(Red, 20.f), kui::PopupAnchor(true, dot(Green, 10.f), [&] { ++dismissed; }) })));
    settle(); settle();
    EXPECT_TRUE(IsRed(scene->At(5, 5)));
    EXPECT_TRUE(IsGreen(scene->At(5, 25))) << "under it, at its left";
    Click({ 50.f, 50.f });
    settle();
    EXPECT_EQ(dismissed, 1) << "a press outside it";
    EXPECT_TRUE(IsBlack(scene->At(5, 25)));
}

TEST_F(WidgetTest, AScrollViewSaysWhereItIsAndGoesWhereItIsTold) {
    float at = -1.f, most = -1.f;
    const auto view = [&](const float jumpTo, const std::uint32_t jump) {
        return kui::ScrollView(kui::SizedBox(64.f, 200.f, kui::DecoratedBox({ .color = Red }, {})),
                               kui::ScrollOptions { kui::Axis::eVertical, [&](const float p, const float m) { at = p; most = m; }, jumpTo, jump });
    };
    Show(view(0.f, 0));
    EXPECT_FLOAT_EQ(at, 0.f);
    EXPECT_FLOAT_EQ(most, 136.f) << "two hundred of content in sixty-four of view";
    Show(view(50.f, 1));
    EXPECT_FLOAT_EQ(at, 50.f);
    Show(view(500.f, 2));
    EXPECT_FLOAT_EQ(at, 136.f) << "no further than there is";
}

TEST_F(WidgetTest, AThemeOfItsOwnLinesKeptToAFewAndASliderLetGoOf) {
    // A theme for part of the tree: what is under it is built and painted with it, not with the view's.
    kui::Theme blue;
    blue.primary = blue.primaryHover = blue.primaryPressed = Blue;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Themed(blue, kui::Button("", [] {}, kui::ButtonOptions {}.SetWidth(40.f)))));
    EXPECT_TRUE(IsBlue(scene->At(20, 18))) << "a button in the theme set over it";

    // A square checkbox, where the theme says how round one is.
    kui::Theme square;
    square.checkboxRadius = 0.f;
    square.primary = square.primaryHover = Red;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Themed(square, kui::Checkbox(true, [](bool) {}))));
    EXPECT_TRUE(IsRed(scene->At(2, 2))) << "filled to its corner: not a circle";

    if (kui::Font::Default()) {
        glm::vec2 size {};
        kui::TextStyle style { .size = 10.f, .color = kui::colors::White };
        const auto text = [&](const int lines) {
            return kui::Align(kui::Alignment::TopLeft(), kui::SizeObserver([&](const glm::vec2 s, glm::vec2) { size = s; },
                              kui::Text("one two three four five six seven eight nine ten", style, kui::TextAlign::eStart, true, lines, true)));
        };
        Show(text(0));
        EXPECT_GT(size.y, 30.f) << "as many lines as it takes";
        Show(text(1));
        EXPECT_FLOAT_EQ(size.y, 12.5f) << "one line, ending in an ellipsis";
        EXPECT_LE(size.x, 64.f);
        Show(text(2));
        EXPECT_FLOAT_EQ(size.y, 25.f);
    }

    int finished = 0;
    float value = 0.f;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Slider(0.f, [&](const float v) { value = v; }, 0.f, 1.f, [&] { ++finished; })));
    Click({ 30.f, 14.f });
    EXPECT_GT(value, 0.f);
    EXPECT_EQ(finished, 1) << "told once, when it is let go of";
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

TEST_F(WidgetTest, ALazyListsItemsAreAsLongAsTheyLikeAndItSaysWhereItIs) {
    std::size_t first = 99;
    float into = -1.f;
    const auto list = [&](const std::uint32_t jump) {
        kui::LazyListOptions options;
        options.count = 100'000;
        options.estimatedExtent = 20.f;
        options.onScrolled = [&](const std::size_t i, const float o) { first = i; into = o; };
        options.jumpIndex = 101;
        options.jump = jump;
        // The even ones ten tall and red, the odd ones thirty and green.
        return kui::LazyList(options, [](const std::size_t i) { return kui::Container({ .height = i % 2 ? 30.f : 10.f, .decoration = { .color = i % 2 ? Green : Red } }, {}); });
    };
    Show(list(0));
    settle();
    EXPECT_TRUE(IsRed(scene->At(10, 5)));
    EXPECT_TRUE(IsGreen(scene->At(10, 25))) << "the second starts where the first, ten tall, ends";
    EXPECT_TRUE(IsRed(scene->At(10, 45))) << "and the third where the second, thirty tall, does";
    EXPECT_EQ(first, 0u);

    scene->SceneInput().FeedMousePosition({ 10.f, 10.f });
    settle();
    scene->SceneInput().FeedScroll({ 0.f, -1.f });   // 48 units: past the first two, eight into the third
    settle();
    settle();
    EXPECT_EQ(first, 2u);
    EXPECT_FLOAT_EQ(into, 8.f);
    EXPECT_TRUE(IsGreen(scene->At(10, 5))) << "the fourth, two units down";

    Show(list(1));
    settle();
    settle();
    EXPECT_EQ(first, 101u) << "told to go to an item, it is first in view";
    EXPECT_TRUE(IsGreen(scene->At(10, 5)));
    EXPECT_TRUE(IsRed(scene->At(10, 35))) << "however long the thousands before it turn out to be";
}

TEST_F(WidgetTest, ALazyListRunsAcrossToo) {
    kui::LazyListOptions options;
    options.count = 1'000'000;
    options.axis = kui::Axis::eHorizontal;
    std::size_t built = 0;
    Show(kui::LazyList(options, [&](const std::size_t i) { ++built; return kui::Container({ .width = 20.f, .decoration = { .color = i % 2 ? Green : Red } }, {}); }));
    settle();
    settle();
    EXPECT_LT(built, 400u);
    EXPECT_TRUE(IsRed(scene->At(5, 10)));
    EXPECT_TRUE(IsGreen(scene->At(25, 10)));
    scene->SceneInput().FeedMousePosition({ 10.f, 10.f });
    settle();
    scene->SceneInput().FeedScroll({ 0.f, -1.f });
    settle();
    settle();
    EXPECT_TRUE(IsRed(scene->At(5, 10))) << "48 along: eight into the third";
    EXPECT_TRUE(IsGreen(scene->At(15, 10)));
}

TEST_F(WidgetTest, IntrinsicIsAsWideAsItsWidestChildAndTextThatSaysBlackIsBlack) {
    // A column as wide as the forty of its first child: the second, stretched, is forty wide too.
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Intrinsic(true, false, kui::Column({
        kui::Container({ .width = 40.f, .height = 10.f, .decoration = { .color = Red } }, {}),
        kui::Container({ .height = 10.f, .decoration = { .color = Green } }, {}),
    }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStretch }))));
    settle();
    EXPECT_TRUE(IsGreen(scene->At(35, 15)));
    EXPECT_FALSE(IsGreen(scene->At(45, 15)));

    const auto darkest = [&] {
        int least = 255;
        for (int y = 0; y < 30; ++y) for (int x = 0; x < 60; ++x) least = std::min<int>(least, scene->At(x, y).r);
        return least;
    };
    kui::TextStyle black;
    black.size = 24.f;
    black.color = kui::colors::Black;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Container({ .width = 60.f, .height = 30.f, .decoration = { .color = kui::colors::White } }, kui::Text("HH", black))));
    settle();
    EXPECT_LT(darkest(), 60) << "black on white, in a dark theme whose own text is white";
    kui::TextStyle unsaid;
    unsaid.size = 24.f;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Container({ .width = 60.f, .height = 30.f, .decoration = { .color = kui::colors::White } }, kui::Text("HH", unsaid))));
    settle();
    EXPECT_GT(darkest(), 200) << "and text that does not say is the theme's";
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

TEST_F(WidgetTest, WhatCanBePickedTellsWhenItIs) {
    int radio = 0, picked = 0, tab = -1;
    bool open = false;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::RadioButton(false, [&] { ++radio; })));
    Click({ 10.f, 10.f });
    EXPECT_EQ(radio, 1);

    Show(kui::Align(kui::Alignment::TopLeft(), kui::Selectable("a", false, [&] { ++picked; })));
    Click({ 40.f, 10.f });
    EXPECT_EQ(picked, 1) << "the whole line, not only its text";

    // A header says what it should be now; whoever builds it keeps that, and shows what is under it.
    glm::vec2 under {};
    const auto header = [&] { return kui::Align(kui::Alignment::TopLeft(), kui::CollapsingHeader("h", open, [&](const bool now) { open = now; }, Probe(under))); };
    Show(header());
    EXPECT_TRUE(IsBlack(scene->At(30, 50))) << "shut: nothing under it";
    Click({ 30.f, 10.f });
    EXPECT_TRUE(open);
    Show(header());
    Click({ 30.f, 10.f });
    EXPECT_FALSE(open);

    Show(kui::Align(kui::Alignment::TopLeft(), kui::TabBar({ "A", "B" }, 0, [&](const int index) { tab = index; })));
    Click({ 50.f, 15.f });
    EXPECT_EQ(tab, 1);
}

TEST_F(WidgetTest, AStepSliderStopsOnlyAtItsSteps) {
    int value = 0;
    const auto slider = [&] { return kui::Align(kui::Alignment::TopLeft(), kui::StepSlider(value, 4, [&](const int step) { value = step; }, { .width = 62.f })); };
    Show(slider());
    // Four places of fourteen, three in from each end; the thumb is the accent's, in the first.
    const auto accent = [&](const int x) { const auto p = scene->At(x, 16); return p.r > 200 && p.g > 90 && p.g < 170 && p.b < 120; };
    EXPECT_TRUE(accent(6));
    EXPECT_FALSE(accent(34));
    Click({ 40.f, 16.f });
    EXPECT_EQ(value, 2) << "the third place, wherever in it the press was";
    Show(slider());
    EXPECT_TRUE(accent(34));
    EXPECT_FALSE(accent(6));
}

TEST_F(WidgetTest, AGradientsStopsArePickedMovedAddedAndTakenAway) {
    std::vector<kui::GradientStop> stops { { 0.f, Red }, { 1.f, Green } };
    int told = 0;
    const auto editor = [&] {
        return kui::Align(kui::Alignment::TopLeft(), kui::GradientEditor(stops, [&](std::vector<kui::GradientStop> now) { stops = std::move(now); ++told; },
                                                                         { .width = 64.f, .picker = false }));
    };
    Show(editor());
    // The bar is 48 wide from 8, 26 tall: red at its left end, green at its right.
    EXPECT_TRUE(IsRed(scene->At(10, 12)));
    EXPECT_TRUE(IsGreen(scene->At(54, 12)));

    // A press on the bar, where no handle is: a stop there, of the colour that was there.
    Click({ 32.f, 12.f });
    ASSERT_EQ(stops.size(), 3u);
    EXPECT_NEAR(stops[1].offset, 0.5f, 0.02f);
    EXPECT_NEAR(stops[1].color.r, (Red.r + Green.r) * 0.5f, 0.05f);

    // Its handle, under the bar, dragged to the right.
    Show(editor());
    auto& input = scene->SceneInput();
    input.FeedMousePosition({ 32.f, 38.f }); settle();
    input.FeedMouseButton(kor::MouseButton::eLeft, true); settle();
    input.FeedMousePosition({ 40.f, 38.f }); settle();
    input.FeedMousePosition({ 44.f, 38.f }); settle();
    input.FeedMouseButton(kor::MouseButton::eLeft, false); settle();
    EXPECT_NEAR(stops[1].offset, 0.75f, 0.02f);

    // And taken away with the right button.
    Show(editor());
    input.FeedMouseButton(kor::MouseButton::eRight, true); settle();
    input.FeedMouseButton(kor::MouseButton::eRight, false); settle();
    EXPECT_EQ(stops.size(), 2u);
    EXPECT_GE(told, 3);
}

TEST_F(WidgetTest, AStatusBarShowsTheLastThingSaidInTheWindowsOwnColour) {
    Show(kui::Column({ kui::Expanded(kui::SizedBox(1.f, 1.f, kui::DecoratedBox({ .color = Blue }, {}))), kui::StatusBar("it broke", kui::StatusLevel::eError) },
                     { .crossAxisAlignment = kui::CrossAxisAlignment::eStretch }));
    EXPECT_TRUE(IsBlue(scene->At(60, 36))) << "what is over it comes right down to it: no line between";
    EXPECT_TRUE(IsBlack(scene->At(60, 39))) << "the bar, in the background's colour — as what is round the docked panels is";
    EXPECT_TRUE(IsBlack(scene->At(60, 60)));
    const auto mark = scene->At(15, 51);
    EXPECT_GT(mark.r, 180) << "an error's mark, in red";
    EXPECT_LT(mark.b, 120);
}

TEST_F(WidgetTest, AScrollThumbHasAStripOfItsOwnBesideTheContent) {
    glm::vec2 size {};
    const auto content = [&](const float height) {
        return kui::ScrollView(kui::CustomPaint([&size](kui::Canvas& c, const glm::vec2 s) {
            size = s;
            c.DrawRect(kui::Rect::FromSize(s), kui::Paint::Fill(Red));
        }, { -1.f, height }));
    };
    Show(content(40.f));
    EXPECT_FLOAT_EQ(size.x, 64.f) << "it all fits: nothing to scroll, and the content has the whole width";
    Show(content(200.f));
    EXPECT_FLOAT_EQ(size.x, 56.f) << "more than shows: the content stops short of the thumb";
    EXPECT_TRUE(IsRed(scene->At(54, 30)));
    EXPECT_FALSE(IsRed(scene->At(58, 30))) << "the strip the thumb moves in";
}

TEST_F(WidgetTest, ASizeObserverTellsTheSizeWhenItChanges) {
    std::vector<glm::vec2> told;
    const auto root = [&](const float height) {
        return kui::Column({ kui::SizedBox(64.f, height), kui::Expanded(kui::SizeObserver([&](const glm::vec2 size, glm::vec2) { told.push_back(size); })) },
                           { .crossAxisAlignment = kui::CrossAxisAlignment::eStretch });
    };
    Show(root(24.f));
    ASSERT_EQ(told.size(), 1u) << "once, when it is first laid out";
    EXPECT_FLOAT_EQ(told.back().x, 64.f);
    EXPECT_FLOAT_EQ(told.back().y, 40.f) << "what the box over it leaves";
    settle();
    EXPECT_EQ(told.size(), 1u) << "and not again while it stays that size";
    Show(root(40.f));
    ASSERT_EQ(told.size(), 2u);
    EXPECT_FLOAT_EQ(told.back().y, 24.f);
}

TEST_F(WidgetTest, ATipShowsWhileThePointerIsOverWhatHasOne) {
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Tooltip("what it is", kui::SizedBox(20.f, 20.f, kui::DecoratedBox({ .color = Red }, {})))));
    scene->SceneInput().FeedMousePosition({ 40.f, 40.f });
    settle();
    EXPECT_TRUE(scene->ui.TooltipText().empty());
    scene->SceneInput().FeedMousePosition({ 10.f, 10.f });
    settle(); settle();
    EXPECT_EQ(scene->ui.TooltipText(), "what it is");
    EXPECT_FALSE(IsBlack(scene->At(34, 40))) << "drawn by the pointer, under it";
    EXPECT_TRUE(IsBlack(scene->At(3, 60))) << "and as big as its text, not as the view";
    scene->SceneInput().FeedMousePosition({ 50.f, 5.f });
    settle(); settle();
    EXPECT_TRUE(scene->ui.TooltipText().empty());
    EXPECT_TRUE(IsBlack(scene->At(34, 40)));
}

TEST_F(WidgetTest, ADialogKeepsThePointerFromWhatIsUnderIt) {
    int under = 0, dismissed = 0;
    const auto root = [&](const bool open) {
        return kui::Modal(open, kui::GestureDetector(kui::GestureOptions {}.OnTap([&] { ++under; }), kui::Container({ .decoration = { .color = Red }, .alignment = kui::Alignment::Center() })),
                          kui::SizedBox(4.f, 4.f), [&] { ++dismissed; });
    };
    Show(root(false));
    Click({ 4.f, 4.f });
    EXPECT_EQ(under, 1);
    Show(root(true));
    EXPECT_FALSE(IsRed(scene->At(4, 4))) << "dimmed";
    Click({ 4.f, 4.f });
    EXPECT_EQ(under, 1) << "the shade took it";
    EXPECT_EQ(dismissed, 1);
    Click({ 32.f, 32.f });
    EXPECT_EQ(dismissed, 1) << "a press on the dialog itself dismisses nothing";
}

TEST_F(WidgetTest, AColourIsPickedFromTheSquare) {
    kui::Color picked = kui::colors::White;
    Show(kui::Align(kui::Alignment::TopLeft(), kui::ColorPicker(kui::colors::White, [&](const kui::Color c) { picked = c; }, { .alpha = false, .hex = false, .width = 60.f })));
    // The square is every saturation across and every brightness down, of red while nothing says another hue: its top-right is red.
    Click({ 58.f, 1.f });
    EXPECT_GT(picked.r, 0.9f);
    EXPECT_LT(picked.g, 0.1f);
    EXPECT_LT(picked.b, 0.1f);
    // The bar of hues is under it: a third of the way along is green.
    Click({ 20.f, 53.f });
    EXPECT_GT(picked.g, 0.9f);
    EXPECT_LT(picked.r, 0.15f);
}

TEST_F(WidgetTest, APlotDrawsItsValuesAndATableLinesItsCellsUp) {
    Show(kui::Plot({ 0.f, 1.f, 0.f, 1.f }, kui::PlotOptions {}.SetKind(kui::PlotKind::eHistogram).SetRange(0.f, 1.f).SetSize({ 64.f, 64.f }).SetColor(Red)));
    EXPECT_TRUE(IsRed(scene->At(24, 30))) << "the second bar, as tall as the plot";
    EXPECT_FALSE(IsRed(scene->At(10, 30))) << "the first has no height";

    const auto dot = [](const kui::Color color) { return kui::SizedBox(6.f, 6.f, kui::DecoratedBox({ .color = color }, {})); };
    Show(kui::Table({ { "a" }, { "b" } }, { { dot(Red), dot(Green) }, { dot(Green), dot(Red) } },
                    kui::TableOptions {}.SetHeader(false).SetStriped(false).SetBorders(false).SetRowHeight(20.f)));
    // Two columns sharing the width, cells ten in from their column's left and in the middle of their row.
    EXPECT_TRUE(IsRed(scene->At(13, 10)));
    EXPECT_TRUE(IsGreen(scene->At(45, 10)));
    EXPECT_TRUE(IsGreen(scene->At(13, 30)));
    EXPECT_TRUE(IsRed(scene->At(45, 30)));
}

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
            }, kui::DockOptions {}.SetGap(4.f).SetStripeGap(0.f).OnChanged([this] { ++changes; }).OnClosed([this](const std::string& id) { closed = id; })));
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

namespace {
    // How big the pictures are: KUI_DOCK_SHOWCASE_SIZE, as "800x500", says otherwise — a small one shows what gives when there is no room.
    int DockShowW = 960, DockShowH = 600;

    /** @brief Clears to a colour that is nothing of the interface's: where the scene shows through, it is plain to see. */
    class SkyPass final : public kor::RenderPass {
    public:
        SkyPass() : RenderPass("Sky") {}
        void Setup(kor::PassBuilder& b) override { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); }
        void Initialize(const kor::PassResources& r) override { _screen = r.ImageNamed(kor::FrameGraph::Screen); }
        void Record(kor::CommandBuffer& cb) const override { cb.ClearColorImage(_screen, glm::vec4(0.10f, 0.32f, 0.55f, 1.f)); }
    private:
        kor::ResourceRef<const kor::Image> _screen;
    };

    class DockShowScene final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(DockShowW * DockShowH * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            Graph().Add<SkyPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
        kor::Resource<kor::Buffer> readback;
    };
}

// A dock space with a panel in every area — two parts down each side, the two parts of the bottom, and the
// middle — drawn to pictures, to be looked at: set KUI_DOCK_SHOWCASE to where they go (a path, less ".png").
TEST(DockShowcase, Renders) {
    const char* out = std::getenv("KUI_DOCK_SHOWCASE");
    if (!s_app || !out) GTEST_SKIP() << "set KUI_DOCK_SHOWCASE to a path prefix to render the dock space";
    if (const char* size = std::getenv("KUI_DOCK_SHOWCASE_SIZE")) std::sscanf(size, "%dx%d", &DockShowW, &DockShowH);
    auto* scene = s_app->OpenOffscreen<DockShowScene>({ .title = "dock showcase", .extent = { static_cast<glm::u32>(DockShowW), static_cast<glm::u32>(DockShowH) }, .format = kor::Window::Format::eRGBA8_SRGB });
    ASSERT_NE(scene, nullptr);
    auto layout = std::make_shared<kui::DockLayout>();
    layout->Dock("project", kui::DockArea::eLeft, 0).Dock("structure", kui::DockArea::eLeft, 1)
           .Dock("inspector", kui::DockArea::eRight, 0).Dock("assets", kui::DockArea::eRight, 1)
           .Dock("log", kui::DockArea::eBottomLeft).Dock("problems", kui::DockArea::eBottomRight)
           .Dock("editor", kui::DockArea::eCenter);
    const auto panel = [](const std::string& name, const std::uint32_t colour) {
        return kui::Container({ .padding = kui::EdgeInsets::All(10.f), .decoration = { .color = kui::Color::Hex(colour) }, .alignment = kui::Alignment::TopLeft() },
                              kui::Text(name + " content"));
    };
    const bool noMiddle = std::getenv("KUI_DOCK_SHOWCASE_NO_MIDDLE") != nullptr;
    std::vector<kui::DockPanel> panels {
        { "project", "Project", panel("Project", 0x3B2F2F) },
        { "structure", "Structure", panel("Structure", 0x2F3B2F) },
        { "inspector", "Inspector", panel("Inspector", 0x2F2F3B) },
        { "assets", "Assets", panel("Assets", 0x3B3B2F) },
        { "log", "Log", panel("Log", 0x3B2F3B) },
        { "problems", "Problems", panel("Problems", 0x2F3B3B) },
    };
    if (!noMiddle) panels.push_back({ "editor", "Editor", panel("Editor", 0x444444) });
    scene->ui.SetRoot(kui::DockSpace(layout, panels));
    const auto shot = [&](const std::string& name) {
        for (int i = 0; i < 4; ++i) settle();
        const auto pixels = scene->readback->Read<glm::u8vec4>(DockShowW * DockShowH);
        stbi_write_png((std::string(out) + "-" + name + ".png").c_str(), DockShowW, DockShowH, 4, pixels.data(), DockShowW * 4);
    };
    const auto move = [&](const glm::vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); };
    const auto button = [&](const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); };

    move({ DockShowW * 0.5f, DockShowH * 0.5f });
    shot("1-all-open");

    // One part of each side folded away, and one end of the bottom.
    layout->Hide("structure");
    layout->Hide("assets");
    layout->Hide("problems");
    shot("2-some-folded");

    // Everything folded: only the stripes.
    layout->Hide("project");
    layout->Hide("inspector");
    layout->Hide("log");
    shot("3-all-folded");

    // All open again, one floated over the space, and a button in hand over the left stripe.
    for (const char* id : { "project", "structure", "inspector", "assets", "log", "problems" }) layout->Activate(id);
    layout->Float("assets", kui::Rect::XYWH(DockShowW * 0.34f, DockShowH * 0.2f, 300.f, 200.f));
    shot("4-one-floating");
    const float w = static_cast<float>(DockShowW), h = static_cast<float>(DockShowH);
    move({ w - 19.f, 19.f }); button(true); move({ w * 0.6f, h * 0.3f }); move({ 19.f, 60.f });
    shot("5-dragging-over-stripe");
    move({ w * 0.5f, h - 40.f });
    shot("6-dragging-over-bottom-margin");
    button(false);
    shot("7-dropped-in-bottom");

    s_app->Close(*scene);
    settle();
}

// The space is 240 by 160. The scene is in the middle; the inspector down the right, a quarter of the
// space wide (60); the log along the bottom, in its left part, 30% of the space tall (48). So there is a
// stripe of 38 down each side — the inspector's button at the top of the right one, the log's at the foot
// of the left one — and between them 164, of which a line of 4 and the inspector's 60 leave the scene 100.
// Every open panel has a title bar of 28 over it.

TEST_F(DockTest, PanelsGoWhereTheLayoutSaysAndFillTheirAreas) {
    EXPECT_FLOAT_EQ(view.x, 100.f);
    EXPECT_FLOAT_EQ(view.y, 80.f) << "108 over the bottom's line, less its title bar";
    EXPECT_FLOAT_EQ(inspector.x, 60.f);
    EXPECT_FLOAT_EQ(inspector.y, 80.f) << "the sides stop where the bottom starts";
    EXPECT_FLOAT_EQ(log.x, 164.f) << "the bottom runs from one stripe to the other";
    EXPECT_FLOAT_EQ(log.y, 20.f);
    Click({ 100.f, 150.f });
    EXPECT_EQ(taps, 1) << "a click in a panel reaches the panel";
    EXPECT_TRUE(scene->ui.WantsPointer());
    EXPECT_TRUE(layout->IsShown("log"));
}

TEST_F(DockTest, AButtonOpensItsPanelAndFoldsItAway) {
    Click({ 19.f, 141.f });   // the log's button, at the foot of the left stripe
    EXPECT_FALSE(layout->IsShown("log")) << "open, its button folds it away";
    EXPECT_TRUE(layout->IsOpen("log")) << "which is not closing it";
    EXPECT_FLOAT_EQ(view.y, 132.f) << "the scene has the height the bottom had";
    EXPECT_GT(changes, 0);
    Click({ 19.f, 141.f });
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FLOAT_EQ(log.y, 20.f) << "as tall as it was";
    Click({ 100.f, 150.f });
    EXPECT_EQ(taps, 1) << "the same panel";
}

TEST_F(DockTest, ATitleBarHidesAndCloses) {
    // The inspector's title bar is 60 wide, from 142: its last button closes it, the one before folds it away.
    Click({ 162.f, 14.f });
    EXPECT_FALSE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(view.x, 164.f) << "the scene has the width the side had";
    Click({ 221.f, 19.f });   // its button, at the top of the right stripe
    EXPECT_TRUE(layout->IsShown("inspector"));
    Click({ 186.f, 14.f });
    EXPECT_EQ(closed, "inspector");
    EXPECT_FALSE(layout->IsOpen("inspector"));
    EXPECT_FLOAT_EQ(view.x, 202.f) << "closed, its button is gone — and with it the right stripe";
    layout->Open("inspector");
    settle(); settle();
    EXPECT_TRUE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(view.x, 100.f);
}

// The sizes a dock space is drawn with are its style's: another title bar height moves what is under it.
TEST(DockStyle, TheSizesAreTheStylesToSay) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<DockScene>({ .title = "dock style", .extent = { 240, 160 } });
    ASSERT_NE(scene, nullptr);
    glm::vec2 size {};
    const auto show = [&](const kui::DockStyle style) {
        auto layout = std::make_shared<kui::DockLayout>();
        layout->Dock("a", kui::DockArea::eCenter);
        scene->ui.SetRoot(kui::DockSpace(layout, { { "a", "Alpha", kui::CustomPaint([&size](kui::Canvas&, const glm::vec2 s) { size = s; }) } },
                                         kui::DockOptions {}.SetStyle(style)));
        settle(); settle();
    };
    show({});
    EXPECT_FLOAT_EQ(size.y, 160.f - 28.f) << "under a title bar of 28, as it always was";
    kui::DockStyle tall;
    tall.titleBarHeight = 40.f;
    show(tall);
    EXPECT_FLOAT_EQ(size.y, 160.f - 40.f);
    s_app->Close(*scene);
    settle();
}

TEST_F(DockTest, OverTheLineBetweenTwoAreasThePointerSaysWhichWayItGoes) {
    Move({ 100.f, 60.f });
    EXPECT_EQ(scene->ui.Cursor(), kui::PointerCursor::eArrow);
    Move({ 140.f, 60.f });    // between the scene and the inspector: dragged sideways
    EXPECT_EQ(scene->ui.Cursor(), kui::PointerCursor::eResizeHorizontal);
    Move({ 100.f, 110.f });   // over the bottom: dragged up and down
    EXPECT_EQ(scene->ui.Cursor(), kui::PointerCursor::eResizeVertical);
    Move({ 100.f, 60.f });
    EXPECT_EQ(scene->ui.Cursor(), kui::PointerCursor::eArrow);
}

TEST_F(DockTest, TheLineBetweenAreasResizesThem) {
    Drag({ 140.f, 60.f }, { 120.f, 60.f });   // the line between the scene and the inspector, twenty to the left
    EXPECT_FLOAT_EQ(inspector.x, 80.f);
    EXPECT_FLOAT_EQ(view.x, 80.f);
    Drag({ 100.f, 110.f }, { 100.f, 90.f });  // the line over the bottom, twenty up
    EXPECT_FLOAT_EQ(log.y, 40.f);
}

TEST_F(DockTest, AButtonDraggedOntoAStripeMovesItsPanelThere) {
    Click({ 100.f, 150.f });
    ASSERT_EQ(taps, 1);
    Drag({ 19.f, 141.f }, { 221.f, 60.f });   // the log's button, to under the inspector's on the right stripe
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_TRUE(layout->IsShown("inspector")) << "a part of its own: the inspector is still shown over it";
    // No left stripe now: 202 between the edge and the right one. The right side's 160 is two parts of 78.
    EXPECT_FLOAT_EQ(view.x, 138.f);
    EXPECT_FLOAT_EQ(view.y, 132.f) << "nothing is left along the bottom";
    EXPECT_FLOAT_EQ(log.x, 60.f);
    EXPECT_FLOAT_EQ(log.y, 50.f);
    EXPECT_FLOAT_EQ(inspector.y, 50.f);
    Click({ 180.f, 140.f });
    EXPECT_EQ(taps, 2) << "the same panel, with what it counted before";

    // Onto the inspector's button itself: into its part, in front of it.
    Drag({ 221.f, 62.f }, { 221.f, 12.f });
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FALSE(layout->IsShown("inspector")) << "one part shows one panel";
    EXPECT_FLOAT_EQ(log.y, 132.f);
}

TEST_F(DockTest, DroppedOnADockedPanelItJoinsItOrGoesUnderIt) {
    // The inspector is shown from 142 to 202, down the 108 over the bottom's line. Over its lower part: under it.
    Drag({ 19.f, 141.f }, { 170.f, 95.f });
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_TRUE(layout->IsShown("inspector")) << "a part of its own, under the inspector's";
    EXPECT_FLOAT_EQ(log.y, 50.f);
    EXPECT_FLOAT_EQ(inspector.y, 50.f);

    // Over its upper part: into its group, in front of it. One part shows one panel; their buttons switch between them.
    Drag({ 221.f, 62.f }, { 170.f, 40.f });
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FALSE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(log.y, 132.f) << "the one part has the whole side";
    Click({ 221.f, 19.f });   // the inspector's button, the first of the two
    EXPECT_TRUE(layout->IsShown("inspector"));
    EXPECT_FALSE(layout->IsShown("log"));
}

TEST_F(DockTest, DroppedInTheMiddleItIsATabThere) {
    // The scene is the middle: 100 wide from 38, 108 tall. About its centre: the log joins it, in front of it.
    Drag({ 19.f, 141.f }, { 88.f, 54.f });
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FALSE(layout->IsShown("scene")) << "a tab behind it now";
    EXPECT_FLOAT_EQ(log.x, 138.f) << "its button left the left stripe, which is gone: the middle starts at the edge";
    EXPECT_FLOAT_EQ(log.y, 132.f) << "and nothing is left along the bottom";

    // The scene's title, next to the log's, brings it back to the front.
    Click({ 20.f, 14.f });
    EXPECT_TRUE(layout->IsShown("scene"));
    EXPECT_FALSE(layout->IsShown("log"));

    // And a tab dragged out by its title, to where nothing is a target, floats.
    Click({ 70.f, 14.f });
    ASSERT_TRUE(layout->IsShown("log"));
    Drag({ 70.f, 14.f }, { 40.f, 100.f });
    EXPECT_TRUE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("scene"));
}

// A side has levels, and its margin a band for each and one more: where a panel is let go says which level it joins.
TEST(DockLevels, ASidesMarginHasABandForEachLevelAndOneMore) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<DockScene>({ .title = "dock levels", .extent = { 480, 320 } });
    ASSERT_NE(scene, nullptr);
    auto layout = std::make_shared<kui::DockLayout>();
    layout->Dock("a", kui::DockArea::eLeft).Float("b", kui::Rect::XYWH(200.f, 100.f, 200.f, 120.f));
    scene->ui.SetRoot(kui::DockSpace(layout, { { "a", "Alpha", kui::SizedBox(10.f, 10.f) }, { "b", "Beta", kui::SizedBox(10.f, 10.f) } },
                                     kui::DockOptions {}.SetStripeGap(0.f)));
    settle(); settle();
    layout->Hide("a");   // folded away: the left side's one level shows nothing, and its margin is free to drop on
    settle(); settle();
    const auto move = [&](const glm::vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); };
    const auto button = [&](const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); };
    const auto drag = [&](const glm::vec2 from, const glm::vec2 to) {
        move(from); button(true);
        move((from + to) * 0.5f); move(to); move(to);
        button(false); settle(); settle();
    };
    const auto levels = [&] {
        const std::string saved = layout->Save();
        int count = 0;
        for (std::size_t at = saved.find("group left"); at != std::string::npos; at = saved.find("group left", at + 1)) ++count;
        return count;
    };
    ASSERT_EQ(levels(), 1);

    // The margin is 15% of the 442 between the stripe and the right edge, from 38. One level: two bands, of 160 each.
    drag({ 215.f, 114.f }, { 60.f, 250.f });   // Beta, by its title, into the lower band: a level of its own
    EXPECT_FALSE(layout->IsFloating("b"));
    EXPECT_TRUE(layout->IsShown("b"));
    EXPECT_EQ(levels(), 2) << layout->Save();
    EXPECT_FALSE(layout->IsShown("a")) << "the level over it is as it was: folded away";

    // Two levels now, Alpha's and Beta's: three bands. Beta's button — the second, under the line — into the first band: Alpha's level.
    drag({ 19.f, 62.f }, { 60.f, 40.f });
    EXPECT_EQ(levels(), 1) << layout->Save();
    EXPECT_TRUE(layout->IsShown("b")) << "in front, in the level it joined";

    s_app->Close(*scene);
    settle();
}

TEST_F(DockTest, DroppedInAMarginItDocksThereAndAnywhereElseItFloats) {
    EXPECT_FALSE(layout->IsFloating("log"));
    Drag({ 19.f, 141.f }, { 120.f, 70.f });   // the log's button, into the middle of the space
    EXPECT_TRUE(layout->IsFloating("log"));
    EXPECT_FLOAT_EQ(view.x, 138.f) << "its button left the left stripe, which is gone";

    const std::string saved = layout->Save();
    auto other = std::make_shared<kui::DockLayout>();
    ASSERT_TRUE(other->Load(saved));
    EXPECT_EQ(other->Save(), saved);
    EXPECT_TRUE(other->IsFloating("log"));
    EXPECT_FALSE(other->Load("not a layout"));

    // By its title, into the bottom margin of the space, in its right half: the bottom's right part.
    Drag({ 122.f, 70.f }, { 200.f, 152.f });
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FLOAT_EQ(log.x, 202.f) << "the only part of the bottom that is open has all of it";

    // And into the left margin: down the left side.
    Drag({ 221.f, 141.f }, { 40.f, 30.f });   // its button, at the foot of the right stripe now — to nowhere (off the margins, and off the middle of the middle): it floats
    EXPECT_TRUE(layout->IsFloating("log"));
    layout->Dock("log", kui::DockArea::eLeft);
    settle(); settle();
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_FLOAT_EQ(log.y, 132.f) << "the whole height of the left side";
}

TEST_F(DockTest, ASideCanBeInSeveralParts) {
    layout->Dock("log", kui::DockArea::eRight, 1);   // a second part of the right side, under the inspector's
    settle(); settle();
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_TRUE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(inspector.y, 50.f) << "half of the side's 160, less the line between them and its title bar";
    EXPECT_FLOAT_EQ(log.y, 50.f);
    EXPECT_FLOAT_EQ(log.x, 60.f);

    Drag({ 170.f, 80.f }, { 170.f, 100.f });  // the line between the two parts, twenty down
    EXPECT_FLOAT_EQ(inspector.y, 70.f);
    EXPECT_FLOAT_EQ(log.y, 30.f);

    auto other = std::make_shared<kui::DockLayout>();
    ASSERT_TRUE(other->Load(layout->Save()));
    EXPECT_EQ(other->Save(), layout->Save());

    layout->Hide("inspector");
    settle(); settle();
    EXPECT_FALSE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(log.y, 132.f) << "the part that is open has the whole side";
}

TEST_F(DockTest, APanelFloatsAgainAtTheSizeItFloatedAtBefore) {
    layout->Float("inspector", kui::Rect::XYWH(10.f, 10.f, 150.f, 100.f));
    settle(); settle();
    ASSERT_TRUE(layout->IsFloating("inspector"));
    const glm::vec2 floated = inspector;

    layout->Dock("inspector", kui::DockArea::eRight);
    settle(); settle();
    ASSERT_FALSE(layout->IsFloating("inspector"));
    EXPECT_NE(inspector, floated) << "docked, it is as big as its area";

    Drag({ 221.f, 19.f }, { 120.f, 70.f });   // its button, out into the middle of the space
    ASSERT_TRUE(layout->IsFloating("inspector"));
    EXPECT_EQ(inspector, floated) << "the size it floated at, not the size it was docked at";
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
