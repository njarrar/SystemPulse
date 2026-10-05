//! Cairo drawing: icons, bars, sparklines, the 10-minute chart, the memory
//! bar, the thermal meter, the logo and the top bar icon.

use super::style::Rgba;
use gtk::cairo::{Context, LineCap, LineJoin, LinearGradient};
use gtk::prelude::*;
use std::cell::{Cell, RefCell};
use std::f64::consts::PI;
use std::rc::Rc;

fn widget_color(w: &impl IsA<gtk::Widget>) -> Rgba {
    let c = w.as_ref().color();
    Rgba(c.red() as f64, c.green() as f64, c.blue() as f64, c.alpha() as f64)
}

fn rrect(cr: &Context, x: f64, y: f64, w: f64, h: f64, r: f64) {
    let r = r.min(w / 2.0).min(h / 2.0);
    cr.new_sub_path();
    cr.arc(x + w - r, y + r, r, -PI / 2.0, 0.0);
    cr.arc(x + w - r, y + h - r, r, 0.0, PI / 2.0);
    cr.arc(x + r, y + h - r, r, PI / 2.0, PI);
    cr.arc(x + r, y + r, r, PI, 1.5 * PI);
    cr.close_path();
}

/// Draws a 16-unit icon path. Strokes use the current source.
pub fn icon_path(cr: &Context, name: &str) {
    cr.set_line_width(1.5);
    cr.set_line_cap(LineCap::Round);
    cr.set_line_join(LineJoin::Round);
    match name {
        "cpu" => {
            rrect(cr, 4.0, 4.0, 8.0, 8.0, 1.5);
            cr.stroke().ok();
            rrect(cr, 6.5, 6.5, 3.0, 3.0, 0.5);
            cr.fill().ok();
            for i in [6.0, 10.0] {
                for (a, b, c, d) in [(i, 1.8, i, 4.0), (i, 12.0, i, 14.2), (1.8, i, 4.0, i), (12.0, i, 14.2, i)] {
                    cr.move_to(a, b);
                    cr.line_to(c, d);
                }
            }
            cr.stroke().ok();
        }
        "mem" => {
            rrect(cr, 1.8, 4.5, 12.4, 6.5, 1.2);
            cr.stroke().ok();
            for x in [5.0, 8.0, 11.0] {
                cr.move_to(x, 6.8);
                cr.line_to(x, 8.7);
            }
            for x in [4.0, 6.5, 9.5, 12.0] {
                cr.move_to(x, 11.0);
                cr.line_to(x, 12.8);
            }
            cr.stroke().ok();
        }
        "nrg" => {
            cr.move_to(9.0, 1.8);
            cr.line_to(3.8, 9.0);
            cr.line_to(8.0, 9.0);
            cr.line_to(7.0, 14.2);
            cr.line_to(12.2, 7.0);
            cr.line_to(8.0, 7.0);
            cr.close_path();
            cr.stroke().ok();
        }
        "thm" => {
            cr.move_to(6.5, 9.5);
            cr.line_to(6.5, 3.3);
            cr.arc(8.0, 3.3, 1.5, PI, 0.0);
            cr.line_to(9.5, 9.5);
            cr.arc(8.0, 11.6, 2.6, -0.95, PI + 0.95);
            cr.close_path();
            cr.stroke().ok();
            cr.arc(8.0, 11.6, 1.1, 0.0, 2.0 * PI);
            cr.fill().ok();
        }
        "gpu" => {
            rrect(cr, 1.8, 4.0, 12.4, 8.0, 1.5);
            cr.stroke().ok();
            cr.arc(10.2, 8.0, 2.0, 0.0, 2.0 * PI);
            cr.stroke().ok();
            cr.move_to(4.2, 6.6);
            cr.line_to(4.2, 9.4);
            cr.move_to(6.2, 6.6);
            cr.line_to(6.2, 9.4);
            cr.stroke().ok();
        }
        "ssd" => {
            rrect(cr, 2.5, 7.0, 11.0, 6.0, 1.5);
            cr.stroke().ok();
            cr.move_to(4.5, 7.0);
            cr.line_to(5.5, 3.0);
            cr.line_to(10.5, 3.0);
            cr.line_to(11.5, 7.0);
            cr.stroke().ok();
            cr.arc(11.0, 10.0, 0.8, 0.0, 2.0 * PI);
            cr.fill().ok();
        }
        "net" => {
            for (r, a) in [(10.5, 0.75), (7.0, 0.75), (3.5, 0.75)] {
                cr.new_sub_path();
                cr.arc(8.0, 13.0, r, -PI / 2.0 - a, -PI / 2.0 + a);
            }
            cr.stroke().ok();
            cr.arc(8.0, 12.6, 1.0, 0.0, 2.0 * PI);
            cr.fill().ok();
        }
        "eth" => {
            rrect(cr, 5.5, 2.0, 5.0, 4.0, 0.8);
            rrect(cr, 1.5, 10.0, 5.0, 4.0, 0.8);
            rrect(cr, 9.5, 10.0, 5.0, 4.0, 0.8);
            cr.move_to(8.0, 6.0);
            cr.line_to(8.0, 8.0);
            cr.move_to(4.0, 10.0);
            cr.line_to(4.0, 8.0);
            cr.line_to(12.0, 8.0);
            cr.line_to(12.0, 10.0);
            cr.stroke().ok();
        }
        "warn" => {
            cr.move_to(8.0, 2.2);
            cr.line_to(14.3, 13.3);
            cr.line_to(1.7, 13.3);
            cr.close_path();
            cr.stroke().ok();
            cr.move_to(8.0, 6.3);
            cr.line_to(8.0, 9.2);
            cr.stroke().ok();
            cr.arc(8.0, 11.3, 0.8, 0.0, 2.0 * PI);
            cr.fill().ok();
        }
        "close" => {
            cr.move_to(4.0, 4.0);
            cr.line_to(12.0, 12.0);
            cr.move_to(12.0, 4.0);
            cr.line_to(4.0, 12.0);
            cr.stroke().ok();
        }
        "chev" => {
            cr.move_to(6.0, 3.5);
            cr.line_to(10.5, 8.0);
            cr.line_to(6.0, 12.5);
            cr.stroke().ok();
        }
        "back" => {
            cr.move_to(10.0, 3.5);
            cr.line_to(5.5, 8.0);
            cr.line_to(10.0, 12.5);
            cr.stroke().ok();
        }
        "moon" => {
            // crescent: outer circle minus an offset circle
            let (c1x, c1y, r1) = (8.0f64, 8.0f64, 5.8f64);
            let (c2x, c2y, r2) = (11.0f64, 5.0f64, 4.6f64);
            let dx = c2x - c1x;
            let dy = c2y - c1y;
            let d = (dx * dx + dy * dy).sqrt();
            let a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
            let h = (r1 * r1 - a * a).max(0.0).sqrt();
            let (mx, my) = (c1x + a * dx / d, c1y + a * dy / d);
            let p1 = (mx + h * dy / d, my - h * dx / d);
            let p2 = (mx - h * dy / d, my + h * dx / d);
            let ang = |p: (f64, f64), cx: f64, cy: f64| (p.1 - cy).atan2(p.0 - cx);
            cr.new_sub_path();
            cr.arc(c1x, c1y, r1, ang(p2, c1x, c1y), ang(p1, c1x, c1y) + 2.0 * PI);
            cr.arc_negative(c2x, c2y, r2, ang(p1, c2x, c2y), ang(p2, c2x, c2y));
            cr.close_path();
            cr.stroke().ok();
        }
        "sun" => {
            cr.arc(8.0, 8.0, 2.8, 0.0, 2.0 * PI);
            cr.stroke().ok();
            for i in 0..8 {
                let a = i as f64 * PI / 4.0;
                cr.move_to(8.0 + a.cos() * 5.0, 8.0 + a.sin() * 5.0);
                cr.line_to(8.0 + a.cos() * 6.6, 8.0 + a.sin() * 6.6);
            }
            cr.stroke().ok();
        }
        "gear" => {
            for (y, x) in [(4.5, 10.5), (8.0, 5.0), (11.5, 9.0)] {
                cr.move_to(2.5, y);
                cr.line_to(13.5, y);
                cr.stroke().ok();
                cr.arc(x, y, 1.6, 0.0, 2.0 * PI);
                cr.fill().ok();
            }
        }
        "ext" => {
            cr.move_to(7.0, 3.5);
            cr.line_to(3.5, 3.5);
            cr.line_to(3.5, 12.5);
            cr.line_to(12.5, 12.5);
            cr.line_to(12.5, 9.0);
            cr.move_to(9.0, 3.0);
            cr.line_to(13.0, 3.0);
            cr.line_to(13.0, 7.0);
            cr.move_to(13.0, 3.0);
            cr.line_to(7.5, 8.5);
            cr.stroke().ok();
        }
        "lock" => {
            rrect(cr, 3.5, 7.0, 9.0, 6.5, 1.5);
            cr.stroke().ok();
            cr.arc(8.0, 7.0, 2.8, PI, 0.0);
            cr.stroke().ok();
        }
        _ => {}
    }
}

/// An icon that takes its color from CSS. `mirror` flips it in RTL.
pub fn icon(name: &'static str, size: i32, mirror: bool) -> gtk::DrawingArea {
    let da = gtk::DrawingArea::builder().content_width(size).content_height(size).valign(gtk::Align::Center).halign(gtk::Align::Center).build();
    da.set_can_target(false);
    da.set_draw_func(move |w, cr, wd, ht| {
        let s = wd.min(ht) as f64 / 16.0;
        if mirror && w.direction() == gtk::TextDirection::Rtl {
            cr.translate(wd as f64, 0.0);
            cr.scale(-1.0, 1.0);
        }
        cr.scale(s, s);
        widget_color(w).set(cr);
        icon_path(cr, name);
    });
    da
}

/// The dual-ring Pulse emblem.
pub fn logo(size: i32) -> gtk::DrawingArea {
    let da = gtk::DrawingArea::builder().content_width(size).content_height(size).build();
    da.set_draw_func(|_, cr, w, h| {
        let s = w.min(h) as f64;
        let r = s * 0.26;
        cr.set_line_width(s * 0.1);
        Rgba::hex(0x10B981).set(cr);
        cr.arc(s * 0.38, s / 2.0, r, 0.0, 2.0 * PI);
        cr.stroke().ok();
        Rgba::hex(0x0EA5E9).set(cr);
        cr.arc(s * 0.62, s / 2.0, r, 0.0, 2.0 * PI);
        cr.stroke().ok();
    });
    da
}

/// A horizontal bar. Fill direction follows the text direction.
#[derive(Clone)]
pub struct Bar {
    pub area: gtk::DrawingArea,
    frac: Rc<Cell<f64>>,
    colors: Rc<RefCell<(Rgba, Option<Rgba>)>>,
}

impl Bar {
    pub fn new(track: Rgba, color: Rgba, height: i32) -> Bar {
        let frac = Rc::new(Cell::new(0.0));
        let colors = Rc::new(RefCell::new((color, None::<Rgba>)));
        let area = gtk::DrawingArea::builder().content_height(height).hexpand(true).valign(gtk::Align::Center).build();
        let (f2, c2) = (frac.clone(), colors.clone());
        area.set_draw_func(move |w, cr, wd, ht| {
            let (wd, ht) = (wd as f64, ht as f64);
            track.set(cr);
            rrect(cr, 0.0, 0.0, wd, ht, ht / 2.0);
            cr.fill().ok();
            let fw = (f64::clamp(f2.get(), 0.0, 1.0) * wd).max(if f2.get() > 0.0 { ht } else { 0.0 });
            if fw <= 0.0 {
                return;
            }
            let rtl = w.direction() == gtk::TextDirection::Rtl;
            let x = if rtl { wd - fw } else { 0.0 };
            let (a, b) = *c2.borrow();
            match b {
                Some(b) => {
                    let g = if rtl { LinearGradient::new(wd, 0.0, x, 0.0) } else { LinearGradient::new(0.0, 0.0, fw, 0.0) };
                    g.add_color_stop_rgba(0.0, a.0, a.1, a.2, a.3);
                    g.add_color_stop_rgba(1.0, b.0, b.1, b.2, b.3);
                    cr.set_source(&g).ok();
                }
                None => a.set(cr),
            }
            rrect(cr, x, 0.0, fw, ht, ht / 2.0);
            cr.fill().ok();
        });
        Bar { area, frac, colors }
    }

    pub fn set(&self, f: f64) {
        if (self.frac.get() - f).abs() > 1e-4 {
            self.frac.set(f);
            self.area.queue_draw();
        }
    }

    pub fn set_colors(&self, a: Rgba, b: Option<Rgba>) {
        if *self.colors.borrow() != (a, b) {
            *self.colors.borrow_mut() = (a, b);
            self.area.queue_draw();
        }
    }
}

/// Catmull-Rom spline through points, as cubic Beziers.
pub fn smooth(cr: &Context, p: &[(f64, f64)]) {
    if p.is_empty() {
        return;
    }
    cr.move_to(p[0].0, p[0].1);
    for i in 0..p.len().saturating_sub(1) {
        let p0 = if i > 0 { p[i - 1] } else { p[i] };
        let (p1, p2) = (p[i], p[i + 1]);
        let p3 = if i + 2 < p.len() { p[i + 2] } else { p2 };
        cr.curve_to(
            p1.0 + (p2.0 - p0.0) / 6.0,
            p1.1 + (p2.1 - p0.1) / 6.0,
            p2.0 - (p3.0 - p1.0) / 6.0,
            p2.1 - (p3.1 - p1.1) / 6.0,
            p2.0,
            p2.1,
        );
    }
}

fn fill_under(cr: &Context, color: Rgba, top: f64, bottom: f64, alpha_top: f64) {
    let g = LinearGradient::new(0.0, top, 0.0, bottom);
    g.add_color_stop_rgba(0.0, color.0, color.1, color.2, alpha_top);
    g.add_color_stop_rgba(1.0, color.0, color.1, color.2, 0.0);
    cr.set_source(&g).ok();
}

/// One-minute sparkline. Points are right-aligned while the buffer fills.
#[derive(Clone)]
pub struct Spark {
    pub area: gtk::DrawingArea,
    data: Rc<RefCell<Vec<f64>>>,
}

impl Spark {
    pub fn new(color: Rgba, capacity: usize) -> Spark {
        let data: Rc<RefCell<Vec<f64>>> = Rc::new(RefCell::new(Vec::new()));
        let area = gtk::DrawingArea::builder().content_height(26).hexpand(true).build();
        let d2 = data.clone();
        area.set_draw_func(move |_, cr, wd, ht| {
            let v = d2.borrow();
            if v.len() < 2 {
                return;
            }
            let (w, h) = (wd as f64, ht as f64);
            let mn = v.iter().cloned().fold(f64::MAX, f64::min);
            let mx = v.iter().cloned().fold(f64::MIN, f64::max);
            let rg = (mx - mn).max(0.5);
            let (lo, hi) = (mn - rg * 0.25, mx + rg * 0.35);
            let off = capacity.saturating_sub(v.len());
            let pts: Vec<(f64, f64)> = v
                .iter()
                .enumerate()
                .map(|(i, x)| ((off + i) as f64 / (capacity - 1) as f64 * w, h - 2.0 - (x - lo) / (hi - lo) * (h - 4.0)))
                .collect();
            smooth(cr, &pts);
            cr.line_to(w, h);
            cr.line_to(pts[0].0, h);
            cr.close_path();
            fill_under(cr, color, 0.0, h, 0.28);
            cr.fill().ok();
            smooth(cr, &pts);
            color.set(cr);
            cr.set_line_width(1.5);
            cr.set_line_join(LineJoin::Round);
            cr.stroke().ok();
        });
        Spark { area, data }
    }

    pub fn set(&self, v: &[f64]) {
        let mut d = self.data.borrow_mut();
        if d.as_slice() != v {
            d.clear();
            d.extend_from_slice(v);
            drop(d);
            self.area.queue_draw();
        }
    }
}

pub type ChartLabel = Rc<dyn Fn(usize, &[f64], &[f64]) -> String>;

/// The 10-minute chart: one series with a fill, or two mirrored around
/// the midline. Hovering scrubs a crosshair with a value pill.
#[derive(Clone)]
pub struct Chart {
    pub area: gtk::DrawingArea,
    a: Rc<RefCell<Vec<f64>>>,
    b: Rc<RefCell<Vec<f64>>>,
}

impl Chart {
    pub fn new(ca: Rgba, cb: Rgba, split: bool, zero: bool, capacity: usize, guide: Rgba, pill_bg: Rgba, pill_ink: Rgba, label: ChartLabel) -> Chart {
        let a: Rc<RefCell<Vec<f64>>> = Rc::new(RefCell::new(Vec::new()));
        let b: Rc<RefCell<Vec<f64>>> = Rc::new(RefCell::new(Vec::new()));
        let hover: Rc<Cell<Option<f64>>> = Rc::new(Cell::new(None));
        let area = gtk::DrawingArea::builder().content_height(140).hexpand(true).build();
        // The time axis runs left to right in every language, like the
        // labels under it.
        area.set_direction(gtk::TextDirection::Ltr);
        let (a2, b2, h2) = (a.clone(), b.clone(), hover.clone());
        area.set_draw_func(move |wdg, cr, wd, ht| {
            let (w, h) = (wd as f64, ht as f64);
            let va = a2.borrow();
            let vb = b2.borrow();
            // guides
            guide.set(cr);
            cr.set_line_width(1.0);
            if split {
                cr.move_to(0.0, h / 2.0);
                cr.line_to(w, h / 2.0);
                cr.stroke().ok();
            } else {
                cr.set_dash(&[2.0, 4.0], 0.0);
                for f in [0.25, 0.5, 0.75] {
                    cr.move_to(0.0, (h * f).round() + 0.5);
                    cr.line_to(w, (h * f).round() + 0.5);
                }
                cr.stroke().ok();
                cr.set_dash(&[], 0.0);
            }
            if va.len() < 2 {
                return;
            }
            let off = capacity.saturating_sub(va.len());
            let x = |i: usize| (off + i) as f64 / (capacity - 1) as f64 * w;
            let (pa, pb): (Vec<(f64, f64)>, Vec<(f64, f64)>) = if !split {
                let mx = va.iter().cloned().fold(f64::MIN, f64::max);
                let mn = va.iter().cloned().fold(f64::MAX, f64::min);
                let lo = if zero { 0.0 } else { (mn - (mx - mn) * 0.8).max(0.0) };
                let hi = (mx + (mx - lo) * 0.2).max(lo + 1e-6);
                (va.iter().enumerate().map(|(i, v)| (x(i), h - 6.0 - (v - lo) / (hi - lo) * (h - 34.0))).collect(), Vec::new())
            } else {
                let c = h / 2.0;
                let ma = (va.iter().cloned().fold(0.0, f64::max) * 1.12).max(1e-6);
                let mb = (vb.iter().cloned().fold(0.0, f64::max) * 1.12).max(1e-6);
                (
                    va.iter().enumerate().map(|(i, v)| (x(i), c - 1.0 - v / ma * (c - 22.0))).collect(),
                    vb.iter().enumerate().map(|(i, v)| (x(i), c + 1.0 + v / mb * (c - 22.0))).collect(),
                )
            };
            let base_a = if split { h / 2.0 } else { h };
            smooth(cr, &pa);
            cr.line_to(pa.last().unwrap().0, base_a);
            cr.line_to(pa[0].0, base_a);
            cr.close_path();
            fill_under(cr, ca, 0.0, base_a, 0.30);
            cr.fill().ok();
            smooth(cr, &pa);
            ca.set(cr);
            cr.set_line_width(1.8);
            cr.stroke().ok();
            if split && pb.len() >= 2 {
                smooth(cr, &pb);
                cr.line_to(pb.last().unwrap().0, h / 2.0);
                cr.line_to(pb[0].0, h / 2.0);
                cr.close_path();
                let g = LinearGradient::new(0.0, h, 0.0, h / 2.0);
                g.add_color_stop_rgba(0.0, cb.0, cb.1, cb.2, 0.30);
                g.add_color_stop_rgba(1.0, cb.0, cb.1, cb.2, 0.0);
                cr.set_source(&g).ok();
                cr.fill().ok();
                smooth(cr, &pb);
                cb.set(cr);
                cr.set_line_width(1.8);
                cr.stroke().ok();
            }
            // crosshair
            if let Some(fx) = h2.get() {
                let gi = ((fx * (capacity - 1) as f64).round() as usize).clamp(off, capacity - 1);
                let i = gi - off;
                let px = x(i);
                guide.a(2.5).set(cr);
                cr.set_dash(&[3.0, 3.0], 0.0);
                cr.move_to(px.round() + 0.5, 0.0);
                cr.line_to(px.round() + 0.5, h);
                cr.stroke().ok();
                cr.set_dash(&[], 0.0);
                for (pts, col) in [(&pa, ca), (&pb, cb)] {
                    if let Some(p) = pts.get(i) {
                        pill_bg.set(cr);
                        cr.arc(p.0, p.1, 4.5, 0.0, 2.0 * PI);
                        cr.fill().ok();
                        col.set(cr);
                        cr.arc(p.0, p.1, 3.0, 0.0, 2.0 * PI);
                        cr.fill().ok();
                    }
                }
                let text = label(capacity - 1 - gi, &va[..=i.min(va.len() - 1)], if vb.is_empty() { &[] } else { &vb[..=i.min(vb.len() - 1)] });
                let layout = wdg.create_pango_layout(Some(&text));
                let mut fd = gtk::pango::FontDescription::new();
                fd.set_size(10 * gtk::pango::SCALE);
                fd.set_weight(gtk::pango::Weight::Bold);
                layout.set_font_description(Some(&fd));
                let (tw, th) = layout.pixel_size();
                let (pw, ph) = (tw as f64 + 16.0, th as f64 + 6.0);
                // The pill follows the pointer across [16%, 84%] and shifts
                // by the same share of its own width, so it stays inside.
                let lim = (px / w * 100.0).clamp(16.0, 84.0) / 100.0;
                let left = (lim * w - lim * pw).clamp(0.0, (w - pw).max(0.0));
                pill_bg.set(cr);
                rrect(cr, left, 2.0, pw, ph, ph / 2.0);
                cr.fill().ok();
                guide.set(cr);
                rrect(cr, left + 0.5, 2.5, pw - 1.0, ph - 1.0, ph / 2.0);
                cr.set_line_width(1.0);
                cr.stroke().ok();
                pill_ink.set(cr);
                cr.move_to(left + 8.0, 5.0);
                pangocairo::functions::show_layout(cr, &layout);
            }
        });
        let motion = gtk::EventControllerMotion::new();
        let (h3, ar) = (hover.clone(), area.clone());
        motion.connect_motion(move |_, x, _| {
            let w = ar.width().max(1) as f64;
            h3.set(Some((x / w).clamp(0.0, 1.0)));
            ar.queue_draw();
        });
        let (h4, ar2) = (hover.clone(), area.clone());
        motion.connect_leave(move |_| {
            h4.set(None);
            ar2.queue_draw();
        });
        area.add_controller(motion);
        Chart { area, a, b }
    }

    pub fn set(&self, a: &[f64], b: &[f64]) {
        *self.a.borrow_mut() = a.to_vec();
        *self.b.borrow_mut() = b.to_vec();
        self.area.queue_draw();
    }
}

/// Memory bar: three used segments over a track.
#[derive(Clone)]
pub struct SegBar {
    pub area: gtk::DrawingArea,
    fr: Rc<Cell<(f64, f64, f64)>>,
}

impl SegBar {
    pub fn new(track: Rgba, c1: Rgba, c2: Rgba, c3: Rgba) -> SegBar {
        let fr = Rc::new(Cell::new((0.0, 0.0, 0.0)));
        let area = gtk::DrawingArea::builder().content_height(7).hexpand(true).build();
        let f2 = fr.clone();
        area.set_draw_func(move |w, cr, wd, ht| {
            let (wd, ht) = (wd as f64, ht as f64);
            track.set(cr);
            rrect(cr, 0.0, 0.0, wd, ht, 2.5);
            cr.fill().ok();
            let rtl = w.direction() == gtk::TextDirection::Rtl;
            let (a, b, c) = f2.get();
            let mut x = 0.0;
            cr.save().ok();
            rrect(cr, 0.0, 0.0, wd, ht, 2.5);
            cr.clip();
            for (i, (f, col)) in [(a, c1), (b, c2), (c, c3)].into_iter().enumerate() {
                let sw = f64::clamp(f, 0.0, 1.0) * wd;
                if sw <= 0.0 {
                    continue;
                }
                let sx = if rtl { wd - x - sw } else { x };
                col.set(cr);
                cr.rectangle(sx, 0.0, (sw - 1.0).max(0.5), ht);
                cr.fill().ok();
                if i == 2 {
                    // hatching marks the compressed segment
                    Rgba(1.0, 1.0, 1.0, 0.55).set(cr);
                    cr.set_line_width(1.0);
                    let mut hx = sx - ht;
                    while hx < sx + sw {
                        cr.move_to(hx, ht);
                        cr.line_to(hx + ht, 0.0);
                        hx += 3.0;
                    }
                    cr.save().ok();
                    cr.rectangle(sx, 0.0, sw, ht);
                    cr.clip();
                    cr.stroke().ok();
                    cr.restore().ok();
                }
                x += sw;
            }
            cr.restore().ok();
        });
        SegBar { area, fr }
    }

    pub fn set(&self, a: f64, b: f64, c: f64) {
        if self.fr.get() != (a, b, c) {
            self.fr.set((a, b, c));
            self.area.queue_draw();
        }
    }
}

/// Four-stage thermal meter segments.
#[derive(Clone)]
pub struct Meter {
    pub area: gtk::DrawingArea,
    level: Rc<Cell<Option<usize>>>,
}

impl Meter {
    pub fn new(track: Rgba, colors: [Rgba; 4]) -> Meter {
        let level = Rc::new(Cell::new(None));
        let area = gtk::DrawingArea::builder().content_height(5).hexpand(true).build();
        let l2 = level.clone();
        area.set_draw_func(move |w, cr, wd, ht| {
            let (wd, ht) = (wd as f64, ht as f64);
            let gap = 4.0;
            let sw = (wd - gap * 3.0) / 4.0;
            let rtl = w.direction() == gtk::TextDirection::Rtl;
            for i in 0..4 {
                let x = i as f64 * (sw + gap);
                let x = if rtl { wd - x - sw } else { x };
                if l2.get() == Some(i) { colors[i].set(cr) } else { track.set(cr) }
                rrect(cr, x, 0.0, sw, ht, ht / 2.0);
                cr.fill().ok();
            }
        });
        Meter { area, level }
    }

    pub fn set(&self, l: Option<usize>) {
        if self.level.get() != l {
            self.level.set(l);
            self.area.queue_draw();
        }
    }
}

/// ARGB32 pixmap for the top bar icon: a status dot and a 6-bar CPU wave.
/// Returned in network byte order as StatusNotifierItem expects.
pub fn tray_pixmap(size: i32, wave: &[f64], hog: bool) -> Vec<u8> {
    let mut surf = gtk::cairo::ImageSurface::create(gtk::cairo::Format::ARgb32, size, size).unwrap();
    {
        let cr = Context::new(&surf).unwrap();
        let s = size as f64 / 22.0;
        cr.scale(s, s);
        let dot = if hog { Rgba::hex(0xFB923C) } else { Rgba::hex(0x34D399) };
        dot.set(&cr);
        cr.arc(3.5, 11.0, 2.6, 0.0, 2.0 * PI);
        cr.fill().ok();
        Rgba::hex(0xF4F6F5).set(&cr);
        for (i, v) in wave.iter().rev().take(6).rev().enumerate() {
            let h = (3.0 + v / 70.0 * 11.0).clamp(3.0, 14.0);
            rrect(&cr, 8.0 + i as f64 * 2.4, 18.0 - h, 1.5, h, 0.7);
            cr.fill().ok();
        }
    }
    surf.flush();
    let stride = surf.stride() as usize;
    let data = surf.data().unwrap();
    let mut out = Vec::with_capacity((size * size * 4) as usize);
    for y in 0..size as usize {
        for x in 0..size as usize {
            let i = y * stride + x * 4;
            // cairo is native-endian BGRA on little endian
            let (b, g, r, a) = (data[i], data[i + 1], data[i + 2], data[i + 3]);
            out.extend_from_slice(&[a, r, g, b]);
        }
    }
    out
}
