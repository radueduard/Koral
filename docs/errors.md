# gfx error reference

Every fallible gfx API call reports failure as a value, not an exception. Nothing you can do to a
resource will throw.

## Builders: a failure is a resource, not an absence

`build()` returns a `gfx::Resource<T>` directly — there is nothing to unwrap:

```cpp
auto pipeline = GraphicsPipeline::Builder{}.setVertexShader(vs).build();
```

That resource is one of three things: **valid**, **empty** (default-constructed), or **poisoned** —
it exists, it has an identity, but it holds a `gfx::Error` instead of the object. Ask it with
`valid()`, `poisoned()` and `error()`.

Poison spreads. Hand a poisoned resource to another builder and the resource it produces is poisoned
too, with the original error linked as its **cause** — so a pipeline that could not be assembled
reports the shader that would not compile, not merely its own confusion. `error()->history()` prints
the whole chain, symptom first and root cause last, and that is what gets logged for you:

```
Could not build ComputePipeline:
eMissingShaderStage: compute shader 'shaders/blur.comp.glsl' is unusable.
  caused by eShaderCompileFailed: GLSL compilation failed for 'shaders/blur.comp.glsl':
    ERROR: 0:12: 'colour' : undeclared identifier
```

## Poisoned resources repair themselves

A failure caused by something you can fix at runtime — a shader that does not compile — is not
permanent. The shader stays registered and watched even though it failed, so when you fix the source
it recompiles, and every pipeline built from it is rebuilt behind it, in place, at the top of the
next frame. References you handed out while everything was broken start working; they are not
re-issued, because a `gfx::ResourceRef` tracks the resource rather than the object inside it.

Failures that *cannot* be fixed at runtime (a buffer or image that failed to allocate) are reported
once, in full, and stay broken. They are still poisoned rather than fatal, so they propagate and are
reported the same way.

## The command buffer

A railway: methods keep returning `CommandBuffer&`, the first error is recorded, and later GPU ops
become no-ops. Recording with a poisoned resource fails the recording — it never reaches the backend.
Read `cb.result()` (a `gfx::VoidResult`) or `cb.errors()` after recording; `Submit()` returns a
`gfx::VoidResult`.

## Everything else

`gfx::Result<T>` (`std::expected<T, gfx::Error>`) is still the return type of non-resource operations
— `Submit()`, buffer reads and writes — where the failure is an event rather than a thing.

Each error carries a `gfx::ErrorCode`, a human `message`, the source location, and optionally a
`cause`. Backend (Vulkan/OpenGL) exceptions never cross the API boundary — they are converted to a
`gfx::Error` (`eBackend`, with the original message preserved, unless a more specific code applies).

This table is kept in sync with `gfx::describe()` in `src/core/error.cpp`.

| Code | Description | Typical cause / fix |
|------|-------------|---------------------|
| `eNone` | No error. | Success sentinel. |
| `eUnknownApi` | The active graphics API is not recognised or supported. | Internal: an unhandled `gfx::API` value. |
| `eBackend` | A backend (Vulkan/OpenGL) operation failed. | Allocation/creation failed; see the message for the backend cause. |
| `eInvalidArgument` | An argument passed to a builder or command was invalid. | Check the offending setter's value. |
| `eUniformBufferTooLarge` | Uniform buffers may be at most 65536 bytes. | Reduce the size or use a storage buffer (`Usage::eStorage`). |
| `eBufferSizeInvalid` | Buffer size and instance count must be strictly positive. | Set a size/instance count greater than zero. |
| `eBufferRangeOutOfBounds` | The requested buffer read/write/map range lies outside the buffer. | Clamp offset+count to the buffer's element capacity. |
| `eBufferAlreadyMapped` | The buffer is already mapped in a conflicting mode. | Release the existing mapping before `Write`/`Map`. |
| `eNoGraphicsPipelineBound` | A graphics command was recorded with no graphics pipeline bound. | `BindGraphicsPipeline(...)` before viewport/scissor/draw. |
| `eNoComputePipelineBound` | A compute command was recorded with no compute pipeline bound. | `BindComputePipeline(...)` before `Dispatch`. |
| `eNoRayTracingPipelineBound` | A ray-tracing command was recorded with no ray-tracing pipeline bound. | `BindRayTracingPipeline(...)` before `TraceRays`. |
| `eNoMeshBound` | An indexed/mesh draw was recorded with no mesh bound. | `BindMesh(...)` before `DrawIndexed`. |
| `eMeshHasNoIndexBuffer` | An indexed draw was recorded for a mesh without an index buffer. | Use a non-indexed draw, or give the mesh an index buffer. |
| `eCopySizeExceedsBuffer` | A copy/clear/fill range exceeds the target buffer size. | Shrink the range or grow the buffer. |
| `eImageSubresourceOutOfRange` | A copy referenced a mip level / array layer outside the image. | Check mip level and array layer against the image. |
| `eResolveRequiresMultisample` | Resolve needs a multisampled source and a single-sampled destination. | Resolve only from an MSAA image into a non-MSAA image. |
| `eRayTracingUnsupported` | Ray tracing is not supported on the active backend. | Use the Vulkan backend with ray tracing available. |
| `eMissingShaderStage` | The pipeline is missing a required shader stage. | Provide the required shader (e.g. vertex or mesh). |
| `eShaderStageMismatch` | A shader was supplied for the wrong pipeline stage. | Pass a shader whose stage matches the slot. |
| `eDescriptorConflict` | Descriptor declarations conflict across the pipeline's shader stages. | Align (set, binding) declarations across stages. |
| `eShaderCompileFailed` | Shader compilation or linking failed. | See the message for the compiler diagnostics. |
