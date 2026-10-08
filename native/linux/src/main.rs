//! Pulse for GNOME: a top bar entry and a 420px flyout with live system
//! vitals from /proc, /sys and netlink.

mod sni;
mod ui;
mod update;

use adw::prelude::*;
use pulse::i18n::Registry;
use std::path::PathBuf;
use std::time::Duration;
use ui::state::{Kind, State, View};

const HELP: &str = "Usage: pulse [options]
  --show-flyout        open the flyout at start and keep it open
  --no-tray            do not publish the top bar entry
  --lang CODE          use this language for this run (default: the saved
                       choice, else LANGUAGE, LC_ALL or LANG)
  --view NAME          cpu, mem, nrg, thm, gpu, ssd, net, settings or app:<key>
  --theme light|dark   force a theme
  --locales-dir DIR    load extra catalogs (<DIR>/<code>/LC_MESSAGES/pulse.mo)
  --sim-hog            start with Simulate CPU hog on
  --sim-charging       start with Charging on
  --hog-after SECS     time above 50% CPU before the hog alert (default 120)
  --screenshot FILE    write the flyout to FILE (PNG) after --screenshot-delay
  --screenshot-delay S seconds to wait before the screenshot (default 4)
  --update-check       print \"current=<version> latest=<version>\" and exit
  --update             install a newer Pulse over this binary and exit
                       (the manifest comes from PULSE_UPDATE_URL, default
                       the GitHub repository)
  -h, --help           show this help";

fn main() -> gtk::glib::ExitCode {
    let mut args = std::env::args().skip(1);
    let mut opts = ui::Options { show: false, pinned: false, view: None, dark: None, tray: true, screenshot: None };
    let mut lang = None;
    let mut extra: Vec<PathBuf> = Vec::new();
    let mut hog_after = 120.0;
    let mut shot: Option<PathBuf> = None;
    let mut delay = 4u64;
    let (mut sim_hog, mut sim_chg) = (false, false);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--show-flyout" => {
                opts.show = true;
                opts.pinned = true;
            }
            "--no-tray" => opts.tray = false,
            "--sim-hog" => sim_hog = true,
            "--sim-charging" => sim_chg = true,
            "--lang" => lang = args.next(),
            "--view" => {
                let v = args.next().unwrap_or_default();
                opts.view = Some(match v.as_str() {
                    "settings" => View::Settings,
                    s if s.starts_with("app:") => View::App(s[4..].to_string()),
                    s => Kind::parse(s).map(View::Detail).unwrap_or(View::Overview),
                });
            }
            "--theme" => opts.dark = args.next().map(|t| t == "dark"),
            "--locales-dir" => extra.extend(args.next().map(PathBuf::from)),
            "--hog-after" => hog_after = args.next().and_then(|s| s.parse().ok()).unwrap_or(hog_after),
            "--screenshot" => shot = args.next().map(PathBuf::from),
            "--screenshot-delay" => delay = args.next().and_then(|s| s.parse().ok()).unwrap_or(delay),
            "--update-check" => return gtk::glib::ExitCode::from(update::cli(false) as u8),
            "--update" => return gtk::glib::ExitCode::from(update::cli(true) as u8),
            "-h" | "--help" => {
                println!("{HELP}");
                return gtk::glib::ExitCode::SUCCESS;
            }
            other => {
                eprintln!("pulse: unknown option {other}\n{HELP}");
                return gtk::glib::ExitCode::FAILURE;
            }
        }
    }
    if let Some(p) = shot {
        opts.screenshot = Some((p, delay));
        opts.show = true;
        opts.pinned = true;
    }

    let app = adw::Application::builder()
        .application_id("io.github.pulse.Monitor")
        .flags(gtk::gio::ApplicationFlags::NON_UNIQUE)
        .build();
    let opts = std::rc::Rc::new(std::cell::RefCell::new(Some(opts)));
    app.connect_activate(move |app| {
        let Some(opts) = opts.borrow_mut().take() else { return };
        let mut reg = Registry::embedded();
        for d in &extra {
            reg.load_dir(d);
        }
        let pref = ui::prefs::language();
        let code = reg.resolve(&lang.clone().or(pref.clone()).unwrap_or_else(ui::prefs::system_language));
        let mut st = State::new(reg, code, Duration::from_secs_f64(hog_after));
        st.sim_hog = sim_hog;
        st.sim_charging = sim_chg;
        st.lang_pref = if lang.is_some() { Some(st.lang.clone()) } else { pref };
        // stays alive with only the top bar entry
        std::mem::forget(app.hold());
        let ctl = ui::Ctl::new(app, st, &opts);
        if let Some((path, delay)) = opts.screenshot.clone() {
            gtk::glib::timeout_add_local_once(Duration::from_secs(delay), move || {
                if !ctl.snapshot_png(&path) {
                    eprintln!("pulse: screenshot failed");
                }
                println!("{}", path.display());
                ctl.quit();
            });
        }
    });
    app.run_with_args::<&str>(&[])
}
