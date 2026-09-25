// A scene library the windowed tests load, open by name, unload and load again: the runtime's side of
// sceneLibrary.h exercised end to end.

#include <scene.h>
#include <sceneLibrary.h>
#include <gui.h>   // one of its scenes has an interface, so the library registers its ImGui

#include <atomic>

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

    class InterfaceScene final : public kor::Scene {
    public:
        InterfaceScene() { ++g_alive; EnableInterface(); }
        ~InterfaceScene() override { --g_alive; }
        void RenderUI() override {
            ImGui::Begin("From a library");
            ImGui::Text("drawn by a scene an unloadable library made");
            ImGui::End();
        }
    };
}

extern "C" KORAL_SCENE_EXPORT_FN int KoralTestScenesAlive() { return g_alive; }

KORAL_SCENES(
    KORAL_SCENE("Library.Plain", PlainScene),
    KORAL_SCENE("Library.Arguments", ArgumentScene),
    KORAL_SCENE("Library.Interface", InterfaceScene),
)
