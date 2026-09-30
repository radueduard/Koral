# Parallel work and waiting

## ParallelFor

`kor::ParallelFor(begin, end, body)` calls `body(i)` for every `i` in the range, on every core. The range
is split into a few parts per core, and each part is an ordinary loop, so a small body costs about what a
plain loop would. It returns a `kor::Work`, which you either wait for or `co_await`:

```cpp
void Physics::FixedUpdate() {
    kor::ParallelFor(0, bodies.size(), [&](std::size_t i) { Integrate(bodies[i], dt); }).Wait();
}

kor::Task<void> Terrain::Build() {
    co_await kor::ParallelFor(0, chunks.size(), [&](std::size_t i) { Mesh(chunks[i]); });
    Upload();      // resumes on the thread it started on, once every part is done
}
```

- `Wait()` doesn't just block. The waiting thread runs parts of this loop too, and only this loop, never
  unrelated work from the pool. A short loop therefore costs no thread switch, and nested loops can't
  deadlock.
- `co_await` suspends the coroutine and resumes it where it was, the way awaiting a `Token` does.
- If the body throws, the exception is thrown again from `Wait()` or the `co_await`. When several parts
  throw, you get the first.
- `ParallelForRanges(begin, end, body(first, last))` hands each part over whole. Use it for a SIMD loop,
  or when each part needs one scratch allocation.
- The last argument, `grain`, sets the number of indices per part. The default is `count / (cores × 4)`.
- `Work::Completion()` is a `Token`, so you can pass it to `Submit({.waitFor})` or to anything else that
  takes a token.

Parts run at the same time. The body should only touch what its index owns, or synchronise. Like a
render pass's `Record`, it should not use the scene's `Window`, `Input` or `Time`. The scene that started
the loop is current in every part. With no application or context there is no pool, so the loop runs on
the calling thread.

## Uploads don't make the CPU wait

Giving a buffer or image its initial data, writing a device-local buffer (`Write`, `WriteAt`), clearing an
image, building an acceleration structure, and importing an image all submit their copy without
waiting. Every later GPU submission waits for those copies on the GPU:

- the frame: each queue's first submit;
- one-off `CommandBuffer::SingleTimeCommand`s;
- `Submit`.

A read or a draw straight after an upload therefore sees the data. Staging buffers are freed only once
the GPU is done with them, because of deferred destruction.

For your own copies, use `CommandBuffer::Upload(record)` where you would use `SingleTimeCommand(record).Wait()`.
It returns the `Token`, in case something wants to wait for it.

## Reading back

`Buffer::Read`/`ReadAt` return the data, so they still wait for the copy out. `Buffer::ReadAsync` doesn't:

```cpp
kor::Task<void> Physics::ReportContacts() {
    const auto contacts = co_await _contacts->ReadAsync<Contact>();   // no stall
    for (const auto& contact : contacts) Report(contact);
}
```

Host-visible buffers (`eDynamic`, `eReadback`, …) are read at once. There is nothing to wait for.

## Where the CPU still waits

- `Read`/`ReadAt`, and saving an image to a file: both return what the GPU wrote.
- Presenting a frame that depends on a token the CPU signals: the present has to wait for it.
- Teardown, reload and closing a window: the GPU has to let go of what is being destroyed.
- The join at the end of a frame graph's parallel recording. There the frame needs every pass
  recorded, and helping out could run unrelated pool work on the main thread.
