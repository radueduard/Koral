//
// Created by radue on 29.07.2026.
//

// The two builders: validate, construct, and hand the camera to the runtime.
//
// This is the whole of "the runtime handles the update stuff". A camera is registered with the
// engine's repository, which drives every registered resource at the top of each frame — before
// the scene's Update, so a scene always reads an already-moved camera. It is the same mechanism a
// buffer uses to propagate its writes; a camera is not a special case, which is why none of this
// needs a module to be running.

#include <format>

#include <context.h>
#include <log.h>

#include "camera.h"

namespace kcam
{
    namespace
    {
        /**
         * @brief Registers a freshly built camera for its per-frame update.
         *
         * Every camera is registered, not only the driven ones: turning a controller on later is
         * then an ordinary setter. An idle camera costs one virtual call per frame.
         */
        template<typename CameraType>
        void handToRuntime(const kor::Resource<CameraType>& camera)
        {
            if (!camera.valid()) return;

            const kor::ResourceRef<Camera> ref(camera);

            // Built before there is an engine to drive it — from a static initializer, or a test.
            // The camera works; nothing will move it.
            if (!kor::Context::HasRepository()) {
                kor::log::warn("[camera] '{}' was built before the engine came up; it will not be "
                               "updated automatically. Build cameras from Scene::Initialize onwards.",
                               camera->name());
                return;
            }
            kor::Context::Repository().addRef(ref);
        }

        /** @brief The depth range checks both kinds share. */
        kor::VoidResult validateDepth(const float zNear, const float zFar, const bool nearMustBePositive)
        {
            if (nearMustBePositive && zNear <= 0.f)
                return std::unexpected(kor::Error{
                    .code = kor::ErrorCode::eInvalidArgument,
                    .message = std::format("a perspective camera's near plane must be greater than "
                                           "zero, not {}", zNear) });

            if (zFar <= zNear)
                return std::unexpected(kor::Error{
                    .code = kor::ErrorCode::eInvalidArgument,
                    .message = std::format("the far plane ({}) has to be beyond the near plane ({})",
                                           zFar, zNear) });
            return {};
        }
    }

    // ---- perspective ----------------------------------------------------------------------------

    PerspectiveCamera::Builder& PerspectiveCamera::Builder::lookAt(const glm::vec3 target, const glm::vec3 up)
    {
        const glm::vec3 to = target - position;
        if (glm::dot(to, to) < 1e-12f) {
            warn("lookAt() was given the camera's own position; the rotation is unchanged");
            return *this;
        }
        rotation = glm::quatLookAt(glm::normalize(to), up);
        return *this;
    }

    kor::Result<std::unique_ptr<PerspectiveCamera>> PerspectiveCamera::Builder::create() const
    {
        beginAttempt();

        if (fovY <= 0.f || fovY >= glm::pi<float>())
            addError(kor::ErrorCode::eInvalidArgument,
                     std::format("the vertical field of view must be between 0 and pi radians, "
                                 "not {} ({:.1f} degrees)", fovY, glm::degrees(fovY)));

        if (aspect <= 0.f)
            addError(kor::ErrorCode::eInvalidArgument,
                     std::format("the aspect ratio must be greater than zero, not {}", aspect));

        if (const auto depth = validateDepth(zNear, zFar, true); !depth)
            addError(depth.error().code, depth.error().message);

        if (const auto valid = validate(); !valid) return std::unexpected(valid.error());

        return kor::MakeBackendPtr<PerspectiveCamera, PerspectiveImpl>(*this);
    }

    kor::Resource<PerspectiveCamera> PerspectiveCamera::Builder::build(const std::source_location where) const
    {
        auto camera = materialize<PerspectiveCamera>(*this, name, where);
        handToRuntime(camera);
        return camera;
    }

    // ---- orthographic ---------------------------------------------------------------------------

    OrthographicCamera::Builder& OrthographicCamera::Builder::lookAt(const glm::vec3 target, const glm::vec3 up)
    {
        const glm::vec3 to = target - position;
        if (glm::dot(to, to) < 1e-12f) {
            warn("lookAt() was given the camera's own position; the rotation is unchanged");
            return *this;
        }
        rotation = glm::quatLookAt(glm::normalize(to), up);
        return *this;
    }

    kor::Result<std::unique_ptr<OrthographicCamera>> OrthographicCamera::Builder::create() const
    {
        beginAttempt();

        // An empty view volume projects everything onto nothing, and the matrix divides by the
        // extent — this is a silent black screen if it is let through.
        if (left == right || bottom == top)
            addError(kor::ErrorCode::eInvalidArgument,
                     std::format("the view volume is empty: left/right are {}/{} and bottom/top "
                                 "are {}/{}", left, right, bottom, top));

        if (const auto depth = validateDepth(zNear, zFar, false); !depth)
            addError(depth.error().code, depth.error().message);

        if (const auto valid = validate(); !valid) return std::unexpected(valid.error());

        return kor::MakeBackendPtr<OrthographicCamera, OrthoImpl>(*this);
    }

    kor::Resource<OrthographicCamera> OrthographicCamera::Builder::build(const std::source_location where) const
    {
        auto camera = materialize<OrthographicCamera>(*this, name, where);
        handToRuntime(camera);
        return camera;
    }
}
