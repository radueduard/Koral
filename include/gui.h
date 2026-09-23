//
// Created by radue on 3/17/2026.
//

#pragma once
#include "commandBuffer.h"
#include "imageView.h"
#include "sampler.h"

#include <imgui.h>
#include <cstddef>

struct ImGui_ImplVulkan_InitInfo;

struct GLFWwindow;

namespace kor
{
    class CommandBuffer;
    class Scene;

    // ---------------------------------------------------------------------------
    //  Cross-module ImGui binding
    //
    //  ImGui keeps its state in process-globals: the current-context pointer (GImGui) and the
    //  allocator function pointers every ImGui allocation is routed through. A scene is a shared
    //  library the runtime loads at startup and it calls ImGui directly from RenderUI(), so the
    //  scene and Koral have to be looking at the *same* globals.
    //
    //  On Linux and macOS they are, for free: the ELF/Mach-O loaders interpose symbols, so the
    //  scene's calls to ImGui::Begin resolve to whichever copy was loaded first (Koral's) and there
    //  is only ever one GImGui in the process. Windows does no such thing — every module binds to
    //  its own statically linked copy at link time, so the scene DLL gets a second, freshly zeroed
    //  GImGui and its first ImGui call dereferences a null context. Building ImGui as a DLL would
    //  fix it at the source, but vcpkg's port opens with vcpkg_check_linkage(ONLY_STATIC_LIBRARY),
    //  which silently forces static linkage back on and makes a triplet's dynamic override a no-op.
    //
    //  So we do what imgui.h itself prescribes for this case: "DLL users: heaps and globals are not
    //  shared across DLL boundaries! You will need to call SetCurrentContext() +
    //  SetAllocatorFunctions()". The registrar below is an inline variable, so every module that
    //  includes this header constructs its own copy when that module loads, and the function
    //  pointers it hands over are resolved inside that module — they are the way to reach *its*
    //  ImGui globals. GUI::Init() then points every registered module at the one context it
    //  creates. A no-op on the platforms that never had the problem, where every module reports the
    //  same pointers anyway.
    // ---------------------------------------------------------------------------
    namespace detail
    {
        // One module's copy of ImGui: how to reach its globals, plus enough of its data layout to
        // tell whether sharing a context with it is safe at all.
        struct ImGuiModule
        {
            const char* version;
            std::size_t sizeOfIO;
            std::size_t sizeOfStyle;
            std::size_t sizeOfVec2;
            std::size_t sizeOfVec4;
            std::size_t sizeOfDrawVert;
            std::size_t sizeOfDrawIdx;
            void (*setCurrentContext)(ImGuiContext*);
            void (*setAllocatorFunctions)(ImGuiMemAllocFunc, ImGuiMemFreeFunc, void*);
        };

        // Named imguiModule, not module: `module` is a context-sensitive keyword since C++20 and
        // MSVC pre-scans for it at the start of a line, which is where a statement using it lands.
        KORAL_API void registerImGuiModule(const ImGuiModule& imguiModule);

        // Deliberately neither KORAL_API nor defined out of line: this has to be compiled into the
        // module that includes the header rather than imported from Koral, or every module would
        // report Koral's own ImGui and the binding would silently do nothing.
        struct ImGuiModuleRegistrar
        {
            ImGuiModuleRegistrar()
            {
                registerImGuiModule({
                    IMGUI_VERSION,
                    sizeof(ImGuiIO),
                    sizeof(ImGuiStyle),
                    sizeof(ImVec2),
                    sizeof(ImVec4),
                    sizeof(ImDrawVert),
                    sizeof(ImDrawIdx),
                    &ImGui::SetCurrentContext,
                    &ImGui::SetAllocatorFunctions,
                });
            }
        };

        inline const ImGuiModuleRegistrar imguiModuleRegistrar {};
    }

    /**
     * @brief A Koral image made displayable inside a Dear ImGui window.
     *
     * ImGui draws textures through an opaque handle of the backend's making; this produces one for
     * an Image and keeps it valid. Create it once — in Scene::Initialize — and hold it, since each
     * one allocates a descriptor:
     *
     * @code
     * // once
     * _preview = kor::GuiImage::Create(_offscreenColor);
     * // every frame, inside RenderUI()
     * ImGui::Image(**_preview, ImVec2(320, 180));
     * @endcode
     *
     * The image must be in a shader-readable state when ImGui draws, which is the frame's end — so
     * a target rendered this frame needs no special handling.
     */
    class KORAL_API GuiImage
    {
        friend class GUI;
    public:
        virtual ~GuiImage() = default;

        /**
         * @brief Displays a different mip level or array layer of the same image.
         * @param layer Array layer to show.
         * @param level Mip level to show.
         */
        virtual void setLayerAndLevel(glm::u32 layer, glm::u32 level) = 0;

        /** @brief Points this handle at a different image, keeping the handle itself valid. */
        virtual void setImage(kor::ResourceRef<const Image> image) = 0;

        /** @brief The ImGui texture handle, to pass to ImGui::Image and friends. */
        virtual ImTextureID operator*() const = 0;

        /**
         * @brief Creates a displayable handle for an image.
         * @param image The image to show. It must be usable as a sampled image.
         * @param layer Array layer to show.
         * @param level Mip level to show.
         */
        static kor::Resource<GuiImage> Create(kor::ResourceRef<const kor::Image> image, glm::u32 layer = 0, glm::u32 level = 0);

    private:
        /**
         * @brief Brings the handle up to date with the image behind it, in the frame's own commands.
         *
         * Called by GUI::Render on every live handle, once a frame, after the scene has drawn its
         * interface and before ImGui's draws are recorded. Private and dispatched through this class:
         * a handle is refreshed by the GUI or not at all.
         *
         * A handle does not sample the image directly — it samples a copy in the format, layer and
         * mip level ImGui can take — so without this a viewport would show whatever the image held
         * when the handle was made. Recorded into the frame's command buffer rather than submitted on
         * its own, which is also what lets the engine's barriers see that the image is read here.
         */
        virtual void refresh(kor::CommandBuffer& commandBuffer) {}
    };

    /** @brief The weights of the interface font that Koral loads at startup. */
    enum class Font : std::uint8_t {
        eLight,     ///< Light weight.
        eRegular,   ///< Regular weight; what the interface uses by default.
        eBold,      ///< Bold weight, for emphasis and headings.
        eItalic,    ///< Italic.
        eBlack      ///< Heaviest weight.
    };

    /**
     * @brief The Dear ImGui integration: one context, styled, with fonts and icons loaded.
     *
     * The runtime brings this up with the window and renders it at the end of every frame, calling
     * Scene::RenderUI in between. A scene therefore only calls ImGui functions — the lifecycle here
     * is the engine's business, and the one member worth reaching for is GetFont().
     */
    class GUI
    {
    public:
        /** @brief Creates the ImGui context, loads the fonts and starts the backend. Called once by the window. */
        KORAL_API static void Init();

        /**
         * @brief Runs one ImGui frame and records its draws.
         * @param commandBuffer The frame's command buffer.
         * @param scene The scene whose RenderUI() supplies the interface.
         */
        KORAL_API static void Render(kor::CommandBuffer& commandBuffer, Scene& scene);

        /**
         * @brief Draws the panels that float outside the main window, in their own windows.
         *
         * **Must be called after the frame's command buffer has been submitted**, which is why it is
         * not part of Render(): it does not record into that command buffer but builds and submits its
         * own, one per undocked window. Called from inside the recording it would run before the
         * frame's barriers were emitted, and an undocked panel showing a render target would sample it
         * in an undefined layout.
         *
         * A no-op when multi-viewport is off — under Wayland, for one, where a client cannot place a
         * window at an absolute position. The runtime calls this; an embedder driving the scheduler
         * itself has to, right after Scheduler::Draw returns.
         */
        KORAL_API static void RenderPlatformWindows();

        /** @brief Destroys the ImGui context and its backend. Called once by the window. */
        KORAL_API static void Shutdown();

        /**
         * @brief One of the loaded interface fonts, for ImGui::PushFont.
         * @param font Which weight.
         * @return The font. Each carries the FontAwesome icon glyphs merged in, so an icon can be
         *         written straight into a label.
         */
        KORAL_API static ImFont* GetFont(Font font);

    private:
        KORAL_API static void DefineStyle();
        // The loaded fonts deliberately do NOT live here. A static data member defined in the
        // header gives the executable and every scene .so its own copy, so GUI::Init would fill
        // one map and a module's GetFont would read another and find it empty. The map lives in
        // gui.cpp and is reached only through the exported accessors above.
    };


}
