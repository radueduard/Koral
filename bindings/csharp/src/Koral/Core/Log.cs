using Koral.Native;

namespace Koral;

/// <summary>kor::log: the console, and what kgui::LogPanel shows. An interpolated string is the format.</summary>
public static partial class Log
{
    public static void Info(string message) => KoralNative.koral_log((int)Level.eInfo, message);
    public static void Warn(string message) => KoralNative.koral_log((int)Level.eWarn, message);
    public static void Error(string message) => KoralNative.koral_log((int)Level.eError, message);
}
