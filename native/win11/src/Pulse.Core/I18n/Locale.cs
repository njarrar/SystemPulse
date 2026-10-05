using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace Pulse.Core.I18n;

/// <summary>
/// A loaded locale with its fallback chain. UI code asks for keys and never
/// branches on a language code: direction, plurals and digits come from the file.
/// Port of i18n/pulse-i18n.js.
/// </summary>
public sealed class Locale
{
    public const char FSI = '⁨', PDI = '⁩';
    static readonly Regex Placeholder = new(@"\{(\w+)\}", RegexOptions.CultureInvariant);

    readonly HashSet<string> _warned = new(StringComparer.Ordinal);

    public LocaleData Data { get; }
    public Locale? Fallback { get; }
    public string Code => Data.Code;
    public string Label => Data.Label ?? Data.Code.ToUpperInvariant();
    public string Name => Data.Name ?? Data.Code;
    public string Dir => Data.IsRtl ? "rtl" : "ltr";
    public bool Rtl => Data.IsRtl;
    public string NumberingSystem => Data.NumberingSystem ?? "latn";
    public PluralSelector Selector { get; }

    /// <summary>Called once per missing key (section.key, locale code).</summary>
    public static Action<string, string>? MissingKey { get; set; }

    public Locale(LocaleData data, Locale? fallback = null)
    {
        Data = data;
        Fallback = fallback is not null && !ReferenceEquals(fallback.Data, data) ? fallback : null;
        Selector = new PluralSelector(data.PluralRules);
    }

    public static bool HasRtlChars(string s)
    {
        foreach (char c in s)
            if ((c >= '֐' && c <= 'ࣿ') || (c >= 'יִ' && c <= '﷿') || (c >= 'ﹰ' && c <= '﻿')) return true;
        return false;
    }

    public static string Isolate(string s) => FSI + s + PDI;

    /// <summary>Removes bidi isolates and embeddings (U+2066..U+2069), for tests and plain logs.</summary>
    public static string StripIsolates(string s)
    {
        var sb = new StringBuilder(s.Length);
        foreach (char c in s) if (c < '⁦' || c > '⁩') sb.Append(c);
        return sb.ToString();
    }

    public string Select(double n, int? decimals = null) => Selector.Select(n, decimals);

    string? Lookup(string section, string key)
    {
        var sec = Data.Section(section);
        if (sec is not null && sec.TryGetValue(key, out var v)) return v;
        return Fallback?.Lookup(section, key);
    }

    string Miss(string section, string key)
    {
        if (_warned.Add(section + "." + key)) MissingKey?.Invoke(section + "." + key, Code);
        return key;
    }

    /// <summary>Formats a number in this locale's numbering system with fixed decimals.</summary>
    public string Num(double x, int decimals = 0) => NumberShaper.Format(x, decimals, NumberingSystem, Code);

    /// <summary>
    /// Replaces {name} with the matching argument. Inserted values are wrapped
    /// in bidi isolates when the locale is RTL or the value holds RTL text, so
    /// numbers with units and Latin names never get reordered.
    /// </summary>
    public string Format(string? template, IReadOnlyDictionary<string, object?>? args)
    {
        if (template is null) return "";
        return Placeholder.Replace(template, m =>
        {
            string name = m.Groups[1].Value;
            if (args is null || !args.TryGetValue(name, out var raw)) return m.Value;
            string v = raw switch
            {
                double d => Num(d),
                float f => Num(f),
                int i => Num(i),
                long l => Num(l),
                null => "null",
                _ => Convert.ToString(raw, CultureInfo.InvariantCulture) ?? ""
            };
            return Rtl || HasRtlChars(v) ? Isolate(v) : v;
        });
    }

    public string T(string key, IReadOnlyDictionary<string, object?>? args = null) =>
        Format(Lookup("strings", key) ?? Miss("strings", key), args);

    public string T(string key, params (string Name, object? Value)[] args) => T(key, Args(args));

    public string Hw(string key, IReadOnlyDictionary<string, object?>? args = null) =>
        Format(Lookup("hardware", key) ?? Miss("hardware", key), args);

    public string Hw(string key, params (string Name, object? Value)[] args) => Hw(key, Args(args));

    public bool Has(string section, string key) => Lookup(section, key) is not null;

    /// <summary>
    /// Picks the plural form for <paramref name="n"/>. Exact forms ("=0") win over
    /// CLDR categories; a missing form falls back to "other", then to the fallback locale.
    /// </summary>
    public string Plural(string key, double n, IReadOnlyDictionary<string, object?>? args = null, int? decimals = null)
    {
        if (!Data.Plurals.TryGetValue(key, out var forms))
            return Fallback is not null ? Fallback.Plural(key, n, args, decimals) : Miss("plurals", key);
        string cat = Selector.Select(n, decimals);
        string? tpl = forms.TryGetValue("=" + PluralOperands.JsNumberString(n), out var exact) ? exact
            : forms.TryGetValue(cat, out var byCat) ? byCat
            : forms.GetValueOrDefault("other");
        var p = new Dictionary<string, object?>(StringComparer.Ordinal) { ["n"] = decimals is int d ? Num(n, d) : Num(n) };
        if (args is not null) foreach (var kv in args) p[kv.Key] = kv.Value;
        return Format(tpl, p);
    }

    /// <summary>Joins items with "listSep". In RTL locales each item is isolated.</summary>
    public string Join(IEnumerable<string?> items)
    {
        string sep = Lookup("strings", "listSep") ?? " · ";
        return string.Join(sep, items.Where(x => !string.IsNullOrEmpty(x)).Select(x => Rtl ? Isolate(x!) : x!));
    }

    public string Join(params string?[] items) => Join((IEnumerable<string?>)items);

    public string App(string name) => Data.Apps.TryGetValue(name, out var v) && v.Length > 0 ? v : name;

    public (string? Ui, string? Hero) Fonts(string platform) =>
        Data.Fonts?.For(platform) ?? (null, null);

    static Dictionary<string, object?> Args((string Name, object? Value)[] args)
    {
        var d = new Dictionary<string, object?>(args.Length, StringComparer.Ordinal);
        foreach (var (n, v) in args) d[n] = v;
        return d;
    }
}
