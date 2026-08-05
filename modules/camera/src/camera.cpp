//
// Created by radue on 29.07.2026.
//

#include "camera.h"

#include <context.h>
#include <log.h>
#include <window.h>

namespace kcam
{
    // ---- what an aspect is matched to ----------------------------------------------------------

    std::optional<glm::uvec2> AspectSource::extent() const
    {
        switch (kind) {
        case Kind::eNone:
            return std::nullopt;
        case Kind::eWindow:
            // A job has no window to follow; the aspect stays as it was configured.
            if (kor::Context::IsHeadless()) return std::nullopt;
            return kor::Context::Window().getExtent();
        case Kind::eFramebuffer:
            if (!framebuffer.valid()) return std::nullopt;
            return framebuffer->getExtent();
        case Kind::eImage:
            // Depth is not part of a shape on screen; a 3D image is followed by its face.
            if (!image.valid()) return std::nullopt;
            return glm::uvec2(image->getExtent());
        }
        return std::nullopt;
    }

    bool AspectSource::dangling() const
    {
        switch (kind) {
        case Kind::eFramebuffer: return !framebuffer.valid();
        case Kind::eImage:       return !image.valid();
        default:                 return false;
        }
    }

    // ---- perspective --------------------------------------------------------------------------

    PerspectiveImpl::PerspectiveImpl(const Builder& builder)
        : CameraBase(builder),
          _fovY(builder.fovY), _aspect(builder.aspect),
          _zNear(builder.zNear), _zFar(builder.zFar),
          _aspectSource(builder.aspectSource)
    {
        // Immediately, not on the first frame: a scene that renders before the repository has run
        // once would otherwise see the configured aspect rather than its target's.
        adoptSourceAspect();
    }

    void PerspectiveImpl::setFovY(const float fovY)
    {
        _fovY = fovY;
        markProjectionDirty();
    }

    void PerspectiveImpl::setAspect(const float aspect)
    {
        _aspect = aspect;
        markProjectionDirty();
    }

    void PerspectiveImpl::setNearFar(const float zNear, const float zFar)
    {
        _zNear = zNear;
        _zFar = zFar;
        markProjectionDirty();
    }

    void PerspectiveImpl::setFollowWindowAspect(const bool follow)
    {
        setAspectSource(follow ? AspectSource::window() : AspectSource::none());
    }

    void PerspectiveImpl::setAspectSource(AspectSource source)
    {
        _aspectSource = std::move(source);
        _warnedDeadSource = false;   // a new source deserves its own warning if it too goes away
        adoptSourceAspect();
    }

    void PerspectiveImpl::automaticUpdate()
    {
        CameraBase::automaticUpdate();
        adoptSourceAspect();
    }

    void PerspectiveImpl::adoptSourceAspect()
    {
        const std::optional<glm::uvec2> extent = _aspectSource.extent();
        if (!extent) {
            if (!_warnedDeadSource && _aspectSource.dangling()) {
                _warnedDeadSource = true;
                kor::log::warn("[camera] '{}' follows the aspect of a {} that is gone or unusable; "
                               "its aspect stays at {:.3f}",
                               name(),
                               _aspectSource.kind == AspectSource::Kind::eFramebuffer
                                   ? "framebuffer" : "image",
                               _aspect);
            }
            return;
        }

        if (extent->y == 0) return;  // minimized, or a target not built yet: keep the last real aspect

        if (const float aspect = static_cast<float>(extent->x) / static_cast<float>(extent->y);
            aspect != _aspect)
        {
            setAspect(aspect);
        }
    }

    glm::mat4 PerspectiveImpl::computeProjection() const
    {
        return glm::perspectiveRH_ZO(_fovY, _aspect, _zNear, _zFar);
    }

    // ---- orthographic -------------------------------------------------------------------------

    OrthoImpl::OrthoImpl(const Builder& builder)
        : CameraBase(builder),
          _left(builder.left), _right(builder.right), _bottom(builder.bottom), _top(builder.top),
          _zNear(builder.zNear), _zFar(builder.zFar) {}

    void OrthoImpl::setBounds(const float left, const float right, const float bottom, const float top)
    {
        _left = left;
        _right = right;
        _bottom = bottom;
        _top = top;
        markProjectionDirty();
    }

    void OrthoImpl::setNearFar(const float zNear, const float zFar)
    {
        _zNear = zNear;
        _zFar = zFar;
        markProjectionDirty();
    }

    glm::mat4 OrthoImpl::computeProjection() const
    {
        return glm::orthoRH_ZO(_left, _right, _bottom, _top, _zNear, _zFar);
    }
}
