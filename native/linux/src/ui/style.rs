//! Part 1 color tokens and the GTK stylesheet built from them.

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Rgba(pub f64, pub f64, pub f64, pub f64);

impl Rgba {
    pub const fn hex(h: u32) -> Rgba {
        Rgba(((h >> 16) & 255) as f64 / 255.0, ((h >> 8) & 255) as f64 / 255.0, (h & 255) as f64 / 255.0, 1.0)
    }
    pub const fn a(self, a: f64) -> Rgba {
        Rgba(self.0, self.1, self.2, self.3 * a)
    }
    pub fn css(&self) -> String {
        format!(
            "rgba({},{},{},{:.3})",
            (self.0 * 255.0).round(),
            (self.1 * 255.0).round(),
            (self.2 * 255.0).round(),
            self.3
        )
    }
    pub fn set(&self, cr: &gtk::cairo::Context) {
        cr.set_source_rgba(self.0, self.1, self.2, self.3);
    }
}

/// OKLCH to sRGB, for the app monogram colors.
pub fn oklch(l: f64, c: f64, h_deg: f64, alpha: f64) -> Rgba {
    let h = h_deg.to_radians();
    let (a, b) = (c * h.cos(), c * h.sin());
    let l_ = l + 0.3963377774 * a + 0.2158037573 * b;
    let m_ = l - 0.1055613458 * a - 0.0638541728 * b;
    let s_ = l - 0.0894841775 * a - 1.2914855480 * b;
    let (l3, m3, s3) = (l_.powi(3), m_.powi(3), s_.powi(3));
    let r = 4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3;
    let g = -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3;
    let bl = -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3;
    let gamma = |x: f64| {
        let x = x.clamp(0.0, 1.0);
        if x <= 0.0031308 { 12.92 * x } else { 1.055 * x.powf(1.0 / 2.4) - 0.055 }
    };
    Rgba(gamma(r), gamma(g), gamma(bl), alpha)
}

#[derive(Clone, Debug)]
pub struct Pal {
    pub dark: bool,
    pub fly: Rgba,
    pub fly_border: Rgba,
    pub card: Rgba,
    pub card_hover: Rgba,
    pub card_b: Rgba,
    pub ink: Rgba,
    pub ink2: Rgba,
    pub ink3: Rgba,
    pub track: Rgba,
    pub track_strong: Rgba,
    pub hover: Rgba,
    pub seg_pill: Rgba,
    pub cpu: Rgba,
    pub mem: Rgba,
    pub nrg: Rgba,
    pub thm: Rgba,
    pub gpu: Rgba,
    pub warn: Rgba,
    pub crit: Rgba,
    pub cpu_ink: Rgba,
    pub mem_ink: Rgba,
    pub nrg_ink: Rgba,
    pub thm_ink: Rgba,
    pub gpu_ink: Rgba,
    pub warn_ink: Rgba,
    pub crit_ink: Rgba,
    pub danger: Rgba,
    pub danger_on: Rgba,
}

impl Pal {
    pub fn light() -> Pal {
        Pal {
            dark: false,
            fly: Rgba::hex(0xF5F8F6),
            fly_border: Rgba::hex(0x0E1E19).a(0.08),
            card: Rgba(1.0, 1.0, 1.0, 0.78),
            card_hover: Rgba(1.0, 1.0, 1.0, 0.96),
            card_b: Rgba::hex(0x0E1E19).a(0.06),
            ink: Rgba::hex(0x0E1E19),
            ink2: Rgba::hex(0x4B635B),
            ink3: Rgba::hex(0x5E746C),
            track: Rgba::hex(0x0E1E19).a(0.07),
            track_strong: Rgba::hex(0x0E1E19).a(0.2),
            hover: Rgba::hex(0x0E1E19).a(0.045),
            seg_pill: Rgba::hex(0xFFFFFF),
            cpu: Rgba::hex(0x10B981),
            mem: Rgba::hex(0x0EA5E9),
            nrg: Rgba::hex(0xF59E0B),
            thm: Rgba::hex(0xF43F5E),
            gpu: Rgba::hex(0x14B8A6),
            warn: Rgba::hex(0xF97316),
            crit: Rgba::hex(0xEF4444),
            cpu_ink: Rgba::hex(0x047857),
            mem_ink: Rgba::hex(0x0369A1),
            nrg_ink: Rgba::hex(0xB45309),
            thm_ink: Rgba::hex(0xBE123C),
            gpu_ink: Rgba::hex(0x0F766E),
            warn_ink: Rgba::hex(0xC2410C),
            crit_ink: Rgba::hex(0xB91C1C),
            danger: Rgba::hex(0xDC2626),
            danger_on: Rgba::hex(0xFFFFFF),
        }
    }

    pub fn dark() -> Pal {
        let ink = Rgba::hex(0xECFDF5);
        Pal {
            dark: true,
            fly: Rgba::hex(0x0F1A16),
            fly_border: Rgba::hex(0xA7F3D0).a(0.12),
            card: Rgba(1.0, 1.0, 1.0, 0.055),
            card_hover: Rgba(1.0, 1.0, 1.0, 0.09),
            card_b: Rgba(1.0, 1.0, 1.0, 0.08),
            ink,
            ink2: ink.a(0.68),
            ink3: ink.a(0.52),
            track: Rgba(1.0, 1.0, 1.0, 0.08),
            track_strong: Rgba(1.0, 1.0, 1.0, 0.22),
            hover: Rgba(1.0, 1.0, 1.0, 0.05),
            seg_pill: Rgba(1.0, 1.0, 1.0, 0.16),
            cpu: Rgba::hex(0x34D399),
            mem: Rgba::hex(0x38BDF8),
            nrg: Rgba::hex(0xFBBF24),
            thm: Rgba::hex(0xFB7185),
            gpu: Rgba::hex(0x2DD4BF),
            warn: Rgba::hex(0xFB923C),
            crit: Rgba::hex(0xF87171),
            cpu_ink: Rgba::hex(0x34D399),
            mem_ink: Rgba::hex(0x38BDF8),
            nrg_ink: Rgba::hex(0xFBBF24),
            thm_ink: Rgba::hex(0xFB7185),
            gpu_ink: Rgba::hex(0x2DD4BF),
            warn_ink: Rgba::hex(0xFDBA74),
            crit_ink: Rgba::hex(0xFCA5A5),
            danger: Rgba::hex(0xF87171),
            danger_on: Rgba::hex(0x2A0E00),
        }
    }

    /// (color, ink) for an accent name.
    pub fn acc(&self, k: &str) -> (Rgba, Rgba) {
        match k {
            "cpu" => (self.cpu, self.cpu_ink),
            "mem" => (self.mem, self.mem_ink),
            "nrg" => (self.nrg, self.nrg_ink),
            "thm" => (self.thm, self.thm_ink),
            "gpu" => (self.gpu, self.gpu_ink),
            "warn" => (self.warn, self.warn_ink),
            _ => (self.crit, self.crit_ink),
        }
    }

    /// Tint over the card: the accent at a low alpha.
    pub fn tint(&self, c: Rgba, pct: f64) -> Rgba {
        c.a(pct / 100.0)
    }
}

/// The whole stylesheet. `ui` and `hero` are CSS font-family lists from
/// the locale and the Linux platform stack. `opaque` is set when the
/// display has no compositor, so the flyout cannot have see-through corners.
pub fn css(p: &Pal, ui: &str, hero: &str, opaque: bool, rtl: bool) -> String {
    let c = |x: Rgba| x.css();
    let accents = ["cpu", "mem", "nrg", "thm", "gpu", "warn", "crit"]
        .iter()
        .map(|k| {
            let (col, ink) = p.acc(k);
            let tint_pct = if *k == "warn" { 16.0 } else { 13.0 };
            format!(
                ".ink-{k} {{ color: {ink}; }}\n\
                 .pill.{k}, .badge.{k} {{ background-color: {tint}; color: {ink}; }}\n\
                 .dot.{k} {{ background-color: {col}; }}\n\
                 .chip.{k} {{ background-color: {tint}; color: {ink}; border-color: {line}; }}\n",
                ink = c(ink),
                tint = c(p.tint(col, tint_pct)),
                col = c(col),
                line = c(col.a(0.35))
            )
        })
        .collect::<String>();
    let window_bg = if opaque { c(p.fly) } else { "transparent".into() };
    let radius = if opaque { "0" } else { "18px" };
    let shadow = if opaque {
        "none".to_string()
    } else if p.dark {
        "inset 0 1px 0 rgba(255,255,255,0.06), 0 18px 40px -10px rgba(0,0,0,0.6)".to_string()
    } else {
        "inset 0 0 0 1px rgba(255,255,255,0.85), 0 18px 40px -12px rgba(14,30,25,0.24), 0 4px 14px rgba(14,30,25,0.06)".to_string()
    };
    let margin = if opaque { "0" } else { "10px 14px 22px 14px" };
    let hog_bg = format!(
        "linear-gradient({}, {}, {})",
        if rtl { "to left" } else { "to right" },
        c(p.warn.a(if p.dark { 0.18 } else { 0.13 })),
        c(p.crit.a(if p.dark { 0.12 } else { 0.08 }))
    );
    format!(
        r#"
window.pulse-window, window.pulse-window > contents {{ background: {window_bg}; box-shadow: none; }}
.fly {{
  background-color: {fly}; color: {ink}; border-radius: {radius}; border: 1px solid {fly_border};
  box-shadow: {shadow}; margin: {margin}; padding: 12px; font-family: {ui}; font-size: 12px;
}}
.fly label {{ font-feature-settings: "tnum"; }}
.fly button {{ min-height: 0; min-width: 0; padding: 0; background-image: none; box-shadow: none; text-shadow: none; -gtk-icon-shadow: none; outline-color: {focus}; }}
.fly button:focus-visible {{ outline: 2px solid {focus}; outline-offset: 1px; }}
.title {{ font-weight: 700; font-size: 15px; }}
.hero {{ font-family: {hero}; font-weight: 700; font-size: 28px; letter-spacing: -0.5px; }}
.hero-sm {{ font-family: {hero}; font-weight: 700; font-size: 26px; }}
.h-card {{ font-weight: 600; font-size: 12.5px; }}
.sub {{ color: {ink2}; font-size: 11px; }}
.tiny {{ color: {ink3}; font-size: 10.5px; }}
.legend {{ font-size: 10px; }}
.min1 {{ color: {ink3}; font-size: 9.5px; }}
.val {{ font-weight: 700; font-size: 11px; }}
.dim {{ color: {ink2}; }}
.dim3 {{ color: {ink3}; }}
.strong {{ font-weight: 700; }}
.danger-ink {{ color: {crit_ink}; }}

button.card, .card {{
  background-color: {card}; border: 1px solid {card_b}; border-radius: 12px; padding: 9px 11px 6px 11px; color: {ink};
}}
button.card:hover {{ background-color: {card_hover}; }}
button.card.warn-border {{ border-color: {warn_line}; }}
.card.strip {{ padding: 7px 11px; }}
.card.tile {{ padding: 8px 11px; }}
.card.chart-card {{ padding: 10px 12px 8px 12px; }}

.badge {{ border-radius: 8px; min-width: 24px; min-height: 24px; }}
.pill {{ border-radius: 999px; padding: 2px 8px; font-weight: 700; font-size: 10.5px; }}
.pill.plain {{ background-color: {track}; color: {ink2}; font-weight: 600; }}
.chip {{ border-radius: 999px; padding: 2px 8px; font-weight: 700; font-size: 11.5px; border: 1px solid transparent; }}
.dot {{ border-radius: 999px; min-width: 7px; min-height: 7px; }}
.legend-sq {{ border-radius: 2px; min-width: 7px; min-height: 7px; }}
.legend-sq.l1 {{ background-color: {mem}; }}
.legend-sq.l2 {{ background-color: {mem_soft}; }}
.legend-sq.l3 {{ background-color: {mem85}; }}
.legend-sq.free {{ background-color: {track_strong}; }}
.procs {{ background-color: {track}; color: {ink2}; border-radius: 5px; padding: 0 4px; font-size: 10px; }}

.status-pill {{ border-radius: 999px; padding: 2px 8px 2px 6px; font-weight: 700; font-size: 11px; }}
.status-pill.calm {{ background-color: {cpu_tint}; color: {cpu_ink}; }}
.status-pill.hog {{ background-color: {warn_tint}; color: {warn_ink}; }}

button.round {{ border-radius: 999px; min-width: 28px; min-height: 28px; background-color: {card}; border: 1px solid {card_b}; color: {ink2}; }}
button.round:hover {{ background-color: {card_hover}; }}
button.round.active {{ border-color: {mem_line}; color: {mem_ink}; }}
.seg {{ background-color: {track}; border-radius: 999px; padding: 2px; }}
.seg button {{ border-radius: 999px; padding: 2px 9px; font-weight: 700; font-size: 11.5px; color: {ink2}; background-color: transparent; }}
.seg button.on {{ background-color: {seg_pill}; color: {ink}; box-shadow: 0 1px 3px rgba(14,30,25,0.14); }}
.seg.lang button {{ min-width: 26px; padding: 2px 6px; }}
menubutton.lang-menu > button {{ border-radius: 999px; padding: 3px 10px; background-color: {card}; border: 1px solid {card_b}; font-weight: 700; color: {ink}; }}

button.pillbtn {{ border-radius: 999px; padding: 5px 12px; font-weight: 700; font-size: 12px; background-color: {card}; border: 1px solid {card_b}; color: {ink}; }}
button.pillbtn:hover {{ background-color: {card_hover}; }}
button.pillbtn.flat {{ background-color: transparent; border-color: transparent; }}
button.pillbtn.danger {{ background-color: {danger}; color: {danger_on}; border-color: transparent; }}
button.pillbtn.warn {{ background-color: {warn_tint}; color: {warn_ink}; border-color: {warn_line}; }}
button.iconbtn {{ border-radius: 999px; min-width: 24px; min-height: 24px; background-color: transparent; color: {ink2}; }}
button.iconbtn:hover {{ background-color: {hover}; }}

.hog {{ background-image: {hog_bg}; border: 1px solid {warn_line}; border-radius: 12px; padding: 7px 8px 7px 10px; }}
.hog .hog-icon {{ background-color: {warn_tint}; color: {warn_ink}; border-radius: 8px; min-width: 26px; min-height: 26px; }}
.notice {{ background-color: {card_hover}; border: 1px solid {danger_line}; border-radius: 12px; padding: 9px 11px; }}
.toast {{ background-color: {cpu_tint}; color: {cpu_ink}; border: 1px solid {cpu_line}; border-radius: 12px; padding: 9px 11px; font-weight: 600; }}

.app-row {{ border-radius: 9px; padding: 0 6px; min-height: 34px; }}
.app-row:hover, .app-row:focus-within {{ background-color: {hover}; }}
.app-row:focus-visible {{ outline: 2px solid {focus}; outline-offset: -2px; }}
.app-row .end-btn {{ opacity: 0; }}
.app-row:hover .end-btn, .app-row:focus-within .end-btn {{ opacity: 1; }}
.app-row:hover .metric, .app-row:focus-within .metric {{ opacity: 0; }}
button.end-btn {{ border-radius: 999px; padding: 2px 10px; font-weight: 700; font-size: 11px; background-color: {crit_tint}; color: {crit_ink}; border: 1px solid {danger_line}; }}
.mono {{ border-radius: 7px; min-width: 22px; min-height: 22px; font-weight: 700; font-size: 11px; }}
.metric {{ font-weight: 600; font-size: 12px; }}

.footer button.pillbtn {{ font-size: 12px; }}
.switch-row {{ padding: 4px 0; }}
dropdown.lang-drop > button {{ border-radius: 999px; padding: 3px 10px; background-color: {card}; border: 1px solid {card_b}; color: {ink}; font-weight: 600; }}
switch {{ background-color: {track_strong}; border-radius: 999px; }}
switch:checked {{ background-color: {cpu}; }}
tooltip.background {{ background-color: rgba(255,255,255,0.96); color: #0E1E19; border-radius: 12px; }}
{accents}
"#,
        window_bg = window_bg,
        fly = c(p.fly),
        ink = c(p.ink),
        ink2 = c(p.ink2),
        ink3 = c(p.ink3),
        radius = radius,
        fly_border = c(p.fly_border),
        shadow = shadow,
        margin = margin,
        ui = ui,
        hero = hero,
        focus = c(p.mem),
        crit_ink = c(p.crit_ink),
        card = c(p.card),
        card_hover = c(p.card_hover),
        card_b = c(p.card_b),
        warn_line = c(p.warn.a(0.34)),
        track = c(p.track),
        track_strong = c(p.track_strong),
        mem_soft = c(p.mem.a(0.55)),
        mem = c(p.mem),
        mem85 = c(p.mem.a(0.85)),
        cpu_tint = c(p.cpu.a(0.12)),
        cpu_ink = c(p.cpu_ink),
        cpu_line = c(p.cpu.a(0.35)),
        warn_tint = c(p.warn.a(0.16)),
        warn_ink = c(p.warn_ink),
        mem_line = c(p.mem.a(0.35)),
        mem_ink = c(p.mem_ink),
        seg_pill = c(p.seg_pill),
        danger = c(p.danger),
        danger_on = c(p.danger_on),
        danger_line = c(p.crit.a(0.4)),
        crit_tint = c(p.crit.a(0.12)),
        hover = c(p.hover),
        hog_bg = hog_bg,
        cpu = c(p.cpu),
        accents = accents,
    )
}
