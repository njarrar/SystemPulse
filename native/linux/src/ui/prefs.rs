//! Saved choices. Pulse ships as a single binary with no installed
//! GSettings schema, so it keeps its settings in a GLib key file:
//! $XDG_CONFIG_HOME/pulse/settings.ini.

use gtk::glib;
use std::path::PathBuf;

const GROUP: &str = "Pulse";

fn path() -> PathBuf {
    glib::user_config_dir().join("pulse").join("settings.ini")
}

fn load() -> glib::KeyFile {
    let kf = glib::KeyFile::new();
    let _ = kf.load_from_file(path(), glib::KeyFileFlags::KEEP_COMMENTS);
    kf
}

/// The saved language: None means "Match system".
pub fn language() -> Option<String> {
    load().string(GROUP, "language").ok().map(|s| s.to_string()).filter(|s| !s.is_empty() && s != "system")
}

pub fn set_language(code: Option<&str>) {
    let kf = load();
    kf.set_string(GROUP, "language", code.unwrap_or("system"));
    let p = path();
    if let Some(d) = p.parent() {
        let _ = std::fs::create_dir_all(d);
    }
    if let Err(e) = kf.save_to_file(&p) {
        eprintln!("pulse: could not save settings: {e}");
    }
}

/// The OS language from LANGUAGE, LC_ALL, LC_MESSAGES or LANG.
pub fn system_language() -> String {
    for k in ["LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"] {
        if let Ok(v) = std::env::var(k) {
            let first = v.split(':').next().unwrap_or("").to_string();
            if !first.is_empty() && first != "C" && first != "POSIX" && !first.starts_with("C.") {
                return first;
            }
        }
    }
    "en".into()
}
