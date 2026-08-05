//
// Created by radue on 30.07.2026.
//

/**
 * @file koralViewport.h
 * @brief A window that shows an image you rendered, and tells you how big to render it next time.
 *
 * Displaying a render target in ImGui is one call — `ImGui::Image` — and that is not the hard part.
 * The hard part is everything around it: the target has to be *re-created* when the window is
 * resized, the image's aspect rarely matches the window's, input must only reach the scene while the
 * pointer is actually over it, and a pick needs the mouse position in the image's own pixels. That is
 * what this holds.
 *
 * @code
 * kgui::Viewport _viewport;
 *
 * void MyScene::Initialize() {
 *     _viewport.setImage(kor::ResourceRef<const kor::Image>(_colorTarget));   // once
 * }
 *
 * void MyScene::Update() {
 *     // The viewport asked for a size last frame; give it one. Resizing the target is the whole of
 *     // the answer — the viewport notices that the image was replaced and follows.
 *     if (_viewport.resized()) framebuffer->Resize(_viewport.size());
 *     if (_viewport.isHovered()) _camera->controller().enable();   // input only over the image
 * }
 *
 * void MyScene::RenderUI() { _viewport.Draw("Scene"); }
 * @endcode
 *
 * @section viewport_lag The size is one frame behind, and that is on purpose
 *
 * A viewport learns its size by *being drawn* — an ImGui window's content region is not knowable until
 * the layout runs — and by then the scene has already rendered this frame. So @ref resized reports the
 * change on the frame *after* it happened, the scene resizes its target in the next Update, and renders
 * into it in the same frame. One frame of lag, and the picture is right from then on.
 *
 * While a *continuous* resize is in progress — dragging a floating viewport's edge — that one frame
 * never catches up: every frame changes the size again, so every frame renders into a target that was
 * replaced this frame and shows it a frame late. Expect it to look soft or lag the cursor during the
 * drag and to settle the moment it stops. Nothing is corrupt; it is the frame of lag, made visible by
 * changing the size faster than a frame.
 *
 * The image is yours: the viewport neither creates nor owns one, it holds a reference and a handle to
 * show it with. An image that is destroyed leaves the viewport drawing nothing rather than a dangling
 * texture.
 *
 * All it asks of the image is kor::Image::Usage::eSampled — a colour target is bound to ImGui directly,
 * with no copy. (A 3D slice, a multisampled or a single-channel image *is* copied, and then needs
 * eTransferSrc as well; the handle says so if it comes to that.)
 *
 * @section viewport_gizmo Gizmos
 *
 * A gizmo has to be drawn *into* the viewport's rectangle, which means telling ImGuizmo where that
 * rectangle is on screen. @ref kgui::Viewport::BeginGizmo does exactly that, and @ref kgui::Gizmo
 * wraps the manipulation itself in glm types. @see koralGizmo.h
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <imgui.h>

#include <glm/glm.hpp>

#include <error.h>
#include <gui.h>
#include <image.h>
#include <log.h>
#include <resource.h>

namespace kgui
{
    /**
     * @brief An ImGui window showing an image, sized to it, with the state a scene needs back.
     *
     * How the image is fitted into the window is @ref Fit. What the scene reads back is @ref size —
     * the resolution the window would like to be rendered at — plus @ref resized, @ref isHovered,
     * @ref isFocused and @ref mousePosition.
     */
    class Viewport
    {
    public:
        /** @brief How the image is laid out inside the window. */
        enum class Fit
        {
            /**
             * @brief Fill the window, and expect a target of exactly that size. The default.
             *
             * The image is stretched to the whole content region, which is right when you re-render
             * at @ref size — the two then always agree and there is nothing to letterbox.
             */
            eStretch,
            /**
             * @brief Keep the image's aspect ratio, centred, with bars where it does not reach.
             *
             * For an image whose size you do *not* control — a loaded texture, a fixed-resolution
             * target — so it is shown whole and undistorted rather than squashed.
             */
            eContain,
        };

        /** @brief Where the image ended up on screen, in ImGui's screen coordinates. */
        struct Rect
        {
            ImVec2 position { 0.f, 0.f };   ///< Top-left corner.
            ImVec2 size { 0.f, 0.f };       ///< Width and height, in pixels.
        };

        /**
         * @brief Points the viewport at the image to display.
         * @param image The image. Must be usable as a sampled image; it is not owned.
         *
         * Once is enough, including across resizes: an image that is *replaced* — which is what
         * resizing one does — is noticed by the next @ref Draw, so a scene answering @ref resized has
         * only its own render target to re-create. Call this again to show a *different* image, and
         * calling it every frame with the same one is cheap.
         */
        void setImage(kor::ResourceRef<const kor::Image> image)
        {
            // Compared by what they point at, since a ResourceRef is a handle and two of them to the
            // same image are the same image. Cheap enough to be called every frame with the same one,
            // which is how a scene naturally uses it.
            if (_image.alive() && image.alive() && _image.get() == image.get()
                && _handleGeneration == image->generation()) return;

            _image = std::move(image);
            _handle = {};
            _reportedHandleFailure = false;   // a different image deserves its own diagnosis
            rebuildHandle();
        }

        /** @brief The image currently being displayed, if any. */
        [[nodiscard]] const kor::ResourceRef<const kor::Image>& image() const { return _image; }

        /**
         * @brief Whether the viewport is actually showing an image.
         *
         * False when it has none, when the one it had was destroyed, and when the backend could not
         * make a texture handle for it — the three cases where Draw puts up a placeholder instead.
         */
        [[nodiscard]] bool showing() const { return _handle.operator bool() && _image.alive(); }

        /** @brief How the image is fitted into the window. @see Fit */
        void setFit(const Fit fit) { _fit = fit; }
        [[nodiscard]] Fit fit() const { return _fit; }

        /**
         * @brief Draws the window.
         * @param title Its title, which is also its ImGui id.
         * @param open Optional flag the window's close button clears.
         * @return Whether the window is open and its content region has a size — i.e. whether
         *         anything was drawn and the state below is worth reading.
         *
         * Call it from Scene::RenderUI, once a frame. Drawn with no padding, so the image meets the
         * window's edges and @ref size is the whole of it.
         */
        bool Draw(const char* title = "Viewport", bool* open = nullptr)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
            const bool visible = ImGui::Begin(title, open);
            ImGui::PopStyleVar();

            if (!visible) {
                // Collapsed or hidden: it has no size, and reporting the last one would have the
                // scene re-render for a window nobody can see.
                _hovered = _focused = false;
                _resized = false;
                _content = { 0, 0 };
                ImGui::End();
                return false;
            }

            const ImVec2 available = ImGui::GetContentRegionAvail();
            const glm::uvec2 content {
                static_cast<glm::u32>(std::max(0.f, std::floor(available.x))),
                static_cast<glm::u32>(std::max(0.f, std::floor(available.y))) };

            _resized = content != _content && content.x > 0 && content.y > 0;
            _content = content;

            // Before the layout, which reads the image's extent, and before the draw, which needs a
            // handle that matches it: a target the scene resized last frame is picked up here rather
            // than being handed back by the scene. @see refreshHandle
            refreshHandle();

            _rect = layout(available);
            drawImage();

            // Read *after* the image, so they describe the item just drawn rather than the window.
            _hovered = ImGui::IsItemHovered();
            _focused = ImGui::IsWindowFocused();

            ImGui::End();
            return content.x > 0 && content.y > 0;
        }

        /**
         * @brief The resolution the viewport would like to be rendered at: its content region.
         *
         * What to size a render target to. Zero while the window is collapsed or hidden, which is
         * also when @ref resized stays false — there is nothing to re-create for.
         */
        [[nodiscard]] glm::uvec2 size() const { return _content; }

        /**
         * @brief Whether @ref size changed on the last Draw.
         *
         * The one thing that makes a viewport more than an ImGui::Image call: a render target has to
         * be re-created when this is true, and a scene checks it once per frame.
         */
        [[nodiscard]] bool resized() const { return _resized; }

        /** @brief Whether the pointer is over the image. What to gate camera and picking input on. */
        [[nodiscard]] bool isHovered() const { return _hovered; }

        /** @brief Whether the pointer is resize handle shaped. */
        [[nodiscard]] bool isResizing() const
        {
            if (!_hovered) return false;
            const ImVec2 mouse = ImGui::GetMousePos();
            constexpr float border = 4.f;   // matches ImGui's resize handle size
            return mouse.x < _rect.position.x + border || mouse.x > _rect.position.x + _rect.size.x - border
                || mouse.y < _rect.position.y + border || mouse.y > _rect.position.y + _rect.size.y - border;
        }

        /** @brief Whether the viewport's window has keyboard focus. */
        [[nodiscard]] bool isFocused() const { return _focused; }

        /** @brief Where the image was drawn on screen, for anything that draws over it. @see BeginGizmo */
        [[nodiscard]] const Rect& rect() const { return _rect; }

        /**
         * @brief The pointer's position in the image's own pixels.
         * @return The position, or nullopt when the pointer is not over the image.
         *
         * What a pick needs: the texel under the cursor, whatever the window's size or the fit did to
         * get it there.
         */
        [[nodiscard]] std::optional<glm::vec2> mousePosition() const
        {
            if (!_hovered || _rect.size.x <= 0.f || _rect.size.y <= 0.f) return std::nullopt;

            const ImVec2 mouse = ImGui::GetMousePos();
            const glm::vec2 normalized {
                (mouse.x - _rect.position.x) / _rect.size.x,
                (mouse.y - _rect.position.y) / _rect.size.y };
            if (normalized.x < 0.f || normalized.x > 1.f || normalized.y < 0.f || normalized.y > 1.f)
                return std::nullopt;

            // Scaled to the *image's* extent, which under eContain is not the window's.
            const auto extent = _image.alive() ? _image->getExtent() : glm::uvec3(_content, 1);
            return glm::vec2(normalized.x * static_cast<float>(extent.x),
                             normalized.y * static_cast<float>(extent.y));
        }

        /**
         * @brief Prepares ImGuizmo to draw inside this viewport, and returns whether it can.
         *
         * Call it between Draw() and ImGuizmo::Manipulate. It sets the gizmo's rectangle to where
         * the image actually is on screen and its draw list to this window's, without which a gizmo
         * lands in the wrong place or behind the image. @see kgui::Gizmo
         */
        [[nodiscard]] bool BeginGizmo() const;

    private:
        /**
         * @brief Makes the ImGui texture handle for whatever @ref _image currently is.
         *
         * A GUI_Image is a backend object — a descriptor set on Vulkan, a texture name on GL — so it
         * is built through the engine. And it can fail: there may be no GUI backend at all (a
         * headless run), or the image may not be sampleable. Guarded rather than left to throw,
         * because the caller is Scene::RenderUI and a viewport that cannot show its image should draw
         * a placeholder, not take the frame down with it.
         */
        void rebuildHandle()
        {
            // Stamped even when the build below fails, so a failure is not retried every frame — the
            // next attempt comes with the next image, or the next resize.
            _handleGeneration = _image.alive() ? _image->generation() : 0;
            _handle = { };
            if (!_image.alive()) return;

            auto created = kor::guard(kor::ErrorCode::eBackend, [this] {
                return kor::GUI_Image::Create(_image);
            });
            if (created) {
                _handle = std::move(*created);
            } else if (!_reportedHandleFailure) {
                _reportedHandleFailure = true;
                kor::log::warn("[viewport] the image cannot be shown: {}", created.error().message);
            }
        }

        /**
         * @brief Rebuilds the handle if the image behind it has been replaced since it was made.
         *
         * Resizing an image does not resize it — it allocates a new one and bumps its generation,
         * which is precisely how anything holding a handle to the old one is meant to find out. So a
         * scene that resizes its render target has to do nothing else: it does not have to hand the
         * viewport an image it never changed, and it cannot forget to. @see kor::Image::generation
         */
        void refreshHandle()
        {
            if (_image.alive() && _image->generation() != _handleGeneration) rebuildHandle();
        }

        /** @brief Where to put the image inside @p available, given the fit. */
        [[nodiscard]] Rect layout(const ImVec2 available) const
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            if (_fit == Fit::eStretch || !_image.alive())
                return Rect{ cursor, available };

            const auto extent = _image->getExtent();
            if (extent.x == 0 || extent.y == 0) return Rect{ cursor, available };

            const float imageAspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
            const float windowAspect = available.y > 0.f ? available.x / available.y : imageAspect;

            ImVec2 size = available;
            if (windowAspect > imageAspect) size.x = available.y * imageAspect;   // bars left and right
            else                           size.y = available.x / imageAspect;   // bars top and bottom

            // Centred, so the bars are even.
            const ImVec2 position { cursor.x + (available.x - size.x) * 0.5f,
                                    cursor.y + (available.y - size.y) * 0.5f };
            return Rect{ position, size };
        }

        void drawImage() const
        {
            if (!_handle || !_image.alive()) {
                // No image, or one that has been destroyed: say so instead of drawing a dangling
                // texture. A scene that re-creates its targets passes through this for one frame.
                ImGui::Dummy(ImGui::GetContentRegionAvail());
                const ImVec2 centre { _rect.position.x + _rect.size.x * 0.5f,
                                      _rect.position.y + _rect.size.y * 0.5f };
                const char* text = "no image";
                const ImVec2 textSize = ImGui::CalcTextSize(text);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(centre.x - textSize.x * 0.5f, centre.y - textSize.y * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
                return;
            }

            ImGui::SetCursorScreenPos(_rect.position);
            ImGui::Image(**_handle, _rect.size);
        }

        kor::ResourceRef<const kor::Image> _image;
        kor::Resource<kor::GUI_Image> _handle;
        /// The image generation the handle was built for, so a replaced image is noticed. @see refreshHandle
        glm::u64 _handleGeneration = 0;
        Fit _fit = Fit::eStretch;

        glm::uvec2 _content { 0, 0 };
        Rect _rect;
        bool _resized = false;
        bool _hovered = false;
        bool _focused = false;
        bool _reportedHandleFailure = false;
    };
}
