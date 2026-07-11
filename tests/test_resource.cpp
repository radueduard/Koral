// Unit tests for gfx::Resource<T> / gfx::ResourceRef<T> — the move-only owning
// handle and its lifetime-tracked, dangling-detecting reference. Pure CPU logic.

#include <gtest/gtest.h>

#include <stdexcept>

#include "resource.h"

using namespace gfx;

namespace {

struct Foo {
    int x = 0;
    explicit Foo(int v) : x(v) {}
};

struct Base {
    virtual ~Base() = default;
    virtual int tag() const { return 1; }
};
struct Derived : Base {
    int tag() const override { return 2; }
};

// -----------------------------------------------------------------------------
// Resource<T>: ownership, access, move-only, reset.
// -----------------------------------------------------------------------------
TEST(Resource, MakeAndAccess) {
    Resource<Foo> r = MakeResource<Foo>(5);
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r->x, 5);
    (*r).x = 7;
    EXPECT_EQ(r->x, 7);
}

TEST(Resource, DefaultConstructedIsEmpty) {
    Resource<Foo> r;
    EXPECT_FALSE(static_cast<bool>(r));
    Resource<Foo> n(nullptr);
    EXPECT_FALSE(static_cast<bool>(n));
}

TEST(Resource, MoveTransfersOwnership) {
    Resource<Foo> a = MakeResource<Foo>(11);
    Resource<Foo> b = std::move(a);
    EXPECT_TRUE(static_cast<bool>(b));
    EXPECT_EQ(b->x, 11);
    EXPECT_FALSE(static_cast<bool>(a)); // moved-from is empty
}

TEST(Resource, ResetClearsOwnership) {
    Resource<Foo> r = MakeResource<Foo>(1);
    r.reset();
    EXPECT_FALSE(static_cast<bool>(r));
}

// -----------------------------------------------------------------------------
// ResourceRef<T>: valid access while the owner lives.
// -----------------------------------------------------------------------------
TEST(ResourceRef, TracksLiveResource) {
    Resource<Foo> r = MakeResource<Foo>(5);
    ResourceRef<Foo> ref = r;
    EXPECT_TRUE(static_cast<bool>(ref));
    EXPECT_EQ(ref->x, 5);
    ref->x = 9; // writes through to the owned object
    EXPECT_EQ(r->x, 9);
}

// The core safety guarantee: once the owning Resource is destroyed, the ref
// reports invalid and throws on dereference instead of reading freed memory.
TEST(ResourceRef, DefaultConstructedIsEmptyAndThrowsOnDeref) {
    ResourceRef<Foo> ref; // default-constructible: empty, no lifetime
    EXPECT_FALSE(static_cast<bool>(ref));
    EXPECT_THROW((void)*ref, std::runtime_error);
    EXPECT_THROW((void)ref->x, std::runtime_error);
}

TEST(ResourceRef, DetectsDanglingAfterOwnerDestroyed) {
    ResourceRef<Foo> ref; // default-construct, then rebind
    {
        Resource<Foo> r = MakeResource<Foo>(5);
        ref = r;
        ASSERT_TRUE(static_cast<bool>(ref));
    }
    EXPECT_FALSE(static_cast<bool>(ref));
    EXPECT_THROW((void)*ref, std::runtime_error);
    EXPECT_THROW((void)ref->x, std::runtime_error);
}

// -----------------------------------------------------------------------------
// const-qualification conversions.
// -----------------------------------------------------------------------------
TEST(ResourceRef, ConstRefFromResource) {
    Resource<Foo> r = MakeResource<Foo>(3);
    ResourceRef<const Foo> cref = r;
    EXPECT_EQ(cref->x, 3);
    EXPECT_TRUE(static_cast<bool>(cref));
}

TEST(ResourceRef, MutableRefConvertsToConstRef) {
    Resource<Foo> r = MakeResource<Foo>(4);
    ResourceRef<Foo> ref = r;
    ResourceRef<const Foo> cref = ref; // const-qualifying conversion
    EXPECT_EQ(cref->x, 4);
}

// -----------------------------------------------------------------------------
// Derived -> Base upcast.
// -----------------------------------------------------------------------------
TEST(ResourceRef, UpcastsDerivedToBase) {
    Resource<Derived> d = MakeResource<Derived>();
    ResourceRef<Base> b = d; // implicit upcast
    EXPECT_TRUE(static_cast<bool>(b));
    EXPECT_EQ(b->tag(), 2); // virtual dispatch reaches Derived
}

// -----------------------------------------------------------------------------
// "unsafe" refs: constructed from a raw pointer/reference with no lifetime
// tracking. They must remain dereferenceable (no throw) despite having no stamp.
// -----------------------------------------------------------------------------
TEST(ResourceRef, UnsafeRefFromPointerDoesNotThrow) {
    Foo foo(42);
    ResourceRef<Foo> u(&foo);
    EXPECT_NO_THROW({ EXPECT_EQ(u->x, 42); });
    u->x = 43;
    EXPECT_EQ(foo.x, 43);
}

// -----------------------------------------------------------------------------
// Poisoned resources: construction failed, and the resource carries the reason
// instead of the object. It is a value, not an exception — nothing throws.
// -----------------------------------------------------------------------------
TEST(Resource, PoisonedIsInvalidButCarriesItsError) {
    auto r = Resource<Foo>::failed(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "undeclared identifier 'colour'" },
        "shaders/forward.frag.glsl");

    EXPECT_FALSE(r.valid());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_TRUE(r.poisoned());
    ASSERT_NE(r.error(), nullptr);
    EXPECT_EQ(r.error()->code, ErrorCode::eShaderCompileFailed);
    EXPECT_EQ(r.name(), "shaders/forward.frag.glsl");
}

TEST(Resource, PoisonedIsDistinctFromEmpty) {
    Resource<Foo> empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_FALSE(empty.poisoned());     // nothing went wrong; there is just nothing here
    EXPECT_EQ(empty.error(), nullptr);

    auto bad = Resource<Foo>::failed(Error{ .code = ErrorCode::eBackend, .message = "out of memory" });
    EXPECT_FALSE(bad.valid());
    EXPECT_TRUE(bad.poisoned());        // something went wrong, and we know what
}

TEST(ResourceRef, PoisonPropagatesThroughRef) {
    auto bad = Resource<Foo>::failed(Error{ .code = ErrorCode::eBackend, .message = "boom" }, "foo");
    ResourceRef<const Foo> ref = bad;

    EXPECT_TRUE(ref.alive());      // the resource exists...
    EXPECT_TRUE(ref.poisoned());   // ...it is just unusable
    EXPECT_FALSE(ref.valid());
    EXPECT_EQ(ref.get(), nullptr);
    ASSERT_NE(ref.errorPtr(), nullptr);
    EXPECT_EQ(ref.errorPtr()->code, ErrorCode::eBackend);
}

// A poisoned resource must not throw on dereference: the whole point is that an
// unusable resource flows through the API as a value. get() reports the absence.
TEST(ResourceRef, PoisonedDerefDoesNotThrow) {
    auto bad = Resource<Foo>::failed(Error{ .code = ErrorCode::eBackend, .message = "boom" }, "foo");
    ResourceRef<const Foo> ref = bad;
    EXPECT_NO_THROW({ EXPECT_EQ(ref.get(), nullptr); });
}

// -----------------------------------------------------------------------------
// Repair. This is the property the whole state-block redesign exists to provide:
// a ref handed out while its resource was poisoned must observe the repair. If
// ResourceRef cached the object pointer (as it used to), every ref taken before
// the fix would be stuck on the old/absent object forever, and "fix the shader
// and the pipeline comes back" would be impossible.
// -----------------------------------------------------------------------------
TEST(Resource, RetryRepairsPoisonedResourceInPlace) {
    auto r = Resource<Foo>::failed(Error{ .code = ErrorCode::eShaderCompileFailed, .message = "broken" }, "foo");
    ASSERT_FALSE(r.valid());

    // Hand out a ref *while poisoned* — as a scene or a renderer would.
    ResourceRef<const Foo> ref = r;
    EXPECT_FALSE(ref.valid());

    bool fixed = false;
    r.setRebuild([&fixed]() -> Result<std::unique_ptr<Foo>> {
        if (!fixed) return fail(ErrorCode::eShaderCompileFailed, "still broken");
        return std::make_unique<Foo>(99);
    });

    // Still broken: the retry fails and the resource keeps its (refreshed) error.
    EXPECT_FALSE(r.retry());
    EXPECT_TRUE(r.poisoned());
    EXPECT_FALSE(ref.valid());

    // The user fixes the source and the retry succeeds.
    fixed = true;
    EXPECT_TRUE(r.retry());
    EXPECT_TRUE(r.valid());
    EXPECT_FALSE(r.poisoned());

    // The ref issued back when the resource was broken now sees the new object,
    // without having been re-issued. This is the load-bearing assertion.
    EXPECT_TRUE(ref.valid());
    ASSERT_NE(ref.get(), nullptr);
    EXPECT_EQ(ref->x, 99);
}

TEST(Resource, RepairBumpsGenerationSoDependentsCanNotice) {
    auto r = Resource<Foo>::failed(Error{ .code = ErrorCode::eBackend, .message = "broken" });
    const auto before = r.generation();

    r.setRebuild([]() -> Result<std::unique_ptr<Foo>> { return std::make_unique<Foo>(1); });
    ASSERT_TRUE(r.retry());

    // A consumer that recorded `before` at build time can see it must rebuild.
    EXPECT_GT(r.generation(), before);
}

TEST(Resource, PoisonedWithoutRebuilderIsUnrecoverable) {
    auto r = Resource<Foo>::failed(Error{ .code = ErrorCode::eBackend, .message = "out of memory" });
    EXPECT_FALSE(r.retry());   // nothing to retry with; a failed allocation is not fixable
    EXPECT_TRUE(r.poisoned());
}

// A moved-from Resource has no state block at all. Accessing it must report the misuse, not
// dereference a null state block and take the process down with it.
TEST(Resource, MovedFromIsInertNotFatal) {
    Resource<Foo> r = MakeResource<Foo>(5);
    Resource<Foo> moved = std::move(r);

    EXPECT_FALSE(r.valid());        // NOLINT(bugprone-use-after-move) — that is the point
    EXPECT_FALSE(r.poisoned());
    EXPECT_EQ(r.get(), nullptr);
    EXPECT_EQ(r.error(), nullptr);
    EXPECT_EQ(r.generation(), 0u);
    EXPECT_NO_THROW({ EXPECT_EQ(r.operator->(), nullptr); });
    EXPECT_FALSE(r.retry());

    EXPECT_TRUE(moved.valid());     // and the destination is unharmed
    EXPECT_EQ(moved->x, 5);
}

// A repair replaces the underlying object, so an upcast ref must re-read it
// rather than hold a pointer adjusted from the old one.
TEST(ResourceRef, UpcastRefSurvivesRepair) {
    auto r = Resource<Derived>::failed(Error{ .code = ErrorCode::eBackend, .message = "broken" });
    ResourceRef<const Base> base = r;
    EXPECT_FALSE(base.valid());

    r.setRebuild([]() -> Result<std::unique_ptr<Derived>> { return std::make_unique<Derived>(); });
    ASSERT_TRUE(r.retry());

    ASSERT_TRUE(base.valid());
    EXPECT_EQ(base->tag(), 2);   // virtual dispatch still reaches Derived
}

} // namespace
