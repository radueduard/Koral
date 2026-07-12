# GFX unit tests

GoogleTest-based unit tests for the framework's **CPU-side, GPU-independent**
logic. They run without a Vulkan device, window or swap chain, so they are safe
for CI and fast (<1s).

## What is covered

| File                       | Unit under test        | Focus |
|----------------------------|------------------------|-------|
| `test_tlsf_allocator.cpp`  | `kor::TLSFAllocator`   | alloc/free, splitting, coalescing, accounting, OOM, a randomized no-overlap stress test |
| `test_flags.cpp`           | `kor::Flags<Enum>`     | bit set/test/combine/mask, equality, conversions |
| `test_error.cpp`           | `error.h` / `error.cpp`| `describe()` completeness, `Error::toString()`, `fail()`, `guard()`, `Result::valueOrThrow`, cause chains (`history()`, `root()`, `causedBy()`) |
| `test_structs.cpp`         | `structs.h`            | `sizeofChannelType()`, default pipeline-state values |
| `test_mesh_layout.cpp`     | `meshLayout.h`         | std430 alignment, `ParamVertex` stride/offsets, `FindPositionAttribute` |
| `test_resource.cpp`        | `resource.h`           | `Resource` move/ownership, `ResourceRef` dangling detection, const/upcast conversions, poisoned resources and in-place repair |
| `test_builder_recoverable.cpp` | `builder.h` / `buffer.h` | which builders may be retained for a retry (`Builder::Recoverable`), and that copying a `Buffer::Builder` does not alias the source's data |

## Running

Tests build by default. Disable with `-DGFX_BUILD_TESTS=OFF`.

```sh
cmake --build cmake-build-debug --target koral_tests
ctest --test-dir cmake-build-debug            # or:
./cmake-build-debug/tests/koral_tests           # gtest runner directly
```

## GPU integration tests (`integration/`)

A second binary, `koral_integration_tests`, exercises the real GPU paths (VMA
allocation, staging, transfer/compute queues, `SingleTimeCommand` submit/fence).
It brings up a **device-only headless context** via `kor::Context::InitHeadless()`
— no window, surface or swap chain — so it runs anywhere a Vulkan device exists,
**no display required** (CI-friendly). The harness (`GpuEnvironment`) inits once
for the whole binary; tests derive from `GpuTest`, which **skips** them if no
Vulkan device can be created, so the suite is still safe to run on a machine with
no GPU.

- `test_buffer_roundtrip.cpp` — device-local & staging upload/readback,
  `WriteAt`/`ReadAt`, partial reads, out-of-range rejection.
- `test_compute_dispatch.cpp` — full compute round-trip: compiles
  `shaders/doubleValues.comp.glsl`, reflects its descriptor layout, builds a
  `ComputePipeline`, binds a storage buffer, dispatches, and verifies every
  element was doubled on the GPU.

Enable/disable with `-DGFX_BUILD_INTEGRATION_TESTS=ON|OFF` (default ON). These
require a desktop session with a Vulkan-capable GPU; in a headless/CI shell they
skip. Good next target: image upload/readback (sampled + storage images).

## Scope / not covered (unit suite)

The pure-logic unit suite deliberately excludes anything needing a live GPU —
that lives in `integration/`. The `CommandBuffer` railway validation (e.g. "no
pipeline bound" `ErrorCode`s) is a good next target: those checks are pure logic
but currently reachable only through the backend command buffer, so exposing
them would let them be unit-tested without a device.
