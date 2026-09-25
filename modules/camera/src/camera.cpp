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

    AspectSource AspectSource::Of(const kor::Window& window)
    {
        // Its default framebuffer, which follows its size — and, for a window that has closed,
        // dangles, which is reported once like any other source that went away.
        return Of(kor::ResourceRef<const kor::Framebuffer>(window.DefaultFramebuffer()));
    }

    std::optional<glm::uvec2> AspectSource::Extent() const
    {
        switch (kind) {
        case Kind::eNone:
            return std::nullopt;
        case Kind::eWindow:
            // A job has no window to follow; the aspect stays as it was configured.
            if (kor::Context::IsHeadless()) return std::nullopt;
            return kor::Context::Window().Extent();
        case Kind::eFramebuffer:
            if (!framebuffer.Valid()) return std::nullopt;
            return framebuffer->Extent();
        case Kind::eImage:
            // Depth is not part of a shape on screen; a 3D image is followed by its face.
            if (!image.Valid()) return std::nullopt;
            return glm::uvec2(image->Extent());
        }
        return std::nullopt;
    }

    bool AspectSource::Dangling() const
    {
        switch (kind) {
        case Kind::eFramebuffer: return !framebuffer.Valid();
        case Kind::eImage:       return !image.Valid();
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

    void PerspectiveImpl::SetFovY(const float fovY)
    {
        _fovY = fovY;
        markProjectionDirty();
    }

    void PerspectiveImpl::SetAspect(const float aspect)
    {
        _aspect = aspect;
        markProjectionDirty();
    }

    void PerspectiveImpl::SetNearFar(const float zNear, const float zFar)
    {
        _zNear = zNear;
        _zFar = zFar;
        markProjectionDirty();
    }

    void PerspectiveImpl::SetFollowWindowAspect(const bool follow)
    {
        SetAspectSource(follow ? AspectSource::Window() : AspectSource::None());
    }

    void PerspectiveImpl::SetAspectSource(AspectSource source)
    {
        _aspectSource = std::move(source);
        _warnedDeadSource = false;   // a new source deserves its own warning if it too goes away
        adoptSourceAspect();
    }

    std::optional<glm::uvec2> PerspectiveImpl::renderExtent() const
    {
        if (_aspectSource.kind != AspectSource::Kind::eNone) return _aspectSource.Extent();
        return CameraBase::renderExtent();
    }

    void PerspectiveImpl::adoptSourceAspect()
    {
        const std::optional<glm::uvec2> extent = _aspectSource.Extent();
        if (!extent) {
            if (!_warnedDeadSource && _aspectSource.Dangling()) {
                _warnedDeadSource = true;
                kor::log::Warn("[camera] '{}' follows the aspect of a {} that is gone or unusable; "
                               "its aspect stays at {:.3f}",
                               Name(),
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
            SetAspect(aspect);
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

    void OrthoImpl::SetBounds(const float left, const float right, const float bottom, const float top)
    {
        _left = left;
        _right = right;
        _bottom = bottom;
        _top = top;
        markProjectionDirty();
    }

    void OrthoImpl::SetNearFar(const float zNear, const float zFar)
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
