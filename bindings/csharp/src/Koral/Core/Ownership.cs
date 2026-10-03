namespace Koral;

/// <summary>
/// Who frees what another assembly made: the same rule Koral's own resources follow. Made in a scene's
/// constructor or any of its hooks, it is that scene's, disposed when the scene goes; made anywhere else,
/// it is the application's, disposed before the device goes.
/// </summary>
/// <remarks>
/// For a binding of a module (Koral.UI's interfaces, say): call <see cref="Adopt"/> as the object is
/// made, and <see cref="Release"/> when it is disposed early.
/// </remarks>
public static class Ownership
{
    /// <summary>Hands <paramref name="disposable"/> to its owner; returns a token for <see cref="Release"/>.</summary>
    public static object? Adopt(IDisposable disposable)
    {
        if (SceneBridge.ConstructingOthers is { } constructing)
        {
            constructing.Add(disposable);
            return new PendingOwner(constructing);
        }
        IResourceOwner? owner = Scene.Current is { } scene && scene.IsManaged ? scene : App.Current;
        owner?.Own(disposable);
        return owner;
    }

    /// <summary>It was disposed on its own: its owner no longer needs to.</summary>
    public static void Release(object? owner, IDisposable disposable)
    {
        switch (owner)
        {
            case IResourceOwner o: o.Disown(disposable); break;
            case PendingOwner p: p.List.Remove(disposable); break;
        }
    }

    private sealed record PendingOwner(List<IDisposable> List);
}

/// <summary>
/// What code replaced while it runs means for the libraries built on Koral. A script host calls
/// <see cref="NotifyUpdated"/> after applying edits in place; a library says which of its types it takes
/// care of itself (<see cref="Claim"/>) — so an edit to one of their constructors does not reopen the
/// scenes — and reacts in <see cref="Updated"/>.
/// </summary>
public static class HotReload
{
    private static readonly List<Func<Type, bool>> Claims = [];

    /// <summary>After code was replaced in place, on the application's thread, with the types whose methods changed.</summary>
    public static event Action<IReadOnlyList<Type>>? Updated;

    /// <summary>Says <paramref name="handles"/>'s types are taken care of: made again by their library, not by reopening scenes.</summary>
    public static void Claim(Func<Type, bool> handles)
    {
        lock (Claims) Claims.Add(handles);
    }

    /// <summary>Whether a library takes care of <paramref name="type"/> itself.</summary>
    public static bool IsClaimed(Type type)
    {
        lock (Claims) return Claims.Any(c => c(type));
    }

    /// <summary>For a script host: code was replaced in place. Each handler's failure is logged, not thrown.</summary>
    public static void NotifyUpdated(IReadOnlyList<Type> types)
    {
        foreach (var handler in Updated?.GetInvocationList() ?? [])
        {
            try { ((Action<IReadOnlyList<Type>>)handler)(types); }
            catch (Exception e) { Log.Error($"[koral] a hot-reload handler threw {e}"); }
        }
    }
}
