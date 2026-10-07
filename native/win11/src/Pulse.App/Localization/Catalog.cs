using Microsoft.Windows.ApplicationModel.Resources;
using Pulse.Core.I18n;

namespace Pulse.App.Localization;

/// <summary>
/// Loads every generated locale back into the i18n engine. The shipped app
/// sits in data\ and reads lang\&lt;code&gt;\Resources.resw beside that folder,
/// one folder per language. A dev build (no lang folder) reads MRT Core
/// (resources.pri, each language through its own ResourceContext), then the
/// Strings\ copies next to the exe.
/// </summary>
public static class Catalog
{
    /// <summary>lang\ next to the data\ folder that holds the exe.</summary>
    public static string LangDir => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "lang"));

    public static LocaleRegistry Load()
    {
        var shipped = FromReswFiles(LangDir);
        if (shipped.Count > 0) return shipped;
        try
        {
            var reg = FromMrt();
            if (reg.Count > 0) return reg;
        }
        catch (Exception) { }
        return FromReswFiles(Path.Combine(AppContext.BaseDirectory, "Strings"));
    }

    static LocaleRegistry FromMrt()
    {
        var rm = new ResourceManager();
        var map = rm.MainResourceMap.TryGetSubtree("Resources") ?? rm.MainResourceMap;
        var reg = new LocaleRegistry();

        List<KeyValuePair<string, string>> Read(string? language)
        {
            var ctx = rm.CreateResourceContext();
            if (language is not null) ctx.QualifierValues["Language"] = language;
            var list = new List<KeyValuePair<string, string>>((int)map.ResourceCount);
            for (uint i = 0; i < map.ResourceCount; i++)
            {
                var kv = map.GetValueByIndex(i, ctx);
                if (kv.Value?.ValueAsString is string v) list.Add(new(kv.Key, v));
            }
            return list;
        }

        ReswCatalog.Decode(Read("en"), out var codes);
        foreach (var code in codes) reg.Register(ReswCatalog.Decode(Read(code), out _));
        return reg;
    }

    public static LocaleRegistry FromReswFiles(string dir)
    {
        var reg = new LocaleRegistry();
        if (!Directory.Exists(dir)) return reg;
        foreach (var sub in Directory.EnumerateDirectories(dir))
        {
            string file = Path.Combine(sub, "Resources.resw");
            if (!File.Exists(file)) continue;
            // One broken file must not take the other languages down with it.
            try { reg.Register(ReswCatalog.Decode(ReswCatalog.ReadResw(File.ReadAllText(file)), out _)); }
            catch (Exception) { }
        }
        return reg;
    }
}
