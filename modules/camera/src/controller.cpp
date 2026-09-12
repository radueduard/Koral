//
// Created by radue on 29.07.2026.
//

#include "controller.h"

#include <cmath>

#include <input.h>

namespace kcam
{
    namespace
    {
        constexpr float kMaxPitch = glm::radians(89.f);

        /** @brief Whether a key is down at all — the frame it goes down included. */
        bool down(const kor::Key key)
        {
            // Both states, not just eHeld: a key is ePressed on the frame it arrives and eHeld only
            // from the next one. Asking for eHeld alone costs a frame on every action and, worse,
            // makes a chord depend on the order its keys happened to be polled in.
            return kor::Input::isKeyPressed(key) || kor::Input::isKeyHeld(key);
        }

        /** @brief Whether every modifier @p required names is down. Left and right count the same. */
        bool modifiersHeld(const Modifier required)
        {
            const auto either = [](const kor::Key left, const kor::Key right)
            { return down(left) || down(right); };

            if ((required & Modifier::eCtrl)  && !either(kor::Key::eLeftControl, kor::Key::eRightControl)) return false;
            if ((required & Modifier::eShift) && !either(kor::Key::eLeftShift,   kor::Key::eRightShift))   return false;
            if ((required & Modifier::eAlt)   && !either(kor::Key::eLeftAlt,     kor::Key::eRightAlt))     return false;
            if ((required & Modifier::eSuper) && !either(kor::Key::eLeftSuper,   kor::Key::eRightSuper))   return false;
            return true;
        }

        // The two constant states are the point of the type: an unbound input is not "the key at
        // position zero is held", it never fires, and eAlways needs nothing pressed. Gates and
        // actions therefore read the same way — there is no second convention for what an absent
        // binding means.
        bool held(const Input& input)
        {
            switch (input.type) {
            case Input::Type::eAlways:
                return true;
            case Input::Type::eKey:
                return down(static_cast<kor::Key>(input.code)) && modifiersHeld(input.modifiers);
            case Input::Type::eMouseButton:
                return (kor::Input::isMouseButtonPressed(static_cast<kor::MouseButton>(input.code)) ||
                        kor::Input::isMouseButtonHeld(static_cast<kor::MouseButton>(input.code)))
                    && modifiersHeld(input.modifiers);
            case Input::Type::eNone:
                break;
            }
            return false;
        }

        /** @brief This frame's value of @p axis, scaled and signed. Zero if it reads nothing. */
        float value(const Axis& axis)
        {
            float raw = 0.f;
            switch (axis.source) {
            case AxisSource::eMouseX:  raw = kor::Input::mousePositionDelta().x; break;
            case AxisSource::eMouseY:  raw = kor::Input::mousePositionDelta().y; break;
            case AxisSource::eScrollX: raw = kor::Input::mouseScrollDelta().x; break;
            case AxisSource::eScrollY: raw = kor::Input::mouseScrollDelta().y; break;
            case AxisSource::eNone:    return 0.f;
            }
            return raw * axis.sensitivity * (axis.invert ? -1.f : 1.f);
        }
    }

    void CameraController::set(const Controller& controller)
    {
        // Only a change of behaviour invalidates the angles. Rewriting the speed or the orbit
        // target — which the panel does on every frame the user drags a slider — must not, or the
        // controller would keep re-deriving from a camera it is itself moving.
        if (controller.kind != _controller.kind) _anglesValid = false;
        _controller = controller;
    }

    void CameraController::pollGrip()
    {
        const Bindings& bindings = _controller.bindings;

        // Both edge-triggered, so a held chord fires once rather than sixty times a second — and
        // both read whatever the behaviour is, because the way out of a captured cursor must not
        // depend on the controller being in a state that can be left.
        const bool releasing = held(bindings.release);
        const bool engaging = held(bindings.engage);

        if (releasing && !_releaseHeld) {
            setReleased(true);
        }
        else if (_released && engaging && !_engageHeld && ownsMouse()) {
            // Only where the controller may read the mouse: while released the pointer is the
            // user's, and a click on a panel has to stay a click on that panel. Taking the cursor
            // back is what clicking on the *scene* means. @see ownsMouse
            setReleased(false);
        }

        _releaseHeld = releasing;
        _engageHeld = engaging;
    }

    void CameraController::setReleased(const bool released)
    {
        _released = released;
        if (!released) return;

        // Handed back here and now rather than on the next frame: a scene that releases in order to
        // show a menu wants the pointer for that menu, not one frame later.
        applyCursor(false);
        _looking = false;
    }

    void CameraController::update(Camera& camera, const float dt)
    {
        pollGrip();

        // Parked. Not the same as Kind::eNone: the configuration and the angles are all still here,
        // so taking the grip back resumes rather than re-derives.
        if (_released) { applyCursor(false); _looking = false; return; }

        switch (_controller.kind) {
            case Controller::Kind::eFly:   fly(camera, dt); break;
            case Controller::Kind::eOrbit: orbit(camera); break;
            case Controller::Kind::eNone:
                // Switched off mid-look: the cursor is still captured and nothing else would give it
                // back, leaving the pointer invisible with no way to reach anything.
                applyCursor(false);
                break;
        }
    }

    void CameraController::deriveAngles(const Camera& camera)
    {
        const glm::vec3 f = camera.forward();
        _pitch = std::asin(glm::clamp(f.y, -1.f, 1.f));
        _yaw = std::atan2(-f.x, -f.z);
        _anglesValid = true;
    }

    void CameraController::applyAngles(Camera& camera) const
    {
        camera.setRotation(glm::angleAxis(_yaw, glm::vec3(0.f, 1.f, 0.f)) *
                           glm::angleAxis(_pitch, glm::vec3(1.f, 0.f, 0.f)));
    }

    void CameraController::look(Camera& camera)
    {
        const Bindings& bindings = _controller.bindings;
        _yaw -= value(bindings.yaw);
        _pitch = glm::clamp(_pitch - value(bindings.pitch), -kMaxPitch, kMaxPitch);
        applyAngles(camera);
    }

    void CameraController::applyCursor(const bool looking)
    {
        if (_controller.cursor == Controller::Cursor::eLeaveAlone) return;
        if (looking == _holdingCursor) return;

        if (looking) {
            _holdingCursor = true;
            kor::Input::setCursorMode(_controller.cursor == Controller::Cursor::eHide
                ? kor::Input::CursorMode::eHidden
                : kor::Input::CursorMode::eCaptured);
        } else {
            _holdingCursor = false;
            kor::Input::setCursorMode(kor::Input::CursorMode::eNormal);
        }
    }

    bool CameraController::ownsMouse() const
    {
        switch (_controller.input) {
        case Controller::Input::eDisabled:
            _looking = false;
            return false;
        case Controller::Input::eEnabled:
            return true;
        default: {
            // The automatic answer, and the latch that keeps a look going: ImGui reports that it wants
            // the mouse as soon as the pointer is over any window, so without this a look that began on
            // the scene would end the moment the pointer crossed a panel.
            if (_looking) return true;
            return !kor::Input::interfaceWantsMouse();
        }
        }
    }

    void CameraController::fly(Camera& camera, const float dt)
    {
        const Bindings& bindings = _controller.bindings;

        // Whether the camera is being aimed right now, which decides both the look and the cursor.
        // Computed before the gate rather than after it, because the *enable* binding is usually the
        // same button that starts the look — so releasing it is the ordinary way a look ends, and an
        // early return here would leave the cursor captured with nothing left to give it back.
        const bool looking = held(bindings.enable) && ownsMouse() && held(bindings.look);
        applyCursor(looking);
        _looking = looking;

        // The gate: while it is closed the controller reads nothing, so a key it would otherwise
        // answer to is free to mean something else.
        if (!held(bindings.enable)) return;

        if (!_anglesValid) deriveAngles(camera);

        if (looking) look(camera);

        // The keyboard is only ever ImGui's while something is being typed into, which is a question
        // the scene has no better answer to — so this one stays automatic even when input does not.
        if (kor::Input::interfaceWantsKeyboard() && _controller.input != Controller::Input::eEnabled) return;
        if (_controller.input == Controller::Input::eDisabled) return;

        glm::vec3 move { 0.f };
        const glm::quat rotation = camera.rotation();
        const glm::vec3 forward = rotation * glm::vec3(0.f, 0.f, -1.f);
        const glm::vec3 right   = rotation * glm::vec3(1.f, 0.f, 0.f);
        constexpr glm::vec3 up { 0.f, 1.f, 0.f };

        if (held(bindings.moveForward)) move += forward;
        if (held(bindings.moveBack))    move -= forward;
        if (held(bindings.moveRight))   move += right;
        if (held(bindings.moveLeft))    move -= right;
        if (held(bindings.moveUp))      move += up;
        if (held(bindings.moveDown))    move -= up;

        if (glm::dot(move, move) < 1e-12f) return;

        const float boost = held(bindings.boost) ? bindings.boostFactor : 1.f;
        camera.setPosition(camera.position() + glm::normalize(move) * _controller.speed * boost * dt);
    }

    void CameraController::orbit(Camera& camera)
    {
        const Bindings& bindings = _controller.bindings;

        // Before the gate, for the reason given in fly(): releasing the enable button is how a look
        // ends, and the cursor has to come back with it.
        const bool looking = held(bindings.enable) && ownsMouse() && held(bindings.look);
        applyCursor(looking);

        if (!held(bindings.enable)) return;

        if (!_anglesValid) deriveAngles(camera);

        float distance = glm::distance(camera.position(), _controller.orbitTarget);
        if (distance < 1e-3f) distance = 1e-3f;

        if (ownsMouse()) {
            _looking = held(bindings.look);
            if (held(bindings.look)) {
                _yaw -= value(bindings.yaw);
                _pitch = glm::clamp(_pitch - value(bindings.pitch), -kMaxPitch, kMaxPitch);
            }
            // Exponential: each unit of the zoom axis *scales* the distance, so it feels the same
            // whether the camera is 2 units out or 200.
            if (const float zoom = value(bindings.zoom); zoom != 0.f)
                distance *= std::exp(-zoom);
        }

        // The camera sits on a sphere around the target, at yaw/pitch, looking inward.
        applyAngles(camera);
        const glm::vec3 back = camera.rotation() * glm::vec3(0.f, 0.f, 1.f);
        camera.setPosition(_controller.orbitTarget + back * distance);
    }
}
