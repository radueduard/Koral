// Unit tests for kor::Flags<Enum> — a small type-safe bitset over a scoped enum.

#include <gtest/gtest.h>

#include "flags.h"

using kor::Flags;

namespace {

enum class Bit : uint32_t {
    A = 1u << 0,
    B = 1u << 1,
    C = 1u << 2,
    D = 1u << 3,
};

TEST(Flags, DefaultIsEmpty) {
    Flags<Bit> f;
    EXPECT_EQ(f.value(), 0u);
    EXPECT_FALSE(f & Bit::A);
}

TEST(Flags, ConstructFromSingleEnum) {
    Flags<Bit> f(Bit::B);
    EXPECT_EQ(f.value(), static_cast<uint32_t>(Bit::B));
    EXPECT_TRUE(f & Bit::B);
    EXPECT_FALSE(f & Bit::A);
}

TEST(Flags, OrCombinesBits) {
    Flags<Bit> f = Flags<Bit>(Bit::A) | Bit::C;
    EXPECT_TRUE(f & Bit::A);
    EXPECT_TRUE(f & Bit::C);
    EXPECT_FALSE(f & Bit::B);
    EXPECT_EQ(f.value(),
              static_cast<uint32_t>(Bit::A) | static_cast<uint32_t>(Bit::C));
}

TEST(Flags, OrAssignWithEnumAndFlags) {
    Flags<Bit> f;
    f |= Bit::A;
    f |= Bit::B;
    EXPECT_TRUE((f & Bit::A) && (f & Bit::B));

    Flags<Bit> g;
    g |= (Flags<Bit>(Bit::C) | Bit::D);
    EXPECT_TRUE((g & Bit::C) && (g & Bit::D));
    EXPECT_FALSE(g & Bit::A);
}

TEST(Flags, OrOfTwoFlagObjects) {
    Flags<Bit> f = Flags<Bit>(Bit::A) | Flags<Bit>(Bit::B);
    EXPECT_EQ(f.value(),
              static_cast<uint32_t>(Bit::A) | static_cast<uint32_t>(Bit::B));
}

TEST(Flags, AndAssignMasksBits) {
    // operator&= keeps only the bits present in the operand.
    Flags<Bit> f = Flags<Bit>(Bit::A) | Bit::B | Bit::C;
    f &= Bit::B;
    EXPECT_EQ(f.value(), static_cast<uint32_t>(Bit::B));
    EXPECT_FALSE(f & Bit::A);
    EXPECT_TRUE(f & Bit::B);
}

TEST(Flags, Equality) {
    Flags<Bit> f = Flags<Bit>(Bit::A) | Bit::B;
    Flags<Bit> g = Flags<Bit>(Bit::B) | Bit::A; // order independent
    Flags<Bit> h = Flags<Bit>(Bit::A);
    EXPECT_EQ(f, g);
    EXPECT_NE(f, h);
}

TEST(Flags, ConvertsToUnderlyingType) {
    Flags<Bit> f = Flags<Bit>(Bit::A) | Bit::D;
    const uint32_t raw = f; // implicit conversion
    EXPECT_EQ(raw, static_cast<uint32_t>(Bit::A) | static_cast<uint32_t>(Bit::D));
}

TEST(Flags, AbsentBitTestIsFalse) {
    Flags<Bit> f(Bit::A);
    EXPECT_FALSE(f & Bit::B);
    EXPECT_FALSE(f & Bit::C);
    EXPECT_FALSE(f & Bit::D);
}

} // namespace
