using System.Text.Json;
using Pulse.Core.I18n;
using Xunit;

namespace Pulse.Core.Tests;

static class Data
{
    public static string Dir(params string[] parts) => Path.Combine([AppContext.BaseDirectory, "data", .. parts]);
    public static LocaleData Load(string code) => LocaleData.FromJsonFile(Dir("locales", code + ".json"));

    public static LocaleRegistry Registry()
    {
        var r = new LocaleRegistry();
        r.LoadFolder(Dir("locales"));
        return r;
    }

    /// <summary>Same locales, after a trip through the generated .resw XML.</summary>
    public static LocaleRegistry ReswRegistry()
    {
        var all = new[] { "en", "ar" }.Select(Load).ToList();
        var codes = all.Select(d => d.Code).ToList();
        var r = new LocaleRegistry();
        foreach (var d in all)
        {
            var xml = ReswCatalog.ToResw(ReswCatalog.Encode(d, codes), d.Code + ".json");
            r.Register(ReswCatalog.Decode(ReswCatalog.ReadResw(xml), out _));
        }
        return r;
    }

    public static IEnumerable<object[]> Sources() => [["json"], ["resw"]];
    public static LocaleRegistry From(string source) => source == "json" ? Registry() : ReswRegistry();
}

public class VectorTests
{
    static JsonElement Vectors() => JsonDocument.Parse(File.ReadAllText(Data.Dir("fixtures", "vectors.json"))).RootElement;

    [Theory]
    [MemberData(nameof(Data.Sources), MemberType = typeof(Data))]
    public void Plural_categories_match_vectors(string source)
    {
        var reg = Data.From(source);
        int checkedCount = 0;
        foreach (var lang in Vectors().EnumerateObject())
        {
            var loc = reg.Get(lang.Name);
            // The vectors were made from the same rules the locale file declares.
            foreach (var rule in lang.Value.GetProperty("pluralRules").EnumerateObject())
                Assert.Equal(rule.Value.GetString(), loc.Data.PluralRules![rule.Name]);
            foreach (var pair in lang.Value.GetProperty("categories").EnumerateArray())
            {
                string n = pair[0].GetString()!, want = pair[1].GetString()!;
                Assert.True(want == loc.Selector.Select(n), $"{lang.Name} {n}: want {want}, got {loc.Selector.Select(n)}");
                checkedCount++;
            }
        }
        Assert.True(checkedCount >= 470, "expected the full 0..230 and decimal vectors for en and ar");
    }

    [Theory]
    [MemberData(nameof(Data.Sources), MemberType = typeof(Data))]
    public void Rendered_samples_match_vectors_including_isolates(string source)
    {
        var reg = Data.From(source);
        foreach (var lang in Vectors().EnumerateObject())
        {
            var loc = reg.Get(lang.Name);
            foreach (var sample in lang.Value.GetProperty("samples").EnumerateObject())
            {
                string key = sample.Name, want = sample.Value.GetString()!;
                string[] head = key.Split(':', 2);
                string got;
                if (double.TryParse(head[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var n))
                    got = loc.Plural(head[0], n);
                else
                {
                    var args = head[1].Split(',').Select(p => p.Split('=', 2)).ToDictionary(p => p[0], p => (object?)p[1]);
                    got = loc.T(head[0], args);
                }
                Assert.True(want == got, $"{lang.Name} {key}: want [{Escape(want)}] got [{Escape(got)}]");
            }
        }
    }

    static string Escape(string s) => string.Concat(s.Select(c => c > 127 && c is >= '⁦' and <= '⁩' ? $"\\u{(int)c:X4}" : c.ToString()));
}

public class IntlParityTests
{
    static readonly Dictionary<char, string> Cat = new() { ['z'] = "zero", ['1'] = "one", ['2'] = "two", ['f'] = "few", ['m'] = "many", ['o'] = "other" };

    [Fact]
    public void Resolver_matches_Intl_PluralRules_for_ten_languages()
    {
        var root = JsonDocument.Parse(File.ReadAllText(Data.Dir("fixtures", "intl-plurals.json"))).RootElement;
        int count = 0;
        foreach (var lang in root.GetProperty("languages").EnumerateObject())
        {
            var rules = lang.Value.GetProperty("rules").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!);
            var sel = new PluralSelector(rules);
            foreach (var s in lang.Value.GetProperty("series").EnumerateObject())
            {
                int d = int.Parse(s.Name);
                string cats = s.Value.GetProperty("cats").GetString()!;
                for (int k = 0; k < cats.Length; k++)
                {
                    double x = Math.Round(k * Math.Pow(10, -d), d);
                    string got = sel.Select(x, d), want = Cat[cats[k]];
                    Assert.True(want == got, $"{lang.Name} {x.ToString("F" + d)}: want {want}, got {got}");
                    count++;
                }
            }
        }
        Assert.Equal(10 * (1201 + 301 + 3001), count);
    }

    [Fact]
    public void Operands_follow_CLDR()
    {
        Assert.Equal(new PluralOperands(1.5, 1, 2, 1, 50, 5, 0, 0), PluralOperands.From("1.50"));
        Assert.Equal(new PluralOperands(7, 7, 0, 0, 0, 0, 0, 0), PluralOperands.From(7));
        Assert.Equal(1200, PluralOperands.From("1.2c3").I);
        Assert.Equal(2, PluralOperands.From(1.0, 2).V);
    }

    [Theory]
    [InlineData("n = 1 and")]
    [InlineData("q = 1")]
    [InlineData("n ? 1")]
    [InlineData("n = ")]
    public void Bad_rules_throw(string rule) => Assert.Throws<FormatException>(() => PluralRules.Compile(rule));

    [Fact]
    public void Samples_after_at_are_ignored() =>
        Assert.True(PluralRules.Compile("i = 1 and v = 0 @integer 1")(PluralOperands.From(1)));
}

public class EngineTests
{
    const char FSI = '⁨', PDI = '⁩';
    static string Strip(string s) => Locale.StripIsolates(s);

    [Theory]
    [MemberData(nameof(Data.Sources), MemberType = typeof(Data))]
    public void Arabic_picks_all_six_forms(string source)
    {
        var l = Data.From(source).Get("ar");
        var want = new Dictionary<int, string> { [0] = "zero", [1] = "one", [2] = "two", [3] = "few", [10] = "few", [11] = "many", [99] = "many", [100] = "other", [101] = "other", [102] = "other", [103] = "few", [111] = "many", [1000] = "other" };
        foreach (var (n, cat) in want) Assert.Equal(cat, l.Select(n));
        Assert.Equal("عملية واحدة", Strip(l.Plural("process", 1)));
        Assert.Equal("عمليتان", Strip(l.Plural("process", 2)));
        Assert.Equal("9 عمليات", Strip(l.Plural("process", 9)));
        Assert.Equal("84 خيطاً", Strip(l.Plural("thread", 84)));
        Assert.Equal("103 خيوط", Strip(l.Plural("thread", 103)));
        Assert.Equal("لم يتم إنهاء أي تطبيق بعد", Strip(l.Plural("ended", 0)));
    }

    [Theory]
    [MemberData(nameof(Data.Sources), MemberType = typeof(Data))]
    public void English_exact_zero_form_wins(string source)
    {
        var l = Data.From(source).Get("en");
        Assert.Equal("No apps ended yet", l.Plural("ended", 0));
        Assert.Equal("1 app ended this session", l.Plural("ended", 1));
        Assert.Equal("3 apps ended this session", l.Plural("ended", 3));
        Assert.Equal("1 thread", l.Plural("thread", 1));
    }

    [Fact]
    public void Rtl_interpolation_isolates_every_value()
    {
        var l = Data.Registry().Get("ar");
        Assert.Equal($"{FSI}Xcode{PDI} يستهلك {FSI}52%{PDI} من المعالج", l.T("hogTitle", ("app", "Xcode"), ("pct", "52%")));
        Assert.Equal($"المعالج {FSI}52°C{PDI} · الرسوميات {FSI}31°C{PDI}", l.T("tempLine", ("cpu", "52°C"), ("gpu", "31°C")));
    }

    [Fact]
    public void Ltr_interpolation_isolates_only_rtl_values()
    {
        var l = Data.Registry().Get("en");
        Assert.Equal("Xcode is using 52% of total CPU", l.T("hogTitle", ("app", "Xcode"), ("pct", "52%")));
        Assert.Equal($"End {FSI}خادم النوافذ{PDI}", l.T("endNamed", ("app", "خادم النوافذ")));
    }

    [Fact]
    public void Missing_keys_fall_back_to_english_then_to_the_key()
    {
        var r = Data.Registry();
        var xx = new LocaleData { Code = "xx", Dir = "ltr" };
        xx.Strings["cpu"] = "XPU";
        r.Register(xx);
        var l = r.Get("xx");
        var missing = new List<string>();
        Locale.MissingKey = (k, c) => missing.Add(k + "@" + c);
        try
        {
            Assert.Equal("XPU", l.T("cpu"));
            Assert.Equal("Quit", l.T("quit"));
            Assert.Equal("3 processes", l.Plural("process", 3));
            Assert.Equal("noSuchKey", l.T("noSuchKey"));
            Assert.Equal("noSuchKey", l.T("noSuchKey"));
            Assert.Equal(["strings.noSuchKey@xx"], missing); // one warning per key
        }
        finally { Locale.MissingKey = null; }
    }

    [Fact]
    public void Codes_resolve_by_prefix()
    {
        var r = Data.Registry();
        Assert.Equal("ar", r.Resolve("ar-EG"));
        Assert.Equal("en", r.Resolve("en_GB"));
        Assert.Equal("en", r.Resolve("zz"));
        Assert.Equal("ar", r.Get("ar-EG").Code);
    }

    [Fact]
    public void Numbers_follow_the_numbering_system()
    {
        var r = Data.Registry();
        r.Register(new LocaleData { Code = "fa", Dir = "rtl", NumberingSystem = "arabext" });
        Assert.Equal("۵۴٫۳", r.Get("fa").Num(54.25, 1));
        Assert.Equal("54.3", r.Get("ar").Num(54.25, 1));
        Assert.Equal("0", r.Get("en").Num(-0.2));
        Assert.Equal("3", r.Get("en").Num(2.5));
    }

    [Fact]
    public void Join_isolates_items_in_rtl()
    {
        var r = Data.Registry();
        Assert.Equal($"{FSI}a{PDI} · {FSI}b{PDI}", r.Get("ar").Join("a", "b"));
        Assert.Equal("a · b", r.Get("en").Join("a", "", "b"));
    }

    [Fact]
    public void Validator_catches_bad_files()
    {
        var bad = new LocaleData { Code = "zz", Dir = "up", PluralRules = new() { ["one"] = "n = 1", ["lots"] = "n > 3" } };
        bad.Plurals["process"] = new() { ["one"] = "x" };
        bad.Strings["calm"] = "{oops}";
        var text = string.Join("\n", LocaleValidator.Validate(bad, Data.Load("en")).Errors);
        Assert.Contains("\"dir\"", text);
        Assert.Contains("bad category \"lots\"", text);
        Assert.Contains("needs an \"other\" form", text);
        Assert.Contains("unknown placeholder {oops}", text);
    }

    [Fact]
    public void Shipped_locales_are_clean()
    {
        var en = Data.Load("en");
        Assert.Empty(LocaleValidator.Validate(en, null).Errors);
        var r = LocaleValidator.Validate(Data.Load("ar"), en);
        Assert.Empty(r.Errors);
        Assert.Empty(r.Warnings);
    }

    [Fact]
    public void Registry_lists_base_first_and_carries_direction()
    {
        var list = Data.Registry().List();
        Assert.Equal("en", list[0].Code);
        Assert.Contains(list, x => x.Code == "ar" && x.Dir == "rtl" && x.Label == "ع");
    }

    [Fact]
    public void Pseudo_locale_loads_and_falls_back()
    {
        var r = Data.Registry();
        r.Register(LocaleData.FromJsonFile(Data.Dir("fixtures", "en-XA.json")));
        var l = r.Get("en-XA");
        Assert.Empty(LocaleValidator.Validate(l.Data, Data.Load("en")).Errors);
        Assert.StartsWith("[", l.T("calm"));
        Assert.True(l.T("calm").Length >= "System Calm".Length * 1.3);
    }
}

public class ReswTests
{
    [Fact]
    public void Round_trip_keeps_every_value()
    {
        foreach (var code in new[] { "en", "ar" })
        {
            var d = Data.Load(code);
            var xml = ReswCatalog.ToResw(ReswCatalog.Encode(d, ["en", "ar"]), code);
            var back = ReswCatalog.Decode(ReswCatalog.ReadResw(xml), out var codes);
            Assert.Equal(["en", "ar"], codes);
            Assert.Equal(d.Code, back.Code);
            Assert.Equal(d.Dir, back.Dir);
            Assert.Equal(d.Label, back.Label);
            Assert.Equal(d.NumberingSystem, back.NumberingSystem);
            Assert.Equal(d.PluralRules, back.PluralRules);
            Assert.Equal(d.Strings, back.Strings);
            Assert.Equal(d.Hardware, back.Hardware);
            Assert.Equal(d.Apps, back.Apps);
            Assert.Equal(d.Plurals.Keys.Order(), back.Plurals.Keys.Order());
            foreach (var (k, forms) in d.Plurals) Assert.Equal(forms, back.Plurals[k]);
            Assert.Equal(d.Fonts?.For("win"), back.Fonts?.For("win"));
        }
    }

    [Fact]
    public void Mrt_style_fallback_does_not_leak_english_forms()
    {
        // MRT returns the default-language value for any key a language lacks.
        // Simulate that: Arabic entries over English entries.
        var codes = new[] { "en", "ar" };
        var en = ReswCatalog.Encode(Data.Load("en"), codes).ToDictionary(x => x.Key, x => x.Value);
        var ar = ReswCatalog.Encode(Data.Load("ar"), codes).ToDictionary(x => x.Key, x => x.Value);
        var merged = new Dictionary<string, string>(en);
        foreach (var (k, v) in ar) merged[k] = v;
        Assert.True(merged.ContainsKey("p_ended_eq0")); // the English-only exact form is present...
        var d = ReswCatalog.Decode(merged.Select(kv => new KeyValuePair<string, string>("Resources/" + kv.Key, kv.Value)), out _);
        var l = new Locale(d);
        Assert.False(d.Plurals["ended"].ContainsKey("=0")); // ...but never read for Arabic
        Assert.Equal("لم يتم إنهاء أي تطبيق بعد", Locale.StripIsolates(l.Plural("ended", 0)));
    }

    [Fact]
    public void Locale_without_rules_does_not_inherit_english_rules()
    {
        var ja = new LocaleData { Code = "ja", Dir = "ltr", PluralRules = new() };
        ja.Plurals["core"] = new() { ["other"] = "{n} コア" };
        var codes = new[] { "en", "ja" };
        var merged = ReswCatalog.Encode(Data.Load("en"), codes).ToDictionary(x => x.Key, x => x.Value);
        foreach (var (k, v) in ReswCatalog.Encode(ja, codes)) merged[k] = v;
        var l = new Locale(ReswCatalog.Decode(merged, out _));
        Assert.Equal("1 コア", l.Plural("core", 1));
    }

    [Theory]
    [InlineData("Microsoft Edge")]
    [InlineData("a_b")]
    [InlineData("tracker-miner-fs-3")]
    [InlineData("Linux (Crostini)")]
    public void Keys_escape_and_unescape(string key)
    {
        string e = ReswCatalog.EscapeKey(key);
        Assert.Matches("^[A-Za-z0-9_]+$", e);
        Assert.Equal(key, ReswCatalog.UnescapeKey(e));
    }

    [Fact]
    public void A_new_locale_file_needs_no_code_change()
    {
        string dir = Path.Combine(Path.GetTempPath(), "pulse-loc-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            foreach (var f in Directory.GetFiles(Data.Dir("locales"))) File.Copy(f, Path.Combine(dir, Path.GetFileName(f)));
            string de = File.ReadAllText(Path.Combine(dir, "en.json"))
                .Replace("\"code\": \"en\"", "\"code\": \"de\"")
                .Replace("\"label\": \"EN\"", "\"label\": \"DE\"")
                .Replace("\"System Calm\"", "\"System ruhig\"");
            File.WriteAllText(Path.Combine(dir, "de.json"), de);
            var r = new LocaleRegistry();
            var codes = r.LoadFolder(dir);
            Assert.Equal(3, r.List().Count);
            var l = r.Get("de-AT");
            Assert.Equal("de", l.Code);
            Assert.Equal("System ruhig", l.T("calm"));
            Assert.Equal("54,3", l.Num(54.25, 1)); // German decimal comma, Latin digits
            var xml = ReswCatalog.ToResw(ReswCatalog.Encode(l.Data, codes), "de.json");
            Assert.Contains("System ruhig", xml);
        }
        finally { Directory.Delete(dir, true); }
    }
}
