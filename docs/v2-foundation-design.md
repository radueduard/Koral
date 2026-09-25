# v2 Foundation Design — Tokens, Features, Concurrency, and Building On Top

> **Status:** design, not yet implemented. This branch (`v2-development`) is where the
> work below lands. `master` ships v1: the current feature set plus the assurance that
> projects run correctly for *mostly single-threaded* apps on all target platforms.

## 1. Goal

After the v1 release, we want to build — **on top of the SDK, without ever forking it** —

1. a **higher-level engine** (camera, transform, material, light, a renderer), and
2. a **GPU physics engine** with real compute/GPU support, and
3. a **multithreaded renderer**.

The higher-level substrate (ECS/component model, camera, material, light, the plugin
system that loads reusable third-party features) lives in the **downstream engine**, not
in the SDK. Plugins are just more dynamic libraries, loaded the same way projects already
are (`dlopen`/`LoadLibrary` + a C entry symbol, mirroring `CreateScene`/`CreateJob`/
`CreateProjectConfig`). The SDK's only job is to be a **sufficient, frozen foundation** the
three consumers above can be written against.

The audit of the current public API found it already close: compute pipelines, storage
buffers, `Buffer::getDeviceAddress()` (buffer_reference), host `Map()`/`Write()`, readback
buffers, `DispatchIndirect`, combinable `eStorage|eVertex|eIndirect` usage, standalone
`CommandBuffer::Create(Usage::eCompute)` + `Submit()`, `ResourceAccess` barriers, and the
bindless device features are all present. What follows is what must be *added* so the three
consumers never need to edit the SDK.

## 2. Principles

- **Coroutines, never raw threads, in consumer code.** All off-main-thread work is a
  coroutine scheduled onto the existing executors; users `co_await SwitchToBackgroundThread()`.
- **Tokens are the only synchronization primitive** the API exposes. No mutexes, fences, or
  semaphores in public headers.
- **Hide hard Vulkan.** Queues, timeline semaphores, and pools stay behind ergonomic
  abstractions.
- **The SDK is a frozen base.** Consumers link `Koral::Koral` and never patch it; every
  capability below is reachable from the public surface.

## 3. The unified `Token`

A single type bridges CPU coroutine scheduling and GPU timeline synchronization, because a
timeline semaphore can be waited on by **both** CPU and GPU. A `Token` is:

- **co_awaitable** — `co_await token` suspends the coroutine until signalled (by another
  coroutine *or* by GPU completion), resuming it on a pool thread.
- **blocking-waitable** — `token.Wait()` for synchronous, non-coroutine code (e.g. the
  builder-side buffer upload paths in `buffer.h`).
- **pollable** — `token.Ready()`.
- **GPU-schedulable** — handed to the scheduler as a wait/signal (§6).

### Bidirectional token (long-lived loops)

A one-shot token signals completion once. A **bidirectional token** is a *recurring
two-phase rendezvous* for producer/consumer loops — two monotonic counters, signalling both
ways each cycle:

```
main   signalRequest(n)  ─▶  loop  awaitRequest(n)     // "simulate step n"
loop   signalDone(n)     ─▶  main  awaitDone(n)        // "results for n ready"
```

Neither side busy-waits. A long-lived loop (e.g. physics) is a **suspending coroutine**: when
it `co_await`s its token it suspends and returns the pool thread; it resumes on signal. So a
long-lived loop costs nothing while idle, and the pool multiplexes many.

```cpp
Task<void> physicsLoop(BiToken& sync) {
    co_await Context::SwitchToBackgroundThread();
    for (glm::u64 n = 1; running; ++n) {
        co_await sync.awaitRequest(n);   // suspends; frees the pool thread
        simulateStepsFor(n);             // record + dispatch (or CPU sim)
        sync.signalDone(n);              // hand results back to the frame
    }
}
```

### `SingleTimeCommand` returns a `Token`

`CommandBuffer::SingleTimeCommand(command, usage)` returns a `Token` instead of `void`; the
internal `runSingleTimeCommand(..., VkFence, VkSemaphore, VkSemaphore, bool wait)` loses the
`wait` bool and the raw handles behind it. Fire-and-forget drops the token; synchronous
callers use `.Wait()`; coroutines `co_await` it.

**Built (2026-09-23).** It no longer blocks, and is `[[nodiscard]]`, so every caller states its
intent (`.Wait()`, `co_await`, or `(void)`). The command buffer is held on a token-keyed retire
list, released by the next one-off on the recording thread (or flushed at shutdown, before modules
unload). Resources its commands use are **not** kept alive — same rule as the frame's command
buffer — until deferred destruction lands. Internal `vk::Device::runSingleTimeCommand` returns a
token too; its four callers `.Wait()` on it, which blocks on that submission alone instead of
`queue->waitIdle()`.

## 4. Feature flags — usage-driven registrar (approach A)

We want: **using a feature automatically records it in a per-module flag; the module (DLL)
exports that flag; the runtime checks it against the GPU + backend before running, and uses
it to enable exactly the right device features.**

**Honest constraint:** C++ has no way to build a `constexpr` set that aggregates across
translation units. So "compile-time generated" means *membership is decided at compile/link
time by whether a feature-gated symbol is used; the mask is materialized at load*.

### Mechanism

Each feature is an enum value with a stable bit. A feature-gated API **ODR-uses**
`RequireFeature<Feature::eX>`. Only *used* features instantiate their registrar (a variable-
template specialization); each registrar's load-time initializer ORs its bit into a
module-global mask. The module exports `korRequiredFeatures()` returning the mask.

- **Header inclusion must not register — only *use* does.** The ODR-use is tied to the
  gated API's instantiation, never to header scope. This must be verified against
  dead-code elimination / `--gc-sections`.
- Portable across ELF/COFF/Mach-O (no custom linker sections); cost is a trivial load-time
  init. (The alternative — a linker-section table with zero load-time init — was rejected as
  too toolchain-fragile, especially under MSVC.)

### Required vs optional (two tiers)

- **Hard-required:** the gated API auto-registers. Absent on the GPU/backend ⇒ refuse to
  launch.
- **Optional / probed:** used behind a runtime check, registers **nothing**. Expressed as
  `ifSupported<Feature::eX>([&]{ ... })`, which branches on a `Supports*()` probe. Ray
  tracing stays here (it already works via `SupportsRayTracing()`), so a project touching RT
  still runs on a non-RT GPU.

### Aggregation and dual use

- The **project DLL, the engine DLL, and every plugin DLL** each export their own mask of
  what *they* use. The runtime **ORs all loaded modules' masks** before checking. A physics
  *plugin* that uses atomic-float thus contributes that requirement even if the game project
  never touches it — essential for reusable plugins.
- The union mask does double duty: it **gates compatibility** (GPU + backend) **and drives
  which device features to enable** at device creation. This **replaces** `device.cpp`'s
  hardcoded `require(...)` list and **subsumes** the earlier idea of a runtime
  `requestFeatures()` call — nothing manual to keep in sync.

### Feature taxonomy

- **Hardware:** `eAtomicFloat` (`VK_EXT_shader_atomic_float` — SPH/particles), `eInt64`,
  `eInt64Atomics`, `eSubgroup*`, `eCooperativeMatrix`, …
- **Backend:** `eAsyncCompute`, `eTimelineTokens`, `eParallelRecording`. These are
  **Vulkan-only** — GL cannot provide them, so a project that uses them fails the load check
  under the GL backend exactly as a project needing atomic-float fails on a weak GPU.

### Load-time flow

1. Select physical device + backend.
2. Load project/engine/plugin modules; read each `korRequiredFeatures()`.
3. OR them → the required set.
4. Query GPU features + backend capabilities → the available set.
5. If `required ⊄ available`: refuse to launch, naming the missing feature(s) and the module
   that needs them (reuse the existing `missingFeatures` message style), or prefer a GPU that
   satisfies the set (device selection already ranks candidates).
6. Enable exactly the required device features at device creation.

## 5. Multithreaded command recording

**Current state:** `Device::requestCommandBuffer(queue, thread)` accepts a `thread` argument
(the frames and `CommandBuffer::Create()` already pass `hash<thread::id>`) but **ignores it**
— `_commandPools` is keyed by queue only, with no mutex. Vulkan command pools require
external synchronization, so the real v1 contract is **"all command recording on the main
thread."** (The async importer honours it by marshaling GPU work back via
`SwitchToMainThread`.)

~~**v2:** key `_commandPools` by `(queueId, thread)`.~~ **Built differently (2026-09-23): a
pool per command buffer.** A pool must be used by one thread at a time, *recording into its
buffers included*, and a coroutine can start recording on one pool thread and finish on another
after a `co_await` — so a per-thread pool would still end up shared. Each command buffer instead
leases a `(pool, buffer)` pair from a free list (mutex-guarded, keyed by queue); the destructor
resets the pool and returns the pair. The only rule left is the one callers already follow: one
thread records a given command buffer at a time. The `thread` argument is gone.

The same step made the rest of the submission path safe to share: one device-wide queue lock
(`Device::lockQueues()`) around every `vkQueueSubmit`, `vkQueuePresentKHR`, queue/device wait-idle
(`Device::waitIdle()`) and ImGui's platform-window submits; a mutex on lazy queue creation; and
one on the descriptor pool. `SeveralThreadsRecordAndSubmitAtOnce` (8 threads) is clean under the
validation layer's thread-safety checks; with the submit lock removed it reports THREADING errors
and hangs the queue.

Still single-threaded: **recording the same resource from two threads at once** — the core's
barrier resolution reads and writes per-resource state (image layouts) at `End()`. That is §6's
cross-command-buffer problem.

**Vulkan-first.** GL is thread-affine; its deferred-replay backend *could* allow off-thread
recording if the record-time state mirror is made thread-safe, but replay serializes on the
GL thread. MT rendering is a backend feature (§4) GL does not provide.

## 6. The scheduler seam (the shared enabler)

**Today** the scheduler owns a *closed* single-command-buffer, single-graphics-queue frame
submit (`scheduler.cpp` builds wait/signal/fence internally; all consumer GPU work goes into
one command buffer). There is no seam for a second queue, extra semaphores, or externally
recorded command buffers.

Async physics, the MT renderer, and async compute **all want the same thing**: to contribute
command buffers recorded elsewhere (another thread/queue) and order them against the frame.
So we build **one** seam:

- **External command-buffer injection:** accept command buffers recorded elsewhere into the
  current frame, executed in caller-specified order.
- **Async compute submit:** submit a command buffer to a compute queue, returning a `Token`.
- **Frame ordering hooks:** `scheduler.frameWaitsOn(Token, stage)` and
  `Token scheduler.frameSignal()`.

Build it once; it serves all three consumers.

**Built (2026-09-23)** as `Scheduler::Execute(cb, Placement)`, `Scheduler::WaitFor(Token)` and
`Scheduler::FrameCompletion()`.

- *Ordering across command buffers.* The resolver already carries each resource's state from one
  `End()` to the next, so it is correct whenever End order equals execution order. The seam keeps
  that true instead of replacing it: executed command buffers are handed over **begun but not
  ended**, and the frame ends them itself, in execution order (before-frame, the frame, after-frame),
  then submits all of them in one batch. Recording — the user's code and its validation — runs in
  parallel; resolving and emitting is serialised by a lock around `End()`, which also makes
  standalone submits on other threads safe. Submit-time patching (resolve each buffer in isolation,
  patch entry barriers at submit) was the alternative; it needs Vulkan image layouts decoupled from
  emission and is only worth it if serialised emission shows up in a profile.
- *Present rule.* A present may not depend on a semaphore signal that has not been submitted, so a
  `WaitFor` token the CPU has yet to signal holds up the present (not the recording or the submit).
  Timelines track the highest value a submitted GPU operation will signal to tell the cases apart.
  Found by synchronization validation (VUID-vkQueuePresentKHR-pWaitSemaphores-03268).
- *OpenGL.* The CPU waits for `WaitFor` tokens before submitting; frame completion is a
  `glFenceSync` polled by the next `Draw`.
- *Not done:* a real second (async compute) queue. `Execute` refuses a command buffer from another
  queue; cross-queue work goes through `Submit` + tokens, and needs queue-family ownership
  transfers once a separate family is used.

## 7. GPU physics (the driving use case)

**Inline model** (works on the *current* API today, no seam): record physics compute into the
per-frame command buffer (`Scene::Render` hands you one) with a barrier before the vertex
read. Correct, deterministic, single queue. Fine for moderate physics.

**Async model** (the performance ceiling): physics is a long-lived coroutine (§3) on the
background executor, suspended on a bidirectional token. It owns its **compute-queue**
submission (per-thread pool, §5), ordered against the frame by GPU tokens (§6); shared state
is double-buffered so the two queues run flat-out.

Two independent axes resolve the "how does it run under FixedUpdate?" question:

- **FixedUpdate multiplicity** is a *recording* concern: running the sim K times = recording
  K dispatch batches with GPU barriers between them (no CPU fence per step). A `MAX_STEPS`
  clamp handles the spiral of death.
- **Async overlap** is a *submission* concern: which queue, ordered by a token. Orthogonal
  to K.

Split physics by latency needs: **gameplay-critical** (low latency, readback gated by a
token, ideally same frame) vs **visual** (debris/cloth/particles — async, happily one frame
behind).

## 8. Plugins (downstream engine's concern, enabled by the above)

Plugins are DLLs loaded like projects (`dlopen` + a C `korRegisterPlugin` entry). Each
exports its own feature mask (§4), aggregated by the runtime. Discovery/linking is the Hub's
job via `koral.json` (which already reserves Hub-only keys like `libraries`).

**ABI discipline** (generalizes the v1 ImGui-DLL work):

- Every plugin links the **single shared copies** of imgui/glm/glfw/vulkan — never a second
  static copy. `koral_copy_runtime_dlls` already stages them.
- Any **new engine global stays behind a `KORAL_API` accessor** — never an inline-static in
  a header — or the plugin and engine get separate copies.
- **C++ interface, same-toolchain**, enforced by the Hub (compiler-id + ABI-version stamp).
  The component store is type-erased on the engine side; typed `add<T>()` templates
  instantiate in the plugin's TU, keeping templates off the ABI boundary.

## 9. Open decisions

- **Physics submission ownership:** does the physics coroutine own its GPU submission
  (per-thread pool + compute queue — full async, designed around) or only produce work the
  main thread submits (a strict subset)?
- ~~**Token backing:**~~ **Built (2026-09-23).** `kor::Timeline` is a monotonic counter owned
  by one in-order producer; `Token` is a value on one (`Next()` reserves, `At(n)` names a value
  without reserving, for rendezvous loops). A timeline gets a `VkSemaphore` lazily, the first
  time a token of it reaches `Submit({.waitFor, .signal})`; until then it is pure CPU. One
  reactor thread (`vk::TokenReactor`) waits-any over every GPU-backed timeline with a parked
  coroutine plus a private wake timeline, and hands due coroutines to the executor they suspended
  on (main thread → main, else background pool). Submitted tokens are held by the command buffer
  until it is re-recorded, so fire-and-forget is safe. OpenGL blocks the submitting thread
  instead. Still open: cancellation (a `Task` destroyed while parked leaves a dangling handle).
- **Feature enum** finalization and granularity.
- **Secondary command buffers** under dynamic rendering (intra-pass parallelism) — later /
  optional; separate-pass parallelism via §5 + §6 captures most of the win.
- **Substrate shape** in the downstream engine (minimal ECS vs transforms-only vs
  feature-registry-only) — an engine decision, not an SDK one, but it gates plugin interop.

## 10. v1 / v2 boundary

**v1 (master):** current feature set; single-threaded-correct on all target platforms. The
only threading rule is the documented **"record commands on the main thread"** contract — no
code change needed, just a doc note. The Windows ImGui single-copy fix, device-lost handling,
and scene-interface cleanup landed here.

**v2 (this branch):** everything in §3–§8.


## 10. Built after the plan (2026-09-23)

- **Deferred destruction.** Every Vulkan submission also signals a per-queue *epoch* timeline;
  `vk::Context::DestroyWhenUnused` holds a destruction until every queue's last submitted epoch is
  reached. Deliberately not per-resource last-use stamping: epochs need no tracking and cover what
  descriptor sets and framebuffers reference indirectly, at the cost of freeing a frame or two late.
  Buffers, images, views, descriptor sets, samplers, acceleration structures and pipelines use it,
  which removed every `waitIdle` stall their destruction and resize paths had. ImGui textures get a
  main-thread-only equivalent (ImGui's descriptor pool is not locked). Covers work *submitted*
  before the destroy — destroying something a not-yet-submitted recording still uses remains a
  caller error.
- **Cancelling a waiting coroutine.** Destroying a `Task` whose coroutine waits on a token is safe:
  the awaiter's destructor cancels its waiter slot, and resumes claim the slot only when they run
  (`Executor::Post`), so a resume already queued is skipped too. Cooperative cancellation
  (`Task::Cancel()`, `kor::Cancelled`) is not built.
- **ImGui platform-window hazard** fixed by `vcpkg-overlay-ports/imgui`.
