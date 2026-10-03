//
// The C interface: the application, its scenes, and what a scene has — window, input, clock, debug lines.
//

#include <cstring>

#include <yyjson.h>

#include "capi.h"

#include "app.h"
#include "context.h"
#include "debugDraw.h"
#include "frameGraph.h"
#include "input.h"
#include "module.h"
#include "projectConfig.h"
#include "reflect.h"
#include "scene.h"
#include "window.h"

using namespace kor;
using namespace kor::capi;

struct KoralProject {
    ProjectConfig config;
    std::string gpu, interfaceDirectory, scene, title;
    std::vector<uint32_t> formats;
};

namespace
{
    std::unique_ptr<App> g_app;

    App& TheApp()
    {
        if (!g_app) throw std::runtime_error("there is no application: koral_app_create first");
        return *g_app;
    }

    Scene* SceneOf(KoralScene* scene) { return reinterpret_cast<Scene*>(scene); }
    KoralScene* HandleOf(Scene* scene) { return reinterpret_cast<KoralScene*>(scene); }

    // A handle kept past its scene — closed, or replaced by a reload — is refused, not dereferenced.
    Scene& OpenScene(KoralScene* scene)
    {
        if (!g_app || !g_app->IsOpen(SceneOf(scene))) throw std::runtime_error("the scene is not open");
        return *SceneOf(scene);
    }

    template<typename T, typename H>
    T& ObjectOf(H* handle, const char* what)
    {
        if (!handle) throw std::runtime_error(std::string("no ") + what + " was given");
        return *reinterpret_cast<T*>(handle);
    }

    Window& WindowOf(KoralWindow* w) { return ObjectOf<Window>(w, "window"); }
    Input& InputOf(KoralInput* i) { return ObjectOf<Input>(i, "input"); }
    Time& TimeOf(KoralTime* t) { return ObjectOf<Time>(t, "clock"); }
    DebugDraw& DebugOf(KoralDebugDraw* d) { return ObjectOf<DebugDraw>(d, "DebugDraw"); }
    View& ViewOf(KoralView* v) { return ObjectOf<View>(v, "view"); }

    SceneArgs ArgumentsOf(const char* json)
    {
        SceneArgs arguments;
        if (!json || !*json) return arguments;
        yyjson_doc* doc = yyjson_read(json, std::strlen(json), 0);
        if (!doc) throw std::runtime_error("the arguments are not JSON");
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (yyjson_is_obj(root)) {
            std::size_t index, max;
            yyjson_val *key, *value;
            yyjson_obj_foreach(root, index, max, key, value) {
                std::string text;
                if (yyjson_is_str(value)) text = yyjson_get_str(value);
                else if (yyjson_is_bool(value)) text = yyjson_get_bool(value) ? "true" : "false";
                else if (yyjson_is_num(value)) text = std::to_string(yyjson_get_num(value));
                arguments.Set(yyjson_get_str(key), text);
            }
        }
        yyjson_doc_free(doc);
        return arguments;
    }

    std::string JsonOf(const SceneArgs& arguments)
    {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        for (const auto& [key, value] : arguments.Values())
            yyjson_mut_obj_add_strcpy(doc, root, key.c_str(), value.c_str());
        char* text = yyjson_mut_write(doc, 0, nullptr);
        std::string out = text ? text : "{}";
        std::free(text);
        yyjson_mut_doc_free(doc);
        return out;
    }

    WindowSettings WindowSettingsOf(const KoralWindowSettings* s, const char* name)
    {
        WindowSettings out;
        if (name) out.title = name;
        if (!s) return out;
        if (s->title) out.title = s->title;
        if (s->extent[0] && s->extent[1]) out.extent = {s->extent[0], s->extent[1]};
        out.resizable = s->resizable;
        out.fullscreen = s->fullscreen;
        out.decorated = s->decorated;
        out.transparentFramebuffer = s->transparent_framebuffer;
        out.vsync = s->vsync;
        if (s->formats && s->format_count) {
            out.formats.clear();
            for (std::size_t i = 0; i < s->format_count; ++i) out.formats.push_back(static_cast<Window::Format>(s->formats[i]));
        }
        return out;
    }

    OffscreenSettings OffscreenSettingsOf(const KoralOffscreenSettings* s, const char* name)
    {
        OffscreenSettings out;
        if (name) out.title = name;
        if (!s) return out;
        if (s->title) out.title = s->title;
        if (s->extent[0] && s->extent[1]) out.extent = {s->extent[0], s->extent[1]};
        out.format = static_cast<Window::Format>(s->format);
        return out;
    }

    DebugStyle StyleOf(const KoralDebugStyle* s)
    {
        if (!s) return {};
        return {.color = {s->color[0], s->color[1], s->color[2], s->color[3]}, .duration = s->duration, .onTop = s->on_top};
    }

    glm::vec3 Vec3Of(const float* v) { return {v[0], v[1], v[2]}; }
    glm::mat4 Mat4Of(const float* v)
    {
        glm::mat4 m(1.f);
        if (v) std::memcpy(&m[0][0], v, sizeof(float) * 16);
        return m;
    }

    void Out2(const glm::vec2 v, float* x, float* y)
    {
        if (x) *x = v.x;
        if (y) *y = v.y;
    }

    std::vector<InputSource> SourcesOf(const KoralInputSource* sources, const std::size_t count)
    {
        std::vector<InputSource> out;
        for (std::size_t i = 0; i < count; ++i) {
            InputSource source;
            source.kind = static_cast<InputSource::Kind>(sources[i].kind);
            source.code = static_cast<std::uint16_t>(sources[i].code);
            source.scale = sources[i].scale;
            out.push_back(source);
        }
        return out;
    }

    // ---- a scene written as callbacks -------------------------------------------------------------------

    class CScene final : public Scene {
    public:
        explicit CScene(const KoralSceneCallbacks& callbacks) : _c(callbacks) {
            if (_c.interface) EnableInterface();
        }
        ~CScene() override { if (_c.destroy) _c.destroy(_c.user); }

        void Initialize() override { if (_c.initialize) _c.initialize(Handle(), _c.user); }
        void FixedUpdate() override { if (_c.fixed_update) _c.fixed_update(Handle(), _c.user); }
        void Update() override { if (_c.update) _c.update(Handle(), _c.user); }
        void LateUpdate() override { if (_c.late_update) _c.late_update(Handle(), _c.user); }
        void Render(CommandBuffer& commands) override {
            if (_c.render) _c.render(Handle(), reinterpret_cast<KoralCommandBuffer*>(&commands), _c.user);
        }
        void RenderUI() override { if (_c.render_ui) _c.render_ui(Handle(), _c.user); }
        void OnResize(const glm::uvec2 extent) override { if (_c.on_resize) _c.on_resize(Handle(), extent.x, extent.y, _c.user); }
        void OnSuspend() override { if (_c.on_suspend) _c.on_suspend(Handle(), _c.user); }
        void OnResume() override { if (_c.on_resume) _c.on_resume(Handle(), _c.user); }
        bool OnCloseRequested() override { return _c.on_close_requested ? _c.on_close_requested(Handle(), _c.user) : true; }
        void Shutdown() override { if (_c.shutdown) _c.shutdown(Handle(), _c.user); }

        std::string SaveState() override {
            if (!_c.save_state) return "null";
            const char* json = _c.save_state(_c.user);
            return json ? json : "null";
        }
        VoidResult LoadState(const std::string_view json) override {
            if (_c.load_state && json != "null" && !json.empty()) _c.load_state(std::string(json).c_str(), _c.user);
            return {};
        }

    private:
        KoralScene* Handle() { return HandleOf(this); }
        KoralSceneCallbacks _c;
    };

    bool Empty(const KoralSceneCallbacks& c)
    {
        return !c.user && !c.interface && !c.initialize && !c.fixed_update && !c.update && !c.late_update && !c.render
            && !c.render_ui && !c.on_resize && !c.on_suspend && !c.on_resume && !c.on_close_requested && !c.shutdown
            && !c.save_state && !c.load_state && !c.destroy;
    }

    KoralScene* Opened(Scene* scene, const char* name)
    {
        if (!scene) Fail(std::string("could not open '") + (name ? name : "") + "': see the log");
        return HandleOf(scene);
    }

    thread_local std::vector<std::string> t_names;
}

extern "C" {

// ---- settings ------------------------------------------------------------------------------------------

KoralAppSettings koral_app_settings_default(void)
{
    const AppSettings d;
    return {.api = static_cast<uint32_t>(d.api), .platform = static_cast<KoralPlatform>(d.platform), .frames_in_flight = d.framesInFlight,
            .gpu = nullptr, .interface_directory = nullptr};
}

KoralWindowSettings koral_window_settings_default(void)
{
    static const WindowSettings d;
    static std::vector<uint32_t> formats = [] {
        std::vector<uint32_t> out;
        for (const auto f : WindowSettings{}.formats) out.push_back(static_cast<uint32_t>(f));
        return out;
    }();
    return {.title = nullptr, .extent = {d.extent.x, d.extent.y}, .resizable = d.resizable, .fullscreen = d.fullscreen,
            .decorated = d.decorated, .transparent_framebuffer = d.transparentFramebuffer, .vsync = d.vsync,
            .formats = formats.data(), .format_count = formats.size()};
}

KoralOffscreenSettings koral_offscreen_settings_default(void)
{
    const OffscreenSettings d;
    return {.title = nullptr, .extent = {d.extent.x, d.extent.y}, .format = static_cast<uint32_t>(d.format)};
}

// ---- the application -------------------------------------------------------------------------------------

KoralStatus koral_app_create(const KoralAppSettings* settings)
{
    if (g_app) return Fail("there is already an application: one per process");
    return Guarded([&] {
        AppSettings s;
        if (settings) {
            s.api = static_cast<API>(settings->api);
            s.platform = static_cast<WindowPlatform>(settings->platform);
            if (settings->frames_in_flight) s.framesInFlight = settings->frames_in_flight;
            if (settings->gpu) s.gpu = settings->gpu;
            if (settings->interface_directory) s.interfaceDirectory = settings->interface_directory;
        }
        g_app = std::make_unique<App>(std::move(s));
        return KORAL_OK;
    }, KORAL_ERROR);
}

void koral_app_destroy(void) { GuardedVoid([] { g_app.reset(); }); }
bool koral_app_exists(void) { return g_app != nullptr; }

KoralStatus koral_app_register(const char* name, const KoralSceneFactory factory, void* factoryUser)
{
    if (!name || !factory) return Fail("koral_app_register needs a name and a factory");
    return Guarded([&] {
        TheApp().Register(name, [factory, factoryUser](const SceneArgs& arguments) -> std::unique_ptr<Scene> {
            const std::string json = JsonOf(arguments);
            const KoralSceneCallbacks callbacks = factory(json.c_str(), factoryUser);
            if (Empty(callbacks)) return nullptr;   // the factory could not make it, and has said why
            return std::make_unique<CScene>(callbacks);
        });
        return KORAL_OK;
    }, KORAL_ERROR);
}

} // extern "C"

namespace
{
    template<typename Body>
    KoralStatus Library(Body&& body)
    {
        return Guarded([&] {
            const auto done = body(TheApp());
            return done ? KORAL_OK : Fail(done.error().message);
        }, KORAL_ERROR);
    }
}

extern "C" {

KoralStatus koral_app_load_library(const char* path) { return Library([&](App& a) { return a.LoadLibrary(path ? path : ""); }); }
KoralStatus koral_app_unload_library(const char* path) { return Library([&](App& a) { return a.UnloadLibrary(path ? path : ""); }); }
KoralStatus koral_app_reload_library(const char* path) { return Library([&](App& a) { return a.ReloadLibrary(path ? path : ""); }); }
KoralStatus koral_app_reload_scenes(const char* const* names, const size_t count)
{
    return Library([&](App& a) {
        std::vector<std::string> list;
        for (std::size_t i = 0; i < count; ++i) if (names && names[i]) list.emplace_back(names[i]);
        return a.ReloadScenes(list);
    });
}
uint32_t koral_app_scene_name_count(void)
{
    return Guarded([] { t_names = TheApp().SceneNames(); return static_cast<uint32_t>(t_names.size()); }, 0u);
}
const char* koral_app_scene_name(const uint32_t index)
{
    return Guarded([&] {
        if (index >= t_names.size()) t_names = TheApp().SceneNames();
        return Keep(t_names.at(index));
    }, Keep(""));
}

KoralScene* koral_app_open(const char* name, const KoralWindowSettings* window, const char* arguments)
{
    return Guarded([&] { return Opened(TheApp().Open(name ? name : "", WindowSettingsOf(window, name), ArgumentsOf(arguments)), name); },
                   static_cast<KoralScene*>(nullptr));
}
KoralScene* koral_app_open_scene(const char* name, const KoralSceneCallbacks* scene, const KoralWindowSettings* window)
{
    if (!scene || Empty(*scene)) { Fail("koral_app_open_scene needs the scene's callbacks"); return nullptr; }
    return Guarded([&] {
        return Opened(TheApp().Open(name ? name : "", std::make_unique<CScene>(*scene), WindowSettingsOf(window, name)), name);
    }, static_cast<KoralScene*>(nullptr));
}
KoralScene* koral_app_open_offscreen(const char* name, const KoralOffscreenSettings* target, const char* arguments)
{
    return Guarded([&] {
        return Opened(TheApp().OpenOffscreen(name ? name : "", OffscreenSettingsOf(target, name), ArgumentsOf(arguments)), name);
    }, static_cast<KoralScene*>(nullptr));
}
KoralScene* koral_app_open_offscreen_scene(const char* name, const KoralSceneCallbacks* scene, const KoralOffscreenSettings* target)
{
    if (!scene || Empty(*scene)) { Fail("koral_app_open_offscreen_scene needs the scene's callbacks"); return nullptr; }
    return Guarded([&] {
        return Opened(TheApp().OpenOffscreen(name ? name : "", std::make_unique<CScene>(*scene), OffscreenSettingsOf(target, name)), name);
    }, static_cast<KoralScene*>(nullptr));
}
size_t koral_app_scenes(KoralScene** out, const size_t capacity)
{
    return Guarded([&] {
        const auto scenes = TheApp().Scenes();
        for (std::size_t i = 0; i < scenes.size() && i < capacity && out; ++i) out[i] = HandleOf(scenes[i]);
        return scenes.size();
    }, std::size_t{0});
}
bool koral_app_is_open(KoralScene* scene) { return g_app && g_app->IsOpen(SceneOf(scene)); }
int koral_app_run(void) { return Guarded([] { return TheApp().Run(); }, 1); }
bool koral_app_frame(void) { return Guarded([] { return TheApp().Frame(); }, false); }
void koral_app_quit(void) { GuardedVoid([] { TheApp().Quit(); }); }
void koral_app_replace(KoralScene* s, const char* name, const char* arguments)
{
    GuardedVoid([&] { TheApp().Replace(OpenScene(s), name ? name : "", ArgumentsOf(arguments)); });
}
void koral_app_push(KoralScene* s, const char* name, const char* arguments)
{
    GuardedVoid([&] { TheApp().Push(OpenScene(s), name ? name : "", ArgumentsOf(arguments)); });
}
void koral_app_pop(KoralScene* s) { GuardedVoid([&] { TheApp().Pop(OpenScene(s)); }); }
void koral_app_close(KoralScene* s) { GuardedVoid([&] { if (g_app && g_app->IsOpen(SceneOf(s))) g_app->Close(*SceneOf(s)); }); }

// ---- Navigator ---------------------------------------------------------------------------------------------

KoralScene* koral_navigator_open(const char* name, const KoralWindowSettings* window, const char* arguments)
{
    return Guarded([&] { return Opened(Navigator::Open(name ? name : "", WindowSettingsOf(window, name), ArgumentsOf(arguments)), name); },
                   static_cast<KoralScene*>(nullptr));
}
KoralScene* koral_navigator_open_offscreen(const char* name, const KoralOffscreenSettings* target, const char* arguments)
{
    return Guarded([&] {
        return Opened(Navigator::OpenOffscreen(name ? name : "", OffscreenSettingsOf(target, name), ArgumentsOf(arguments)), name);
    }, static_cast<KoralScene*>(nullptr));
}
void koral_navigator_replace(const char* name, const char* arguments) { GuardedVoid([&] { Navigator::Replace(name ? name : "", ArgumentsOf(arguments)); }); }
void koral_navigator_push(const char* name, const char* arguments) { GuardedVoid([&] { Navigator::Push(name ? name : "", ArgumentsOf(arguments)); }); }
void koral_navigator_pop(void) { GuardedVoid([] { Navigator::Pop(); }); }
void koral_navigator_close(void) { GuardedVoid([] { Navigator::Close(); }); }
void koral_navigator_quit(void) { GuardedVoid([] { Navigator::Quit(); }); }

// ---- Scene, View ---------------------------------------------------------------------------------------------

KoralScene* koral_scene_current(void) { return HandleOf(Scene::Current()); }
const char* koral_scene_name(KoralScene* s) { return Guarded([&] { return Keep(OpenScene(s).Name()); }, Keep("")); }
KoralFrameGraph* koral_scene_graph(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralFrameGraph*>(&OpenScene(s).Graph()); }, static_cast<KoralFrameGraph*>(nullptr));
}
KoralWindow* koral_scene_scene_window(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralWindow*>(&OpenScene(s).SceneWindow()); }, static_cast<KoralWindow*>(nullptr));
}
KoralInput* koral_scene_scene_input(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralInput*>(&OpenScene(s).SceneInput()); }, static_cast<KoralInput*>(nullptr));
}
KoralTime* koral_scene_scene_time(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralTime*>(&OpenScene(s).SceneTime()); }, static_cast<KoralTime*>(nullptr));
}
KoralDebugDraw* koral_scene_scene_debug(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralDebugDraw*>(&OpenScene(s).SceneDebug()); }, static_cast<KoralDebugDraw*>(nullptr));
}
bool koral_scene_has_interface(KoralScene* s) { return Guarded([&] { return OpenScene(s).HasInterface(); }, false); }
const char* koral_scene_save_state(KoralScene* s) { return Guarded([&] { return Keep(OpenScene(s).SaveState()); }, Keep("null")); }
KoralStatus koral_scene_load_state(KoralScene* s, const char* json)
{
    return Guarded([&] {
        const auto loaded = OpenScene(s).LoadState(json ? json : "null");
        return loaded ? KORAL_OK : Fail(loaded.error().message);
    }, KORAL_ERROR);
}
KoralView* koral_scene_add_view(KoralScene* s, const char* name, const KoralOffscreenSettings* target)
{
    return Guarded([&] { return reinterpret_cast<KoralView*>(&OpenScene(s).AddView(name ? name : "", OffscreenSettingsOf(target, name))); },
                   static_cast<KoralView*>(nullptr));
}
void koral_scene_remove_view(KoralScene* s, const char* name) { GuardedVoid([&] { OpenScene(s).RemoveView(name ? name : ""); }); }
KoralView* koral_scene_find_view(KoralScene* s, const char* name)
{
    return Guarded([&] { return reinterpret_cast<KoralView*>(OpenScene(s).FindView(name ? name : "")); }, static_cast<KoralView*>(nullptr));
}
uint32_t koral_scene_view_count(KoralScene* s) { return Guarded([&] { return static_cast<uint32_t>(OpenScene(s).Views().size()); }, 0u); }
KoralView* koral_scene_view(KoralScene* s, const uint32_t i)
{
    return Guarded([&] { return reinterpret_cast<KoralView*>(OpenScene(s).Views().at(i).get()); }, static_cast<KoralView*>(nullptr));
}

KoralSceneLife* koral_scene_life(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralSceneLife*>(new std::weak_ptr<detail::SceneLife>(OpenScene(s).Life())); },
                   static_cast<KoralSceneLife*>(nullptr));
}
KoralScene* koral_scene_life_scene(KoralSceneLife* life)
{
    if (!life) return nullptr;
    const auto alive = reinterpret_cast<std::weak_ptr<detail::SceneLife>*>(life)->lock();
    return alive ? HandleOf(alive->scene) : nullptr;
}
void koral_scene_life_release(KoralSceneLife* life) { delete reinterpret_cast<std::weak_ptr<detail::SceneLife>*>(life); }

KoralSceneScope* koral_scene_scope_enter(KoralScene* s)
{
    return Guarded([&] { return reinterpret_cast<KoralSceneScope*>(new detail::SceneScope(&OpenScene(s))); },
                   static_cast<KoralSceneScope*>(nullptr));
}
void koral_scene_scope_exit(KoralSceneScope* scope) { GuardedVoid([&] { delete reinterpret_cast<detail::SceneScope*>(scope); }); }

const char* koral_view_name(KoralView* v) { return Guarded([&] { return Keep(ViewOf(v).Name()); }, Keep("")); }
KoralFrameGraph* koral_view_graph(KoralView* v) { return Guarded([&] { return reinterpret_cast<KoralFrameGraph*>(&ViewOf(v).Graph()); }, static_cast<KoralFrameGraph*>(nullptr)); }
KoralWindow* koral_view_target(KoralView* v) { return Guarded([&] { return reinterpret_cast<KoralWindow*>(&ViewOf(v).Target()); }, static_cast<KoralWindow*>(nullptr)); }
KoralImage* koral_view_image(KoralView* v) { return Guarded([&] { return Borrow(ViewOf(v).Image()); }, static_cast<KoralResource*>(nullptr)); }
void koral_view_resize(KoralView* v, const uint32_t x, const uint32_t y) { GuardedVoid([&] { ViewOf(v).Resize({x, y}); }); }
bool koral_view_enabled(KoralView* v) { return Guarded([&] { return ViewOf(v).Enabled(); }, false); }
void koral_view_set_enabled(KoralView* v, const bool e) { GuardedVoid([&] { ViewOf(v).SetEnabled(e); }); }

KoralWindow* koral_current_window(void)
{
    return Guarded([] { return reinterpret_cast<KoralWindow*>(&Scene::Window::Get()); }, static_cast<KoralWindow*>(nullptr));
}
KoralInput* koral_current_input(void)
{
    return Guarded([] { return reinterpret_cast<KoralInput*>(&Scene::Input::Get()); }, static_cast<KoralInput*>(nullptr));
}
KoralTime* koral_current_time(void)
{
    return Guarded([] { return reinterpret_cast<KoralTime*>(&Scene::Time::Get()); }, static_cast<KoralTime*>(nullptr));
}
KoralDebugDraw* koral_current_debug(void)
{
    return Guarded([] { return reinterpret_cast<KoralDebugDraw*>(&Scene::Debug::Get()); }, static_cast<KoralDebugDraw*>(nullptr));
}

// ---- Window --------------------------------------------------------------------------------------------------

bool koral_window_should_close(KoralWindow* w) { return Guarded([&] { return WindowOf(w).ShouldClose(); }, false); }
void koral_window_close(KoralWindow* w) { GuardedVoid([&] { WindowOf(w).Close(); }); }
bool koral_window_is_offscreen(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsOffscreen(); }, false); }
KoralImage* koral_window_image(KoralWindow* w) { return Guarded([&] { return Borrow(WindowOf(w).Image()); }, static_cast<KoralResource*>(nullptr)); }
void koral_window_resize(KoralWindow* w, const uint32_t x, const uint32_t y) { GuardedVoid([&] { WindowOf(w).Resize({x, y}); }); }
void koral_window_extent(KoralWindow* w, uint32_t* x, uint32_t* y)
{
    const auto e = Guarded([&] { return WindowOf(w).Extent(); }, glm::uvec2(0));
    if (x) *x = e.x;
    if (y) *y = e.y;
}
bool koral_window_is_paused(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsPaused(); }, false); }
bool koral_window_is_resizable(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsResizable(); }, false); }
bool koral_window_is_fullscreen(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsFullscreen(); }, false); }
bool koral_window_is_decorated(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsDecorated(); }, false); }
bool koral_window_is_vsync(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsVSync(); }, false); }
bool koral_window_is_framebuffer_transparent(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsFramebufferTransparent(); }, false); }
uint32_t koral_window_pixel_format(KoralWindow* w) { return Guarded([&] { return static_cast<uint32_t>(WindowOf(w).PixelFormat()); }, 0u); }
void koral_window_pause(KoralWindow* w) { GuardedVoid([&] { WindowOf(w).Pause(); }); }
void koral_window_unpause(KoralWindow* w) { GuardedVoid([&] { WindowOf(w).Unpause(); }); }
void koral_window_set_title(KoralWindow* w, const char* title) { GuardedVoid([&] { WindowOf(w).SetTitle(title ? title : ""); }); }
const char* koral_window_title(KoralWindow* w) { return Guarded([&] { return Keep(WindowOf(w).Title()); }, Keep("")); }
KoralFramebuffer* koral_window_default_framebuffer(KoralWindow* w)
{
    return Guarded([&] { return BorrowWritable(WindowOf(w).DefaultFramebuffer()); }, static_cast<KoralResource*>(nullptr));
}
bool koral_window_has_resized(KoralWindow* w) { return Guarded([&] { return WindowOf(w).HasResized(); }, false); }
void koral_window_set_icon(KoralWindow* w, const char* path) { GuardedVoid([&] { WindowOf(w).SetIcon(path ? path : ""); }); }
bool koral_window_is_focused(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsFocused(); }, false); }
bool koral_window_is_shown_this_frame(KoralWindow* w) { return Guarded([&] { return WindowOf(w).IsShownThisFrame(); }, false); }

// ---- Input -----------------------------------------------------------------------------------------------------

bool koral_input_source_parse(const char* name, KoralInputSource* source)
{
    return Guarded([&] {
        const auto parsed = InputSource::Parse(name ? name : "");
        if (!parsed) return false;
        if (source) *source = {static_cast<uint32_t>(parsed->kind), parsed->code, parsed->scale};
        return true;
    }, false);
}
const char* koral_input_source_name(const KoralInputSource* source)
{
    return Guarded([&] { return Keep(SourcesOf(source, 1).front().Name()); }, Keep(""));
}
uint32_t koral_input_state_of(KoralInput* i, const uint32_t key) { return Guarded([&] { return static_cast<uint32_t>(InputOf(i).StateOf(static_cast<Key>(key))); }, 0u); }
uint32_t koral_input_mouse_button_state(KoralInput* i, const uint32_t b)
{
    return Guarded([&] { return static_cast<uint32_t>(InputOf(i).MouseButtonState(static_cast<MouseButton>(b))); }, 0u);
}
bool koral_input_first_key_pressed(KoralInput* i, uint32_t* key)
{
    return Guarded([&] {
        const auto k = InputOf(i).FirstKeyPressed();
        if (k && key) *key = static_cast<uint32_t>(*k);
        return k.has_value();
    }, false);
}
bool koral_input_first_mouse_button_pressed(KoralInput* i, uint32_t* button)
{
    return Guarded([&] {
        const auto b = InputOf(i).FirstMouseButtonPressed();
        if (b && button) *button = static_cast<uint32_t>(*b);
        return b.has_value();
    }, false);
}
bool koral_input_interface_wants_mouse(KoralInput* i) { return Guarded([&] { return InputOf(i).InterfaceWantsMouse(); }, false); }
bool koral_input_interface_wants_keyboard(KoralInput* i) { return Guarded([&] { return InputOf(i).InterfaceWantsKeyboard(); }, false); }
void koral_input_mouse_position(KoralInput* i, float* x, float* y) { GuardedVoid([&] { Out2(InputOf(i).MousePosition(), x, y); }); }
void koral_input_mouse_position_delta(KoralInput* i, float* x, float* y) { GuardedVoid([&] { Out2(InputOf(i).MousePositionDelta(), x, y); }); }
void koral_input_mouse_scroll_delta(KoralInput* i, float* x, float* y) { GuardedVoid([&] { Out2(InputOf(i).MouseScrollDelta(), x, y); }); }
void koral_input_last_mouse_position(KoralInput* i, float* x, float* y) { GuardedVoid([&] { Out2(InputOf(i).LastMousePosition(), x, y); }); }
void koral_input_set_cursor_mode(KoralInput* i, const uint32_t m) { GuardedVoid([&] { InputOf(i).SetCursorMode(static_cast<Input::CursorMode>(m)); }); }
uint32_t koral_input_current_cursor_mode(KoralInput* i) { return Guarded([&] { return static_cast<uint32_t>(InputOf(i).CurrentCursorMode()); }, 0u); }
const char* koral_input_describe_key(const uint32_t key) { return Guarded([&] { return Keep(Input::Describe(static_cast<Key>(key))); }, Keep("")); }
const char* koral_input_describe_mouse_button(const uint32_t b) { return Guarded([&] { return Keep(Input::Describe(static_cast<MouseButton>(b))); }, Keep("")); }
bool koral_input_is_gamepad_connected(KoralInput* i, const int pad) { return Guarded([&] { return InputOf(i).IsGamepadConnected(pad); }, false); }
const char* koral_input_gamepad_name(KoralInput* i, const int pad) { return Guarded([&] { return Keep(InputOf(i).GamepadName(pad)); }, Keep("")); }
uint32_t koral_input_gamepad_button_state(KoralInput* i, const uint32_t b, const int pad)
{
    return Guarded([&] { return static_cast<uint32_t>(InputOf(i).GamepadButtonState(static_cast<GamepadButton>(b), pad)); }, 0u);
}
float koral_input_gamepad_axis_value(KoralInput* i, const uint32_t a, const int pad)
{
    return Guarded([&] { return InputOf(i).GamepadAxisValue(static_cast<GamepadAxis>(a), pad); }, 0.f);
}
void koral_input_set_gamepad_dead_zone(KoralInput* i, const float z) { GuardedVoid([&] { InputOf(i).SetGamepadDeadZone(z); }); }
void koral_input_bind_action(KoralInput* i, const char* action, const KoralInputSource* sources, const size_t count)
{
    GuardedVoid([&] { InputOf(i).BindAction(action ? action : "", SourcesOf(sources, count)); });
}
void koral_input_bind_axis(KoralInput* i, const char* axis, const KoralInputSource* sources, const size_t count)
{
    GuardedVoid([&] { InputOf(i).BindAxis(axis ? axis : "", SourcesOf(sources, count)); });
}
uint32_t koral_input_action_state(KoralInput* i, const char* action)
{
    return Guarded([&] { return static_cast<uint32_t>(InputOf(i).ActionState(action ? action : "")); }, 0u);
}
float koral_input_axis(KoralInput* i, const char* axis) { return Guarded([&] { return InputOf(i).Axis(axis ? axis : ""); }, 0.f); }
void koral_input_axis_2d(KoralInput* i, const char* x, const char* y, float* outX, float* outY)
{
    GuardedVoid([&] { Out2(InputOf(i).Axis2D(x ? x : "", y ? y : ""), outX, outY); });
}
const char* koral_input_bindings(KoralInput* i) { return Guarded([&] { return Keep(ToJson(InputOf(i).Bindings())); }, Keep("null")); }
KoralStatus koral_input_set_bindings(KoralInput* i, const char* json)
{
    return Guarded([&] {
        InputBindings bindings;
        if (const auto read = FromJson(bindings, json ? json : ""); !read) return Fail(read.error().message);
        InputOf(i).SetBindings(bindings);
        return KORAL_OK;
    }, KORAL_ERROR);
}
void koral_input_feed_key(KoralInput* i, const uint32_t key, const bool down) { GuardedVoid([&] { InputOf(i).FeedKey(static_cast<Key>(key), down); }); }
void koral_input_feed_mouse_button(KoralInput* i, const uint32_t b, const bool down) { GuardedVoid([&] { InputOf(i).FeedMouseButton(static_cast<MouseButton>(b), down); }); }
void koral_input_feed_mouse_position(KoralInput* i, const float x, const float y) { GuardedVoid([&] { InputOf(i).FeedMousePosition({x, y}); }); }
void koral_input_feed_mouse_delta(KoralInput* i, const float x, const float y) { GuardedVoid([&] { InputOf(i).FeedMouseDelta({x, y}); }); }
void koral_input_feed_scroll(KoralInput* i, const float x, const float y) { GuardedVoid([&] { InputOf(i).FeedScroll({x, y}); }); }
void koral_input_feed_text(KoralInput* i, const char* text)
{
    GuardedVoid([&] {
        // UTF-8 to code points; a malformed byte stands for itself, as U+FFFD would hide what it was.
        std::u32string out;
        const std::string_view t = text ? text : "";
        for (std::size_t k = 0; k < t.size();) {
            const auto c = static_cast<unsigned char>(t[k]);
            const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
            char32_t cp = len == 1 ? c : c & (0xFF >> (len + 1));
            for (std::size_t j = 1; j < len && k + j < t.size(); ++j) cp = (cp << 6) | (static_cast<unsigned char>(t[k + j]) & 0x3F);
            out.push_back(cp);
            k += len;
        }
        InputOf(i).FeedText(out);
    });
}
void koral_input_feed_key_repeat(KoralInput* i, const uint32_t key) { GuardedVoid([&] { InputOf(i).FeedKeyRepeat(static_cast<Key>(key)); }); }
const char* koral_input_typed_text(KoralInput* i)
{
    return Guarded([&] {
        std::string out;
        for (const char32_t cp : InputOf(i).TypedText()) {
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
            else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        }
        return Keep(std::move(out));
    }, "");
}
bool koral_input_is_key_repeated(KoralInput* i, const uint32_t key) { return Guarded([&] { return InputOf(i).IsKeyRepeated(static_cast<Key>(key)); }, false); }
void koral_input_feed_gamepad_button(KoralInput* i, const uint32_t b, const bool down, const int pad)
{
    GuardedVoid([&] { InputOf(i).FeedGamepadButton(static_cast<GamepadButton>(b), down, pad); });
}
void koral_input_feed_gamepad_axis(KoralInput* i, const uint32_t a, const float v, const int pad)
{
    GuardedVoid([&] { InputOf(i).FeedGamepadAxis(static_cast<GamepadAxis>(a), v, pad); });
}
void koral_input_release_all(KoralInput* i) { GuardedVoid([&] { InputOf(i).ReleaseAll(); }); }

// ---- Time ------------------------------------------------------------------------------------------------------

float koral_time_frame_time(KoralTime* t) { return Guarded([&] { return TimeOf(t).FrameTime(); }, 0.f); }
float koral_time_unscaled_frame_time(KoralTime* t) { return Guarded([&] { return TimeOf(t).UnscaledFrameTime(); }, 0.f); }
float koral_time_fixed_delta_time(KoralTime* t) { return Guarded([&] { return TimeOf(t).FixedDeltaTime(); }, 0.f); }
void koral_time_set_fixed_delta_time(KoralTime* t, const float s) { GuardedVoid([&] { TimeOf(t).SetFixedDeltaTime(s); }); }
float koral_time_fixed_step_fraction(KoralTime* t) { return Guarded([&] { return TimeOf(t).FixedStepFraction(); }, 0.f); }
bool koral_time_in_fixed_step(KoralTime* t) { return Guarded([&] { return TimeOf(t).InFixedStep(); }, false); }
float koral_time_elapsed(KoralTime* t) { return Guarded([&] { return TimeOf(t).Elapsed(); }, 0.f); }
uint64_t koral_time_frame_count(KoralTime* t) { return Guarded([&] { return TimeOf(t).FrameCount(); }, std::uint64_t{0}); }
float koral_time_time_scale(KoralTime* t) { return Guarded([&] { return TimeOf(t).TimeScale(); }, 1.f); }
void koral_time_set_time_scale(KoralTime* t, const float s) { GuardedVoid([&] { TimeOf(t).SetTimeScale(s); }); }

// ---- DebugDraw ---------------------------------------------------------------------------------------------------

KoralDebugStyle koral_debug_style_default(void)
{
    const DebugStyle d;
    return {.color = {d.color.r, d.color.g, d.color.b, d.color.a}, .duration = d.duration, .on_top = d.onTop};
}
void koral_debug_line(KoralDebugDraw* d, const float from[3], const float to[3], const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Line(Vec3Of(from), Vec3Of(to), StyleOf(s)); });
}
void koral_debug_box(KoralDebugDraw* d, const float min[3], const float max[3], const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Box(Vec3Of(min), Vec3Of(max), StyleOf(s)); });
}
void koral_debug_box_transform(KoralDebugDraw* d, const float transform[16], const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Box(Mat4Of(transform), StyleOf(s)); });
}
void koral_debug_circle(KoralDebugDraw* d, const float center[3], const float normal[3], const float radius, const KoralDebugStyle* s, const int segments)
{
    GuardedVoid([&] { DebugOf(d).Circle(Vec3Of(center), Vec3Of(normal), radius, StyleOf(s), segments > 0 ? segments : 32); });
}
void koral_debug_sphere(KoralDebugDraw* d, const float center[3], const float radius, const KoralDebugStyle* s, const int segments)
{
    GuardedVoid([&] { DebugOf(d).Sphere(Vec3Of(center), radius, StyleOf(s), segments > 0 ? segments : 32); });
}
void koral_debug_arrow(KoralDebugDraw* d, const float from[3], const float to[3], const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Arrow(Vec3Of(from), Vec3Of(to), StyleOf(s)); });
}
void koral_debug_point(KoralDebugDraw* d, const float position[3], const float size, const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Point(Vec3Of(position), size, StyleOf(s)); });
}
void koral_debug_axes(KoralDebugDraw* d, const float transform[16], const float size, const float duration)
{
    GuardedVoid([&] { DebugOf(d).Axes(Mat4Of(transform), size, duration); });
}
void koral_debug_grid(KoralDebugDraw* d, const float center[3], const float size, const int cells, const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Grid(Vec3Of(center), size, cells, StyleOf(s)); });
}
void koral_debug_frustum(KoralDebugDraw* d, const float viewProjection[16], const KoralDebugStyle* s)
{
    GuardedVoid([&] { DebugOf(d).Frustum(Mat4Of(viewProjection), StyleOf(s)); });
}
void koral_debug_clear(KoralDebugDraw* d) { GuardedVoid([&] { DebugOf(d).Clear(); }); }
uint64_t koral_debug_line_count(KoralDebugDraw* d) { return Guarded([&] { return static_cast<uint64_t>(DebugOf(d).LineCount()); }, uint64_t{0}); }

// ---- Context, paths ----------------------------------------------------------------------------------------------

bool koral_context_has_device(void) { return Context::HasDevice(); }
bool koral_context_supports_ray_tracing(void) { return Guarded([] { return Context::SupportsRayTracing(); }, false); }
bool koral_context_supports_async_compute(void) { return Guarded([] { return Context::SupportsAsyncCompute(); }, false); }
bool koral_context_async_compute_is_separate_family(void) { return Guarded([] { return Context::AsyncComputeIsSeparateFamily(); }, false); }
const char* koral_asset_path(const char* relative) { return Guarded([&] { return Keep(AssetPath(relative ? relative : "").string()); }, Keep("")); }
const char* koral_shader_path(const char* relative) { return Guarded([&] { return Keep(ShaderPath(relative ? relative : "").string()); }, Keep("")); }
void koral_add_asset_search_path(const char* directory, const bool front) { GuardedVoid([&] { AddAssetSearchPath(directory ? directory : "", front); }); }

// ---- a project's configuration ---------------------------------------------------------------------------------------

KoralProject* koral_project_load(const char* searchFrom, const int argc, const char* const* argv)
{
    return Guarded([&]() -> KoralProject* {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) if (argv && argv[i]) args.emplace_back(argv[i]);
        const std::filesystem::path from = searchFrom && *searchFrom ? searchFrom : ".";

        auto project = std::make_unique<KoralProject>();
        auto& config = project->config;
        const auto file = ProjectConfig::Locate(args, from);
        if (!file) { Fail(file.error()); return nullptr; }
        if (*file) {
            if (const auto merged = config.MergeFile(**file); !merged) { Fail(merged.error().message); return nullptr; }
            log::Info("[project] configuration: {}", (*file)->string());
        }
        if (const auto overridden = config.ApplyOverrides(args); !overridden) { Fail(overridden.error().message); return nullptr; }
        if (config.imguiIni.empty() && *file) config.imguiIni = (*file)->parent_path() / "imgui.ini";

        // What applies before there is an application, as the runtime applies it.
        config.RegisterSearchPaths();
        auto moduleDirectories = config.moduleDirectories;
        std::error_code ec;
        moduleDirectories.push_back(std::filesystem::absolute(from, ec));
        if (const auto loaded = ModuleHost::Load(config.modules, moduleDirectories); !loaded) {
            Fail(loaded.error().message);
            return nullptr;
        }
        project->gpu = config.gpu;
        project->interfaceDirectory = config.imguiIni.empty() ? std::string() : config.imguiIni.parent_path().string();
        project->scene = config.scene;
        project->title = config.title;
        for (const auto f : WindowSettings{}.formats) project->formats.push_back(static_cast<uint32_t>(f));
        return project.release();
    }, static_cast<KoralProject*>(nullptr));
}

void koral_project_destroy(KoralProject* project) { delete project; }
const char* koral_project_scene(KoralProject* project) { return project ? project->scene.c_str() : ""; }
bool koral_project_hot_reload(KoralProject* project) { return project && project->config.hotReload; }

void koral_project_app_settings(KoralProject* project, KoralAppSettings* settings)
{
    if (!project || !settings) return;
    const auto& c = project->config;
    *settings = koral_app_settings_default();
    settings->api = static_cast<uint32_t>(c.api);
    settings->platform = static_cast<KoralPlatform>(c.platform);
    settings->gpu = project->gpu.empty() ? nullptr : project->gpu.c_str();
    settings->interface_directory = project->interfaceDirectory.empty() ? nullptr : project->interfaceDirectory.c_str();
}

void koral_project_window_settings(KoralProject* project, KoralWindowSettings* settings)
{
    if (!project || !settings) return;
    const auto& c = project->config;
    *settings = koral_window_settings_default();
    settings->title = project->title.empty() ? nullptr : project->title.c_str();
    settings->extent[0] = c.extent.x;
    settings->extent[1] = c.extent.y;
    settings->resizable = c.resizable;
    settings->fullscreen = c.fullscreen;
    settings->decorated = c.decorated;
    settings->transparent_framebuffer = c.transparentFramebuffer;
    settings->vsync = c.vsync;
    settings->formats = project->formats.data();
    settings->format_count = project->formats.size();
}

const char* koral_project_usage(void) { return Keep(std::string(ProjectConfig::Usage())); }

} // extern "C"
