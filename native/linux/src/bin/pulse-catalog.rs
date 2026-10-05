//! Converts locale JSON files into gettext catalogs.
//!
//!   pulse-catalog <locale.json>... <out-dir>
//!
//! Writes <out-dir>/<code>/LC_MESSAGES/pulse.po and pulse.mo for each file.
//! The build runs the same code for locales/*.json; use this tool for extra
//! files such as the en-XA pseudo-locale, then start Pulse with
//! --locales-dir <out-dir>.

use pulse::catalog;
use std::{env, fs, path::PathBuf, process::exit};

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.len() < 2 {
        eprintln!("usage: pulse-catalog <locale.json>... <out-dir>");
        exit(2);
    }
    let out = PathBuf::from(args.last().unwrap());
    for f in &args[..args.len() - 1] {
        let text = fs::read_to_string(f).unwrap_or_else(|e| {
            eprintln!("{f}: {e}");
            exit(1)
        });
        let doc: serde_json::Value = serde_json::from_str(&text).unwrap_or_else(|e| {
            eprintln!("{f}: {e}");
            exit(1)
        });
        let Some(code) = doc.get("code").and_then(|c| c.as_str()) else {
            eprintln!("{f}: no \"code\", skipped");
            continue;
        };
        let entries = catalog::entries_from_json(&doc).unwrap();
        let d = out.join(code).join("LC_MESSAGES");
        fs::create_dir_all(&d).unwrap();
        fs::write(d.join("pulse.po"), catalog::write_po(code, &entries, None)).unwrap();
        fs::write(d.join("pulse.mo"), catalog::write_mo(code, &entries)).unwrap();
        println!("{code}: {} messages -> {}", entries.len(), d.display());
    }
}
