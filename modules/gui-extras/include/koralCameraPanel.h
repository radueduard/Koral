//
// Created by radue on 30.07.2026.
//

/**
 * @file koralCameraPanel.h
 * @brief Where the GUI extras meet the camera module: a debugging view of a camera, that you draw.
 *
 * Pose, projection, controller and every binding, editable while the scene runs. It is a development
 * tool — the sort of thing you want while tuning a fly camera and never in a shipped interface.
 *
 * @code
 * kgui::CameraPanel _cameras;
 *
 * void MyScene::RenderUI() {
 *     if (_showCameras) _cameras.Draw("Cameras", &_showCameras, *_player, *_minimap);
 * }
 * @endcode
 *
 * **The camera module does not draw this, or anything else.** Nothing appears unless a scene calls
 * Draw, and a project that wants a different interface writes one — everything here is built on the
 * public API in koralCamera.h and kor::Input, so there is nothing this can do that a project cannot.
 * Read it as a worked example as much as a widget.
 *
 * Like koralModelMesh.h this is a *seam*: it is templates and inline functions, compiled into
 * whoever includes it, so `koral-gui-extras` does not link `koral-camera` and `koral-camera` knows
 * nothing about ImGui. It needs both modules linked, and says so if one is missing.
 */

#pragma once

// The compatibility gate. Including this header is a statement that both modules are in play, so say
// plainly which one is missing instead of failing on the include below.
#if !__has_include(<koralCamera.h>)
#  error "koralCameraPanel.h draws the camera module's cameras, and the camera module is not here. Link koral-camera too (target_link_libraries(<target> PRIVATE Koral::koral-gui-extras Koral::koral-camera)), or drop this include if you do not use cameras."
#endif

#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include <imgui.h>

#include <glm/glm.hpp>

#include <input.h>
#include <scene.h>

#include <koralCamera.h>

namespace kgui
{
    /**
     * @brief A panel for editing cameras: pose, projection, controller, and the bindings it reads.
     *
     * Holds only interface state — which control is waiting for a key, and which camera it belongs
     * to — so a panel can be created and destroyed freely, and two of them do not interfere.
     */
    class CameraPanel
    {
    public:
        /**
         * @brief Draws a window with each of @p cameras in it.
         * @param title The window's title, which is also its ImGui id.
         * @param open Optional flag the close button clears, as ImGui::Begin takes.
         * @param cameras The cameras to show, by reference. Perspective and orthographic may be mixed.
         * @return Whether the window is open and was drawn into.
         */
        template<typename... Cameras>
        bool Draw(const char* title, bool* open, Cameras&... cameras)
        {
            if (!ImGui::Begin(title, open)) { ImGui::End(); return false; }

            if constexpr (sizeof...(Cameras) == 0) ImGui::TextDisabled("no cameras");
            (Draw(cameras), ...);

            ImGui::End();
            return true;
        }

        /**
         * @brief Draws one camera's controls where the cursor is, with no window of its own.
         *
         * For a project that puts them in its own layout — a tab, a tree, a properties panel next to
         * everything else it edits.
         */
        void Draw(kcam::PerspectiveCamera& camera)
        {
            if (!Header(camera, "perspective")) return;
            ImGui::PushID(&camera);

            DrawPose(camera);

            float fov = glm::degrees(camera.FovY());
            if (ImGui::SliderFloat("fov", &fov, 10.f, 140.f, "%.0f deg"))
                camera.SetFovY(glm::radians(fov));

            DrawAspect(camera);

            glm::vec2 depth { camera.ZNear(), camera.ZFar() };
            if (ImGui::DragFloat2("near / far", &depth.x, 0.1f, 0.001f, 100000.f) &&
                depth.x > 0.f && depth.y > depth.x)
                camera.SetNearFar(depth.x, depth.y);

            DrawController(camera);
            ImGui::PopID();
        }

        /** @brief Draws one orthographic camera's controls, inline. @see Draw(PerspectiveCamera&) */
        void Draw(kcam::OrthographicCamera& camera)
        {
            if (!Header(camera, "orthographic")) return;
            ImGui::PushID(&camera);

            DrawPose(camera);

            glm::vec4 bounds = camera.Bounds();
            if (ImGui::DragFloat4("l / r / b / t", &bounds.x, 0.1f))
                camera.SetBounds(bounds.x, bounds.y, bounds.z, bounds.w);

            glm::vec2 depth { camera.ZNear(), camera.ZFar() };
            if (ImGui::DragFloat2("near / far", &depth.x, 0.1f, 0.001f, 100000.f) && depth.y > depth.x)
                camera.SetNearFar(depth.x, depth.y);

            DrawController(camera);
            ImGui::PopID();
        }

    private:
        static bool Header(const kcam::Camera& camera, const char* kind)
        {
            return ImGui::CollapsingHeader(std::format("{} ({})", camera.Name(), kind).c_str(),
                                           ImGuiTreeNodeFlags_DefaultOpen);
        }

        static void DrawPose(kcam::Camera& camera)
        {
            glm::vec3 position = camera.Position();
            if (ImGui::DragFloat3("position", &position.x, 0.1f)) camera.SetPosition(position);
        }

        static void DrawAspect(kcam::PerspectiveCamera& camera)
        {
            // A camera following a framebuffer or an image is following something this panel cannot
            // offer — it has no way to name one — so it says what is happening and gives the way back
            // out, rather than a checkbox that would silently drop the target.
            using Kind = kcam::AspectSource::Kind;
            const Kind source = camera.AspectSourceSettings().kind;

            if (source == Kind::eFramebuffer || source == Kind::eImage) {
                ImGui::TextUnformatted(source == Kind::eFramebuffer
                    ? "aspect follows a framebuffer" : "aspect follows an image");
                ImGui::SameLine();
                if (ImGui::SmallButton("release")) camera.SetAspectSource(kcam::AspectSource::None());
            } else {
                bool follows = camera.FollowsWindowAspect();
                if (ImGui::Checkbox("follow window aspect", &follows))
                    camera.SetFollowWindowAspect(follows);
            }

            ImGui::BeginDisabled(camera.AspectSourceSettings().kind != Kind::eNone);
            float aspect = camera.Aspect();
            if (ImGui::DragFloat("aspect", &aspect, 0.01f, 0.1f, 10.f, "%.3f")) camera.SetAspect(aspect);
            ImGui::EndDisabled();
        }

        /**
         * @brief The controller, edited as one value.
         *
         * One value in, one value out: the panel edits a copy and hands the whole thing back if
         * anything moved, which is exactly what the API offers a project.
         */
        void DrawController(kcam::Camera& camera)
        {
            kcam::Controller controller = camera.ControllerSettings();
            bool changed = false;

            static constexpr const char* kKinds[] { "none", "fly", "orbit" };
            int kind = static_cast<int>(controller.kind);
            if (ImGui::Combo("controller", &kind, kKinds, 3)) {
                controller.kind = static_cast<kcam::Controller::Kind>(kind);
                changed = true;
            }

            if (controller.kind == kcam::Controller::Kind::eFly) {
                changed |= ImGui::DragFloat("speed", &controller.speed, 0.1f, 0.1f, 100.f, "%.1f u/s");
                changed |= ImGui::DragFloat("boost", &controller.bindings.boostFactor,
                                            0.1f, 1.f, 20.f, "x%.1f");
            }
            if (controller.kind == kcam::Controller::Kind::eOrbit)
                changed |= ImGui::DragFloat3("orbit target", &controller.orbitTarget.x, 0.1f);

            if (controller.kind != kcam::Controller::Kind::eNone) {
                // Whether the camera currently has the cursor, and the way to take it back without
                // hunting for the release chord.
                bool released = camera.Released();
                if (ImGui::Checkbox("released (the cursor is the user's)", &released))
                    camera.SetReleased(released);

                changed |= DrawBindings(camera, controller.bindings,
                                        controller.kind == kcam::Controller::Kind::eOrbit);
            }

            if (changed) camera.SetController(controller);
        }

        // ---- naming things, for the bindings interface -----------------------------------------

        static const char* ActionName(const kcam::Action action)
        {
            switch (action) {
            case kcam::Action::eMoveForward: return "forward";
            case kcam::Action::eMoveBack:    return "back";
            case kcam::Action::eMoveLeft:    return "left";
            case kcam::Action::eMoveRight:   return "right";
            case kcam::Action::eMoveUp:      return "up";
            case kcam::Action::eMoveDown:    return "down";
            case kcam::Action::eBoost:       return "boost";
            case kcam::Action::eCount:       break;
            }
            return "?";
        }

        static std::string InputName(const kcam::Input& input)
        {
            std::string prefix;
            if (input.modifiers & kcam::Modifier::eCtrl)  prefix += "Ctrl+";
            if (input.modifiers & kcam::Modifier::eShift) prefix += "Shift+";
            if (input.modifiers & kcam::Modifier::eAlt)   prefix += "Alt+";
            if (input.modifiers & kcam::Modifier::eSuper) prefix += "Super+";

            switch (input.type) {
            case kcam::Input::Type::eAlways:
                return "always on";
            case kcam::Input::Type::eKey:
                return prefix + kor::Input::Describe(static_cast<kor::Key>(input.code));
            case kcam::Input::Type::eMouseButton:
                return prefix + kor::Input::Describe(static_cast<kor::MouseButton>(input.code));
            case kcam::Input::Type::eNone:
                break;
            }
            return "unbound";
        }

        /** @brief Which modifiers are held right now, so a rebind captures the whole chord. */
        static kcam::Modifier HeldModifiers()
        {
            const auto either = [](const kor::Key left, const kor::Key right) {
                return kor::Scene::Input::IsKeyHeld(left) || kor::Scene::Input::IsKeyPressed(left)
                    || kor::Scene::Input::IsKeyHeld(right) || kor::Scene::Input::IsKeyPressed(right);
            };

            auto modifiers = kcam::Modifier::eNone;
            if (either(kor::Key::eLeftControl, kor::Key::eRightControl)) modifiers = modifiers | kcam::Modifier::eCtrl;
            if (either(kor::Key::eLeftShift,   kor::Key::eRightShift))   modifiers = modifiers | kcam::Modifier::eShift;
            if (either(kor::Key::eLeftAlt,     kor::Key::eRightAlt))     modifiers = modifiers | kcam::Modifier::eAlt;
            if (either(kor::Key::eLeftSuper,   kor::Key::eRightSuper))   modifiers = modifiers | kcam::Modifier::eSuper;
            return modifiers;
        }

        /** @brief Whether @p key is one of the modifiers, which cannot be a binding on their own. */
        static bool IsModifier(const kor::Key key)
        {
            switch (key) {
            case kor::Key::eLeftControl: case kor::Key::eRightControl:
            case kor::Key::eLeftShift:   case kor::Key::eRightShift:
            case kor::Key::eLeftAlt:     case kor::Key::eRightAlt:
            case kor::Key::eLeftSuper:   case kor::Key::eRightSuper:
                return true;
            default:
                return false;
            }
        }

        /**
         * @brief The key or button that completes a rebind, with whatever modifiers are held.
         *
         * A modifier alone is skipped rather than bound: pressing Ctrl on the way to Ctrl+Shift+O
         * would otherwise end the rebind with "Ctrl" the moment it went down. Mouse buttons only
         * count while the pointer is outside every window, so clicking around the panel cannot bind
         * itself by accident.
         */
        static std::optional<kcam::Input> FirstInputPressed()
        {
            if (const auto key = kor::Scene::Input::FirstKeyPressed(); key && !IsModifier(*key))
                return kcam::Input::FromKey(*key, HeldModifiers());

            if (!kor::Scene::Input::InterfaceWantsMouse()) {
                if (const auto button = kor::Scene::Input::FirstMouseButtonPressed())
                    return kcam::Input::FromMouse(*button, HeldModifiers());
            }
            return std::nullopt;
        }

        /**
         * @brief One rebindable input: what it is bound to now, clicked to bind it to something else.
         * @param gate Adds an "always on" toggle. A gate is the thing anyone actually wants to switch
         *        off, and "always on" is a state no keypress can arm.
         */
        bool DrawInput(const kcam::Camera& camera, const char* label, kcam::Input& input, const int id,
                       const bool gate = false)
        {
            const bool waiting = _arming == &camera && _armed == id;
            bool changed = false;

            ImGui::PushID(id);
            const bool always = input.type == kcam::Input::Type::eAlways;

            ImGui::BeginDisabled(always);
            if (ImGui::Button(waiting ? "press a key or click the scene..." : InputName(input).c_str(),
                              ImVec2(220.f, 0.f)))
            {
                _arming = &camera;
                _armed = id;
            }
            ImGui::EndDisabled();

            if (gate) {
                ImGui::SameLine();
                bool on = always;
                if (ImGui::Checkbox("always", &on)) {
                    // Off leaves it unbound rather than restoring what was bound before: the panel
                    // does not keep a shadow copy, and unbound is the honest "nothing opens this".
                    input = on ? kcam::Input::Always() : kcam::Input{};
                    changed = true;
                    if (waiting) Disarm();
                }
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(label);

            if (waiting) {
                if (const auto pressed = FirstInputPressed()) {
                    // Escape clears the binding rather than binding escape — an unbound input is a
                    // real thing to want, and there is no other way to say it.
                    input = *pressed == kcam::Input::FromKey(kor::Key::eEsc) ? kcam::Input{} : *pressed;
                    changed = true;
                    Disarm();
                }
            }
            ImGui::PopID();
            return changed;
        }

        /** @brief One axis: where it reads from, how hard, and which way round. */
        static bool DrawAxis(const char* label, kcam::Axis& axis, const float speed, const char* format)
        {
            bool changed = false;

            ImGui::PushID(label);
            if (ImGui::TreeNode(label)) {
                static constexpr const char* kSources[] { "none", "mouse X", "mouse Y",
                                                          "scroll X", "scroll Y" };
                int source = static_cast<int>(axis.source);
                if (ImGui::Combo("source", &source, kSources, 5)) {
                    axis.source = static_cast<kcam::AxisSource>(source);
                    changed = true;
                }
                changed |= ImGui::DragFloat("sensitivity", &axis.sensitivity, speed, 0.f, 10.f, format);
                changed |= ImGui::Checkbox("invert", &axis.invert);
                ImGui::TreePop();
            }
            ImGui::PopID();
            return changed;
        }

        /** @brief Everything the controller reads: the gates, the actions, and the axes. */
        bool DrawBindings(const kcam::Camera& camera, kcam::Bindings& bindings, const bool orbiting)
        {
            if (!ImGui::TreeNode("controls")) return false;

            bool changed = false;

            // The gates first: what has to be held for any of the rest to be read at all, and the two
            // that hand the cursor over. Negative ids so they cannot collide with an action's.
            changed |= DrawInput(camera, "enable", bindings.enable, -1, true);
            changed |= DrawInput(camera, "look", bindings.look, -2, true);
            changed |= DrawInput(camera, "release the cursor", bindings.release, -3);
            changed |= DrawInput(camera, "take it back", bindings.engage, -4);
            ImGui::Separator();

            for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(kcam::Action::eCount); ++i) {
                const auto action = static_cast<kcam::Action>(i);
                changed |= DrawInput(camera, ActionName(action), bindings[action], i);
            }

            ImGui::Separator();
            changed |= DrawAxis("yaw", bindings.yaw, 0.0005f, "%.4f rad");
            changed |= DrawAxis("pitch", bindings.pitch, 0.0005f, "%.4f rad");
            if (orbiting) changed |= DrawAxis("zoom", bindings.zoom, 0.005f, "%.3f");

            ImGui::TreePop();
            return changed;
        }

        void Disarm() { _arming = nullptr; _armed = -1; }

        /// Which control is waiting for a key, and whose. At most one rebind is ever in flight.
        const kcam::Camera* _arming = nullptr;
        int _armed = -1;
    };
}
