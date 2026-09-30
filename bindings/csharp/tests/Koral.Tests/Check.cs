using System.Runtime.CompilerServices;

namespace Koral.Tests;

public sealed class SkipException(string message) : Exception(message);

public static class Check
{
    public static int Failures { get; private set; }

    public static void That(bool condition, string what, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0)
    {
        if (condition) return;
        ++Failures;
        Console.Error.WriteLine($"FAILED: {what} ({Path.GetFileName(file)}:{line})");
    }

    public static void Equal<T>(T expected, T actual, string what, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0)
    {
        if (EqualityComparer<T>.Default.Equals(expected, actual)) return;
        ++Failures;
        Console.Error.WriteLine($"FAILED: {what}: expected {expected}, got {actual} ({Path.GetFileName(file)}:{line})");
    }

    public static TException Throws<TException>(Action action, string what, [CallerFilePath] string file = "", [CallerLineNumber] int line = 0)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException e)
        {
            return e;
        }
        catch (Exception e)
        {
            ++Failures;
            Console.Error.WriteLine($"FAILED: {what}: threw {e.GetType().Name} rather than {typeof(TException).Name}: {e.Message} ({Path.GetFileName(file)}:{line})");
            return null!;
        }
        ++Failures;
        Console.Error.WriteLine($"FAILED: {what}: did not throw ({Path.GetFileName(file)}:{line})");
        return null!;
    }

    /// <summary>An application with no windowing system — or a skip, on a machine with no Vulkan device.</summary>
    public static App HeadlessApp()
    {
        try
        {
            return new App(new AppSettings { Platform = WindowPlatform.eNone });
        }
        catch (KoralException e)
        {
            throw new SkipException($"no Vulkan device: {e.Message}");
        }
    }
}
