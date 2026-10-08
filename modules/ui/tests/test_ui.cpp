// koral-ui: the canvas's shapes drawn by a real GPU into an offscreen scene and read back pixel by
// pixel, plus the CPU-side pieces (paths, tessellation, text layout) on their own.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <numbers>
#include <thread>

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
        void Record(kor::CommandBuffer& cb) const override { cb.ClearColorImage(_screen, kor::Vec4(0.f, 0.f, 0.f, 1.f)); }
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
        [[nodiscard]] kor::U8Vec4 At(const int x, const int y) const {
            const auto pixels = readback->Read<kor::U8Vec4>(Size * Size);
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

    bool IsRed(const kor::U8Vec4 p) { return p.x > 240 && p.y < 15 && p.z < 15; }
    bool IsGreen(const kor::U8Vec4 p) { return p.y > 240 && p.x < 15 && p.z < 15; }
    bool IsBlue(const kor::U8Vec4 p) { return p.z > 240 && p.x < 15 && p.y < 15; }
    bool IsBlack(const kor::U8Vec4 p) { return p.x < 5 && p.y < 5 && p.z < 5; }
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
    EXPECT_GT(edge.y, 40);
    EXPECT_LT(edge.y, 215) << "anti-aliased";
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

TEST_F(Gpu, HolesStayHolesWhereTheTriangulatorWouldFillThem) {
    // Clipper's triangulation says it succeeded on this one and fills its nine holes over: their sides line
    // up with where the outline's rounded corners begin. Caught by its area, and drawn by the sweep instead.
    Draw([](kui::Canvas& c) { kui::MaterialIcon("grid_on")->Draw(c, kui::Rect::XYWH(0, 0, 48, 48), Red); });
    EXPECT_TRUE(IsBlack(scene->At(12, 12))) << "a hole";
    EXPECT_TRUE(IsBlack(scene->At(36, 36))) << "another";
    EXPECT_TRUE(IsRed(scene->At(6, 18))) << "the frame";
    EXPECT_TRUE(IsRed(scene->At(18, 12))) << "a bar between holes";
}

TEST_F(Gpu, AStrokedPathIsOneShapeEvenWhereItsPiecesOverlap) {
    Draw([](kui::Canvas& c) {
        kui::Path path;
        path.MoveTo({ 8, 32 }).LineTo({ 56, 32 }).LineTo({ 32, 8 }).LineTo({ 32, 56 });
        c.DrawPath(path, kui::Paint::Stroked(Red.WithAlpha(0.5f), 6.f).SetStroke({ .width = 6.f, .color = Red.WithAlpha(0.5f), .join = kui::StrokeJoin::eRound }));
    });
    const auto crossing = scene->At(32, 32);   // two segments cross here
    const auto single = scene->At(16, 32);
    EXPECT_NEAR(crossing.x, single.x, 3) << "a translucent stroke does not darken where it crosses itself";
    EXPECT_GT(single.x, 100);
    EXPECT_LT(single.x, 160);
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
    EXPECT_GT(left.x, 240);
    EXPECT_GT(right.z, 240);
    EXPECT_NEAR(mid.x, 127, 6);
    EXPECT_NEAR(mid.z, 127, 6);
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
        for (int x = 0; x < Size; ++x) lit += scene->At(x, y).x > 128;
    EXPECT_GT(lit, 60) << "a capital I at 60 units is a solid bar";
    EXPECT_LT(lit, 1200);
}

TEST_F(Gpu, AnElementShaderFillsItsRectangleWithItsParameters) {
    const auto shader = kui::ElementShader::Load(std::filesystem::path(KUI_TEST_SHADERS) / "testElement.frag.glsl");
    ASSERT_TRUE(shader->Valid());
    struct Params { kor::Vec4 left, right; };
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
    struct Params { kor::Vec4 left, right; };
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
    EXPECT_GT(inside.x, 240);
    EXPECT_GT(edge.x, 80);
    EXPECT_LT(edge.x, 180);
    EXPECT_LT(far.x, 10);
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
        const kor::Vec2 a = contours[0].points[i], b = contours[0].points[(i + 1) % contours[0].points.size()];
        EXPECT_NEAR(kor::Length(a), 100.f, 0.05f);
        EXPECT_GT(kor::Length((a + b) * 0.5f), 100.f - 0.2f) << "each chord stays near the arc";
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
    const kor::Vec2 caret = one.CaretPosition(6);
    EXPECT_GT(caret.x, 0.f);
    EXPECT_EQ(one.IndexAt(caret + kor::Vec2(0.5f, 2.f)), 6u);
}

TEST(Transform, ComposesLikeMatrices) {
    const auto t = kui::Transform::Translation({ 10, 0 }) * kui::Transform::Rotation(std::numbers::pi_v<float> * 0.5f);
    const kor::Vec2 p = t.Apply({ 1, 0 });
    EXPECT_NEAR(p.x, 10.f, 1e-5f);
    EXPECT_NEAR(p.y, 1.f, 1e-5f);
    const kor::Vec2 back = t.Inverse().Apply(p);
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
        [[nodiscard]] kor::U8Vec4 At(const int x, const int y) const {
            return readback->Read<kor::U8Vec4>(Size * Size)[static_cast<std::size_t>(y) * Size + x];
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
        void Click(const kor::Vec2 p) {
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
    kui::Widget Probe(kor::Vec2& size, const kui::Color color = Red) {
        return kui::CustomPaint([&size, color](kui::Canvas& c, const kor::Vec2 s) {
            size = s;
            c.DrawRect(kui::Rect::FromSize(s), kui::Paint::Fill(color));
        });
    }
}

TEST_F(WidgetTest, RowsShareWhatIsLeftAmongTheirExpandedChildren) {
    kor::Vec2 a {}, b {};
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
        rows.push_back(kui::CustomPaint([&painted, i](kui::Canvas& c, const kor::Vec2 s) {
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
    kor::Vec2 before = {};
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

// A DragValue double-clicked is a text box for typing the value — a single click is not — and Enter or
// clicking away sets it, kept to the range; Escape, or what is no number, leaves it as it was.
TEST_F(WidgetTest, ADragValueClickedIsTypedIn) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    float value = 1.f;
    auto& input = scene->SceneInput();
    const auto press = [&](const kor::Key key) { input.FeedKey(key, true); settle(); input.FeedKey(key, false); settle(); };
    const auto show = [&] {
        Show(kui::Align(kui::Alignment::TopLeft(),
                        kui::DragValue(value, [&](const float v) { value = v; }, kui::DragValueOptions {}.SetRange(0.f, 10.f).SetWidth(60.f))));
    };
    const auto typeIn = [&](const std::u32string& text) {
        Click({ 30.f, 15.f });
        Click({ 30.f, 15.f });
        settle();
        ASSERT_TRUE(input.InterfaceWantsKeyboard()) << "a double click turns it into a text box that has the keyboard";
        input.FeedText(text);
        settle();
    };

    show();
    Click({ 30.f, 15.f });
    settle();
    EXPECT_FALSE(input.InterfaceWantsKeyboard()) << "one click is not enough";
    // Clicked again long after: two single clicks, not a double one.
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    Click({ 30.f, 15.f });
    settle();
    EXPECT_FALSE(input.InterfaceWantsKeyboard()) << "nor are two far apart";

    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    typeIn(U"7.25");
    press(kor::Key::eEnter);
    EXPECT_FLOAT_EQ(value, 7.25f) << "what was typed replaces what it held, all of it selected";
    EXPECT_FALSE(input.InterfaceWantsKeyboard()) << "and it is a drag again";

    show();
    typeIn(U"50");
    press(kor::Key::eEnter);
    EXPECT_FLOAT_EQ(value, 10.f) << "kept to its range";

    show();
    typeIn(U"3");
    press(kor::Key::eEsc);
    EXPECT_FLOAT_EQ(value, 10.f) << "Escape leaves it as it was";

    show();
    typeIn(U"not a number");
    press(kor::Key::eEnter);
    EXPECT_FLOAT_EQ(value, 10.f) << "and so does what is no number";

    show();
    typeIn(U" 4,5 ");
    Click({ 30.f, 55.f });
    EXPECT_FLOAT_EQ(value, 4.5f) << "clicked away from, it is set: spaces round it and a decimal comma are fine";
    EXPECT_FALSE(input.InterfaceWantsKeyboard());
}

// Dragged, it is a drag and no text box; and one that is not typeable never is one.
TEST_F(WidgetTest, ADragValueDraggedOrNotTypeableIsNoTextBox) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    float value = 1.f;
    auto& input = scene->SceneInput();
    Show(kui::Align(kui::Alignment::TopLeft(),
                    kui::DragValue(value, [&](const float v) { value = v; }, kui::DragValueOptions {}.SetSpeed(0.1f).SetWidth(60.f))));
    input.FeedMousePosition({ 30.f, 15.f });
    settle();
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    settle();
    input.FeedMousePosition({ 50.f, 15.f });
    input.FeedMouseDelta({ 20.f, 0.f });
    settle();
    input.FeedMouseButton(kor::MouseButton::eLeft, false);
    settle(); settle();
    EXPECT_GT(value, 1.f) << "dragged";
    EXPECT_FALSE(input.InterfaceWantsKeyboard()) << "a drag is no click";

    Show(kui::Align(kui::Alignment::TopLeft(),
                    kui::DragValue(value, [&](const float v) { value = v; }, kui::DragValueOptions {}.SetWidth(60.f).SetTypeable(false))));
    Click({ 30.f, 15.f });
    Click({ 30.f, 15.f });
    settle();
    EXPECT_FALSE(input.InterfaceWantsKeyboard()) << "not typeable: a double click does nothing";
}

TEST(DragValueOptions, WrappingBringsAValueRoundIntoItsRangeAndClampingKeepsItAtTheEnds) {
    const auto angle = kui::DragValueOptions {}.SetRange(0.f, 360.f).SetWrap(true);
    EXPECT_FLOAT_EQ(angle.Keep(370.f), 10.f);
    EXPECT_FLOAT_EQ(angle.Keep(-10.f), 350.f);
    EXPECT_FLOAT_EQ(angle.Keep(360.f), 0.f) << "the two ends are one place";
    EXPECT_FLOAT_EQ(angle.Keep(725.f), 5.f) << "as many times round as it takes";
    EXPECT_FLOAT_EQ(angle.Keep(90.f), 90.f);
    EXPECT_FLOAT_EQ(kui::DragValueOptions {}.SetRange(-1.f, 1.f).SetWrap(true).Keep(1.5f), -0.5f);
    EXPECT_FLOAT_EQ(kui::DragValueOptions {}.SetRange(0.f, 360.f).Keep(370.f), 360.f) << "not wrapping, it stops at the end";
    EXPECT_FLOAT_EQ(kui::DragValueOptions {}.SetRange(0.f, kui::Infinity).SetWrap(true).Keep(-5.f), 0.f) << "an open range does not wrap";
}

TEST_F(WidgetTest, ADragValueThatWrapsComesBackInAtTheOtherEnd) {
    float value = 350.f;
    auto& input = scene->SceneInput();
    Show(kui::Align(kui::Alignment::TopLeft(), kui::DragValue(value, [&](const float v) { value = v; },
                    kui::DragValueOptions {}.SetSpeed(1.f).SetRange(0.f, 360.f).SetWrap(true).SetWidth(60.f))));
    input.FeedMousePosition({ 30.f, 15.f });
    settle();
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    settle();
    input.FeedMousePosition({ 50.f, 15.f });
    input.FeedMouseDelta({ 20.f, 0.f });
    settle();
    input.FeedMouseButton(kor::MouseButton::eLeft, false);
    settle(); settle();
    EXPECT_NEAR(value, 10.f, 0.01f) << "350 dragged on by 20 is 10, not 360";
}

// Tab goes through drag values as it does text fields: each tabbed to is typed in, and tabbed out of, set.
TEST_F(WidgetTest, TabGoesThroughDragValuesTypingInEach) {
    if (!kui::Font::Default()) GTEST_SKIP() << "no default font";
    std::string name;
    float first = 1.f, second = 2.f;
    auto& input = scene->SceneInput();
    const auto press = [&](const kor::Key key) { input.FeedKey(key, true); settle(); input.FeedKey(key, false); settle(); };
    const auto show = [&] {
        Show(kui::Align(kui::Alignment::TopLeft(), kui::Column({
            kui::TextField({ .onChanged = [&](const std::string& t) { name = t; }, .width = 60.f }),
            kui::DragValue(first, [&](const float v) { first = v; }, kui::DragValueOptions {}.SetWidth(60.f)),
            kui::DragValue(second, [&](const float v) { second = v; }, kui::DragValueOptions {}.SetWidth(60.f)),
            kui::DragValue(0.f, {}, kui::DragValueOptions {}.SetWidth(60.f).SetTypeable(false)),
        })));
    };
    show();
    Click({ 20.f, 10.f });
    input.FeedText(U"a");
    settle();
    ASSERT_EQ(name, "a");

    press(kor::Key::eTab);
    settle();
    input.FeedText(U"5");
    settle();
    press(kor::Key::eTab);
    settle();
    EXPECT_FLOAT_EQ(first, 5.f) << "tabbed to, the first drag was typed in; tabbed out of, set";
    input.FeedText(U"6");
    settle();
    press(kor::Key::eEnter);
    EXPECT_FLOAT_EQ(second, 6.f) << "and the keyboard went on to the second";

    // From the second, past the one that is not typeable, round to the text field.
    show();
    Click({ 20.f, 10.f });
    press(kor::Key::eTab);
    press(kor::Key::eTab);
    press(kor::Key::eTab);
    settle();
    input.FeedText(U"b");
    settle();
    EXPECT_EQ(name, "ab") << "a drag that is not typeable is not in the order";
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
    kor::Vec2 size {};
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
        const kor::Vec2 second = context.measure(1, kui::BoxConstraints::Loose({ 10.f, 10.f }).Tighten(10.f, 10.f));
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
        kor::Vec2 size {};
        kui::TextStyle style { .size = 10.f, .color = kui::colors::White };
        const auto text = [&](const int lines) {
            return kui::Align(kui::Alignment::TopLeft(), kui::SizeObserver([&](const kor::Vec2 s, kor::Vec2) { size = s; },
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
        for (int y = 0; y < 30; ++y) for (int x = 0; x < 60; ++x) least = std::min<int>(least, scene->At(x, y).x);
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

            auto shapes = kui::CustomPaint([](kui::Canvas& c, const kor::Vec2 size) {
                c.DrawRRect({ kui::Rect::FromSize(size), 10.f }, kui::Paint{}.SetGradient(
                    kui::Gradient::Linear({ 0, 0 }, { size.x, size.y }, kui::Color::Hex(0x2B2F6B), kui::Color::Hex(0x6B2B5E))));
                c.DrawCircle({ 50, 60 }, 30.f, kui::Paint::Fill(kui::Color::Hex(0xFFB74D)).SetStroke(3.f, kui::colors::White));
                c.DrawArc({ 130, 60 }, 30.f, -1.57f, 4.2f, false, kui::Paint::Stroked(kui::Color::Hex(0x4DD0E1), 6.f)
                    .SetStroke({ .width = 6.f, .color = kui::Color::Hex(0x4DD0E1), .cap = kui::StrokeCap::eRound }));
                c.DrawTriangle({ 180, 90 }, { 240, 90 }, { 210, 30 }, kui::Paint::Fill(kui::Color::Hex(0x81C784)));
                kui::Path star;
                for (int i = 0; i < 10; ++i) {
                    const float a = -1.5708f + i * 3.14159f / 5.f, r = i % 2 ? 12.f : 30.f;
                    const kor::Vec2 p { 290 + r * std::cos(a), 62 + r * std::sin(a) };
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
    const auto pixels = scene->readback->Read<kor::U8Vec4>(ShowW * ShowH);
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
                    const kor::Vec2 q { 290 + r * std::cos(a), 62 + r * std::sin(a) };
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
                const kor::Vec2 p { x + 0.5f + 63.f - fx, y + 0.5f + 76.f - fy };
                float d = 1e9f;
                for (std::size_t i = 0; i + 1 < line.size(); ++i) {
                    const kor::Vec2 a = line[i], b = line[i + 1];
                    const float h = std::clamp(kor::Dot(p - a, b - a) / kor::Dot(b - a, b - a), 0.f, 1.f);
                    d = std::min(d, kor::Length(p - a - (b - a) * h));
                }
                if (d < 0.9f) EXPECT_GT(scene->At(x, y).x, 190) << "pixel " << x << "," << y << " at offset " << fx << "," << fy << " d=" << d;
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
    kor::Vec2 under {};
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
    const auto accent = [&](const int x) { const auto p = scene->At(x, 16); return p.x > 200 && p.y > 90 && p.y < 170 && p.z < 120; };
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
    EXPECT_GT(mark.x, 180) << "an error's mark, in red";
    EXPECT_LT(mark.z, 120);
}

TEST_F(WidgetTest, AScrollThumbHasAStripOfItsOwnBesideTheContent) {
    kor::Vec2 size {};
    const auto content = [&](const float height) {
        return kui::ScrollView(kui::CustomPaint([&size](kui::Canvas& c, const kor::Vec2 s) {
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
    std::vector<kor::Vec2> told;
    const auto root = [&](const float height) {
        return kui::Column({ kui::SizedBox(64.f, height), kui::Expanded(kui::SizeObserver([&](const kor::Vec2 size, kor::Vec2) { told.push_back(size); })) },
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
            .OnDrop([&](const kui::DragData& d, kor::Vec2) { dropped = *d.As<std::string>(); })),
    }));
    auto& input = scene->SceneInput();
    const auto frame = [&](const kor::Vec2 p) { input.FeedMousePosition(p); settle(); };

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
        /** @brief The pixel at (@p x, @p y) of what was last drawn. The picture is read back from the first time this is asked. */
        [[nodiscard]] kor::U8Vec4 At(const int x, const int y) {
            const auto extent = SceneWindow().Extent();
            if (!_readback.Valid()) {
                _readback = kor::Buffer::RawBuilder{}.SetRawSize(extent.x * extent.y * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                    .SetType(kor::Buffer::Type::eReadback).Build();
                Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(_readback));
                settle();
                settle();
            }
            return _readback->Read<kor::U8Vec4>(extent.x * extent.y)[static_cast<std::size_t>(y) * extent.x + static_cast<std::size_t>(x)];
        }
        kui::Ui ui;
    private:
        kor::Resource<kor::Buffer> _readback;
    };

    /** @brief A panel that fills its space, counts its taps in its own state, and says how big it is. */
    struct Tapped final : kui::StatefulWidget {
        int n = 0;
        int* out;
        kor::Vec2* size;
        Tapped(int* o, kor::Vec2* s) : out(o), size(s) {}
        kui::Widget Build() override {
            return kui::CustomPaint([s = size](kui::Canvas&, const kor::Vec2 given) { if (s) *s = given; })
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
        void Move(const kor::Vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); }
        void Button(const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); }
        void Click(const kor::Vec2 p) { Move(p); Button(true); Button(false); }
        void Drag(const kor::Vec2 from, const kor::Vec2 to) {
            Move(from); Button(true);
            Move(from + (to - from) * 0.5f); Move(to); Move(to);
            Button(false); settle();
        }
        DockScene* scene = nullptr;
        std::shared_ptr<kui::DockLayout> layout;
        kor::Vec2 view {}, inspector {}, log {};
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
        void Record(kor::CommandBuffer& cb) const override { cb.ClearColorImage(_screen, kor::Vec4(0.10f, 0.32f, 0.55f, 1.f)); }
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
    auto* scene = s_app->OpenOffscreen<DockShowScene>({ .title = "dock showcase", .extent = { static_cast<kor::u32>(DockShowW), static_cast<kor::u32>(DockShowH) }, .format = kor::Window::Format::eRGBA8_SRGB });
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
    // Each button an icon of Material's — but the assets', which shows its title's first letter.
    std::vector<kui::DockPanel> panels {
        { .id = "project", .title = "Project", .content = panel("Project", 0x3B2F2F), .icon = kui::MaterialIcon("folder") },
        { .id = "structure", .title = "Structure", .content = panel("Structure", 0x2F3B2F), .icon = kui::MaterialIcon("account_tree") },
        { .id = "inspector", .title = "Inspector", .content = panel("Inspector", 0x2F2F3B), .icon = kui::MaterialIcon("tune") },
        { .id = "assets", .title = "Assets", .content = panel("Assets", 0x3B3B2F) },
        { .id = "log", .title = "Log", .content = panel("Log", 0x3B2F3B), .icon = kui::MaterialIcon("terminal") },
        { .id = "problems", .title = "Problems", .content = panel("Problems", 0x2F3B3B), .icon = kui::MaterialIcon("warning", kui::IconStyle::eOutlined) },
    };
    if (!noMiddle) {
        // The middle's panel with a toolbar of its own in its title bar, between its title and its buttons.
        kui::DockPanel editor { "editor", "Editor", panel("Editor", 0x444444) };
        const auto tool = [](const char* icon) {
            return kui::Button(kui::Icon(icon), [] {}, { .style = kui::ButtonStyle::eSecondary, .padding = kui::EdgeInsets::All(2.f) });
        };
        editor.titleBar = kui::Row({ tool("PlayArrow"), tool("Pause"), tool("Stop") },
                                   { .mainAxisAlignment = kui::MainAxisAlignment::eCenter, .crossAxisAlignment = kui::CrossAxisAlignment::eCenter, .gap = 4.f })
                              .Background(kui::Color::Hex(0x1E1E24)).Height(32.f);
        panels.push_back(std::move(editor));
    }
    scene->ui.SetRoot(kui::DockSpace(layout, panels));
    const auto shot = [&](const std::string& name) {
        for (int i = 0; i < 4; ++i) settle();
        const auto pixels = scene->readback->Read<kor::U8Vec4>(DockShowW * DockShowH);
        stbi_write_png((std::string(out) + "-" + name + ".png").c_str(), DockShowW, DockShowH, 4, pixels.data(), DockShowW * 4);
    };
    const auto move = [&](const kor::Vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); };
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
// Every open panel has a title bar of 28 over it. Half the line's 4 is kept from the top and the foot of
// the space, and from a side whose stripe is gone.

TEST_F(DockTest, PanelsGoWhereTheLayoutSaysAndFillTheirAreas) {
    EXPECT_FLOAT_EQ(view.x, 100.f);
    EXPECT_FLOAT_EQ(view.y, 76.f) << "104 over the bottom's line, less its title bar";
    EXPECT_FLOAT_EQ(inspector.x, 60.f);
    EXPECT_FLOAT_EQ(inspector.y, 76.f) << "the sides stop where the bottom starts";
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
    EXPECT_FLOAT_EQ(view.y, 128.f) << "the scene has the height the bottom had";
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
    EXPECT_FLOAT_EQ(view.x, 200.f) << "closed, its button is gone — and with it the right stripe";
    layout->Open("inspector");
    settle(); settle();
    EXPECT_TRUE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(view.x, 100.f);
}

// A panel's own widget in its title bar: between its title and its buttons, the bar as tall as it wants. The space is
// 240 by 160 with the one panel in the middle: an island from 2 to 238 across and 2 to 158 down, its one button
// (it closes it) ending at 214 — so the widget ends at 208.
TEST(DockTitleBar, APanelsOwnTitleBarSitsBetweenItsTitleAndItsButtons) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<DockScene>({ .title = "dock title bar", .extent = { 240, 160 } });
    ASSERT_NE(scene, nullptr);
    auto layout = std::make_shared<kui::DockLayout>();
    layout->Dock("scene");
    kor::Vec2 view {}, tool {};
    int toolTaps = 0;
    kui::DockPanel panel { "scene", "Scene", kui::Make<Tapped>(nullptr, &view) };
    // A tool button 20 by 36 at its end; the rest of it takes no press.
    panel.titleBar = kui::Row({ kui::SizedBox(20.f, 36.f, kui::Make<Tapped>(&toolTaps, &tool)) }, { .mainAxisAlignment = kui::MainAxisAlignment::eEnd });
    scene->ui.SetRoot(kui::DockSpace(layout, { panel }, kui::DockOptions {}.SetGap(4.f).SetStripeGap(0.f)));
    settle(); settle();
    const auto move = [&](const kor::Vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); };
    const auto button = [&](const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); };
    const auto drag = [&](const kor::Vec2 from, const kor::Vec2 to) { move(from); button(true); move((from + to) * 0.5f); move(to); move(to); button(false); settle(); };

    EXPECT_EQ(tool, kor::Vec2(20.f, 36.f));
    EXPECT_FLOAT_EQ(view.y, 120.f) << "the bar grew from 28 to 36 for it";
    EXPECT_FLOAT_EQ(view.x, 236.f);

    move({ 198.f, 20.f }); button(true); button(false);
    EXPECT_EQ(toolTaps, 1) << "a press on what takes one in it is its own";
    drag({ 198.f, 20.f }, { 120.f, 110.f });
    EXPECT_FALSE(layout->IsFloating("scene")) << "and does not pick the panel up";
    drag({ 150.f, 20.f }, { 120.f, 110.f });
    EXPECT_TRUE(layout->IsFloating("scene")) << "the rest of it picks the panel up, as the bar does";

    s_app->Close(*scene);
    settle();
}

// A dock keeps a panel no narrower than its content's least width — here three DragValues sharing a row, each needing
// its label and its widest value — and the DragValues share whatever width more there is. The space is 800 by 160.
TEST(DockMinWidth, APanelIsNoNarrowerThanItsContentNeedsAndItsDragsShareTheRest) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<DockScene>({ .title = "dock min width", .extent = { 800, 160 } });
    ASSERT_NE(scene, nullptr);
    auto layout = std::make_shared<kui::DockLayout>();
    layout->Dock("scene").Dock("transform", kui::DockSide::eRight, "scene", 0.05f);   // asked for 40 wide: far too little
    kor::Vec2 view {}, x {}, y {}, z {};
    const auto drag = [](kor::Vec2& size, std::string label) {
        return kui::Expanded(kui::SizeObserver([&size](const kor::Vec2 s, kor::Vec2) { size = s; },
                                               kui::DragValue(0.f, [](float) {}, kui::DragValueOptions {}.SetLabel(std::move(label)))));
    };
    scene->ui.SetRoot(kui::DockSpace(layout, {
        { "scene", "Scene", kui::Make<Tapped>(nullptr, &view) },
        { "transform", "Transform", kui::Padding(kui::EdgeInsets::All(12.f), kui::Row({ drag(x, "X"), drag(y, "Y"), drag(z, "Z") }, { .gap = 6.f })) },
    }, kui::DockOptions {}.SetGap(4.f).SetStripeGap(0.f)));
    const auto move = [&](const kor::Vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); };
    const auto button = [&](const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); };
    settle(); settle();

    // What each needs: its label, a gap, and its widest value — open-ended, a sign and four whole digits — and the room round them.
    const kui::TextStyle style = kui::Theme::Current().textStyle;
    const float number = kui::Paragraph("-8888.00", style).Size().x;
    const float label = kui::Paragraph("X", style).Size().x;
    for (const kor::Vec2 size : { x, y, z }) EXPECT_GE(size.x, number + label) << "every drag shows its widest value whole";
    const float least = x.x + y.x + z.x + 2.f * 6.f + 2.f * 12.f;
    // The space is 800: a stripe of 38 for the transform's button, half a gap at the left, the gap between the two.
    const float panel = 800.f - 38.f - 2.f - 4.f - view.x;
    EXPECT_NEAR(panel, least, 1.f) << "held at its content's least width, not the 12 it was asked for";
    EXPECT_NEAR(x.x, y.x, 1.f);

    // The line between the two dragged right, to make it narrower still: it stays.
    const float line = 2.f + view.x + 2.f;
    move({ line, 80.f }); button(true); move({ line + 30.f, 80.f }); move({ 790.f, 80.f }); button(false);
    EXPECT_NEAR(800.f - 38.f - 2.f - 4.f - view.x, least, 1.f) << "dragged, it is no narrower";

    // Made wider, the drags share the width: each a third of the row.
    move({ 2.f + view.x + 2.f, 80.f }); button(true); move({ 200.f, 80.f }); move({ 150.f, 80.f }); button(false);
    const float wider = 800.f - 38.f - 2.f - 4.f - view.x;
    EXPECT_GT(wider, least + 20.f);
    EXPECT_NEAR(x.x, (wider - 24.f - 12.f) / 3.f, 1.f) << "a third of the row each";

    s_app->Close(*scene);
    settle();
}

// The sizes a dock space is drawn with are its style's: another title bar height moves what is under it.
TEST(DockStyle, TheSizesAreTheStylesToSay) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<DockScene>({ .title = "dock style", .extent = { 240, 160 } });
    ASSERT_NE(scene, nullptr);
    kor::Vec2 size {};
    const auto show = [&](const kui::DockStyle style) {
        auto layout = std::make_shared<kui::DockLayout>();
        layout->Dock("a", kui::DockArea::eCenter);
        scene->ui.SetRoot(kui::DockSpace(layout, { { "a", "Alpha", kui::CustomPaint([&size](kui::Canvas&, const kor::Vec2 s) { size = s; }) } },
                                         kui::DockOptions {}.SetStyle(style)));
        settle(); settle();
    };
    show({});
    EXPECT_FLOAT_EQ(size.y, 160.f - 28.f - 6.f) << "under a title bar of 28, as it always was";
    kui::DockStyle tall;
    tall.titleBarHeight = 40.f;
    show(tall);
    EXPECT_FLOAT_EQ(size.y, 160.f - 40.f - 6.f);
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
    EXPECT_FLOAT_EQ(log.y, 38.f);
}

TEST_F(DockTest, AButtonDraggedOntoAStripeMovesItsPanelThere) {
    Click({ 100.f, 150.f });
    ASSERT_EQ(taps, 1);
    Drag({ 19.f, 141.f }, { 221.f, 60.f });   // the log's button, to under the inspector's on the right stripe
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_TRUE(layout->IsShown("inspector")) << "a part of its own: the inspector is still shown over it";
    // No left stripe now: 202 between the edge and the right one. The right side's 160 is two parts of 78.
    EXPECT_FLOAT_EQ(view.x, 136.f);
    EXPECT_FLOAT_EQ(view.y, 128.f) << "nothing is left along the bottom";
    EXPECT_FLOAT_EQ(log.x, 60.f);
    EXPECT_FLOAT_EQ(log.y, 48.f);
    EXPECT_FLOAT_EQ(inspector.y, 48.f);
    Click({ 180.f, 140.f });
    EXPECT_EQ(taps, 2) << "the same panel, with what it counted before";

    // Onto the inspector's button itself: into its part, in front of it.
    Drag({ 221.f, 62.f }, { 221.f, 12.f });
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FALSE(layout->IsShown("inspector")) << "one part shows one panel";
    EXPECT_FLOAT_EQ(log.y, 128.f);
}

TEST_F(DockTest, DroppedOnADockedPanelItJoinsItOrGoesUnderIt) {
    // The inspector is shown from 142 to 202, down the 108 over the bottom's line. Over its lower part: under it.
    Drag({ 19.f, 141.f }, { 170.f, 95.f });
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_TRUE(layout->IsShown("inspector")) << "a part of its own, under the inspector's";
    EXPECT_FLOAT_EQ(log.y, 48.f);
    EXPECT_FLOAT_EQ(inspector.y, 48.f);

    // Over its upper part: into its group, in front of it. One part shows one panel; their buttons switch between them.
    Drag({ 221.f, 62.f }, { 170.f, 40.f });
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_FALSE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(log.y, 128.f) << "the one part has the whole side";
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
    EXPECT_FLOAT_EQ(log.x, 136.f) << "its button left the left stripe, which is gone: the middle starts at the edge";
    EXPECT_FLOAT_EQ(log.y, 128.f) << "and nothing is left along the bottom";

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
    const auto move = [&](const kor::Vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); };
    const auto button = [&](const bool down) { scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, down); settle(); };
    const auto drag = [&](const kor::Vec2 from, const kor::Vec2 to) {
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
    EXPECT_FLOAT_EQ(view.x, 136.f) << "its button left the left stripe, which is gone";

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
    EXPECT_FLOAT_EQ(log.x, 200.f) << "the only part of the bottom that is open has all of it";

    // And into the left margin: down the left side.
    Drag({ 221.f, 141.f }, { 40.f, 30.f });   // its button, at the foot of the right stripe now — to nowhere (off the margins, and off the middle of the middle): it floats
    EXPECT_TRUE(layout->IsFloating("log"));
    layout->Dock("log", kui::DockArea::eLeft);
    settle(); settle();
    EXPECT_FALSE(layout->IsFloating("log"));
    EXPECT_FLOAT_EQ(log.y, 128.f) << "the whole height of the left side";
}

TEST_F(DockTest, ASideCanBeInSeveralParts) {
    layout->Dock("log", kui::DockArea::eRight, 1);   // a second part of the right side, under the inspector's
    settle(); settle();
    EXPECT_TRUE(layout->IsShown("log"));
    EXPECT_TRUE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(inspector.y, 48.f) << "half of the side's 160, less the line between them and its title bar";
    EXPECT_FLOAT_EQ(log.y, 48.f);
    EXPECT_FLOAT_EQ(log.x, 60.f);

    Drag({ 170.f, 80.f }, { 170.f, 100.f });  // the line between the two parts, twenty down
    EXPECT_FLOAT_EQ(inspector.y, 68.f);
    EXPECT_FLOAT_EQ(log.y, 28.f);

    auto other = std::make_shared<kui::DockLayout>();
    ASSERT_TRUE(other->Load(layout->Save()));
    EXPECT_EQ(other->Save(), layout->Save());

    layout->Hide("inspector");
    settle(); settle();
    EXPECT_FALSE(layout->IsShown("inspector"));
    EXPECT_FLOAT_EQ(log.y, 128.f) << "the part that is open has the whole side";
}

TEST_F(DockTest, APanelFloatsAgainAtTheSizeItFloatedAtBefore) {
    layout->Float("inspector", kui::Rect::XYWH(10.f, 10.f, 150.f, 100.f));
    settle(); settle();
    ASSERT_TRUE(layout->IsFloating("inspector"));
    const kor::Vec2 floated = inspector;

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
    const auto frame = [&](const kor::Vec2 p) { input.FeedMousePosition(p); settle(); };
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

// ---- icons -----------------------------------------------------------------------------------------------

namespace {
    /** @brief Where @p image's shapes really reach, curves flattened: control points may stray outside. */
    kui::Rect DrawnBounds(const kui::VectorImage& image) {
        kui::Rect r { 1e9f, 1e9f, -1e9f, -1e9f };
        for (const auto& shape : image.Shapes())
            for (const auto& contour : shape.path.Flatten(0.01f))
                for (const kor::Vec2 p : contour.points) {
                    r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y);
                    r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y);
                }
        return r;
    }
}

TEST(Icons, EveryMaterialIconInEveryStyleIsReadAndStaysInItsBox) {
    const auto names = kui::MaterialIconNames();
    EXPECT_EQ(names.size(), 2132u) << "Compose's material-icons-core and -extended";
    for (const auto style : { kui::IconStyle::eFilled, kui::IconStyle::eOutlined, kui::IconStyle::eRounded, kui::IconStyle::eSharp, kui::IconStyle::eTwoTone })
        for (const std::string_view name : names) {
            const auto icon = kui::MaterialIcon(name, style);
            ASSERT_NE(icon, nullptr) << name;
            EXPECT_FALSE(icon->Empty()) << name;
            EXPECT_EQ(icon->ViewBox(), kui::Rect::XYWH(0, 0, 24, 24)) << name;
            const kui::Rect r = DrawnBounds(*icon);
            // A few of Google's paths overshoot the box by a few hundredths: as drawn, not as read.
            EXPECT_GE(r.left, -0.05f) << name; EXPECT_GE(r.top, -0.05f) << name;
            EXPECT_LE(r.right, 24.05f) << name; EXPECT_LE(r.bottom, 24.05f) << name;
            EXPECT_GT(std::max(r.Width(), r.Height()), 3.f) << name << ": a box of fill=\"none\" is not drawn, the icon is";
        }
}

TEST(Icons, NamesAreMaterialsOrComposesAndUnknownOnesAreNull) {
    EXPECT_EQ(kui::MaterialIcon("ArrowBack"), kui::MaterialIcon("arrow_back")) << "the same icon, read once";
    EXPECT_NE(kui::MaterialIcon("ArrowBack"), kui::MaterialIcon("ArrowBack", kui::IconStyle::eOutlined));
    EXPECT_EQ(kui::MaterialIcon("NoSuchIcon"), nullptr);
    // Two-tone icons have their light part at 30 %.
    const auto twoTone = kui::MaterialIcon("Lock", kui::IconStyle::eTwoTone);
    EXPECT_TRUE(std::ranges::any_of(twoTone->Shapes(), [](const auto& s) { return std::abs(s.opacity - 0.3f) < 1e-4f; }));
    EXPECT_TRUE(std::ranges::any_of(twoTone->Shapes(), [](const auto& s) { return s.opacity == 1.f; }));
}

TEST(Icons, SvgArcsShorthandsTransformsAndShapes) {
    // A half circle from (0, 5) to (10, 5) over the top, as the sweep flag says, and its flags run together.
    auto arc = kui::VectorImage::FromSvg(R"svg(<svg viewBox="0 0 10 10"><path d="M0 5a5 5 0 0110 0z"/></svg>)svg");
    ASSERT_EQ(arc.Shapes().size(), 1u);
    EXPECT_EQ(arc.ViewBox(), kui::Rect::XYWH(0, 0, 10, 10));
    kui::Rect r = DrawnBounds(arc);
    EXPECT_NEAR(r.top, 0.f, 0.01f); EXPECT_NEAR(r.bottom, 5.f, 0.01f);
    EXPECT_NEAR(r.left, 0.f, 0.01f); EXPECT_NEAR(r.right, 10.f, 0.01f);

    // Groups move, scale and hide what is in them; a <defs> is not drawn; comments are skipped.
    auto groups = kui::VectorImage::FromSvg(R"svg(<?xml version="1.0"?><!-- a comment --><svg width="20" height="20">
        <defs><rect width="20" height="20"/></defs>
        <g transform="translate(10 2) scale(2)" opacity=".5"><rect x="0" y="0" width="2" height="1" fill-opacity="0.5"/></g>
        <g fill="none"><circle cx="5" cy="5" r="4"/></g>
        <circle cx="5" cy="15" r="2" style="fill:#000;opacity:.25"/></svg>)svg");
    EXPECT_EQ(groups.ViewBox(), kui::Rect::XYWH(0, 0, 20, 20));
    ASSERT_EQ(groups.Shapes().size(), 2u);
    EXPECT_FLOAT_EQ(groups.Shapes()[0].opacity, 0.25f);
    EXPECT_EQ(groups.Shapes()[0].path.Bounds(), kui::Rect::LTRB(10, 2, 14, 4));
    EXPECT_FLOAT_EQ(groups.Shapes()[1].opacity, 0.25f);

    // S reflects the last control point; H, V and an L's repeated coordinates are lines.
    auto smooth = kui::VectorImage::FromSvg(R"svg(<svg viewBox="0 0 24 24"><path d="M2 12C2 2 12 2 12 12S22 22 22 12M2 20H8V14L10 20 2 20" fill-rule="evenodd"/></svg>)svg");
    ASSERT_EQ(smooth.Shapes().size(), 1u);
    EXPECT_EQ(smooth.Shapes()[0].path.GetFillRule(), kui::FillRule::eEvenOdd);
    r = DrawnBounds(smooth);
    EXPECT_NEAR(r.top, 4.5f, 0.05f); EXPECT_NEAR(r.bottom, 20.f, 0.05f);

    EXPECT_TRUE(kui::VectorImage::FromSvg("not an svg").Empty());
}

TEST_F(WidgetTest, AnIconIs24UnitsInTheThemesTextColourUnlessSizedOrTinted) {
    Show(kui::Align(kui::Alignment::TopLeft(), kui::Icon("Add", kui::IconStyle::eFilled, Red)));
    // Material's plus: bars from 5 to 19, 11 to 13 across.
    EXPECT_TRUE(IsRed(scene->At(12, 6)));
    EXPECT_TRUE(IsRed(scene->At(6, 12)));
    EXPECT_TRUE(IsBlack(scene->At(7, 7)));
    EXPECT_TRUE(IsBlack(scene->At(12, 21)));

    Show(kui::Align(kui::Alignment::TopLeft(), kui::SizedBox(48.f, 48.f, kui::Icon("add"))));
    const auto text = kui::Theme::Current().text;
    const auto p = scene->At(24, 12);
    EXPECT_NEAR(p.x, text.r * 255.f, 3.f); EXPECT_NEAR(p.y, text.g * 255.f, 3.f) << "the theme's text colour";
    EXPECT_TRUE(IsBlack(scene->At(14, 14))) << "twice the size";
    EXPECT_FALSE(IsBlack(scene->At(24, 36)));
}

namespace {
    // Every Material icon in one style: 48 a row, each 32 units in a 40-unit cell.
    constexpr int IconCell = 40, IconColumns = 48;
    const int SheetW = IconColumns * IconCell;
    const int SheetH = (static_cast<int>(kui::MaterialIconNames().size()) + IconColumns - 1) / IconColumns * IconCell;
    kui::IconStyle s_sheetStyle = kui::IconStyle::eFilled;

    class IconSheetScene final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(SheetW * SheetH * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            std::vector<kui::Widget> rows, row;
            for (const std::string_view name : kui::MaterialIconNames()) {
                row.push_back(kui::SizedBox(IconCell, IconCell, kui::Center(kui::SizedBox(32.f, 32.f, kui::Icon(name, s_sheetStyle, kui::colors::White)))));
                if (row.size() == IconColumns) rows.push_back(kui::Row(std::exchange(row, {})));
            }
            if (!row.empty()) rows.push_back(kui::Row(std::move(row)));
            ui.SetRoot(kui::Align(kui::Alignment::TopLeft(), kui::Column(std::move(rows), { .crossAxisAlignment = kui::CrossAxisAlignment::eStart })));
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
        kor::Resource<kor::Buffer> readback;
    };
}

TEST(IconSheet, Renders) {
    const char* out = std::getenv("KUI_ICON_SHEET");
    if (!s_app || !out) GTEST_SKIP() << "set KUI_ICON_SHEET to a path prefix to render every icon, a .png a style";
    const std::pair<kui::IconStyle, const char*> styles[] = { { kui::IconStyle::eFilled, "filled" }, { kui::IconStyle::eOutlined, "outlined" },
        { kui::IconStyle::eRounded, "rounded" }, { kui::IconStyle::eSharp, "sharp" }, { kui::IconStyle::eTwoTone, "twotone" } };
    for (const auto& [style, name] : styles) {
        s_sheetStyle = style;
        auto* scene = s_app->OpenOffscreen<IconSheetScene>({ .title = "icons", .extent = { static_cast<std::uint32_t>(SheetW), static_cast<std::uint32_t>(SheetH) },
                                                             .format = kor::Window::Format::eRGBA8_SRGB });
        ASSERT_NE(scene, nullptr);
        for (int i = 0; i < 4; ++i) settle();
        const auto pixels = scene->readback->Read<kor::U8Vec4>(static_cast<std::size_t>(SheetW) * SheetH);
        stbi_write_png((std::string(out) + "-" + name + ".png").c_str(), SheetW, SheetH, 4, pixels.data(), SheetW * 4);
        s_app->Close(*scene);
        settle();
    }
}

// ---- text at every weight and scale, when KUI_TEXT_SHEET names a path prefix --------------------------------

namespace {
    constexpr int TextSheetW = 760, TextSheetH = 560;   // in units: drawn at the scale times as many pixels
    float s_textScale = 1.f;
    bool s_textLight = false;

    class TextSheetScene final : public kor::Scene {
    public:
        int w = 0, h = 0;
        void Initialize() override {
            w = static_cast<int>(std::lround(TextSheetW * s_textScale));
            h = static_cast<int>(std::lround(TextSheetH * s_textScale));
            readback = kor::Buffer::RawBuilder{}.SetRawSize(static_cast<kor::i64>(w) * h * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            const kui::Theme theme = s_textLight ? kui::Theme::Light() : kui::Theme::Dark();
            ui.SetTheme(theme);
            ui.SetScale(s_textScale);
            std::vector<kui::Widget> lines;
            for (const char* weight : { "Regular", "Bold", "Black" }) {
                const auto font = kui::Font::Load(std::format("fonts/Inter_28pt-{}.ttf", weight));
                for (const float size : { 11.f, 13.f, 16.f, 22.f, 34.f }) {
                    kui::TextStyle style = theme.textStyle;
                    style.font = font;
                    style.size = size;
                    lines.push_back(kui::Text(std::format("{} {}  Hamburgefonstiv 0123 aeg@&%", weight, size), style));
                }
            }
            ui.SetRoot(kui::Container({ .padding = kui::EdgeInsets::All(10.f), .decoration = { .color = theme.background } },
                kui::Column(std::move(lines), { .crossAxisAlignment = kui::CrossAxisAlignment::eStart, .gap = 4.f })));
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
        kor::Resource<kor::Buffer> readback;
    };
}

TEST(TextSheet, Renders) {
    const char* out = std::getenv("KUI_TEXT_SHEET");
    if (!s_app || !out) GTEST_SKIP() << "set KUI_TEXT_SHEET to a path prefix to render text at each scale, light and dark";
    for (const bool light : { true, false }) {
        for (const float scale : { 1.f, 1.25f, 1.5f, 2.f }) {
            s_textLight = light;
            s_textScale = scale;
            auto* scene = s_app->OpenOffscreen<TextSheetScene>({ .title = "text",
                .extent = { static_cast<std::uint32_t>(std::lround(TextSheetW * scale)), static_cast<std::uint32_t>(std::lround(TextSheetH * scale)) },
                .format = kor::Window::Format::eRGBA8_SRGB });
            ASSERT_NE(scene, nullptr);
            for (int i = 0; i < 4; ++i) settle();
            const auto pixels = scene->readback->Read<kor::U8Vec4>(static_cast<std::size_t>(scene->w) * scene->h);
            stbi_write_png(std::format("{}-{}-{}.png", out, light ? "light" : "dark", scale).c_str(), scene->w, scene->h, 4, pixels.data(), scene->w * 4);
            s_app->Close(*scene);
            settle();
        }
    }
}

// ---- text, against stb_truetype's own rasteriser -----------------------------------------------------------

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace {
    /**
     * How much of each pixel stb_truetype's coverage rasteriser fills, drawing @p text (ASCII) where a Paragraph lays
     * it out with its top-left at @p origin and its baseline on the pixel grid, as kui draws it. It fills a font's
     * overlapping contours as the font means them: once.
     */
    std::vector<float> ReferenceCoverage(const std::string& file, const std::string& text, const float size, const kor::Vec2 origin)
    {
        std::ifstream in(kor::AssetPath(file), std::ios::binary);
        const std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        stbtt_fontinfo info {};
        stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0));
        const float scale = stbtt_ScaleForMappingEmToPixels(&info, size);
        kui::TextStyle style;
        style.font = kui::Font::Load(file);
        style.size = size;
        const kui::Paragraph paragraph(text, style);
        const auto& line = paragraph.Lines().front();
        const float baseline = std::round(origin.y + line.baseline);
        std::vector<float> cover(static_cast<std::size_t>(Size) * Size, 0.f);
        for (std::size_t c = 0; c < text.size(); ++c) {
            if (text[c] == ' ') continue;
            const float pen = origin.x + line.carets[c];
            const float whole = std::floor(pen), shift = pen - whole;
            const int glyph = stbtt_FindGlyphIndex(&info, text[c]);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetGlyphBitmapBoxSubpixel(&info, glyph, scale, scale, shift, 0.f, &x0, &y0, &x1, &y1);
            const int w = x1 - x0, h = y1 - y0;
            if (w <= 0 || h <= 0) continue;
            std::vector<unsigned char> bitmap(static_cast<std::size_t>(w) * h);
            stbtt_MakeGlyphBitmapSubpixel(&info, bitmap.data(), w, h, w, scale, scale, shift, 0.f, glyph);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const int px = static_cast<int>(whole) + x0 + x, py = static_cast<int>(baseline) + y0 + y;
                if (px < 0 || py < 0 || px >= Size || py >= Size) continue;
                float& to = cover[static_cast<std::size_t>(py) * Size + px];
                to = std::min(1.f, to + bitmap[static_cast<std::size_t>(y) * w + x] / 255.f);
            }
        }
        return cover;
    }

    float CoverageAt(const std::vector<float>& cover, const int x, const int y)
    {
        return x < 0 || y < 0 || x >= Size || y >= Size ? 0.f : cover[static_cast<std::size_t>(y) * Size + x];
    }
}

// Fonts made from variable ones keep their contours overlapping where strokes meet, and the heaviest weights
// most of all. A distance to every edge, buried ones too, once showed as light seams through the letters.
TEST_F(Gpu, HeavyLettersAreSolidWhereTheirStrokesOverlap) {
    const std::string file = "fonts/Inter_28pt-Black.ttf";
    const std::string text = "k4e";
    constexpr float size = 30.f;
    const kor::Vec2 origin { 2.f, 4.f };
    Draw([&](kui::Canvas& c) { c.DrawText(text, origin, { .font = kui::Font::Load(file), .size = size, .color = kui::colors::White }); });
    const auto reference = ReferenceCoverage(file, text, size, origin);
    int deep = 0;
    for (int y = 0; y < Size; ++y) for (int x = 0; x < Size; ++x) {
        bool inside = true;   // the pixel and all round it wholly covered
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) inside = inside && CoverageAt(reference, x + dx, y + dy) >= 1.f;
        if (!inside) continue;
        ++deep;
        EXPECT_GE(scene->At(x, y).x, 235) << "a seam through the letters at " << x << "," << y;
    }
    EXPECT_GT(deep, 100) << "the reference and the drawing did not line up";
}

// A glyph is drawn as a quad grown by an anti-aliasing fringe of a pixel and a half, which in small text is
// several texels of the atlas — past its own cell, into the glyph beside it there, which once showed as a
// sliver of some other letter next to it.
TEST_F(Gpu, SmallTextShowsNothingOfTheGlyphsBesideItInTheAtlas) {
    const std::string file = "fonts/Inter_28pt-Regular.ttf";
    const std::string text = "H1a1e1g1k4";
    constexpr float size = 7.f;
    const kor::Vec2 origin { 3.f, 20.f };
    Draw([&](kui::Canvas& c) { c.DrawText(text, origin, { .font = kui::Font::Load(file), .size = size, .color = kui::colors::White }); });
    const auto reference = ReferenceCoverage(file, text, size, origin);
    for (int y = 0; y < Size; ++y) for (int x = 0; x < Size; ++x) {
        bool clear = true;   // nothing of the text within a pixel of it
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) clear = clear && CoverageAt(reference, x + dx, y + dy) <= 0.f;
        if (clear) EXPECT_LT(scene->At(x, y).x, 40) << "something drawn at " << x << "," << y << ", away from every letter";
    }
}

// Text sits on whole pixels: drawn at a fraction of a pixel down, its baseline still falls between two rows,
// so the foot of a letter is one sharp row rather than two grey ones (at a scale of 1.25 or 1.5, everything
// falls at fractions of a pixel).
TEST_F(Gpu, TextsBaselineFallsBetweenRowsOfPixels) {
    const auto font = kui::Font::Load("fonts/Inter_28pt-Bold.ttf");
    constexpr float size = 30.f;
    for (const float fraction : { 0.f, 0.3f, 0.5f, 0.7f }) {
        const kor::Vec2 origin { 4.f, 6.f + fraction };
        Draw([&](kui::Canvas& c) { c.DrawText("H", origin, { .font = font, .size = size, .color = kui::colors::White }); });
        const kui::Paragraph paragraph("H", { .font = font, .size = size });
        const int baseline = static_cast<int>(std::round(origin.y + paragraph.Lines().front().baseline));
        int stem = -1;   // the middle of the H's left stem, found a little above its foot
        for (int x = 0; x < Size && stem < 0; ++x)
            if (scene->At(x, baseline - 4).x > 240 && scene->At(x + 1, baseline - 4).x > 240) stem = x + 1;
        ASSERT_GE(stem, 0) << "no H at " << fraction;
        EXPECT_GE(scene->At(stem, baseline - 1).x, 235) << "the foot's row is not wholly lit, " << fraction << " of a pixel down";
        EXPECT_LE(scene->At(stem, baseline).x, 20) << "the row under the foot is lit, " << fraction << " of a pixel down";
    }
}

// Blended in linear light, as an sRGB target blends, a stem a pixel wide and partly covered once came out far
// paler than its colour: black text on white read as thin and grey. Dark text is covered as if blended in sRGB,
// where it is seen, so it carries as much ink as the letters cover.
TEST(DarkText, IsAsDarkOnAnSrgbTargetAsItsLettersCover) {
    if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
    auto* scene = s_app->OpenOffscreen<Canvas>({ .title = "kui srgb", .extent = { Size, Size }, .format = kor::Window::Format::eRGBA8_SRGB });
    ASSERT_NE(scene, nullptr);
    const std::string file = "fonts/Inter_28pt-Regular.ttf", text = "lllll";
    constexpr float size = 13.f;
    const kor::Vec2 origin { 4.3f, 10.f };
    kui::Canvas canvas;
    canvas.DrawRect(kui::Rect::LTRB(0, 0, Size, Size), kui::Paint::Fill(kui::colors::White));
    canvas.DrawText(text, origin, { .font = kui::Font::Load(file), .size = size, .color = kui::colors::Black });
    scene->root->SetPicture(canvas.Finish());
    settle();
    settle();
    // Along a row through the middle of the stems: how much darker than the white it is, against how much of
    // it the letters cover.
    const auto reference = ReferenceCoverage(file, text, size, origin);
    const int row = 18;
    float ink = 0.f, covered = 0.f;
    for (int x = 0; x < Size; ++x) {
        ink += (255.f - scene->At(x, row).x) / 255.f;
        covered += CoverageAt(reference, x, row);
    }
    s_app->Close(*scene);
    settle();
    ASSERT_GT(covered, 3.f) << "the row misses the letters";
    EXPECT_GT(ink / covered, 0.85f) << "black text is paler than what its letters cover: " << ink << " of " << covered;
}

// ---- the node editor ---------------------------------------------------------------------------------------

namespace {
    class NodeTest : public ::testing::Test {
    protected:
        static constexpr kor::u32 W = 400, H = 300;
        void SetUp() override {
            if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
            scene = s_app->OpenOffscreen<DockScene>({ .title = "kui nodes", .extent = { W, H } });
            ASSERT_NE(scene, nullptr);
        }
        void TearDown() override {
            if (scene) { s_app->Close(*scene); settle(); }
        }
        void Show(kui::Widget root) { scene->ui.SetRoot(std::move(root)); settle(); settle(); }
        void Move(const kor::Vec2 p) { scene->SceneInput().FeedMousePosition(p); settle(); }
        void Button(const kor::MouseButton b, const bool down) { scene->SceneInput().FeedMouseButton(b, down); settle(); }
        /** @brief Pressed at @p from, moved to @p to in steps, let go there. */
        void Drag(const kor::Vec2 from, const kor::Vec2 to, const kor::MouseButton b = kor::MouseButton::eLeft) {
            Move(from);
            Button(b, true);
            for (int i = 1; i <= 4; ++i) Move(from + (to - from) * (static_cast<float>(i) / 4.f));
            Button(b, false);
        }
        void Click(const kor::Vec2 at) { Move(at); Button(kor::MouseButton::eLeft, true); Button(kor::MouseButton::eLeft, false); }
        void Key(const kor::Key key, const bool control = false) {
            auto& input = scene->SceneInput();
            if (control) { input.FeedKey(kor::Key::eLeftControl, true); settle(); }
            input.FeedKey(key, true); settle();
            input.FeedKey(key, false); settle();
            if (control) { input.FeedKey(kor::Key::eLeftControl, false); settle(); }
        }

        /** @brief Two nodes side by side: A's image out, B's image and number in. */
        static kui::NodeGraph TwoNodes() {
            kui::NodeGraph g;
            g.nodes.push_back({ .id = "A", .title = "A", .position = { 20.f, 20.f },
                                .outputs = { { .id = "out", .label = "Image", .type = "image" } } });
            g.nodes.push_back({ .id = "B", .title = "B", .position = { 220.f, 20.f },
                                .inputs = { { .id = "in", .label = "Image", .type = "image" }, { .id = "n", .label = "Count", .type = "number" } } });
            return g;
        }
        /** @brief Where a port is in the view, at zoom 1 and no pan: nodes are 140 wide unless they need more. */
        static kor::Vec2 PortAt(const kor::Vec2 node, const bool output, const std::size_t index, const float width = kui::nodes::MinWidth) {
            return node + kui::nodes::PortOffset(output, index, width);
        }
        DockScene* scene = nullptr;
    };
}

TEST_F(NodeTest, AWireIsDrawnBetweenPortsThatFit) {
    std::vector<kui::GraphWire> made;
    Show(kui::NodeEditor(TwoNodes(), kui::NodeEditorOptions{}.OnConnect([&](const kui::GraphWire& w) { made.push_back(w); })));
    const kor::Vec2 out = PortAt({ 20, 20 }, true, 0), in = PortAt({ 220, 20 }, false, 0), count = PortAt({ 220, 20 }, false, 1);

    Drag(out, in);
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(made[0], (kui::GraphWire { { "A", "out" }, { "B", "in" } }));

    Drag(out, count);
    EXPECT_EQ(made.size(), 1u) << "an image does not go into a number";
    Drag(in, out);
    ASSERT_EQ(made.size(), 2u) << "drawn the other way, from the input";
    EXPECT_EQ(made[1], (kui::GraphWire { { "A", "out" }, { "B", "in" } }));
}

TEST_F(NodeTest, ACustomRuleDecidesWhatFits) {
    std::vector<kui::GraphWire> made;
    Show(kui::NodeEditor(TwoNodes(), kui::NodeEditorOptions{}
        .CanConnect([](const kui::NodePort&, const kui::NodePort& to) { return to.type == "number"; })
        .OnConnect([&](const kui::GraphWire& w) { made.push_back(w); })));
    Drag(PortAt({ 20, 20 }, true, 0), PortAt({ 220, 20 }, false, 1));
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(made[0].to.port, "n");
}

TEST_F(NodeTest, DraggingANodeMovesEverythingPicked) {
    std::vector<std::string> moved, picked;
    kor::Vec2 by {};
    Show(kui::NodeEditor(TwoNodes(), kui::NodeEditorOptions{}
        .OnMove([&](const std::vector<std::string>& n, const kor::Vec2 b) { moved = n; by = b; })
        .OnSelectionChanged([&](const std::vector<std::string>& n) { picked = n; })));
    Click({ 60.f, 30.f });   // A's title
    EXPECT_EQ(picked, std::vector<std::string>{ "A" });
    scene->SceneInput().FeedKey(kor::Key::eLeftShift, true);
    Click({ 260.f, 30.f });
    scene->SceneInput().FeedKey(kor::Key::eLeftShift, false);
    EXPECT_EQ(picked, (std::vector<std::string>{ "A", "B" }));
    Drag({ 60.f, 30.f }, { 90.f, 40.f });
    EXPECT_EQ(moved, (std::vector<std::string>{ "A", "B" }));
    EXPECT_NEAR(by.x, 30.f, 0.01f);
    EXPECT_NEAR(by.y, 10.f, 0.01f);
}

// What a node shows is drawn where the node is: under its outline, its ports and the pointer that moves it. It was
// drawn with the view's pan and zoom, and a drag in hand, counted twice — so it left its own card behind as soon as
// the view moved or the node was picked up.
TEST_F(NodeTest, ANodesContentIsDrawnWhereTheNodeIs) {
    const kui::Color mark = kui::Color::Hex(0xFF00FF);
    kui::NodeGraph graph;
    // Its body: a block of one colour, 140 by 40, under the title.
    graph.nodes.push_back({ .id = "A", .title = "A", .position = { 20.f, 20.f }, .body = kui::SizedBox(140.f, 40.f).Background(mark) });
    Show(kui::NodeEditor(graph));
    const auto marked = [&](const float x, const float y) {
        const auto p = scene->At(static_cast<int>(x), static_cast<int>(y));
        return p.x > 200 && p.y < 60 && p.z > 200;
    };
    const kor::Vec2 body { 20.f + 70.f, 20.f + kui::nodes::TitleHeight + 20.f };   // the middle of the block
    EXPECT_TRUE(marked(body.x, body.y)) << "at rest";

    // Picked up by its title and held 40 across and 30 down: the block is there, and not as far again.
    Move({ 60.f, 30.f });
    Button(kor::MouseButton::eLeft, true);
    for (int i = 1; i <= 4; ++i) Move({ 60.f + i * 10.f, 30.f + i * 7.5f });
    settle();
    EXPECT_TRUE(marked(body.x + 40.f, body.y + 30.f)) << "in hand, it is under the pointer that holds it";
    EXPECT_FALSE(marked(body.x + 40.f + 75.f, body.y + 30.f + 45.f)) << "and nowhere beyond it";
    EXPECT_FALSE(marked(body.x - 60.f, body.y - 10.f)) << "nor where it was picked up";
    Button(kor::MouseButton::eLeft, false);   // nothing applies the move here: it is back where the graph says
    settle();
    EXPECT_TRUE(marked(body.x, body.y));

    // The view dragged 100 across and 50 down with the right button: the block went with it, once.
    Drag({ 300.f, 250.f }, { 400.f - 1.f, 300.f - 1.f }, kor::MouseButton::eRight);
    settle();
    const kor::Vec2 pan { 99.f, 49.f };
    EXPECT_TRUE(marked(body.x + pan.x, body.y + pan.y)) << "panned, it is where its card is";
    EXPECT_FALSE(marked(body.x + pan.x * 2.f, body.y + pan.y * 2.f + 25.f)) << "and not as far again";

    // Zoomed in about the block's middle: it stays under the pointer, and is bigger about it.
    Move(body + pan);
    scene->SceneInput().FeedScroll({ 0.f, 1.f });
    settle();
    settle();
    EXPECT_TRUE(marked(body.x + pan.x, body.y + pan.y));
    EXPECT_TRUE(marked(body.x + pan.x + 75.f, body.y + pan.y)) << "its edge was 70 from its middle, and is further now";
    EXPECT_FALSE(marked(body.x + pan.x + 110.f, body.y + pan.y)) << "but not by much";
}

TEST_F(NodeTest, ABoxPicksWhatItTouchesAndDeleteRemovesIt) {
    std::vector<std::string> picked, deleted;
    std::vector<kui::GraphWire> deletedWires;
    auto graph = TwoNodes();
    graph.wires.push_back({ { "A", "out" }, { "B", "in" } });
    Show(kui::NodeEditor(graph, kui::NodeEditorOptions{}
        .OnSelectionChanged([&](const std::vector<std::string>& n) { picked = n; })
        .OnDelete([&](const std::vector<std::string>& n, const std::vector<kui::GraphWire>& w, const std::vector<std::string>&) {
            deleted = n;
            deletedWires = w;
        })));
    Drag({ 5.f, 200.f }, { 100.f, 30.f });   // from below A, up over its lower-left corner
    EXPECT_EQ(picked, std::vector<std::string>{ "A" });
    Key(kor::Key::eDelete);
    EXPECT_EQ(deleted, std::vector<std::string>{ "A" });

    // A wire, picked by clicking near it, and deleted.
    const kor::Vec2 out = PortAt({ 20, 20 }, true, 0), in = PortAt({ 220, 20 }, false, 0);
    Click((out + in) * 0.5f);
    Key(kor::Key::eDelete);
    ASSERT_EQ(deletedWires.size(), 1u);
    EXPECT_EQ(deletedWires[0], graph.wires[0]);
}

TEST_F(NodeTest, TheWheelZoomsAboutThePointerAndTheRightButtonPans) {
    std::vector<kui::GraphWire> made;
    kor::Vec2 menuAt { -1.f };
    Show(kui::NodeEditor(TwoNodes(), kui::NodeEditorOptions{}
        .OnConnect([&](const kui::GraphWire& w) { made.push_back(w); })
        .OnContextMenu([&](const kor::Vec2 at, kor::Vec2) { menuAt = at; })));
    const kor::Vec2 out = PortAt({ 20, 20 }, true, 0), in = PortAt({ 220, 20 }, false, 0);

    // Zoomed in about A's port: it stays under the pointer, and B's port moves away from it.
    Move(out);
    scene->SceneInput().FeedScroll({ 0.f, 1.f });
    settle();
    const float zoom = 1.15f;
    const kor::Vec2 zoomedIn = out + (in - out) * zoom;
    Drag(out, zoomedIn);
    ASSERT_EQ(made.size(), 1u) << "the ports were where zooming about the pointer put them";

    // Dragged 50 to the left with the right button: everything is 50 to the left.
    Drag({ 200.f, 250.f }, { 150.f, 250.f }, kor::MouseButton::eRight);
    Drag(out - kor::Vec2(50.f, 0.f), zoomedIn - kor::Vec2(50.f, 0.f));
    EXPECT_EQ(made.size(), 2u);

    // A right click that does not drag is a context menu, at the point of the graph under it.
    Move({ 300.f, 250.f });
    Button(kor::MouseButton::eRight, true);
    Button(kor::MouseButton::eRight, false);
    const kor::Vec2 pan = out * (1.f - zoom) - kor::Vec2(50.f, 0.f);
    EXPECT_NEAR(menuAt.x, (300.f - pan.x) / zoom, 0.1f);
    EXPECT_NEAR(menuAt.y, (250.f - pan.y) / zoom, 0.1f);
}

TEST_F(NodeTest, AWireIsPickedUpOffTheInputItGoesInto) {
    std::vector<kui::GraphWire> dropped, made;
    kui::PortRef offeredFrom;
    auto graph = TwoNodes();
    graph.wires.push_back({ { "A", "out" }, { "B", "in" } });
    Show(kui::NodeEditor(graph, kui::NodeEditorOptions{}
        .OnDisconnect([&](const kui::GraphWire& w) { dropped.push_back(w); })
        .OnConnect([&](const kui::GraphWire& w) { made.push_back(w); })
        .OnWireDropped([&](const kui::PortRef& from, bool, kor::Vec2) { offeredFrom = from; })));
    const kor::Vec2 in = PortAt({ 220, 20 }, false, 0);
    Drag(in, { 200.f, 250.f });
    ASSERT_EQ(dropped.size(), 1u) << "picked up and let go over nothing: taken away";
    EXPECT_EQ(dropped[0], graph.wires[0]);
    EXPECT_TRUE(made.empty());

    // A new wire let go over nothing is offered, to make a node for it.
    Drag(PortAt({ 20, 20 }, true, 0), { 150.f, 250.f });
    EXPECT_EQ(offeredFrom, (kui::PortRef { "A", "out" }));
}

TEST_F(NodeTest, ANodesOwnControlsWorkAndDoNotDragIt) {
    int pressed = 0;
    std::vector<std::string> moved;
    auto graph = TwoNodes();
    graph.nodes[0].body = kui::Button("Go", [&] { ++pressed; });
    Show(kui::NodeEditor(graph, kui::NodeEditorOptions{}.OnMove([&](const std::vector<std::string>& n, kor::Vec2) { moved = n; })));
    // The button sits under A's one port row: from 20 + 28 + 24 + 8 down.
    const kor::Vec2 button { 60.f, 20.f + kui::nodes::TitleHeight + kui::nodes::RowHeight + 20.f };
    Click(button);
    EXPECT_EQ(pressed, 1);
    Drag(button, button + kor::Vec2(30.f, 80.f));   // let go off the button, which would count it a press
    EXPECT_TRUE(moved.empty()) << "a press its button took does not drag the node";
    EXPECT_EQ(pressed, 1);

    // Zoomed out about the origin, the button is where the zoom put it, and still takes the click.
    Move({ 0.f, 0.f });
    scene->SceneInput().FeedScroll({ 0.f, -1.f });
    settle();
    Click(button / 1.15f);
    EXPECT_EQ(pressed, 2);
}

TEST_F(NodeTest, KeysCopyPasteAndDuplicateWhatIsPicked) {
    std::vector<std::string> copied, duplicated;
    kor::Vec2 pastedAt { -1.f };
    Show(kui::NodeEditor(TwoNodes(), kui::NodeEditorOptions{}
        .OnCopy([&](const std::vector<std::string>& n) { copied = n; })
        .OnPaste([&](const kor::Vec2 at) { pastedAt = at; })
        .OnDuplicate([&](const std::vector<std::string>& n) { duplicated = n; })));
    Click({ 260.f, 30.f });
    Key(kor::Key::eC, true);
    EXPECT_EQ(copied, std::vector<std::string>{ "B" });
    Key(kor::Key::eD, true);
    EXPECT_EQ(duplicated, std::vector<std::string>{ "B" });
    Move({ 300.f, 200.f });
    Key(kor::Key::eV, true);
    EXPECT_NEAR(pastedAt.x, 300.f, 0.01f);
    EXPECT_NEAR(pastedAt.y, 200.f, 0.01f);
    Key(kor::Key::eA, true);
    Key(kor::Key::eC, true);
    EXPECT_EQ(copied, (std::vector<std::string>{ "A", "B" })) << "Control+A picked everything";
}

TEST_F(NodeTest, ACommentCarriesTheNodesInsideIt) {
    std::vector<std::string> moved;
    kui::Rect commentNow {};
    auto graph = TwoNodes();
    graph.comments.push_back({ .id = "group", .text = "Inputs", .rect = kui::Rect::LTRB(10.f, 0.f, 190.f, 150.f) });
    Show(kui::NodeEditor(graph, kui::NodeEditorOptions{}
        .OnMove([&](const std::vector<std::string>& n, kor::Vec2) { moved = n; })
        .OnCommentChanged([&](const std::string&, const kui::Rect r) { commentNow = r; })));
    Drag({ 100.f, 10.f }, { 120.f, 22.f });   // by its title: the strip along its top, above A
    EXPECT_EQ(commentNow, graph.comments[0].rect.Shift({ 20.f, 12.f }));
    EXPECT_EQ(moved, std::vector<std::string>{ "A" }) << "A is inside it, B is not";

    Drag({ 184.f, 144.f }, { 214.f, 164.f });   // its corner
    EXPECT_EQ(commentNow, kui::Rect::LTRB(10.f, 0.f, 220.f, 170.f));
}

TEST_F(NodeTest, ThreeHundredNodesPanAndDragWithoutBuildingAgain) {
    kui::NodeGraph graph;
    for (int i = 0; i < 300; ++i) {
        graph.nodes.push_back({ .id = std::format("n{}", i), .title = std::format("Node {}", i),
                                .position = { static_cast<float>(i % 20) * 170.f, static_cast<float>(i / 20) * 110.f },
                                .inputs = { { .id = "in", .label = "In", .type = "t" } }, .outputs = { { .id = "out", .label = "Out", .type = "t" } } });
        if (i > 0) graph.wires.push_back({ { std::format("n{}", i - 1), "out" }, { std::format("n{}", i), "in" } });
    }
    std::vector<std::string> moved;
    Show(kui::NodeEditor(graph, kui::NodeEditorOptions{}.OnMove([&](const std::vector<std::string>& n, kor::Vec2) { moved = n; })));

    std::size_t builds = 0;
    double worst = 0.0;   // the view's own work in a frame: input, building, layout and painting
    const auto sample = [&] {
        const auto& s = scene->ui.Stats();
        builds += s.builds;
        worst = std::max(worst, s.inputMs + s.buildMs + s.layoutMs + s.paintMs);
    };
    Move({ 200.f, 280.f });
    Button(kor::MouseButton::eMiddle, true);
    for (int i = 0; i < 10; ++i) { Move({ 200.f - static_cast<float>(i) * 10.f, 280.f }); sample(); }
    Button(kor::MouseButton::eMiddle, false);
    // Panned 90 to the left: the first node is from -90 to 50 across now.
    Move({ 20.f, 30.f });
    Button(kor::MouseButton::eLeft, true);
    for (int i = 0; i < 10; ++i) { Move({ 20.f + static_cast<float>(i) * 5.f, 30.f }); sample(); }
    Button(kor::MouseButton::eLeft, false);
    EXPECT_EQ(builds, 0u) << "panning and dragging built nothing again";
    EXPECT_EQ(moved, std::vector<std::string>{ "n0" });
    std::printf("300 nodes: the view's work in the worst dragged frame %.2f ms\n", worst);
}

namespace {
    constexpr int NodeSheetW = 760, NodeSheetH = 420;
    class NodeSheetScene final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(NodeSheetW * NodeSheetH * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            kui::NodeGraph g;
            const kui::Color image = kui::Color::Hex(0x4DB6AC), number = kui::Color::Hex(0xFFB74D), buffer = kui::Color::Hex(0x9575CD);
            g.comments.push_back({ .id = "c", .text = "Inputs", .rect = kui::Rect::XYWH(20.f, 20.f, 210.f, 330.f) });
            g.nodes.push_back({ .id = "tex", .title = "Image", .position = { 40.f, 60.f },
                                .outputs = { { .id = "img", .label = "Image", .type = "image", .color = image } } });
            g.nodes.push_back({ .id = "count", .title = "Particles", .position = { 40.f, 200.f },
                                .outputs = { { .id = "n", .label = "Count", .type = "number", .color = number } },
                                .body = kui::DragValue(4096.f, [](float) {}, { .label = "n" }) });
            g.nodes.push_back({ .id = "blur", .title = "Blur (compute)", .position = { 300.f, 60.f },
                                .inputs = { { .id = "src", .label = "Source", .type = "image", .color = image },
                                            { .id = "radius", .label = "Radius", .type = "number", .color = number } },
                                .outputs = { { .id = "dst", .label = "Result", .type = "image", .color = image } },
                                .accent = kui::Color::Hex(0x3A4A6B) });
            g.nodes.push_back({ .id = "sim", .title = "Simulate", .position = { 300.f, 240.f },
                                .inputs = { { .id = "count", .label = "Count", .type = "number", .color = number } },
                                .outputs = { { .id = "state", .label = "State", .type = "buffer", .color = buffer } },
                                .error = "no shader named 'simulate'" });
            g.nodes.push_back({ .id = "out", .title = "Screen", .position = { 560.f, 120.f },
                                .inputs = { { .id = "color", .label = "Color", .type = "image", .color = image } } });
            g.wires = { { { "tex", "img" }, { "blur", "src" } }, { { "count", "n" }, { "blur", "radius" } },
                        { { "count", "n" }, { "sim", "count" } }, { { "blur", "dst" }, { "out", "color" } } };
            ui.SetRoot(kui::NodeEditor(std::move(g)));
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
            Graph().Add<ReadPass>(kor::ResourceRef<const kor::Buffer>(readback));
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
        kor::Resource<kor::Buffer> readback;
    };
}

TEST(NodeSheet, Renders) {
    const char* out = std::getenv("KUI_NODE_SHEET");
    if (!s_app || !out) GTEST_SKIP() << "set KUI_NODE_SHEET to a .png path to render a node graph";
    auto* scene = s_app->OpenOffscreen<NodeSheetScene>({ .title = "nodes", .extent = { NodeSheetW, NodeSheetH }, .format = kor::Window::Format::eRGBA8_SRGB });
    ASSERT_NE(scene, nullptr);
    for (int i = 0; i < 3; ++i) settle();
    // The blur node picked, as a click on its title does.
    scene->SceneInput().FeedMousePosition({ 340.f, 70.f }); settle();
    scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, true); settle();
    scene->SceneInput().FeedMouseButton(kor::MouseButton::eLeft, false); settle();
    for (int i = 0; i < 3; ++i) settle();
    const auto pixels = scene->readback->Read<kor::U8Vec4>(static_cast<std::size_t>(NodeSheetW) * NodeSheetH);
    stbi_write_png(out, NodeSheetW, NodeSheetH, 4, pixels.data(), NodeSheetW * 4);
    s_app->Close(*scene);
    settle();
}
