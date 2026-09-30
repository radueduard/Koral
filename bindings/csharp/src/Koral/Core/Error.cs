using Koral.Native;

namespace Koral;

/// <summary>
/// kor::Error: what went wrong — its code and message, and the whole chain of causes in
/// <see cref="History"/> (the shader that did not compile, under the pipeline that could not be built from it).
/// </summary>
public sealed class Error(ErrorCode code, string message, string history)
{
    public ErrorCode Code { get; } = code;
    public string Message { get; } = message;
    /// <summary>This error and every cause under it, as kor::Error::History prints them.</summary>
    public string History { get; } = history;

    /// <summary>kor::Describe(code).</summary>
    public static unsafe string Describe(ErrorCode code) => KoralNative.Text(KoralNative.koral_error_describe((uint)code));

    public override string ToString() => History.Length > 0 ? History : Message;
}

/// <summary>
/// Something Koral could not do. Where C++ returns a kor::Result, C# throws this; a builder never does —
/// it makes a poisoned resource, as in C++ (<see cref="Resource.Poisoned"/>).
/// </summary>
public sealed class KoralException(string message) : Exception(message);
