# A first program

A window with a triangle in it: a scene that builds a pipeline when it starts and draws with it every frame.

```
hello/
├── CMakeLists.txt
├── main.cpp
└── shaders/
    └── triangle.slang
```

`CMakeLists.txt` is the one in [Building the SDK](building.md#using-the-sdk).

## The shader

`shaders/triangle.slang` makes its three corners out of the number of the vertex, so there is no mesh to make:

```slang
struct VertexOut {
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOut vertexMain(uint id : SV_VulkanVertexID)
{
    const float2 corners[3] = { float2(0.0, -0.6), float2(0.6, 0.6), float2(-0.6, 0.6) };
    const float3 colors[3] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
    VertexOut output;
    output.position = float4(corners[id], 0.0, 1.0);
    output.color = colors[id];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target
{
    return float4(input.color, 1.0);
}
```

## The program

```cpp
#include <app.h>
#include <koral.h>

class Hello final : public kor::Scene {
public:
    explicit Hello(const kor::SceneArgs&) {}

    void Initialize() override {
        const auto vertex = kor::Shader::Builder{}.SetEntryPoint("triangle", "vertexMain").GetOrBuild();
        const auto fragment = kor::Shader::Builder{}.SetEntryPoint("triangle", "fragmentMain").GetOrBuild();
        _pipeline = kor::GraphicsPipeline::Builder{}
            .SetVertexShader(vertex)
            .SetFragmentShader(fragment)
            .SetFramebuffer(Window::DefaultFramebuffer())
            .Build();
    }

    void Render(kor::CommandBuffer& cb) override {
        cb.BeginRendering()                 // the window's own framebuffer, cleared
          .BindGraphicsPipeline(_pipeline)
          .Draw(3)
          .EndRendering();
    }

private:
    kor::Resource<kor::GraphicsPipeline> _pipeline;
};

int main() {
    kor::Shader::AddSearchPath("shaders", true);
    kor::App app;
    app.Register<Hello>("Hello");
    app.Open("Hello", {.title = "Hello, Koral"});
    return app.Run();
}
```

Build it and run it from the folder `shaders/` is in:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Koral>/stage/release
cmake --build build
./build/hello
```

## What happened

- **`kor::App`** made the device and runs the frames. `Register` named the scene and `Open` opened it in a window;
  `Run` returns when the last window closes. See [Scenes and the application](../scenes.md).
- **`Initialize`** ran once, with the window there: where a scene makes its resources. `Window::` is this scene's
  window; its default framebuffer is the swap chain the window presents.
- **The shaders** were found by module name on the search path, compiled, and reflected: the pipeline knows their
  inputs and outputs without being told. `GetOrBuild` keeps one per source and entry point, and a pipeline made from
  them rebuilds when the file is saved. See [Shaders, pipelines and GPU features](../pipelines.md).
- **`Build()`** returned a `kor::Resource`, which is either valid or carries the error that says why not. A broken
  shader does not throw: the pipeline is poisoned and says so. See [Errors](../errors.md).
- **`Render`** recorded the frame's own work into the frame's command buffer. `BeginRendering()` with nothing given
  draws into the window, cleared; viewport and scissor default to its size. See [Command buffers](../commands.md).

## Where to go from here

- A mesh and a camera: [Buffers, images and meshes](../resources.md) and [Modules, the camera and debug drawing](../modules.md).
- Passes that order themselves: [The frame graph](../frame-graph.md).
- Keys and gamepads: [Input](../input.md). An interface: [Interfaces with koral-ui](../ui.md).
- The same program in C# or Kotlin: [C#](../csharp.md), [Kotlin and Compose](../kotlin.md).
- A project run by Koral's runtime instead of a `main` of its own — its scenes in a library, `koral.json` saying
  where its assets and shaders are: [Scene libraries](../scenes.md#scene-libraries) and
  [Configuring a project](../configuration.md).
