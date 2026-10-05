//! App state: what the person chose, plus the rolling history the charts
//! draw and the hog rule ("over 50% of total CPU for over 2 min").

use pulse::i18n::Registry;
use pulse::telemetry::power::{BattState, Battery};
use pulse::telemetry::procs::Launch;
use pulse::telemetry::{AppGroup, Snapshot};
use std::collections::{HashMap, VecDeque};
use std::time::{Duration, Instant};

pub const SPARK_N: usize = 40; // 40 polls x 1.5 s = 1 min
pub const LONG_N: usize = 121; // 121 points x 5 s = 10 min
pub const LONG_STEP: Duration = Duration::from_secs(5);
pub const HOG_PCT: f64 = 50.0;
/// A dip below the line shorter than this does not restart the clock.
pub const HOG_GRACE: Duration = Duration::from_secs(4);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Kind {
    Cpu,
    Mem,
    Nrg,
    Thm,
    Gpu,
    Ssd,
    Net,
}

impl Kind {
    pub fn parse(s: &str) -> Option<Kind> {
        Some(match s {
            "cpu" => Kind::Cpu,
            "mem" => Kind::Mem,
            "nrg" => Kind::Nrg,
            "thm" => Kind::Thm,
            "gpu" => Kind::Gpu,
            "ssd" => Kind::Ssd,
            "net" => Kind::Net,
            _ => return None,
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum View {
    Overview,
    Detail(Kind),
    App(String),
    Settings,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SortKey {
    Cpu,
    Mem,
    Gpu,
}

#[derive(Default)]
pub struct History {
    pub spark: HashMap<&'static str, VecDeque<f64>>,
    pub long: HashMap<String, VecDeque<f64>>,
    last_long: Option<Instant>,
}

fn push(q: &mut VecDeque<f64>, v: f64, cap: usize) {
    if q.len() == cap {
        q.pop_front();
    }
    q.push_back(v);
}

impl History {
    pub fn spark(&self, k: &str) -> Vec<f64> {
        self.spark.get(k).map(|q| q.iter().cloned().collect()).unwrap_or_default()
    }
    pub fn long(&self, k: &str) -> Vec<f64> {
        self.long.get(k).map(|q| q.iter().cloned().collect()).unwrap_or_default()
    }

    pub fn ingest(&mut self, s: &Snapshot, now: Instant) {
        let mut vals: Vec<(&'static str, Option<f64>)> = vec![
            ("cpu", s.cpu.valid.then_some(s.cpu.total)),
            ("mem", s.mem.as_ref().map(|m| m.used_pct())),
            ("nrg", s.power.watts()),
            ("thm", s.thermal.cpu_c),
            ("gpu", s.gpu.as_ref().and_then(|g| g.busy_pct)),
        ];
        for (k, v) in &vals {
            if let Some(v) = v {
                push(self.spark.entry(k).or_default(), *v, SPARK_N);
            }
        }
        let due = self.last_long.map(|t| now.duration_since(t) >= LONG_STEP).unwrap_or(true);
        if !due {
            return;
        }
        self.last_long = Some(now);
        vals.push(("ssd_r", s.disk.as_ref().and_then(|d| d.read_bps)));
        vals.push(("ssd_w", s.disk.as_ref().and_then(|d| d.write_bps)));
        vals.push(("net_d", s.net.as_ref().and_then(|n| n.down_bps)));
        vals.push(("net_u", s.net.as_ref().and_then(|n| n.up_bps)));
        for (k, v) in vals {
            if let Some(v) = v {
                push(self.long.entry(k.to_string()).or_default(), v, LONG_N);
            }
        }
        let mut live = std::collections::HashSet::new();
        for a in &s.apps {
            let k = format!("app:{}", a.key);
            push(self.long.entry(k.clone()).or_default(), a.cpu, LONG_N);
            live.insert(k);
        }
        self.long.retain(|k, _| !k.starts_with("app:") || live.contains(k));
    }
}

pub struct State {
    pub registry: Registry,
    pub lang: String,
    /// What Settings shows: None is "Match system".
    pub lang_pref: Option<String>,
    pub dark: bool,
    pub view: View,
    pub sort: SortKey,
    pub unit_f: bool,
    pub live: bool,
    pub confirm: Option<AppGroup>,
    pub toast: Option<String>,
    pub hog_dismissed: Option<String>,
    pub hog_since: HashMap<String, Instant>,
    pub hog_last_above: HashMap<String, Instant>,
    pub hog_after: Duration,
    pub hog: Option<AppGroup>,
    pub hist: History,
    pub snap: Snapshot,
    /// Last real reading, before the demo switches are applied.
    pub raw: Snapshot,
    pub sim_hog: bool,
    pub sim_charging: bool,
    /// Apps Pulse ended this session, for "Restore ended apps".
    pub ended: Vec<Launch>,
}

impl State {
    pub fn new(registry: Registry, lang: String, hog_after: Duration) -> State {
        State {
            registry,
            lang,
            lang_pref: None,
            dark: false,
            view: View::Overview,
            sort: SortKey::Cpu,
            unit_f: false,
            live: true,
            confirm: None,
            toast: None,
            hog_dismissed: None,
            hog_since: HashMap::new(),
            hog_last_above: HashMap::new(),
            hog_after,
            hog: None,
            hist: History::default(),
            snap: Snapshot::default(),
            raw: Snapshot::default(),
            sim_hog: false,
            sim_charging: false,
            ended: Vec::new(),
        }
    }

    /// Re-applies the demo switches to the last real reading and pushes a
    /// fresh point, so a toggle shows at once even while paused.
    pub fn resync(&mut self, now: Instant) {
        let raw = self.raw.clone();
        self.ingest(raw, now);
    }

    /// The demo switches in Settings, laid over real readings. "Simulate
    /// CPU hog" lifts the busiest real app to at least 53% of total CPU
    /// (and the totals, power and CPU temperature with it) and raises the
    /// alert at once. "Charging" shows the battery charging at 48 W; with
    /// no battery it shows a demo battery.
    fn overlay(&self, mut s: Snapshot) -> (Snapshot, Option<String>) {
        let mut forced = None;
        if self.sim_hog {
            let own = std::process::id() as i32;
            if let Some(a) = s
                .apps
                .iter_mut()
                .filter(|a| !a.pids.contains(&own))
                .max_by(|a, b| a.cpu.partial_cmp(&b.cpu).unwrap_or(std::cmp::Ordering::Equal))
            {
                let extra = (53.0 - a.cpu).max(0.0);
                a.cpu += extra;
                forced = Some(a.key.clone());
                let c = &mut s.cpu;
                if c.valid {
                    c.total = (c.total + extra).min(100.0);
                    c.user = (c.user + extra * 0.66).min(100.0);
                    c.system = (c.system + extra * 0.34).min(100.0 - c.user);
                    c.idle = (100.0 - c.total).max(0.0);
                    c.group_a = (c.group_a + extra * 1.2).min(100.0);
                    c.group_b = (c.group_b + extra * 0.8).min(100.0);
                }
            }
            if let Some(t) = s.thermal.cpu_c.as_mut() {
                *t = t.max(52.0);
            }
            if let Some(b) = s.power.battery.as_mut() {
                b.watts = b.watts.map(|w| w + 5.4);
                if let (Some(m), BattState::Discharging) = (b.minutes_left, b.state) {
                    b.minutes_left = Some(m * 0.64);
                }
            }
            s.power.rapl_watts = s.power.rapl_watts.map(|w| w + 5.4);
        }
        if self.sim_charging {
            let mut b = s.power.battery.clone().unwrap_or(Battery {
                capacity: 84.0,
                watts: None,
                state: BattState::Charging,
                minutes_left: None,
                minutes_to_full: None,
                health_pct: Some(95.0),
                cycles: Some(118),
                energy_now_wh: None,
                energy_full_wh: Some(69.0),
                energy_design_wh: Some(72.6),
                temp_c: None,
            });
            b.state = BattState::Charging;
            b.watts = Some(48.0);
            b.minutes_left = None;
            b.minutes_to_full = match (b.energy_now_wh, b.energy_full_wh) {
                (Some(n), Some(f)) => Some(((f - n).max(0.0) / 48.0 * 60.0).max(1.0)),
                _ => Some(52.0),
            };
            s.power.battery = Some(b);
            s.power.ac_online = Some(true);
        }
        (s, forced)
    }

    pub fn ingest(&mut self, raw: Snapshot, now: Instant) {
        self.raw = raw.clone();
        let (s, forced) = self.overlay(raw);
        self.hist.ingest(&s, now);
        // Hog rule: one app above 50% of total CPU for `hog_after`.
        for a in &s.apps {
            if a.cpu > HOG_PCT {
                self.hog_since.entry(a.key.clone()).or_insert(now);
                self.hog_last_above.insert(a.key.clone(), now);
            }
        }
        let last = self.hog_last_above.clone();
        self.hog_since.retain(|k, _| last.get(k).map(|t| now.duration_since(*t) <= HOG_GRACE).unwrap_or(false));
        self.hog_last_above.retain(|k, _| now.duration_since(last[k]) <= HOG_GRACE);
        self.hog = s
            .apps
            .iter()
            .filter(|a| self.hog_since.get(&a.key).map(|t| now.duration_since(*t) >= self.hog_after).unwrap_or(false))
            .max_by(|a, b| a.cpu.partial_cmp(&b.cpu).unwrap_or(std::cmp::Ordering::Equal))
            .cloned();
        if let Some(k) = forced {
            self.hog = s.apps.iter().find(|a| a.key == k).cloned();
        }
        if self.hog.is_none() {
            self.hog_dismissed = None;
        }
        self.snap = s;
    }

    /// The hog alert to show, unless dismissed.
    pub fn hog_shown(&self) -> Option<&AppGroup> {
        self.hog.as_ref().filter(|h| Some(&h.key) != self.hog_dismissed.as_ref())
    }

    pub fn app(&self, key: &str) -> Option<&AppGroup> {
        self.snap.apps.iter().find(|a| a.key == key)
    }

    pub fn top_apps(&self, n: usize) -> Vec<AppGroup> {
        let mut v = self.snap.apps.clone();
        let k = |a: &AppGroup| match self.sort {
            SortKey::Cpu => a.cpu,
            SortKey::Mem => a.mem_bytes as f64,
            SortKey::Gpu => a.gpu.unwrap_or(0.0) * 1e6 + a.cpu,
        };
        v.sort_by(|a, b| k(b).partial_cmp(&k(a)).unwrap_or(std::cmp::Ordering::Equal).then(a.key.cmp(&b.key)));
        v.truncate(n);
        v
    }
}
