//
// Created by radue on 28.06.2026.
//

#include <error.h>

#include <format>

namespace kor
{
    // Symbolic name of a code, for logs / toString(). Kept next to describe() so
    // both are updated together when ErrorCode grows.
    static std::string_view name(const ErrorCode code)
    {
        switch (code) {
        case ErrorCode::eNone:                       return "eNone";
        case ErrorCode::eUnknownApi:                 return "eUnknownApi";
        case ErrorCode::eBackend:                    return "eBackend";
        case ErrorCode::eInvalidArgument:            return "eInvalidArgument";
        case ErrorCode::eUniformBufferTooLarge:      return "eUniformBufferTooLarge";
        case ErrorCode::eBufferSizeInvalid:          return "eBufferSizeInvalid";
        case ErrorCode::eBufferRangeOutOfBounds:     return "eBufferRangeOutOfBounds";
        case ErrorCode::eBufferAlreadyMapped:        return "eBufferAlreadyMapped";
        case ErrorCode::eNoGraphicsPipelineBound:    return "eNoGraphicsPipelineBound";
        case ErrorCode::eNoComputePipelineBound:     return "eNoComputePipelineBound";
        case ErrorCode::eNoRayTracingPipelineBound:  return "eNoRayTracingPipelineBound";
        case ErrorCode::eNoMeshBound:                return "eNoMeshBound";
        case ErrorCode::eMeshHasNoIndexBuffer:       return "eMeshHasNoIndexBuffer";
        case ErrorCode::eCopySizeExceedsBuffer:      return "eCopySizeExceedsBuffer";
        case ErrorCode::eImageSubresourceOutOfRange: return "eImageSubresourceOutOfRange";
        case ErrorCode::eResolveRequiresMultisample: return "eResolveRequiresMultisample";
        case ErrorCode::eRayTracingUnsupported:      return "eRayTracingUnsupported";
        case ErrorCode::eMissingShaderStage:         return "eMissingShaderStage";
        case ErrorCode::eShaderStageMismatch:        return "eShaderStageMismatch";
        case ErrorCode::eDescriptorConflict:         return "eDescriptorConflict";
        case ErrorCode::eShaderCompileFailed:        return "eShaderCompileFailed";
        case ErrorCode::eConfigInvalid:              return "eConfigInvalid";
        }
        return "eUnknown";
    }

    std::string_view describe(const ErrorCode code)
    {
        switch (code) {
        case ErrorCode::eNone:
            return "No error.";
        case ErrorCode::eUnknownApi:
            return "The active graphics API is not recognised or supported.";
        case ErrorCode::eBackend:
            return "A backend (Vulkan/OpenGL) operation failed.";
        case ErrorCode::eInvalidArgument:
            return "An argument passed to a builder or command was invalid.";
        case ErrorCode::eUniformBufferTooLarge:
            return "Uniform buffers may be at most 65536 bytes; reduce the size or use a storage buffer.";
        case ErrorCode::eBufferSizeInvalid:
            return "Buffer size and instance count must be strictly positive.";
        case ErrorCode::eBufferRangeOutOfBounds:
            return "The requested buffer read/write/map range lies outside the buffer.";
        case ErrorCode::eBufferAlreadyMapped:
            return "The buffer is already mapped in a conflicting mode; release the existing mapping first.";
        case ErrorCode::eNoGraphicsPipelineBound:
            return "Bind a graphics pipeline before recording graphics state or draws.";
        case ErrorCode::eNoComputePipelineBound:
            return "Bind a compute pipeline before dispatching.";
        case ErrorCode::eNoRayTracingPipelineBound:
            return "Bind a ray-tracing pipeline before tracing rays.";
        case ErrorCode::eNoMeshBound:
            return "Bind a mesh before recording an indexed or mesh draw.";
        case ErrorCode::eMeshHasNoIndexBuffer:
            return "Indexed draws require a mesh that has an index buffer.";
        case ErrorCode::eCopySizeExceedsBuffer:
            return "The copy/clear/fill range exceeds the target buffer's size.";
        case ErrorCode::eImageSubresourceOutOfRange:
            return "The copy referenced a mip level or array layer outside the image.";
        case ErrorCode::eResolveRequiresMultisample:
            return "Resolve needs a multisampled source and a single-sampled destination.";
        case ErrorCode::eRayTracingUnsupported:
            return "Ray tracing is not supported on the active backend.";
        case ErrorCode::eMissingShaderStage:
            return "The pipeline is missing a required shader stage.";
        case ErrorCode::eShaderStageMismatch:
            return "A shader was supplied for the wrong pipeline stage.";
        case ErrorCode::eDescriptorConflict:
            return "Descriptor declarations conflict across the pipeline's shader stages.";
        case ErrorCode::eShaderCompileFailed:
            return "Shader compilation or linking failed.";
        case ErrorCode::eConfigInvalid:
            return "The koral.json config file is malformed, or one of its keys has the wrong type.";
        }
        return "Unknown error.";
    }

    std::string Error::toString() const
    {
        return std::format("kor::Error({}): {} [{}:{}]",
                           name(code), message, where.file_name(), where.line());
    }

    std::size_t Error::depth() const
    {
        std::size_t d = 0;
        for (const Error* e = cause.get(); e != nullptr; e = e->cause.get())
            ++d;
        return d;
    }

    const Error& Error::root() const
    {
        const Error* e = this;
        while (e->cause) e = e->cause.get();
        return *e;
    }

    std::string Error::history() const
    {
        // The symptom first, then one "caused by" per level, indented so a deep chain
        // still reads top-down. The deepest line is the thing the user has to fix.
        std::string out = std::format("{}: {} [{}:{}]",
                                      name(code), message, where.file_name(), where.line());

        std::size_t indent = 1;
        for (const Error* e = cause.get(); e != nullptr; e = e->cause.get(), ++indent) {
            out += std::format("\n{:{}}caused by {}: {} [{}:{}]",
                               "", indent * 2,
                               name(e->code), e->message, e->where.file_name(), e->where.line());
        }
        return out;
    }

    Error causedBy(Error e, std::shared_ptr<const Error> cause)
    {
        e.cause = std::move(cause);
        return e;
    }
}
