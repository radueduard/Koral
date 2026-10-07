//
// Created by radue on 29.07.2026.
//

// The camera objects themselves — the things a project holds a kor::Resource to. Internal to the
// module: consumers see only the interfaces in koralCamera.h, and this header is not installed.

#pragma once

#include <cstring>
#include <optional>
#include <string>

#include <kmath/transform.h>

#include <buffer.h>
#include <context.h>
#include <window.h>
#include <gtime.h>
#include <scene.h>

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
        [[nodiscard]] kor::Vec3 Position() const override { return _position; }
        [[nodiscard]] kor::Quat Rotation() const override { return _rotation; }
        [[nodiscard]] kor::Vec3 Forward() const override { return _rotation * kor::Vec3(0.f, 0.f, -1.f); }
        [[nodiscard]] std::string_view Name() const override { return _name; }

        void SetPosition(const kor::Vec3 position) override
        {
            _position = position;
            _viewDirty = true;
        }

        void SetRotation(const kor::Quat rotation) override
        {
            _rotation = kor::Normalize(rotation);
            _viewDirty = true;
        }

        void LookAt(const kor::Vec3 target, const kor::Vec3 up) override
        {
            const kor::Vec3 to = target - _position;
            if (kor::Dot(to, to) < 1e-12f) return;  // looking at yourself is not a direction
            SetRotation(kor::Quat::LookRotation(kor::Normalize(to), up));
        }

        [[nodiscard]] const kor::Mat4& View() const override
        {
            if (_viewDirty) {
                // inverse(translate * rotate), assembled directly rather than inverted.
                _view = kor::ToMat4(kor::Conjugate(_rotation)) *
                        kor::Translate(kor::Mat4(1.f), -_position);
                _viewDirty = false;
                _viewProjectionDirty = true;
            }
            return _view;
        }

        [[nodiscard]] const kor::Mat4& Projection() const override
        {
            const kor::Mat4& base = unjitteredProjection();
            if (_jitteredDirty) {
                // A translation in clip space, applied after the projection: x' = x + jitter.x * w.
                // It moves the whole image by a fraction of a pixel and leaves the frustum's shape
                // — [0][0] and [1][1] — alone, for a perspective and an orthographic camera alike.
                kor::Mat4 shift(1.f);
                shift[3][0] = _jitter.x;
                shift[3][1] = _jitter.y;
                _jitteredProjection = shift * base;
                _jitteredDirty = false;
                _viewProjectionDirty = true;
            }
            return _jitteredProjection;
        }

        [[nodiscard]] const kor::Mat4& ViewProjection() const override
        {
            const kor::Mat4& v = View();
            const kor::Mat4& p = Projection();
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
            if (!enabled) setJitter(kor::Vec2(0.f));
        }
        [[nodiscard]] bool Jittering() const override { return _jitterEnabled; }
        [[nodiscard]] kor::Vec2 Jitter() const override { return _jitter; }

        [[nodiscard]] const kor::Mat4& UnjitteredViewProjection() const override
        {
            (void)ViewProjection();
            return _unjitteredViewProjection;
        }

        [[nodiscard]] const kor::Mat4& PreviousViewProjection() const override
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
            // In the scene the camera was made in: its input drives the controller, its window is
            // the one "follow the window" means, its clock times the movement. A camera made outside
            // any scene — a headless job's — has none of those, and is only serialized.
            const auto life = _scene.lock();
            kor::detail::SceneScope scope(life && life->scene ? life : nullptr);
            _controller.update(*this, kor::Scene::Current() ? kor::Scene::Time::FrameTime() : 0.f);
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

            if (semantic == sem::InverseViewMatrix)       { slot.Set(kor::Inverse(View())); return true; }
            if (semantic == sem::InverseProjectionMatrix) { slot.Set(kor::Inverse(Projection())); return true; }
            if (semantic == sem::InverseViewProjectionMatrix) {
                slot.Set(kor::Inverse(ViewProjection()));
                return true;
            }
            if (semantic == sem::UnjitteredViewProjectionMatrix) { slot.Set(UnjitteredViewProjection()); return true; }
            if (semantic == sem::PreviousViewProjectionMatrix)  { slot.Set(PreviousViewProjection()); return true; }
            if (semantic == sem::Jitter)                        { slot.Set(_jitter); return true; }

            if (semantic == sem::Position) { slot.Set(Position()); return true; }
            if (semantic == sem::Forward)  { slot.Set(Forward()); return true; }
            if (semantic == sem::Up)       { slot.Set(Rotation() * kor::Vec3(0.f, 1.f, 0.f)); return true; }
            if (semantic == sem::Right)    { slot.Set(Rotation() * kor::Vec3(1.f, 0.f, 0.f)); return true; }

            const kor::Vec4 depth = depthRange();
            if (semantic == sem::NearPlane)  { slot.Set(depth.x); return true; }
            if (semantic == sem::FarPlane)   { slot.Set(depth.y); return true; }
            if (semantic == sem::DepthRange) { slot.Set(depth); return true; }

            return false;   // not ours: reported against the field that asked for it
        }

        kor::SemanticBuffers& SemanticStorage() override { return _semanticBuffers; }

    protected:
        /** @brief x = near, y = far, z = far - near, w = 1 / (far - near). */
        [[nodiscard]] virtual kor::Vec4 depthRange() const = 0;
        template<typename BuilderType>
        explicit CameraBase(const BuilderType& builder)
            : _name(builder.name), _position(builder.position), _rotation(kor::Normalize(builder.rotation)),
              _scene(kor::detail::CurrentSceneLife())
        {
            _controller.set(builder.controller);
            _controller.setReleased(builder.released);
        }

        [[nodiscard]] virtual kor::Mat4 computeProjection() const = 0;

        void markProjectionDirty() { _projectionDirty = true; }

        /** @brief Runs in AutomaticUpdate after the controller and before anything is written out. */
        virtual void beforeSerialize() {}

        /**
         * @brief The size the camera's image is rendered at, which one pixel of jitter is a fraction of.
         * The window, unless the camera knows better (a perspective camera following a viewport).
         */
        [[nodiscard]] virtual std::optional<kor::UVec2> renderExtent() const
        {
            if (const auto* scene = kor::Scene::Current()) return scene->SceneWindow().Extent();
            return std::nullopt;
        }

    private:
        /** @brief Projection without the jitter, Y already pointing down. */
        [[nodiscard]] const kor::Mat4& unjitteredProjection() const
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

        void setJitter(const kor::Vec2 jitter)
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
            if (!extent || extent->x == 0 || extent->y == 0) { setJitter(kor::Vec2(0.f)); return; }

            const auto halton = [](kor::u32 index, const kor::u32 base) {
                float result = 0.f, fraction = 1.f;
                for (++index; index > 0; index /= base) {
                    fraction /= static_cast<float>(base);
                    result += fraction * static_cast<float>(index % base);
                }
                return result;
            };
            // [-0.5, 0.5) of a pixel, and a pixel is 2 / extent in NDC.
            const kor::Vec2 pixel(halton(_jitterIndex, 2) - 0.5f, halton(_jitterIndex, 3) - 0.5f);
            setJitter(pixel * 2.f / kor::Vec2(*extent));
            _jitterIndex = (_jitterIndex + 1) % 8;
        }

        std::string _name;
        kor::Vec3 _position;
        kor::Quat _rotation;
        CameraController _controller;
        std::weak_ptr<kor::detail::SceneLife> _scene;   // the scene it was made in

        bool _jitterEnabled = false;
        kor::u32 _jitterIndex = 0;
        kor::Vec2 _jitter { 0.f };
        kor::Mat4 _lastUnjittered { 1.f };          // what this frame serialized
        kor::Mat4 _previousViewProjection { 1.f };  // what the frame before serialized
        bool _hasSerialized = false;
        bool _hasPrevious = false;

        // Lazy caches: the setters only flip a flag, so a burst of changes costs one recompute
        // at the next read. All mutable because reading through a const interface is still
        // allowed to fill the cache.
        mutable kor::Mat4 _view { 1.f };
        mutable kor::Mat4 _projection { 1.f };            // unjittered
        mutable kor::Mat4 _jitteredProjection { 1.f };
        mutable kor::Mat4 _viewProjection { 1.f };
        mutable kor::Mat4 _unjitteredViewProjection { 1.f };
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

        [[nodiscard]] kor::Vec4 depthRange() const override
        { return { _zNear, _zFar, _zFar - _zNear, 1.f / (_zFar - _zNear) }; }

    private:
        [[nodiscard]] kor::Mat4 computeProjection() const override;

        /** @brief The aspect source, before the matrices are written — so a resize reaches the GPU that frame. */
        void beforeSerialize() override { adoptSourceAspect(); }

        /** @brief What the aspect follows, if anything; else the window. */
        [[nodiscard]] std::optional<kor::UVec2> renderExtent() const override;

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

        [[nodiscard]] kor::Vec4 Bounds() const override { return { _left, _right, _bottom, _top }; }
        [[nodiscard]] float ZNear() const override { return _zNear; }
        [[nodiscard]] float ZFar() const override { return _zFar; }

        void SetBounds(float left, float right, float bottom, float top) override;
        void SetNearFar(float zNear, float zFar) override;

        [[nodiscard]] kor::Vec4 depthRange() const override
        { return { _zNear, _zFar, _zFar - _zNear, 1.f / (_zFar - _zNear) }; }

    private:
        [[nodiscard]] kor::Mat4 computeProjection() const override;

        float _left, _right, _bottom, _top, _zNear, _zFar;
    };
}
