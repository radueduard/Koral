# Buffers, images and meshes

Everything the GPU reads or writes is a resource made by a builder. `Build()` never throws: it returns a
`kor::Resource<T>`, which is either valid or *poisoned* with the `kor::Error` that explains why (see
[errors](errors.md)). A resource is owned by whoever holds the `Resource`; anything that only uses it holds a
`ResourceRef`, which notices when the resource is gone. Destroying a resource the GPU may still be using is safe:
its memory is freed once the work submitted before it has finished.

## Buffers

```cpp
// Typed: the element type gives the stride, and the data the size.
auto particles = kor::Buffer::Builder<Particle>{}
    .SetData(initialParticles)                        // any contiguous range of Particle
    .SetUsage(kor::Buffer::Usage::eStorage | kor::Buffer::Usage::eTransferDst)
    .SetType(kor::Buffer::Type::eDeviceLocal)
    .Build();

// Raw: a size in bytes and nothing else.
auto readback = kor::Buffer::RawBuilder{}.SetRawSize(1 << 20)
    .SetUsage(kor::Buffer::Usage::eTransferDst).SetType(kor::Buffer::Type::eReadback).Build();
```

- **Usage** says what the buffer may be bound as: `eVertex`, `eIndex`, `eUniform`, `eStorage`, `eIndirect`,
  `eTexel`, `eShaderDeviceAddress`, `eAccelerationStructureInput`, and `eTransferSrc` / `eTransferDst` for
  copies. `SetUsage` *replaces* the default set, so name the transfer roles too when you upload into or read
  back from the buffer. A command that needs a role the buffer lacks is reported, naming the missing flag.
- **Type** says where the memory lives. `eDeviceLocal` is fastest for the GPU and can't be mapped. `eDynamic`
  (the default) is host memory the CPU reads and writes cheaply. `eDeviceDynamic` is GPU memory the CPU can
  write directly where the platform allows it (ReBAR). `eStaging` is for copies only: reading it on the CPU is
  very slow. `eReadback` is for reading results back.
- **Per frame**: `RawBuilder::SetIsPerFrame(true)` gives one copy per frame in flight, so the CPU can write
  next frame's data while the GPU reads this frame's. Every command picks the copy of the frame it runs in.

Reading and writing:

| | |
|---|---|
| `Write(range, offset)`, `WriteAt(index, value)` | Writes elements. Device-local buffers are written through a staging copy that the CPU doesn't wait for. |
| `Read(count, offset)`, `ReadAt(index)` | Returns the data, waiting for any copy out. |
| `co_await ReadAsync<T>()` | The same without stalling the CPU. See [parallel work](parallel.md). |
| `Map()` | A mapping of a host-visible buffer, as a span of `T`. |
| `DeviceAddress()` | The buffer's GPU address, for `buffer_reference` (needs `eShaderDeviceAddress`). |

## Images

```cpp
auto albedo = kor::Image::Builder{}
    .SetType(kor::Image::Type::e2D)
    .SetFormat(kor::Image::Format::eRGBA8_SRGB)
    .SetExtent(glm::uvec2{ 1024, 1024 })
    .SetMipLevels(11)                      // the full chain for 1024², generated from the data by SetData
    .SetUsage(kor::Image::Usage::eSampled | kor::Image::Usage::eTransferDst | kor::Image::Usage::eTransferSrc)
    .SetData(pixels)
    .Build();
```

- **Usage**: `eSampled` (a texture), `eStorage`, `eColorAttachment`, `eDepthStencilAttachment`, and the
  transfer roles: `eTransferDst` to upload, `eTransferSrc` to read back or generate mipmaps.
- **Formats** include 8, 16 and 32-bit colour formats, depth and stencil formats, and block-compressed formats
  (BC, ETC2, ASTC). The image modules ([`kimg`](../modules)) load and save files, and transcode KTX2.
- **Views**: `image->View(shape, …)` returns a view of a mip range, an array layer range, or a cube. Views are
  cached, and stay valid across `Resize`.
- **Resize**: `image->Resize(extent)` replaces the storage. Framebuffers, views and descriptor sets made from
  the image follow it.

Layouts and barriers are never yours to manage: a command says how it uses an image, and the transitions are
worked out when the command buffer is resolved. See [command buffers](commands.md).

## Samplers

`kor::Sampler::Builder{}.SetMinFilter(…).SetMagFilter(…).SetAddressModeU(…)…Build()`. Anisotropy, comparison
(for shadow maps), LOD bias and clamping are all builder settings.

## Meshes and vertex layouts

A `kor::Mesh` is vertex buffers and, optionally, an index buffer, described by a `kor::VertexLayout`: its
bindings (each a buffer, with a stride) and its attributes (what each vertex holds, and where).

```cpp
kor::VertexLayout layout;
layout.bindings.push_back({ .binding = 0, .stride = sizeof(Vertex) });
layout.attributes.push_back({ .semantic = "POSITION", .binding = 0, .offset = offsetof(Vertex, position),
                              .channelType = kor::ChannelType::eFloat, .channelCount = 3 });

auto mesh = kor::Mesh::Builder{}.SetVertexLayout(layout)
    .SetVertexBuffer(0, std::move(vertices)).SetIndexBuffer(std::move(indices)).Build();
```

Attributes are matched to a vertex shader's inputs by **semantic**: the shader annotates them (Slang `: POSITION`,
GLSL `#pragma mesh(POSITION)`). A layout that names no semantics is matched by location instead: an attribute's
`location`, or else its place in the list. The mesh module ([`kmesh`](../modules)) describes common vertex
formats for you.

**Matrices, arrays and instancing.** An input of several locations, such as a `mat4` (one location per column),
is fed by an attribute with that many `locations`. `VertexLayout::Attribute::Matrix(semantic, binding, offset)`
makes one. A binding with `.inputRate = kor::VertexInputRate::eInstance` steps once per instance instead of
once per vertex. Bind such a buffer beside the mesh with `CommandBuffer::BindVertexBuffer`:

```cpp
layout.bindings.push_back({ .binding = 1, .stride = sizeof(Instance), .inputRate = kor::VertexInputRate::eInstance });
layout.attributes.push_back(kor::VertexLayout::Attribute::Matrix("TRANSFORM", 1, offsetof(Instance, model)));

cb.BindMesh(cube).BindVertexBuffer(1, instances).DrawIndexed(kor::WholeSize, instanceCount);
```

A matrix input answered by a one-location attribute (or the other way round) is a pipeline error that names
both, rather than a shader reading garbage.
