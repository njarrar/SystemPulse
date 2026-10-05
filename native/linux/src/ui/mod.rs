//! The flyout window and its controller.
//!
//! Widgets for the current view are built once per structural change
//! (view, language, theme, unit). Each widget registers a small "bind"
//! closure; every poll runs the binds to refresh text, bars and charts in
//! place, so hover, focus and tooltips survive updates.

pub mod draw;
pub mod fmt;
pub mod prefs;
pub mod state;
pub mod style;
mod views;

use crate::sni;
use adw::prelude::*;
use fmt::Fmt;
use gtk::{gdk, gio, glib};
use pulse::i18n::Locale;
use pulse::telemetry::{AppGroup, Sampler};
use state::{State, View};
use std::cell::{Cell, RefCell};
use std::rc::Rc;
use std::time::{Duration, Instant};
use style::Pal;

pub const POLL: Duration = Duration::from_millis(1500);
pub const TOAST_MS: u64 = 2800;
pub const WIDTH: i32 = 420;

pub struct Ctx<'a> {
    pub st: &'a State,
    pub lc: &'a Locale,
    pub f: &'a Fmt,
    pub pal: &'a Pal,
}

pub type Bind = Box<dyn Fn(&Ctx)>;

pub struct Options {
    pub show: bool,
    pub pinned: bool,
    pub view: Option<View>,
    pub dark: Option<bool>,
    pub tray: bool,
    pub screenshot: Option<(std::path::PathBuf, u64)>,
}

pub struct Ctl {
    pub app: adw::Application,
    pub win: gtk::ApplicationWindow,
    pub st: RefCell<State>,
    sampler: RefCell<Sampler>,
    binds: RefCell<Vec<Bind>>,
    css: gtk::CssProvider,
    opaque: Cell<bool>,
    pinned: bool,
    tray: RefCell<Option<sni::Tray>>,
    toast_gen: Cell<u64>,
    built: RefCell<Option<(View, String, bool, bool)>>,
}

impl Ctl {
    pub fn new(app: &adw::Application, st: State, opts: &Options) -> Rc<Ctl> {
        let win = gtk::ApplicationWindow::builder()
            .application(app)
            .title("Pulse")
            .decorated(false)
            .resizable(false)
            .default_width(WIDTH)
            .build();
        win.add_css_class("pulse-window");
        let css = gtk::CssProvider::new();
        gtk::style_context_add_provider_for_display(&gdk::Display::default().unwrap(), &css, gtk::STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        let ctl = Rc::new(Ctl {
            app: app.clone(),
            win,
            st: RefCell::new(st),
            sampler: RefCell::new(Sampler::new("/")),
            binds: RefCell::new(Vec::new()),
            css,
            opaque: Cell::new(true),
            pinned: opts.pinned,
            tray: RefCell::new(None),
            toast_gen: Cell::new(0),
            built: RefCell::new(None),
        });
        if let Some(d) = opts.dark {
            ctl.st.borrow_mut().dark = d;
        } else {
            ctl.st.borrow_mut().dark = adw::StyleManager::default().is_dark();
        }
        if let Some(v) = &opts.view {
            ctl.st.borrow_mut().view = v.clone();
        }
        ctl.install_actions();

        // Esc closes; losing focus closes unless pinned.
        let keys = gtk::EventControllerKey::new();
        let c2 = ctl.clone();
        keys.connect_key_pressed(move |_, key, _, _| {
            if key == gdk::Key::Escape {
                let back = c2.st.borrow().view != View::Overview;
                if back {
                    c2.set_view(View::Overview);
                } else if !c2.pinned {
                    c2.win.set_visible(false);
                }
                return glib::Propagation::Stop;
            }
            glib::Propagation::Proceed
        });
        ctl.win.add_controller(keys);
        let c3 = ctl.clone();
        ctl.win.connect_is_active_notify(move |w| {
            if !w.is_active() && !c3.pinned && w.is_visible() {
                let w2 = w.clone();
                glib::timeout_add_local_once(Duration::from_millis(150), move || {
                    if !w2.is_active() {
                        w2.set_visible(false);
                    }
                });
            }
        });
        let c4 = ctl.clone();
        ctl.win.connect_map(move |_| {
            let c5 = c4.clone();
            glib::idle_add_local_once(move || c5.place());
        });
        ctl.win.connect_close_request(|w| {
            w.set_visible(false);
            glib::Propagation::Stop
        });

        // first two samples give rates; then poll.
        ctl.sample_now();
        let c6 = ctl.clone();
        glib::timeout_add_local(POLL, move || {
            if c6.st.borrow().live {
                c6.sample_now();
            }
            glib::ControlFlow::Continue
        });

        if opts.tray {
            let c7 = ctl.clone();
            *ctl.tray.borrow_mut() = sni::Tray::spawn(move |ev| match ev {
                sni::TrayEvent::Activate => c7.toggle(),
            });
        }
        ctl.rebuild();
        if opts.show {
            ctl.win.present();
            ctl.update();
        }
        ctl
    }

    fn install_actions(self: &Rc<Self>) {
        let cur = self.st.borrow().lang.clone();
        let act = gio::SimpleAction::new_stateful("lang", Some(glib::VariantTy::STRING), &cur.to_variant());
        let c = self.clone();
        act.connect_activate(move |a, v| {
            if let Some(code) = v.and_then(|v| v.get::<String>()) {
                a.set_state(&code.to_variant());
                c.set_lang(&code);
            }
        });
        self.app.add_action(&act);
    }

    pub fn locale(&self) -> Locale {
        let st = self.st.borrow();
        st.registry.get(&st.lang)
    }

    pub fn fmt(&self) -> Fmt {
        Fmt { lc: self.locale(), unit_f: self.st.borrow().unit_f }
    }

    pub fn pal(&self) -> Pal {
        if self.st.borrow().dark { Pal::dark() } else { Pal::light() }
    }

    pub fn toggle(self: &Rc<Self>) {
        if self.win.is_visible() {
            self.win.set_visible(false);
        } else {
            self.st.borrow_mut().view = View::Overview;
            self.win.present();
            self.rebuild();
        }
    }

    /// X11: anchor the flyout to the top-end corner under the top bar.
    /// Wayland gives clients no way to place a window; GNOME centers it.
    fn place(&self) {
        let Some(surface) = self.win.surface() else { return };
        let display = WidgetExt::display(&self.win);
        let Ok(xs) = surface.clone().downcast::<gdk4_x11::X11Surface>() else { return };
        let Ok(xd) = display.clone().downcast::<gdk4_x11::X11Display>() else { return };
        let Some(mon) = display.monitor_at_surface(&surface) else { return };
        let g = mon.geometry();
        let w = self.win.width();
        let rtl = self.locale().rtl();
        let x = if rtl { g.x() + 8 } else { g.x() + g.width() - w - 8 };
        let y = g.y() + 32;
        #[link(name = "X11")]
        extern "C" {
            fn XMoveWindow(d: *mut std::ffi::c_void, w: std::ffi::c_ulong, x: i32, y: i32) -> i32;
            fn XFlush(d: *mut std::ffi::c_void) -> i32;
        }
        unsafe {
            let dpy = xd.xdisplay() as *mut std::ffi::c_void;
            XMoveWindow(dpy, xs.xid() as std::ffi::c_ulong, x, y);
            XFlush(dpy);
        }
    }

    pub fn sample_now(self: &Rc<Self>) {
        let snap = self.sampler.borrow_mut().sample();
        self.st.borrow_mut().ingest(snap, Instant::now());
        self.update();
    }

    /// Rebuilds the widget tree when the view, language, theme or unit
    /// changed; otherwise only refreshes.
    pub fn rebuild(self: &Rc<Self>) {
        let key = {
            let st = self.st.borrow();
            (st.view.clone(), st.lang.clone(), st.dark, st.unit_f)
        };
        let lc = self.locale();
        let pal = self.pal();
        let rtl = lc.rtl();
        gtk::Widget::set_default_direction(if rtl { gtk::TextDirection::Rtl } else { gtk::TextDirection::Ltr });
        let (ui, hero) = fonts(&lc);
        self.opaque.set(!WidgetExt::display(&self.win).is_composited());
        self.css.load_from_string(&style::css(&pal, &ui, &hero, self.opaque.get(), rtl));
        adw::StyleManager::default().set_color_scheme(if pal.dark { adw::ColorScheme::ForceDark } else { adw::ColorScheme::ForceLight });
        views::set_rtl(rtl);
        let mut b = views::B::new(self.clone(), lc.clone(), self.fmt(), pal);
        let root = views::build(&mut b);
        self.win.set_child(Some(&root));
        self.win.set_direction(if rtl { gtk::TextDirection::Rtl } else { gtk::TextDirection::Ltr });
        *self.binds.borrow_mut() = b.binds;
        *self.built.borrow_mut() = Some(key);
        self.update();
        // the window shrinks or grows with the view
        self.win.set_default_size(WIDTH, -1);
        let c = self.clone();
        glib::idle_add_local_once(move || c.place());
    }

    pub fn update(self: &Rc<Self>) {
        let st = self.st.borrow();
        let lc = st.registry.get(&st.lang);
        let f = Fmt { lc: lc.clone(), unit_f: st.unit_f };
        let pal = if st.dark { Pal::dark() } else { Pal::light() };
        let ctx = Ctx { st: &st, lc: &lc, f: &f, pal: &pal };
        // A hidden flyout skips widget work; it refreshes when shown.
        if self.win.is_visible() {
            for b in self.binds.borrow().iter() {
                b(&ctx);
            }
        }
        if let Some(t) = self.tray.borrow().as_ref() {
            t.update(views::tray_state(&ctx));
        }
    }

    fn needs_rebuild(&self) -> bool {
        let st = self.st.borrow();
        self.built.borrow().as_ref() != Some(&(st.view.clone(), st.lang.clone(), st.dark, st.unit_f))
    }

    pub fn refresh(self: &Rc<Self>) {
        if self.needs_rebuild() {
            self.rebuild();
        } else {
            self.update();
        }
    }

    pub fn set_view(self: &Rc<Self>, v: View) {
        {
            let mut st = self.st.borrow_mut();
            st.view = v;
        }
        self.refresh();
    }

    /// Switches language live and saves the choice. None follows the OS
    /// language, with English as the fallback.
    pub fn choose_lang(self: &Rc<Self>, code: Option<String>) {
        prefs::set_language(code.as_deref());
        let want = code.clone().unwrap_or_else(prefs::system_language);
        let resolved = self.st.borrow().registry.resolve(&want);
        {
            let mut st = self.st.borrow_mut();
            st.lang = resolved;
            st.lang_pref = code;
        }
        if let Some(a) = self.app.lookup_action("lang").and_then(|a| a.downcast::<gio::SimpleAction>().ok()) {
            a.set_state(&self.st.borrow().lang.to_variant());
        }
        self.refresh();
    }

    pub fn set_lang(self: &Rc<Self>, code: &str) {
        self.choose_lang(Some(code.to_string()));
    }

    pub fn toggle_theme(self: &Rc<Self>) {
        let d = !self.st.borrow().dark;
        self.st.borrow_mut().dark = d;
        self.refresh();
    }

    pub fn set_sort(self: &Rc<Self>, k: state::SortKey) {
        self.st.borrow_mut().sort = k;
        self.update();
    }

    pub fn set_live(self: &Rc<Self>, on: bool) {
        self.st.borrow_mut().live = on;
        self.update();
    }

    pub fn set_unit(self: &Rc<Self>, f: bool) {
        self.st.borrow_mut().unit_f = f;
        self.refresh();
    }

    pub fn request_end(self: &Rc<Self>, app: AppGroup) {
        self.st.borrow_mut().confirm = Some(app);
        self.st.borrow_mut().toast = None;
        self.update();
    }

    pub fn cancel_end(self: &Rc<Self>) {
        self.st.borrow_mut().confirm = None;
        self.update();
    }

    /// Sends SIGTERM to every process of the app, then refreshes readings
    /// right away (also when live updates are paused).
    pub fn confirm_end(self: &Rc<Self>) {
        let Some(app) = self.st.borrow_mut().confirm.take() else { return };
        let launch = pulse::telemetry::procs::launch_info(std::path::Path::new("/"), &app.name, &app.pids);
        let n = self.sampler.borrow().end_app(&app.pids);
        let lc = self.locale();
        if n == 0 {
            self.toast(lc.tp("endFailed", &[("app", lc.app(&app.name))]));
        } else {
            {
                let mut st = self.st.borrow_mut();
                if let Some(l) = launch {
                    st.ended.push(l);
                }
                if st.view == View::App(app.key.clone()) {
                    st.view = View::Overview;
                }
            }
            self.toast(lc.tp("toastEnded", &[("app", lc.app(&app.name))]));
        }
        self.refresh();
        let c = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(400), move || c.sample_now());
    }

    /// Starts again every app Pulse ended this session.
    pub fn restore_apps(self: &Rc<Self>) {
        let list = std::mem::take(&mut self.st.borrow_mut().ended);
        if list.is_empty() {
            return;
        }
        for l in &list {
            pulse::telemetry::procs::relaunch(l);
        }
        let lc = self.locale();
        self.toast(lc.t("toastRestored"));
        let c = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(600), move || c.sample_now());
    }

    pub fn set_sim(self: &Rc<Self>, hog: Option<bool>, charging: Option<bool>) {
        {
            let mut st = self.st.borrow_mut();
            if let Some(h) = hog {
                st.sim_hog = h;
                st.hog_dismissed = None;
            }
            if let Some(c) = charging {
                st.sim_charging = c;
            }
            st.resync(Instant::now());
        }
        self.update();
    }

    pub fn dismiss_hog(self: &Rc<Self>) {
        let k = self.st.borrow().hog.as_ref().map(|h| h.key.clone());
        self.st.borrow_mut().hog_dismissed = k;
        self.update();
    }

    pub fn toast(self: &Rc<Self>, text: String) {
        self.st.borrow_mut().toast = Some(text);
        let gen = self.toast_gen.get() + 1;
        self.toast_gen.set(gen);
        self.update();
        let c = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(TOAST_MS), move || {
            if c.toast_gen.get() == gen {
                c.st.borrow_mut().toast = None;
                c.update();
            }
        });
    }

    pub fn open_monitor(self: &Rc<Self>) {
        let lc = self.locale();
        let launched = gio::AppInfo::create_from_commandline("gnome-system-monitor", None, gio::AppInfoCreateFlags::NONE)
                .map(|a| a.launch(&[], gio::AppLaunchContext::NONE).is_ok())
                .unwrap_or(false);
        if !launched {
            eprintln!("pulse: gnome-system-monitor is not installed");
        }
        self.toast(lc.tp("toastMonitor", &[("monitor", lc.hw("systemMonitor"))]));
    }

    pub fn quit(&self) {
        self.app.quit();
    }

    /// Writes the flyout as a PNG (used for checks without a screen grab).
    pub fn snapshot_png(&self, path: &std::path::Path) -> bool {
        let Some(child) = self.win.child() else { return false };
        let paintable = gtk::WidgetPaintable::new(Some(&child));
        let (w, h) = (child.width() as f64, child.height() as f64);
        if w < 1.0 || h < 1.0 {
            return false;
        }
        let snap = gtk::Snapshot::new();
        paintable.snapshot(&snap, w, h);
        let Some(node) = snap.to_node() else { return false };
        let Some(native) = self.win.native() else { return false };
        let Some(renderer) = native.renderer() else { return false };
        let tex = renderer.render_texture(&node, Some(&gtk::graphene::Rect::new(0.0, 0.0, w as f32, h as f32)));
        tex.save_to_png(path).is_ok()
    }
}

/// UI and hero font stacks: locale fonts first for UI text, the platform
/// stack first for hero numerals (as in the prototype).
pub fn fonts(lc: &Locale) -> (String, String) {
    let ui_latin = "\"Cantarell\", \"Ubuntu\", \"Noto Sans\", sans-serif";
    let hero_latin = "\"Ubuntu\", \"Cantarell\", \"Noto Sans\"";
    let (lui, lhero) = lc.fonts("linux");
    let strip = |s: &str| {
        s.split(',')
            .map(str::trim)
            .filter(|x| !matches!(*x, "system-ui" | "sans-serif" | "serif" | "ui-sans-serif"))
            .collect::<Vec<_>>()
            .join(", ")
    };
    let ui = match lui {
        Some(u) => format!("{}, {}", strip(&u), ui_latin),
        None => ui_latin.to_string(),
    };
    let hero = match lhero {
        Some(h) => format!("{}, {}, sans-serif", hero_latin, strip(&h)),
        None => format!("{hero_latin}, sans-serif"),
    };
    (ui, hero)
}
