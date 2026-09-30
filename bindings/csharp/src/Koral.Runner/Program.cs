// koral-dotnet <scenes> [flags]
//
// Runs a project whose scenes are C#: <scenes> is a directory of .cs files (compiled here) or a built
// .dll. The flags and koral.json are the runtime's own (--help lists them), found from the scenes'
// directory up; the scene to open is koral.json's "scene", --scene, or the first there is.
// With --hot-reload, an edit to a script is applied to the running code in place, when it only changed
// method bodies, and otherwise reopens its scenes (in their windows) with their state.

using System.Diagnostics;
using Koral;
using Koral.Scripting;

if (args.Length == 0 || args[0] is "--help" or "-h")
{
    Console.WriteLine("usage: koral-dotnet <scripts directory | assembly.dll> [flags]\n\nFlags:\n" + ProjectConfig.Usage);
    return args.Length == 0 ? 2 : 0;
}

// Replacing running code needs the runtime told so before it starts. Launched without it (the Hub, a
// terminal), the runner starts itself again with it; a debugger launch sets it in its own configuration,
// since a new process would leave the debugger behind.
if (args.Contains("--hot-reload") && Environment.GetEnvironmentVariable("DOTNET_MODIFIABLE_ASSEMBLIES") is null
    && !Debugger.IsAttached && Environment.ProcessPath is { } self)
{
    var restart = new ProcessStartInfo(self) { UseShellExecute = false };
    // Under `dotnet koral-dotnet.dll`, the process is dotnet itself: hand it the runner's assembly again.
    if (Path.GetFileNameWithoutExtension(self) == "dotnet") restart.ArgumentList.Add(Environment.GetCommandLineArgs()[0]);
    foreach (var a in args) restart.ArgumentList.Add(a);
    restart.Environment["DOTNET_MODIFIABLE_ASSEMBLIES"] = "debug";
    using var child = Process.Start(restart)!;
    child.WaitForExit();
    return child.ExitCode;
}

var source = Path.GetFullPath(args[0]);
var searchFrom = Directory.Exists(source) ? source : Path.GetDirectoryName(source)!;

try
{
    using var project = ProjectConfig.Load(args[1..], searchFrom);
    using var app = new App(project.AppSettings);
    using var scripts = new ScriptHost(app, source);
    try
    {
        scripts.Load();
    }
    catch (ScriptException e)
    {
        Console.Error.WriteLine(string.Join('\n', e.Errors));
        return 1;
    }

    var start = project.Scene is { Length: > 0 } named ? named : scripts.Scenes.FirstOrDefault();
    if (start is null)
    {
        Console.Error.WriteLine($"'{source}' has no scenes: a public class deriving from Koral.Scene.");
        return 1;
    }

    var window = project.WindowSettings;
    app.Open(start, window with { Title = window.Title.Length > 0 ? window.Title : start });
    if (!project.HotReload) return app.Run();

    Log.Info($"[koral-dotnet] hot reload: watching '{source}'" + (scripts.UpdatesInPlace ? " (edits applied in place)" : ""));
    while (app.Frame()) scripts.Poll();
    return 0;
}
catch (KoralException e)
{
    Console.Error.WriteLine(e.Message);
    return 1;
}
