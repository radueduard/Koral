//
// Created by radue on 29.07.2026.
//

// The camera objects themselves — the things a project holds a kor::Resource to. Internal to the
// module: consumers see only the interfaces in koralCamera.h, and this header is not installed.

#pragma once

#include <cstring>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include <buffer.h>
#include <gtime.h>

#include <koralCamera.h>

#include "controller.h"

namespace kcam
{
    /**
     * @brief The pose, the matrix caching and the controller: everything both kinds of camera share.
     *
     * Templated on the interface it fills in so PerspectiveImpl and OrthoImpl share it without a
     * diamond — both interfaces already derive from Camera. The projection stays abstract: it is
     * the one thing the two kinds actually differ in.
     */
    template<typename Interface>
    class CameraBase : public Interface
    {
    public:
        [[nodiscard]] glm::vec3 position() const override { return _position; }
        [[nodiscard]] glm::quat rotation() const override { return _rotation; }
        [[nodiscard]] glm::vec3 forward() const override { return _rotation * glm::vec3(0.f, 0.f, -1.f); }
        [[nodiscard]] std::string_view name() const override { return _name; }

        void setPosition(const glm::vec3 position) override
        {
            _position = position;
            _viewDirty = true;
        }

        void setRotation(const glm::quat rotation) override
        {
            _rotation = glm::normalize(rotation);
            _viewDirty = true;
        }

        void lookAt(const glm::vec3 target, const glm::vec3 up) override
        {
            const glm::vec3 to = target - _position;
            if (glm::dot(to, to) < 1e-12f) return;  // looking at yourself is not a direction
            setRotation(glm::quatLookAt(glm::normalize(to), up));
        }

        [[nodiscard]] const glm::mat4& view() const override
        {
            if (_viewDirty) {
                // inverse(translate * rotate), assembled directly rather than inverted.
                _view = glm::mat4_cast(glm::conjugate(_rotation)) *
                        glm::translate(glm::mat4(1.f), -_position);
                _viewDirty = false;
                _viewProjectionDirty = true;
            }
            return _view;
        }

        [[nodiscard]] const glm::mat4& projection() const override
        {
            if (_projectionDirty) {
                _projection = computeProjection();
                // Y points down in the engine's clip space (Vulkan conventions; the GL backend
                // matches them). Baked in here so every consumer just multiplies.
                _projection[1][1] *= -1.f;
                _projectionDirty = false;
                _viewProjectionDirty = true;
            }
            return _projection;
        }

        [[nodiscard]] const glm::mat4& viewProjection() const override
        {
            const glm::mat4& v = view();
            const glm::mat4& p = projection();
            if (_viewProjectionDirty) {
                _viewProjection = p * v;
                _viewProjectionDirty = false;
            }
            return _viewProjection;
        }

        // ---- per-frame behaviour ----------------------------------------------------------------

        void setController(const Controller& controller) override { _controller.set(controller); }
        [[nodiscard]] const Controller& controller() const override { return _controller.get(); }

        [[nodiscard]] bool released() const override { return _controller.released(); }
        void setReleased(const bool released) override { _controller.setReleased(released); }

        /**
         * @brief What the repository calls at the top of every frame.
         *
         * Every camera is registered, whether or not anything is driving it, so that turning a
         * controller on later is an ordinary setter rather than a rebuild. A camera with nothing
         * enabled costs one virtual call and a switch.
         */
        void automaticUpdate() override
        {
            _controller.update(*this, kor::Time::frameTime());
            _semanticBuffers.refresh(*this);
        }

        // ---- the GPU copy -----------------------------------------------------------------------

        /**
         * @brief Answers for whatever a shader asked a camera for.
         *
         * One `if` per semantic and nothing cached: a block is only re-serialized when something
         * moved, and the matrices behind these are themselves lazy, so the repeated calls cost a
         * dirty-flag check each.
         */
        bool serialize(const std::string_view semantic, kor::SemanticSlot& slot) const override
        {
            namespace sem = kcam::semantics;

            if (semantic == sem::ViewMatrix)           { slot.set(view()); return true; }
            if (semantic == sem::ProjectionMatrix)     { slot.set(projection()); return true; }
            if (semantic == sem::ViewProjectionMatrix) { slot.set(viewProjection()); return true; }

            if (semantic == sem::InverseViewMatrix)       { slot.set(glm::inverse(view())); return true; }
            if (semantic == sem::InverseProjectionMatrix) { slot.set(glm::inverse(projection())); return true; }
            if (semantic == sem::InverseViewProjectionMatrix) {
                slot.set(glm::inverse(viewProjection()));
                return true;
            }

            if (semantic == sem::Position) { slot.set(position()); return true; }
            if (semantic == sem::Forward)  { slot.set(forward()); return true; }
            if (semantic == sem::Up)       { slot.set(rotation() * glm::vec3(0.f, 1.f, 0.f)); return true; }
            if (semantic == sem::Right)    { slot.set(rotation() * glm::vec3(1.f, 0.f, 0.f)); return true; }

            const glm::vec4 depth = depthRange();
            if (semantic == sem::NearPlane)  { slot.set(depth.x); return true; }
            if (semantic == sem::FarPlane)   { slot.set(depth.y); return true; }
            if (semantic == sem::DepthRange) { slot.set(depth); return true; }

            return false;   // not ours: reported against the field that asked for it
        }

        kor::SemanticBuffers& semanticBuffers() override { return _semanticBuffers; }

    protected:
        /** @brief x = near, y = far, z = far - near, w = 1 / (far - near). */
        [[nodiscard]] virtual glm::vec4 depthRange() const = 0;
        template<typename BuilderType>
        explicit CameraBase(const BuilderType& builder)
            : _name(builder.name), _position(builder.position), _rotation(glm::normalize(builder.rotation))
        {
            _controller.set(builder.controller);
            _controller.setReleased(builder.released);
        }

        [[nodiscard]] virtual glm::mat4 computeProjection() const = 0;

        void markProjectionDirty() { _projectionDirty = true; }

    private:
        std::string _name;
        glm::vec3 _position;
        glm::quat _rotation;
        CameraController _controller;

        // Lazy caches: the setters only flip a flag, so a burst of changes costs one recompute
        // at the next read. All mutable because reading through a const interface is still
        // allowed to fill the cache.
        mutable glm::mat4 _view { 1.f };
        mutable glm::mat4 _projection { 1.f };
        mutable glm::mat4 _viewProjection { 1.f };
        mutable bool _viewDirty = true;
        mutable bool _projectionDirty = true;
        mutable bool _viewProjectionDirty = true;

        // One buffer per block shape a shader has asked this camera to fill; none at all for a
        // camera nothing renders with. @see kor::SemanticBuffers
        kor::SemanticBuffers _semanticBuffers;
    };

    /** @brief The concrete perspective camera. */
    class PerspectiveImpl final : public CameraBase<PerspectiveCamera>
    {
    public:
        explicit PerspectiveImpl(const Builder& builder);

        [[nodiscard]] float fovY() const override { return _fovY; }
        [[nodiscard]] float aspect() const override { return _aspect; }
        [[nodiscard]] float zNear() const override { return _zNear; }
        [[nodiscard]] float zFar() const override { return _zFar; }

        void setFovY(float fovY) override;
        void setAspect(float aspect) override;
        void setNearFar(float zNear, float zFar) override;

        void setFollowWindowAspect(bool follow) override;
        [[nodiscard]] bool followsWindowAspect() const override
        { return _aspectSource.kind == AspectSource::Kind::eWindow; }

        void setAspectSource(AspectSource source) override;
        [[nodiscard]] const AspectSource& aspectSource() const override { return _aspectSource; }

        [[nodiscard]] glm::vec4 depthRange() const override
        { return { _zNear, _zFar, _zFar - _zNear, 1.f / (_zFar - _zNear) }; }

        /** @brief The controller, then the aspect source — which is what makes a resize free. */
        void automaticUpdate() override;

    private:
        [[nodiscard]] glm::mat4 computeProjection() const override;

        /** @brief Matches the aspect to whatever it follows, if that has a usable size. */
        void adoptSourceAspect();

        float _fovY, _aspect, _zNear, _zFar;
        AspectSource _aspectSource;
        /// So a source that has been destroyed is reported once rather than every frame, or never.
        bool _warnedDeadSource = false;
    };

    /** @brief The concrete orthographic camera. */
    class OrthoImpl final : public CameraBase<OrthographicCamera>
    {
    public:
        explicit OrthoImpl(const Builder& builder);

        [[nodiscard]] glm::vec4 bounds() const override { return { _left, _right, _bottom, _top }; }
        [[nodiscard]] float zNear() const override { return _zNear; }
        [[nodiscard]] float zFar() const override { return _zFar; }

        void setBounds(float left, float right, float bottom, float top) override;
        void setNearFar(float zNear, float zFar) override;

        [[nodiscard]] glm::vec4 depthRange() const override
        { return { _zNear, _zFar, _zFar - _zNear, 1.f / (_zFar - _zNear) }; }

    private:
        [[nodiscard]] glm::mat4 computeProjection() const override;

        float _left, _right, _bottom, _top, _zNear, _zFar;
    };
}
