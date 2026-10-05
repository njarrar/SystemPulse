using Microsoft.Windows.ApplicationModel.Resources;
using Pulse.Core.I18n;

namespace Pulse.App.Localization;

/// <summary>
/// Loads every generated locale back into the i18n engine. The primary source
/// is MRT Core (resources.pri built from Strings/&lt;lang&gt;/Resources.resw); each
/// language is read with its own ResourceContext so switching needs no restart.
/// If resources.pri is missing, the plain .resw copies next to the exe are read.
/// </summary>
public static class Catalog
{
    public static LocaleRegistry Load()
    {
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
            if (File.Exists(file)) reg.Register(ReswCatalog.Decode(ReswCatalog.ReadResw(File.ReadAllText(file)), out _));
        }
        return reg;
    }
}
