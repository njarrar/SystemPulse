using System.Diagnostics;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

namespace Pulse.App.Update;

/// <summary>
/// Checks GitHub for a newer Pulse and installs it. Every app reads
/// build/&lt;platform&gt;/latest.json from the repo (written by tools/update_manifest.py):
/// a version plus, per architecture, the zip's path and SHA-256. Nothing is
/// fetched unless the user picks Check for Updates (or runs --update).
///
/// Installing: the zip is checked, unpacked to %TEMP%\PulseUpdate, and its own
/// Pulse.exe (the launcher) is started with --apply-update. That copy waits for
/// this app to close, swaps data\, lang\ and Pulse.exe, then starts Pulse again.
/// </summary>
public static class Updater
{
    public const string DefaultBase = "https://raw.githubusercontent.com/njarrar/SystemPulse/main/";
    const string ManifestPath = "build/windows-11/latest.json";

    public sealed record Release(string Version, string Path, string Sha256);

    public static string BaseUrl
    {
        get
        {
            string url = Environment.GetEnvironmentVariable("PULSE_UPDATE_URL") is { Length: > 0 } v ? v : DefaultBase;
            return url.EndsWith('/') ? url : url + "/";
        }
    }

    public static string Rid => RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? "win-arm64" : "win-x64";

    public static string CurrentVersion
    {
        get
        {
            var v = typeof(Updater).Assembly.GetName().Version ?? new Version(0, 0, 0);
            return $"{v.Major}.{v.Minor}.{Math.Max(0, v.Build)}";
        }
    }

    /// <summary>The folder that holds the launcher, data\ and lang\; null for a dev build.</summary>
    public static string? InstallRoot
    {
        get
        {
            string root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
            return File.Exists(Path.Combine(root, "Pulse.exe")) && Directory.Exists(Path.Combine(root, "data")) ? root : null;
        }
    }

    /// <summary>Compares dotted version numbers part by part ("1.10.0" is newer than "1.9.2").</summary>
    public static int Compare(string a, string b)
    {
        int[] Parts(string s) => s.Trim().TrimStart('v').Split('.').Select(p => int.TryParse(new string(p.TakeWhile(char.IsAsciiDigit).ToArray()), out int n) ? n : 0).ToArray();
        int[] x = Parts(a), y = Parts(b);
        for (int i = 0; i < Math.Max(x.Length, y.Length); i++)
        {
            int c = (i < x.Length ? x[i] : 0).CompareTo(i < y.Length ? y[i] : 0);
            if (c != 0) return c;
        }
        return 0;
    }

    static HttpClient Http() => new() { Timeout = TimeSpan.FromMinutes(5), DefaultRequestHeaders = { { "User-Agent", "Pulse/" + CurrentVersion } } };

    /// <summary>Reads the manifest. Throws if it cannot be fetched or has no build for this PC.</summary>
    public static async Task<Release> LatestAsync()
    {
        using var http = Http();
        string json = await http.GetStringAsync(BaseUrl + ManifestPath);
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        string version = root.GetProperty("version").GetString() ?? throw new InvalidDataException("No version in latest.json");
        if (!root.GetProperty("files").TryGetProperty(Rid, out var file))
            throw new InvalidDataException($"No {Rid} build in latest.json");
        return new Release(version, file.GetProperty("path").GetString()!, file.GetProperty("sha256").GetString()!);
    }

    /// <summary>Downloads and checks the zip, unpacks it, and returns the unpacked folder.</summary>
    public static async Task<string> DownloadAsync(Release r)
    {
        string work = Path.Combine(Path.GetTempPath(), "PulseUpdate", r.Version);
        if (Directory.Exists(work)) Directory.Delete(work, true);
        Directory.CreateDirectory(work);
        string zip = Path.Combine(work, "Pulse.zip");
        using (var http = Http())
        await using (var src = await http.GetStreamAsync(BaseUrl + r.Path))
        await using (var dst = File.Create(zip))
            await src.CopyToAsync(dst);

        string hash;
        await using (var f = File.OpenRead(zip))
            hash = Convert.ToHexString(await SHA256.HashDataAsync(f));
        if (!hash.Equals(r.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The download is damaged (checksum mismatch)");

        string files = Path.Combine(work, "files");
        ZipFile.ExtractToDirectory(zip, files);
        if (!File.Exists(Path.Combine(files, "Pulse.exe")) || !File.Exists(Path.Combine(files, "data", "Pulse.exe")))
            throw new InvalidDataException("The download is missing Pulse.exe");
        return files;
    }

    /// <summary>Starts the new launcher to swap the files once every running Pulse from this folder has closed.</summary>
    public static void StartApply(string files, string root)
    {
        var start = new ProcessStartInfo(Path.Combine(files, "Pulse.exe")) { UseShellExecute = false, WorkingDirectory = files };
        start.ArgumentList.Add("--apply-update");
        start.ArgumentList.Add(root);
        Process.Start(start)?.Dispose();
    }

    /// <summary>Clears what an earlier update left in %TEMP%.</summary>
    public static void CleanUp()
    {
        try
        {
            string dir = Path.Combine(Path.GetTempPath(), "PulseUpdate");
            if (Directory.Exists(dir)) Directory.Delete(dir, true);
        }
        catch (Exception) { /* still in use; next start tries again */ }
    }
}
