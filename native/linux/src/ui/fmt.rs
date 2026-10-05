//! Number and unit formatting through the active locale. Units and signs
//! stay as written; digits follow the locale's numbering system.

use pulse::i18n::Locale;

pub const DASH: &str = "—";

#[derive(Clone)]
pub struct Fmt {
    pub lc: Locale,
    pub unit_f: bool,
}

impl Fmt {
    pub fn n(&self, x: f64, d: usize) -> String {
        self.lc.num(x, d)
    }
    pub fn pct(&self, x: f64) -> String {
        format!("{}%", self.n(x.round(), 0))
    }
    pub fn pct1(&self, x: f64) -> String {
        format!("{}%", self.n(x, 1))
    }
    pub fn opct(&self, x: Option<f64>) -> String {
        x.map(|v| self.pct(v)).unwrap_or_else(|| DASH.into())
    }
    pub fn w(&self, x: f64) -> String {
        format!("{} W", self.n(x, 1))
    }
    pub fn t(&self, c: f64, d: usize) -> String {
        if self.unit_f {
            format!("{}°F", self.n(c * 9.0 / 5.0 + 32.0, d))
        } else {
            format!("{}°C", self.n(c, d))
        }
    }
    pub fn ot(&self, c: Option<f64>, d: usize) -> String {
        c.map(|v| self.t(v, d)).unwrap_or_else(|| DASH.into())
    }
    /// Bytes as GB with one decimal, or MB under 1 GB.
    pub fn size(&self, b: f64) -> String {
        let gb = b / 1024f64.powi(3);
        if gb >= 1.0 {
            format!("{} GB", self.n(gb, if gb >= 100.0 { 0 } else { 1 }))
        } else {
            let mb = b / 1024f64.powi(2);
            format!("{} MB", self.n(mb, if mb < 10.0 { 1 } else { 0 }))
        }
    }
    /// Amount in GB with `d` decimals, no unit.
    pub fn gb(&self, b: f64, d: usize) -> String {
        self.n(b / 1024f64.powi(3), d)
    }
    /// Decimals that show a total such as 16 GB without a stray ".0".
    pub fn gb_total(&self, b: f64) -> String {
        let g = b / 1024f64.powi(3);
        self.n(g, if (g - g.round()).abs() < 0.05 { 0 } else { 1 })
    }
    /// Throughput: MB/s from 1 MB/s, else KB/s.
    pub fn rate(&self, bps: Option<f64>) -> String {
        match bps {
            None => DASH.into(),
            Some(b) if b >= 1024.0 * 1024.0 => format!("{} MB/s", self.n(b / 1048576.0, 1)),
            Some(b) => {
                let k = b / 1024.0;
                format!("{} KB/s", self.n(k, if k < 10.0 && k > 0.0 { 1 } else { 0 }))
            }
        }
    }
    pub fn dur(&self, minutes: f64) -> String {
        let m = minutes.max(0.0).round() as i64;
        let (h, mm) = (m / 60, m % 60);
        if h > 0 {
            self.lc.tp("durHM", &[("h", self.n(h as f64, 0)), ("m", self.n(mm as f64, 0))])
        } else {
            self.lc.tp("durM", &[("m", self.n(mm as f64, 0))])
        }
    }
    pub fn mhz(&self, m: f64) -> String {
        if m >= 1000.0 {
            let g = m / 1000.0;
            let d = if ((g * 100.0).round() / 10.0).fract() == 0.0 { 1 } else { 2 };
            format!("{} GHz", self.n(g, d))
        } else {
            format!("{} MHz", self.n(m, 0))
        }
    }
}
