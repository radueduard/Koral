using System.Diagnostics;
using System.Reflection;
using Koral.Tests;

// With a case's name: runs it. With none: runs every case, each in a process of its own.
var cases = typeof(Cases).GetMethods(BindingFlags.Public | BindingFlags.Static)
    .Where(m => m.ReturnType == typeof(void) && m.GetParameters().Length == 0)
    .ToDictionary(m => m.Name, m => m);

if (args.Length == 1)
{
    if (!cases.TryGetValue(args[0], out var method))
    {
        Console.Error.WriteLine($"no case '{args[0]}'. Known: {string.Join(", ", cases.Keys)}");
        return 2;
    }
    try
    {
        method.Invoke(null, null);
    }
    catch (TargetInvocationException e) when (e.InnerException is SkipException skip)
    {
        Console.WriteLine($"SKIPPED: {skip.Message}");
        return 0;
    }
    catch (TargetInvocationException e)
    {
        Console.Error.WriteLine($"FAILED: {e.InnerException}");
        return 1;
    }
    return Check.Failures == 0 ? 0 : 1;
}

var failed = new List<string>();
foreach (var name in cases.Keys)
{
    var start = new ProcessStartInfo(Environment.ProcessPath!) { RedirectStandardOutput = true, RedirectStandardError = true };
    // Under `dotnet Koral.Tests.dll`, the process is dotnet itself: hand it the assembly again.
    if (Path.GetFileNameWithoutExtension(Environment.ProcessPath!) == "dotnet") start.ArgumentList.Add(typeof(Cases).Assembly.Location);
    start.ArgumentList.Add(name);
    using var process = Process.Start(start)!;
    var output = process.StandardOutput.ReadToEndAsync();
    var error = process.StandardError.ReadToEndAsync();
    process.WaitForExit();
    var text = (await output + await error).Trim();
    var skipped = text.Contains("SKIPPED:");
    Console.WriteLine($"{(process.ExitCode != 0 ? "FAIL" : skipped ? "SKIP" : "ok  ")} {name}");
    if (process.ExitCode != 0)
    {
        failed.Add(name);
        // What the case said, and what went wrong in the log — not the device's life story.
        var said = text.Split('\n').Where(l => !l.Contains("] [info]") && !l.Contains("] [debug]"));
        Console.WriteLine(string.Join('\n', said.Select(l => "     " + l)));
    }
}
Console.WriteLine(failed.Count == 0 ? $"all {cases.Count} passed" : $"{failed.Count} of {cases.Count} failed: {string.Join(", ", failed)}");
return failed.Count == 0 ? 0 : 1;
