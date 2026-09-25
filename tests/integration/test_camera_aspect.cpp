// Integration coverage for what a camera matches its aspect to, and for the way out of a captured
// cursor.
//
// Both need a device: an aspect source is a real kor::Image or kor::Framebuffer, and a camera's
// AutomaticUpdate() is what the repository would call each frame. What cannot be covered here is the
// *input* half — GLFW has no way to inject a key press, so a release binding on a key can only be
// exercised through the state it toggles. The binding that is always held is the exception, and it is
// also the one worth pinning: it is what a missing edge-trigger would break.

#include "gpu_fixture.h"

#include "framebuffer.h"
#include "image.h"
#include "imageView.h"

#include <koralCamera.h>

using kcam::AspectSource;
using kcam::Controller;
using kcam::OrthographicCamera;
using kcam::PerspectiveCamera;

namespace {

struct CameraAspect : GpuTest { };

kor::Resource<kor::Image> colorImage(const glm::uvec2 extent)
{
    return kor::Image::Builder()
        .SetFormat(kor::Image::Format::eRGBA8_UNORM)
        .SetUsage(kor::Image::Usage::eColorAttachment)
        .SetExtent(extent)
        .Build();
}

TEST_F(CameraAspect, ACameraFollowingAnImageTakesItsShapeStraightAway)
{
    auto target = colorImage({ 800, 400 });
    ASSERT_TRUE(target);

    // At build, not on the first frame: a scene that renders before the repository has run once
    // would otherwise draw one frame at whatever the builder's default aspect was.
    const auto camera = PerspectiveCamera::Builder{}
        .SetAspect(1.f)
        .FollowAspectOf(target)
        .Build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->Aspect(), 2.f);

    // And it follows: resizing the target is the whole point, since that is what a viewport does.
    target->Resize({ 400, 800, 1 });
    camera->AutomaticUpdate();
    EXPECT_FLOAT_EQ(camera->Aspect(), 0.5f);
}

TEST_F(CameraAspect, ACameraCanFollowAWholeFramebufferInstead)
{
    auto color = colorImage({ 1200, 400 });
    ASSERT_TRUE(color);
    auto view = kor::ImageView::Builder(color).Build();
    ASSERT_TRUE(view);
    auto framebuffer = kor::Framebuffer::Builder().AddColor({ .view = view }).Build();
    ASSERT_TRUE(framebuffer);

    const auto camera = PerspectiveCamera::Builder{}.FollowAspectOf(framebuffer).Build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->Aspect(), 3.f);
    EXPECT_EQ(camera->AspectSourceSettings().kind, AspectSource::Kind::eFramebuffer);

    framebuffer->Resize({ 400, 400 });
    camera->AutomaticUpdate();
    EXPECT_FLOAT_EQ(camera->Aspect(), 1.f);
}

TEST_F(CameraAspect, FollowingTheWindowIsStillOneCall)
{
    const auto camera = PerspectiveCamera::Builder{}.SetFollowWindowAspect().Build();
    ASSERT_TRUE(camera);
    EXPECT_TRUE(camera->FollowsWindowAspect());
    EXPECT_EQ(camera->AspectSourceSettings().kind, AspectSource::Kind::eWindow);

    // And switching it off leaves the aspect alone rather than resetting it: the last thing it
    // followed is the shape the scene is currently drawing at.
    const float wasFollowing = camera->Aspect();
    camera->SetFollowWindowAspect(false);
    EXPECT_FALSE(camera->FollowsWindowAspect());
    EXPECT_FLOAT_EQ(camera->Aspect(), wasFollowing);
}

TEST_F(CameraAspect, ASourceThatIsDestroyedLeavesTheAspectWhereItWas)
{
    auto target = colorImage({ 900, 300 });
    ASSERT_TRUE(target);

    const auto camera = PerspectiveCamera::Builder{}.FollowAspectOf(target).Build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->Aspect(), 3.f);

    target = { };   // the scene dropped what the camera was following

    EXPECT_TRUE(camera->AspectSourceSettings().Dangling());
    EXPECT_FALSE(camera->AspectSourceSettings().Extent().has_value());

    // Not a crash, and not a division by a zero extent either: the aspect simply stops moving. The
    // camera says so once, which is the difference between this and silently doing nothing.
    camera->AutomaticUpdate();
    camera->AutomaticUpdate();
    EXPECT_FLOAT_EQ(camera->Aspect(), 3.f);
}

TEST_F(CameraAspect, PointingACameraAtSomethingElseRetargetsIt)
{
    auto first = colorImage({ 1000, 500 });
    auto second = colorImage({ 500, 1000 });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    const auto camera = PerspectiveCamera::Builder{}.FollowAspectOf(first).Build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->Aspect(), 2.f);

    camera->FollowAspectOf(second);
    EXPECT_FLOAT_EQ(camera->Aspect(), 0.5f);

    // Nothing at all: the aspect is the scene's to set by hand again.
    camera->SetAspectSource(AspectSource::None());
    camera->SetAspect(1.25f);
    camera->AutomaticUpdate();
    EXPECT_FLOAT_EQ(camera->Aspect(), 1.25f);
}

// The escape hatch, and the part of it a test can drive: GLFW has no way to inject a key press, but a
// binding that is *always* held is one, and it is the case an edge trigger exists for. Release only
// ever releases — holding it does not oscillate, and it does not take the cursor back either. That is
// what @ref Bindings::engage is for, and why they are two bindings rather than one pressed twice.
TEST_F(CameraAspect, ReleasingOnlyEverReleases)
{
    const auto camera = PerspectiveCamera::Builder{}
        .SetReleased(false)   // in control from the first frame, so there is a grip to let go of
        .SetController({
            .kind = Controller::Kind::eFly,
            // Engage unbound, so nothing can take the cursor back and the release is what is observed.
            .bindings = { .release = kcam::Input::Always(), .engage = { } },
        })
        .Build();
    ASSERT_TRUE(camera);
    EXPECT_FALSE(camera->Released());

    camera->AutomaticUpdate();
    EXPECT_TRUE(camera->Released());

    for (int i = 0; i < 8; ++i) camera->AutomaticUpdate();
    EXPECT_TRUE(camera->Released());
}

// Nothing should take the user's pointer before the user has asked it to, so a camera begins parked
// whatever it was built with — and a scene that opens straight into play says so.
TEST_F(CameraAspect, ACameraStartsHavingLetGoOfTheCursor)
{
    const auto idle = PerspectiveCamera::Builder{}.Build();
    ASSERT_TRUE(idle);
    EXPECT_TRUE(idle->Released());

    const auto flying = PerspectiveCamera::Builder{}
        .SetController({ .kind = Controller::Kind::eFly })
        .Build();
    ASSERT_TRUE(flying);
    EXPECT_TRUE(flying->Released()) << "a controller does not make a camera grab the pointer";

    const auto ortho = OrthographicCamera::Builder{}.Build();
    ASSERT_TRUE(ortho);
    EXPECT_TRUE(ortho->Released()) << "both kinds of camera start the same way";

    const auto straightIn = PerspectiveCamera::Builder{}
        .SetController({ .kind = Controller::Kind::eFly })
        .SetReleased(false)
        .Build();
    ASSERT_TRUE(straightIn);
    EXPECT_FALSE(straightIn->Released());
}

// And engaging only ever engages — but not from anywhere: it counts where the controller is allowed
// to read the mouse, which is what "click the scene to take the cursor back" means. A click on a
// panel while released has to stay a click on that panel.
TEST_F(CameraAspect, EngagingTakesTheCursorBackOnlyWhereTheMouseIsTheControllersToRead)
{
    // Two cameras rather than one told to change its mind, and for a reason worth stating: engaging
    // is edge-triggered, so a button already down when the pointer arrives is not a click on the
    // scene. Changing `input` under one camera mid-press would test that instead.
    const auto build = [](const Controller::Input input) {
        return PerspectiveCamera::Builder{}
            .SetController({
                .kind = Controller::Kind::eFly,
                .input = input,
                .bindings = { .release = { }, .engage = kcam::Input::Always() },
            })
            .Build();
    };

    const auto elsewhere = build(Controller::Input::eDisabled);
    ASSERT_TRUE(elsewhere);
    elsewhere->SetReleased(true);
    elsewhere->AutomaticUpdate();
    EXPECT_TRUE(elsewhere->Released()) << "a click somewhere else must not take the cursor back";

    const auto overTheScene = build(Controller::Input::eEnabled);
    ASSERT_TRUE(overTheScene);
    overTheScene->SetReleased(true);
    overTheScene->AutomaticUpdate();
    EXPECT_FALSE(overTheScene->Released());
}

// A scene releases the cursor itself when it opens a menu, and the state it writes is the same one
// the binding toggles — so the two cannot disagree about who has the pointer.
TEST_F(CameraAspect, ASceneCanLetTheCursorGoAndTakeItBack)
{
    const auto camera = PerspectiveCamera::Builder{}
        .SetController({ .kind = Controller::Kind::eFly })
        .Build();
    ASSERT_TRUE(camera);

    const glm::vec3 where = camera->Position();

    camera->SetReleased(true);
    EXPECT_TRUE(camera->Released());

    // Parked: the controller reads nothing while released. Nothing is being pressed here either, so
    // what this really pins is that a released camera is not touched by the update at all.
    camera->AutomaticUpdate();
    EXPECT_EQ(camera->Position(), where);
    EXPECT_TRUE(camera->Released());

    camera->SetReleased(false);
    EXPECT_FALSE(camera->Released());
}

// ---- temporal jitter and the previous frame's matrix -------------------------------------------

TEST_F(CameraAspect, JitterMovesTheImageByLessThanAPixelAndChangesEveryFrame)
{
    auto target = colorImage({ 100, 50 });
    const auto camera = PerspectiveCamera::Builder{}.FollowAspectOf(target).Build();
    ASSERT_TRUE(camera);
    camera->SetJitter(true);

    std::vector<glm::vec2> seen;
    for (int frame = 0; frame < 16; ++frame) {
        camera->AutomaticUpdate();
        const glm::vec2 jitter = camera->Jitter();
        // Half a pixel either way, and a pixel is 2 / extent in NDC.
        EXPECT_LE(std::abs(jitter.x), 1.f / 100.f + 1e-6f) << "frame " << frame;
        EXPECT_LE(std::abs(jitter.y), 1.f / 50.f + 1e-6f) << "frame " << frame;

        // The whole image moves by it: a point lands jitter * w further along in clip space.
        const glm::vec4 point(0.3f, -0.2f, -5.f, 1.f);
        const glm::vec4 jittered = camera->ViewProjection() * point;
        const glm::vec4 plain = camera->UnjitteredViewProjection() * point;
        EXPECT_NEAR(jittered.x - plain.x, jitter.x * plain.w, 1e-5f);
        EXPECT_NEAR(jittered.y - plain.y, jitter.y * plain.w, 1e-5f);
        EXPECT_FLOAT_EQ(jittered.w, plain.w);
        seen.push_back(jitter);
    }
    for (int i = 1; i < 8; ++i) EXPECT_NE(seen[i], seen[i - 1]) << "frame " << i << " did not move";
    for (int i = 0; i < 8; ++i) EXPECT_EQ(seen[i], seen[i + 8]) << "an 8-sample sequence repeats";
}

TEST_F(CameraAspect, WithoutJitterTheProjectionIsLeftAlone)
{
    auto target = colorImage({ 100, 50 });
    const auto camera = PerspectiveCamera::Builder{}.FollowAspectOf(target).Build();
    camera->SetJitter(true);
    camera->AutomaticUpdate();
    camera->SetJitter(false);
    EXPECT_EQ(camera->Jitter(), glm::vec2(0.f)) << "switching it off takes the current offset away too";
    camera->AutomaticUpdate();
    EXPECT_EQ(camera->Jitter(), glm::vec2(0.f));
    EXPECT_EQ(camera->ViewProjection(), camera->UnjitteredViewProjection());
}

TEST_F(CameraAspect, ThePreviousViewProjectionIsWhatTheFrameBeforeWasDrawnWith)
{
    auto target = colorImage({ 100, 50 });
    const auto camera = PerspectiveCamera::Builder{}.FollowAspectOf(target).SetPosition({ 0.f, 0.f, 5.f }).Build();
    camera->SetJitter(true);

    camera->AutomaticUpdate();
    EXPECT_EQ(camera->PreviousViewProjection(), camera->UnjitteredViewProjection()) << "no frame before the first";
    const glm::mat4 first = camera->UnjitteredViewProjection();

    camera->SetPosition({ 1.f, 0.f, 5.f });   // moved between frames, as a scene would
    camera->AutomaticUpdate();
    EXPECT_EQ(camera->PreviousViewProjection(), first);
    EXPECT_NE(camera->PreviousViewProjection(), camera->UnjitteredViewProjection());
}

} // namespace
