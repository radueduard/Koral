//
// Scenes, and the current one.
//

#include "scene.h"

#include <charconv>
#include <cctype>
#include <format>
#include <stdexcept>

#include "app.h"
#include "current.h"
#include "framebuffer.h"
#include "interface.h"

namespace kor
{
    // ---- the current scene ------------------------------------------------------------------------

    namespace
    {
        // Held as the life rather than the scene, so a scene destroyed while current on some thread
        // reads as gone instead of dangling.
        thread_local std::shared_ptr<detail::SceneLife> t_current;

        Scene& required(const char* what)
        {
            Scene* scene = Scene::Current();
            if (!scene)
                throw std::logic_error(std::format(
                    "{}:: used with no scene current on this thread. Inside a scene's hooks, its render "
                    "passes and the coroutines it starts, the scene is current; anywhere else, reach "
                    "the scene's own parts with SceneWindow(), SceneInput() and SceneTime().", what));
            return *scene;
        }
    }

    detail::SceneScope::SceneScope(Scene* scene)
        : _previous(std::exchange(t_current, scene ? scene->Life().lock() : nullptr)) {}

    detail::SceneScope::SceneScope(const std::shared_ptr<SceneLife>& life)
        : _previous(std::exchange(t_current, life)) {}

    detail::SceneScope::~SceneScope() { t_current = std::move(_previous); }

    std::shared_ptr<detail::SceneLife> detail::CurrentSceneLife() { return t_current; }

    Scene* Scene::Current() { return t_current ? t_current->scene : nullptr; }

    kor::Window* detail::CurrentWindowOrNull()
    {
        if (const Scene* scene = Scene::Current(); scene && scene->_window) return scene->_window;
        if (App::Exists()) {
            const auto scenes = App::Current().Scenes();
            if (!scenes.empty()) return &scenes.front()->SceneWindow();
        }
        return nullptr;
    }

    kor::Window& detail::CurrentWindow(const char* what)
    {
        if (auto* window = CurrentWindowOrNull()) return *window;
        throw std::logic_error(std::format("{} needs a window, and there is none: no scene is current and none is open", what));
    }

    // ---- the scene --------------------------------------------------------------------------------

    Scene::Scene()
        : _input(std::make_unique<kor::Input>()),
          _life(std::make_shared<detail::SceneLife>(detail::SceneLife{this}))
    {
        _graph._scene = this;
    }

    Scene::~Scene()
    {
        // From here on the scene is gone for anyone holding its life — a coroutine, a camera.
        _life->scene = nullptr;
        // Before the input it forwards to, which is destroyed after it anyway; explicit, so the order
        // does not depend on where the members happen to be declared.
        _interface.reset();
    }

    kor::Window& Scene::SceneWindow() const
    {
        if (!_window) throw std::logic_error("Scene::SceneWindow before the scene was opened: its window arrives before Initialize");
        return *_window;
    }

    void Scene::EnableInterface(InterfaceSettings settings)
    {
        if (_interface) return;
        // From the constructor there is no window yet: the application makes the interface when it
        // opens the scene. From Initialize on, it is made here and now.
        if (!_window) {
            _interfaceRequest = std::move(settings);
            return;
        }
        if (settings.iniFile.empty() && App::Exists() && !App::Current().Settings().interfaceDirectory.empty()) {
            std::string file = "imgui." + _name + ".ini";
            for (auto& c : file) if (c == '/' || c == '\\' || c == ':') c = '_';
            settings.iniFile = App::Current().Settings().interfaceDirectory / file;
        }
        _interface = std::make_unique<Interface>(*this, settings);
    }

    // ---- arguments --------------------------------------------------------------------------------

    SceneArgs& SceneArgs::Set(std::string key, const double value)
    {
        return Set(std::move(key), std::format("{}", value));
    }

    std::string SceneArgs::String(const std::string_view key, std::string fallback) const
    {
        const auto it = _values.find(std::string(key));
        return it == _values.end() ? std::move(fallback) : it->second;
    }

    double SceneArgs::Number(const std::string_view key, const double fallback) const
    {
        const auto it = _values.find(std::string(key));
        if (it == _values.end()) return fallback;
        double value = 0.0;
        const auto& text = it->second;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() ? value : fallback;
    }

    long long SceneArgs::Integer(const std::string_view key, const long long fallback) const
    {
        const auto it = _values.find(std::string(key));
        if (it == _values.end()) return fallback;
        long long value = 0;
        const auto& text = it->second;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() ? value : fallback;
    }

    bool SceneArgs::Flag(const std::string_view key, const bool fallback) const
    {
        const auto it = _values.find(std::string(key));
        if (it == _values.end()) return fallback;
        std::string text = it->second;
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text == "true" || text == "1" || text == "yes" || text == "on";
    }

    // ---- Window:: ---------------------------------------------------------------------------------

    kor::Window& Scene::Window::Get() { return required("Window").SceneWindow(); }
    glm::uvec2 Scene::Window::Extent() { return Get().Extent(); }
    bool Scene::Window::HasResized() { return Get().HasResized(); }
    bool Scene::Window::IsPaused() { return Get().IsPaused(); }
    bool Scene::Window::IsFocused() { return Get().IsFocused(); }
    const std::string& Scene::Window::Title() { return Get().Title(); }
    void Scene::Window::SetTitle(const std::string& title) { Get().SetTitle(title); }
    void Scene::Window::SetIcon(const std::filesystem::path& icon) { Get().SetIcon(icon); }
    void Scene::Window::Close() { Get().Close(); }
    ResourceRef<Framebuffer> Scene::Window::DefaultFramebuffer() { return Get().DefaultFramebuffer(); }

    // ---- Input:: ----------------------------------------------------------------------------------

    kor::Input& Scene::Input::Get() { return required("Input").SceneInput(); }
    KeyState Scene::Input::StateOf(const Key key) { return Get().StateOf(key); }
    KeyState Scene::Input::MouseButtonState(const MouseButton button) { return Get().MouseButtonState(button); }
    bool Scene::Input::IsKeyPressed(const Key key) { return Get().IsKeyPressed(key); }
    bool Scene::Input::IsKeyHeld(const Key key) { return Get().IsKeyHeld(key); }
    bool Scene::Input::IsKeyReleased(const Key key) { return Get().IsKeyReleased(key); }
    bool Scene::Input::IsMouseButtonPressed(const MouseButton button) { return Get().IsMouseButtonPressed(button); }
    bool Scene::Input::IsMouseButtonHeld(const MouseButton button) { return Get().IsMouseButtonHeld(button); }
    bool Scene::Input::IsMouseButtonReleased(const MouseButton button) { return Get().IsMouseButtonReleased(button); }
    std::optional<Key> Scene::Input::FirstKeyPressed() { return Get().FirstKeyPressed(); }
    std::optional<MouseButton> Scene::Input::FirstMouseButtonPressed() { return Get().FirstMouseButtonPressed(); }
    bool Scene::Input::InterfaceWantsMouse() { return Get().InterfaceWantsMouse(); }
    bool Scene::Input::InterfaceWantsKeyboard() { return Get().InterfaceWantsKeyboard(); }
    const glm::vec2& Scene::Input::MousePosition() { return Get().MousePosition(); }
    const glm::vec2& Scene::Input::MousePositionDelta() { return Get().MousePositionDelta(); }
    const glm::vec2& Scene::Input::MouseScrollDelta() { return Get().MouseScrollDelta(); }
    const glm::vec2& Scene::Input::LastMousePosition() { return Get().LastMousePosition(); }
    void Scene::Input::SetCursorMode(const CursorMode mode) { Get().SetCursorMode(mode); }
    Scene::Input::CursorMode Scene::Input::CurrentCursorMode() { return Get().CurrentCursorMode(); }

    // ---- Time:: -----------------------------------------------------------------------------------

    kor::Time& Scene::Time::Get() { return required("Time").SceneTime(); }
    float Scene::Time::FrameTime() { return Get().FrameTime(); }
    float Scene::Time::UnscaledFrameTime() { return Get().UnscaledFrameTime(); }
    float Scene::Time::FixedDeltaTime() { return Get().FixedDeltaTime(); }
    void Scene::Time::SetFixedDeltaTime(const float seconds) { Get().SetFixedDeltaTime(seconds); }
    float Scene::Time::FixedStepFraction() { return Get().FixedStepFraction(); }
    float Scene::Time::Elapsed() { return Get().Elapsed(); }
    std::uint64_t Scene::Time::FrameCount() { return Get().FrameCount(); }
    float Scene::Time::TimeScale() { return Get().TimeScale(); }
    void Scene::Time::SetTimeScale(const float scale) { Get().SetTimeScale(scale); }
}
