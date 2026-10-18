# Descriptors: sets, the heap, and bindless

How a shader reaches the buffers, images and samplers it reads is the *descriptor model*. Koral has two, and a
project picks one in `koral.json`; nothing it writes depends on which. On top of either sit three things a
descriptor set alone does not give you — binding by name without a set, bindless indices, and embedded
samplers — and, on the heap only, a heap the project fills itself.

## Choosing the model

```json
{ "rendering": { "descriptors": "heap" } }
```

| Value | What the device does |
|---|---|
| `"sets"` (default) | Descriptor pools and sets, as every Vulkan device has them. |
| `"heap"` | `VK_EXT_descriptor_heap`: every descriptor lives in one table for the whole context, and binding a set is telling the shader where its run of that table starts. |

The same is `--descriptors heap` on the command line, `AppSettings::descriptors` (C++), `AppSettings.Descriptors`
(C#), `AppSettings(descriptors = ...)` (Kotlin) and `KoralAppSettings::descriptors` (C). The `KORAL_DESCRIPTORS`
environment variable (`sets`/`heap`) overrides all of them — it is how the test suites run twice.

A heap asked of a GPU without the extension is said once, and the device is made for sets: it never turns a run
that could start into one that does not. `Context::ActiveDescriptorModel()` says which one a run got. A project
that cannot do without the heap requires `Feature::eDescriptorHeap` instead (`features.required` in koral.json),
and is refused on such a GPU, naming it.

### What changes, and what does not

Under both models, a `DescriptorSet` is built, bound, rebound and reloaded the same way, the barriers a draw
needs follow from what is bound the same way, and a shader is the same SPIR-V. Under the heap:

- **Push constants share 256 bytes of push data with the sets.** Binding set *N* pushes where its descriptors
  start into the 8 bytes at `256 − 8·(N+1)`, so a pipeline using sets 0 and 1 has 240 bytes for push constants.
  A pipeline whose push constants reach into that room fails to build, and says how many bytes it has.
- **Every buffer a shader can bind has a device address**: the heap describes a buffer by one.
- **No pipeline layouts, no descriptor pools.** The heap is sized once (a quarter of a million resource slots
  and two thousand samplers, or what the GPU allows) and bound once per command buffer. A set that does not
  fit is refused with the numbers; a destroyed set gives its slots back when the GPU is done with them.

## Binding by name, without a set

```cpp
cb.BindGraphicsPipeline(pipeline)
  .Bind("camera", cameraBuffer)
  .Bind("albedo", texture, linearSampler)
  .DrawMesh(mesh);
```

The next draw or dispatch builds a set for each set index these name — starting from whatever set is bound
there, so `Bind` patches a set rather than requiring all of it — and binds it. Names follow
`DescriptorSet::Builder::Write` (`"textures[3]"` picks an element). Binding another pipeline forgets them, and so
does `BindDescriptorSet` at the same index. Each draw after a change builds a set, so for thousands of objects a
set built once per object is still cheaper.

## Bindless indices

The short way: push the resource itself into the `uint` the shader reads it by.

```cpp
cb.PushConstant("albedo", texture).PushConstant("linear", linearSampler);
```

```slang
import koral_bindless;
[[vk::push_constant]] cbuffer Push { uint albedo; uint linear; };
float4 color = kor::texture2D(albedo).Sample(kor::sampler(linear), uv);
```

An index is a `uint` the shader picks a resource by, at run time: from a push constant, a material buffer, a
vertex attribute. It stays the same for as long as its resource lives, and goes back when the resource is
destroyed (or `Bindless::Release`), once the frames that may read it are done. Ask for one by what the shader
will do with the resource — `Sampled`, `Storage`, `StorageBuffer`, `UniformBuffer`, `Sampler`,
`AccelerationStructure` — because the device describes an image sampled and an image written differently.

Pushing an image, image view, buffer, sampler or acceleration structure by name asks for its index on the way
(`pushConstant` in Kotlin, `PushConstant` in C#, `koral_cmd_push_resource` in C). Which table it goes in follows
from what the resource was made for: an image with only `Usage::eStorage` is a storage image, a buffer with only
`Usage::eUniform` a uniform buffer. One made for both is settled by which tables the bound pipeline reads; when
it reads both, or neither, the push is refused as ambiguous, and you push `Bindless::Sampled(image)` (or
`Storage`, `StorageBuffer`, `UniformBuffer`) instead. An image is viewed as its natural shape — push a view for
another. Ask for an index yourself, too, when it goes somewhere other than a push constant: into a material
buffer, say.

### Typed: Slang descriptor handles

A push constant can be a Slang descriptor handle — `T.Handle` — instead of a `uint`. The shader then uses it as
the resource itself, and the engine reads from its type which table to index, so nothing is ever ambiguous:

```slang
import koral_bindless;   // routes every handle into the tables below

struct Push { RWTexture2D<float4>.Handle output; Texture2D.Handle albedo; SamplerState.Handle linear; };

[shader("compute")] [numthreads(8, 8, 1)]
void run(uint3 id : SV_DispatchThreadID, uniform Push push)
{
    push.output[id.xy] = push.albedo.SampleLevel(push.linear, uv, 0);
}
```

```cpp
cb.PushConstant("push.output", image).PushConstant("push.albedo", texture).PushConstant("push.linear", sampler);
```

**Pushing a struct.** In Kotlin and C#, a class mirroring the shader's struct is pushed field by field, by name,
so it can hold resources and needs no matching padding: `pushConstant("push", PushConstants(image, color, 1f))`
pushes `push.outputTexture`, `push.color` and `push.radius` (C# camel-cases `OutputTexture`). `pushConstants(value)`
does the same for a push-constant block's bare field names. In C++, and for any struct copied whole
(`PushConstant("push", s)`, `PushConstantBlock`), the bytes are copied as they stand: a `kor::DescriptorIndex` is 8
bytes like a handle, and whatever index lands on a handle is synchronised for. An index stored in a plain `uint`
cannot be told from a number, so declare it with `Uses`.

A handle takes a resource or a `DescriptorIndex`, and refuses one of another kind ("a handle to a storage image,
but a sampled image index was pushed"). Without `import koral_bindless`, Slang would resolve handles into arrays
of its own at set 0, so import it. Handles work for textures (`Texture*` sampled, `RWTexture*` storage),
`SamplerState`, `StructuredBuffer`/`ByteAddressBuffer` (and their `RW` forms), `ConstantBuffer<T>` (reached as
`(*handle).field`) and `RaytracingAccelerationStructure`. Texel buffers and combined `Sampler2D`s have no table,
and pushing into one is refused. A handle is 8 bytes (a `uint2`, index first); writing one in a buffer means
writing the index into its first word.

The tables are declared at **`kor::BindlessSet` (set 7)**, one per kind, by `koral_bindless.slang` (or
`koral_bindless.glsl`, for GLSL). Keep a pipeline's own sets below 7. Under sets the engine binds the tables'
set for every pipeline that declares it; under the heap, the tables *are* the heap. A uniform-buffer table is
typed by its block, so a shader declares its own at binding 3:
`[[vk::binding(3, 7)]] ConstantBuffer<Material> materials[];`.

**Barriers.** An index pushed with `PushConstant(name, index)` brings its resource into the draw's uses: the
draw is synchronised for it as for a bound one. An index the shader reads out of a buffer cannot be seen;
declare what it reaches with `Uses`, which holds until the next pipeline is bound:

```cpp
cb.BindComputePipeline(shade)
  .Uses(materialTexture)                                    // an index the shader reads from a buffer
  .Uses(output, kor::ResourceAccess::eAllShaderReadWrite)   // a buffer it writes through one
  .Dispatch(groups, 1, 1);
```

Nothing can check a use that was not declared.

**Limits.** A per-frame resource has a different object each frame, and so no single index: it is refused.
Under sets, storage images, storage and uniform buffers and acceleration structures need the device's
update-after-bind for their kind; `Bindless::Capacity(kind)` is 0 where it has none.

## Embedded samplers

```cpp
auto pipeline = kor::GraphicsPipeline::Builder()
    .SetSampler("albedoSampler", linear)       // a sampler binding, or a combined image sampler
    ...
    .Build();
kor::DescriptorSet::Builder(pipeline, 0).Write("albedo", texture);   // no sampler written
```

The pipeline carries the sampler: a sampler binding needs nothing written, a texture binding only its image.
Immutable samplers under sets, embedded ones under the heap. A name that is no sampler binding fails the build.

## A heap the project fills (heap only)

```cpp
auto materials = kor::DescriptorHeap::Builder().SetSlotCount(1024).Build();
materials->Write(0, kor::Descriptor(albedoView), kor::DescriptorType::eSampledImage);
cb.PushConstant("first", materials->Index(0, kor::BindlessKind::eSampledImage));
```

A run of the context's heap that is yours: you decide what is in each slot and when, and a shader reaches any
slot through the bindless tables by `Index`. Descriptors become data:

- `DescriptorHeap::Encode(descriptor, type)` gives a descriptor's bytes; a buffer of them goes into place with
  `cb.CopyDescriptors(source, offset, heap, firstSlot, count)`, synchronised for the shaders after it.
- A shader may write descriptors itself at `heap->DeviceAddress(slot)`; `cb.DescriptorHeapBarrier()` then makes
  them visible to the shaders that follow.

What the slots point at is invisible to the resolver: declare it with `Uses`. Under descriptor sets the builder
fails with `ErrorCode::eDescriptorHeapUnsupported`.

## From C#, Kotlin and C

The same, by the same names:

```csharp
var index = Bindless.StorageBuffer(input);
cb.BindComputePipeline(pipeline).Bind("data", output).PushConstant("source", index).Dispatch();
```

```kotlin
val index = Bindless.storageBuffer(input)
cb.bindComputePipeline(pipeline).bind("data", output).pushConstant("source", index).dispatch()
```

In C: `koral_bindless_*`, `koral_cmd_bind`, `koral_cmd_push_index`, `koral_cmd_uses`/`koral_cmd_uses_index`,
`koral_*_pipeline_builder_set_sampler`, and `koral_descriptor_heap_*` with `koral_cmd_copy_descriptors`.
`Context.ActiveDescriptorModel` (C#), `Context.activeDescriptorModel` (Kotlin) and
`koral_context_descriptor_model()` say which model a run got.
