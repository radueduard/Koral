//
// The C interface (koral_c.h): a thin layer over the C++ one, catching everything at the boundary.
//

#include "koral_c.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>

#include <yyjson.h>

#include "app.h"
#include "buffer.h"
#include "commandBuffer.h"
#include "debugDraw.h"
#include "computePipeline.h"
#include "descriptorSet.h"
#include "frameGraph.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "log.h"
#include "sampler.h"
#include "scene.h"
#include "shader.h"

// The opaque handles, as the C side sees them.
struct KoralBuffer { kor::Resource<kor::Buffer> buffer; };
struct KoralImage {
    kor::Resource<kor::Image> owned;          // made with koral_image_create
    kor::ResourceRef<const kor::Image> ref;   // what it is, owned or borrowed
};
struct KoralPipeline {
    kor::Resource<kor::GraphicsPipeline> graphics;
    kor::Resource<kor::ComputePipeline> compute;
    [[nodiscard]] kor::ResourceRef<const kor::Pipeline> Any() const {
        return graphics.Valid() ? kor::ResourceRef<const kor::Pipeline>(kor::ResourceRef<const kor::GraphicsPipeline>(graphics))
                                : kor::ResourceRef<const kor::Pipeline>(kor::ResourceRef<const kor::ComputePipeline>(compute));
    }
};
struct KoralDescriptorSet { kor::Resource<kor::DescriptorSet> set; };

namespace
{
    thread_local std::string t_error;
    thread_local std::string t_string;

    std::unique_ptr<kor::App> g_app;

    KoralStatus fail(std::string message)
    {
        t_error = std::move(message);
        return KORAL_ERROR;
    }

    // Runs @p body with nothing escaping into C: an exception becomes the thread's last error.
    template<typename Body>
    auto guarded(Body&& body, decltype(body()) failed) -> decltype(body())
    {
        try {
            t_error.clear();
            return body();
        } catch (const std::exception& e) {
            t_error = e.what();
        } catch (...) {
            t_error = "an unknown exception";
        }
        return failed;
    }

    template<typename Body>
    void guardedVoid(Body&& body)
    {
        guarded([&] { body(); return 0; }, 0);
    }

    const char* keep(std::string text)
    {
        t_string = std::move(text);
        return t_string.c_str();
    }

    kor::Scene* sceneOf(KoralScene* scene) { return reinterpret_cast<kor::Scene*>(scene); }
    KoralScene* handleOf(kor::Scene* scene) { return reinterpret_cast<KoralScene*>(scene); }
    kor::CommandBuffer& commandsOf(KoralCommandBuffer* commands) { return *reinterpret_cast<kor::CommandBuffer*>(commands); }

    kor::SceneArgs argumentsOf(const char* json)
    {
        kor::SceneArgs arguments;
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

    std::string jsonOf(const kor::SceneArgs& arguments)
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

    template<typename E>
    kor::Flags<E> flagsOf(const std::uint32_t bits)
    {
        kor::Flags<E> flags;
        for (std::uint32_t bit = 0; bit < 32; ++bit)
            if (bits & (1u << bit)) flags |= static_cast<E>(1u << bit);
        return flags;
    }

    kor::Image::Format formatOf(const KoralFormat format)
    {
        switch (format) {
        case KORAL_FORMAT_RGBA8_UNORM: return kor::Image::Format::eRGBA8_UNORM;
        case KORAL_FORMAT_RGBA8_SRGB: return kor::Image::Format::eRGBA8_SRGB;
        case KORAL_FORMAT_RGBA16_SFLOAT: return kor::Image::Format::eRGBA16_SFLOAT;
        case KORAL_FORMAT_RGBA32_SFLOAT: return kor::Image::Format::eRGBA32_SFLOAT;
        case KORAL_FORMAT_R32_SFLOAT: return kor::Image::Format::eR32_SFLOAT;
        case KORAL_FORMAT_D32_SFLOAT: return kor::Image::Format::eD32_SFLOAT;
        }
        throw std::runtime_error("unknown KoralFormat");
    }

    // ---- a scene written in C --------------------------------------------------------------------

    class CScene final : public kor::Scene {
    public:
        explicit CScene(const KoralSceneCallbacks& callbacks) : _c(callbacks) {
            if (_c.interface) EnableInterface();
        }
        ~CScene() override { if (_c.destroy) _c.destroy(_c.user); }

        void Initialize() override { if (_c.initialize) _c.initialize(Handle(), _c.user); }
        void FixedUpdate() override { if (_c.fixed_update) _c.fixed_update(Handle(), _c.user); }
        void Update() override { if (_c.update) _c.update(Handle(), _c.user); }
        void LateUpdate() override { if (_c.late_update) _c.late_update(Handle(), _c.user); }
        void Render(kor::CommandBuffer& commands) override {
            if (_c.render) _c.render(Handle(), reinterpret_cast<KoralCommandBuffer*>(&commands), _c.user);
        }
        void RenderUI() override { if (_c.render_ui) _c.render_ui(Handle(), _c.user); }
        void OnResize(const glm::uvec2 extent) override { if (_c.on_resize) _c.on_resize(Handle(), extent.x, extent.y, _c.user); }
        bool OnCloseRequested() override { return _c.on_close_requested ? _c.on_close_requested(Handle(), _c.user) : true; }
        void Shutdown() override { if (_c.shutdown) _c.shutdown(Handle(), _c.user); }

        std::string SaveState() override {
            if (!_c.save_state) return "null";
            const char* json = _c.save_state(_c.user);
            return json ? json : "null";
        }
        kor::VoidResult LoadState(const std::string_view json) override {
            if (_c.load_state && json != "null" && !json.empty()) _c.load_state(std::string(json).c_str(), _c.user);
            return {};
        }

    private:
        KoralScene* Handle() { return handleOf(this); }
        KoralSceneCallbacks _c;
    };

    // ---- a render pass written in C ----------------------------------------------------------------

    class CPass;
}

struct KoralPassResources {
    const kor::PassResources* resources;
    CPass* pass;
};

namespace
{
    class CPass final : public kor::RenderPass {
    public:
        CPass(std::string name, const KoralPassCallbacks& callbacks) : RenderPass(std::move(name)), _c(callbacks) {}
        ~CPass() override { if (_c.destroy) _c.destroy(_c.user); }

        void Setup(kor::PassBuilder& builder) override {
            if (_c.setup) _c.setup(reinterpret_cast<KoralPassBuilder*>(&builder), _c.user);
        }
        void Initialize(const kor::PassResources& resources) override {
            if (!_c.initialize) return;
            KoralPassResources handle{&resources, this};
            _c.initialize(&handle, _c.user);
        }
        void Record(kor::CommandBuffer& commands) const override {
            if (_c.record) _c.record(reinterpret_cast<KoralCommandBuffer*>(&commands), _c.user);
        }

        KoralImage* Image(const kor::PassResources& resources, const std::string& name) {
            auto& image = _images[name];
            if (!image) image = std::make_unique<KoralImage>();
            image->ref = resources.ImageNamed(name);
            return image->ref.Alive() ? image.get() : nullptr;
        }

    private:
        KoralPassCallbacks _c;
        std::map<std::string, std::unique_ptr<KoralImage>> _images;
    };
}

extern "C" {

const char* koral_last_error(void) { return t_error.c_str(); }

// ---- the application ------------------------------------------------------------------------------

KoralStatus koral_app_create(const KoralAppSettings* settings)
{
    if (g_app) return fail("there is already an application: one per process");
    return guarded([&] {
        kor::AppSettings s;
        if (settings) {
            switch (settings->platform) {
            case KORAL_PLATFORM_X11: s.platform = kor::WindowPlatform::eX11; break;
            case KORAL_PLATFORM_WAYLAND: s.platform = kor::WindowPlatform::eWayland; break;
            case KORAL_PLATFORM_NONE: s.platform = kor::WindowPlatform::eNone; break;
            default: break;
            }
            if (settings->frames_in_flight) s.framesInFlight = settings->frames_in_flight;
            if (settings->gpu) s.gpu = settings->gpu;
            if (settings->interface_directory) s.interfaceDirectory = settings->interface_directory;
        }
        g_app = std::make_unique<kor::App>(std::move(s));
        return KORAL_OK;
    }, KORAL_ERROR);
}

void koral_app_destroy(void) { guardedVoid([] { g_app.reset(); }); }

KoralStatus koral_app_load_library(const char* path)
{
    if (!g_app) return fail("no application");
    return guarded([&] {
        const auto loaded = g_app->LoadLibrary(path);
        return loaded ? KORAL_OK : fail(loaded.error().message);
    }, KORAL_ERROR);
}

KoralStatus koral_app_reload_library(const char* path)
{
    if (!g_app) return fail("no application");
    return guarded([&] {
        const auto reloaded = g_app->ReloadLibrary(path);
        return reloaded ? KORAL_OK : fail(reloaded.error().message);
    }, KORAL_ERROR);
}

bool koral_app_frame(void) { return g_app && guarded([] { return g_app->Frame(); }, false); }
int koral_app_run(void) { return g_app ? guarded([] { return g_app->Run(); }, 1) : 1; }
void koral_app_quit(void) { if (g_app) g_app->Quit(); }

// ---- scenes ------------------------------------------------------------------------------------------

KoralStatus koral_register_scene(const char* name, const KoralSceneFactory factory, void* factoryUser)
{
    if (!g_app) return fail("no application");
    if (!name || !factory) return fail("koral_register_scene needs a name and a factory");
    return guarded([&] {
        g_app->Register(name, [factory, factoryUser](const kor::SceneArgs& arguments) -> std::unique_ptr<kor::Scene> {
            const std::string json = jsonOf(arguments);
            return std::make_unique<CScene>(factory(json.c_str(), factoryUser));
        });
        return KORAL_OK;
    }, KORAL_ERROR);
}

KoralScene* koral_open(const char* name, const KoralWindowSettings* settings, const char* argumentsJson)
{
    if (!g_app) { fail("no application"); return nullptr; }
    return guarded([&] {
        kor::WindowSettings window;
        window.title = name;
        if (settings) {
            if (settings->title) window.title = settings->title;
            if (settings->width && settings->height) window.extent = {settings->width, settings->height};
            window.fullscreen = settings->fullscreen;
            window.decorated = !settings->borderless;
            window.vsync = !settings->no_vsync;
        }
        auto* scene = g_app->Open(name, window, argumentsOf(argumentsJson));
        if (!scene) fail(std::string("could not open '") + name + "': see the log");
        return handleOf(scene);
    }, static_cast<KoralScene*>(nullptr));
}

KoralScene* koral_open_offscreen(const char* name, const uint32_t width, const uint32_t height, const char* argumentsJson)
{
    if (!g_app) { fail("no application"); return nullptr; }
    return guarded([&] {
        kor::OffscreenSettings target{.title = name};
        if (width && height) target.extent = {width, height};
        auto* scene = g_app->OpenOffscreen(name, target, argumentsOf(argumentsJson));
        if (!scene) fail(std::string("could not open '") + name + "': see the log");
        return handleOf(scene);
    }, static_cast<KoralScene*>(nullptr));
}

void koral_close(KoralScene* scene) { if (g_app && scene) g_app->Close(*sceneOf(scene)); }

const char* koral_scene_name(KoralScene* scene) { return scene ? keep(sceneOf(scene)->Name()) : ""; }
KoralScene* koral_current_scene(void) { return handleOf(kor::Scene::Current()); }

const char* koral_scene_save_state(KoralScene* scene)
{
    return guarded([&] { return keep(sceneOf(scene)->SaveState()); }, keep("null"));
}

KoralStatus koral_scene_load_state(KoralScene* scene, const char* json)
{
    return guarded([&] {
        const auto loaded = sceneOf(scene)->LoadState(json ? json : "null");
        return loaded ? KORAL_OK : fail(loaded.error().message);
    }, KORAL_ERROR);
}

void koral_navigate_replace(const char* name, const char* argumentsJson) { guardedVoid([&] { kor::Navigator::Replace(name, argumentsOf(argumentsJson)); }); }
void koral_navigate_push(const char* name, const char* argumentsJson) { guardedVoid([&] { kor::Navigator::Push(name, argumentsOf(argumentsJson)); }); }
void koral_navigate_pop(void) { guardedVoid([] { kor::Navigator::Pop(); }); }
void koral_navigate_close(void) { guardedVoid([] { kor::Navigator::Close(); }); }
void koral_navigate_quit(void) { guardedVoid([] { kor::Navigator::Quit(); }); }

// ---- the current scene's window, input and time -----------------------------------------------------

void koral_window_extent(uint32_t* width, uint32_t* height)
{
    guardedVoid([&] {
        const auto extent = kor::Scene::Window::Extent();
        if (width) *width = extent.x;
        if (height) *height = extent.y;
    });
}
bool koral_window_resized(void) { return guarded([] { return kor::Scene::Window::HasResized(); }, false); }
void koral_window_set_title(const char* title) { guardedVoid([&] { kor::Scene::Window::SetTitle(title ? title : ""); }); }
void koral_window_resize(const uint32_t width, const uint32_t height) { guardedVoid([&] { kor::Scene::Window::Get().Resize({width, height}); }); }
void koral_window_close(void) { guardedVoid([] { kor::Scene::Window::Close(); }); }

KoralKeyState koral_input_key(const int key)
{
    return guarded([&] { return static_cast<KoralKeyState>(kor::Scene::Input::StateOf(static_cast<kor::Key>(key))); }, KORAL_NOT_PRESSED);
}
KoralKeyState koral_input_mouse_button(const int button)
{
    return guarded([&] { return static_cast<KoralKeyState>(kor::Scene::Input::MouseButtonState(static_cast<kor::MouseButton>(button))); }, KORAL_NOT_PRESSED);
}
void koral_input_mouse_position(float* x, float* y)
{
    guardedVoid([&] { const auto p = kor::Scene::Input::MousePosition(); if (x) *x = p.x; if (y) *y = p.y; });
}
void koral_input_mouse_delta(float* x, float* y)
{
    guardedVoid([&] { const auto d = kor::Scene::Input::MousePositionDelta(); if (x) *x = d.x; if (y) *y = d.y; });
}
void koral_input_scroll(float* x, float* y)
{
    guardedVoid([&] { const auto s = kor::Scene::Input::MouseScrollDelta(); if (x) *x = s.x; if (y) *y = s.y; });
}
void koral_input_set_cursor_mode(const int mode)
{
    guardedVoid([&] { kor::Scene::Input::SetCursorMode(static_cast<kor::Input::CursorMode>(mode)); });
}
void koral_input_feed_key(KoralScene* scene, const int key, const bool down)
{
    if (scene) sceneOf(scene)->SceneInput().FeedKey(static_cast<kor::Key>(key), down);
}
void koral_input_feed_mouse_button(KoralScene* scene, const int button, const bool down)
{
    if (scene) sceneOf(scene)->SceneInput().FeedMouseButton(static_cast<kor::MouseButton>(button), down);
}
void koral_input_feed_mouse_position(KoralScene* scene, const float x, const float y)
{
    if (scene) sceneOf(scene)->SceneInput().FeedMousePosition({x, y});
}

bool koral_input_gamepad_connected(const int pad) { return guarded([&] { return kor::Scene::Input::IsGamepadConnected(pad); }, false); }
KoralKeyState koral_input_gamepad_button(const int button, const int pad)
{
    return guarded([&] { return static_cast<KoralKeyState>(kor::Scene::Input::GamepadButtonState(static_cast<kor::GamepadButton>(button), pad)); }, KORAL_NOT_PRESSED);
}
float koral_input_gamepad_axis(const int axis, const int pad)
{
    return guarded([&] { return kor::Scene::Input::GamepadAxisValue(static_cast<kor::GamepadAxis>(axis), pad); }, 0.f);
}

namespace
{
    std::vector<kor::InputSource> sourcesOf(const char* list)
    {
        std::vector<kor::InputSource> sources;
        std::string_view rest = list ? list : "";
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            std::string_view name = rest.substr(0, comma);
            while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
            while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
            if (!name.empty()) {
                const auto source = kor::InputSource::Parse(name);
                if (!source) throw std::runtime_error("'" + std::string(name) + "' names nothing that can be pressed");
                sources.push_back(*source);
            }
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        return sources;
    }
}

KoralStatus koral_input_bind_action(const char* action, const char* sources)
{
    return guarded([&] { kor::Scene::Input::BindAction(action, sourcesOf(sources)); return KORAL_OK; }, KORAL_ERROR);
}
KoralStatus koral_input_bind_axis(const char* axis, const char* sources)
{
    return guarded([&] { kor::Scene::Input::BindAxis(axis, sourcesOf(sources)); return KORAL_OK; }, KORAL_ERROR);
}
KoralKeyState koral_input_action(const char* action)
{
    return guarded([&] { return static_cast<KoralKeyState>(kor::Scene::Input::ActionState(action)); }, KORAL_NOT_PRESSED);
}
float koral_input_axis(const char* axis) { return guarded([&] { return kor::Scene::Input::Axis(axis); }, 0.f); }
const char* koral_input_bindings(void)
{
    return guarded([] { return keep(kor::ToJson(kor::Scene::Input::Get().Bindings())); }, keep("null"));
}
KoralStatus koral_input_set_bindings(const char* json)
{
    return guarded([&] {
        kor::InputBindings bindings;
        if (const auto read = kor::FromJson(bindings, json ? json : ""); !read) return fail(read.error().message);
        kor::Scene::Input::Get().SetBindings(bindings);
        return KORAL_OK;
    }, KORAL_ERROR);
}

float koral_time_frame(void) { return guarded([] { return kor::Scene::Time::FrameTime(); }, 0.f); }
float koral_time_fixed_step(void) { return guarded([] { return kor::Scene::Time::FixedDeltaTime(); }, 0.f); }
float koral_time_elapsed(void) { return guarded([] { return kor::Scene::Time::Elapsed(); }, 0.f); }
uint64_t koral_time_frame_count(void) { return guarded([] { return kor::Scene::Time::FrameCount(); }, std::uint64_t{0}); }
void koral_time_set_scale(const float scale) { guardedVoid([&] { kor::Scene::Time::SetTimeScale(scale); }); }
void koral_time_set_fixed_step(const float seconds) { guardedVoid([&] { kor::Scene::Time::SetFixedDeltaTime(seconds); }); }

// ---- resources -----------------------------------------------------------------------------------------

KoralBuffer* koral_buffer_create(const uint64_t size, const uint32_t usage, const KoralMemory memory)
{
    return guarded([&]() -> KoralBuffer* {
        auto buffer = kor::Buffer::RawBuilder{}
            .SetRawSize(static_cast<glm::i64>(size))
            .SetUsage(flagsOf<kor::Buffer::Usage>(usage))
            .SetType(static_cast<kor::Buffer::Type>(memory))
            .Build();
        if (!buffer.Valid()) { fail(buffer.Failure() ? buffer.Failure()->message : "the buffer could not be made"); return nullptr; }
        return new KoralBuffer{std::move(buffer)};
    }, static_cast<KoralBuffer*>(nullptr));
}

void koral_buffer_destroy(KoralBuffer* buffer) { delete buffer; }

KoralStatus koral_buffer_write(KoralBuffer* buffer, const void* data, const uint64_t size, const uint64_t offset)
{
    if (!buffer || !data) return fail("koral_buffer_write needs a buffer and data");
    return guarded([&] {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer->buffer->Write(std::span(bytes, static_cast<std::size_t>(size)), offset);
        return KORAL_OK;
    }, KORAL_ERROR);
}

KoralStatus koral_buffer_read(KoralBuffer* buffer, void* data, const uint64_t size, const uint64_t offset)
{
    if (!buffer || !data) return fail("koral_buffer_read needs a buffer and somewhere to put what it holds");
    return guarded([&] {
        const auto bytes = buffer->buffer->Read<std::uint8_t>(size, offset);
        std::memcpy(data, bytes.data(), std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(size)));
        return bytes.size() >= size ? KORAL_OK : fail("the buffer is smaller than what was asked for");
    }, KORAL_ERROR);
}

KoralImage* koral_image_create(const uint32_t width, const uint32_t height, const KoralFormat format, const uint32_t usage)
{
    return guarded([&]() -> KoralImage* {
        auto image = kor::Image::Builder()
            .SetExtent(glm::uvec2{width, height})
            .SetFormat(formatOf(format))
            .SetUsage(flagsOf<kor::Image::Usage>(usage))
            .Build();
        if (!image.Valid()) { fail(image.Failure() ? image.Failure()->message : "the image could not be made"); return nullptr; }
        auto* handle = new KoralImage{std::move(image), {}};
        handle->ref = kor::ResourceRef<const kor::Image>(handle->owned);
        return handle;
    }, static_cast<KoralImage*>(nullptr));
}

void koral_image_destroy(KoralImage* image) { delete image; }

void koral_image_extent(KoralImage* image, uint32_t* width, uint32_t* height)
{
    const auto extent = image && image->ref.Valid() ? image->ref->Extent() : glm::uvec3(0);
    if (width) *width = extent.x;
    if (height) *height = extent.y;
}

namespace
{
    kor::ResourceRef<const kor::Shader> shaderOf(const char* path, const char* entry)
    {
        kor::Shader::Builder builder;
        builder.SetPath(path);
        if (entry && *entry) builder.SetEntryPoint(entry);
        auto shader = builder.GetOrBuild();
        if (!shader.Valid()) throw std::runtime_error(std::string("the shader '") + path + "' did not build"
            + (shader.Failure() ? ": " + shader.Failure()->message : std::string()));
        return shader;
    }
}

KoralPipeline* koral_graphics_pipeline_create(const char* vertexPath, const char* vertexEntry,
                                              const char* fragmentPath, const char* fragmentEntry)
{
    return guarded([&]() -> KoralPipeline* {
        auto pipeline = kor::GraphicsPipeline::Builder()
            .SetVertexShader(shaderOf(vertexPath, vertexEntry))
            .SetFragmentShader(shaderOf(fragmentPath, fragmentEntry))
            .Build();
        if (!pipeline.Valid()) { fail(pipeline.Failure() ? pipeline.Failure()->message : "the pipeline could not be made"); return nullptr; }
        return new KoralPipeline{.graphics = std::move(pipeline)};
    }, static_cast<KoralPipeline*>(nullptr));
}

KoralPipeline* koral_compute_pipeline_create(const char* path, const char* entry)
{
    return guarded([&]() -> KoralPipeline* {
        auto pipeline = kor::ComputePipeline::Builder().SetComputeShader(shaderOf(path, entry)).Build();
        if (!pipeline.Valid()) { fail(pipeline.Failure() ? pipeline.Failure()->message : "the pipeline could not be made"); return nullptr; }
        return new KoralPipeline{.compute = std::move(pipeline)};
    }, static_cast<KoralPipeline*>(nullptr));
}

void koral_pipeline_destroy(KoralPipeline* pipeline) { delete pipeline; }

KoralDescriptorSet* koral_descriptor_set_create(KoralPipeline* pipeline, const uint32_t set,
                                                const KoralDescriptorWrite* writes, const size_t count)
{
    if (!pipeline) { fail("koral_descriptor_set_create needs a pipeline"); return nullptr; }
    return guarded([&]() -> KoralDescriptorSet* {
        static kor::Resource<kor::Sampler> linear = kor::Sampler::Builder{}.Build();
        kor::DescriptorSet::Builder builder(pipeline->Any(), set);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& write = writes[i];
            if (!write.name) continue;
            if (write.buffer) {
                builder.Write(write.name, kor::Descriptor(kor::ResourceRef<const kor::Buffer>(write.buffer->buffer)));
            } else if (write.image && write.image->ref.Valid()) {
                const auto view = write.image->ref->View(write.image->ref->NaturalShape());
                builder.Write(write.name, write.sampled
                    ? kor::Descriptor(view, kor::ResourceRef<const kor::Sampler>(linear))
                    : kor::Descriptor(view));
            }
        }
        auto made = builder.Build();
        if (!made.Valid()) { fail(made.Failure() ? made.Failure()->message : "the descriptor set could not be made"); return nullptr; }
        return new KoralDescriptorSet{std::move(made)};
    }, static_cast<KoralDescriptorSet*>(nullptr));
}

void koral_descriptor_set_destroy(KoralDescriptorSet* set) { delete set; }

// ---- recording -------------------------------------------------------------------------------------------

void koral_cmd_begin_rendering(KoralCommandBuffer* commands) { guardedVoid([&] { commandsOf(commands).BeginRendering(); }); }
void koral_cmd_end_rendering(KoralCommandBuffer* commands) { guardedVoid([&] { commandsOf(commands).EndRendering(); }); }

void koral_cmd_bind_pipeline(KoralCommandBuffer* commands, KoralPipeline* pipeline)
{
    if (!pipeline) return;
    guardedVoid([&] {
        if (pipeline->graphics.Valid()) commandsOf(commands).BindGraphicsPipeline(pipeline->graphics);
        else if (pipeline->compute.Valid()) commandsOf(commands).BindComputePipeline(pipeline->compute);
    });
}

void koral_cmd_bind_descriptor_set(KoralCommandBuffer* commands, const uint32_t index, KoralDescriptorSet* set)
{
    if (set) guardedVoid([&] { commandsOf(commands).BindDescriptorSet(index, set->set); });
}

void koral_cmd_push_floats(KoralCommandBuffer* commands, const char* name, const float* values, const uint32_t count)
{
    guardedVoid([&] {
        auto& cb = commandsOf(commands);
        switch (count) {
        case 1: cb.PushConstant(name, values[0]); break;
        case 2: cb.PushConstant(name, glm::vec2(values[0], values[1])); break;
        case 3: cb.PushConstant(name, glm::vec3(values[0], values[1], values[2])); break;
        case 4: cb.PushConstant(name, glm::vec4(values[0], values[1], values[2], values[3])); break;
        case 16: { glm::mat4 m; std::memcpy(&m, values, sizeof(m)); cb.PushConstant(name, m); break; }
        default: throw std::runtime_error("koral_cmd_push_floats takes 1, 2, 3, 4 or 16 values");
        }
    });
}

void koral_cmd_push_int(KoralCommandBuffer* commands, const char* name, const int32_t value)
{
    guardedVoid([&] { commandsOf(commands).PushConstant(name, value); });
}

void koral_cmd_draw(KoralCommandBuffer* commands, const uint32_t vertices, const uint32_t instances)
{
    guardedVoid([&] { commandsOf(commands).Draw(vertices, instances ? instances : 1); });
}

void koral_cmd_dispatch(KoralCommandBuffer* commands, const uint32_t x, const uint32_t y, const uint32_t z)
{
    guardedVoid([&] { commandsOf(commands).Dispatch(x, y, z); });
}

void koral_cmd_clear_image(KoralCommandBuffer* commands, KoralImage* image, const float r, const float g, const float b, const float a)
{
    if (image) guardedVoid([&] { commandsOf(commands).ClearColorImage(image->ref, glm::vec4(r, g, b, a)); });
}

void koral_cmd_copy_image_to_buffer(KoralCommandBuffer* commands, KoralImage* image, KoralBuffer* buffer)
{
    if (image && buffer) guardedVoid([&] { commandsOf(commands).CopyImageToBuffer(image->ref, buffer->buffer); });
}

void koral_cmd_blit(KoralCommandBuffer* commands, KoralImage* from, KoralImage* to)
{
    if (from && to) guardedVoid([&] { commandsOf(commands).Blit(from->ref, to->ref); });
}

// ---- the frame graph -------------------------------------------------------------------------------------

KoralStatus koral_graph_add_pass(KoralScene* scene, const char* name, const KoralPassCallbacks* pass)
{
    if (!scene || !name || !pass) return fail("koral_graph_add_pass needs a scene, a name and callbacks");
    return guarded([&] {
        sceneOf(scene)->Graph().Add<CPass>(std::string(name), *pass);
        return KORAL_OK;
    }, KORAL_ERROR);
}

void koral_pass_read(KoralPassBuilder* builder, const char* name, const uint32_t usage)
{
    auto& b = *reinterpret_cast<kor::PassBuilder*>(builder);
    if (usage) b.Read(name, flagsOf<kor::Image::Usage>(usage)); else b.Read(name);
}

void koral_pass_write(KoralPassBuilder* builder, const char* name, const uint32_t usage)
{
    auto& b = *reinterpret_cast<kor::PassBuilder*>(builder);
    if (usage) b.Write(name, flagsOf<kor::Image::Usage>(usage)); else b.Write(name);
}

void koral_pass_create_image(KoralPassBuilder* builder, const char* name, const KoralFormat format, const uint32_t usage, const float scale)
{
    guardedVoid([&] {
        reinterpret_cast<kor::PassBuilder*>(builder)->Create(name, kor::ImageDesc{
            .format = formatOf(format), .usage = flagsOf<kor::Image::Usage>(usage), .scale = scale > 0.f ? scale : 1.f});
    });
}

void koral_pass_side_effect(KoralPassBuilder* builder) { reinterpret_cast<kor::PassBuilder*>(builder)->SideEffect(); }
void koral_pass_async_compute(KoralPassBuilder* builder) { reinterpret_cast<kor::PassBuilder*>(builder)->AsyncCompute(); }

KoralImage* koral_pass_image(KoralPassResources* resources, const char* name)
{
    if (!resources || !name) return nullptr;
    return resources->pass->Image(*resources->resources, name);
}

// ---- debug lines ------------------------------------------------------------------------------------------

namespace
{
    kor::DebugStyle styleOf(const KoralDebugStyle* style)
    {
        if (!style) return {};
        return {.color = {style->r, style->g, style->b, style->a}, .duration = style->duration, .onTop = style->on_top};
    }
    glm::vec3 vec3Of(const float* v) { return {v[0], v[1], v[2]}; }
}

void koral_debug_line(const float from[3], const float to[3], const KoralDebugStyle* style)
{
    guardedVoid([&] { kor::Scene::Debug::Line(vec3Of(from), vec3Of(to), styleOf(style)); });
}
void koral_debug_box(const float min[3], const float max[3], const KoralDebugStyle* style)
{
    guardedVoid([&] { kor::Scene::Debug::Box(vec3Of(min), vec3Of(max), styleOf(style)); });
}
void koral_debug_sphere(const float center[3], const float radius, const KoralDebugStyle* style)
{
    guardedVoid([&] { kor::Scene::Debug::Sphere(vec3Of(center), radius, styleOf(style)); });
}
void koral_debug_arrow(const float from[3], const float to[3], const KoralDebugStyle* style)
{
    guardedVoid([&] { kor::Scene::Debug::Arrow(vec3Of(from), vec3Of(to), styleOf(style)); });
}

KoralStatus koral_graph_add_debug_pass(KoralScene* scene, void (*viewProjection)(float out[16], void* user), void* user, const char* depth)
{
    if (!scene) return fail("koral_graph_add_debug_pass needs a scene");
    return guarded([&] {
        auto* s = sceneOf(scene);
        s->Graph().Add<kor::DebugDrawPass>(s->SceneDebug(), [viewProjection, user] {
            glm::mat4 matrix(1.f);
            if (viewProjection) viewProjection(&matrix[0][0], user);
            return matrix;
        }, std::string(kor::FrameGraph::Screen), depth ? std::string(depth) : std::string());
        return KORAL_OK;
    }, KORAL_ERROR);
}

} // extern "C"
