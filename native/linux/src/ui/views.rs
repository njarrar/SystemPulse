//! Widget trees for the overview, the 10-minute detail views and settings.

use super::draw::{self, Bar, Chart, Meter, SegBar, Spark};
use super::fmt::{Fmt, DASH};
use super::state::{Kind, SortKey, State, View, LONG_N, SPARK_N};
use super::style::{oklch, Pal, Rgba};
use super::{Bind, Ctl, Ctx};
use crate::sni::TrayState;
use gtk::prelude::*;
use gtk::{glib, pango};
use pulse::i18n::Locale;
use pulse::telemetry::{power::BattState, AppGroup, CoreGroups};
use std::cell::RefCell;
use std::rc::Rc;

pub struct B {
    pub binds: Vec<Bind>,
    pub ctl: Rc<Ctl>,
    pub lc: Locale,
    pub f: Fmt,
    pub pal: Pal,
}

impl B {
    pub fn new(ctl: Rc<Ctl>, lc: Locale, f: Fmt, pal: Pal) -> B {
        B { binds: Vec::new(), ctl, lc, f, pal }
    }

    fn bind(&mut self, f: impl Fn(&Ctx) + 'static) {
        self.binds.push(Box::new(f));
    }

    fn t(&self, k: &str) -> String {
        self.lc.t(k)
    }

    /// A label whose text follows the data.
    fn dyn_label(&mut self, classes: &[&str], ellipsize: bool, f: impl Fn(&Ctx) -> String + 'static) -> gtk::Label {
        let l = label("", classes, ellipsize);
        let l2 = l.clone();
        self.bind(move |c| {
            let t = bidi(&f(c));
            if l2.text() != t {
                l2.set_text(&t);
            }
        });
        l
    }
}

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

thread_local! {
    static RTL: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
}

/// Pango picks a paragraph's direction from its first strong letter, so an
/// Arabic sentence that starts with a Latin app name would lay out left to
/// right. In RTL locales, text that holds RTL letters starts with a
/// right-to-left mark. Bare values ("65.0 MB/s", "eth0") stay left to right.
pub fn set_rtl(on: bool) {
    RTL.with(|r| r.set(on));
}

fn bidi(text: &str) -> String {
    let has_rtl = text.chars().any(|c| matches!(c as u32, 0x0590..=0x08FF | 0xFB1D..=0xFDFF | 0xFE70..=0xFEFF));
    if RTL.with(|r| r.get()) && has_rtl {
        format!("\u{200F}{text}")
    } else {
        text.to_string()
    }
}

fn label(text: &str, classes: &[&str], ellipsize: bool) -> gtk::Label {
    let l = gtk::Label::new(Some(&bidi(text)));
    l.set_xalign(0.0);
    l.set_single_line_mode(true);
    for c in classes {
        l.add_css_class(c);
    }
    if ellipsize {
        l.set_ellipsize(pango::EllipsizeMode::End);
        // Keep the natural width small so long text ellipsizes instead of
        // widening the 420px flyout.
        l.set_max_width_chars(1);
        l.set_hexpand(true);
    }
    l
}

/// Caps a label's natural width at `chars`, ellipsizing beyond it, for
/// labels in slots that do not stretch (pills, buttons).
fn cap(l: &gtk::Label, chars: i32) {
    l.set_hexpand(false);
    l.set_max_width_chars(chars);
}

/// Shows the full text on hover when a label is cut short.
fn full_tip(l: &gtk::Label) {
    l.set_has_tooltip(true);
    l.connect_query_tooltip(|l, _, _, _, tip| {
        if l.layout().is_ellipsized() {
            tip.set_text(Some(&l.text()));
            true
        } else {
            false
        }
    });
}

fn hbox(sp: i32) -> gtk::Box {
    gtk::Box::new(gtk::Orientation::Horizontal, sp)
}

fn vbox(sp: i32) -> gtk::Box {
    gtk::Box::new(gtk::Orientation::Vertical, sp)
}

fn spacer() -> gtk::Box {
    let b = hbox(0);
    b.set_hexpand(true);
    b
}

fn set_a11y(w: &impl IsA<gtk::Accessible>, text: &str) {
    w.update_property(&[gtk::accessible::Property::Label(text)]);
}

fn badge(icon: &'static str, acc: &str) -> gtk::Box {
    let b = hbox(0);
    b.add_css_class("badge");
    b.add_css_class(acc);
    b.set_valign(gtk::Align::Center);
    b.set_halign(gtk::Align::Start);
    b.set_hexpand(false);
    let i = draw::icon(icon, 15, false);
    i.add_css_class(&format!("ink-{acc}"));
    b.append(&i);
    b
}

fn dot(acc: &str) -> gtk::Box {
    let d = hbox(0);
    d.add_css_class("dot");
    d.add_css_class(acc);
    d.set_valign(gtk::Align::Center);
    d.set_halign(gtk::Align::Center);
    d
}

fn round_btn(icon: &'static str, tip: &str) -> gtk::Button {
    let b = gtk::Button::new();
    b.add_css_class("round");
    let i = draw::icon(icon, 14, false);
    b.set_child(Some(&i));
    b.set_tooltip_text(Some(tip));
    set_a11y(&b, tip);
    b.set_valign(gtk::Align::Center);
    b
}

fn pill_btn(text: &str, classes: &[&str]) -> gtk::Button {
    let b = gtk::Button::with_label(text);
    b.add_css_class("pillbtn");
    for c in classes {
        b.add_css_class(c);
    }
    b.set_valign(gtk::Align::Center);
    b
}

fn set_classes(w: &impl IsA<gtk::Widget>, all: &[&str], on: &str) {
    for c in all {
        if *c != on {
            w.remove_css_class(c);
        }
    }
    if !on.is_empty() {
        w.add_css_class(on);
    }
}

fn thermal_level(c: f64) -> usize {
    if c < 48.0 {
        0
    } else if c < 70.0 {
        1
    } else if c < 90.0 {
        2
    } else {
        3
    }
}

const LEVEL_ACC: [&str; 4] = ["mem", "nrg", "thm", "crit"];
const LEVEL_KEYS: [&str; 4] = ["cool", "warm", "hot", "throttled"];

fn group_labels(lc: &Locale, g: &CoreGroups) -> (String, String) {
    match g {
        CoreGroups::Hybrid { amd: true, .. } => (lc.hw("coreZen4"), lc.hw("coreZen4c")),
        CoreGroups::Hybrid { amd: false, .. } => (lc.hw("coreP"), lc.hw("coreE")),
        CoreGroups::Halves { a, b } => {
            let r = |v: &Vec<usize>| match (v.first(), v.last()) {
                (Some(x), Some(y)) if x != y => format!("\u{2066}{}–{}\u{2069}", lc.num(*x as f64, 0), lc.num(*y as f64, 0)),
                (Some(x), _) => lc.num(*x as f64, 0),
                _ => String::new(),
            };
            (lc.hwp("coreN", &[("n", r(a))]), lc.hwp("coreN", &[("n", r(b))]))
        }
        CoreGroups::Single => (lc.hw("user"), lc.hw("kernel")),
    }
}

fn cores_label(c: &Ctx) -> String {
    let cpu = &c.st.snap.cpu;
    let mut parts = vec![c.lc.plural("core", cpu.physical_cores as f64, None)];
    if cpu.threads != cpu.physical_cores {
        parts.push(c.lc.plural("thread", cpu.threads as f64, None));
    }
    c.lc.join(&parts)
}

/// Memory pressure level from PSI "some avg10": 0 normal, 1 high, 2 critical.
fn pressure_level(c: &Ctx) -> usize {
    match c.st.snap.mem.as_ref().and_then(|m| m.psi_some_avg10) {
        Some(p) if p >= 40.0 => 2,
        Some(p) if p >= 10.0 => 1,
        _ => 0,
    }
}

const PRESSURE_KEYS: [&str; 3] = ["pressure", "pressureHigh", "pressureCritical"];
const PRESSURE_SHORT: [&str; 3] = ["pressureShort", "pressureHighShort", "pressureCriticalShort"];
const PRESSURE_ACC: [&str; 3] = ["cpu", "warn", "crit"];

fn watts(c: &Ctx) -> Option<f64> {
    c.st.snap.power.watts()
}

/// "−14.8 W" on battery, "+48.0 W" charging, plain watts from RAPL.
fn flow(c: &Ctx) -> String {
    let p = &c.st.snap.power;
    match (&p.battery, p.watts()) {
        (Some(b), Some(w)) if b.state == BattState::Charging => format!("+{}", c.f.w(w)),
        (Some(b), Some(w)) if b.state == BattState::Discharging => format!("−{}", c.f.w(w)),
        (_, Some(w)) => c.f.w(w),
        _ => DASH.into(),
    }
}

fn batt_sub(c: &Ctx) -> String {
    match &c.st.snap.power.battery {
        Some(b) if b.state == BattState::Charging => b.minutes_to_full.map(|m| c.lc.tp("fullIn", &[("time", c.f.dur(m))])).unwrap_or_else(|| c.lc.t("charging")),
        Some(b) => b.minutes_left.map(|m| c.lc.tp("timeLeft", &[("time", c.f.dur(m))])).unwrap_or_default(),
        None => c.lc.t("noBattery"),
    }
}

fn mem_values(c: &Ctx) -> Option<(String, String, String, String, String, String)> {
    let m = c.st.snap.mem.as_ref()?;
    let gb = |b: u64| format!("{} GB", c.f.gb(b as f64, 1));
    Some((gb(m.used), format!("{} GB", c.f.gb_total(m.total as f64)), gb(m.app), gb(m.buffers), gb(m.compressed), gb(m.free)))
}

fn hue(key: &str) -> f64 {
    let mut h: u32 = 2166136261;
    for b in key.bytes() {
        h ^= b as u32;
        h = h.wrapping_mul(16777619);
    }
    (h % 360) as f64
}

// ---------------------------------------------------------------------------
// tree
// ---------------------------------------------------------------------------

pub fn build(b: &mut B) -> gtk::Widget {
    let fly = vbox(8);
    fly.add_css_class("fly");
    fly.set_size_request(super::WIDTH - 28, -1);
    fly.append(&header(b));
    fly.append(&notices(b));
    let view = b.ctl.st.borrow().view.clone();
    match view {
        View::Overview => overview(b, &fly),
        View::Detail(k) => detail(b, &fly, Some(k), None),
        View::App(key) => detail(b, &fly, None, Some(key)),
        View::Settings => settings(b, &fly),
    }
    fly.append(&footer(b));
    fly.upcast()
}

fn header(b: &mut B) -> gtk::Widget {
    let row = hbox(8);
    row.append(&draw::logo(26));
    let title = label("Pulse", &["title"], false);
    row.append(&title);
    // status pill
    let pill = gtk::Box::builder().orientation(gtk::Orientation::Horizontal).spacing(5).accessible_role(gtk::AccessibleRole::Status).valign(gtk::Align::Center).build();
    pill.add_css_class("status-pill");
    let d = dot("cpu");
    pill.append(&d);
    let pl = b.dyn_label(&[], false, |c| match c.st.hog_shown() {
        Some(h) => c.lc.tp("pillValue", &[("label", c.lc.t("hogPill")), ("value", c.f.pct(h.cpu))]),
        None => match watts(c) {
            Some(w) => c.lc.tp("pillValue", &[("label", c.lc.t("calm")), ("value", format!("{}W", c.f.n(w, 1)))]),
            None => c.lc.t("calm"),
        },
    });
    pl.set_ellipsize(pango::EllipsizeMode::End);
    pill.append(&pl);
    let (p2, d2) = (pill.clone(), d.clone());
    b.bind(move |c| {
        let hog = c.st.hog_shown().is_some();
        set_classes(&p2, &["calm", "hog"], if hog { "hog" } else { "calm" });
        set_classes(&d2, &["cpu", "warn"], if hog { "warn" } else { "cpu" });
    });
    row.append(&pill);
    row.append(&spacer());
    // theme
    let dark = b.pal.dark;
    let theme = round_btn(if dark { "sun" } else { "moon" }, &b.t(if dark { "aLight" } else { "aDark" }));
    let c = b.ctl.clone();
    theme.connect_clicked(move |_| c.toggle_theme());
    row.append(&theme);
    row.append(&lang_switch(b));
    let gear = round_btn("gear", &b.t("aSettings"));
    if b.ctl.st.borrow().view == View::Settings {
        gear.add_css_class("active");
    }
    let c = b.ctl.clone();
    gear.connect_clicked(move |_| {
        let v = if c.st.borrow().view == View::Settings { View::Overview } else { View::Settings };
        c.set_view(v);
    });
    row.append(&gear);
    row.upcast()
}

/// Two locales: the EN | ع pill. Three or more: a menu of radio items.
fn lang_switch(b: &mut B) -> gtk::Widget {
    let list = b.ctl.st.borrow().registry.list();
    let cur = b.lc.code().to_string();
    let aria = format!("{}: {}", b.t("aLang"), b.lc.name());
    if list.len() <= 2 {
        let seg = hbox(0);
        seg.add_css_class("seg");
        seg.add_css_class("lang");
        seg.set_valign(gtk::Align::Center);
        // Locale order stays fixed (EN first) in both directions, like the
        // segmented pill in the prototype.
        seg.set_direction(gtk::TextDirection::Ltr);
        for l in list {
            let btn = gtk::Button::with_label(&l.label);
            if l.code == cur {
                btn.add_css_class("on");
            }
            set_a11y(&btn, &format!("{} ({})", l.name, b.t("aLang")));
            btn.set_tooltip_text(Some(&l.name));
            let c = b.ctl.clone();
            let code = l.code.clone();
            btn.connect_clicked(move |_| c.set_lang(&code));
            seg.append(&btn);
        }
        seg.update_property(&[gtk::accessible::Property::Label(&aria)]);
        seg.upcast()
    } else {
        let menu = gtk::gio::Menu::new();
        for l in &list {
            let item = gtk::gio::MenuItem::new(Some(&format!("{} · {}", l.label, l.name)), None);
            item.set_action_and_target_value(Some("app.lang"), Some(&l.code.to_variant()));
            menu.append_item(&item);
        }
        let label_now = list.iter().find(|l| l.code == cur).map(|l| l.label.clone()).unwrap_or_default();
        let mb = gtk::MenuButton::builder().label(&label_now).menu_model(&menu).valign(gtk::Align::Center).build();
        mb.add_css_class("lang-menu");
        mb.set_tooltip_text(Some(&aria));
        set_a11y(&mb, &aria);
        if let Some(a) = b.ctl.app.lookup_action("lang").and_then(|a| a.downcast::<gtk::gio::SimpleAction>().ok()) {
            a.set_state(&cur.to_variant());
        }
        mb.upcast()
    }
}

/// Hog banner (overview only), end-app confirmation and toast.
fn notices(b: &mut B) -> gtk::Widget {
    let col = vbox(6);
    let overview = b.ctl.st.borrow().view == View::Overview;

    // hog banner
    let hog = gtk::Box::builder().orientation(gtk::Orientation::Horizontal).spacing(9).accessible_role(gtk::AccessibleRole::Alert).build();
    hog.add_css_class("hog");
    let ic = hbox(0);
    ic.add_css_class("hog-icon");
    ic.set_valign(gtk::Align::Center);
    ic.set_halign(gtk::Align::Start);
    ic.set_hexpand(false);
    let wi = draw::icon("warn", 15, false);
    ic.append(&wi);
    hog.append(&ic);
    let txt = vbox(1);
    txt.set_hexpand(true);
    let t1 = b.dyn_label(&["strong"], true, |c| {
        c.st.hog_shown().map(|h| c.lc.tp("hogTitle", &[("app", c.lc.app(&h.name)), ("pct", c.f.pct(h.cpu))])).unwrap_or_default()
    });
    full_tip(&t1);
    let t2 = label(&b.lc.tp("hogSub", &[("pct", b.f.pct(50.0))]), &["sub"], true);
    full_tip(&t2);
    txt.append(&t1);
    txt.append(&t2);
    hog.append(&txt);
    let end = pill_btn(&b.t("endApp"), &["warn"]);
    let c = b.ctl.clone();
    end.connect_clicked(move |_| {
        let h = c.st.borrow().hog_shown().cloned();
        if let Some(h) = h {
            c.request_end(h);
        }
    });
    hog.append(&end);
    let x = gtk::Button::new();
    x.add_css_class("iconbtn");
    x.set_child(Some(&draw::icon("close", 12, false)));
    x.set_valign(gtk::Align::Center);
    set_a11y(&x, &b.t("aDismiss"));
    x.set_tooltip_text(Some(&b.t("aDismiss")));
    let c = b.ctl.clone();
    x.connect_clicked(move |_| c.dismiss_hog());
    hog.append(&x);
    let h2 = hog.clone();
    b.bind(move |c| {
        h2.set_visible(overview && c.st.hog_shown().is_some() && c.st.confirm.is_none() && c.st.toast.is_none());
    });
    col.append(&hog);

    // confirmation
    let conf = gtk::Box::builder().orientation(gtk::Orientation::Horizontal).spacing(8).accessible_role(gtk::AccessibleRole::AlertDialog).build();
    conf.add_css_class("notice");
    let ct = vbox(1);
    ct.set_hexpand(true);
    let title = b.dyn_label(&["strong"], true, |c| {
        c.st.confirm.as_ref().map(|a| c.lc.tp("confirmTitle", &[("app", c.lc.app(&a.name))])).unwrap_or_default()
    });
    ct.append(&title);
    ct.append(&label(&b.t("confirmSub"), &["sub"], true));
    conf.append(&ct);
    let cancel = pill_btn(&b.t("cancel"), &[]);
    let c = b.ctl.clone();
    cancel.connect_clicked(move |_| c.cancel_end());
    conf.append(&cancel);
    let ok = pill_btn(&b.t("confirmBtn"), &["danger"]);
    let c = b.ctl.clone();
    ok.connect_clicked(move |_| c.confirm_end());
    conf.append(&ok);
    let (c2, ok2) = (conf.clone(), ok.clone());
    b.bind(move |c| {
        let show = c.st.confirm.is_some();
        if show && !c2.is_visible() {
            c2.set_visible(true);
            ok2.grab_focus();
        }
        c2.set_visible(show);
    });
    col.append(&conf);

    // toast
    let toast = gtk::Box::builder().orientation(gtk::Orientation::Horizontal).accessible_role(gtk::AccessibleRole::Status).build();
    toast.add_css_class("toast");
    let tl = b.dyn_label(&[], true, |c| c.st.toast.clone().unwrap_or_default());
    toast.append(&tl);
    let t2 = toast.clone();
    b.bind(move |c| t2.set_visible(c.st.confirm.is_none() && c.st.toast.is_some()));
    col.append(&toast);

    let col2 = col.clone();
    b.bind(move |c| {
        col2.set_visible((overview && c.st.hog_shown().is_some()) || c.st.confirm.is_some() || c.st.toast.is_some());
    });
    col.upcast()
}

/// A card that opens a detail view on click, Enter or Space.
fn card(b: &mut B, kind: Kind, child: &gtk::Box) -> gtk::Button {
    let btn = gtk::Button::new();
    btn.add_css_class("card");
    btn.set_child(Some(child));
    btn.set_hexpand(true);
    let c = b.ctl.clone();
    btn.connect_clicked(move |_| c.set_view(View::Detail(kind)));
    btn
}

fn card_head(b: &mut B, icon: &'static str, acc: &str, title: &str) -> gtk::Box {
    let h = hbox(7);
    h.append(&badge(icon, acc));
    h.append(&label(title, &["h-card"], true));
    let _ = b;
    h
}

fn chevron() -> gtk::DrawingArea {
    let c = draw::icon("chev", 12, true);
    c.add_css_class("dim3");
    c
}

fn spark_overlay(b: &mut B, key: &'static str, color: Rgba) -> gtk::Overlay {
    let sp = Spark::new(color, SPARK_N);
    let ov = gtk::Overlay::new();
    ov.set_child(Some(&sp.area));
    let l = label(&b.t("min1"), &["min1"], false);
    l.set_halign(gtk::Align::Start);
    l.set_valign(gtk::Align::Start);
    l.set_margin_top(0);
    ov.add_overlay(&l);
    b.bind(move |c| sp.set(&c.st.hist.spark(key)));
    ov
}

fn bar_row(b: &mut B, name: impl Fn(&Ctx) -> String + 'static, color: Rgba, f: impl Fn(&Ctx) -> Option<f64> + 'static) -> gtk::Box {
    let r = hbox(8);
    let n = b.dyn_label(&["sub"], true, name);
    r.append(&n);
    let bar = Bar::new(b.pal.track, color, 4);
    bar.area.set_size_request(60, -1);
    bar.area.set_hexpand(false);
    r.append(&bar.area);
    let v = b.dyn_label(&["val"], false, {
        let f2 = Rc::new(f);
        let f3 = f2.clone();
        let bar2 = bar.clone();
        move |c: &Ctx| {
            let x = f3(c);
            bar2.set(x.unwrap_or(0.0) / 100.0);
            c.f.opct(x)
        }
    });
    v.set_width_chars(4);
    v.set_xalign(1.0);
    r.append(&v);
    r
}

fn overview(b: &mut B, fly: &gtk::Box) {
    let grid = gtk::Grid::new();
    grid.set_column_homogeneous(true);
    grid.set_column_spacing(8);
    grid.set_row_spacing(8);
    let pal = b.pal.clone();

    // CPU
    let v = vbox(3);
    let h = card_head(b, "cpu", "cpu", &b.t("cpu"));
    let high = label(&b.t("high"), &["pill", "warn"], false);
    high.set_valign(gtk::Align::Center);
    h.append(&high);
    h.append(&chevron());
    v.append(&h);
    let hero = b.dyn_label(&["hero"], false, |c| if c.st.snap.cpu.valid { c.f.pct(c.st.snap.cpu.total) } else { DASH.into() });
    v.append(&hero);
    v.append(&b.dyn_label(&["sub"], true, |c| {
        let s = &c.st.snap.cpu;
        c.lc.tp("userSys", &[("user", c.f.pct(s.user)), ("sys", c.f.pct(s.system))])
    }));
    let bars = vbox(2);
    bars.set_margin_top(3);
    bars.append(&bar_row(b, |c| group_labels(c.lc, &c.st.snap.cpu.groups).0, pal.cpu, |c| c.st.snap.cpu.valid.then_some(c.st.snap.cpu.group_a)));
    bars.append(&bar_row(b, |c| group_labels(c.lc, &c.st.snap.cpu.groups).1, pal.cpu.a(0.55), |c| c.st.snap.cpu.valid.then_some(c.st.snap.cpu.group_b)));
    bars.append(&bar_row(b, |c| c.lc.t("gpu"), pal.gpu, |c| c.st.snap.gpu.as_ref().and_then(|g| g.busy_pct)));
    v.append(&bars);
    v.append(&spark_overlay(b, "cpu", pal.cpu));
    let cpu_card = card(b, Kind::Cpu, &v);
    let (cc, hl, hr) = (cpu_card.clone(), high.clone(), hero.clone());
    b.bind(move |c| {
        let hi = c.st.snap.cpu.valid && c.st.snap.cpu.total >= 50.0;
        hl.set_visible(hi);
        set_classes(&hr, &["ink-warn"], if hi { "ink-warn" } else { "" });
        set_classes(&cc, &["warn-border"], if hi { "warn-border" } else { "" });
    });
    set_a11y(&cpu_card, &b.t("cpu"));
    grid.attach(&cpu_card, 0, 0, 1, 1);

    // Memory
    let v = vbox(3);
    let h = card_head(b, "mem", "mem", &b.t("mem"));
    h.append(&chevron());
    v.append(&h);
    let hr = hbox(6);
    hr.append(&b.dyn_label(&["hero"], false, |c| c.st.snap.mem.as_ref().map(|m| c.f.pct(m.used_pct())).unwrap_or_else(|| DASH.into())));
    let pb = hbox(4);
    pb.add_css_class("pill");
    pb.add_css_class("cpu");
    pb.set_valign(gtk::Align::Center);
    pb.set_hexpand(true);
    pb.set_halign(gtk::Align::End);
    pb.set_valign(gtk::Align::Center);
    let pd = dot("cpu");
    pb.append(&pd);
    let pl = b.dyn_label(&[], true, |c| c.lc.t(PRESSURE_KEYS[pressure_level(c)]));
    cap(&pl, 15);
    full_tip(&pl);
    pb.append(&pl);
    let (pb2, pd2) = (pb.clone(), pd.clone());
    b.bind(move |c| {
        let acc = PRESSURE_ACC[pressure_level(c)];
        set_classes(&pb2, &PRESSURE_ACC, acc);
        set_classes(&pd2, &PRESSURE_ACC, acc);
        pb2.set_visible(c.st.snap.mem.is_some());
    });
    hr.append(&pb);
    v.append(&hr);
    v.append(&b.dyn_label(&["sub"], true, |c| match mem_values(c) {
        Some((u, t, ..)) => c.lc.tp("memOf", &[("used", u), ("total", t)]),
        None => DASH.into(),
    }));
    let seg = SegBar::new(pal.track, pal.mem, pal.mem.a(0.55), pal.mem.a(0.85));
    seg.area.set_margin_top(4);
    let s2 = seg.clone();
    b.bind(move |c| {
        if let Some(m) = &c.st.snap.mem {
            let t = m.total.max(1) as f64;
            s2.set(m.app as f64 / t, m.buffers as f64 / t, m.compressed as f64 / t);
        }
    });
    v.append(&seg.area);
    let lg = gtk::Grid::new();
    lg.set_column_spacing(4);
    lg.set_row_spacing(0);
    lg.set_column_homogeneous(true);
    lg.set_margin_top(3);
    let items: [(&str, Box<dyn Fn(&Ctx) -> String>); 4] = [
        ("l1", Box::new(|c: &Ctx| mem_values(c).map(|v| format!("{} {}", c.lc.t("segApp"), v.2)).unwrap_or_default())),
        ("l2", Box::new(|c: &Ctx| mem_values(c).map(|v| format!("{} {}", c.lc.hw("buffers"), v.3)).unwrap_or_default())),
        ("l3", Box::new(|c: &Ctx| {
            let kind = c.st.snap.mem.as_ref().map(|m| m.compressed_kind).unwrap_or("zswap");
            mem_values(c).map(|v| format!("{kind} {}", v.4)).unwrap_or_default()
        })),
        ("free", Box::new(|c: &Ctx| mem_values(c).map(|v| format!("{} {}", c.lc.t("segFree"), v.5)).unwrap_or_default())),
    ];
    for (i, (cls, f)) in items.into_iter().enumerate() {
        let r = hbox(4);
        let sq = hbox(0);
        sq.add_css_class("legend-sq");
        sq.add_css_class(cls);
        sq.set_valign(gtk::Align::Center);
        r.append(&sq);
        let l = b.dyn_label(&["tiny", "legend"], true, f);
        full_tip(&l);
        r.append(&l);
        lg.attach(&r, (i % 2) as i32, (i / 2) as i32, 1, 1);
    }
    v.append(&lg);
    v.append(&spark_overlay(b, "mem", pal.mem));
    let mem_card = card(b, Kind::Mem, &v);
    set_a11y(&mem_card, &b.t("mem"));
    grid.attach(&mem_card, 1, 0, 1, 1);

    // Energy
    let v = vbox(3);
    let h = card_head(b, "nrg", "nrg", &b.t("nrg"));
    h.append(&chevron());
    v.append(&h);
    v.append(&b.dyn_label(&["hero"], false, |c| c.st.snap.power.battery.as_ref().map(|bt| c.f.pct(bt.capacity)).unwrap_or_else(|| DASH.into())));
    let bs = b.dyn_label(&["sub"], true, batt_sub);
    v.append(&bs);
    let fr = hbox(6);
    fr.set_margin_top(2);
    let chip = hbox(5);
    chip.add_css_class("chip");
    chip.add_css_class("nrg");
    chip.set_direction(gtk::TextDirection::Ltr);
    chip.append(&dot("nrg"));
    chip.append(&b.dyn_label(&[], false, flow));
    fr.append(&chip);
    fr.append(&b.dyn_label(&["sub"], true, |c| {
        let p = &c.st.snap.power;
        match &p.battery {
            Some(bt) if bt.state == BattState::Charging => c.lc.t("charging"),
            Some(bt) if bt.state == BattState::Discharging => c.lc.t("onBatt"),
            Some(_) => String::new(),
            None if p.ac_online == Some(true) || p.rapl_watts.is_some() => c.lc.t("acPower"),
            None => String::new(),
        }
    }));
    v.append(&fr);
    let hl = b.dyn_label(&["sub"], true, |c| match &c.st.snap.power.battery {
        Some(bt) => {
            let cyc = bt.cycles.map(|n| c.lc.plural("cycle", n as f64, None)).unwrap_or_else(|| DASH.into());
            c.lc.tp("health", &[("pct", c.f.opct(bt.health_pct)), ("cycles", cyc)])
        }
        None => String::new(),
    });
    hl.set_margin_top(4);
    v.append(&hl);
    v.append(&spark_overlay(b, "nrg", pal.nrg));
    let nrg_card = card(b, Kind::Nrg, &v);
    set_a11y(&nrg_card, &b.t("nrg"));
    grid.attach(&nrg_card, 0, 1, 1, 1);

    // Thermal
    let v = vbox(3);
    let h = card_head(b, "thm", "thm", &b.t("thm"));
    h.append(&chevron());
    v.append(&h);
    let hero = b.dyn_label(&["hero"], true, |c| c.st.snap.thermal.cpu_c.map(|t| c.lc.t(LEVEL_KEYS[thermal_level(t)])).unwrap_or_else(|| DASH.into()));
    v.append(&hero);
    v.append(&b.dyn_label(&["sub"], true, |c| c.lc.tp("tempLine", &[("cpu", c.f.ot(c.st.snap.thermal.cpu_c, 0)), ("gpu", c.f.ot(c.st.snap.thermal.gpu_c, 0))])));
    let meter = Meter::new(pal.track, [pal.mem, pal.nrg, pal.thm, pal.crit]);
    meter.area.set_margin_top(4);
    let meter_wrap = gtk::Box::builder().accessible_role(gtk::AccessibleRole::Meter).build();
    meter_wrap.append(&meter.area);
    meter_wrap.set_hexpand(true);
    v.append(&meter_wrap);
    let labs = gtk::Grid::new();
    labs.set_column_homogeneous(true);
    labs.set_column_spacing(4);
    let mut lls = Vec::new();
    for (i, k) in LEVEL_KEYS.iter().enumerate() {
        let l = label(&b.t(k), &["tiny"], true);
        full_tip(&l);
        labs.attach(&l, i as i32, 0, 1, 1);
        lls.push(l);
    }
    v.append(&labs);
    let note = b.dyn_label(&["tiny"], true, |c| {
        let thr = c.st.snap.thermal.cpu_c.map(|t| t >= 90.0).unwrap_or(false) || c.st.snap.thermal.throttle_events.map(|n| n > 0).unwrap_or(false);
        c.lc.t(if thr { "throttling" } else { "zero" })
    });
    full_tip(&note);
    v.append(&note);
    v.append(&spark_overlay(b, "thm", pal.thm));
    let (m2, h2, n2, mw) = (meter.clone(), hero.clone(), note.clone(), meter_wrap.clone());
    b.bind(move |c| {
        let lv = c.st.snap.thermal.cpu_c.map(thermal_level);
        m2.set(lv);
        let acc = lv.map(|l| format!("ink-{}", LEVEL_ACC[l])).unwrap_or_default();
        set_classes(&h2, &["ink-mem", "ink-nrg", "ink-thm", "ink-crit"], "");
        set_classes(&h2, &[], &acc);
        for (i, l) in lls.iter().enumerate() {
            let on = lv == Some(i);
            set_classes(l, &["ink-mem", "ink-nrg", "ink-thm", "ink-crit", "strong"], "");
            if on {
                l.add_css_class(&format!("ink-{}", LEVEL_ACC[i]));
                l.add_css_class("strong");
            }
        }
        let thr = lv == Some(3) || c.st.snap.thermal.throttle_events.map(|n| n > 0).unwrap_or(false);
        set_classes(&n2, &["danger-ink"], if thr { "danger-ink" } else { "" });
        let now = lv.map(|l| l + 1).unwrap_or(0) as f64;
        mw.update_property(&[
            gtk::accessible::Property::ValueMin(1.0),
            gtk::accessible::Property::ValueMax(4.0),
            gtk::accessible::Property::ValueNow(now),
            gtk::accessible::Property::ValueText(&lv.map(|l| c.lc.t(LEVEL_KEYS[l])).unwrap_or_default()),
        ]);
    });
    let thm_card = card(b, Kind::Thm, &v);
    set_a11y(&thm_card, &b.t("thm"));
    grid.attach(&thm_card, 1, 1, 1, 1);
    fly.append(&grid);

    // GPU strip
    let r = hbox(8);
    r.append(&badge("gpu", "gpu"));
    r.append(&label(&b.t("gpu"), &["h-card"], false));
    let gn = b.dyn_label(&["sub"], true, |c| c.st.snap.gpu.as_ref().map(|g| g.name.clone()).unwrap_or_else(|| DASH.into()));
    full_tip(&gn);
    r.append(&gn);
    let gbar = Bar::new(pal.track, pal.gpu, 5);
    gbar.area.set_size_request(80, -1);
    gbar.area.set_hexpand(false);
    r.append(&gbar.area);
    let gb2 = gbar.clone();
    let gv = b.dyn_label(&["strong"], false, move |c| {
        let x = c.st.snap.gpu.as_ref().and_then(|g| g.busy_pct);
        gb2.set(x.unwrap_or(0.0) / 100.0);
        c.f.opct(x)
    });
    gv.set_width_chars(4);
    gv.set_xalign(1.0);
    r.append(&gv);
    let gt = b.dyn_label(&["pill"], false, |c| c.f.ot(c.st.snap.thermal.gpu_c, 0));
    gt.set_valign(gtk::Align::Center);
    let gt2 = gt.clone();
    b.bind(move |c| {
        let acc = match c.st.snap.thermal.gpu_c {
            Some(t) if t >= 80.0 => "thm",
            Some(t) if t >= 60.0 => "nrg",
            _ => "mem",
        };
        set_classes(&gt2, &["mem", "nrg", "thm"], acc);
    });
    r.append(&gt);
    let vb = vbox(0);
    vb.append(&r);
    let gpu_card = card(b, Kind::Gpu, &vb);
    gpu_card.add_css_class("strip");
    set_a11y(&gpu_card, &b.t("gpu"));
    fly.append(&gpu_card);

    // Storage + Network
    let two = gtk::Grid::new();
    two.set_column_homogeneous(true);
    two.set_column_spacing(8);
    let v = vbox(4);
    let h = hbox(6);
    let si = draw::icon("ssd", 13, false);
    si.add_css_class("dim");
    h.append(&si);
    let dn = label(&b.lc.hwp("fsRoot", &[("path", "/".into())]), &["h-card"], true);
    full_tip(&dn);
    h.append(&dn);
    v.append(&h);
    let r = hbox(6);
    r.append(&b.dyn_label(&["strong"], true, |c| c.st.snap.disk.as_ref().map(|d| c.lc.tp("used", &[("pct", c.f.pct(d.used_pct()))])).unwrap_or_else(|| DASH.into())));
    let fr = b.dyn_label(&["sub"], false, |c| c.st.snap.disk.as_ref().map(|d| c.lc.tp("free", &[("value", c.f.size(d.free as f64))])).unwrap_or_default());
    r.append(&fr);
    v.append(&r);
    let dbar = Bar::new(pal.track, pal.cpu, 5);
    dbar.set_colors(pal.cpu, Some(pal.nrg));
    let db2 = dbar.clone();
    b.bind(move |c| db2.set(c.st.snap.disk.as_ref().map(|d| d.used_pct() / 100.0).unwrap_or(0.0)));
    v.append(&dbar.area);
    let ssd = card(b, Kind::Ssd, &v);
    ssd.add_css_class("strip");
    set_a11y(&ssd, &b.t("storage"));
    two.attach(&ssd, 0, 0, 1, 1);

    let v = vbox(4);
    let h = hbox(6);
    let wifi = b.ctl.st.borrow().snap.net.as_ref().map(|n| n.wireless).unwrap_or(false);
    let ni = draw::icon(if wifi { "net" } else { "eth" }, 13, false);
    ni.add_css_class("ink-mem");
    h.append(&ni);
    let nn = b.dyn_label(&["h-card"], true, |c| match &c.st.snap.net {
        Some(n) => format!("{} · {}", if n.wireless { "Wi-Fi" } else { "Ethernet" }, n.iface),
        None => c.lc.t("network"),
    });
    full_tip(&nn);
    h.append(&nn);
    v.append(&h);
    let r = hbox(10);
    r.append(&b.dyn_label(&["strong", "ink-mem"], false, |c| format!("↓ {}", c.f.rate(c.st.snap.net.as_ref().and_then(|n| n.down_bps)))));
    r.append(&b.dyn_label(&["strong", "ink-cpu"], false, |c| format!("↑ {}", c.f.rate(c.st.snap.net.as_ref().and_then(|n| n.up_bps)))));
    v.append(&r);
    let net = card(b, Kind::Net, &v);
    net.add_css_class("strip");
    set_a11y(&net, &b.t("network"));
    two.attach(&net, 1, 0, 1, 1);
    fly.append(&two);

    fly.append(&top_apps(b));
}

fn top_apps(b: &mut B) -> gtk::Widget {
    let cardb = vbox(2);
    cardb.add_css_class("card");
    let h = hbox(8);
    h.append(&label(&b.t("top"), &["h-card"], true));
    let seg = gtk::Box::builder().orientation(gtk::Orientation::Horizontal).accessible_role(gtk::AccessibleRole::TabList).build();
    seg.add_css_class("seg");
    let mut btns = Vec::new();
    for (k, key) in [(SortKey::Cpu, "cpu"), (SortKey::Mem, "mem"), (SortKey::Gpu, "gpu")] {
        let btn = gtk::Button::builder().label(b.t(key)).accessible_role(gtk::AccessibleRole::Tab).build();
        let c = b.ctl.clone();
        btn.connect_clicked(move |_| c.set_sort(k));
        seg.append(&btn);
        btns.push((k, btn));
    }
    b.bind(move |c| {
        for (k, btn) in &btns {
            let on = c.st.sort == *k;
            set_classes(btn, &["on"], if on { "on" } else { "" });
            btn.update_state(&[gtk::accessible::State::Selected(Some(on))]);
        }
    });
    h.append(&seg);
    h.set_margin_bottom(4);
    cardb.append(&h);

    for i in 0..5 {
        cardb.append(&app_row(b, i));
    }
    cardb.upcast()
}

fn app_row(b: &mut B, i: usize) -> gtk::Widget {
    let cur: Rc<RefCell<Option<AppGroup>>> = Rc::new(RefCell::new(None));
    let row = gtk::Box::builder().orientation(gtk::Orientation::Horizontal).spacing(8).accessible_role(gtk::AccessibleRole::Button).focusable(true).build();
    row.add_css_class("app-row");
    let pal = b.pal.clone();
    // monogram
    let mono = gtk::DrawingArea::builder().content_width(22).content_height(22).valign(gtk::Align::Center).build();
    let c1 = cur.clone();
    let dark = pal.dark;
    mono.set_draw_func(move |w, cr, wd, ht| {
        let Some(a) = c1.borrow().clone() else { return };
        let h = hue(&a.key);
        let (bg, ink) = if dark { (oklch(0.4, 0.07, h, 0.75), oklch(0.9, 0.08, h, 1.0)) } else { (oklch(0.89, 0.075, h, 1.0), oklch(0.4, 0.11, h, 1.0)) };
        bg.set(cr);
        let (wd, ht) = (wd as f64, ht as f64);
        cr.new_sub_path();
        let r = 7.0;
        cr.arc(wd - r, r, r, -std::f64::consts::FRAC_PI_2, 0.0);
        cr.arc(wd - r, ht - r, r, 0.0, std::f64::consts::FRAC_PI_2);
        cr.arc(r, ht - r, r, std::f64::consts::FRAC_PI_2, std::f64::consts::PI);
        cr.arc(r, r, r, std::f64::consts::PI, 1.5 * std::f64::consts::PI);
        cr.close_path();
        cr.fill().ok();
        let letter: String = a.name.chars().find(|c| c.is_alphanumeric()).map(|c| c.to_uppercase().collect()).unwrap_or_default();
        let layout = w.create_pango_layout(Some(&letter));
        let mut fd = pango::FontDescription::new();
        fd.set_size(10 * pango::SCALE);
        fd.set_weight(pango::Weight::Bold);
        layout.set_font_description(Some(&fd));
        let (tw, th) = layout.pixel_size();
        ink.set(cr);
        cr.move_to((wd - tw as f64) / 2.0, (ht - th as f64) / 2.0);
        pangocairo::functions::show_layout(cr, &layout);
    });
    row.append(&mono);
    let name = label("", &[], false);
    name.set_ellipsize(pango::EllipsizeMode::End);
    name.set_max_width_chars(20);
    row.append(&name);
    let procs = label("", &["procs"], false);
    procs.set_valign(gtk::Align::Center);
    procs.set_direction(gtk::TextDirection::Ltr);
    row.append(&procs);
    row.append(&spacer());
    let bar = Bar::new(pal.track, pal.cpu, 4);
    bar.area.set_size_request(56, -1);
    bar.area.set_hexpand(false);
    row.append(&bar.area);
    let ov = gtk::Overlay::new();
    let metric = label("", &["metric"], false);
    metric.set_xalign(1.0);
    metric.set_width_chars(7);
    ov.set_child(Some(&metric));
    let end = gtk::Button::with_label(&b.t("end"));
    end.add_css_class("end-btn");
    end.set_halign(gtk::Align::End);
    end.set_valign(gtk::Align::Center);
    ov.add_overlay(&end);
    row.append(&ov);

    let c = b.ctl.clone();
    let c2 = cur.clone();
    end.connect_clicked(move |_| {
        if let Some(a) = c2.borrow().clone() {
            c.request_end(a);
        }
    });
    let open = {
        let c = b.ctl.clone();
        let c3 = cur.clone();
        move || {
            if let Some(a) = c3.borrow().clone() {
                c.set_view(View::App(a.key));
            }
        }
    };
    let click = gtk::GestureClick::new();
    let o1 = open.clone();
    click.connect_released(move |g, _, _, _| {
        g.set_state(gtk::EventSequenceState::Claimed);
        o1();
    });
    row.add_controller(click);
    let keys = gtk::EventControllerKey::new();
    keys.connect_key_pressed(move |_, k, _, _| {
        if matches!(k, gtk::gdk::Key::Return | gtk::gdk::Key::KP_Enter | gtk::gdk::Key::space) {
            open();
            return glib::Propagation::Stop;
        }
        glib::Propagation::Proceed
    });
    row.add_controller(keys);

    // tooltip with the per-app breakdown
    let lc = b.lc.clone();
    let f = b.f.clone();
    let c4 = cur.clone();
    let total_mem = b.ctl.st.borrow().snap.mem.as_ref().map(|m| m.total).unwrap_or(0);
    let tip_lines = Rc::new(RefCell::new(String::new()));
    let tl = tip_lines.clone();
    row.set_has_tooltip(true);
    row.connect_query_tooltip(move |_, _, _, _, tip| {
        let s = tl.borrow();
        if s.is_empty() {
            return false;
        }
        tip.set_markup(Some(&s));
        true
    });
    let _ = (&lc, &f, total_mem);

    let (row2, name2, procs2, metric2, end2, mono2) = (row.clone(), name.clone(), procs.clone(), metric.clone(), end.clone(), mono.clone());
    b.bind(move |c| {
        let apps = c.st.top_apps(5);
        let a = apps.get(i).cloned();
        row2.set_visible(a.is_some());
        let Some(a) = a else {
            *c4.borrow_mut() = None;
            return;
        };
        let changed = c4.borrow().as_ref().map(|x| x.key != a.key).unwrap_or(true);
        let nm = c.lc.app(&a.name);
        name2.set_text(&bidi(&nm));
        procs2.set_visible(a.procs() > 1);
        procs2.set_text(&format!("×{}", c.f.n(a.procs() as f64, 0)));
        let hog = c.st.hog.as_ref().map(|h| h.key == a.key).unwrap_or(false) && c.st.sort == SortKey::Cpu;
        let (val, max) = match c.st.sort {
            SortKey::Cpu => (a.cpu, apps[0].cpu),
            SortKey::Mem => (a.mem_bytes as f64, apps[0].mem_bytes as f64),
            SortKey::Gpu => (a.gpu.unwrap_or(0.0), apps[0].gpu.unwrap_or(0.0)),
        };
        bar.set((val / max.max(1e-9)).max(0.03));
        match (c.st.sort, hog) {
            (_, true) => bar.set_colors(c.pal.warn, Some(c.pal.crit)),
            (SortKey::Cpu, _) => bar.set_colors(c.pal.cpu, None),
            (SortKey::Mem, _) => bar.set_colors(c.pal.mem, None),
            (SortKey::Gpu, _) => bar.set_colors(c.pal.gpu, None),
        }
        metric2.set_text(&bidi(&match c.st.sort {
            SortKey::Cpu => c.f.pct1(a.cpu),
            SortKey::Mem => c.f.size(a.mem_bytes as f64),
            SortKey::Gpu => c.f.opct(a.gpu),
        }));
        set_classes(&metric2, &["ink-warn"], if hog { "ink-warn" } else { "" });
        let end_label = c.lc.tp("endNamed", &[("app", nm.clone())]);
        set_a11y(&end2, &end_label);
        set_a11y(&row2, &nm);
        // tooltip
        let tm = c.st.snap.mem.as_ref().map(|m| m.total).unwrap_or(0);
        let uk = a.user_frac.map(|u| format!("{} · {}", c.f.pct(u * 100.0), c.f.pct((1.0 - u) * 100.0))).unwrap_or_else(|| DASH.into());
        let esc = |s: &str| glib::markup_escape_text(s).to_string();
        let rows = [
            (c.lc.t("tipUserKernel"), uk),
            (c.lc.t("tipRam"), if tm > 0 { c.f.pct1(a.mem_bytes as f64 / tm as f64 * 100.0) } else { DASH.into() }),
            (c.lc.t("tipGpu"), c.f.opct(a.gpu)),
            (c.lc.t("tipProc"), c.f.n(a.procs() as f64, 0)),
        ];
        let mut s = format!("<b>{}</b>", esc(&nm));
        for (k, v) in rows {
            s.push_str(&format!("\n{}  <b>{}</b>", esc(&k), esc(&v)));
        }
        s.push_str(&format!("\n<small>{}</small>", esc(&c.lc.t("tipClick"))));
        *tip_lines.borrow_mut() = s;
        *c4.borrow_mut() = Some(a);
        if changed {
            mono2.queue_draw();
        }
    });
    row.upcast()
}

// ---------------------------------------------------------------------------
// detail views
// ---------------------------------------------------------------------------

struct Detail {
    acc: &'static str,
    acc2: &'static str,
    title: String,
    split: bool,
    zero: bool,
}

fn tile(b: &mut B, k: impl Fn(&Ctx) -> String + 'static, v: impl Fn(&Ctx) -> String + 'static) -> gtk::Widget {
    let t = vbox(1);
    t.add_css_class("card");
    t.add_css_class("tile");
    let kl = b.dyn_label(&["sub"], true, k);
    full_tip(&kl);
    t.append(&kl);
    let vl = b.dyn_label(&["strong"], true, v);
    vl.add_css_class("h-card");
    full_tip(&vl);
    t.append(&vl);
    t.upcast()
}

type TileFn = (Box<dyn Fn(&Ctx) -> String>, Box<dyn Fn(&Ctx) -> String>);

fn tk(s: &'static str) -> Box<dyn Fn(&Ctx) -> String> {
    Box::new(move |c: &Ctx| c.lc.t(s))
}

fn detail(b: &mut B, fly: &gtk::Box, kind: Option<Kind>, app: Option<String>) {
    let lc = b.lc.clone();
    let level_acc = b.ctl.st.borrow().snap.thermal.cpu_c.map(|t| LEVEL_ACC[thermal_level(t)]).unwrap_or("thm");
    let hog_key = b.ctl.st.borrow().hog.as_ref().map(|h| h.key.clone());
    let d = match (kind, &app) {
        (Some(Kind::Cpu), _) => Detail { acc: "cpu", acc2: "cpu", title: lc.t("cpu"), split: false, zero: true },
        (Some(Kind::Mem), _) => Detail { acc: "mem", acc2: "mem", title: lc.t("mem"), split: false, zero: false },
        (Some(Kind::Nrg), _) => Detail { acc: "nrg", acc2: "nrg", title: lc.t("nrg"), split: false, zero: true },
        (Some(Kind::Thm), _) => Detail { acc: level_acc, acc2: level_acc, title: lc.t("thm"), split: false, zero: false },
        (Some(Kind::Gpu), _) => Detail { acc: "gpu", acc2: "gpu", title: lc.t("gpu"), split: false, zero: true },
        (Some(Kind::Ssd), _) => Detail { acc: "cpu", acc2: "nrg", title: lc.t("storage"), split: true, zero: true },
        (Some(Kind::Net), _) => Detail { acc: "mem", acc2: "cpu", title: lc.t("network"), split: true, zero: true },
        (None, Some(k)) => {
            let name = b.ctl.st.borrow().app(k).map(|a| a.name.clone()).unwrap_or_else(|| k.clone());
            let hog = hog_key.as_deref() == Some(k.as_str());
            Detail { acc: if hog { "warn" } else { "cpu" }, acc2: "cpu", title: lc.app(&name), split: false, zero: true }
        }
        _ => Detail { acc: "cpu", acc2: "cpu", title: lc.t("cpu"), split: false, zero: true },
    };
    let pal = b.pal.clone();
    let (ca, _) = pal.acc(d.acc);
    let (cb, _) = pal.acc(d.acc2);

    // back row
    let r = hbox(8);
    let back = gtk::Button::new();
    back.add_css_class("pillbtn");
    let bb = hbox(4);
    bb.append(&draw::icon("back", 12, true));
    bb.append(&label(&lc.t("back"), &[], false));
    back.set_child(Some(&bb));
    let c = b.ctl.clone();
    back.connect_clicked(move |_| c.set_view(View::Overview));
    r.append(&back);
    let pill = hbox(6);
    pill.add_css_class("pill");
    pill.add_css_class(d.acc);
    pill.set_valign(gtk::Align::Center);
    pill.append(&dot(d.acc));
    let pt = label(&d.title, &[], true);
    cap(&pt, 22);
    full_tip(&pt);
    pill.append(&pt);
    pill.set_hexpand(false);
    r.append(&pill);
    r.append(&spacer());
    if let Some(k) = app.clone() {
        let end = pill_btn(&lc.t("endApp"), &["warn"]);
        let c = b.ctl.clone();
        end.connect_clicked(move |_| {
            let a = c.st.borrow().app(&k).cloned();
            if let Some(a) = a {
                c.request_end(a);
            }
        });
        r.append(&end);
    }
    fly.append(&r);
    // focus Back so keyboard users land in the view
    let bk = back.clone();
    glib::idle_add_local_once(move || {
        bk.grab_focus();
    });

    // hero
    let hr = hbox(10);
    let ak = app.clone();
    let hero = b.dyn_label(&["hero"], false, move |c| hero_text(c, kind, ak.as_deref()));
    hr.append(&hero);
    let ak = app.clone();
    let sub = b.dyn_label(&["sub"], true, move |c| sub_text(c, kind, ak.as_deref()));
    full_tip(&sub);
    sub.set_valign(gtk::Align::Center);
    hr.append(&sub);
    fly.append(&hr);

    // chart card
    let cc = vbox(4);
    cc.add_css_class("card");
    cc.add_css_class("chart-card");
    let ch = hbox(8);
    ch.append(&label(&lc.t("last10"), &["h-card"], true));
    if d.split {
        let (la, lb) = if kind == Some(Kind::Ssd) { (lc.t("read"), lc.t("write")) } else { (lc.t("down"), lc.t("up")) };
        for (txt, acc) in [(la, d.acc), (lb, d.acc2)] {
            let p = hbox(4);
            p.add_css_class("pill");
            p.add_css_class(acc);
            p.append(&dot(acc));
            p.append(&label(&txt, &[], false));
            ch.append(&p);
        }
    }
    let ak = app.clone();
    let stat = b.dyn_label(&["tiny"], false, move |c| stat_text(c, kind, ak.as_deref()));
    stat.set_hexpand(true);
    stat.set_xalign(1.0);
    if d.split {
        stat.set_visible(false);
    }
    ch.append(&stat);
    cc.append(&ch);
    let lbl_fmt = chart_label(b, kind, app.clone());
    let chart = Chart::new(ca, cb, d.split, d.zero, LONG_N, pal.track_strong.a(0.5), if pal.dark { Rgba::hex(0x14201C) } else { Rgba::hex(0xFFFFFF) }, pal.ink, lbl_fmt);
    let ch2 = chart.clone();
    let ak = app.clone();
    b.bind(move |c| {
        let (a, bb) = series(c, kind, ak.as_deref());
        ch2.set(&a, &bb);
    });
    cc.append(&chart.area);
    let axis = gtk::CenterBox::new();
    axis.set_direction(gtk::TextDirection::Ltr);
    axis.set_start_widget(Some(&label(&lc.t("ago10"), &["tiny"], false)));
    axis.set_center_widget(Some(&label(&lc.t("ago5"), &["tiny"], false)));
    axis.set_end_widget(Some(&label(&lc.t("now"), &["tiny"], false)));
    cc.append(&axis);
    fly.append(&cc);

    // tiles
    let grid = gtk::Grid::new();
    grid.set_column_homogeneous(true);
    grid.set_column_spacing(8);
    grid.set_row_spacing(8);
    for (i, (k, v)) in tiles(kind, app.clone()).into_iter().enumerate() {
        let t = tile(b, k, v);
        grid.attach(&t, (i % 2) as i32, (i / 2) as i32, 1, 1);
    }
    fly.append(&grid);
}

fn hero_text(c: &Ctx, kind: Option<Kind>, app: Option<&str>) -> String {
    let s = &c.st.snap;
    match (kind, app) {
        (Some(Kind::Cpu), _) => if s.cpu.valid { c.f.pct(s.cpu.total) } else { DASH.into() },
        (Some(Kind::Mem), _) => s.mem.as_ref().map(|m| c.f.pct(m.used_pct())).unwrap_or_else(|| DASH.into()),
        (Some(Kind::Nrg), _) => s.power.battery.as_ref().map(|b| c.f.pct(b.capacity)).or_else(|| s.power.rapl_watts.map(|w| c.f.w(w))).unwrap_or_else(|| DASH.into()),
        (Some(Kind::Thm), _) => c.f.ot(s.thermal.cpu_c, 0),
        (Some(Kind::Gpu), _) => c.f.opct(s.gpu.as_ref().and_then(|g| g.busy_pct)),
        (Some(Kind::Ssd), _) => s.disk.as_ref().map(|d| c.f.pct(d.used_pct())).unwrap_or_else(|| DASH.into()),
        (Some(Kind::Net), _) => c.f.rate(s.net.as_ref().and_then(|n| n.down_bps)),
        (None, Some(k)) => c.st.app(k).map(|a| c.f.pct1(a.cpu)).unwrap_or_else(|| DASH.into()),
        _ => DASH.into(),
    }
}

fn sub_text(c: &Ctx, kind: Option<Kind>, app: Option<&str>) -> String {
    let s = &c.st.snap;
    match (kind, app) {
        (Some(Kind::Cpu), _) => c.lc.tp("cpuDetailSub", &[("user", c.f.pct(s.cpu.user)), ("sys", c.f.pct(s.cpu.system)), ("cores", cores_label(c))]),
        (Some(Kind::Mem), _) => match mem_values(c) {
            Some((u, t, ..)) => c.lc.tp("memInUse", &[("used", u), ("total", t)]),
            None => String::new(),
        },
        (Some(Kind::Nrg), _) => match &s.power.battery {
            Some(b) if b.state == BattState::Charging => b.minutes_to_full.map(|m| c.lc.tp("chargingFullIn", &[("time", c.f.dur(m))])).unwrap_or_else(|| c.lc.t("charging")),
            Some(b) => b.minutes_left.map(|m| c.lc.tp("onBattLeft", &[("time", c.f.dur(m))])).unwrap_or_else(|| c.lc.t("onBatt")),
            None => c.lc.join(&[c.lc.t("noBattery"), if s.power.ac_online == Some(false) { String::new() } else { c.lc.t("acPower") }]),
        },
        (Some(Kind::Thm), _) => match s.thermal.cpu_c {
            Some(t) => {
                let lv = thermal_level(t);
                let thr = lv == 3 || s.thermal.throttle_events.map(|n| n > 0).unwrap_or(false);
                c.lc.join(&[c.lc.t(LEVEL_KEYS[lv]), c.lc.t(if thr { "throttling" } else { "zero" })])
            }
            None => String::new(),
        },
        (Some(Kind::Gpu), _) => match &s.gpu {
            Some(g) => c.lc.join(&[g.name.clone(), c.f.ot(s.thermal.gpu_c, 0)]),
            None => String::new(),
        },
        (Some(Kind::Ssd), _) => match &s.disk {
            Some(d) => c.lc.join(&[c.lc.hwp("fsRoot", &[("path", "/".into())]), c.lc.tp("free", &[("value", c.f.size(d.free as f64))])]),
            None => String::new(),
        },
        (Some(Kind::Net), _) => match &s.net {
            Some(n) => {
                let detail = if n.wireless { "Wi-Fi".to_string() } else { n.link_mbps.map(|m| format!("{} Mbps", c.f.n(m, 0))).unwrap_or_else(|| "Ethernet".into()) };
                let state = if n.wireless && n.signal_dbm.map(|d| d >= -60).unwrap_or(false) { c.lc.hw("strongSignal") } else { c.lc.hw("connected") };
                c.lc.hwp("netSub", &[("link", n.iface.clone()), ("detail", detail), ("state", state)])
            }
            None => String::new(),
        },
        (None, Some(k)) => match c.st.app(k) {
            Some(a) => c.lc.join(&[c.lc.plural("process", a.procs() as f64, None), c.lc.plural("thread", a.threads as f64, None), format!("PID {}", a.main_pid)]),
            None => String::new(),
        },
        _ => String::new(),
    }
}

fn series(c: &Ctx, kind: Option<Kind>, app: Option<&str>) -> (Vec<f64>, Vec<f64>) {
    let h = &c.st.hist;
    match (kind, app) {
        (Some(Kind::Cpu), _) => (h.long("cpu"), vec![]),
        (Some(Kind::Mem), _) => (h.long("mem"), vec![]),
        (Some(Kind::Nrg), _) => (h.long("nrg"), vec![]),
        (Some(Kind::Thm), _) => (h.long("thm"), vec![]),
        (Some(Kind::Gpu), _) => (h.long("gpu"), vec![]),
        (Some(Kind::Ssd), _) => (h.long("ssd_r"), h.long("ssd_w")),
        (Some(Kind::Net), _) => (h.long("net_d"), h.long("net_u")),
        (None, Some(k)) => (h.long(&format!("app:{k}")), vec![]),
        _ => (vec![], vec![]),
    }
}

fn value_fmt(f: &Fmt, kind: Option<Kind>, x: f64) -> String {
    match kind {
        Some(Kind::Nrg) => f.w(x),
        Some(Kind::Thm) => f.t(x, 1),
        Some(Kind::Ssd) | Some(Kind::Net) => f.rate(Some(x)),
        None => f.pct1(x),
        _ => f.pct(x),
    }
}

fn stat_text(c: &Ctx, kind: Option<Kind>, app: Option<&str>) -> String {
    let (a, bb) = series(c, kind, app);
    if a.is_empty() {
        return String::new();
    }
    let mx = a.iter().cloned().fold(f64::MIN, f64::max);
    if kind == Some(Kind::Ssd) || kind == Some(Kind::Net) {
        let mb = bb.iter().cloned().fold(0.0, f64::max);
        return c.lc.tp("peakSplit", &[("a", value_fmt(c.f, kind, mx)), ("b", value_fmt(c.f, kind, mb))]);
    }
    let avg = a.iter().sum::<f64>() / a.len() as f64;
    c.lc.tp("peakAvg", &[("peak", value_fmt(c.f, kind, mx)), ("avg", value_fmt(c.f, kind, avg))])
}

fn chart_label(b: &B, kind: Option<Kind>, _app: Option<String>) -> draw::ChartLabel {
    let lc = b.lc.clone();
    let f = b.f.clone();
    let unit = match kind {
        Some(Kind::Cpu) => lc.t("unitCpu"),
        Some(Kind::Mem) => lc.t("unitRam"),
        Some(Kind::Gpu) => lc.t("unitGpu"),
        None => lc.t("unitOfCpu"),
        _ => String::new(),
    };
    Rc::new(move |steps_back: usize, a: &[f64], bb: &[f64]| {
        let secs = steps_back * 5;
        let (m, s) = (secs / 60, secs % 60);
        let n = |x: usize| f.n(x as f64, 0);
        let ago = if secs == 0 {
            lc.t("now")
        } else if m == 0 {
            lc.tp("agoS", &[("s", n(s))])
        } else if s > 0 {
            lc.tp("agoMS", &[("m", n(m)), ("s", n(s))])
        } else {
            lc.tp("agoM", &[("m", n(m))])
        };
        let va = a.last().copied().unwrap_or(0.0);
        let val = match kind {
            Some(Kind::Ssd) => lc.join(&[value_fmt(&f, kind, va), value_fmt(&f, kind, bb.last().copied().unwrap_or(0.0))]),
            Some(Kind::Net) => lc.join(&[format!("↓ {}", value_fmt(&f, kind, va)), format!("↑ {}", value_fmt(&f, kind, bb.last().copied().unwrap_or(0.0)))]),
            _ if unit.is_empty() => value_fmt(&f, kind, va),
            _ => format!("{} {}", value_fmt(&f, kind, va), unit),
        };
        lc.join(&[ago, val])
    })
}

fn tiles(kind: Option<Kind>, app: Option<String>) -> Vec<TileFn> {
    let s = |f: fn(&Ctx) -> String| -> Box<dyn Fn(&Ctx) -> String> { Box::new(f) };
    match kind {
        Some(Kind::Cpu) => vec![
            (tk("tUser"), s(|c| c.f.pct(c.st.snap.cpu.user))),
            (tk("tSystem"), s(|c| c.f.pct(c.st.snap.cpu.system))),
            (tk("tIdle"), s(|c| c.f.pct(c.st.snap.cpu.idle))),
            (s(|c| group_labels(c.lc, &c.st.snap.cpu.groups).0), s(|c| c.f.pct(c.st.snap.cpu.group_a))),
            (s(|c| group_labels(c.lc, &c.st.snap.cpu.groups).1), s(|c| c.f.pct(c.st.snap.cpu.group_b))),
            (tk("tLoad"), s(|c| c.st.snap.cpu.load1.map(|l| c.f.n(l, 2)).unwrap_or_else(|| DASH.into()))),
        ],
        Some(Kind::Mem) => vec![
            (tk("segApp"), s(|c| mem_values(c).map(|v| v.2).unwrap_or_else(|| DASH.into()))),
            (s(|c| c.lc.hw("buffers")), s(|c| mem_values(c).map(|v| v.3).unwrap_or_else(|| DASH.into()))),
            (s(|c| c.st.snap.mem.as_ref().map(|m| m.compressed_kind).unwrap_or("zswap").to_string()), s(|c| mem_values(c).map(|v| v.4).unwrap_or_else(|| DASH.into()))),
            (tk("segFree"), s(|c| mem_values(c).map(|v| v.5).unwrap_or_else(|| DASH.into()))),
            (s(|c| c.lc.hw("swapUsed")), s(|c| match &c.st.snap.mem {
                Some(m) if m.swap_total > 0 => format!("{} / {} GB", c.f.gb(m.swap_used as f64, 1), c.f.gb_total(m.swap_total as f64)),
                Some(_) => format!("{} GB", c.f.n(0.0, 0)),
                None => DASH.into(),
            })),
            (s(|c| c.lc.hw("psiPressure")), s(|c| match c.st.snap.mem.as_ref().and_then(|m| m.psi_some_avg10) {
                Some(p) if pressure_level(c) == 0 => c.lc.hwp("pctNormal", &[("pct", c.f.pct1(p))]),
                Some(p) => c.lc.join(&[c.f.pct1(p), c.lc.t(PRESSURE_SHORT[pressure_level(c)])]),
                None => DASH.into(),
            })),
        ],
        Some(Kind::Nrg) => vec![
            (tk("tDraw"), s(flow)),
            (tk("tTimeLeft"), s(|c| {
                let t = batt_sub(c);
                if t.is_empty() || c.st.snap.power.battery.is_none() { DASH.into() } else { t }
            })),
            (tk("tHealth"), s(|c| c.f.opct(c.st.snap.power.battery.as_ref().and_then(|b| b.health_pct)))),
            (tk("tCycles"), s(|c| c.st.snap.power.battery.as_ref().and_then(|b| b.cycles).map(|n| c.f.n(n as f64, 0)).unwrap_or_else(|| DASH.into()))),
            (tk("tCapacity"), s(|c| match c.st.snap.power.battery.as_ref().map(|b| (b.energy_full_wh, b.energy_design_wh)) {
                Some((Some(a), Some(d))) => format!("{} / {} Wh", c.f.n(a, 1), c.f.n(d, 1)),
                Some((Some(a), None)) => format!("{} Wh", c.f.n(a, 1)),
                _ => DASH.into(),
            })),
            (tk("tSource"), s(|c| match (c.st.snap.power.ac_online, &c.st.snap.power.battery) {
                (Some(true), _) => c.lc.t("acPower"),
                (_, Some(_)) => c.lc.t("tBattery"),
                (Some(false), None) | (None, None) => c.lc.t("noBattery"),
            })),
        ],
        Some(Kind::Thm) => vec![
            (tk("tCpuDie"), s(|c| c.f.ot(c.st.snap.thermal.cpu_c, 1))),
            (tk("gpu"), s(|c| c.f.ot(c.st.snap.thermal.gpu_c, 1))),
            (tk("tStorage"), s(|c| c.f.ot(c.st.snap.thermal.nvme_c, 0))),
            (tk("tBattery"), s(|c| c.f.ot(c.st.snap.thermal.battery_c, 0))),
            (tk("tFans"), s(|c| {
                let f = &c.st.snap.thermal.fans_rpm;
                if f.is_empty() { DASH.into() } else { c.lc.join(&f.iter().map(|r| format!("{} RPM", c.f.n(*r as f64, 0))).collect::<Vec<_>>()) }
            })),
            (tk("tThrottling"), s(|c| c.st.snap.thermal.throttle_events.map(|n| c.f.n(n as f64, 0)).unwrap_or_else(|| DASH.into()))),
        ],
        Some(Kind::Gpu) => vec![
            (tk("util"), s(|c| c.f.opct(c.st.snap.gpu.as_ref().and_then(|g| g.busy_pct)))),
            (tk("tTemperature"), s(|c| c.f.ot(c.st.snap.gpu.as_ref().and_then(|g| g.temp_c).or(c.st.snap.thermal.gpu_c), 1))),
            (tk("tVideoMem"), s(|c| match c.st.snap.gpu.as_ref().map(|g| (g.vram_used, g.vram_total)) {
                Some((Some(u), Some(t))) => format!("{} / {} GB", c.f.gb(u as f64, 1), c.f.gb_total(t as f64)),
                _ => DASH.into(),
            })),
            (tk("tCoreClock"), s(|c| c.st.snap.gpu.as_ref().and_then(|g| g.clock_mhz).map(|m| c.f.mhz(m)).unwrap_or_else(|| DASH.into()))),
            (tk("tPower"), s(|c| c.st.snap.gpu.as_ref().and_then(|g| g.power_w).map(|w| c.f.w(w)).unwrap_or_else(|| DASH.into()))),
            (tk("tHotspot"), s(|c| c.f.ot(c.st.snap.gpu.as_ref().and_then(|g| g.hotspot_c), 1))),
        ],
        Some(Kind::Ssd) => vec![
            (tk("tUsed"), s(|c| c.st.snap.disk.as_ref().map(|d| c.f.size(d.used as f64)).unwrap_or_else(|| DASH.into()))),
            (tk("tFree"), s(|c| c.st.snap.disk.as_ref().map(|d| c.f.size(d.free as f64)).unwrap_or_else(|| DASH.into()))),
            (tk("read"), s(|c| c.f.rate(c.st.snap.disk.as_ref().and_then(|d| d.read_bps)))),
            (tk("write"), s(|c| c.f.rate(c.st.snap.disk.as_ref().and_then(|d| d.write_bps)))),
            (tk("tHealth"), s(|c| c.st.snap.thermal.nvme_c.map(|t| c.f.t(t, 0)).unwrap_or_else(|| DASH.into()))),
            (tk("tFormat"), s(|c| c.st.snap.disk.as_ref().map(|d| d.fstype.clone()).filter(|x| !x.is_empty()).unwrap_or_else(|| DASH.into()))),
        ],
        Some(Kind::Net) => vec![
            (tk("down"), s(|c| c.f.rate(c.st.snap.net.as_ref().and_then(|n| n.down_bps)))),
            (tk("up"), s(|c| c.f.rate(c.st.snap.net.as_ref().and_then(|n| n.up_bps)))),
            (tk("tInterface"), s(|c| c.st.snap.net.as_ref().map(|n| format!("{} · {}", n.iface, if n.wireless { "Wi-Fi" } else { "Ethernet" })).unwrap_or_else(|| DASH.into()))),
            (
                s(|c| c.lc.hw(if c.st.snap.net.as_ref().map(|n| n.wireless).unwrap_or(false) { "signal" } else { "linkSpeed" })),
                s(|c| match &c.st.snap.net {
                    Some(n) if n.wireless => n.signal_dbm.map(|d| format!("{}{} dBm", if d < 0 { "−" } else { "" }, c.f.n(d.abs() as f64, 0))).unwrap_or_else(|| DASH.into()),
                    Some(n) => n.link_mbps.map(|m| format!("{} Mbps", c.f.n(m, 0))).unwrap_or_else(|| DASH.into()),
                    None => DASH.into(),
                }),
            ),
            (tk("tLatency"), s(|_| DASH.into())),
            (tk("tToday"), s(|c| c.st.snap.net.as_ref().map(|n| format!("↓ {} · ↑ {}", c.f.size(n.rx_total as f64), c.f.size(n.tx_total as f64))).unwrap_or_else(|| DASH.into()))),
        ],
        None => {
            let k = app.unwrap_or_default();
            let g = move |f: fn(&Ctx, &AppGroup) -> String| -> Box<dyn Fn(&Ctx) -> String> {
                let k = k.clone();
                Box::new(move |c: &Ctx| c.st.app(&k).map(|a| f(c, a)).unwrap_or_else(|| DASH.into()))
            };
            vec![
                (tk("cpu"), g(|c, a| c.f.pct1(a.cpu))),
                (tk("mem"), g(|c, a| c.f.size(a.mem_bytes as f64))),
                (tk("gpu"), g(|c, a| c.f.opct(a.gpu))),
                (tk("tipProc"), g(|c, a| c.f.n(a.procs() as f64, 0))),
                (tk("tThreads"), g(|c, a| c.f.n(a.threads as f64, 0))),
                (Box::new(|_: &Ctx| "PID".to_string()), g(|_, a| a.main_pid.to_string())),
            ]
        }
    }
}

// ---------------------------------------------------------------------------
// settings and footer
// ---------------------------------------------------------------------------

fn switch_row(b: &mut B, title: &str, sub: &str, on: bool, set: impl Fn(&Rc<Ctl>, bool) + 'static) -> gtk::Box {
    let row = hbox(10);
    row.add_css_class("switch-row");
    let txt = vbox(1);
    txt.set_hexpand(true);
    let t = label(title, &["strong"], true);
    full_tip(&t);
    txt.append(&t);
    let s = label(sub, &["sub"], true);
    full_tip(&s);
    txt.append(&s);
    row.append(&txt);
    let sw = gtk::Switch::new();
    sw.set_valign(gtk::Align::Center);
    sw.set_active(on);
    set_a11y(&sw, title);
    let c = b.ctl.clone();
    sw.connect_state_set(move |_, v| {
        set(&c, v);
        glib::Propagation::Proceed
    });
    row.append(&sw);
    row
}

/// Language choice: "Match system" plus every locale by its own name.
fn language_row(b: &mut B) -> gtk::Box {
    let lc = b.lc.clone();
    let row = hbox(10);
    row.add_css_class("switch-row");
    let txt = vbox(1);
    txt.set_hexpand(true);
    let t = label(&lc.t("language"), &["strong"], true);
    full_tip(&t);
    txt.append(&t);
    let s = label(&lc.t("languageSub"), &["sub"], true);
    full_tip(&s);
    txt.append(&s);
    row.append(&txt);
    let list = b.ctl.st.borrow().registry.list();
    let mut names = vec![lc.t("langSystem")];
    names.extend(list.iter().map(|l| l.name.clone()));
    let refs: Vec<&str> = names.iter().map(String::as_str).collect();
    let dd = gtk::DropDown::from_strings(&refs);
    dd.add_css_class("lang-drop");
    dd.set_valign(gtk::Align::Center);
    set_a11y(&dd, &lc.t("language"));
    let pref = b.ctl.st.borrow().lang_pref.clone();
    let sel = match &pref {
        None => 0,
        Some(code) => list.iter().position(|l| &l.code == code).map(|i| i + 1).unwrap_or(0),
    };
    dd.set_selected(sel as u32);
    let codes: Vec<String> = list.iter().map(|l| l.code.clone()).collect();
    let c = b.ctl.clone();
    dd.connect_selected_notify(move |d| {
        let i = d.selected() as usize;
        let want = if i == 0 { None } else { codes.get(i - 1).cloned() };
        let cur = c.st.borrow().lang_pref.clone();
        if want != cur {
            // let the popover close before the tree is rebuilt
            let c2 = c.clone();
            glib::idle_add_local_once(move || c2.choose_lang(want));
        }
    });
    row.append(&dd);
    row
}

fn settings(b: &mut B, fly: &gtk::Box) {
    let lc = b.lc.clone();
    let r = hbox(8);
    let back = gtk::Button::new();
    back.add_css_class("pillbtn");
    let bb = hbox(4);
    bb.append(&draw::icon("back", 12, true));
    bb.append(&label(&lc.t("back"), &[], false));
    back.set_child(Some(&bb));
    let c = b.ctl.clone();
    back.connect_clicked(move |_| c.set_view(View::Overview));
    r.append(&back);
    r.append(&label(&lc.t("aSettings"), &["h-card"], true));
    fly.append(&r);

    let (sim_hog, sim_chg, live, unit_f) = {
        let st = b.ctl.st.borrow();
        (st.sim_hog, st.sim_charging, st.live, st.unit_f)
    };
    let card = vbox(6);
    card.add_css_class("card");
    card.append(&switch_row(b, &lc.t("simHog"), &lc.tp("simHogSub", &[("pct", b.f.pct(50.0))]), sim_hog, |c, v| c.set_sim(Some(v), None)));
    card.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
    card.append(&switch_row(b, &lc.t("simCharging"), &lc.tp("simChargingSub", &[("adapter", "USB-C 96 W".into())]), sim_chg, |c, v| c.set_sim(None, Some(v))));
    card.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
    let secs = lc.tp("seconds", &[("n", b.f.n(1.5, 1))]);
    card.append(&switch_row(b, &lc.t("simLive"), &lc.tp("simLiveSub", &[("secs", secs)]), live, |c, v| c.set_live(v)));
    card.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
    card.append(&language_row(b));
    card.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
    // temperature unit
    let row = hbox(10);
    row.add_css_class("switch-row");
    row.append(&label(&lc.t("tempUnit"), &["strong"], true));
    let seg = hbox(0);
    seg.add_css_class("seg");
    seg.set_direction(gtk::TextDirection::Ltr);
    for (txt, f) in [("°C", false), ("°F", true)] {
        let btn = gtk::Button::with_label(txt);
        if unit_f == f {
            btn.add_css_class("on");
        }
        let c = b.ctl.clone();
        btn.connect_clicked(move |_| c.set_unit(f));
        seg.append(&btn);
    }
    row.append(&seg);
    card.append(&row);
    card.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
    // restore ended apps
    let row = hbox(10);
    row.add_css_class("switch-row");
    let txt = vbox(1);
    txt.set_hexpand(true);
    txt.append(&label(&lc.t("restore"), &["strong"], true));
    let ended = b.dyn_label(&["sub"], true, |c| c.lc.plural("ended", c.st.ended.len() as f64, None));
    full_tip(&ended);
    txt.append(&ended);
    row.append(&txt);
    let rb = pill_btn(&lc.t("restoreBtn"), &[]);
    let c = b.ctl.clone();
    rb.connect_clicked(move |_| c.restore_apps());
    let rb2 = rb.clone();
    b.bind(move |c| {
        let any = !c.st.ended.is_empty();
        rb2.set_sensitive(any);
        rb2.set_opacity(if any { 1.0 } else { 0.5 });
    });
    row.append(&rb);
    card.append(&row);
    fly.append(&card);
}

fn footer(b: &mut B) -> gtk::Widget {
    let lc = b.lc.clone();
    let r = hbox(8);
    r.add_css_class("footer");
    let open = gtk::Button::new();
    open.add_css_class("pillbtn");
    let ob = hbox(6);
    let mon = lc.tp("openMonitor", &[("monitor", lc.hw("systemMonitor"))]);
    let ol = label(&mon, &[], true);
    cap(&ol, 24);
    full_tip(&ol);
    ob.append(&ol);
    ob.append(&draw::icon("ext", 11, true));
    open.set_child(Some(&ob));
    open.set_hexpand(false);
    let c = b.ctl.clone();
    open.connect_clicked(move |_| c.open_monitor());
    r.append(&open);
    let p = hbox(5);
    p.set_hexpand(true);
    p.set_halign(gtk::Align::Center);
    let li = draw::icon("lock", 11, false);
    li.add_css_class("dim3");
    p.append(&li);
    let pl = label(&lc.t("privacy"), &["tiny"], true);
    cap(&pl, 24);
    full_tip(&pl);
    p.append(&pl);
    r.append(&p);
    let quit = pill_btn(&lc.t("quit"), &["flat"]);
    let c = b.ctl.clone();
    quit.connect_clicked(move |_| c.quit());
    r.append(&quit);
    r.upcast()
}

/// Data for the top bar entry: wave, readouts and hog state.
pub fn tray_state(c: &Ctx) -> TrayState {
    let s = &c.st.snap;
    let mut parts = vec![
        format!("{} {}", c.lc.t("barCpu"), if s.cpu.valid { c.f.pct(s.cpu.total) } else { DASH.into() }),
        format!("{} {}", c.lc.t("barRam"), s.mem.as_ref().map(|m| c.f.pct(m.used_pct())).unwrap_or_else(|| DASH.into())),
    ];
    parts.push(match watts(c) {
        Some(w) => format!("⚡{}W", c.f.n(w, 1)),
        None => format!("⚡{DASH}"),
    });
    parts.push(format!("↓{}", c.f.rate(s.net.as_ref().and_then(|n| n.down_bps))));
    let label = parts.join("  ");
    let hog = c.st.hog_shown().map(|h| c.lc.tp("hogTitle", &[("app", c.lc.app(&h.name)), ("pct", c.f.pct(h.cpu))]));
    TrayState {
        label,
        title: "Pulse".into(),
        tooltip: hog.clone().unwrap_or_else(|| c.lc.t("calm")),
        wave: c.st.hist.spark("cpu"),
        hog: hog.is_some(),
    }
}

#[allow(dead_code)]
fn _unused(_: &State) {}
