// Unit tests for input sources and bindings: names, parsing, and bindings saved as JSON.

#include <gtest/gtest.h>

#include "input.h"

using kor::InputSource;

TEST(InputSource, NamesEveryKindOfSourceAndReadsTheNameBack) {
    const std::vector<InputSource> sources = {
        kor::Key::eSpace, kor::Key::eW, kor::Key::eLeftShift, kor::MouseButton::eLeft, kor::MouseButton::e5,
        kor::GamepadButton::eA, kor::GamepadButton::eDpadUp, kor::GamepadAxis::eLeftX,
        InputSource(kor::GamepadAxis::eLeftY, -1.f), InputSource(kor::Key::eA, 0.5f),
    };
    for (const auto& source : sources) {
        const auto name = source.Name();
        const auto parsed = InputSource::Parse(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(*parsed, source) << name;
    }
    EXPECT_EQ(InputSource(kor::Key::eSpace).Name(), "Key.Space");
    EXPECT_EQ(InputSource(kor::GamepadButton::eA).Name(), "Gamepad.A");
    EXPECT_EQ(InputSource(kor::GamepadAxis::eLeftY, -1.f).Name(), "-GamepadAxis.LeftY");
    EXPECT_EQ(InputSource(kor::MouseButton::eRight).Name(), "Mouse.Right");
}

TEST(InputSource, RefusesANameThatNamesNothing) {
    EXPECT_FALSE(InputSource::Parse("Key.NoSuchKey"));
    EXPECT_FALSE(InputSource::Parse("Space"));
    EXPECT_FALSE(InputSource::Parse("Mouse.9"));
    EXPECT_FALSE(InputSource::Parse("Gamepad.Z"));
    EXPECT_FALSE(InputSource::Parse("Key.W*fast"));
}

TEST(InputBindings, AreSavedAndLoadedAsJsonAndSurviveTheTripThroughAnInput) {
    kor::Input input;
    input.BindAction("Jump", {kor::Key::eSpace, kor::GamepadButton::eA});
    input.BindAxis("MoveX", {{kor::Key::eD, 1.f}, {kor::Key::eA, -1.f}, kor::GamepadAxis::eLeftX});

    const std::string json = kor::ToJson(input.Bindings());
    EXPECT_NE(json.find("\"Key.Space\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"-Key.A\""), std::string::npos) << json;

    kor::InputBindings loaded;
    ASSERT_TRUE(kor::FromJson(loaded, json));
    kor::Input other;
    other.SetBindings(loaded);
    EXPECT_EQ(kor::ToJson(other.Bindings()), json) << "the same bindings, whichever way round";
}
