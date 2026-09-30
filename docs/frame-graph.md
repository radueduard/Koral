# The frame graph

A scene's rendering is a **frame graph**: render passes that say what they read, write and create, from
which the graph works out the order they run in, which ones are not needed, how long each resource it
makes has to live, and which of them can share memory. Every scene has one (`Graph()`), and so does
every view (`View::Graph()`).

```cpp
class SSAO final : public kor::RenderPass {
public:
    SSAO() : RenderPass("SSAO") {}
    void Setup(kor::PassBuilder& b) override {
        b.Read("depth", kor::Image::Usage::eSampled)
         .Read("normal", kor::Image::Usage::eSampled)
         .Create("ao", {.format = kor::Image::Format::eR8_UNORM, .usage = kor::Image::Usage::eStorage});
    }
    void Initialize(const kor::PassResources& r) override { /* pipelines and sets from r.ImageNamed(...) */ }
    void Record(kor::CommandBuffer& cb) const override { cb.BindComputePipeline(_pipeline).Dispatch(...); }
};

Graph().Add<SSAO>();
```

## Passes

| Hook | Thread | When |
|---|---|---|
| `Setup(builder)` | main | When the graph is rebuilt: declare what the pass uses. |
| `Initialize(resources)` | main | After allocation, and again when a resource it names changed. |
| `Prepare()` | main | Every frame: read the scene, write this frame's values. |
| `Record(commandBuffer) const` | any | Every frame, alongside the other passes. |

Passes record in parallel on the background pool; the frame ends their command buffers — which is where
barriers are worked out — in the order the graph decided. A `CpuPass` runs `Run()` on the pool instead,
before the GPU passes that use what it makes.

## Resources

| Declaration | Meaning |
|---|---|
| `Create(name, ImageDesc / BufferDesc)` | The graph makes it: sized to the screen (or a `scale` of it), allocated for exactly as long as it is used. |
| `Read(name, usage)` | Uses what the passes before it left. |
| `Write(name, usage)` | Modifies it in place; writers run in the order their passes were added. |
| `Consume(name, as, usage)` | Modifies it and publishes the result under a new name: readers of the old name run first. |
| `ReadPrevious(name, usage)` | Reads it as it was at the end of the previous frame (TAA, reprojection). |
| `SideEffect()` | Keeps the pass even when nothing reads what it makes. |
| `AsyncCompute()` | Runs it on the async compute queue (below). |

`FrameGraph::Screen` is the scene's window — or the view's target. `Graph().Import(name, image)` brings
in anything else. Resources the graph makes share memory when they are never in use at the same time
(`SetAliasing(false)` turns that off; `Memory()` says what it saved); `ImageNamed(name)` keeps one to
itself, for a screenshot, a debug view or another scene to read.

The graph's windows (`DrawMenuItems`, `DrawGUI`) show the schedule, what was culled or skipped, the
memory, and each pass's CPU and GPU time.

## Async compute

`AsyncCompute()` runs a compute pass on a second queue, alongside the graphics passes it does not depend
on — SSAO beside the shadow maps, light culling beside the depth pre-pass. The graph works out which
passes on the other queue it has to wait for, and keeps apart passes that would disturb each other:

- passes on different queues that share a resource are ordered, except when they only read it — a
  buffer freely, an image when every pass reading it this frame reads it in one state (declared with
  `eSampled` or `eTransferSrc`); then the pass that makes it leaves it in that state for all of them;
- what an async pass uses never shares memory;
- a pass using the screen stays on the graphics queue.

The second queue is of the graphics queue's family where the device has one (NVIDIA), and a family of
its own otherwise (AMD, Intel: `Context::AsyncComputeIsSeparateFamily()`). Then what both queues use has
to be made shared — the graph does that for what it creates, and an async pass that imports something
not made with `SetSharedAcrossQueues(true)` runs on the graphics queue, with a warning. On a device with
no second queue at all, async passes run in order, which is always correct. `Scheduler::Execute` takes
`CommandBuffer::Usage::eAsyncCompute` command buffers directly too, ordered by the tokens it waits for.

## Debug lines

`kor::DebugDrawPass` draws the scene's debug lines (`Debug::Line`, `Box`, `Sphere`, …) into the screen,
with the camera it is given — or into a view with that view's:

```cpp
Graph().Add<kor::DebugDrawPass>(SceneDebug(), [this] { return _camera->ViewProjection(); },
                                std::string(kor::FrameGraph::Screen), "depth");   // tested against "depth"
```
