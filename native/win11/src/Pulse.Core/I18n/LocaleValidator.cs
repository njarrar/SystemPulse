using System.Text.RegularExpressions;

namespace Pulse.Core.I18n;

/// <summary>Checks a locale against the base locale. Port of validate() in pulse-i18n.js.</summary>
public static class LocaleValidator
{
    static readonly Regex Placeholder = new(@"\{(\w+)\}", RegexOptions.CultureInvariant);
    static readonly Regex ExactForm = new(@"^=\d+$", RegexOptions.CultureInvariant);

    public sealed record Result(List<string> Errors, List<string> Warnings);

    public static Result Validate(LocaleData data, LocaleData? baseData)
    {
        var errors = new List<string>();
        var warnings = new List<string>();
        var cats = PluralRules.Categories;
        if (string.IsNullOrEmpty(data.Code)) errors.Add("missing \"code\"");
        if (data.Dir is not null && data.Dir != "ltr" && data.Dir != "rtl") errors.Add("\"dir\" must be \"ltr\" or \"rtl\"");
        if (data.PluralRules is not null)
        {
            foreach (var (c, rule) in data.PluralRules)
            {
                if (Array.IndexOf(cats, c) < 0 || c == "other") errors.Add($"pluralRules: bad category \"{c}\"");
                else
                {
                    try { PluralRules.Compile(rule); }
                    catch (FormatException e) { errors.Add($"pluralRules.{c}: {e.Message}"); }
                }
            }
        }
        var used = new List<string> { "other" };
        if (data.PluralRules is not null) used.AddRange(data.PluralRules.Keys);
        foreach (var (key, forms) in data.Plurals)
        {
            if (!forms.ContainsKey("other")) errors.Add($"plurals.{key}: needs an \"other\" form");
            foreach (var f in forms.Keys)
            {
                if (!ExactForm.IsMatch(f) && Array.IndexOf(cats, f) < 0) errors.Add($"plurals.{key}: unknown form \"{f}\"");
                else if (data.PluralRules is not null && Array.IndexOf(cats, f) >= 0 && !used.Contains(f))
                    warnings.Add($"plurals.{key}.{f}: this language never selects \"{f}\"");
            }
            if (data.PluralRules is not null)
                foreach (var c in used)
                    if (c != "other" && !forms.ContainsKey(c)) warnings.Add($"plurals.{key}: no \"{c}\" form, \"other\" is used");
        }
        if (baseData is not null)
        {
            foreach (var sec in new[] { "strings", "hardware", "plurals" })
            {
                IEnumerable<string> baseKeys = sec == "plurals" ? baseData.Plurals.Keys : baseData.Section(sec)!.Keys;
                IEnumerable<string> dataKeys = sec == "plurals" ? data.Plurals.Keys : data.Section(sec)!.Keys;
                var dataSet = new HashSet<string>(dataKeys, StringComparer.Ordinal);
                var baseSet = new HashSet<string>(baseKeys, StringComparer.Ordinal);
                foreach (var k in baseKeys)
                {
                    if (!dataSet.Contains(k)) { warnings.Add($"{sec}.{k}: missing, falls back to {baseData.Code}"); continue; }
                    if (sec == "plurals") continue;
                    var pb = Names(baseData.Section(sec)![k]);
                    var pd = Names(data.Section(sec)![k]);
                    foreach (var p in pb) if (!pd.Contains(p)) warnings.Add($"{sec}.{k}: drops {{{p}}}");
                    foreach (var p in pd) if (!pb.Contains(p)) errors.Add($"{sec}.{k}: unknown placeholder {{{p}}}");
                }
                foreach (var k in dataKeys) if (!baseSet.Contains(k)) warnings.Add($"{sec}.{k}: not in {baseData.Code}, never used");
            }
        }
        return new Result(errors, warnings);
    }

    static List<string> Names(string s) =>
        Placeholder.Matches(s).Select(m => m.Groups[1].Value).Distinct().ToList();
}
