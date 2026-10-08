# Shaders, pipelines and GPU features

## Shaders

```cpp
// GLSL: one entry point, main. The stage comes from the file name (.vert.glsl, .frag.glsl, .comp.glsl…) or SetStage.
auto lit = kor::Shader::Builder{}.SetPath("lit.frag.glsl").GetOrBuild();

// Slang: a module, and the entry point to use from it.
auto vs = kor::Shader::Builder{}.SetEntryPoint("lit", "vertexMain").GetOrBuild();
```

- Paths are searched for in the shader directories: the project's (`koral.json` `paths.shaderDirectories`) and
  Koral's own. `GetOrBuild` keeps one shader per source and entry point, and rebuilds it when the file changes.
  Pipelines made from it rebuild themselves too.
- `SetLang<kor::Shader::Lang::eSPIRV>()` reads compiled SPIR-V instead.
- Everything the pipeline needs is reflected out of the compiled code: descriptor sets and bindings (with the
  names they have in the source), push constant blocks (by member name), specialization constants (by name,
  with their ids and defaults), vertex inputs (with how many locations each takes), and which bindings the
  entry point actually reaches. A tool can list them: `BlockLayout()` in C++, `koral_shader_parameter` and its
  siblings in C, `Shader.Parameters` in C# and `Shader.parameters` in Kotlin.
- A shader doesn't need to say where its descriptors go. Slang and GLSL descriptors without bindings are numbered
  by the compiler, and a pipeline can number them again (below).
- **Semantics.** A shader can ask for engine data by name instead of by binding: the camera's matrices, the time.
  Slang uses `[Kor(...)]` and GLSL uses `#pragma kor(...)`. A module fills the block. Vertex inputs are matched
  to a mesh's attributes by semantic (see [buffers, images and meshes](resources.md)).

## Graphics pipelines

```cpp
auto pipeline = kor::GraphicsPipeline::Builder{}
    .SetVertexShader(vs, mesh->Layout())        // the vertex layout to match the inputs against
    .SetFragmentShader(lit)
    .SetFramebuffer(gbuffer)                    // formats and sample count come from it
    .SetDepthStencilState({ .depthTestEnable = true, .depthWriteEnable = true })
    .SetRasterizationState({ .cullMode = kor::CullMode::eBack })
    .Build();
```

The other stages are `SetGeometryShader`, `SetTessellationState`, and `SetTaskShader` with `SetMeshShader`.
Input assembly, multisampling and colour blending are builder settings too. `SetSpecializationConstant` sets a
specialization constant, by the id its shader declares or by its name. Viewport, scissor and most rasterization state are dynamic: set them on the
command buffer, or leave them, and they default to the framebuffer's size.

## Compute pipelines

`kor::ComputePipeline::Builder{}.SetComputeShader(shader).Build()`, then
`cb.BindComputePipeline(p).BindDescriptorSet(0, set).Dispatch(x, y, z)` (or `DispatchIndirect`).

## Where a pipeline puts its descriptors

Every pipeline builder can move its shaders' descriptors to other sets and bindings, by name, whatever the shader
said:

```cpp
auto blur = kor::ComputePipeline::Builder{}
    .SetComputeShader(shader)
    .SetBinding("source", 0, 0)                 // set 0: bound once for the pipeline
    .SetBinding("target", 1, 0)                 // set 1: one per object
    .SetSpecializationConstant("radius", 4)
    .Build();
```

The pipeline is built from the shader's SPIR-V with those set and binding numbers written in. The shader itself
is untouched, so one compiled shader serves pipelines that number it differently, and changing the numbering
doesn't recompile it. A name no stage declares fails the build, and so do two descriptors put in one place. Sets
the pipeline doesn't use, below the highest it does, are empty.

## Descriptor sets

```cpp
auto set = kor::DescriptorSet::Builder(pipeline, 0)      // set 0, as the pipeline's shaders declare it
    .Write(0, cameraBuffer)
    .Write(1, albedo->View(), sampler)
    .Write("lights", lightBuffer)                        // or by the name the shader gives the binding
    .Build();
```

Bindless arrays are written element by element (`Write(binding, view, index)`). An unwritten element is allowed
where the shader doesn't read it. The access a binding gets (read, write, or both) is read from the shader, and
that's how the command buffer knows what barriers a dispatch or draw needs.

## Push constants

`cb.PushConstant("tint", kor::Vec4{1, 0, 0, 1})` writes one member of the push constant block, by name. Members
are merged across the pipeline's stages. Two stages declaring the same name with different types is a pipeline
error. `PushConstantBlock(value)` writes the whole block at once.

## GPU features beyond Koral's own

Koral creates the device with what it needs itself: dynamic rendering, descriptor indexing, buffer device
addresses, timeline semaphores. It also enables every core Vulkan 1.0 feature the GPU has. Anything more, for
example atomic floats for a particle solver, 64-bit integers, 16-bit storage, cooperative matrices or indirect
draw counts, is a `kor::Feature`. The project asks for it, and gets a device with it enabled:

```cpp
// At namespace scope in any source file of the library that uses it:
KORAL_REQUIRE_FEATURES(kor::Feature::eAtomicFloat32 | kor::Feature::eShaderInt64);   // can't run without
KORAL_REQUEST_FEATURES(kor::Feature::eCooperativeMatrix);                            // uses it where present

if (kor::Context::Supports(kor::Feature::eCooperativeMatrix)) UseTensorCores();
```

- **Where to ask.** The macros register when the library is loaded. The runtime loads the project's library
  before it creates the device, so its features are the device's. A program with its own `main` passes
  `AppSettings::requiredFeatures` / `optionalFeatures`, or lists its plugins in `AppSettings::libraries`, which
  are loaded before the device. `koral.json` can ask too:
  `"features": { "required": ["AtomicFloat32"], "optional": ["CooperativeMatrix"] }`.
- **A GPU without a required feature** is refused at startup, with a message naming each missing feature and who
  required it.
- **A library loaded after the device**, requiring a feature the device was made without, fails to load and says
  which feature, and where to ask for it instead.
- `Context::Supports(feature)` says what the device was made with. `Context::GpuHas(feature)` says what the GPU
  has, whether enabled or not.
- Mesh shaders and ray tracing are enabled wherever the GPU has them, asked for or not. Check
  `Context::SupportsRayTracing()` before building an acceleration structure.
