# Tokens and tasks

Koral has one way to say "after this has happened": a `kor::Token`. A token stands for an event (a submission
finishing, a frame completing, a task finishing, something the CPU signals) and it can be waited on, `co_await`ed,
or handed to the GPU to wait on. Concurrency is coroutines (`kor::Task`) that suspend on tokens, not raw threads.
The design is in [v2 foundation design](v2-foundation-design.md).

## Tokens

```cpp
const kor::Token ready = kor::Token::Create();

ready.Signal();                 // from anywhere: the CPU says it happened
ready.Wait();                   // blocks this thread until it has
co_await ready;                 // suspends this coroutine until it has; no thread is held
if (ready.Ready()) { … }

cb->Submit({ .waitFor = { ready }, .signal = { done } });   // the GPU waits for one, and signals the other
```

A token is a value on a `kor::Timeline`: a counter owned by one producer that only moves forward.
`timeline.Next()` reserves the next value. `timeline.At(n)` names value n without reserving it, which is how
both sides of a long-running loop agree on step n. A timeline is plain CPU state until a token of it reaches a GPU
submission. Then it gets a timeline semaphore, and one background thread waits on all of them at once, resuming
whatever was waiting.

Two rules from Vulkan: a submission must not wait for a signal on its own queue that hasn't been submitted yet,
and a present must not depend on one either. The scheduler holds a present back until such a token is on its
way.

## Tasks

A function returning `kor::Task<T>` is a coroutine. It starts when called, runs until it first suspends, and
continues wherever it is resumed.

```cpp
kor::Task<Mesh> LoadTerrain(std::string path) {
    co_await kor::Context::SwitchToBackgroundThread();
    auto heights = ReadHeights(path);                              // off the main thread
    auto mesh = co_await BuildMesh(heights);                       // another task
    co_await kor::Context::SwitchToMainThread();
    co_return mesh;
}
```

- `co_await task` yields its value, and rethrows what it threw. From ordinary code, `Wait()`, then `Take()`.
- `kor::WhenAll(tasks)` finishes when all of them have.
- A task resumes on the executor it suspended on: the main thread, or the background pool. The scene that was
  current when it suspended is current again.
- A `Task` owns its coroutine. It must outlive the work: destroying one that waits on a token is safe (it's never
  resumed), but destroying one that is running at that moment on another thread is not.

## Cancelling a task

`task.Cancel()` asks a task to stop. It throws `kor::Cancelled` at its next suspension point, so it unwinds:
destructors run, and a `catch` can tidy up and rethrow. Where it is waiting at that moment, it is woken at once:

- on a token: resumed now, without the token happening;
- on another task: that task is cancelled too, and its stopping wakes this one.

```cpp
kor::Task<void> Stream(World& world) {
    try {
        while (true) {
            co_await world.ChunkNeeded();                          // woken here when cancelled
            LoadChunk(world);
            co_await kor::CancellationPoint{};                     // a place to stop in a long stretch of work
        }
    } catch (const kor::Cancelled&) {
        world.ReleasePending();
        throw;
    }
}

streaming.Cancel();
streaming.Wait();                       // stopped
assert(streaming.IsCancelled());        // and Take() reports "cancelled"
```

Cancellation is cooperative: work between two suspension points runs on to the next one. Cancelling a task that
has finished does nothing.

## Parallel loops

`kor::ParallelFor` returns a `kor::Work`, which is awaitable like a task. See [parallel work](parallel.md).
