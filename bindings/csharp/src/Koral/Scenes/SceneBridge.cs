using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

/// <summary>
/// A C# scene as the table of callbacks Koral calls. Each scene is kept alive by a GCHandle handed to Koral
/// as the callbacks' user data, and freed by <c>destroy</c> — which is also what lets a reloaded script's
/// assembly be unloaded: once its scenes are destroyed, nothing native points into it.
/// </summary>
internal static unsafe class SceneBridge
{
    private static readonly ConcurrentDictionary<IntPtr, Scene> Live = new();

    /// <summary>The scene the last factory made on this thread, for Open to give its pointer.</summary>
    [ThreadStatic] internal static Scene? LastMade;

    /// <summary>Resources made while a scene's constructor runs: its, once it exists.</summary>
    [ThreadStatic] internal static List<Resource>? Constructing;

    [ThreadStatic] private static IntPtr t_state;

    /// <summary>The C# object for a kor::Scene*: the C# scene itself, or a face on one of Koral's own.</summary>
    internal static Scene? Of(IntPtr native)
    {
        if (native == IntPtr.Zero) return null;
        if (Live.TryGetValue(native, out var scene) && scene.IsOpen) return scene;
        var foreign = new ForeignScene();
        Adopt(foreign, native);
        return foreign;
    }

    internal static void Adopt(Scene scene, IntPtr native)
    {
        if (scene.Native == native && scene.Life != IntPtr.Zero) return;
        if (scene.Native != IntPtr.Zero) Live.TryRemove(new KeyValuePair<IntPtr, Scene>(scene.Native, scene));
        scene.ForgetNative();
        scene.Native = native;
        scene.Life = KoralNative.koral_scene_life(native);
        Live[native] = scene;
    }

    internal static KoralSceneCallbacks CallbacksFor(Scene scene) => new()
    {
        user = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(scene)),
        initialize = &Initialize,
        fixed_update = &FixedUpdate,
        update = &Update,
        late_update = &LateUpdate,
        render = &Render,
        on_resize = &OnResize,
        on_suspend = &OnSuspend,
        on_resume = &OnResume,
        on_close_requested = &OnCloseRequested,
        shutdown = &Shutdown,
        save_state = &SaveState,
        load_state = &LoadState,
        destroy = &Destroy,
    };

    /// <summary>The scene a hook runs in: its pointer known, and its context current.</summary>
    private static Scene Enter(IntPtr native, void* user, out SynchronizationContext? previous)
    {
        var scene = (Scene)GCHandle.FromIntPtr((IntPtr)user).Target!;
        Adopt(scene, native);
        previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(scene.Context);
        return scene;
    }

    private static Scene Of(void* user) => (Scene)GCHandle.FromIntPtr((IntPtr)user).Target!;

    // No exception may leave a callback — it would cross into C++ and end the process — so each is
    // reported (App.HookFailed) and the frame goes on.

    private static void Run(IntPtr native, void* user, string hook, Action<Scene> body)
    {
        var scene = Enter(native, user, out var previous);
        try { body(scene); }
        catch (Exception e) { App.Report(scene, hook, e); }
        finally { SynchronizationContext.SetSynchronizationContext(previous); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Initialize(IntPtr native, void* user) => Run(native, user, nameof(Initialize), s => s.Initialize());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void FixedUpdate(IntPtr native, void* user) => Run(native, user, nameof(FixedUpdate), s => s.FixedUpdate());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Update(IntPtr native, void* user) => Run(native, user, nameof(Update), s => s.Update());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void LateUpdate(IntPtr native, void* user) => Run(native, user, nameof(LateUpdate), s => s.LateUpdate());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Render(IntPtr native, IntPtr commands, void* user) =>
        Run(native, user, nameof(Render), s => s.Render(new CommandBuffer(commands)));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnResize(IntPtr native, uint width, uint height, void* user) =>
        Run(native, user, nameof(OnResize), s => s.OnResize(new UVec2(width, height)));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnSuspend(IntPtr native, void* user) => Run(native, user, nameof(OnSuspend), s => s.OnSuspend());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnResume(IntPtr native, void* user) => Run(native, user, nameof(OnResume), s => s.OnResume());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static byte OnCloseRequested(IntPtr native, void* user)
    {
        var close = true;
        Run(native, user, nameof(OnCloseRequested), s => close = s.OnCloseRequested());
        return KoralNative.Bool(close);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Shutdown(IntPtr native, void* user) => Run(native, user, nameof(Shutdown), s => s.Shutdown());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static byte* SaveState(void* user)
    {
        var scene = Of(user);
        string json;
        try { json = scene.SaveState(); }
        catch (Exception e) { App.Report(scene, nameof(SaveState), e); json = "null"; }
        // Koral copies it at once; it is freed on the next save on this thread.
        Marshal.FreeCoTaskMem(t_state);
        t_state = Marshal.StringToCoTaskMemUTF8(json);
        return (byte*)t_state;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void LoadState(byte* json, void* user)
    {
        var scene = Of(user);
        try { scene.LoadState(KoralNative.Text(json)); }
        catch (Exception e) { App.Report(scene, nameof(LoadState), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Destroy(void* user)
    {
        var handle = GCHandle.FromIntPtr((IntPtr)user);
        var scene = (Scene)handle.Target!;
        try
        {
            if (scene.Native != IntPtr.Zero) Live.TryRemove(new KeyValuePair<IntPtr, Scene>(scene.Native, scene));
            scene.ForgetNative();
            scene.ReleaseResources();
            (scene as IDisposable)?.Dispose();
        }
        catch (Exception e) { App.Report(scene, "Dispose", e); }
        finally { handle.Free(); }
    }
}
