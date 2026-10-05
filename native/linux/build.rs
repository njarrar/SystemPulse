// Build step: turns every locales/*.json file into a gettext catalog
// (pulse.po and pulse.mo) under OUT_DIR/locale/<code>/LC_MESSAGES/ and
// embeds the .mo files in the binary. Adding a locale file needs no code
// change: it is picked up here on the next build.

#[path = "src/catalog.rs"]
mod catalog;

use std::{env, fs, path::PathBuf};

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let dir = env::var("PULSE_LOCALES_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| manifest.join("../../locales"));
    println!("cargo:rerun-if-env-changed=PULSE_LOCALES_DIR");
    println!("cargo:rerun-if-changed={}", dir.display());
    println!("cargo:rerun-if-changed=src/catalog.rs");
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());

    let mut files: Vec<PathBuf> = fs::read_dir(&dir)
        .unwrap_or_else(|e| panic!("cannot read locales at {}: {e}", dir.display()))
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().map(|x| x == "json").unwrap_or(false))
        .collect();
    files.sort();

    let mut docs = Vec::new();
    for f in &files {
        println!("cargo:rerun-if-changed={}", f.display());
        let text = fs::read_to_string(f).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&text)
            .unwrap_or_else(|e| panic!("{}: {e}", f.display()));
        // schema.json and other helper files have no "code".
        if doc.get("code").and_then(|c| c.as_str()).is_none() {
            continue;
        }
        let entries = catalog::entries_from_json(&doc).unwrap();
        docs.push((doc["code"].as_str().unwrap().to_string(), entries));
    }
    let source = docs.iter().find(|(c, _)| c == "en").map(|(_, e)| e.clone());

    let mut rs = String::from("pub static CATALOGS: &[(&str, &[u8])] = &[\n");
    for (code, entries) in &docs {
        let d = out.join("locale").join(code).join("LC_MESSAGES");
        fs::create_dir_all(&d).unwrap();
        let po = catalog::write_po(code, entries, source.as_deref());
        fs::write(d.join("pulse.po"), po).unwrap();
        let mo_path = d.join("pulse.mo");
        fs::write(&mo_path, catalog::write_mo(code, entries)).unwrap();
        rs.push_str(&format!("    ({:?}, include_bytes!({:?})),\n", code, mo_path.display().to_string()));
    }
    rs.push_str("];\n");
    fs::write(out.join("catalogs.rs"), rs).unwrap();
}
