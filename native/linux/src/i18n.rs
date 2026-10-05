//! Locale engine, ported from i18n/pulse-i18n.js.
//!
//! Catalogs come from the gettext .mo files that build.rs makes out of
//! locales/*.json. Plural forms are chosen with the CLDR rules stored in the
//! catalog (exact "=N" forms win), values put into a template are wrapped in
//! Unicode isolates (U+2068 .. U+2069) in RTL locales, missing keys fall back
//! to the locale's fallback (English by default) and then to the key, and a
//! code such as "ar-EG" resolves to "ar".

use crate::catalog::{self, Entry};
use std::cell::RefCell;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::rc::Rc;

pub const CATEGORIES: [&str; 6] = ["zero", "one", "two", "few", "many", "other"];
pub const FSI: char = '\u{2068}';
pub const PDI: char = '\u{2069}';

include!(concat!(env!("OUT_DIR"), "/catalogs.rs"));

// ---------------------------------------------------------------------------
// CLDR plural operands
// ---------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Operands {
    pub n: f64,
    pub i: f64,
    pub v: f64,
    pub w: f64,
    pub f: f64,
    pub t: f64,
    pub c: f64,
    pub e: f64,
}

impl Operands {
    fn get(&self, op: char) -> f64 {
        match op {
            'n' => self.n,
            'i' => self.i,
            'v' => self.v,
            'w' => self.w,
            'f' => self.f,
            't' => self.t,
            'c' => self.c,
            _ => self.e,
        }
    }
}

fn shift_decimal(s: &str, e: usize) -> String {
    let dot = s.find('.');
    let mut digits: String = s.replace('.', "");
    let pos = dot.unwrap_or(s.len()) + e;
    while digits.len() < pos {
        digits.push('0');
    }
    if pos >= digits.len() {
        digits
    } else {
        format!("{}.{}", &digits[..pos], &digits[pos..])
    }
}

fn parse_int(s: &str) -> f64 {
    if s.is_empty() {
        0.0
    } else {
        s.parse::<f64>().unwrap_or(0.0)
    }
}

/// Operands of a decimal string such as "1.50" or "1.2c3".
pub fn operands_str(value: &str) -> Operands {
    let mut s = value.trim().trim_start_matches(['-', '+']).to_string();
    let mut e = 0usize;
    if let Some(pos) = s.find(['c', 'e', 'C', 'E']) {
        if let Ok(x) = s[pos + 1..].parse::<usize>() {
            e = x;
            s = shift_decimal(&s[..pos], e);
        }
    }
    let (int_part, frac) = match s.find('.') {
        Some(d) => (s[..d].to_string(), s[d + 1..].to_string()),
        None => (s.clone(), String::new()),
    };
    let trimmed = frac.trim_end_matches('0');
    Operands {
        n: s.parse::<f64>().unwrap_or(0.0).abs(),
        i: parse_int(&int_part),
        v: frac.len() as f64,
        w: trimmed.len() as f64,
        f: parse_int(&frac),
        t: parse_int(trimmed),
        c: e as f64,
        e: e as f64,
    }
}

/// Operands of a number. With `decimals`, the visible fraction digits are
/// fixed ("1.0" is not "1" in most languages).
pub fn operands(value: f64, decimals: Option<usize>) -> Operands {
    let s = match decimals {
        Some(d) => format!("{:.*}", d, value.abs()),
        None => js_number_string(value.abs()),
    };
    operands_str(&s)
}

/// Shortest round-trip decimal form, like JavaScript's String(number).
pub fn js_number_string(x: f64) -> String {
    if x.fract() == 0.0 && x.abs() < 1e21 {
        format!("{}", x as i64)
    } else {
        format!("{x}")
    }
}

// ---------------------------------------------------------------------------
// CLDR plural rule compiler
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
struct Relation {
    op: char,
    modulo: Option<f64>,
    neg: bool,
    integer_only: bool,
    ranges: Vec<(f64, f64)>,
}

impl Relation {
    fn eval(&self, o: &Operands) -> bool {
        let mut x = o.get(self.op);
        if let Some(m) = self.modulo {
            x %= m;
        }
        let hit = self
            .ranges
            .iter()
            .any(|&(lo, hi)| x >= lo && x <= hi && (!self.integer_only || x == x.floor()));
        hit != self.neg
    }
}

/// A compiled rule: OR of ANDs of relations. An empty rule is always true.
#[derive(Clone, Debug, Default)]
pub struct Rule {
    ors: Vec<Vec<Relation>>,
}

impl Rule {
    pub fn eval(&self, o: &Operands) -> bool {
        self.ors.is_empty() || self.ors.iter().any(|and| and.iter().all(|r| r.eval(o)))
    }
}

fn tokenize(src: &str) -> Result<Vec<String>, String> {
    let src = match src.find('@') {
        Some(p) => &src[..p],
        None => src,
    }
    .trim();
    let b: Vec<char> = src.chars().collect();
    let mut i = 0;
    let mut out = Vec::new();
    while i < b.len() {
        let c = b[i];
        if c.is_whitespace() {
            i += 1;
        } else if c == '.' && b.get(i + 1) == Some(&'.') {
            out.push("..".into());
            i += 2;
        } else if c == '!' && b.get(i + 1) == Some(&'=') {
            out.push("!=".into());
            i += 2;
        } else if c == '=' || c == '%' || c == ',' {
            out.push(c.to_string());
            i += 1;
        } else if c.is_ascii_lowercase() {
            let st = i;
            while i < b.len() && b[i].is_ascii_lowercase() {
                i += 1;
            }
            out.push(b[st..i].iter().collect());
        } else if c.is_ascii_digit() {
            let st = i;
            while i < b.len() && b[i].is_ascii_digit() {
                i += 1;
            }
            if i + 1 < b.len() && b[i] == '.' && b[i + 1].is_ascii_digit() {
                i += 1;
                while i < b.len() && b[i].is_ascii_digit() {
                    i += 1;
                }
            }
            out.push(b[st..i].iter().collect());
        } else {
            return Err(format!("Bad plural rule near: {}", b[i..].iter().collect::<String>()));
        }
    }
    Ok(out)
}

pub fn compile_rule(src: &str) -> Result<Rule, String> {
    let tk = tokenize(src)?;
    if tk.is_empty() {
        return Ok(Rule::default());
    }
    let mut p = 0usize;
    let peek = |p: usize| tk.get(p).map(String::as_str);
    let num = |p: &mut usize| -> Result<f64, String> {
        let x = tk.get(*p).cloned().unwrap_or_default();
        *p += 1;
        if !x.starts_with(|c: char| c.is_ascii_digit()) {
            return Err(format!("Expected number in plural rule: {src}"));
        }
        x.parse::<f64>().map_err(|e| e.to_string())
    };
    let mut ors = Vec::new();
    loop {
        let mut ands = Vec::new();
        loop {
            // expr
            let op = tk.get(p).cloned().unwrap_or_default();
            p += 1;
            if op.len() != 1 || !"nivwftce".contains(op.as_str()) {
                return Err(format!("Unknown operand \"{op}\" in plural rule: {src}"));
            }
            let mut modulo = None;
            if matches!(peek(p), Some("%") | Some("mod")) {
                p += 1;
                modulo = Some(num(&mut p)?);
            }
            // relation
            let mut neg = false;
            let mut integer_only = true;
            let ranges;
            let rel = tk.get(p).cloned().unwrap_or_default();
            p += 1;
            if rel == "is" {
                if peek(p) == Some("not") {
                    p += 1;
                    neg = true;
                }
                let v = num(&mut p)?;
                ranges = vec![(v, v)];
            } else if rel == "=" || rel == "!=" {
                neg = rel == "!=";
                ranges = range_list(&tk, &mut p, src)?;
            } else {
                let mut r = rel;
                if r == "not" {
                    neg = true;
                    r = tk.get(p).cloned().unwrap_or_default();
                    p += 1;
                }
                if r == "within" {
                    integer_only = false;
                } else if r != "in" {
                    return Err(format!("Unknown relation \"{r}\" in plural rule: {src}"));
                }
                ranges = range_list(&tk, &mut p, src)?;
            }
            ands.push(Relation { op: op.chars().next().unwrap(), modulo, neg, integer_only, ranges });
            if peek(p) == Some("and") {
                p += 1;
            } else {
                break;
            }
        }
        ors.push(ands);
        if peek(p) == Some("or") {
            p += 1;
        } else {
            break;
        }
    }
    if p != tk.len() {
        return Err(format!("Unexpected \"{}\" in plural rule: {src}", tk[p]));
    }
    Ok(Rule { ors })
}

fn range_list(tk: &[String], p: &mut usize, src: &str) -> Result<Vec<(f64, f64)>, String> {
    let num = |p: &mut usize| -> Result<f64, String> {
        let x = tk.get(*p).cloned().unwrap_or_default();
        *p += 1;
        if !x.starts_with(|c: char| c.is_ascii_digit()) {
            return Err(format!("Expected number in plural rule: {src}"));
        }
        x.parse::<f64>().map_err(|e| e.to_string())
    };
    let mut out = Vec::new();
    loop {
        let lo = num(p)?;
        let mut hi = lo;
        if tk.get(*p).map(String::as_str) == Some("..") {
            *p += 1;
            hi = num(p)?;
        }
        out.push((lo, hi));
        if tk.get(*p).map(String::as_str) == Some(",") {
            *p += 1;
        } else {
            break;
        }
    }
    Ok(out)
}

/// Category selector built from a locale's pluralRules.
#[derive(Clone, Debug, Default)]
pub struct PluralSelector {
    rules: Vec<(&'static str, Rule)>,
}

impl PluralSelector {
    pub fn new(rules: &BTreeMap<String, String>) -> Self {
        let mut out = Vec::new();
        for cat in CATEGORIES {
            if cat == "other" {
                continue;
            }
            if let Some(src) = rules.get(cat) {
                match compile_rule(src) {
                    Ok(r) => out.push((cat, r)),
                    Err(e) => eprintln!("[i18n] {e}"),
                }
            }
        }
        // No rules at all: one/other, like the JS engine without Intl.
        if rules.is_empty() {
            out.push(("one", compile_rule("n = 1").unwrap()));
        }
        PluralSelector { rules: out }
    }

    pub fn select_operands(&self, o: &Operands) -> &'static str {
        for (cat, r) in &self.rules {
            if r.eval(o) {
                return cat;
            }
        }
        "other"
    }

    pub fn select(&self, value: f64, decimals: Option<usize>) -> &'static str {
        self.select_operands(&operands(value, decimals))
    }

    pub fn select_str(&self, value: &str) -> &'static str {
        self.select_operands(&operands_str(value))
    }
}

// ---------------------------------------------------------------------------
// Locale data
// ---------------------------------------------------------------------------

#[derive(Clone, Debug, Default)]
pub struct LocaleData {
    pub code: String,
    pub label: String,
    pub name: String,
    pub rtl: bool,
    pub fallback: Option<String>,
    pub numbering: String,
    pub order: Option<f64>,
    pub fonts: HashMap<String, String>,
    pub plural_rules: BTreeMap<String, String>,
    pub plurals: HashMap<String, HashMap<String, String>>,
    pub strings: HashMap<String, String>,
    pub hardware: HashMap<String, String>,
    pub apps: HashMap<String, String>,
}

impl LocaleData {
    pub fn from_entries(entries: &[Entry]) -> Result<Self, String> {
        let mut d = LocaleData::default();
        for e in entries {
            let v = e.text.clone();
            match e.ctx.as_str() {
                "meta" => match e.id.as_str() {
                    "code" => d.code = v,
                    "label" => d.label = v,
                    "name" => d.name = v,
                    "dir" => d.rtl = v == "rtl",
                    "fallback" => d.fallback = Some(v),
                    "numberingSystem" => d.numbering = v,
                    "order" => d.order = v.parse().ok(),
                    k if k.starts_with("fonts.") => {
                        d.fonts.insert(k["fonts.".len()..].to_string(), v);
                    }
                    _ => {}
                },
                "pluralRules" => {
                    d.plural_rules.insert(e.id.clone(), v);
                }
                "strings" => {
                    d.strings.insert(e.id.clone(), v);
                }
                "hardware" => {
                    d.hardware.insert(e.id.clone(), v);
                }
                "apps" => {
                    d.apps.insert(e.id.clone(), v);
                }
                c if c.starts_with("plural:") => {
                    d.plurals
                        .entry(c["plural:".len()..].to_string())
                        .or_default()
                        .insert(e.id.clone(), v);
                }
                _ => {}
            }
        }
        if d.code.is_empty() {
            return Err("catalog has no meta/code".into());
        }
        if d.label.is_empty() {
            d.label = d.code.to_uppercase();
        }
        if d.name.is_empty() {
            d.name = d.code.clone();
        }
        if d.numbering.is_empty() {
            d.numbering = "latn".into();
        }
        Ok(d)
    }

    pub fn from_mo(bytes: &[u8]) -> Result<Self, String> {
        Self::from_entries(&catalog::read_mo(bytes)?)
    }
}

fn is_rtl_char(c: char) -> bool {
    matches!(c as u32, 0x0590..=0x08FF | 0xFB1D..=0xFDFF | 0xFE70..=0xFEFF)
}

pub fn isolate(s: &str) -> String {
    format!("{FSI}{s}{PDI}")
}

/// A resolved locale with its fallback chain.
#[derive(Clone)]
pub struct Locale {
    chain: Vec<Rc<LocaleData>>,
    select: Rc<PluralSelector>,
    warned: Rc<RefCell<HashSet<String>>>,
}

impl Locale {
    pub fn data(&self) -> &LocaleData {
        &self.chain[0]
    }
    pub fn code(&self) -> &str {
        &self.chain[0].code
    }
    pub fn rtl(&self) -> bool {
        self.chain[0].rtl
    }
    pub fn name(&self) -> &str {
        &self.chain[0].name
    }

    fn lookup(&self, f: impl Fn(&LocaleData) -> Option<&String>) -> Option<String> {
        self.chain.iter().find_map(|d| f(d).cloned())
    }

    fn miss(&self, id: &str, key: &str) -> String {
        if self.warned.borrow_mut().insert(id.to_string()) {
            eprintln!("[i18n] missing {id} in {}", self.code());
        }
        key.to_string()
    }

    /// Formats a number with fixed decimals in the locale's numbering system.
    pub fn num(&self, x: f64, decimals: usize) -> String {
        let s = format!("{:.*}", decimals, x);
        let s = if s.starts_with('-') && s[1..].chars().all(|c| c == '0' || c == '.') {
            s[1..].to_string()
        } else {
            s
        };
        let (zero, sep) = match self.chain[0].numbering.as_str() {
            "arab" => (0x660u32, '\u{066B}'),
            "arabext" => (0x6F0u32, '\u{066B}'),
            "deva" => (0x966u32, '.'),
            "beng" => (0x9E6u32, '.'),
            _ => return s,
        };
        s.chars()
            .map(|c| match c {
                '0'..='9' => char::from_u32(zero + (c as u32 - '0' as u32)).unwrap(),
                '.' => sep,
                _ => c,
            })
            .collect()
    }

    /// Fills {name} with values. In RTL locales, or when a value holds RTL
    /// text, each value is wrapped in isolates.
    pub fn format(&self, template: &str, params: &[(&str, String)]) -> String {
        let mut out = String::with_capacity(template.len() + 16);
        let mut rest = template;
        while let Some(open) = rest.find('{') {
            out.push_str(&rest[..open]);
            let after = &rest[open + 1..];
            match after.find('}') {
                Some(close)
                    if close > 0
                        && after[..close].chars().all(|c| c.is_ascii_alphanumeric() || c == '_') =>
                {
                    let name = &after[..close];
                    match params.iter().find(|(k, _)| *k == name) {
                        Some((_, v)) => {
                            if self.rtl() || v.chars().any(is_rtl_char) {
                                out.push_str(&isolate(v));
                            } else {
                                out.push_str(v);
                            }
                        }
                        None => {
                            out.push('{');
                            out.push_str(name);
                            out.push('}');
                        }
                    }
                    rest = &after[close + 1..];
                }
                _ => {
                    out.push('{');
                    rest = after;
                }
            }
        }
        out.push_str(rest);
        out
    }

    pub fn t(&self, key: &str) -> String {
        self.tp(key, &[])
    }

    pub fn tp(&self, key: &str, params: &[(&str, String)]) -> String {
        let s = self
            .lookup(|d| d.strings.get(key))
            .unwrap_or_else(|| self.miss(&format!("strings.{key}"), key));
        self.format(&s, params)
    }

    pub fn hw(&self, key: &str) -> String {
        self.hwp(key, &[])
    }

    pub fn hwp(&self, key: &str, params: &[(&str, String)]) -> String {
        let s = self
            .lookup(|d| d.hardware.get(key))
            .unwrap_or_else(|| self.miss(&format!("hardware.{key}"), key));
        self.format(&s, params)
    }

    /// Picks the plural form for `n`. Exact "=N" forms win over CLDR
    /// categories; a missing form falls back to "other", and a missing key
    /// to the fallback locale.
    pub fn plural(&self, key: &str, n: f64, decimals: Option<usize>) -> String {
        for (idx, d) in self.chain.iter().enumerate() {
            if let Some(forms) = d.plurals.get(key) {
                let sel = if idx == 0 {
                    self.select.clone()
                } else {
                    Rc::new(PluralSelector::new(&d.plural_rules))
                };
                let cat = sel.select(n, decimals);
                let exact = format!("={}", js_number_string(n));
                let tpl = forms
                    .get(&exact)
                    .or_else(|| forms.get(cat))
                    .or_else(|| forms.get("other"))
                    .cloned()
                    .unwrap_or_default();
                let shown = self.num(n, decimals.unwrap_or(0));
                // The template is filled with this locale's isolation rules.
                return self.format(&tpl, &[("n", shown)]);
            }
        }
        self.miss(&format!("plurals.{key}"), key)
    }

    pub fn category(&self, n: f64, decimals: Option<usize>) -> &'static str {
        self.select.select(n, decimals)
    }

    pub fn category_str(&self, n: &str) -> &'static str {
        self.select.select_str(n)
    }

    /// Joins items with "listSep"; in RTL each item is isolated.
    pub fn join<S: AsRef<str>>(&self, items: &[S]) -> String {
        let sep = self
            .lookup(|d| d.strings.get("listSep"))
            .unwrap_or_else(|| " · ".to_string());
        items
            .iter()
            .map(|s| s.as_ref())
            .filter(|s| !s.is_empty())
            .map(|s| if self.rtl() { isolate(s) } else { s.to_string() })
            .collect::<Vec<_>>()
            .join(&sep)
    }

    pub fn app(&self, name: &str) -> String {
        self.chain[0].apps.get(name).cloned().unwrap_or_else(|| name.to_string())
    }

    /// Font stacks (ui, hero) for a platform, as CSS family lists.
    pub fn fonts(&self, platform: &str) -> (Option<String>, Option<String>) {
        let f = &self.chain[0].fonts;
        let pick = |k: &str| {
            f.get(&format!("platforms.{platform}.{k}")).or_else(|| f.get(k)).cloned()
        };
        (pick("ui"), pick("hero"))
    }
}

/// All registered locales.
#[derive(Clone, Default)]
pub struct Registry {
    locales: BTreeMap<String, Rc<LocaleData>>,
    base: String,
}

#[derive(Clone, Debug)]
pub struct LocaleInfo {
    pub code: String,
    pub label: String,
    pub name: String,
    pub rtl: bool,
}

impl Registry {
    pub fn new() -> Self {
        Registry { locales: BTreeMap::new(), base: "en".into() }
    }

    /// Registry with every catalog embedded at build time.
    pub fn embedded() -> Self {
        let mut r = Self::new();
        for (code, bytes) in CATALOGS {
            match LocaleData::from_mo(bytes) {
                Ok(d) => r.register(d),
                Err(e) => eprintln!("[i18n] catalog {code}: {e}"),
            }
        }
        r
    }

    pub fn register(&mut self, d: LocaleData) {
        self.locales.insert(d.code.clone(), Rc::new(d));
    }

    /// Loads every <dir>/<code>/LC_MESSAGES/pulse.mo and <dir>/*.mo.
    pub fn load_dir(&mut self, dir: &std::path::Path) -> usize {
        let mut n = 0;
        let mut paths = Vec::new();
        if let Ok(rd) = std::fs::read_dir(dir) {
            for e in rd.flatten() {
                let p = e.path();
                if p.is_dir() {
                    paths.push(p.join("LC_MESSAGES").join("pulse.mo"));
                } else if p.extension().map(|x| x == "mo").unwrap_or(false) {
                    paths.push(p);
                }
            }
        }
        for p in paths {
            if let Ok(b) = std::fs::read(&p) {
                match LocaleData::from_mo(&b) {
                    Ok(d) => {
                        self.register(d);
                        n += 1;
                    }
                    Err(e) => eprintln!("[i18n] {}: {e}", p.display()),
                }
            }
        }
        n
    }

    /// "ar-EG" -> "ar" when only "ar" exists; unknown -> base locale.
    pub fn resolve(&self, code: &str) -> String {
        if self.locales.contains_key(code) {
            return code.to_string();
        }
        let norm = code.split('.').next().unwrap_or("").replace('_', "-");
        if self.locales.contains_key(&norm) {
            return norm;
        }
        let mut parts: Vec<&str> = norm.split('-').collect();
        while parts.len() > 1 {
            parts.pop();
            let c = parts.join("-");
            if self.locales.contains_key(&c) {
                return c;
            }
        }
        if self.locales.contains_key(&self.base) {
            self.base.clone()
        } else {
            self.locales.keys().next().cloned().unwrap_or_default()
        }
    }

    pub fn get(&self, code: &str) -> Locale {
        let mut chain = Vec::new();
        let mut cur = self.resolve(code);
        let mut seen = HashSet::new();
        while let Some(d) = self.locales.get(&cur) {
            if !seen.insert(cur.clone()) {
                break;
            }
            chain.push(d.clone());
            let next = d.fallback.clone().unwrap_or_else(|| self.base.clone());
            if next == cur {
                break;
            }
            cur = next;
        }
        if chain.is_empty() {
            chain.push(Rc::new(LocaleData {
                code: "en".into(),
                label: "EN".into(),
                name: "English".into(),
                numbering: "latn".into(),
                ..Default::default()
            }));
        }
        let select = Rc::new(PluralSelector::new(&chain[0].plural_rules));
        Locale { chain, select, warned: Rc::new(RefCell::new(HashSet::new())) }
    }

    /// Locales in switcher order: base first, then "order", then code.
    pub fn list(&self) -> Vec<LocaleInfo> {
        let mut v: Vec<&Rc<LocaleData>> = self.locales.values().collect();
        let base = self.base.clone();
        let ord = |d: &LocaleData| d.order.unwrap_or(if d.code == base { -1.0 } else { 0.0 });
        v.sort_by(|a, b| {
            ord(a).partial_cmp(&ord(b)).unwrap_or(std::cmp::Ordering::Equal).then(a.code.cmp(&b.code))
        });
        v.iter()
            .map(|d| LocaleInfo { code: d.code.clone(), label: d.label.clone(), name: d.name.clone(), rtl: d.rtl })
            .collect()
    }

    pub fn len(&self) -> usize {
        self.locales.len()
    }

    pub fn is_empty(&self) -> bool {
        self.locales.is_empty()
    }
}
