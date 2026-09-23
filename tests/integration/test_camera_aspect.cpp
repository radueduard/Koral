// Integration coverage for what a camera matches its aspect to, and for the way out of a captured
// cursor.
//
// Both need a device: an aspect source is a real kor::Image or kor::Framebuffer, and a camera's
// automaticUpdate() is what the repository would call each frame. What cannot be covered here is the
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
        .setFormat(kor::Image::Format::eRGBA8_UNORM)
        .setUsage(kor::Image::Usage::eColorAttachment)
        .setExtent(extent)
        .build();
}

TEST_F(CameraAspect, ACameraFollowingAnImageTakesItsShapeStraightAway)
{
    auto target = colorImage({ 800, 400 });
    ASSERT_TRUE(target);

    // At build, not on the first frame: a scene that renders before the repository has run once
    // would otherwise draw one frame at whatever the builder's default aspect was.
    const auto camera = PerspectiveCamera::Builder{}
        .setAspect(1.f)
        .followAspectOf(target)
        .build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->aspect(), 2.f);

    // And it follows: resizing the target is the whole point, since that is what a viewport does.
    target->Resize({ 400, 800, 1 });
    camera->automaticUpdate();
    EXPECT_FLOAT_EQ(camera->aspect(), 0.5f);
}

TEST_F(CameraAspect, ACameraCanFollowAWholeFramebufferInstead)
{
    auto color = colorImage({ 1200, 400 });
    ASSERT_TRUE(color);
    auto view = kor::ImageView::Builder(color).build();
    ASSERT_TRUE(view);
    auto framebuffer = kor::Framebuffer::Builder().addColor({ .view = view }).build();
    ASSERT_TRUE(framebuffer);

    const auto camera = PerspectiveCamera::Builder{}.followAspectOf(framebuffer).build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->aspect(), 3.f);
    EXPECT_EQ(camera->aspectSource().kind, AspectSource::Kind::eFramebuffer);

    framebuffer->Resize({ 400, 400 });
    camera->automaticUpdate();
    EXPECT_FLOAT_EQ(camera->aspect(), 1.f);
}

TEST_F(CameraAspect, FollowingTheWindowIsStillOneCall)
{
    const auto camera = PerspectiveCamera::Builder{}.setFollowWindowAspect().build();
    ASSERT_TRUE(camera);
    EXPECT_TRUE(camera->followsWindowAspect());
    EXPECT_EQ(camera->aspectSource().kind, AspectSource::Kind::eWindow);

    // And switching it off leaves the aspect alone rather than resetting it: the last thing it
    // followed is the shape the scene is currently drawing at.
    const float wasFollowing = camera->aspect();
    camera->setFollowWindowAspect(false);
    EXPECT_FALSE(camera->followsWindowAspect());
    EXPECT_FLOAT_EQ(camera->aspect(), wasFollowing);
}

TEST_F(CameraAspect, ASourceThatIsDestroyedLeavesTheAspectWhereItWas)
{
    auto target = colorImage({ 900, 300 });
    ASSERT_TRUE(target);

    const auto camera = PerspectiveCamera::Builder{}.followAspectOf(target).build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->aspect(), 3.f);

    target = { };   // the scene dropped what the camera was following

    EXPECT_TRUE(camera->aspectSource().dangling());
    EXPECT_FALSE(camera->aspectSource().extent().has_value());

    // Not a crash, and not a division by a zero extent either: the aspect simply stops moving. The
    // camera says so once, which is the difference between this and silently doing nothing.
    camera->automaticUpdate();
    camera->automaticUpdate();
    EXPECT_FLOAT_EQ(camera->aspect(), 3.f);
}

TEST_F(CameraAspect, PointingACameraAtSomethingElseRetargetsIt)
{
    auto first = colorImage({ 1000, 500 });
    auto second = colorImage({ 500, 1000 });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    const auto camera = PerspectiveCamera::Builder{}.followAspectOf(first).build();
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->aspect(), 2.f);

    camera->followAspectOf(second);
    EXPECT_FLOAT_EQ(camera->aspect(), 0.5f);

    // Nothing at all: the aspect is the scene's to set by hand again.
    camera->setAspectSource(AspectSource::none());
    camera->setAspect(1.25f);
    camera->automaticUpdate();
    EXPECT_FLOAT_EQ(camera->aspect(), 1.25f);
}

// The escape hatch, and the part of it a test can drive: GLFW has no way to inject a key press, but a
// binding that is *always* held is one, and it is the case an edge trigger exists for. Release only
// ever releases — holding it does not oscillate, and it does not take the cursor back either. That is
// what @ref Bindings::engage is for, and why they are two bindings rather than one pressed twice.
TEST_F(CameraAspect, ReleasingOnlyEverReleases)
{
    const auto camera = PerspectiveCamera::Builder{}
        .setReleased(false)   // in control from the first frame, so there is a grip to let go of
        .setController({
            .kind = Controller::Kind::eFly,
            // Engage unbound, so nothing can take the cursor back and the release is what is observed.
            .bindings = { .release = kcam::Input::always(), .engage = { } },
        })
        .build();
    ASSERT_TRUE(camera);
    EXPECT_FALSE(camera->released());

    camera->automaticUpdate();
    EXPECT_TRUE(camera->released());

    for (int i = 0; i < 8; ++i) camera->automaticUpdate();
    EXPECT_TRUE(camera->released());
}

// Nothing should take the user's pointer before the user has asked it to, so a camera begins parked
// whatever it was built with — and a scene that opens straight into play says so.
TEST_F(CameraAspect, ACameraStartsHavingLetGoOfTheCursor)
{
    const auto idle = PerspectiveCamera::Builder{}.build();
    ASSERT_TRUE(idle);
    EXPECT_TRUE(idle->released());

    const auto flying = PerspectiveCamera::Builder{}
        .setController({ .kind = Controller::Kind::eFly })
        .build();
    ASSERT_TRUE(flying);
    EXPECT_TRUE(flying->released()) << "a controller does not make a camera grab the pointer";

    const auto ortho = OrthographicCamera::Builder{}.build();
    ASSERT_TRUE(ortho);
    EXPECT_TRUE(ortho->released()) << "both kinds of camera start the same way";

    const auto straightIn = PerspectiveCamera::Builder{}
        .setController({ .kind = Controller::Kind::eFly })
        .setReleased(false)
        .build();
    ASSERT_TRUE(straightIn);
    EXPECT_FALSE(straightIn->released());
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
            .setController({
                .kind = Controller::Kind::eFly,
                .input = input,
                .bindings = { .release = { }, .engage = kcam::Input::always() },
            })
            .build();
    };

    const auto elsewhere = build(Controller::Input::eDisabled);
    ASSERT_TRUE(elsewhere);
    elsewhere->setReleased(true);
    elsewhere->automaticUpdate();
    EXPECT_TRUE(elsewhere->released()) << "a click somewhere else must not take the cursor back";

    const auto overTheScene = build(Controller::Input::eEnabled);
    ASSERT_TRUE(overTheScene);
    overTheScene->setReleased(true);
    overTheScene->automaticUpdate();
    EXPECT_FALSE(overTheScene->released());
}

// A scene releases the cursor itself when it opens a menu, and the state it writes is the same one
// the binding toggles — so the two cannot disagree about who has the pointer.
TEST_F(CameraAspect, ASceneCanLetTheCursorGoAndTakeItBack)
{
    const auto camera = PerspectiveCamera::Builder{}
        .setController({ .kind = Controller::Kind::eFly })
        .build();
    ASSERT_TRUE(camera);

    const glm::vec3 where = camera->position();

    camera->setReleased(true);
    EXPECT_TRUE(camera->released());

    // Parked: the controller reads nothing while released. Nothing is being pressed here either, so
    // what this really pins is that a released camera is not touched by the update at all.
    camera->automaticUpdate();
    EXPECT_EQ(camera->position(), where);
    EXPECT_TRUE(camera->released());

    camera->setReleased(false);
    EXPECT_FALSE(camera->released());
}

} // namespace
