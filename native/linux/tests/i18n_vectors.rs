//! Checks the Rust port of the locale engine against
//! i18n/test/fixtures/vectors.json, through the generated .mo catalogs.

use pulse::catalog;
use pulse::i18n::{compile_rule, operands_str, LocaleData, Registry, FSI, PDI};
use serde_json::Value;
use std::path::PathBuf;

fn repo() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn vectors() -> Value {
    let t = std::fs::read_to_string(repo().join("i18n/test/fixtures/vectors.json")).unwrap();
    serde_json::from_str(&t).unwrap()
}

#[test]
fn embedded_catalogs_cover_every_locale_file() {
    let reg = Registry::embedded();
    let mut codes = Vec::new();
    for e in std::fs::read_dir(repo().join("locales")).unwrap().flatten() {
        let t = std::fs::read_to_string(e.path()).unwrap();
        let v: Value = serde_json::from_str(&t).unwrap();
        if let Some(c) = v.get("code").and_then(Value::as_str) {
            codes.push(c.to_string());
        }
    }
    assert!(codes.len() >= 2);
    let listed: Vec<String> = reg.list().into_iter().map(|l| l.code).collect();
    for c in &codes {
        assert!(listed.contains(c), "{c} missing from embedded catalogs");
    }
    assert_eq!(listed[0], "en", "base locale comes first");
}

#[test]
fn plural_categories_match_vectors() {
    let reg = Registry::embedded();
    let v = vectors();
    let mut checked = 0;
    for (code, data) in v.as_object().unwrap() {
        let loc = reg.get(code);
        for pair in data["categories"].as_array().unwrap() {
            let n = pair[0].as_str().unwrap();
            let want = pair[1].as_str().unwrap();
            let got = loc.category_str(n);
            assert_eq!(got, want, "{code} {n}");
            checked += 1;
        }
    }
    assert!(checked > 400);
}

#[test]
fn rendered_samples_match_vectors() {
    let reg = Registry::embedded();
    let v = vectors();
    for (code, data) in v.as_object().unwrap() {
        let loc = reg.get(code);
        for (key, want) in data["samples"].as_object().unwrap() {
            let want = want.as_str().unwrap();
            let (name, arg) = key.split_once(':').unwrap();
            let got = if let Ok(n) = arg.parse::<f64>() {
                loc.plural(name, n, None)
            } else {
                let params: Vec<(&str, String)> = arg
                    .split(',')
                    .map(|kv| {
                        let (k, v) = kv.split_once('=').unwrap();
                        (k, v.to_string())
                    })
                    .collect();
                loc.tp(name, &params)
            };
            assert_eq!(got, want, "{code} {key}");
        }
    }
}

#[test]
fn rtl_values_are_isolated_and_ltr_values_are_not() {
    let reg = Registry::embedded();
    let ar = reg.get("ar");
    let en = reg.get("en");
    let s = ar.tp("used", &[("pct", "49%".into())]);
    assert!(s.contains(&format!("{FSI}49%{PDI}")));
    assert_eq!(en.tp("used", &[("pct", "49%".into())]), "49% used");
    // An Arabic value inside an English string is still isolated.
    let mixed = en.tp("endNamed", &[("app", "الملفات".into())]);
    assert!(mixed.contains(FSI));
    assert_eq!(ar.join(&["a", "b"]), format!("{FSI}a{PDI} · {FSI}b{PDI}"));
}

#[test]
fn exact_forms_win_and_zero_category_works() {
    let reg = Registry::embedded();
    assert_eq!(reg.get("en").plural("ended", 0.0, None), "No apps ended yet");
    assert_eq!(reg.get("en").plural("ended", 2.0, None), "2 apps ended this session");
    assert_eq!(reg.get("ar").category(0.0, None), "zero");
    assert_eq!(reg.get("ar").category(101.0, None), "other");
    assert_eq!(reg.get("ar").category(103.0, None), "few");
    assert_eq!(reg.get("ar").category(111.0, None), "many");
}

#[test]
fn resolve_and_fallback() {
    let reg = Registry::embedded();
    assert_eq!(reg.resolve("ar-EG"), "ar");
    assert_eq!(reg.resolve("ar_EG.UTF-8"), "ar");
    assert_eq!(reg.resolve("xx"), "en");
    // A key missing in a locale falls back to English, then to the key.
    let mut d = LocaleData::from_entries(&[catalog::Entry { ctx: "meta".into(), id: "code".into(), text: "xx".into() }]).unwrap();
    d.strings.insert("calm".into(), "Calme".into());
    let mut r = Registry::embedded();
    r.register(d);
    let x = r.get("xx");
    assert_eq!(x.t("calm"), "Calme");
    assert_eq!(x.t("quit"), "Quit");
    assert_eq!(x.t("noSuchKey"), "noSuchKey");
    assert_eq!(x.plural("thread", 3.0, None), "3 threads");
}

#[test]
fn rule_compiler_handles_cldr_syntax() {
    let r = compile_rule("n % 10 = 2..4 and n % 100 != 12..14 @integer 2~4, 22~24").unwrap();
    assert!(r.eval(&operands_str("22")));
    assert!(!r.eval(&operands_str("12")));
    let r = compile_rule("i = 0,1 or n is not 5 and n not in 6..8").unwrap();
    assert!(r.eval(&operands_str("1.5")));
    let r = compile_rule("n within 0..2 and n != 2").unwrap();
    assert!(r.eval(&operands_str("1.5")));
    assert!(compile_rule("q = 1").is_err());
    let o = operands_str("1.20");
    assert_eq!((o.i, o.v, o.w, o.f, o.t), (1.0, 2.0, 1.0, 20.0, 2.0));
    let o = operands_str("1.2c3");
    assert_eq!((o.n, o.c), (1200.0, 3.0));
}

#[test]
fn pseudo_locale_fixture_loads_through_the_catalog() {
    let t = std::fs::read_to_string(repo().join("i18n/test/fixtures/en-XA.json")).unwrap();
    let v: Value = serde_json::from_str(&t).unwrap();
    let entries = catalog::entries_from_json(&v).unwrap();
    let mo = catalog::write_mo("en-XA", &entries);
    let back = catalog::read_mo(&mo).unwrap();
    assert_eq!(back.len(), entries.len());
    let mut r = Registry::embedded();
    r.register(LocaleData::from_mo(&mo).unwrap());
    assert_eq!(r.list().len(), 3);
    let xa = r.get("en-XA");
    assert!(xa.t("calm").starts_with('['));
    assert!(xa.plural("ended", 0.0, None).contains("Nó"));
}

#[test]
fn po_output_is_valid_for_msgfmt() {
    let t = std::fs::read_to_string(repo().join("locales/ar.json")).unwrap();
    let v: Value = serde_json::from_str(&t).unwrap();
    let po = catalog::write_po("ar", &catalog::entries_from_json(&v).unwrap(), None);
    assert!(po.contains("msgctxt \"plural:thread\"\nmsgid \"few\"\nmsgstr \"{n} خيوط\""));
    let dir = std::env::temp_dir().join(format!("pulse-po-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("ar.po"), &po).unwrap();
    // msgfmt is optional on build machines; check with it when present.
    if let Ok(out) = std::process::Command::new("msgfmt").arg("--check").arg("-o").arg(dir.join("ar.mo")).arg(dir.join("ar.po")).output() {
        assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
        let ours = catalog::read_mo(&std::fs::read(dir.join("ar.mo")).unwrap()).unwrap();
        assert!(ours.iter().any(|e| e.ctx == "strings" && e.id == "calm" && e.text == "النظام هادئ"));
    }
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn numbers_follow_numbering_system() {
    let reg = Registry::embedded();
    assert_eq!(reg.get("ar").num(14.75, 1), "14.8");
    let mut d = LocaleData::from_entries(&[catalog::Entry { ctx: "meta".into(), id: "code".into(), text: "fa".into() }]).unwrap();
    d.numbering = "arabext".into();
    let mut r = Registry::new();
    r.register(d);
    assert_eq!(r.get("fa").num(12.5, 1), "۱۲٫۵");
}
