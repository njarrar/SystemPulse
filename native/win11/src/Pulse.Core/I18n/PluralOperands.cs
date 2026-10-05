using System.Globalization;
using System.Text.RegularExpressions;

namespace Pulse.Core.I18n;

/// <summary>
/// CLDR plural operands (https://unicode.org/reports/tr35/tr35-numbers.html#Operands).
/// Built from the visible decimal text, so "1.0" and "1" give different operands.
/// </summary>
public readonly record struct PluralOperands(double N, double I, double V, double W, double F, double T, double C, double E)
{
    static readonly Regex Exponent = new(@"^(.*?)[ce](\d+)$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    public double this[char op] => op switch
    {
        'n' => N, 'i' => I, 'v' => V, 'w' => W, 'f' => F, 't' => T, 'c' => C, 'e' => E,
        _ => throw new ArgumentOutOfRangeException(nameof(op))
    };

    /// <summary>Operands of a number. <paramref name="decimals"/> fixes the visible fraction digits.</summary>
    public static PluralOperands From(double value, int? decimals = null)
    {
        double a = Math.Abs(value);
        string s = decimals is int d
            ? a.ToString("F" + d.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture)
            : JsNumberString(a);
        return From(s);
    }

    /// <summary>Operands of a decimal string such as "1.50" or "1.2c3".</summary>
    public static PluralOperands From(string text)
    {
        string s = text.Trim().TrimStart('-', '+');
        int e = 0;
        var m = Exponent.Match(s);
        if (m.Success)
        {
            e = int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
            s = ShiftDecimal(m.Groups[1].Value, e);
        }
        int dot = s.IndexOf('.');
        string intPart = dot < 0 ? s : s[..dot];
        string frac = dot < 0 ? "" : s[(dot + 1)..];
        string trimmed = frac.TrimEnd('0');
        return new PluralOperands(
            N: Math.Abs(double.Parse(s.Length == 0 ? "0" : s, CultureInfo.InvariantCulture)),
            I: ParseDigits(intPart),
            V: frac.Length,
            W: trimmed.Length,
            F: ParseDigits(frac),
            T: ParseDigits(trimmed),
            C: e,
            E: e);
    }

    static double ParseDigits(string s) =>
        s.Length == 0 ? 0 : double.Parse(s, NumberStyles.None, CultureInfo.InvariantCulture);

    static string ShiftDecimal(string s, int e)
    {
        int dot = s.IndexOf('.');
        string digits = s.Replace(".", "");
        int pos = (dot < 0 ? s.Length : dot) + e;
        while (digits.Length < pos) digits += "0";
        return pos >= digits.Length ? digits : digits[..pos] + "." + digits[pos..];
    }

    /// <summary>Same text JavaScript's String(x) gives for ordinary magnitudes.</summary>
    public static string JsNumberString(double x)
    {
        if (x == Math.Floor(x) && Math.Abs(x) < 1e21)
            return x.ToString("0", CultureInfo.InvariantCulture);
        return x.ToString("R", CultureInfo.InvariantCulture);
    }
}
