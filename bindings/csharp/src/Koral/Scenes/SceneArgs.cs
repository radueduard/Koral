using System.Globalization;
using System.Text;
using System.Text.Json;

namespace Koral;

/// <summary>
/// What a scene is opened with: named strings, read back as what they are — so a scene can be opened
/// by name from a menu, a config file or another language. The same as kor::SceneArgs.
/// </summary>
public sealed class SceneArgs
{
    private readonly Dictionary<string, string> _values = new();

    public SceneArgs() { }
    public SceneArgs(IEnumerable<KeyValuePair<string, string>> values)
    {
        foreach (var (key, value) in values) _values[key] = value;
    }

    public IReadOnlyDictionary<string, string> Values => _values;

    public SceneArgs Set(string key, string value) { _values[key] = value; return this; }
    public SceneArgs Set(string key, double value) => Set(key, value.ToString(CultureInfo.InvariantCulture));
    public SceneArgs Set(string key, long value) => Set(key, value.ToString(CultureInfo.InvariantCulture));
    public SceneArgs Set(string key, bool value) => Set(key, value ? "true" : "false");

    public bool Has(string key) => _values.ContainsKey(key);
    public string String(string key, string fallback = "") => _values.GetValueOrDefault(key, fallback);

    public double Number(string key, double fallback = 0) =>
        _values.TryGetValue(key, out var text) && double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value)
            ? value : fallback;

    public long Integer(string key, long fallback = 0) =>
        _values.TryGetValue(key, out var text) && double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value)
            ? (long)value : fallback;

    /// <summary>True for "true", "yes", "on" and "1"; <paramref name="fallback"/> when absent.</summary>
    public bool Flag(string key, bool fallback = false) =>
        _values.TryGetValue(key, out var text)
            ? text.Trim().ToLowerInvariant() is "true" or "yes" or "on" or "1"
            : fallback;

    /// <summary>As the C interface takes them: a JSON object of strings.</summary>
    public string ToJson()
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            foreach (var (key, value) in _values) writer.WriteString(key, value);
            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray());
    }

    /// <summary>From the C interface's JSON object: strings, numbers and booleans, each as text.</summary>
    public static SceneArgs FromJson(string? json)
    {
        var args = new SceneArgs();
        if (string.IsNullOrWhiteSpace(json)) return args;
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object) return args;
        foreach (var property in document.RootElement.EnumerateObject())
        {
            args._values[property.Name] = property.Value.ValueKind switch
            {
                JsonValueKind.String => property.Value.GetString() ?? "",
                JsonValueKind.True => "true",
                JsonValueKind.False => "false",
                _ => property.Value.GetRawText(),
            };
        }
        return args;
    }

    internal static string? JsonOf(SceneArgs? args) => args is null || args._values.Count == 0 ? null : args.ToJson();
}
