using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Koral;

/// <summary>A scene's <see cref="KeepAttribute"/> members, as a JSON object by name.</summary>
internal static class KeptState
{
    private const BindingFlags Instance = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

    // A new one each time, never cached: options remember every type they have written, and a type
    // from a reloaded script held here would keep its whole assembly from being unloaded.
    private static JsonSerializerOptions Options() => new()
    {
        IncludeFields = true,
        NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };

    // What is kept: the object State() returns — all of its public fields and properties — or else the
    // scene's own [Keep] members.
    private static IEnumerable<(string Name, Type Type, Func<object?> Get, Action<object?> Set)> Members(Scene scene)
    {
        if (scene.State() is { } state)
        {
            foreach (var field in state.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public))
                if (!field.IsInitOnly) yield return (field.Name, field.FieldType, () => field.GetValue(state), v => field.SetValue(state, v));
            foreach (var property in state.GetType().GetProperties(BindingFlags.Instance | BindingFlags.Public))
                if (property is { CanRead: true, CanWrite: true } && property.GetIndexParameters().Length == 0)
                    yield return (property.Name, property.PropertyType, () => property.GetValue(state), v => property.SetValue(state, v));
            yield break;
        }
        foreach (var member in KeptMembers(scene)) yield return member;
    }

    private static IEnumerable<(string Name, Type Type, Func<object?> Get, Action<object?> Set)> KeptMembers(object owner)
    {
        for (var type = owner.GetType(); type is not null && type != typeof(Scene); type = type.BaseType)
        {
            foreach (var field in type.GetFields(Instance | BindingFlags.DeclaredOnly))
                if (field.IsDefined(typeof(KeepAttribute)))
                    yield return (NameOf(field.Name), field.FieldType, () => field.GetValue(owner), v => field.SetValue(owner, v));
            foreach (var property in type.GetProperties(Instance | BindingFlags.DeclaredOnly))
                if (property.IsDefined(typeof(KeepAttribute)) && property is { CanRead: true, CanWrite: true })
                    yield return (property.Name, property.PropertyType, () => property.GetValue(owner), v => property.SetValue(owner, v));
        }
    }

    // `_score` and `score` are one thing to whoever reads the JSON: the underscore is a C# habit.
    private static string NameOf(string field) => field.StartsWith('_') && field.Length > 1 ? field[1..] : field;

    public static string Save(Scene owner)
    {
        var options = Options();
        var root = new JsonObject();
        foreach (var member in Members(owner))
            root[member.Name] = JsonSerializer.SerializeToNode(member.Get(), member.Type, options);
        return root.Count == 0 ? "null" : root.ToJsonString();
    }

    public static void Load(Scene owner, string json)
    {
        if (string.IsNullOrWhiteSpace(json) || json == "null") return;
        if (JsonNode.Parse(json) is not JsonObject root) return;
        var options = Options();
        foreach (var member in Members(owner))
        {
            if (!root.TryGetPropertyValue(member.Name, out var node)) continue;
            try
            {
                member.Set(node.Deserialize(member.Type, options));
            }
            catch (Exception e) when (e is JsonException or NotSupportedException or InvalidOperationException)
            {
                // Its type changed shape in the rebuild: it starts over, the rest is kept.
                Log.Warn($"[{owner.GetType().Name}] '{member.Name}' was not kept: {e.Message}");
            }
        }
    }
}
