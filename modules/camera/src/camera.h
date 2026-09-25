//
// Created by radue on 29.07.2026.
//

// The camera objects themselves — the things a project holds a kor::Resource to. Internal to the
// module: consumers see only the interfaces in koralCamera.h, and this header is not installed.

#pragma once

#include <cstring>
#include <optional>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include <buffer.h>
#include <context.h>
#include <window.h>
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
        [[nodiscard]] glm::vec3 Position() const override { return _position; }
        [[nodiscard]] glm::quat Rotation() const override { return _rotation; }
        [[nodiscard]] glm::vec3 Forward() const override { return _rotation * glm::vec3(0.f, 0.f, -1.f); }
        [[nodiscard]] std::string_view Name() const override { return _name; }

        void SetPosition(const glm::vec3 position) override
        {
            _position = position;
            _viewDirty = true;
        }

        void SetRotation(const glm::quat rotation) override
        {
            _rotation = glm::normalize(rotation);
            _viewDirty = true;
        }

        void LookAt(const glm::vec3 target, const glm::vec3 up) override
        {
            const glm::vec3 to = target - _position;
            if (glm::dot(to, to) < 1e-12f) return;  // looking at yourself is not a direction
            SetRotation(glm::quatLookAt(glm::normalize(to), up));
        }

        [[nodiscard]] const glm::mat4& View() const override
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

        [[nodiscard]] const glm::mat4& Projection() const override
        {
            const glm::mat4& base = unjitteredProjection();
            if (_jitteredDirty) {
                // A translation in clip space, applied after the projection: x' = x + jitter.x * w.
                // It moves the whole image by a fraction of a pixel and leaves the frustum's shape
                // — [0][0] and [1][1] — alone, for a perspective and an orthographic camera alike.
                glm::mat4 shift(1.f);
                shift[3][0] = _jitter.x;
                shift[3][1] = _jitter.y;
                _jitteredProjection = shift * base;
                _jitteredDirty = false;
                _viewProjectionDirty = true;
            }
            return _jitteredProjection;
        }

        [[nodiscard]] const glm::mat4& ViewProjection() const override
        {
            const glm::mat4& v = View();
            const glm::mat4& p = Projection();
            if (_viewProjectionDirty) {
                _viewProjection = p * v;
                _unjitteredViewProjection = unjitteredProjection() * v;
                _viewProjectionDirty = false;
            }
            return _viewProjection;
        }

        // ---- temporal effects -------------------------------------------------------------------

        void SetJitter(const bool enabled) override
        {
            _jitterEnabled = enabled;
            if (!enabled) setJitter(glm::vec2(0.f));
        }
        [[nodiscard]] bool Jittering() const override { return _jitterEnabled; }
        [[nodiscard]] glm::vec2 Jitter() const override { return _jitter; }

        [[nodiscard]] const glm::mat4& UnjitteredViewProjection() const override
        {
            (void)ViewProjection();
            return _unjitteredViewProjection;
        }

        [[nodiscard]] const glm::mat4& PreviousViewProjection() const override
        {
            return _hasPrevious ? _previousViewProjection : UnjitteredViewProjection();
        }

        // ---- per-frame behaviour ----------------------------------------------------------------

        void SetController(const Controller& controller) override { _controller.set(controller); }
        [[nodiscard]] const Controller& ControllerSettings() const override { return _controller.get(); }

        [[nodiscard]] bool Released() const override { return _controller.released(); }
        void SetReleased(const bool released) override { _controller.setReleased(released); }

        /**
         * @brief What the repository calls at the top of every frame.
         *
         * Every camera is registered, whether or not anything is driving it, so that turning a
         * controller on later is an ordinary setter rather than a rebuild. A camera with nothing
         * enabled costs one virtual call and a switch.
         */
        void AutomaticUpdate() override
        {
            _controller.update(*this, kor::Time::FrameTime());
            // Everything that changes the matrices comes before they are written out: a resize,
            // then the jitter for this frame. Serialized once, at the end, so the GPU and every
            // getter agree for the whole frame.
            beforeSerialize();

            // What the frame before was drawn with — the unjittered matrix it serialized.
            if (_hasSerialized) {
                _previousViewProjection = _lastUnjittered;
                _hasPrevious = true;
            }
            advanceJitter();
            _lastUnjittered = UnjitteredViewProjection();
            _hasSerialized = true;

            _semanticBuffers.Refresh(*this);
        }

        // ---- the GPU copy -----------------------------------------------------------------------

        /**
         * @brief Answers for whatever a shader asked a camera for.
         *
         * One `if` per semantic and nothing cached: a block is only re-serialized when something
         * moved, and the matrices behind these are themselves lazy, so the repeated calls cost a
         * dirty-flag check each.
         */
        bool Serialize(const std::string_view semantic, kor::SemanticSlot& slot) const override
        {
            namespace sem = kcam::semantics;

            if (semantic == sem::ViewMatrix)           { slot.Set(View()); return true; }
            if (semantic == sem::ProjectionMatrix)     { slot.Set(Projection()); return true; }
            if (semantic == sem::ViewProjectionMatrix) { slot.Set(ViewProjection()); return true; }

            if (semantic == sem::InverseViewMatrix)       { slot.Set(glm::inverse(View())); return true; }
            if (semantic == sem::InverseProjectionMatrix) { slot.Set(glm::inverse(Projection())); return true; }
            if (semantic == sem::InverseViewProjectionMatrix) {
                slot.Set(glm::inverse(ViewProjection()));
                return true;
            }
            if (semantic == sem::UnjitteredViewProjectionMatrix) { slot.Set(UnjitteredViewProjection()); return true; }
            if (semantic == sem::PreviousViewProjectionMatrix)  { slot.Set(PreviousViewProjection()); return true; }
            if (semantic == sem::Jitter)                        { slot.Set(_jitter); return true; }

            if (semantic == sem::Position) { slot.Set(Position()); return true; }
            if (semantic == sem::Forward)  { slot.Set(Forward()); return true; }
            if (semantic == sem::Up)       { slot.Set(Rotation() * glm::vec3(0.f, 1.f, 0.f)); return true; }
            if (semantic == sem::Right)    { slot.Set(Rotation() * glm::vec3(1.f, 0.f, 0.f)); return true; }

            const glm::vec4 depth = depthRange();
            if (semantic == sem::NearPlane)  { slot.Set(depth.x); return true; }
            if (semantic == sem::FarPlane)   { slot.Set(depth.y); return true; }
            if (semantic == sem::DepthRange) { slot.Set(depth); return true; }

            return false;   // not ours: reported against the field that asked for it
        }

        kor::SemanticBuffers& SemanticStorage() override { return _semanticBuffers; }

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

        /** @brief Runs in AutomaticUpdate after the controller and before anything is written out. */
        virtual void beforeSerialize() {}

        /**
         * @brief The size the camera's image is rendered at, which one pixel of jitter is a fraction of.
         * The window, unless the camera knows better (a perspective camera following a viewport).
         */
        [[nodiscard]] virtual std::optional<glm::uvec2> renderExtent() const
        {
            if (kor::Context::IsHeadless()) return std::nullopt;
            return kor::Context::Window().Extent();
        }

    private:
        /** @brief Projection without the jitter, Y already pointing down. */
        [[nodiscard]] const glm::mat4& unjitteredProjection() const
        {
            if (_projectionDirty) {
                _projection = computeProjection();
                // Y points down in the engine's clip space (Vulkan conventions; the GL backend
                // matches them). Baked in here so every consumer just multiplies.
                _projection[1][1] *= -1.f;
                _projectionDirty = false;
                _jitteredDirty = true;
                _viewProjectionDirty = true;
            }
            return _projection;
        }

        void setJitter(const glm::vec2 jitter)
        {
            if (jitter == _jitter) return;
            _jitter = jitter;
            _jitteredDirty = true;
        }

        /** @brief The next point of an 8-sample Halton(2, 3) sequence, as a clip-space offset. */
        void advanceJitter()
        {
            if (!_jitterEnabled) return;
            const auto extent = renderExtent();
            if (!extent || extent->x == 0 || extent->y == 0) { setJitter(glm::vec2(0.f)); return; }

            const auto halton = [](glm::u32 index, const glm::u32 base) {
                float result = 0.f, fraction = 1.f;
                for (++index; index > 0; index /= base) {
                    fraction /= static_cast<float>(base);
                    result += fraction * static_cast<float>(index % base);
                }
                return result;
            };
            // [-0.5, 0.5) of a pixel, and a pixel is 2 / extent in NDC.
            const glm::vec2 pixel(halton(_jitterIndex, 2) - 0.5f, halton(_jitterIndex, 3) - 0.5f);
            setJitter(pixel * 2.f / glm::vec2(*extent));
            _jitterIndex = (_jitterIndex + 1) % 8;
        }

        std::string _name;
        glm::vec3 _position;
        glm::quat _rotation;
        CameraController _controller;

        bool _jitterEnabled = false;
        glm::u32 _jitterIndex = 0;
        glm::vec2 _jitter { 0.f };
        glm::mat4 _lastUnjittered { 1.f };          // what this frame serialized
        glm::mat4 _previousViewProjection { 1.f };  // what the frame before serialized
        bool _hasSerialized = false;
        bool _hasPrevious = false;

        // Lazy caches: the setters only flip a flag, so a burst of changes costs one recompute
        // at the next read. All mutable because reading through a const interface is still
        // allowed to fill the cache.
        mutable glm::mat4 _view { 1.f };
        mutable glm::mat4 _projection { 1.f };            // unjittered
        mutable glm::mat4 _jitteredProjection { 1.f };
        mutable glm::mat4 _viewProjection { 1.f };
        mutable glm::mat4 _unjitteredViewProjection { 1.f };
        mutable bool _viewDirty = true;
        mutable bool _projectionDirty = true;
        mutable bool _jitteredDirty = true;
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

        [[nodiscard]] float FovY() const override { return _fovY; }
        [[nodiscard]] float Aspect() const override { return _aspect; }
        [[nodiscard]] float ZNear() const override { return _zNear; }
        [[nodiscard]] float ZFar() const override { return _zFar; }

        void SetFovY(float fovY) override;
        void SetAspect(float aspect) override;
        void SetNearFar(float zNear, float zFar) override;

        void SetFollowWindowAspect(bool follow) override;
        [[nodiscard]] bool FollowsWindowAspect() const override
        { return _aspectSource.kind == AspectSource::Kind::eWindow; }

        void SetAspectSource(AspectSource source) override;
        [[nodiscard]] const AspectSource& AspectSourceSettings() const override { return _aspectSource; }

        [[nodiscard]] glm::vec4 depthRange() const override
        { return { _zNear, _zFar, _zFar - _zNear, 1.f / (_zFar - _zNear) }; }

    private:
        [[nodiscard]] glm::mat4 computeProjection() const override;

        /** @brief The aspect source, before the matrices are written — so a resize reaches the GPU that frame. */
        void beforeSerialize() override { adoptSourceAspect(); }

        /** @brief What the aspect follows, if anything; else the window. */
        [[nodiscard]] std::optional<glm::uvec2> renderExtent() const override;

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

        [[nodiscard]] glm::vec4 Bounds() const override { return { _left, _right, _bottom, _top }; }
        [[nodiscard]] float ZNear() const override { return _zNear; }
        [[nodiscard]] float ZFar() const override { return _zFar; }

        void SetBounds(float left, float right, float bottom, float top) override;
        void SetNearFar(float zNear, float zFar) override;

        [[nodiscard]] glm::vec4 depthRange() const override
        { return { _zNear, _zFar, _zFar - _zNear, 1.f / (_zFar - _zNear) }; }

    private:
        [[nodiscard]] glm::mat4 computeProjection() const override;

        float _left, _right, _bottom, _top, _zNear, _zFar;
    };
}
