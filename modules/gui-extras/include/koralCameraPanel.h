/**
 * @file koralCameraPanel.h
 * @brief Where the GUI extras meet the camera module: a debugging view of a camera, as a koral-ui panel.
 *
 * Pose, projection, controller and every binding, editable while the scene runs. It is a development
 * tool — the sort of thing you want while tuning a fly camera and never in a shipped interface.
 *
 * @code
 * kui::Ui _ui { kui::DockSpace(layout, {{"cameras", "Cameras", kgui::CameraPanel(*_player, *_minimap)}}) };
 * @endcode
 *
 * **The camera module does not draw this, or anything else.** Nothing appears unless a scene shows it,
 * and a project that wants a different interface writes one — everything here is built on the public
 * API in koralCamera.h and kor::Input, so there is nothing this can do that a project cannot. Read it
 * as a worked example as much as a widget.
 *
 * Like koralModelMesh.h this is a *seam*: it is templates and inline functions, compiled into whoever
 * includes it, so `koral-gui-extras` does not link `koral-camera` and `koral-camera` knows nothing about
 * interfaces. It needs both modules linked, and says so if one is missing. The cameras must outlive it.
 */

#pragma once

// The compatibility gate. Including this header is a statement that both modules are in play, so say
// plainly which one is missing instead of failing on the include below.
#if !__has_include(<koralCamera.h>)
#  error "koralCameraPanel.h draws the camera module's cameras, and the camera module is not here. Link koral-camera too (target_link_libraries(<target> PRIVATE Koral::koral-gui-extras Koral::koral-camera)), or drop this include if you do not use cameras."
#endif

#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include <input.h>
#include <scene.h>

#include <koralCamera.h>

#include "kgui/layout.h"

namespace kgui
{
    namespace detail
    {
        using CameraRef = std::variant<kcam::PerspectiveCamera*, kcam::OrthographicCamera*>;

        /**
         * @brief The panel: each camera under a header of its own. Holds only interface state — which
         *        sections are open, which control is waiting for a key — so two of them do not interfere.
         */
        class CameraPanelWidget final : public Live {
        public:
            explicit CameraPanelWidget(std::vector<CameraRef> cameras) : _cameras(std::move(cameras)) {}

            void DidUpdateWidget(const kui::StatefulWidget& newer) override
            {
                _cameras = static_cast<const CameraPanelWidget&>(newer)._cameras;
            }

            kui::Widget Build() override
            {
                std::vector<kui::Widget> sections;
                if (_cameras.empty()) sections.push_back(Muted("no cameras"));
                for (const auto& camera : _cameras) std::visit([&](auto* c) { sections.push_back(Section(*c)); }, camera);
                return kui::ScrollView(kui::Padding(kui::EdgeInsets::All(10.f),
                                                    kui::Column(std::move(sections), kui::FlexOptions {}.SetGap(8.f)
                                                        .SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch))));
            }

        protected:
            bool Poll(const float dt) override
            {
                // A rebind waits for the next key or click — one frame long, so it is looked for every frame.
                if (_arming) {
                    if (const auto pressed = FirstInputPressed()) {
                        // Escape clears the binding rather than binding escape — an unbound input is a
                        // real thing to want, and there is no other way to say it.
                        Rebind(*_arming, _armed, *pressed == kcam::Input::FromKey(kor::Key::eEsc) ? kcam::Input{} : *pressed);
                        Disarm();
                        return true;
                    }
                }
                // The cameras move under the panel — a fly camera is being flown — so it follows them.
                return Every(0.1f, dt);
            }

        private:
            // ---- one camera ---------------------------------------------------------------------------

            template<typename Camera>
            kui::Widget Section(Camera& camera)
            {
                constexpr bool perspective = std::is_same_v<Camera, kcam::PerspectiveCamera>;
                std::vector<kui::Widget> rows;
                rows.push_back(Labeled("position", DragFloats({camera.Position().x, camera.Position().y, camera.Position().z},
                    [&camera](const std::vector<float>& v) { camera.SetPosition({v[0], v[1], v[2]}); }, 0.1f)));
                if constexpr (perspective) {
                    rows.push_back(Labeled("fov", kui::DragValue(glm::degrees(camera.FovY()),
                        [&camera](const float degrees) { camera.SetFovY(glm::radians(degrees)); },
                        kui::DragValueOptions {}.SetRange(10.f, 140.f).SetDecimals(0).SetSpeed(0.5f))));
                    Aspect(camera, rows);
                    rows.push_back(Labeled("near / far", DragFloats({camera.ZNear(), camera.ZFar()}, [&camera](const std::vector<float>& v) {
                        if (v[0] > 0.f && v[1] > v[0]) camera.SetNearFar(v[0], v[1]);
                    }, 0.1f, 3, 0.001f, 100000.f)));
                } else {
                    const glm::vec4 bounds = camera.Bounds();
                    rows.push_back(Labeled("l / r / b / t", DragFloats({bounds.x, bounds.y, bounds.z, bounds.w},
                        [&camera](const std::vector<float>& v) { camera.SetBounds(v[0], v[1], v[2], v[3]); }, 0.1f)));
                    rows.push_back(Labeled("near / far", DragFloats({camera.ZNear(), camera.ZFar()}, [&camera](const std::vector<float>& v) {
                        if (v[1] > v[0]) camera.SetNearFar(v[0], v[1]);
                    }, 0.1f, 3, 0.001f, 100000.f)));
                }
                Controller(camera, rows);

                const void* key = &camera;
                const bool open = !_closed.contains(key);
                return kui::CollapsingHeader(std::format("{} ({})", camera.Name(), perspective ? "perspective" : "orthographic"), open,
                    [this, key](const bool now) { SetState([&] { if (now) _closed.erase(key); else _closed[key] = true; }); },
                    kui::Column(std::move(rows), kui::FlexOptions {}.SetGap(6.f).SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch)));
            }

            void Aspect(kcam::PerspectiveCamera& camera, std::vector<kui::Widget>& rows)
            {
                // A camera following a framebuffer or an image is following something this panel cannot
                // offer — it has no way to name one — so it says what is happening and gives the way back
                // out, rather than a checkbox that would silently drop the target.
                using Kind = kcam::AspectSource::Kind;
                const Kind source = camera.AspectSourceSettings().kind;
                if (source == Kind::eFramebuffer || source == Kind::eImage) {
                    rows.push_back(kui::Row({
                        kui::Expanded(Muted(source == Kind::eFramebuffer ? "aspect follows a framebuffer" : "aspect follows an image")),
                        kui::Button("release", [&camera] { camera.SetAspectSource(kcam::AspectSource::None()); },
                                    kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                    }, kui::FlexOptions {}.SetGap(8.f)));
                } else {
                    rows.push_back(kui::Checkbox(camera.FollowsWindowAspect(),
                        [&camera](const bool follows) { camera.SetFollowWindowAspect(follows); }, "follow window aspect"));
                }
                rows.push_back(kui::Disabled(Labeled("aspect", kui::DragValue(camera.Aspect(), [&camera](const float a) { camera.SetAspect(a); },
                    kui::DragValueOptions {}.SetRange(0.1f, 10.f).SetDecimals(3))), source != Kind::eNone));
            }

            /**
             * @brief The controller, edited as one value: the panel edits a copy and hands the whole thing
             *        back when anything moved, which is exactly what the API offers a project.
             */
            void Controller(kcam::Camera& camera, std::vector<kui::Widget>& rows)
            {
                const kcam::Controller controller = camera.ControllerSettings();
                const auto edit = [&camera](auto change) {
                    return [&camera, change](auto value) {
                        kcam::Controller c = camera.ControllerSettings();
                        change(c, value);
                        camera.SetController(c);
                    };
                };
                rows.push_back(Labeled("controller", kui::Dropdown({"none", "fly", "orbit"}, static_cast<int>(controller.kind),
                    edit([](kcam::Controller& c, const int kind) { c.kind = static_cast<kcam::Controller::Kind>(kind); }))));

                if (controller.kind == kcam::Controller::Kind::eFly) {
                    rows.push_back(Labeled("speed", kui::DragValue(controller.speed, edit([](kcam::Controller& c, const float v) { c.speed = v; }),
                        kui::DragValueOptions {}.SetRange(0.1f, 100.f).SetDecimals(1).SetSpeed(0.1f))));
                    rows.push_back(Labeled("boost", kui::DragValue(controller.bindings.boostFactor,
                        edit([](kcam::Controller& c, const float v) { c.bindings.boostFactor = v; }),
                        kui::DragValueOptions {}.SetRange(1.f, 20.f).SetDecimals(1).SetSpeed(0.1f))));
                }
                if (controller.kind == kcam::Controller::Kind::eOrbit) {
                    const glm::vec3 target = controller.orbitTarget;
                    rows.push_back(Labeled("orbit target", DragFloats({target.x, target.y, target.z},
                        edit([](kcam::Controller& c, const std::vector<float>& v) { c.orbitTarget = {v[0], v[1], v[2]}; }), 0.1f)));
                }
                if (controller.kind == kcam::Controller::Kind::eNone) return;

                // Whether the camera currently has the cursor, and the way to take it back without hunting
                // for the release chord.
                rows.push_back(kui::Checkbox(camera.Released(), [&camera](const bool released) { camera.SetReleased(released); },
                                             "released (the cursor is the user's)"));
                rows.push_back(Bindings(camera, controller.bindings, controller.kind == kcam::Controller::Kind::eOrbit));
            }

            // ---- the bindings ---------------------------------------------------------------------------

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
                case kcam::Input::Type::eAlways:      return "always on";
                case kcam::Input::Type::eKey:         return prefix + kor::Input::Describe(static_cast<kor::Key>(input.code));
                case kcam::Input::Type::eMouseButton: return prefix + kor::Input::Describe(static_cast<kor::MouseButton>(input.code));
                case kcam::Input::Type::eNone:        break;
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
             * A modifier alone is skipped rather than bound: pressing Ctrl on the way to Ctrl+Shift+O would
             * otherwise end the rebind with "Ctrl" the moment it went down. Mouse buttons only count while
             * the pointer is off the interface, so clicking around the panel cannot bind itself by accident.
             */
            static std::optional<kcam::Input> FirstInputPressed()
            {
                if (const auto key = kor::Scene::Input::FirstKeyPressed(); key && !IsModifier(*key))
                    return kcam::Input::FromKey(*key, HeldModifiers());
                if (!kor::Scene::Input::InterfaceWantsMouse())
                    if (const auto button = kor::Scene::Input::FirstMouseButtonPressed())
                        return kcam::Input::FromMouse(*button, HeldModifiers());
                return std::nullopt;
            }

            /** @brief The binding @p id names: the gates are negative, the actions their own number. */
            static kcam::Input& Binding(kcam::Bindings& bindings, const int id)
            {
                switch (id) {
                case -1: return bindings.enable;
                case -2: return bindings.look;
                case -3: return bindings.release;
                case -4: return bindings.engage;
                default: return bindings[static_cast<kcam::Action>(id)];
                }
            }

            void Rebind(kcam::Camera& camera, const int id, const kcam::Input input)
            {
                kcam::Controller controller = camera.ControllerSettings();
                Binding(controller.bindings, id) = input;
                camera.SetController(controller);
            }

            /**
             * @brief One rebindable input: what it is bound to now, pressed to bind it to something else.
             * @param gate Adds an "always on" toggle. A gate is the thing anyone actually wants to switch
             *        off, and "always on" is a state no keypress can arm.
             */
            kui::Widget Input(kcam::Camera& camera, const std::string& label, const kcam::Input& input, const int id, const bool gate = false)
            {
                const bool waiting = _arming == &camera && _armed == id;
                const bool always = input.type == kcam::Input::Type::eAlways;
                std::vector<kui::Widget> parts;
                parts.push_back(kui::Disabled(kui::Button(waiting ? "press a key or click the scene..." : InputName(input),
                    [this, &camera, id] { SetState([&] { _arming = &camera; _armed = id; }); },
                    kui::ButtonOptions {}.SetStyle(waiting ? kui::ButtonStyle::ePrimary : kui::ButtonStyle::eSecondary).SetWidth(220.f)), always));
                if (gate)
                    parts.push_back(kui::Checkbox(always, [this, &camera, id](const bool on) {
                        // Off leaves it unbound rather than restoring what was bound before: the panel does
                        // not keep a shadow copy, and unbound is the honest "nothing opens this".
                        Rebind(camera, id, on ? kcam::Input::Always() : kcam::Input{});
                        SetState([&] { if (_arming == &camera && _armed == id) Disarm(); });
                    }, "always"));
                parts.push_back(kui::Expanded(Line(label)));
                return kui::Row(std::move(parts), kui::FlexOptions {}.SetGap(8.f));
            }

            /** @brief One axis: where it reads from, how hard, and which way round. */
            kui::Widget Axis(kcam::Camera& camera, const std::string& label, const kcam::Axis& axis, const float speed,
                             kcam::Axis kcam::Bindings::* member)
            {
                const auto edit = [&camera, member](auto change) {
                    return [&camera, member, change](auto value) {
                        kcam::Controller c = camera.ControllerSettings();
                        change(c.bindings.*member, value);
                        camera.SetController(c);
                    };
                };
                const std::string key = std::format("{}:{}", static_cast<const void*>(&camera), label);
                return kui::TreeNode(label, _openTrees.contains(key), [this, key](const bool open) {
                    SetState([&] { if (open) _openTrees[key] = true; else _openTrees.erase(key); });
                }, {
                    Labeled("source", kui::Dropdown({"none", "mouse X", "mouse Y", "scroll X", "scroll Y"}, static_cast<int>(axis.source),
                        edit([](kcam::Axis& a, const int source) { a.source = static_cast<kcam::AxisSource>(source); }))),
                    Labeled("sensitivity", kui::DragValue(axis.sensitivity, edit([](kcam::Axis& a, const float v) { a.sensitivity = v; }),
                        kui::DragValueOptions {}.SetRange(0.f, 10.f).SetDecimals(4).SetSpeed(speed))),
                    kui::Checkbox(axis.invert, edit([](kcam::Axis& a, const bool v) { a.invert = v; }), "invert"),
                });
            }

            /** @brief Everything the controller reads: the gates, the actions, and the axes. */
            kui::Widget Bindings(kcam::Camera& camera, kcam::Bindings bindings, const bool orbiting)
            {
                // The gates first: what has to be held for any of the rest to be read at all, and the two
                // that hand the cursor over.
                std::vector<kui::Widget> rows {
                    Input(camera, "enable", bindings.enable, -1, true),
                    Input(camera, "look", bindings.look, -2, true),
                    Input(camera, "release the cursor", bindings.release, -3),
                    Input(camera, "take it back", bindings.engage, -4),
                    kui::Separator(),
                };
                for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(kcam::Action::eCount); ++i) {
                    const auto action = static_cast<kcam::Action>(i);
                    rows.push_back(Input(camera, ActionName(action), bindings[action], i));
                }
                rows.push_back(kui::Separator());
                rows.push_back(Axis(camera, "yaw", bindings.yaw, 0.0005f, &kcam::Bindings::yaw));
                rows.push_back(Axis(camera, "pitch", bindings.pitch, 0.0005f, &kcam::Bindings::pitch));
                if (orbiting) rows.push_back(Axis(camera, "zoom", bindings.zoom, 0.005f, &kcam::Bindings::zoom));

                const std::string key = std::format("{}:controls", static_cast<const void*>(&camera));
                return kui::TreeNode("controls", _openTrees.contains(key), [this, key](const bool open) {
                    SetState([&] { if (open) _openTrees[key] = true; else _openTrees.erase(key); });
                }, std::move(rows));
            }

            void Disarm() { _arming = nullptr; _armed = -1; }

            std::vector<CameraRef> _cameras;
            std::map<const void*, bool> _closed;            // the cameras whose section is folded away
            std::map<std::string, bool> _openTrees;
            /// Which control is waiting for a key, and whose. At most one rebind is ever in flight.
            kcam::Camera* _arming = nullptr;
            int _armed = -1;
        };
    }

    /**
     * @brief A panel for editing @p cameras: pose, projection, controller, and the bindings it reads.
     *        Perspective and orthographic may be mixed; they are held by reference.
     */
    template<typename... Cameras>
    kui::Widget CameraPanel(Cameras&... cameras)
    {
        return kui::Make<detail::CameraPanelWidget>(std::vector<detail::CameraRef> { &cameras... });
    }
}
