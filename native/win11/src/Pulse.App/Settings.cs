using System.Text;
using System.Text.Json;

namespace Pulse.App;

public enum ThemeChoice { System, Light, Dark }

/// <summary>User choices, kept in %LOCALAPPDATA%\Pulse\settings.json. Written by hand so trimming keeps nothing extra.</summary>
public sealed class Settings
{
    public string? Language { get; set; }
    public ThemeChoice Theme { get; set; } = ThemeChoice.System;
    public bool Fahrenheit { get; set; }
    public bool LiveUpdates { get; set; } = true;

    static string FilePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Pulse", "settings.json");

    public static Settings Load()
    {
        var s = new Settings();
        try
        {
            if (!File.Exists(FilePath)) return s;
            using var doc = JsonDocument.Parse(File.ReadAllText(FilePath));
            var r = doc.RootElement;
            if (r.TryGetProperty("language", out var l) && l.ValueKind == JsonValueKind.String) s.Language = l.GetString();
            if (r.TryGetProperty("theme", out var t) && Enum.TryParse<ThemeChoice>(t.GetString(), true, out var th)) s.Theme = th;
            if (r.TryGetProperty("fahrenheit", out var f) && f.ValueKind is JsonValueKind.True or JsonValueKind.False) s.Fahrenheit = f.GetBoolean();
            if (r.TryGetProperty("liveUpdates", out var u) && u.ValueKind is JsonValueKind.True or JsonValueKind.False) s.LiveUpdates = u.GetBoolean();
        }
        catch (Exception) { }
        return s;
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            using var ms = new MemoryStream();
            using (var w = new Utf8JsonWriter(ms, new JsonWriterOptions { Indented = true }))
            {
                w.WriteStartObject();
                if (Language is not null) w.WriteString("language", Language);
                w.WriteString("theme", Theme.ToString().ToLowerInvariant());
                w.WriteBoolean("fahrenheit", Fahrenheit);
                w.WriteBoolean("liveUpdates", LiveUpdates);
                w.WriteEndObject();
            }
            File.WriteAllText(FilePath, Encoding.UTF8.GetString(ms.ToArray()));
        }
        catch (Exception) { }
    }
}
