// A scene library the windowed tests load, open by name, unload and load again: the runtime's side of
// sceneLibrary.h exercised end to end.

#include <scene.h>
#include <sceneLibrary.h>

#include <atomic>
#include <string>

namespace
{
    // How many of this library's scenes are alive, readable from the test through the C symbol below.
    std::atomic<int> g_alive = 0;

    class PlainScene final : public kor::Scene {
    public:
        PlainScene() { ++g_alive; }
        ~PlainScene() override { --g_alive; }
        void Update() override { ++updates; }
        int updates = 0;
    };

    class ArgumentScene final : public kor::Scene {
    public:
        explicit ArgumentScene(const kor::SceneArgs& arguments) : level(arguments.Integer("level", -1)) { ++g_alive; }
        ~ArgumentScene() override { --g_alive; }
        void Initialize() override { Window::SetTitle("level " + std::to_string(level)); }
        long long level;
    };

    // What a scene keeps across a reload of this library.
    struct Progress {
        int frames = 0;
        std::string note;
    };
    KORAL_REFLECT(Progress, frames, note)

    class StatefulScene final : public kor::Scene {
    public:
        StatefulScene() { ++g_alive; }
        ~StatefulScene() override { --g_alive; }
        kor::Ref State() override { return progress; }
        void Initialize() override { framesAtInitialize = progress.frames; }
        void Update() override { ++progress.frames; }
        Progress progress;
        int framesAtInitialize = -1;
    };

}

extern "C" KORAL_SCENE_EXPORT_FN int KoralTestScenesAlive() { return g_alive; }

KORAL_SCENES(
    KORAL_SCENE("Library.Plain", PlainScene),
    KORAL_SCENE("Library.Arguments", ArgumentScene),
    KORAL_SCENE("Library.Stateful", StatefulScene),
)
