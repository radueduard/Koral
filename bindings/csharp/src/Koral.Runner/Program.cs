// koral-dotnet <scenes> [flags]
//
// Runs a project whose scenes are C#: <scenes> is a directory of .cs files (compiled here) or a built
// .dll. The flags and koral.json are the runtime's own (--help lists them), found from the scenes'
// directory up; the scene to open is koral.json's "scene", --scene, or the first there is.
// With --hot-reload, an edit to a script (or a rebuild of the .dll) reopens its scenes with their state.

using Koral;
using Koral.Scripting;

if (args.Length == 0 || args[0] is "--help" or "-h")
{
    Console.WriteLine("usage: koral-dotnet <scripts directory | assembly.dll> [flags]\n\nFlags:\n" + ProjectConfig.Usage);
    return args.Length == 0 ? 2 : 0;
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

    Log.Info($"[koral-dotnet] hot reload: watching '{source}'");
    while (app.Frame()) scripts.Poll();
    return 0;
}
catch (KoralException e)
{
    Console.Error.WriteLine(e.Message);
    return 1;
}
