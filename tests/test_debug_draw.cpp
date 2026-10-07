// Unit tests for debug drawing: how many lines and triangles each shape is, and gizmos picked and
// dragged through a camera. No device.

#include <gtest/gtest.h>

#include <kmath/transform.h>

#include "debugDraw.h"

TEST(DebugDraw, EachShapeIsTheLinesItShouldBe) {
    kor::DebugDraw draw;
    const auto count = [&](auto&& shape) { draw.Clear(); shape(); return draw.LineCount(); };
    EXPECT_EQ(count([&] { draw.Line({0, 0, 0}, {1, 0, 0}); }), 1u);
    EXPECT_EQ(count([&] { draw.Box({0, 0, 0}, {1, 1, 1}); }), 12u);
    EXPECT_EQ(count([&] { draw.Box(kor::Mat4(1.f)); }), 12u);
    EXPECT_EQ(count([&] { draw.Sphere({0, 0, 0}, 1.f, {}, 16); }), 48u);
    EXPECT_EQ(count([&] { draw.Arrow({0, 0, 0}, {0, 0, 1}); }), 5u);
    EXPECT_EQ(count([&] { draw.Point({0, 0, 0}); }), 3u);
    EXPECT_EQ(count([&] { draw.Axes(kor::Mat4(1.f)); }), 15u);
    EXPECT_EQ(count([&] { draw.Grid({0, 0, 0}, 10.f, 4); }), 10u);
    EXPECT_EQ(count([&] { draw.Frustum(kor::Mat4(1.f)); }), 12u);
    EXPECT_EQ(count([&] { draw.Arrow({1, 1, 1}, {1, 1, 1}); }), 1u) << "a zero-length arrow has no head";
    EXPECT_EQ(count([&] { draw.Triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}); }), 3u);
    EXPECT_EQ(count([&] { draw.Plane({0, 0, 0}, {0, 1, 0}, {2, 2}); }), 4u);
    EXPECT_EQ(count([&] { draw.Cylinder({0, 0, 0}, {0, 1, 0}, 0.5f, {}, 24); }), 52u) << "two rings and four sides";
    EXPECT_EQ(count([&] { draw.Cone({0, 0, 0}, {0, 1, 0}, 0.5f, {}, 24); }), 28u);
    EXPECT_EQ(count([&] { draw.Capsule({0, 0, 0}, {0, 1, 0}, 0.5f, {}, 24); }), 100u) << "and two half-circles over each end";
    EXPECT_EQ(count([&] { draw.SpotLight({0, 0, 0}, {0, -1, 0}, 5.f, 0.5f); }), 29u);
    EXPECT_EQ(count([&] { draw.SpotLight({0, 0, 0}, {0, -1, 0}, 5.f, 0.5f, 0.3f); }), 53u) << "with its inner ring";
    EXPECT_EQ(count([&] { draw.PointLight({0, 0, 0}, 0.f); }), 7u) << "a star, and no sphere for no limit";
    EXPECT_EQ(count([&] { draw.PointLight({0, 0, 0}, 3.f); }), 7u + 96u);
    EXPECT_EQ(count([&] { draw.DirectionalLight({0, 0, 0}, {0, -1, 0}); }), 32u + 25u);
    const kor::Mat4 view = kor::LookAt(kor::Vec3(0, 0, 5), kor::Vec3(0), kor::Vec3(0, 1, 0));
    EXPECT_EQ(count([&] { draw.Camera(view, kor::Perspective(1.f, 1.5f, 0.1f, 100.f)); }), 11u)
        << "the pyramid's eight edges and the triangle on top";
    EXPECT_EQ(count([&] { draw.Camera(view, kor::Orthographic(-1.f, 1.f, -1.f, 1.f, 0.1f, 100.f)); }), 15u)
        << "an orthographic one is a box, open at the far end";
}

TEST(DebugDraw, FillsAreTrianglesAndTheOutlineIsOptional) {
    kor::DebugDraw draw;
    const kor::DebugStyle filled {.fill = {1, 0, 0, 0.5f}, .outline = false};
    const auto triangles = [&](auto&& shape) { draw.Clear(); shape(); return draw.TriangleCount(); };
    EXPECT_EQ(triangles([&] { draw.Box(kor::Mat4(1.f)); }), 0u) << "no fill, no triangles";
    EXPECT_EQ(triangles([&] { draw.Box(kor::Mat4(1.f), filled); }), 12u);
    EXPECT_EQ(draw.LineCount(), 0u) << "and no outline when it says so";
    EXPECT_EQ(triangles([&] { draw.Triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, filled); }), 1u);
    EXPECT_EQ(triangles([&] { draw.Quad({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, filled); }), 2u);
    EXPECT_EQ(triangles([&] { draw.Circle({0, 0, 0}, {0, 1, 0}, 1.f, filled, 16); }), 16u);
    EXPECT_EQ(triangles([&] { draw.Sphere({0, 0, 0}, 1.f, filled, 16); }), 224u) << "one triangle a step at the poles, two elsewhere";
    EXPECT_EQ(triangles([&] { draw.Cylinder({0, 0, 0}, {0, 1, 0}, 1.f, filled, 16); }), 64u);
    EXPECT_EQ(triangles([&] { draw.Cone({0, 0, 0}, {0, 1, 0}, 1.f, filled, 16); }), 32u);
    EXPECT_GT(triangles([&] { draw.Capsule({0, 0, 0}, {0, 1, 0}, 0.5f, filled, 16); }), 64u);

    // Both: outlined and filled.
    draw.Clear();
    draw.Box(kor::Mat4(1.f), {.color = {1, 1, 1, 1}, .fill = {1, 1, 1, 0.2f}});
    EXPECT_EQ(draw.LineCount(), 12u);
    EXPECT_EQ(draw.TriangleCount(), 12u);
    draw.Arrow({0, 0, 0}, {0, 0, 1}, {.fill = {1, 1, 1, 1}});
    EXPECT_GT(draw.TriangleCount(), 12u) << "a filled arrow has a cone for a head";
    draw.Clear();
    EXPECT_EQ(draw.TriangleCount(), 0u);
}

namespace {
    // A camera at (0, 0, 5) looking at the origin, onto an 800 by 600 image.
    struct GizmoRig {
        kor::Vec2 viewport {800.f, 600.f};
        kor::Mat4 viewProjection = kor::Perspective(kor::Radians(60.f), 800.f / 600.f, 0.1f, 100.f)
                                 * kor::LookAt(kor::Vec3(0, 0, 5), kor::Vec3(0), kor::Vec3(0, 1, 0));

        [[nodiscard]] kor::Vec2 ToScreen(const kor::Vec3 p) const
        {
            const kor::Vec4 clip = viewProjection * kor::Vec4(p, 1.f);
            return {(clip.x / clip.w * 0.5f + 0.5f) * viewport.x, (clip.y / clip.w * 0.5f + 0.5f) * viewport.y};
        }
        /** Which way @p axis points on screen, from @p at. */
        [[nodiscard]] kor::Vec2 ScreenDirection(const kor::Vec3 at, const kor::Vec3 axis) const
        {
            return kor::Normalize(ToScreen(at + axis * 0.01f) - ToScreen(at));
        }
        [[nodiscard]] kor::GizmoPointer At(const kor::Vec2 position, const bool down, const bool pressed = false) const
        {
            return {.position = position, .viewport = viewport, .down = down, .pressed = pressed};
        }
    };
}

TEST(DebugDraw, ATranslateGizmoMovesAlongTheAxisItIsGrabbedBy) {
    kor::DebugDraw draw;
    const GizmoRig rig;
    kor::Mat4 transform(1.f);
    const kor::Vec2 center = rig.ToScreen({0, 0, 0}), x = rig.ScreenDirection({0, 0, 0}, {1, 0, 0});
    constexpr std::uint64_t id = 7;

    EXPECT_FALSE(draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + x * 300.f, false), {}, id));
    EXPECT_FALSE(draw.GizmoHovered()) << "nowhere near a handle";
    EXPECT_GT(draw.LineCount(), 0u) << "its handles are drawn";

    EXPECT_FALSE(draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + x * 60.f, false), {}, id));
    EXPECT_TRUE(draw.GizmoHovered()) << "over the X arrow";
    EXPECT_FALSE(draw.GizmoActive());

    draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + x * 60.f, true, true), {}, id);
    EXPECT_TRUE(draw.GizmoActive()) << "grabbed";
    // Dragged along X — and a little down, which an X arrow ignores.
    EXPECT_TRUE(draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection,
                           rig.At(center + x * 160.f + kor::Vec2(0.f, 40.f), true), {}, id));
    EXPECT_GT(transform[3].x, 0.5f);
    EXPECT_NEAR(transform[3].y, 0.f, 1e-4f);
    EXPECT_NEAR(transform[3].z, 0.f, 1e-4f);

    draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + x * 160.f, false), {}, id);
    EXPECT_FALSE(draw.GizmoActive()) << "let go of when the button is";
}

TEST(DebugDraw, ATranslateGizmoSnaps) {
    kor::DebugDraw draw;
    const GizmoRig rig;
    kor::Mat4 transform(1.f);
    const kor::Vec2 center = rig.ToScreen({0, 0, 0}), y = rig.ScreenDirection({0, 0, 0}, {0, 1, 0});
    const kor::GizmoOptions options {.snap = 0.5f};
    draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + y * 60.f, true, true), options, 1);
    ASSERT_TRUE(draw.GizmoActive());
    draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + y * 137.f, true), options, 1);
    EXPECT_GT(transform[3].y, 0.f);
    EXPECT_NEAR(std::fmod(transform[3].y, 0.5f), 0.f, 1e-4f) << "in steps of the snap: " << transform[3].y;
}

TEST(DebugDraw, ARotateGizmoTurnsAroundTheRingItIsGrabbedBy) {
    kor::DebugDraw draw;
    const GizmoRig rig;
    kor::Mat4 transform = kor::Translate(kor::Mat4(1.f), kor::Vec3(0.f));
    const kor::Vec2 center = rig.ToScreen({0, 0, 0});
    // The Z ring faces the camera: a circle of the gizmo's size on screen. Grabbed at 45 degrees, where
    // the X and Y rings (seen edge-on, as lines through the middle) are not.
    const kor::Vec2 grab = center + kor::Vec2(1.f, 1.f) * (100.f / std::sqrt(2.f));
    draw.Gizmo(kor::GizmoMode::eRotate, transform, rig.viewProjection, rig.At(grab, true, true), {}, 3);
    ASSERT_TRUE(draw.GizmoActive());
    // A quarter of a turn on screen, in two steps so the angle is followed.
    draw.Gizmo(kor::GizmoMode::eRotate, transform, rig.viewProjection, rig.At(center + kor::Vec2(0.f, 100.f), true), {}, 3);
    EXPECT_TRUE(draw.Gizmo(kor::GizmoMode::eRotate, transform, rig.viewProjection,
                           rig.At(center + kor::Vec2(-1.f, 1.f) * (100.f / std::sqrt(2.f)), true), {}, 3));
    EXPECT_NEAR(transform[0].x, 0.f, 1e-3f) << "X turned a quarter of the way around Z";
    EXPECT_NEAR(std::abs(transform[0].y), 1.f, 1e-3f);
    EXPECT_NEAR(transform[2].z, 1.f, 1e-4f) << "around Z, so Z stays";
    EXPECT_NEAR(kor::Length(kor::Vec3(transform[3])), 0.f, 1e-4f) << "and it turns in place";

    // The pointer went a quarter turn around the middle, from screen right towards screen down: X,
    // which pointed right, points down now — the turn follows the pointer, whichever way Z faces.
    const kor::Vec2 xOnScreen = rig.ScreenDirection({0, 0, 0}, kor::Vec3(transform[0]));
    EXPECT_GT(kor::Dot(xOnScreen, kor::Vec2(0.f, 1.f)), 0.9f) << "turned the way the pointer went";
}

TEST(DebugDraw, AScaleGizmoScalesTheAxisItIsGrabbedByOrAllOfThem) {
    kor::DebugDraw draw;
    const GizmoRig rig;
    kor::Mat4 transform(1.f);
    const kor::Vec2 center = rig.ToScreen({0, 0, 0}), x = rig.ScreenDirection({0, 0, 0}, {1, 0, 0});
    draw.Gizmo(kor::GizmoMode::eScale, transform, rig.viewProjection, rig.At(center + x * 80.f, true, true), {}, 4);
    ASSERT_TRUE(draw.GizmoActive());
    EXPECT_TRUE(draw.Gizmo(kor::GizmoMode::eScale, transform, rig.viewProjection, rig.At(center + x * 160.f, true), {}, 4));
    EXPECT_NEAR(transform[0].x, 2.f, 0.05f) << "twice as far along is twice the size";
    EXPECT_NEAR(transform[1].y, 1.f, 1e-5f);
    EXPECT_NEAR(transform[2].z, 1.f, 1e-5f);
    draw.Gizmo(kor::GizmoMode::eScale, transform, rig.viewProjection, rig.At(center, false), {}, 4);

    // The cube in the middle: all three.
    transform = kor::Mat4(1.f);
    draw.Gizmo(kor::GizmoMode::eScale, transform, rig.viewProjection, rig.At(center, true, true), {}, 5);
    ASSERT_TRUE(draw.GizmoActive());
    draw.Gizmo(kor::GizmoMode::eScale, transform, rig.viewProjection, rig.At(center + kor::Vec2(50.f, 0.f), true), {}, 5);
    EXPECT_NEAR(transform[0].x, 1.5f, 1e-4f);
    EXPECT_NEAR(transform[1].y, 1.5f, 1e-4f);
    EXPECT_NEAR(transform[2].z, 1.5f, 1e-4f);
}

TEST(DebugDraw, AGizmoIsNotGrabbedThroughAnInterfaceOrOffItsHandles) {
    kor::DebugDraw draw;
    const GizmoRig rig;
    kor::Mat4 transform(1.f);
    const kor::Vec2 center = rig.ToScreen({0, 0, 0}), x = rig.ScreenDirection({0, 0, 0}, {1, 0, 0});
    // No position: the pointer is something else's.
    draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, {.viewport = rig.viewport, .down = true, .pressed = true}, {}, 9);
    EXPECT_FALSE(draw.GizmoActive());
    draw.Gizmo(kor::GizmoMode::eTranslate, transform, rig.viewProjection, rig.At(center + x * 300.f, true, true), {}, 9);
    EXPECT_FALSE(draw.GizmoActive()) << "a click off every handle grabs nothing";
    EXPECT_EQ(transform, kor::Mat4(1.f));
}
