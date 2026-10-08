using System.Runtime.CompilerServices;
using Koral.Scripting;
using Buffer = Koral.Buffer;

namespace Koral.Tests;

/// <summary>Clears the screen to a colour the scene picks, and copies it where the test can read it.</summary>
public sealed class Paint(Buffer readback) : RenderPass("Paint")
{
    private Image? _screen;
    public Vec4 Color = new(0, 0, 0, 1);

    public override void Setup(PassBuilder builder) =>
        builder.Write(FrameGraph.Screen, Image.Usage.eTransferDst | Image.Usage.eTransferSrc).SideEffect();

    public override void Initialize(PassResources resources) => _screen = resources.ImageNamed(FrameGraph.Screen);

    public override void Record(CommandBuffer commandBuffer) =>
        commandBuffer
            .ClearColorImage(_screen!, Color)
            .CopyImageToBuffer(_screen!, readback);
}

public sealed class Painter : Scene
{
    public static Painter? Last;

    [Keep] public int Updates;
    [Keep] private string _note = "fresh";
    public int FixedUpdates, JumpPressedAt = -1;
    public UVec2 Extent;
    public bool ThrowOnce;
    public Buffer? Readback;
    public Buffer MadeInConstructor = new Buffer.Builder<float>().SetInstanceCount(16).SetUsage(Buffer.Usage.eStorage).Build();
    public Paint? Pass;
    public bool Green;
    public string Note => _note;

    public Painter(SceneArgs args)
    {
        Green = args.Flag("green");
        Last = this;
    }

    protected override void Initialize()
    {
        Readback = new Buffer.Builder<byte>()
            .SetInstanceCount(16 * 16 * 4)
            .SetUsage(Buffer.Usage.eTransferDst)
            .SetType(Buffer.Type.eReadback)
            .Build();
        Pass = Graph.Add(new Paint(Readback) { Color = Green ? new Vec4(0, 1, 0, 1) : new Vec4(1, 0, 0, 1) });
        Input.BindAction("Jump", Key.eSpace, GamepadButton.eA);
        Window.SetTitle("painting");
    }

    protected override void FixedUpdate() => ++FixedUpdates;

    protected override void Update()
    {
        ++Updates;
        Extent = Window.Extent;
        if (Input.IsActionPressed("Jump")) JumpPressedAt = Updates;
        if (ThrowOnce)
        {
            ThrowOnce = false;
            throw new InvalidOperationException("thrown from Update");
        }
        _note = $"updated {Updates}";
    }
}

public sealed class Refuses : Scene
{
    public Refuses() => throw new InvalidOperationException("this scene cannot be made");
}

public sealed class Lines : Scene
{
    public static int CameraCalls;
    public static WeakReference? Camera;
    private readonly Mat4 _projection = KMath.Orthographic(-2, 2, -2, 2, -1, 1);

    protected override void Initialize()
    {
        // Capturing the scene: a lambda that captures nothing is cached by the compiler, and never collected.
        Func<Mat4> camera = () => { ++CameraCalls; return _projection; };
        Camera = new WeakReference(camera);
        Graph.Add(new DebugDrawPass(SceneDebug, camera));
    }

    protected override void Update()
    {
        Debug.Line(new Vec3(-1, 0, 0), new Vec3(1, 0, 0));
        Debug.Box(-Vec3.One * 0.5f, Vec3.One * 0.5f, new DebugStyle { Color = new Vec4(0, 1, 0, 1), OnTop = true });
        Debug.Sphere(Vec3.Zero, 1f, new DebugStyle { Duration = 1f });
        Debug.Arrow(Vec3.Zero, Vec3.UnitY);
        Debug.Axes(Mat4.Identity);
    }
}

/// <summary>Awaits GPU work and a Task from its hooks, and notes where it resumed.</summary>
public sealed class Waiter : Scene
{
    public Scene? ResumedIn, DelayedIn, ReadIn;
    public int[] Filled = [], ReadLater = [];

    protected override async void Initialize()
    {
        var buffer = new Buffer.Builder<int>().SetInstanceCount(4).SetUsage(Buffer.Usage.eTransferDst | Buffer.Usage.eStorage)
            .SetType(Buffer.Type.eDynamic).Build();
        await CommandBuffer.SingleTimeCommand(cb => cb.FillBuffer<int>(buffer, [7, 7, 7, 7]));
        ResumedIn = Current;
        Filled = buffer.Read<int>();
        await Task.Delay(5);
        DelayedIn = Current;
        using var gpuOnly = new Buffer.Builder<int>().SetData(new[] { 3, 1, 4, 1, 5 }).SetType(Buffer.Type.eDeviceLocal).Build();
        ReadLater = await gpuOnly.ReadAsync<int>();   // the upload, then the copy out: neither waited for
        ReadIn = Current;
    }
}

public static partial class Cases
{
    private static void Frames(App app, int count)
    {
        for (var i = 0; i < count; ++i) Check.That(app.Frame(), "a frame");
    }

    private static OffscreenSettings Offscreen(uint size) => new() { Extent = new UVec2(size, size) };

    /// <summary>A scene in C#: its hooks, its window and input through the facades, a pass, and its state.</summary>
    public static void SceneRunsAndPaints()
    {
        using var app = Check.HeadlessApp();
        app.Register<Painter>();
        var failures = new List<string>();
        App.HookFailed += (_, hook, _) => failures.Add(hook);

        var scene = app.OpenOffscreen("Painter", Offscreen(16), new SceneArgs().Set("green", true));
        var painter = Painter.Last!;
        Check.That(ReferenceEquals(scene, painter), "Open returns the C# scene itself");
        Check.That(painter.Green, "the arguments reached the constructor");
        Check.Equal("Painter", scene.Name, "its name");
        Check.Equal("painting", scene.SceneWindow.Title, "its window, set from inside through Window");

        Frames(app, 2);
        Thread.Sleep(40);   // more than a fixed step of real time
        Frames(app, 1);
        scene.SceneInput.FeedKey(Key.eSpace, true);
        Frames(app, 1);
        painter.ThrowOnce = true;
        Frames(app, 2);   // the frames in flight

        Check.Equal(6, painter.Updates, "updated every frame, the one that threw included");
        Check.That(painter.FixedUpdates > 0, "fixed updates ran");
        Check.Equal(new UVec2(16, 16), painter.Extent, "its window's size, through Window");
        Check.Equal(4, painter.JumpPressedAt, "fed input arrives the next frame, as the action");
        Check.That(failures.SequenceEqual(["Update"]), $"the throw was reported, and only it: [{string.Join(", ", failures)}]");
        Check.Equal(6ul, scene.SceneTime.FrameCount, "its clock");

        Check.GpuIsDone();
        var texel = painter.Readback!.Read<byte>(4);
        Check.That(texel is [0, 255, 0, 255], $"the pass painted the screen green: [{string.Join(", ", texel)}]");

        Check.Equal("{\"Updates\":6,\"note\":\"updated 6\"}", scene.SaveState(), "the [Keep] members, by name");
        scene.LoadState("{\"Updates\":40,\"note\":\"loaded\",\"gone\":1}");
        Check.Equal(40, painter.Updates, "loaded back");
        Check.Equal("loaded", painter.Note, "private ones too");

        var readback = painter.Readback;
        var madeInConstructor = painter.MadeInConstructor;
        app.Close(scene);
        Check.That(!app.Frame(), "no scene left");
        Check.That(!scene.IsOpen, "a closed scene is not open");
        Check.That(readback.IsDisposed && madeInConstructor.IsDisposed, "what the scene made went with it");
    }

    /// <summary>Builders as in C++: chained, and poisoning rather than throwing — the cause kept all the way down.</summary>
    public static void BuildersChainAndPoison()
    {
        using var app = Check.HeadlessApp();

        var image = new Image.Builder()
            .SetFormat(Image.Format.eRGBA16_SFLOAT)
            .SetExtent(new UVec2(32, 8))
            .SetMipLevels(2)
            .SetUsage(Image.Usage.eSampled | Image.Usage.eTransferDst)
            .Build();
        Check.That(image.Valid && image.Owned, "an image, from a chain");
        Check.Equal(new UVec3(32, 8, 1), image.Extent, "its extent");
        Check.Equal(Image.Format.eRGBA16_SFLOAT, image.PixelFormat, "its format");
        Check.Equal(2u, image.MipLevels, "its mip levels");
        var view = image.View();
        Check.That(!view.Owned && view.SourceImage.Equals(image), "its own view, borrowed, of it");

        var bad = new Buffer.Builder<float>().SetInstanceCount(-1);
        Check.That(bad.HasErrors, "a setter's problem is known before the build");
        var poisonedBuffer = bad.Build();
        Check.That(poisonedBuffer.Poisoned && !poisonedBuffer.Valid, "and the build is poisoned, not thrown");
        Check.Equal(ErrorCode.eBufferSizeInvalid, poisonedBuffer.Failure?.Code, "with the reason's code");
        Check.Throws<KoralException>(() => _ = poisonedBuffer.Size, "a poisoned resource's own members throw");

        var shader = new Shader.Builder().SetPath("no/such/shader.comp").SetStage(Shader.Stage.eCompute).Build();
        Check.That(shader.Poisoned, "a shader that is not there");
        var pipeline = new ComputePipeline.Builder().SetComputeShader(shader).Build();
        Check.That(pipeline.Poisoned, "what is built from it is poisoned too");
        Check.That(pipeline.Failure!.History.Contains("no/such/shader.comp"), $"and names the cause: {pipeline.Failure.History}");

        using var sampler = new Sampler.Builder().SetMinFilter(Filter.eNearest).SetAddressModeU(Sampler.AddressMode.eClampToEdge).Build();
        Check.That(sampler.Valid, "a sampler");
        sampler.Dispose();
        Check.Throws<ObjectDisposedException>(() => _ = sampler.Name, "a disposed resource refuses to be used");
    }

    /// <summary>Mappings are scoped by using; a host buffer written through one reads back.</summary>
    public static void MappingsAreScoped()
    {
        using var app = Check.HeadlessApp();
        using var buffer = new Buffer.Builder<int>().SetInstanceCount(8).SetType(Buffer.Type.eDynamic).Build();
        using (var mapping = buffer.Map<int>())
        {
            for (var i = 0; i < mapping.Count; ++i) mapping[i] = i * i;
        }
        Check.That(buffer.Read<int>().SequenceEqual([0, 1, 4, 9, 16, 25, 36, 49]), "what the mapping wrote");
        using (var mapping = buffer.MapConst<int>(2, 3))
            Check.That(mapping.AsSpan().SequenceEqual(new[] { 9, 16 }), "a const mapping of part of it");
        buffer.WriteAt(1, 100);
        Check.Equal(100, buffer.ReadAt<int>(1), "WriteAt and ReadAt");
        Check.Equal(32ul, buffer.Size, "its size, in bytes");
    }

    private const string ComputeShader = """
        #version 450
        layout(local_size_x = 4) in;
        layout(std430, set = 0, binding = 0) buffer Data { uint values[]; } data;
        layout(push_constant) uniform Push { uint add; } push;
        void main() { data.values[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 10u + push.add; }
        """;

    /// <summary>The whole path: a shader, a pipeline, a descriptor set by name, a push constant by name, a dispatch.</summary>
    public static void ComputeDispatches()
    {
        using var app = Check.HeadlessApp();
        var directory = Directory.CreateTempSubdirectory("koral-compute-");
        try
        {
            var path = Path.Combine(directory.FullName, "fill.comp");
            File.WriteAllText(path, ComputeShader);
            var pipeline = new ComputePipeline.Builder()
                .SetComputeShader(new Shader.Builder().SetPath(path).SetStage(Shader.Stage.eCompute).Build())
                .Build();
            Check.That(pipeline.Valid, $"a compute pipeline: {pipeline.Failure}");
            Check.That(pipeline.HasPushConstant("add"), "its push constant, by name");
            Check.Equal(0u, pipeline.SetLayout(0).FindBinding("data"), "its binding, by name");

            var values = new Buffer.Builder<uint>().SetInstanceCount(4)
                .SetUsage(Buffer.Usage.eStorage | Buffer.Usage.eTransferSrc | Buffer.Usage.eTransferDst)
                .SetType(Buffer.Type.eDynamic).Build();
            var set = new DescriptorSet.Builder(pipeline, 0).Write("data", values).Build();
            Check.That(set.Valid, $"a descriptor set: {set.Failure}");

            CommandBuffer.SingleTimeCommand(cb => cb
                .BindComputePipeline(pipeline)
                .BindDescriptorSet(0, set)
                .PushConstant("add", 7u)
                .Dispatch(), CommandBuffer.Usage.eCompute).Wait();
            var read = values.Read<uint>();
            Check.That(read.SequenceEqual([7u, 17u, 27u, 37u]), $"the shader wrote them: [{string.Join(", ", read)}]");

            var errors = new List<string>();
            CommandBuffer.SingleTimeCommand(cb =>
            {
                cb.BindComputePipeline(pipeline).PushConstant("add", 1f).Dispatch();
                errors.AddRange(cb.Errors);
            }, CommandBuffer.Usage.eCompute).Wait();
            Check.That(errors.Count == 1 && errors[0].Contains("add"), $"a push constant of the wrong type is kept as an error: [{string.Join(" | ", errors)}]");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    private const string UnboundShader = """
        #version 450
        layout(local_size_x = 4) in;
        layout(constant_id = 0) const uint factor = 3;
        layout(std430) buffer Data { uint values[]; } data;
        layout(push_constant) uniform Push { uint add; } push;
        void main() { data.values[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * factor + push.add; }
        """;

    /// <summary>A shader with no bindings, reflected; a pipeline that numbers its descriptor itself and sets a constant by name.</summary>
    public static void PipelinesNumberTheirShadersDescriptors()
    {
        using var app = Check.HeadlessApp();
        var directory = Directory.CreateTempSubdirectory("koral-bindings-");
        try
        {
            var path = Path.Combine(directory.FullName, "unbound.comp");
            File.WriteAllText(path, UnboundShader);
            var shader = new Shader.Builder().SetPath(path).SetStage(Shader.Stage.eCompute).Build();
            Check.That(shader.Valid, $"a shader with no bindings: {shader.Failure}");
            Check.That(shader.Parameters.Any(p => p.Name == "data" && p.Type == DescriptorType.eStorageBuffer), "its buffer, reflected");
            Check.That(shader.PushConstants.Any(p => p.Name == "add"), "its push constant, reflected");
            var constant = shader.SpecializationConstants.Single();
            Check.That(constant is { Name: "factor", Id: 0, DefaultValue: 3 }, $"its specialization constant, reflected: {constant}");

            var pipeline = new ComputePipeline.Builder()
                .SetComputeShader(shader)
                .SetBinding("data", 2, 5)
                .SetSpecializationConstant("factor", 10u)
                .Build();
            Check.That(pipeline.Valid, $"a pipeline that moves it: {pipeline.Failure}");
            Check.Equal(5u, pipeline.SetLayout(2).FindBinding("data"), "where the pipeline put it");

            var values = new Buffer.Builder<uint>().SetInstanceCount(4).SetType(Buffer.Type.eDynamic).Build();
            var set = new DescriptorSet.Builder(pipeline, 2).Write("data", values).Build();
            CommandBuffer.SingleTimeCommand(cb => cb.BindComputePipeline(pipeline).BindDescriptorSet(2, set).PushConstant("add", 7u).Dispatch(),
                                            CommandBuffer.Usage.eCompute).Wait();
            var read = values.Read<uint>();
            Check.That(read.SequenceEqual([7u, 17u, 27u, 37u]), $"the shader wrote them through it: [{string.Join(", ", read)}]");

            var typo = new ComputePipeline.Builder().SetComputeShader(shader).SetBinding("dtaa", 0, 0).Build();
            Check.That(!typo.Valid, "a name the shader does not declare fails the build");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    /// <summary>An await in a hook resumes on the application's thread, with its scene current again.</summary>
    public static void AwaitResumesInItsScene()
    {
        using var app = Check.HeadlessApp();
        var waiter = (Waiter)app.OpenOffscreen("Waiter", new Waiter(), Offscreen(8));
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (waiter.ReadIn is null && DateTime.UtcNow < deadline) app.Frame();
        Check.That(ReferenceEquals(waiter.ResumedIn, waiter), "after GPU work, in its scene");
        Check.That(waiter.Filled.SequenceEqual([7, 7, 7, 7]), $"with the work done: [{string.Join(", ", waiter.Filled)}]");
        Check.That(ReferenceEquals(waiter.DelayedIn, waiter), "and after an ordinary Task, too");
        Check.That(ReferenceEquals(waiter.ReadIn, waiter), "and after ReadAsync");
        Check.That(waiter.ReadLater.SequenceEqual([3, 1, 4, 1, 5]), $"with what it read: [{string.Join(", ", waiter.ReadLater)}]");
    }

    /// <summary>Debug lines, drawn by a DebugDrawPass whose camera is C# — and let go of with the scene.</summary>
    public static void DebugLinesDraw()
    {
        using var app = Check.HeadlessApp();
        app.Register<Lines>();
        var scene = app.OpenOffscreen("Lines", Offscreen(32));
        Frames(app, 4);
        Check.That(Lines.CameraCalls >= 4, $"the camera was asked each frame ({Lines.CameraCalls})");
        Check.That(scene.SceneDebug.LineCount > 0, "the lines are there");
        app.Close(scene);
        app.Frame();
        for (var i = 0; i < 10 && Lines.Camera!.IsAlive; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check.That(!Lines.Camera!.IsAlive, "and let go of when the scene closed");
    }

    /// <summary>Filled shapes are triangles, and a gizmo dragged by a pointer of our own moves what it is on.</summary>
    public static void DebugFillsAndGizmos()
    {
        using var app = Check.HeadlessApp();
        app.Register<Lines>();
        var scene = app.OpenOffscreen("Lines", Offscreen(32));
        var draw = scene.SceneDebug;
        draw.Clear();
        draw.Box(Mat4.Identity, new DebugStyle { Fill = new Vec4(1, 0, 0, 0.5f), Outline = false });
        Check.That(draw.TriangleCount == 12 && draw.LineCount == 0, $"a filled box without its outline ({draw.TriangleCount}, {draw.LineCount})");
        draw.SpotLight(Vec3.Zero, -Vec3.UnitY, 5f, 0.5f);
        Check.That(draw.LineCount == 29, $"a spot light's cone ({draw.LineCount})");

        // A camera at (0, 0, 5) looking at the origin.
        var viewport = new Vec2(800, 600);
        var viewProjection = KMath.Perspective(MathF.PI / 3f, 800f / 600f, 0.1f, 100f) * KMath.LookAt(new Vec3(0, 0, 5), Vec3.Zero, Vec3.Up);
        Vec2 ToScreen(Vec3 p)
        {
            var clip = viewProjection * new Vec4(p, 1f);
            return new Vec2((clip.X / clip.W * 0.5f + 0.5f) * viewport.X, (clip.Y / clip.W * 0.5f + 0.5f) * viewport.Y);
        }
        var center = ToScreen(Vec3.Zero);
        var x = KMath.Normalize(ToScreen(new Vec3(0.01f, 0, 0)) - center);
        var transform = Mat4.Identity;
        draw.Gizmo(GizmoMode.eTranslate, ref transform, viewProjection, new GizmoPointer(center + x * 60f, viewport, true, true), id: 1);
        Check.That(draw.GizmoActive, "the X arrow is grabbed");
        var moved = draw.Gizmo(GizmoMode.eTranslate, ref transform, viewProjection, new GizmoPointer(center + x * 160f, viewport, true, false), id: 1);
        Check.That(moved && transform.C3.X > 0.5f && MathF.Abs(transform.C3.Y) < 1e-4f,
                   $"and dragged along X ({transform.C3})");
        draw.Gizmo(GizmoMode.eTranslate, ref transform, viewProjection, new GizmoPointer(null, viewport, false, false), id: 1);
        Check.That(!draw.GizmoActive, "and let go of");
        app.Close(scene);
        app.Frame();
    }

    /// <summary>Failures that are not builds come back as exceptions, with Koral's reason.</summary>
    public static void FailuresAreExceptions()
    {
        using var app = Check.HeadlessApp();
        app.Register<Refuses>();
        var unknown = Check.Throws<KoralException>(() => app.OpenOffscreen("Nope", Offscreen(8)), "an unknown scene");
        Check.That(unknown?.Message.Contains("Nope") == true, $"says which: {unknown?.Message}");
        Check.Throws<KoralException>(() => app.OpenOffscreen("Refuses", Offscreen(8)), "a constructor that throws");
        Check.Throws<InvalidOperationException>(() => new App(), "a second application");
        Check.Throws<ArgumentException>(() => app.Register(typeof(string)), "a type that is not a scene");
        Check.Throws<KoralException>(() => Scene.Input.Get(), "the current scene's input, with none current");
        Check.That(InputSource.Parse("Key.Nothing") is null, "a source that is not one");
        Check.Equal("-GamepadAxis.LeftY", InputSource.Parse("-GamepadAxis.LeftY")?.Name, "and one that is, round the other way");
        Check.Equal("Key.Space", ((InputSource)Key.eSpace).Name, "a key, as a source");
    }

    /// <summary>Arguments, as the C interface carries them.</summary>
    public static void SceneArgsRoundTrip()
    {
        var args = new SceneArgs().Set("map", "caves").Set("level", 3).Set("hard", true).Set("scale", 1.5);
        var back = SceneArgs.FromJson(args.ToJson());
        Check.Equal("caves", back.String("map"), "a string");
        Check.Equal(3L, back.Integer("level"), "an integer");
        Check.That(back.Flag("hard"), "a flag");
        Check.Equal(1.5, back.Number("scale"), "a number");
        Check.Equal(7L, back.Integer("missing", 7), "a default");
    }

    /// <summary>koral.json and flags, read the runtime's way.</summary>
    public static void ProjectIsRead()
    {
        var directory = Directory.CreateTempSubdirectory("koral-project-");
        try
        {
            File.WriteAllText(Path.Combine(directory.FullName, "koral.json"),
                """{ "scene": "Level", "name": "From the file", "rendering": { "platform": "none", "window": { "width": 640, "vsync": false } } }""");
            using (var project = ProjectConfig.Load(["--hot-reload"], directory.FullName))
            {
                Check.Equal("Level", project.Scene, "the scene koral.json names");
                Check.That(project.HotReload, "--hot-reload");
                Check.Equal("From the file", project.WindowSettings.Title, "the window's title");
                Check.Equal(WindowPlatform.eNone, project.AppSettings.Platform, "the platform");
                Check.Equal(640u, project.WindowSettings.Extent.X, "the window's width");
                Check.That(!project.WindowSettings.Vsync, "and its vsync");
            }
            using (var project = ProjectConfig.Load(["--scene", "Menu"], directory.FullName))
                Check.Equal("Menu", project.Scene, "a flag over the file");
            Check.Throws<KoralException>(() => ProjectConfig.Load(["--no-such-flag"], directory.FullName), "an unknown flag");
            Check.That(ProjectConfig.Usage.Contains("--hot-reload"), "the usage lists the flags");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    private const string ScriptV1 = """
        public sealed class Counter : Scene
        {
            [Keep] public int Frames;
            public static int Version => 1;
            protected override void Update() { Frames += 1; Debug.Line(Vec3.Zero, Vec3.One); }
        }
        """;

    private const string ScriptV2 = """
        public sealed class Counter : Scene
        {
            [Keep] public int Frames;
            [Keep] public string Added = "new field";
            public static int Version => 2;
            protected override void Update() { Frames += 100; }
        }
        """;

    private const string Broken = "public sealed class Counter : Scene { this does not compile }";

    // By name each time: a reload replaces the scene, so the object from before it is no longer it.
    private static object? Field(App app, string name, string scene = "Counter") => Field(app.FindScene(scene)!, name);

    private static object? Field(Scene scene, string name)
    {
        var type = scene.GetType();
        return (object?)type.GetField(name)?.GetValue(scene) ?? type.GetProperty(name)?.GetValue(scene);
    }

    /// <summary>Scenes from source: compiled, run, edited, reloaded with their state — and the old build unloaded.</summary>
    public static void ScriptsReloadWithState()
    {
        var directory = Directory.CreateTempSubdirectory("koral-scripts-");
        try
        {
            var script = Path.Combine(directory.FullName, "Counter.cs");
            File.WriteAllText(script, ScriptV1);
            using var app = Check.HeadlessApp();
            using var host = new ScriptHost(app, directory.FullName);
            host.Load();
            Check.That(host.Scenes.SequenceEqual(["Counter"]), $"its scenes: [{string.Join(", ", host.Scenes)}]");

            var before = OpenWeakly(app, "Counter");
            Frames(app, 3);
            Check.Equal(3, Field(app, "Frames"), "the script runs");
            Check.Equal(1, Field(app, "Version"), "version 1");

            File.WriteAllText(script, ScriptV2);
            Check.That(host.Reload(), "the edit reloads");
            CheckReplaced(before);
            Check.Equal(3, Field(app, "Frames"), "its state was kept");
            Check.Equal("new field", Field(app, "Added"), "a field added in the edit starts at its default");
            Check.Equal(2, Field(app, "Version"), "the new code is what runs");
            Frames(app, 1);
            Check.Equal(103, Field(app, "Frames"), "and it runs the new Update");

            File.WriteAllText(script, Broken);
            var errors = new List<string>();
            host.BuildFailed += e => errors.AddRange(e);
            Check.That(!host.Reload(), "a broken edit does not reload");
            Check.That(errors.Count > 0 && errors[0].Contains("Counter.cs"), $"it says where: {errors.FirstOrDefault()}");
            Frames(app, 1);
            Check.Equal(203, Field(app, "Frames"), "and the scene keeps running the last good build");

            Check.That(Unloads(host), "the first build was unloaded");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    // Held weakly, and only inside these: a strong reference to a scene of the first build, left in a
    // stack slot of the test itself, would be what keeps that build from being unloaded.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference OpenWeakly(App app, string name) => new(app.OpenOffscreen(name, Offscreen(8)));

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CheckReplaced(WeakReference before)
    {
        if (before.Target is not Scene scene) return;   // gone altogether: certainly not open
        Check.That(!scene.IsOpen, "the scene from before the reload is not open");
        Check.Equal("", scene.Name, "and is refused rather than read");
    }

    private const string Ticking = """
        public sealed class Tick : Scene
        {
            [Keep] public int Frames;
            public static int Initialized;
            protected override void Initialize() { Initialized += 1; Graph.Add(new Paint()); }
            protected override void Update() { Frames += 1; }
        }

        public sealed class Paint() : RenderPass("Paint")
        {
            public static int Setups, Initializes;
            private Image? _screen;
            public override void Setup(PassBuilder builder) { Setups += 1; builder.Write(FrameGraph.Screen, Image.Usage.eTransferDst).SideEffect(); }
            public override void Initialize(PassResources resources) { Initializes += 1; _screen = resources.ImageNamed(FrameGraph.Screen); }
            public override void Record(CommandBuffer commandBuffer) => commandBuffer.ClearColorImage(_screen!, new Vec4(1, 0, 0, 1));
        }
        """;

    private static int Static(Scene scene, string type, string field) =>
        (int)scene.GetType().Assembly.GetType(type)!.GetField(field)!.GetValue(null)!;

    /// <summary>
    /// Edits applied to the running code: a body edit changes nothing else, a pass's Setup or Initialize is
    /// run again, a scene's Initialize reopens only it — and a new field is a new build.
    /// </summary>
    public static void InPlaceEditsKeepWhatIsRunning()
    {
        var directory = Directory.CreateTempSubdirectory("koral-inplace-");
        try
        {
            var script = Path.Combine(directory.FullName, "Tick.cs");
            File.WriteAllText(script, Ticking);
            using var app = Check.HeadlessApp();
            using var host = new ScriptHost(app, directory.FullName);
            Check.That(host.UpdatesInPlace, "in-place updates are on (DOTNET_MODIFIABLE_ASSEMBLIES=debug)");
            host.Load();
            app.OpenOffscreen("Tick", Offscreen(8));
            Frames(app, 2);
            var scene = app.FindScene("Tick")!;
            Check.Equal(2, Field(app, "Frames", "Tick"), "it runs");

            void Edit(string from, string to)
            {
                var text = File.ReadAllText(script);
                Check.That(text.Contains(from), $"the script has '{from}'");
                File.WriteAllText(script, text.Replace(from, to));
                Check.That(host.Reload(), $"'{to}' applies");
            }

            Edit("Frames += 1;", "Frames += 100;");
            Check.Equal(ReloadKind.InPlace, host.LastReload, "a body edit is applied in place");
            Check.That(ReferenceEquals(app.FindScene("Tick"), scene), "the very same scene carries on");
            Frames(app, 1);
            Check.Equal(102, Field(app, "Frames", "Tick"), "running the new Update");
            Check.Equal(1, Static(scene, "Tick", "Initialized"), "not initialized again");

            var setups = Static(scene, "Paint", "Setups");
            var initializes = Static(scene, "Paint", "Initializes");
            Edit("new Vec4(1, 0, 0, 1)", "new Vec4(0, 1, 0, 1)");
            Check.Equal(ReloadKind.InPlace, host.LastReload, "a pass's Record edit, in place");
            Frames(app, 1);
            Check.Equal(setups, Static(scene, "Paint", "Setups"), "Record's edit does not set the graph up again");

            Edit("Initializes += 1;", "Initializes += 1; _ = resources;");
            Check.Equal(ReloadKind.InPlace, host.LastReload, "a pass's Initialize edit, in place");
            Frames(app, 1);
            Check.Equal(initializes + 1, Static(scene, "Paint", "Initializes"), "and the pass initialized again");
            Check.That(Static(scene, "Paint", "Setups") > setups, "its graph set up again");
            Check.That(ReferenceEquals(app.FindScene("Tick"), scene), "the scene still the same one");

            Edit("Initialized += 1;", "Initialized += 10;");
            Check.Equal(ReloadKind.ScenesReopened, host.LastReload, "a scene's Initialize edit reopens it");
            Check.That(!ReferenceEquals(app.FindScene("Tick"), scene), "a new scene");
            Check.Equal(11, Static(app.FindScene("Tick")!, "Tick", "Initialized"), "initialized by the new code");
            Check.Equal(302, Field(app, "Frames", "Tick"), "with its state kept");

            Edit("[Keep] public int Frames;", "[Keep] public int Frames;\n    public int Added = 5;");
            Check.Equal(ReloadKind.Full, host.LastReload, "a new field is a new build");
            Check.Equal(5, Field(app, "Added", "Tick"), "whose code runs");
            Check.Equal(302, Field(app, "Frames", "Tick"), "with the state kept");

            File.WriteAllText(script, File.ReadAllText(script).Replace("Frames += 100;", "Frames += ;"));
            Check.That(!host.Reload(), "a broken edit applies nothing");
            Frames(app, 1);
            Check.Equal(402, Field(app, "Frames", "Tick"), "and the last good code runs on");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool Unloads(ScriptHost host)
    {
        for (var i = 0; i < 20 && host.PreviousContext is { IsAlive: true }; ++i)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        return host.PreviousContext is { IsAlive: false };
    }

    /// <summary>Poll notices an edit once it has settled, as the runner uses it.</summary>
    public static void PollNoticesEdits()
    {
        var directory = Directory.CreateTempSubdirectory("koral-poll-");
        try
        {
            var script = Path.Combine(directory.FullName, "Counter.cs");
            File.WriteAllText(script, ScriptV1);
            using var app = Check.HeadlessApp();
            using var host = new ScriptHost(app, directory.FullName)
            {
                PollInterval = TimeSpan.Zero,
                SettleTime = TimeSpan.FromMilliseconds(50),
            };
            host.Load();
            app.OpenOffscreen("Counter", Offscreen(8));
            Frames(app, 1);
            Check.That(!host.Poll(), "nothing changed");

            File.WriteAllText(script, ScriptV2);
            File.SetLastWriteTimeUtc(script, DateTime.UtcNow.AddSeconds(1));
            var reloaded = false;
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (!reloaded && DateTime.UtcNow < deadline)
            {
                app.Frame();
                reloaded = host.Poll();
                Thread.Sleep(10);
            }
            Check.That(reloaded, "the edit was noticed and reloaded");
            Check.Equal(2, host.Generation, "a second build");
            Check.Equal(2, Field(app, "Version"), "running the new code");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }
}
