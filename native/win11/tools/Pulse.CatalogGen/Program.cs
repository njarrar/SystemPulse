// Pulse.CatalogGen: converts locales/*.json into one .resw per language.
//
//   Pulse.CatalogGen <locales dir> <output dir>
//
// Writes <output dir>/<code>/Resources.resw for every locale file, removes
// folders of locales that no longer exist, and leaves unchanged files alone so
// incremental builds stay quiet. Validation errors fail the build; warnings
// (missing keys that fall back to English) are printed.
using Pulse.Core.I18n;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: Pulse.CatalogGen <locales dir> <output dir>");
    return 2;
}
string localesDir = Path.GetFullPath(args[0]);
string outDir = Path.GetFullPath(args[1]);

var files = Directory.EnumerateFiles(localesDir, "*.json")
    .Where(f => !string.Equals(Path.GetFileName(f), "schema.json", StringComparison.OrdinalIgnoreCase))
    .OrderBy(f => f, StringComparer.Ordinal)
    .ToList();
if (files.Count == 0)
{
    Console.Error.WriteLine($"error: no locale files in {localesDir}");
    return 1;
}

var locales = new List<(string File, LocaleData Data)>();
foreach (var f in files)
{
    try { locales.Add((f, LocaleData.FromJsonFile(f))); }
    catch (Exception e)
    {
        Console.Error.WriteLine($"{f}: error: {e.Message}");
        return 1;
    }
}
var baseData = locales.FirstOrDefault(x => x.Data.Code == "en").Data;
var codes = locales.Select(x => x.Data.Code).ToList();
int errors = 0;

foreach (var (file, data) in locales)
{
    var r = LocaleValidator.Validate(data, ReferenceEquals(data, baseData) ? null : baseData);
    foreach (var e in r.Errors) { Console.Error.WriteLine($"{file}: error: {e}"); errors++; }
    foreach (var w in r.Warnings) Console.WriteLine($"{file}: warning: {w}");
    if (r.Errors.Count > 0) continue;

    string xml = ReswCatalog.ToResw(ReswCatalog.Encode(data, codes), "locales/" + Path.GetFileName(file));
    string dir = Path.Combine(outDir, data.Code);
    string path = Path.Combine(dir, "Resources.resw");
    Directory.CreateDirectory(dir);
    if (!File.Exists(path) || File.ReadAllText(path) != xml)
    {
        File.WriteAllText(path, xml);
        Console.WriteLine($"wrote {path}");
    }
}

if (Directory.Exists(outDir))
{
    foreach (var d in Directory.EnumerateDirectories(outDir))
    {
        if (codes.Contains(Path.GetFileName(d))) continue;
        if (File.Exists(Path.Combine(d, "Resources.resw"))) File.Delete(Path.Combine(d, "Resources.resw"));
        if (!Directory.EnumerateFileSystemEntries(d).Any()) Directory.Delete(d);
        Console.WriteLine($"removed stale catalog {d}");
    }
}
return errors > 0 ? 1 : 0;
