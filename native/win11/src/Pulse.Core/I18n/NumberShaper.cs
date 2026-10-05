using System.Globalization;
using System.Text;

namespace Pulse.Core.I18n;

/// <summary>
/// Formats numbers with fixed decimals, no grouping, in a CLDR numbering
/// system ("latn", "arab", "arabext", ...). Rounds half away from zero, the
/// way Intl.NumberFormat does.
/// </summary>
public static class NumberShaper
{
    // Zero digit of each decimal numbering system Pulse may meet.
    static readonly Dictionary<string, int> ZeroDigit = new(StringComparer.Ordinal)
    {
        ["latn"] = '0', ["arab"] = 0x0660, ["arabext"] = 0x06F0, ["beng"] = 0x09E6, ["deva"] = 0x0966,
        ["gujr"] = 0x0AE6, ["guru"] = 0x0A66, ["knda"] = 0x0CE6, ["mlym"] = 0x0D66, ["orya"] = 0x0B66,
        ["tamldec"] = 0x0BE6, ["telu"] = 0x0C66, ["thai"] = 0x0E50, ["laoo"] = 0x0ED0, ["tibt"] = 0x0F20,
        ["mymr"] = 0x1040, ["khmr"] = 0x17E0, ["mong"] = 0x1810, ["fullwide"] = 0xFF10
    };

    const char ArabicDecimal = '٫';

    public static string Format(double x, int decimals, string? numberingSystem, string? localeCode)
    {
        if (double.IsNaN(x) || double.IsInfinity(x)) return "—";
        decimals = Math.Clamp(decimals, 0, 10);
        string latin;
        try
        {
            var rounded = Math.Round((decimal)x, decimals, MidpointRounding.AwayFromZero);
            latin = rounded.ToString("F" + decimals.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
        }
        catch (OverflowException)
        {
            latin = x.ToString("F" + decimals.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
        }
        if (latin.StartsWith('-') && latin.Trim('-', '0', '.').Length == 0) latin = latin[1..]; // no "-0"

        string nu = numberingSystem ?? "latn";
        char sep = DecimalSeparator(nu, localeCode);
        int zero = ZeroDigit.TryGetValue(nu, out var z) ? z : '0';
        if (zero == '0' && sep == '.') return latin;

        var sb = new StringBuilder(latin.Length);
        foreach (char c in latin)
        {
            if (c >= '0' && c <= '9') sb.Append((char)(zero + (c - '0')));
            else if (c == '.') sb.Append(sep);
            else sb.Append(c);
        }
        return sb.ToString();
    }

    /// <summary>
    /// Decimal separator for a numbering system in a language. Arabic-script
    /// digits take the Arabic decimal separator; Latin digits take the
    /// language's own symbol ("," in German), never the Arabic one.
    /// </summary>
    public static char DecimalSeparator(string numberingSystem, string? localeCode)
    {
        if (numberingSystem is "arab" or "arabext") return ArabicDecimal;
        string? s = null;
        if (!string.IsNullOrEmpty(localeCode))
        {
            try { s = CultureInfo.GetCultureInfo(localeCode).NumberFormat.NumberDecimalSeparator; }
            catch (CultureNotFoundException) { }
        }
        if (string.IsNullOrEmpty(s) || s.Length != 1 || s[0] == ArabicDecimal) return '.';
        return s[0];
    }
}
