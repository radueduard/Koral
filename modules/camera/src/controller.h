//
// Created by radue on 29.07.2026.
//

// The per-frame behaviours a camera can be handed to: the public kcam::Controller value, plus the
// state running it needs, plus the input reading itself. Internal to the module. A camera knows
// nothing about this — it is moved through the same Camera interface a project would use by hand.

#pragma once

#include <glm/glm.hpp>

#include <koralCamera.h>

namespace kcam
{
    /**
     * @brief A kcam::Controller in flight: the configuration, and the state driving it.
     *
     * The public struct is the whole of what a project sets, and it is stored here verbatim. What
     * this adds is the part a project has no business seeing: yaw and pitch, kept here rather than
     * re-derived from the camera every frame so the controller cannot drift or flip as pitch
     * approaches straight up or down. They are (re)derived from wherever the camera points the
     * first time a behaviour runs, so switching kind — or moving the camera by hand in between —
     * never snaps the view.
     */
    class CameraController
    {
    public:
        /** @brief Replaces the configuration. Re-derives the angles if the behaviour changed. */
        void set(const Controller& controller);

        [[nodiscard]] const Controller& get() const { return _controller; }

        /** @brief One frame of the current behaviour. Does nothing for Kind::eNone. */
        void update(Camera& camera, float dt);

        /** @brief Whether the controller has let go of the cursor and the input. @see Bindings::release */
        [[nodiscard]] bool released() const { return _released; }

        /** @brief Lets go, or takes it back. Giving the cursor up happens here and now. */
        void setReleased(bool released);

    private:
        /**
         * @brief Reads the release and engage bindings, and hands the cursor over accordingly.
         *
         * Run before anything else each frame, whatever the behaviour is: a camera whose gate is
         * closed still holds the cursor it took, and the way out of that cannot be behind the gate.
         */
        void pollGrip();

        void fly(Camera& camera, float dt);
        void orbit(Camera& camera);

        /**
         * @brief Whether the mouse belongs to this controller this frame.
         *
         * Controller::Input::eAutomatic asks ImGui, which is right until the scene is *inside* an ImGui
         * window — then the scene has to say. And once a look has started it keeps the mouse until the
         * button is released, however the answer changes in between: dragging the pointer off the
         * viewport mid-look must not cut the look in half.
         */
        [[nodiscard]] bool ownsMouse() const;

        /**
         * @brief Hides and locks the cursor while looking, and gives it back afterwards.
         *
         * Driven from the one place that knows a look has started or stopped, so a look that ends by
         * releasing the button, by the gate closing, or by the controller being switched off all give
         * the cursor back the same way. @see Controller::Cursor
         */
        void applyCursor(bool looking);

        /** @brief Turns this frame's mouse movement into yaw/pitch. */
        void look(Camera& camera);

        void deriveAngles(const Camera& camera);
        void applyAngles(Camera& camera) const;

        Controller _controller;
        float _yaw = 0.f;
        float _pitch = 0.f;
        bool _anglesValid = false;
        /// True from the frame a look began until the look button is released. @see ownsMouse
        mutable bool _looking = false;
        /// Whether *this* controller is the one currently holding the cursor, so it only ever gives
        /// back what it took. @see applyCursor
        bool _holdingCursor = false;
        /// The escape hatch: parked, reading nothing, cursor handed back. @see Bindings::release
        bool _released = false;
        /// Last frame's bindings, so each fires once per press and not once per frame. @see pollGrip
        bool _releaseHeld = false;
        bool _engageHeld = false;
    };
}
