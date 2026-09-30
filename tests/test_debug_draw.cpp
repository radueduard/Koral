// Unit tests for debug drawing's shapes: how many lines each is, and how long they last. No device.

#include <gtest/gtest.h>

#include "debugDraw.h"

TEST(DebugDraw, EachShapeIsTheLinesItShouldBe) {
    kor::DebugDraw draw;
    const auto count = [&](auto&& shape) { draw.Clear(); shape(); return draw.LineCount(); };
    EXPECT_EQ(count([&] { draw.Line({0, 0, 0}, {1, 0, 0}); }), 1u);
    EXPECT_EQ(count([&] { draw.Box({0, 0, 0}, {1, 1, 1}); }), 12u);
    EXPECT_EQ(count([&] { draw.Box(glm::mat4(1.f)); }), 12u);
    EXPECT_EQ(count([&] { draw.Sphere({0, 0, 0}, 1.f, {}, 16); }), 48u);
    EXPECT_EQ(count([&] { draw.Arrow({0, 0, 0}, {0, 0, 1}); }), 5u);
    EXPECT_EQ(count([&] { draw.Point({0, 0, 0}); }), 3u);
    EXPECT_EQ(count([&] { draw.Axes(glm::mat4(1.f)); }), 15u);
    EXPECT_EQ(count([&] { draw.Grid({0, 0, 0}, 10.f, 4); }), 10u);
    EXPECT_EQ(count([&] { draw.Frustum(glm::mat4(1.f)); }), 12u);
    EXPECT_EQ(count([&] { draw.Arrow({1, 1, 1}, {1, 1, 1}); }), 1u) << "a zero-length arrow has no head";
}
