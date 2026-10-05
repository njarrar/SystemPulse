//! Locale catalog conversion: locales/<code>.json -> gettext .po and .mo.
//!
//! This file is shared by build.rs (which runs it at build time for every
//! locale file) and by the app (which reads the .mo files back). It only
//! depends on serde_json and std, so build.rs can include it with #[path].
//!
//! Every value in the JSON file becomes one gettext message. The message
//! context names the section and the msgid names the key:
//!
//!   msgctxt "strings"        msgid "calm"   msgstr "System Calm"
//!   msgctxt "hardware"       msgid "coreP"  msgstr "P-Cores"
//!   msgctxt "plural:thread"  msgid "few"    msgstr "{n} threads"
//!   msgctxt "pluralRules"    msgid "few"    msgstr "n % 100 = 3..10"
//!   msgctxt "meta"           msgid "dir"    msgstr "rtl"
//!
//! Plural forms stay one message per CLDR category (and per exact "=N"
//! form) so the CLDR rules in the file pick the form at run time. gettext's
//! own Plural-Forms formula is not used.

#![allow(dead_code)]

use serde_json::Value;

#[derive(Clone, Debug, PartialEq)]
pub struct Entry {
    pub ctx: String,
    pub id: String,
    pub text: String,
}

/// Flattens one locale JSON document into catalog entries.
pub fn entries_from_json(doc: &Value) -> Result<Vec<Entry>, String> {
    let obj = doc.as_object().ok_or("locale file is not a JSON object")?;
    let code = obj
        .get("code")
        .and_then(Value::as_str)
        .ok_or("locale file needs a \"code\"")?;
    let mut out = Vec::new();
    let mut push = |ctx: &str, id: &str, text: &str| {
        out.push(Entry { ctx: ctx.to_string(), id: id.to_string(), text: text.to_string() })
    };
    push("meta", "code", code);
    for key in ["label", "name", "dir", "fallback", "numberingSystem"] {
        if let Some(v) = obj.get(key).and_then(Value::as_str) {
            push("meta", key, v);
        }
    }
    if let Some(v) = obj.get("order").and_then(Value::as_f64) {
        push("meta", "order", &v.to_string());
    }
    if let Some(fonts) = obj.get("fonts") {
        flatten_strings("fonts", fonts, &mut |path, v| push("meta", path, v));
    }
    if let Some(rules) = obj.get("pluralRules").and_then(Value::as_object) {
        for (cat, rule) in rules {
            if let Some(r) = rule.as_str() {
                push("pluralRules", cat, r);
            }
        }
    }
    if let Some(plurals) = obj.get("plurals").and_then(Value::as_object) {
        for (key, forms) in plurals {
            if let Some(forms) = forms.as_object() {
                for (form, tpl) in forms {
                    if let Some(t) = tpl.as_str() {
                        push(&format!("plural:{key}"), form, t);
                    }
                }
            }
        }
    }
    for section in ["strings", "hardware", "apps"] {
        if let Some(sec) = obj.get(section).and_then(Value::as_object) {
            for (k, v) in sec {
                if let Some(s) = v.as_str() {
                    push(section, k, s);
                }
            }
        }
    }
    Ok(out)
}

fn flatten_strings(prefix: &str, v: &Value, f: &mut dyn FnMut(&str, &str)) {
    match v {
        Value::String(s) => f(prefix, s),
        Value::Object(m) => {
            for (k, x) in m {
                flatten_strings(&format!("{prefix}.{k}"), x, f);
            }
        }
        _ => {}
    }
}

fn po_escape(s: &str) -> String {
    let mut o = String::with_capacity(s.len() + 2);
    for c in s.chars() {
        match c {
            '\\' => o.push_str("\\\\"),
            '"' => o.push_str("\\\""),
            '\n' => o.push_str("\\n"),
            '\t' => o.push_str("\\t"),
            _ => o.push(c),
        }
    }
    o
}

fn header(code: &str) -> String {
    format!(
        "Project-Id-Version: pulse\nLanguage: {code}\nMIME-Version: 1.0\n\
         Content-Type: text/plain; charset=UTF-8\nContent-Transfer-Encoding: 8bit\n\
         X-Generator: pulse-catalog (from locales/{code}.json)\n"
    )
}

/// Writes a .po file. `source` is the English entry list, used for
/// translator comments.
pub fn write_po(code: &str, entries: &[Entry], source: Option<&[Entry]>) -> String {
    let mut o = String::new();
    o.push_str("# Pulse catalog, generated from locales/");
    o.push_str(code);
    o.push_str(".json. Do not edit; edit the JSON file.\n");
    o.push_str("msgid \"\"\nmsgstr \"\"\n");
    for line in header(code).lines() {
        o.push_str(&format!("\"{}\\n\"\n", po_escape(line)));
    }
    for e in entries {
        o.push('\n');
        if let Some(src) = source {
            if let Some(en) = src.iter().find(|x| x.ctx == e.ctx && x.id == e.id) {
                if en.text != e.text {
                    o.push_str(&format!("#. en: {}\n", en.text.replace('\n', " ")));
                }
            }
        }
        o.push_str(&format!("msgctxt \"{}\"\n", po_escape(&e.ctx)));
        o.push_str(&format!("msgid \"{}\"\n", po_escape(&e.id)));
        o.push_str(&format!("msgstr \"{}\"\n", po_escape(&e.text)));
    }
    o
}

/// Writes a GNU .mo file (little endian, sorted, no hash table).
pub fn write_mo(code: &str, entries: &[Entry]) -> Vec<u8> {
    let mut msgs: Vec<(Vec<u8>, Vec<u8>)> = Vec::with_capacity(entries.len() + 1);
    msgs.push((Vec::new(), header(code).into_bytes()));
    for e in entries {
        let mut k = e.ctx.as_bytes().to_vec();
        k.push(0x04);
        k.extend_from_slice(e.id.as_bytes());
        msgs.push((k, e.text.as_bytes().to_vec()));
    }
    msgs.sort_by(|a, b| a.0.cmp(&b.0));
    msgs.dedup_by(|a, b| a.0 == b.0);
    let n = msgs.len() as u32;
    let orig_tab = 28u32;
    let trans_tab = orig_tab + n * 8;
    let mut data_off = trans_tab + n * 8;
    let mut out = Vec::new();
    for v in [0x950412deu32, 0, n, orig_tab, trans_tab, 0, data_off] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    let mut table_o = Vec::new();
    let mut table_t = Vec::new();
    let mut blob = Vec::new();
    for (k, _) in &msgs {
        table_o.extend_from_slice(&(k.len() as u32).to_le_bytes());
        table_o.extend_from_slice(&data_off.to_le_bytes());
        blob.extend_from_slice(k);
        blob.push(0);
        data_off += k.len() as u32 + 1;
    }
    for (_, v) in &msgs {
        table_t.extend_from_slice(&(v.len() as u32).to_le_bytes());
        table_t.extend_from_slice(&data_off.to_le_bytes());
        blob.extend_from_slice(v);
        blob.push(0);
        data_off += v.len() as u32 + 1;
    }
    out.extend(table_o);
    out.extend(table_t);
    out.extend(blob);
    out
}

/// Reads a .mo file back into entries (the header entry is skipped).
pub fn read_mo(bytes: &[u8]) -> Result<Vec<Entry>, String> {
    if bytes.len() < 28 {
        return Err("mo file too short".into());
    }
    let magic = u32::from_le_bytes(bytes[0..4].try_into().unwrap());
    let le = match magic {
        0x950412de => true,
        0xde120495 => false,
        _ => return Err("not a mo file".into()),
    };
    let rd = |off: usize| -> Result<u32, String> {
        let b: [u8; 4] = bytes
            .get(off..off + 4)
            .ok_or("mo file truncated")?
            .try_into()
            .unwrap();
        Ok(if le { u32::from_le_bytes(b) } else { u32::from_be_bytes(b) })
    };
    let n = rd(8)? as usize;
    let ot = rd(12)? as usize;
    let tt = rd(16)? as usize;
    let s = |len: u32, off: u32| -> Result<String, String> {
        let b = bytes
            .get(off as usize..(off + len) as usize)
            .ok_or("mo string out of range")?;
        String::from_utf8(b.to_vec()).map_err(|_| "mo string is not UTF-8".to_string())
    };
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let k = s(rd(ot + i * 8)?, rd(ot + i * 8 + 4)?)?;
        let v = s(rd(tt + i * 8)?, rd(tt + i * 8 + 4)?)?;
        if k.is_empty() {
            continue;
        }
        let (ctx, id) = match k.split_once('\u{4}') {
            Some((c, i)) => (c.to_string(), i.to_string()),
            None => (String::new(), k),
        };
        out.push(Entry { ctx, id, text: v });
    }
    Ok(out)
}
