using System.Text.Json;

namespace Pulse.Core.I18n;

/// <summary>Font stacks a locale asks for, with per-platform overrides.</summary>
public sealed class LocaleFonts
{
    public string? Ui { get; set; }
    public string? Hero { get; set; }
    public Dictionary<string, (string? Ui, string? Hero)> Platforms { get; } = new(StringComparer.Ordinal);

    public (string? Ui, string? Hero) For(string platform)
    {
        Platforms.TryGetValue(platform, out var o);
        return (o.Ui ?? Ui, o.Hero ?? Hero);
    }
}

/// <summary>
/// One locale file (locales/&lt;code&gt;.json) as data. The same shape comes back
/// from the generated .resw catalog, so the engine does not care which it got.
/// </summary>
public sealed class LocaleData
{
    public required string Code { get; init; }
    public string? Label { get; set; }
    public string? Name { get; set; }
    public string? Dir { get; set; }
    public string? NumberingSystem { get; set; }
    public string? Fallback { get; set; }
    public int? Order { get; set; }

    /// <summary>CLDR rule text per category. Null when the file leaves pluralRules out.</summary>
    public Dictionary<string, string>? PluralRules { get; set; }

    /// <summary>plural key, then form ("one", "few", "=0") to template.</summary>
    public Dictionary<string, Dictionary<string, string>> Plurals { get; } = new(StringComparer.Ordinal);
    public Dictionary<string, string> Strings { get; } = new(StringComparer.Ordinal);
    public Dictionary<string, string> Hardware { get; } = new(StringComparer.Ordinal);
    public Dictionary<string, string> Apps { get; } = new(StringComparer.Ordinal);
    public LocaleFonts? Fonts { get; set; }

    public bool IsRtl => Dir == "rtl";

    public Dictionary<string, string>? Section(string name) => name switch
    {
        "strings" => Strings,
        "hardware" => Hardware,
        "apps" => Apps,
        _ => null
    };

    // ---------------------------------------------------------------------
    // JSON
    // ---------------------------------------------------------------------

    public static LocaleData FromJsonFile(string path) => FromJson(File.ReadAllText(path));

    public static LocaleData FromJson(string json)
    {
        using var doc = JsonDocument.Parse(json, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
        var root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("code", out var codeEl) || codeEl.ValueKind != JsonValueKind.String)
            throw new FormatException("Locale file needs a \"code\"");

        var d = new LocaleData
        {
            Code = codeEl.GetString()!,
            Label = Str(root, "label"),
            Name = Str(root, "name"),
            Dir = Str(root, "dir"),
            NumberingSystem = Str(root, "numberingSystem"),
            Fallback = Str(root, "fallback"),
            Order = root.TryGetProperty("order", out var ord) && ord.ValueKind == JsonValueKind.Number ? ord.GetInt32() : null
        };

        if (root.TryGetProperty("pluralRules", out var pr) && pr.ValueKind == JsonValueKind.Object)
        {
            d.PluralRules = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var p in pr.EnumerateObject())
                if (p.Value.ValueKind == JsonValueKind.String) d.PluralRules[p.Name] = p.Value.GetString()!;
        }
        if (root.TryGetProperty("plurals", out var pl) && pl.ValueKind == JsonValueKind.Object)
        {
            foreach (var key in pl.EnumerateObject())
            {
                if (key.Value.ValueKind != JsonValueKind.Object) continue;
                var forms = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var f in key.Value.EnumerateObject())
                    if (f.Value.ValueKind == JsonValueKind.String) forms[f.Name] = f.Value.GetString()!;
                d.Plurals[key.Name] = forms;
            }
        }
        Fill(root, "strings", d.Strings);
        Fill(root, "hardware", d.Hardware);
        Fill(root, "apps", d.Apps);

        if (root.TryGetProperty("fonts", out var fo) && fo.ValueKind == JsonValueKind.Object)
        {
            var fonts = new LocaleFonts { Ui = Str(fo, "ui"), Hero = Str(fo, "hero") };
            if (fo.TryGetProperty("platforms", out var plats) && plats.ValueKind == JsonValueKind.Object)
                foreach (var p in plats.EnumerateObject())
                    if (p.Value.ValueKind == JsonValueKind.Object)
                        fonts.Platforms[p.Name] = (Str(p.Value, "ui"), Str(p.Value, "hero"));
            d.Fonts = fonts;
        }
        return d;
    }

    static string? Str(JsonElement e, string name) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    static void Fill(JsonElement root, string name, Dictionary<string, string> into)
    {
        if (!root.TryGetProperty(name, out var sec) || sec.ValueKind != JsonValueKind.Object) return;
        foreach (var p in sec.EnumerateObject())
            if (p.Value.ValueKind == JsonValueKind.String) into[p.Name] = p.Value.GetString()!;
    }
}
