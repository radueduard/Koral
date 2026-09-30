using System.Runtime.CompilerServices;
using Koral.Native;

namespace Koral;

/// <summary>
/// kor::Token: something that will be done — GPU work submitted, a load finished — waited for with
/// <see cref="Wait"/>, or awaited, as C++ co_awaits it.
/// </summary>
/// <example>
/// <code>
/// protected override async void Initialize()
/// {
///     await CommandBuffer.SingleTimeCommand(cb => cb.GenerateMipmaps(_texture));
///     _ready = true;   // on the application's thread, with this scene current again
/// }
/// </code>
/// </example>
/// <remarks>
/// An await resumes on the application's thread, at the start of a frame, with the scene that awaited
/// current — what a kor::Task does around each resumption, so <see cref="Scene.Window"/> and the rest
/// still mean that scene.
/// </remarks>
public sealed unsafe class Token
{
    internal IntPtr Native { get; }

    internal Token(IntPtr native) => Native = native;

    ~Token() => KoralNative.koral_token_destroy(Native);

    /// <summary>Token::Create: one of one's own, signalled with <see cref="Signal"/>.</summary>
    public static Token Create() => new(KoralNative.koral_token_create());

    public bool Ready => KoralNative.koral_token_ready(Native).AsBool();
    public void Wait() => KoralNative.koral_token_wait(Native);
    public void Signal() => KoralNative.koral_token_signal(Native);
    public ulong Value => KoralNative.koral_token_value(Native);

    public Awaiter GetAwaiter() => new(this);

    /// <summary>What <c>await</c> uses.</summary>
    public readonly struct Awaiter(Token token) : INotifyCompletion
    {
        public bool IsCompleted => token.Ready;
        public void GetResult() { }
        public void OnCompleted(Action continuation) => MainThread.WhenReady(token, continuation);
    }
}

/// <summary>
/// What resumes on the application's thread, at the start of each frame: continuations waiting on tokens,
/// and whatever a scene's <see cref="SceneSynchronizationContext"/> was posted — each run with the scene it
/// belongs to current.
/// </summary>
internal static class MainThread
{
    private static readonly List<(Token? Token, Action Continuation, Scene? Scene)> Waiting = [];
    private static readonly object Lock = new();

    /// <summary>The scene an await began in: its context's, or the current one.</summary>
    private static Scene? CurrentScene() => SynchronizationContext.Current is SceneSynchronizationContext c ? c.Scene : Scene.Current;

    public static void WhenReady(Token token, Action continuation)
    {
        if (App.Current is null)
        {
            // No frames to resume in: wait on a worker, as a program with no application must.
            ThreadPool.QueueUserWorkItem(_ => { token.Wait(); continuation(); });
            return;
        }
        var scene = CurrentScene();
        lock (Lock) Waiting.Add((token, continuation, scene));
    }

    public static void Post(Scene? scene, Action action)
    {
        lock (Lock) Waiting.Add((null, action, scene));
    }

    /// <summary>Called by App.Frame: resumes whatever is ready.</summary>
    public static unsafe void Resume()
    {
        List<(Token?, Action, Scene?)> ready;
        lock (Lock)
        {
            if (Waiting.Count == 0) return;
            ready = Waiting.Where(w => w.Token is null || w.Token.Ready).ToList();
            Waiting.RemoveAll(w => w.Token is null || w.Token.Ready);
        }
        foreach (var (_, continuation, scene) in ready)
        {
            var scope = scene is not null && scene.IsOpen ? KoralNative.koral_scene_scope_enter(scene.Native) : IntPtr.Zero;
            var previous = SynchronizationContext.Current;
            SynchronizationContext.SetSynchronizationContext(scene?.Context ?? SceneSynchronizationContext.None);
            try
            {
                continuation();
            }
            catch (Exception e)
            {
                if (scene is not null) App.Report(scene, "an awaited continuation", e);
                else Log.Error($"[koral] an awaited continuation threw {e}");
            }
            finally
            {
                SynchronizationContext.SetSynchronizationContext(previous);
                if (scope != IntPtr.Zero) KoralNative.koral_scene_scope_exit(scope);
            }
        }
    }
}

/// <summary>
/// A scene's SynchronizationContext, current during its hooks: an <c>await</c> started in the scene — on a
/// <see cref="Token"/>, a Task, anything — resumes on the application's thread with the scene current, and
/// an exception from an <c>async void</c> hook is reported (App.HookFailed) instead of ending the process.
/// </summary>
public sealed class SceneSynchronizationContext : SynchronizationContext
{
    internal static readonly SceneSynchronizationContext None = new(null);

    internal SceneSynchronizationContext(Scene? scene) => Scene = scene;

    public Scene? Scene { get; }

    public override void Post(SendOrPostCallback d, object? state) => MainThread.Post(Scene, () => d(state));
    public override void Send(SendOrPostCallback d, object? state) => d(state);
    public override SynchronizationContext CreateCopy() => this;
}
