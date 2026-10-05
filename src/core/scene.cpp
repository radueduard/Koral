//
// Scenes, and the current one.
//

#include "scene.h"

#include <algorithm>

#include <charconv>
#include <cctype>
#include <format>
#include <stdexcept>

#include "app.h"
#include "current.h"
#include "framebuffer.h"
#include "parseNumber.h"

namespace kor
{
    // ---- the current scene ------------------------------------------------------------------------

    namespace
    {
        // Held as the life rather than the scene, so a scene destroyed while current on some thread
        // reads as gone instead of dangling.
        thread_local std::shared_ptr<detail::SceneLife> t_current;
        thread_local kor::Window* t_window = nullptr;

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

    detail::WindowScope::WindowScope(kor::Window* window)
    {
        if (!window) return;
        _previous = std::exchange(t_window, window);
        _set = true;
    }

    detail::WindowScope::~WindowScope() { if (_set) t_window = _previous; }

    kor::Window* detail::CurrentWindowOverride() { return t_window; }

    Scene* Scene::Current() { return t_current ? t_current->scene : nullptr; }

    kor::Window* detail::CurrentWindowOrNull()
    {
        if (t_window) return t_window;
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
    }

    kor::Window& Scene::SceneWindow() const
    {
        if (!_window) throw std::logic_error("Scene::SceneWindow before the scene was opened: its window arrives before Initialize");
        return *_window;
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
        return detail::ParseFloating<double>(it->second).value_or(fallback);
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

    kor::Window& Scene::Window::Get()
    {
        // Inside a view's passes, the view's target: its size is the one they draw at.
        if (auto* window = detail::CurrentWindowOverride()) return *window;
        return required("Window").SceneWindow();
    }
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
    bool Scene::Input::IsGamepadConnected(const int pad) { return Get().IsGamepadConnected(pad); }
    KeyState Scene::Input::GamepadButtonState(const GamepadButton button, const int pad) { return Get().GamepadButtonState(button, pad); }
    bool Scene::Input::IsGamepadButtonPressed(const GamepadButton button, const int pad) { return Get().IsGamepadButtonPressed(button, pad); }
    bool Scene::Input::IsGamepadButtonHeld(const GamepadButton button, const int pad) { return Get().IsGamepadButtonHeld(button, pad); }
    float Scene::Input::GamepadAxisValue(const GamepadAxis axis, const int pad) { return Get().GamepadAxisValue(axis, pad); }
    void Scene::Input::BindAction(std::string action, std::vector<InputSource> sources) { Get().BindAction(std::move(action), std::move(sources)); }
    void Scene::Input::BindAxis(std::string axis, std::vector<InputSource> sources) { Get().BindAxis(std::move(axis), std::move(sources)); }
    KeyState Scene::Input::ActionState(const std::string_view action) { return Get().ActionState(action); }
    bool Scene::Input::IsActionPressed(const std::string_view action) { return Get().IsActionPressed(action); }
    bool Scene::Input::IsActionHeld(const std::string_view action) { return Get().IsActionHeld(action); }
    bool Scene::Input::IsActionReleased(const std::string_view action) { return Get().IsActionReleased(action); }
    float Scene::Input::Axis(const std::string_view axis) { return Get().Axis(axis); }
    glm::vec2 Scene::Input::Axis2D(const std::string_view x, const std::string_view y) { return Get().Axis2D(x, y); }

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

    // ---- Debug:: ------------------------------------------------------------------------------------

    kor::DebugDraw& Scene::Debug::Get() { return required("Debug").SceneDebug(); }
    void Scene::Debug::Line(const glm::vec3 from, const glm::vec3 to, const Style& style) { Get().Line(from, to, style); }
    void Scene::Debug::Box(const glm::vec3 min, const glm::vec3 max, const Style& style) { Get().Box(min, max, style); }
    void Scene::Debug::Box(const glm::mat4& transform, const Style& style) { Get().Box(transform, style); }
    void Scene::Debug::Circle(const glm::vec3 center, const glm::vec3 normal, const float radius, const Style& style) { Get().Circle(center, normal, radius, style); }
    void Scene::Debug::Sphere(const glm::vec3 center, const float radius, const Style& style) { Get().Sphere(center, radius, style); }
    void Scene::Debug::Arrow(const glm::vec3 from, const glm::vec3 to, const Style& style) { Get().Arrow(from, to, style); }
    void Scene::Debug::Point(const glm::vec3 position, const float size, const Style& style) { Get().Point(position, size, style); }
    void Scene::Debug::Axes(const glm::mat4& transform, const float size, const float duration) { Get().Axes(transform, size, duration); }
    void Scene::Debug::Grid(const glm::vec3 center, const float size, const int cells, const Style& style) { Get().Grid(center, size, cells, style); }
    void Scene::Debug::Triangle(const glm::vec3 a, const glm::vec3 b, const glm::vec3 c, const Style& style) { Get().Triangle(a, b, c, style); }
    void Scene::Debug::Quad(const glm::vec3 a, const glm::vec3 b, const glm::vec3 c, const glm::vec3 d, const Style& style) { Get().Quad(a, b, c, d, style); }
    void Scene::Debug::Plane(const glm::vec3 center, const glm::vec3 normal, const glm::vec2 size, const Style& style) { Get().Plane(center, normal, size, style); }
    void Scene::Debug::Cylinder(const glm::vec3 from, const glm::vec3 to, const float radius, const Style& style) { Get().Cylinder(from, to, radius, style); }
    void Scene::Debug::Cone(const glm::vec3 base, const glm::vec3 tip, const float radius, const Style& style) { Get().Cone(base, tip, radius, style); }
    void Scene::Debug::Capsule(const glm::vec3 from, const glm::vec3 to, const float radius, const Style& style) { Get().Capsule(from, to, radius, style); }
    void Scene::Debug::Camera(const glm::mat4& view, const glm::mat4& projection, const float size, const Style& style) { Get().Camera(view, projection, size, style); }
    void Scene::Debug::PointLight(const glm::vec3 position, const float range, const Style& style) { Get().PointLight(position, range, style); }
    void Scene::Debug::SpotLight(const glm::vec3 position, const glm::vec3 direction, const float range, const float outerAngle,
                                 const float innerAngle, const Style& style)
    {
        Get().SpotLight(position, direction, range, outerAngle, innerAngle, style);
    }
    void Scene::Debug::DirectionalLight(const glm::vec3 position, const glm::vec3 direction, const float size, const Style& style)
    {
        Get().DirectionalLight(position, direction, size, style);
    }
    bool Scene::Debug::Gizmo(const GizmoMode mode, glm::mat4& transform, const glm::mat4& viewProjection, const GizmoOptions& options,
                             const std::uint64_t id)
    {
        kor::DebugDraw& draw = Get();
        const kor::Input& input = Input::Get();
        GizmoPointer pointer {.viewport = glm::vec2(Window::Extent()),
                              .down = input.IsMouseButtonHeld(MouseButton::eLeft),
                              .pressed = input.IsMouseButtonPressed(MouseButton::eLeft)};
        if (!input.InterfaceWantsMouse() || draw.GizmoActive()) pointer.position = input.MousePosition();
        else pointer.down = pointer.pressed = false;
        return draw.Gizmo(mode, transform, viewProjection, pointer, options, id);
    }
    bool Scene::Debug::GizmoActive() { return Get().GizmoActive(); }
    bool Scene::Debug::GizmoHovered() { return Get().GizmoHovered(); }
    void Scene::Debug::Frustum(const glm::mat4& viewProjection, const Style& style) { Get().Frustum(viewProjection, style); }

    // ---- state ---------------------------------------------------------------------------------------

    std::string Scene::SaveState()
    {
        const Ref state = State();
        return state.Valid() ? ToJson(state) : std::string("null");
    }

    VoidResult Scene::LoadState(const std::string_view json)
    {
        const Ref state = State();
        if (!state.Valid() || json.empty() || json == "null") return {};
        return FromJson(state, json);
    }

    // ---- views --------------------------------------------------------------------------------------

    View::View(Scene& scene, std::string name, const OffscreenSettings& target)
        : _name(std::move(name))
    {
        OffscreenSettings settings = target;
        if (settings.title == OffscreenSettings{}.title) settings.title = scene.Name() + " / " + _name;
        _window.reset(new kor::Window(settings));
        _graph._scene = &scene;
        _graph._target = _window.get();
    }

    View::~View() = default;

    ResourceRef<const kor::Image> View::Image() const { return _window->Image(); }

    void View::Resize(const glm::uvec2 extent) { _window->Resize(extent); }

    View& Scene::AddView(std::string name, const OffscreenSettings& target)
    {
        std::erase_if(_views, [&](const auto& view) { return view->Name() == name; });
        return *_views.emplace_back(new View(*this, std::move(name), target));
    }

    void Scene::RemoveView(const std::string_view name)
    {
        std::erase_if(_views, [&](const auto& view) { return view->Name() == name; });
    }

    View* Scene::FindView(const std::string_view name)
    {
        const auto it = std::ranges::find_if(_views, [&](const auto& view) { return view->Name() == name; });
        return it == _views.end() ? nullptr : it->get();
    }
}
