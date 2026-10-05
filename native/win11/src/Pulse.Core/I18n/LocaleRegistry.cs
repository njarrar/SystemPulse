namespace Pulse.Core.I18n;

public sealed record LocaleInfo(string Code, string Label, string Name, string Dir);

/// <summary>
/// The set of registered locales. Fallback chains are built lazily: a locale
/// falls back to its "fallback" code, or to the base locale ("en").
/// </summary>
public sealed class LocaleRegistry
{
    readonly Dictionary<string, LocaleData> _data = new(StringComparer.Ordinal);
    readonly Dictionary<string, Locale> _built = new(StringComparer.Ordinal);

    public string BaseCode { get; }

    public LocaleRegistry(string baseCode = "en") => BaseCode = baseCode;

    public int Count => _data.Count;

    public string Register(LocaleData data)
    {
        _data[data.Code] = data;
        _built.Clear(); // rebuild fallback chains lazily
        return data.Code;
    }

    /// <summary>Registers every *.json in a folder except schema.json.</summary>
    public IReadOnlyList<string> LoadFolder(string dir)
    {
        var codes = new List<string>();
        foreach (var f in Directory.EnumerateFiles(dir, "*.json").OrderBy(x => x, StringComparer.Ordinal))
        {
            if (string.Equals(Path.GetFileName(f), "schema.json", StringComparison.OrdinalIgnoreCase)) continue;
            codes.Add(Register(LocaleData.FromJsonFile(f)));
        }
        return codes;
    }

    public Locale Get(string? code)
    {
        string? resolved = Resolve(code);
        if (resolved is null || !_data.TryGetValue(resolved, out var data))
            throw new KeyNotFoundException("No locale registered for " + code);
        if (_built.TryGetValue(resolved, out var built)) return built;
        string? fb = data.Fallback ?? (data.Code == BaseCode ? null : BaseCode);
        var fallback = fb is not null && fb != data.Code && _data.ContainsKey(fb) ? Get(fb) : null;
        var locale = new Locale(data, fallback);
        _built[resolved] = locale;
        return locale;
    }

    /// <summary>"ar-EG" resolves to "ar" when only "ar" exists; unknown codes resolve to the base locale.</summary>
    public string? Resolve(string? code)
    {
        if (code is not null && _data.ContainsKey(code)) return code;
        var parts = (code ?? "").Split('-', '_').ToList();
        while (parts.Count > 1)
        {
            parts.RemoveAt(parts.Count - 1);
            string c = string.Join("-", parts);
            if (_data.ContainsKey(c)) return c;
        }
        if (_data.ContainsKey(BaseCode)) return BaseCode;
        return _data.Keys.FirstOrDefault();
    }

    public IReadOnlyList<LocaleInfo> List() =>
        _data.Values
            .OrderBy(d => d.Order ?? (d.Code == BaseCode ? -1 : 0))
            .ThenBy(d => d.Code, StringComparer.Ordinal)
            .Select(d => new LocaleInfo(d.Code, d.Label ?? d.Code.ToUpperInvariant(), d.Name ?? d.Code, d.IsRtl ? "rtl" : "ltr"))
            .ToList();
}
