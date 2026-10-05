using System.Globalization;
using System.Text.RegularExpressions;

namespace Pulse.Core.I18n;

/// <summary>
/// Compiles CLDR plural rule text into a predicate.
/// <code>
///   condition     = and_condition ('or' and_condition)*
///   and_condition = relation ('and' relation)*
///   relation      = expr ('=' | '!=') range_list
///                 | expr 'is' 'not'? value
///                 | expr 'not'? ('in' | 'within') range_list
///   expr          = operand ('%' | 'mod' value)?
///   range_list    = (value | value '..' value) (',' range_list)*
/// </code>
/// Sample lists ("@integer ...", "@decimal ...") are ignored.
/// </summary>
public static class PluralRules
{
    public static readonly string[] Categories = ["zero", "one", "two", "few", "many", "other"];

    static readonly Regex Token = new(@"\G\s*(\.\.|!=|=|%|,|[a-z]+|\d+(?:\.\d+)?)\s*", RegexOptions.CultureInvariant);

    public static List<string> Tokenize(string src)
    {
        var tokens = new List<string>();
        int at = src.IndexOf('@');
        src = (at >= 0 ? src[..at] : src).Trim();
        int pos = 0;
        while (pos < src.Length)
        {
            var m = Token.Match(src, pos);
            if (!m.Success || m.Length == 0) throw new FormatException("Bad plural rule near: " + src[pos..]);
            tokens.Add(m.Groups[1].Value);
            pos += m.Length;
        }
        return tokens;
    }

    public static Func<PluralOperands, bool> Compile(string src)
    {
        List<string> tk = Tokenize(src);
        if (tk.Count == 0) return _ => true;
        int p = 0;
        string? Peek() => p < tk.Count ? tk[p] : null;
        string? Next() { var t = p < tk.Count ? tk[p] : null; p++; return t; }

        double Num()
        {
            var x = Next();
            if (x is null || !char.IsAsciiDigit(x[0])) throw new FormatException("Expected number in plural rule: " + src);
            return double.Parse(x, CultureInfo.InvariantCulture);
        }

        Func<PluralOperands, double> Expr()
        {
            var op = Next();
            if (op is null || op.Length != 1 || "nivwftce".IndexOf(op[0]) < 0)
                throw new FormatException($"Unknown operand \"{op}\" in plural rule: {src}");
            char c = op[0];
            double? mod = null;
            if (Peek() is "%" or "mod") { Next(); mod = Num(); }
            return mod is double m ? o => o[c] % m : o => o[c];
        }

        List<(double Lo, double Hi)> RangeList()
        {
            var ranges = new List<(double, double)>();
            do
            {
                double lo = Num(), hi = lo;
                if (Peek() == "..") { Next(); hi = Num(); }
                ranges.Add((lo, hi));
            } while (Peek() == "," && Next() is not null);
            return ranges;
        }

        Func<PluralOperands, bool> Relation()
        {
            var e = Expr();
            bool neg = false, integerOnly = true;
            List<(double Lo, double Hi)> ranges;
            var op = Next();
            if (op == "is")
            {
                if (Peek() == "not") { Next(); neg = true; }
                double v = Num();
                ranges = [(v, v)];
            }
            else if (op is "=" or "!=")
            {
                neg = op == "!=";
                ranges = RangeList();
            }
            else
            {
                if (op == "not") { neg = true; op = Next(); }
                if (op == "within") integerOnly = false;
                else if (op != "in") throw new FormatException($"Unknown relation \"{op}\" in plural rule: {src}");
                ranges = RangeList();
            }
            return o =>
            {
                double x = e(o);
                bool hit = false;
                foreach (var (lo, hi) in ranges)
                {
                    if (x >= lo && x <= hi && (!integerOnly || x == Math.Floor(x))) { hit = true; break; }
                }
                return neg ? !hit : hit;
            };
        }

        Func<PluralOperands, bool> AndCond()
        {
            var rs = new List<Func<PluralOperands, bool>> { Relation() };
            while (Peek() == "and") { Next(); rs.Add(Relation()); }
            return o => { foreach (var r in rs) if (!r(o)) return false; return true; };
        }

        var ors = new List<Func<PluralOperands, bool>> { AndCond() };
        while (Peek() == "or") { Next(); ors.Add(AndCond()); }
        if (p != tk.Count) throw new FormatException($"Unexpected \"{(p < tk.Count ? tk[p] : "end")}\" in plural rule: {src}");
        return o => { foreach (var r in ors) if (r(o)) return true; return false; };
    }
}

/// <summary>
/// Picks a CLDR category from the rules a locale file declares. Declared but
/// empty rules (Japanese, Chinese) always give "other". With no rules at all
/// it falls back to "one" for exactly 1 and "other" for everything else.
/// </summary>
public sealed class PluralSelector
{
    readonly (string Category, Func<PluralOperands, bool> Test)[] _compiled;
    readonly bool _hasRules;

    public PluralSelector(IReadOnlyDictionary<string, string>? rules)
    {
        _hasRules = rules is not null;
        _compiled = rules is { Count: > 0 }
            ? PluralRules.Categories
                .Where(c => c != "other" && rules!.TryGetValue(c, out var r) && !string.IsNullOrEmpty(r))
                .Select(c => (c, PluralRules.Compile(rules![c])))
                .ToArray()
            : [];
    }

    public string Select(PluralOperands o)
    {
        if (!_hasRules) return o.N == 1 && o.V == 0 ? "one" : "other";
        foreach (var (cat, test) in _compiled) if (test(o)) return cat;
        return "other";
    }

    public string Select(double value, int? decimals = null) => Select(PluralOperands.From(value, decimals));
    public string Select(string decimalText) => Select(PluralOperands.From(decimalText));
}
