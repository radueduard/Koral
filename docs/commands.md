# Command buffers

A `kor::CommandBuffer` records GPU work. A scene is handed the frame's in `Render`. Anything else creates one,
records into it, and submits it:

```cpp
void Scene::Render(kor::CommandBuffer& cb) {
    cb.BeginRendering(gbuffer)
      .BindGraphicsPipeline(pipeline)
      .BindDescriptorSet(0, set)
      .PushConstant("model", transform)
      .DrawMesh(mesh, 1, 0)
      .EndRendering();
}
```

Commands chain, and most return the command buffer. Ordinary C++ control flow works around them: `If` and
`ForEach` exist so a frame written as one expression doesn't have to be broken up.

## Barriers are worked out for you

Every command says which resources it reads and writes, and how: a sampled image, a storage buffer written by a
dispatch, an attachment, a copy destination. When the recording is resolved, the barriers and layout transitions
that implies are inserted where they are legal. That is often not where the command sits. For example, an image
sampled inside a render pass is transitioned before the pass opens.

What the analysis can't see:

- **Buffers reached through raw device addresses** (`buffer_reference`) appear in no descriptor set. Declare those
  with `Barrier()`. A write to one that is never covered by a barrier is reported, naming the command.
- **A read inside a render pass of what that pass writes** (a feedback loop) can't be fixed by a barrier, and is
  reported as one: render into another image, or end the pass first.

## End, Submit, and threads

- `Begin()` starts a recording and clears the previous one's errors.
- `End()` resolves the recording (works out its barriers against where the command buffer resolved before it
  left each resource) and emits it. Errors are on the buffer from here: `Ok()`, `Errors()`. `Run` callbacks run
  here, in the place they were recorded.
- `Submit({.waitFor = tokens, .signal = tokens})` hands it to its queue. It doesn't wait for completion: signal a
  token and wait for it, or `co_await` it (see [tokens and tasks](tokens.md)).

Barriers are resolved against the order command buffers are **ended**. So command buffers that share resources
must be ended in the order they're submitted. On one thread that happens naturally. **On several threads, call
`Submit()` without `End()`:** it ends and submits in one step, taking turns with every other thread, so the order
work is resolved in is the order it reaches the queue.

```cpp
// On any thread:
auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eCompute);
cb->Begin();
cb->BindComputePipeline(solver).BindDescriptorSet(0, set).Dispatch(groups, 1, 1);
cb->Submit({ .signal = { done } });        // ended and submitted in one step
```

A command buffer submitted after one that was resolved against it (ended first, submitted second) is reported by
`Submit()`, naming the resource. The alternative is barriers that silently start from the wrong state.

Recording itself is free to happen on any thread, each command buffer on one thread at a time.

## Draws the GPU decides

`DrawIndirect`, `DrawIndexedIndirect` and `DrawMeshTasksIndirect` read their parameters from a buffer
(`IndirectDrawCommand` and its siblings in `structs.h`), so a compute shader can cull and build the draw list.
A stride of 0 means the commands are packed. With `kor::Feature::eDrawIndirectCount` (ask for it with
`KORAL_REQUIRE_FEATURES`), `DrawIndirectCount` and `DrawIndexedIndirectCount` also read *how many* to draw from
a buffer, at most `maxDrawCount`. The culling shader counts survivors with an atomic add, and the CPU never
learns the number:

```cpp
cb.BindGraphicsPipeline(pipeline).BindMesh(mesh)
  .DrawIndexedIndirectCount(commands, 0, visibleCount, 0, maxObjects);
```

Both buffers are read as indirect buffers, and the barrier after the compute pass that wrote them is inserted
for you.

## One-off work

`CommandBuffer::SingleTimeCommand(record)` records, submits and returns a `Token`. `Upload(record)` does the same,
and makes every later submission wait for it on the GPU, so uploads never stall the CPU (see
[parallel work](parallel.md)).

## Work in the frame

Work recorded elsewhere joins the frame through the scheduler. Hand it over begun but not ended, and the frame
ends it in order with its own:

```cpp
auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eAsyncCompute);
cb->Begin();
RecordSimulation(*cb);
const kor::Token simulated = kor::Context::Scheduler().Execute(std::move(cb), { .placement = kor::Scheduler::Placement::eBeforeFrame });
```

`Execute` returns the work's own completion token. `ExecuteInfo::after` holds it back until other tokens have
happened. `Usage::eAsyncCompute` runs on a second queue, beside the frame's graphics work, where the GPU has one.
`Scheduler().WaitFor(token)` holds the whole frame back, and `FrameCompletion()` is the token of the frame being
drawn. The [frame graph](frame-graph.md) does all of this for its passes.

## Timers

`BeginTimer("shadows") … EndTimer()`, or `Timer("shadows", [&]{ … })`, measures GPU time between two points.
Scopes nest. The results of a recording are read at its next `Begin()` with `Timings()`, without waiting.

## Errors

A failed command records an error and puts the buffer into a failed state, where later GPU work is skipped, so a
chain never has to be interrupted to be checked. Ask afterwards with `Ok()` or `Errors()`. `Submit()` returns the
first error. See [errors](errors.md).
