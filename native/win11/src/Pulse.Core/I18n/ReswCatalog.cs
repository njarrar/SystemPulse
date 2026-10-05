using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace Pulse.Core.I18n;

/// <summary>
/// Maps a locale file to flat .resw entries and back.
///
/// .resw has no plurals, no metadata and no nesting, and MRT falls back to the
/// default language key by key. So the generator writes:
/// <list type="bullet">
/// <item><c>meta_*</c>: code, label, name, dir, numbering system, fallback, order,
///   the list of every generated locale, and the Windows font stacks.</item>
/// <item><c>meta_rules</c>: the plural categories this file declares ("none" when
///   it has no pluralRules), and <c>rule_&lt;category&gt;</c> with the CLDR text.</item>
/// <item><c>forms_&lt;key&gt;</c>: the forms this file gives a plural key, and
///   <c>p_&lt;key&gt;_&lt;form&gt;</c> with each template ("=0" is written "eq0").</item>
/// <item><c>s_</c>, <c>h_</c>, <c>a_</c>: strings, hardware and app names.</item>
/// </list>
/// The forms and rules lists let the reader drop values that MRT filled in
/// from English, so an English "=0" form never leaks into Arabic.
/// Characters outside [A-Za-z0-9] in keys are written as _HHHH_.
/// </summary>
public static class ReswCatalog
{
    public const string Platform = "win";
    static readonly Regex Escaped = new("_([0-9A-F]{4})_", RegexOptions.CultureInvariant);

    public static string EscapeKey(string key)
    {
        var sb = new StringBuilder(key.Length + 8);
        foreach (char c in key)
        {
            if (char.IsAsciiLetterOrDigit(c)) sb.Append(c);
            else sb.Append('_').Append(((int)c).ToString("X4", CultureInfo.InvariantCulture)).Append('_');
        }
        return sb.ToString();
    }

    public static string UnescapeKey(string name) =>
        Escaped.Replace(name, m => ((char)int.Parse(m.Groups[1].Value, NumberStyles.HexNumber, CultureInfo.InvariantCulture)).ToString());

    static string EncodeForm(string form) => form.StartsWith('=') ? "eq" + form[1..] : form;
    static string DecodeForm(string form) => form.StartsWith("eq", StringComparison.Ordinal) ? "=" + form[2..] : form;

    /// <summary>Flat entries for one locale, in a stable order.</summary>
    public static List<KeyValuePair<string, string>> Encode(LocaleData d, IEnumerable<string> allCodes)
    {
        var e = new List<KeyValuePair<string, string>>();
        void Add(string k, string? v) { if (v is not null) e.Add(new(k, v)); }
        // Every meta key is written in every file ("" when absent), so MRT never
        // fills one in from the default language.
        void Meta(string k, string? v) => e.Add(new("meta_" + k, v ?? ""));

        Meta("code", d.Code);
        Meta("label", d.Label);
        Meta("name", d.Name);
        Meta("dir", d.Dir);
        Meta("numberingSystem", d.NumberingSystem);
        Meta("fallback", d.Fallback);
        Meta("order", d.Order?.ToString(CultureInfo.InvariantCulture));
        Meta("locales", string.Join(",", allCodes));
        var (ui, hero) = d.Fonts?.For(Platform) ?? (null, null);
        Meta("fontUi", ui);
        Meta("fontHero", hero);

        if (d.PluralRules is null) Meta("rules", "none");
        else
        {
            var cats = PluralRules.Categories.Where(d.PluralRules.ContainsKey).ToList();
            Meta("rules", cats.Count == 0 ? "other" : string.Join(" ", cats)); // "other" alone: declared, no rules
            foreach (var c in cats) Add("rule_" + c, d.PluralRules[c]);
        }
        foreach (var (key, forms) in d.Plurals.OrderBy(x => x.Key, StringComparer.Ordinal))
        {
            string ek = EscapeKey(key);
            var order = forms.Keys.OrderBy(f => f.StartsWith('=') ? -1 : Array.IndexOf(PluralRules.Categories, f)).ThenBy(f => f, StringComparer.Ordinal).ToList();
            Add("forms_" + ek, string.Join(" ", order.Select(EncodeForm)));
            foreach (var f in order) Add($"p_{ek}_{EncodeForm(f)}", forms[f]);
        }
        foreach (var (k, v) in d.Strings) Add("s_" + EscapeKey(k), v);
        foreach (var (k, v) in d.Hardware) Add("h_" + EscapeKey(k), v);
        foreach (var (k, v) in d.Apps) Add("a_" + EscapeKey(k), v);
        return e;
    }

    /// <summary>
    /// Rebuilds locale data from flat entries. Keys may carry a map prefix such
    /// as "Resources/". Returns the list of every generated locale too.
    /// </summary>
    public static LocaleData Decode(IEnumerable<KeyValuePair<string, string>> entries, out string[] allCodes)
    {
        var map = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var (k, v) in entries)
        {
            int slash = k.LastIndexOf('/');
            map[slash >= 0 ? k[(slash + 1)..] : k] = v;
        }
        string? Get(string k) => map.TryGetValue(k, out var v) && !(k.StartsWith("meta_", StringComparison.Ordinal) && v.Length == 0) ? v : null;

        var d = new LocaleData
        {
            Code = Get("meta_code") ?? throw new FormatException("Catalog has no meta_code"),
            Label = Get("meta_label"),
            Name = Get("meta_name"),
            Dir = Get("meta_dir"),
            NumberingSystem = Get("meta_numberingSystem"),
            Fallback = Get("meta_fallback"),
            Order = int.TryParse(Get("meta_order"), NumberStyles.Integer, CultureInfo.InvariantCulture, out var o) ? o : null
        };
        allCodes = (Get("meta_locales") ?? d.Code).Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        string? fu = Get("meta_fontUi"), fh = Get("meta_fontHero");
        if (fu is not null || fh is not null) d.Fonts = new LocaleFonts { Ui = fu, Hero = fh };

        string rules = Get("meta_rules") ?? "none";
        if (rules != "none")
        {
            d.PluralRules = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var c in rules.Split(' ', StringSplitOptions.RemoveEmptyEntries))
                if (c != "other" && Get("rule_" + c) is string r) d.PluralRules[c] = r;
        }
        foreach (var (name, list) in map)
        {
            if (name.StartsWith("forms_", StringComparison.Ordinal))
            {
                string ek = name["forms_".Length..];
                var forms = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var f in list.Split(' ', StringSplitOptions.RemoveEmptyEntries))
                    if (Get($"p_{ek}_{f}") is string tpl) forms[DecodeForm(f)] = tpl;
                d.Plurals[UnescapeKey(ek)] = forms;
            }
            else if (name.StartsWith("s_", StringComparison.Ordinal)) d.Strings[UnescapeKey(name[2..])] = list;
            else if (name.StartsWith("h_", StringComparison.Ordinal)) d.Hardware[UnescapeKey(name[2..])] = list;
            else if (name.StartsWith("a_", StringComparison.Ordinal)) d.Apps[UnescapeKey(name[2..])] = list;
        }
        return d;
    }

    // ---------------------------------------------------------------------
    // .resw XML
    // ---------------------------------------------------------------------

    public static string ToResw(IEnumerable<KeyValuePair<string, string>> entries, string sourceNote)
    {
        var root = new XElement("root",
            new XComment(" Generated from " + sourceNote + " by Pulse.CatalogGen. Do not edit. "),
            Header("resmimetype", "text/microsoft-resx"),
            Header("version", "2.0"),
            Header("reader", "System.Resources.ResXResourceReader, System.Windows.Forms, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089"),
            Header("writer", "System.Resources.ResXResourceWriter, System.Windows.Forms, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089"));
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase); // MRT names ignore case
        foreach (var (k, v) in entries)
        {
            if (!seen.Add(k)) throw new InvalidOperationException("Two catalog keys differ only by case: " + k);
            root.Add(new XElement("data",
                new XAttribute("name", k),
                new XAttribute(XNamespace.Xml + "space", "preserve"),
                new XElement("value", v)));
        }
        var doc = new XDocument(new XDeclaration("1.0", "utf-8", null), root);
        var sb = new StringBuilder();
        using (var w = XmlWriter.Create(new StringWriterUtf8(sb), new XmlWriterSettings { Indent = true, Encoding = new UTF8Encoding(false), NewLineChars = "\n" }))
            doc.Save(w);
        return sb.Append('\n').ToString();
    }

    public static List<KeyValuePair<string, string>> ReadResw(string xml)
    {
        var doc = XDocument.Parse(xml, LoadOptions.PreserveWhitespace);
        return doc.Root!.Elements("data")
            .Select(x => new KeyValuePair<string, string>((string)x.Attribute("name")!, (string?)x.Element("value") ?? ""))
            .ToList();
    }

    static XElement Header(string name, string value) =>
        new("resheader", new XAttribute("name", name), new XElement("value", value));

    sealed class StringWriterUtf8(StringBuilder sb) : StringWriter(sb, CultureInfo.InvariantCulture)
    {
        public override Encoding Encoding => new UTF8Encoding(false);
    }
}
