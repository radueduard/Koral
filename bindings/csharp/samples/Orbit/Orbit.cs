// A scene in C#, run by koral-dotnet: `koral-dotnet samples/Orbit --hot-reload`, then edit this file while
// it runs — the change is compiled and the scene reopened, with its [Keep] state.

/// <summary>Clears the screen: the passes after it draw over a known colour.</summary>
public sealed class Clear(Vector4 color) : RenderPass("Clear")
{
    private Image? _screen;

    public override void Setup(PassBuilder builder) => builder.Write(FrameGraph.Screen, Image.Usage.eTransferDst);
    public override void Initialize(PassResources resources) => _screen = resources.ImageNamed(FrameGraph.Screen);
    public override void Record(CommandBuffer commandBuffer) => commandBuffer.ClearColorImage(_screen!, color);
}

public sealed class Orbit : Scene
{
    [Keep] private float _angle;
    [Keep] private Vector3 _position;

    protected override void Initialize()
    {
        Input.BindAxis("MoveX", Key.eD, new InputSource(Key.eA, -1), GamepadAxis.eLeftX);
        Input.BindAxis("MoveZ", Key.eS, new InputSource(Key.eW, -1), GamepadAxis.eLeftY);
        Input.BindAction("Quit", Key.eEsc, GamepadButton.eBack);

        Graph.Add(new Clear(new Vector4(0.05f, 0.06f, 0.09f, 1f)));
        Graph.Add(new DebugDrawPass(SceneDebug, Camera));
    }

    private Matrix4x4 Camera()
    {
        var view = Matrix4x4.CreateLookAt(new Vector3(0, 4, 8), Vector3.Zero, Vector3.UnitY);
        var extent = Window.Extent;
        var projection = Matrix4x4.CreatePerspectiveFieldOfView(MathF.PI / 3, (float)extent.X / Math.Max(extent.Y, 1u), 0.1f, 100f);
        projection.M22 *= -1;   // Vulkan's Y points down
        return view * projection;
    }

    protected override void Update()
    {
        if (Input.IsActionPressed("Quit")) Navigator.Quit();

        var move = Input.Axis2D("MoveX", "MoveZ");
        _position += new Vector3(move.X, 0, move.Y) * 3f * Time.FrameTime;
        _angle += Time.FrameTime;

        var orbit = new Vector3(MathF.Cos(_angle), 0, MathF.Sin(_angle)) * 2f;
        Debug.Box(_position - Vector3.One * 0.5f, _position + Vector3.One * 0.5f, new DebugStyle { Color = new Vector4(0.3f, 0.8f, 1f, 1f) });
        Debug.Sphere(_position + orbit, 0.25f, new DebugStyle { Color = new Vector4(1f, 0.7f, 0.2f, 1f) });
        Debug.Arrow(_position, _position + orbit, new DebugStyle { Color = new Vector4(1f, 1f, 1f, 0.5f) });
        Debug.Grid(new Vector3(0, -0.5f, 0), 10f, 10, new DebugStyle { Color = new Vector4(0.25f, 0.25f, 0.3f, 1f) });
    }
}
