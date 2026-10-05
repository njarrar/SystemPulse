//! Battery from /sys/class/power_supply, package power from RAPL
//! (/sys/class/powercap) when there is no battery.

use super::util::{list, p, rate, read_f64, read_trim, read_u64};
use std::path::{Path, PathBuf};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BattState {
    Charging,
    Discharging,
    Full,
    Unknown,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Battery {
    pub capacity: f64,
    /// Watts flowing in (charging) or out (discharging).
    pub watts: Option<f64>,
    pub state: BattState,
    pub minutes_left: Option<f64>,
    pub minutes_to_full: Option<f64>,
    pub health_pct: Option<f64>,
    pub cycles: Option<u64>,
    pub energy_now_wh: Option<f64>,
    pub energy_full_wh: Option<f64>,
    pub energy_design_wh: Option<f64>,
    pub temp_c: Option<f64>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct PowerSnapshot {
    pub battery: Option<Battery>,
    pub ac_online: Option<bool>,
    /// Package power from RAPL, watts.
    pub rapl_watts: Option<f64>,
}

impl PowerSnapshot {
    /// The best "W" figure for the top bar and status pill.
    pub fn watts(&self) -> Option<f64> {
        self.battery.as_ref().and_then(|b| b.watts).or(self.rapl_watts)
    }
}

/// Reads one battery directory (BAT0 and friends).
pub fn read_battery(dir: &Path) -> Option<Battery> {
    let f = |n: &str| read_f64(dir.join(n));
    let capacity = f("capacity").or_else(|| {
        let now = f("energy_now").or_else(|| f("charge_now"))?;
        let full = f("energy_full").or_else(|| f("charge_full"))?;
        (full > 0.0).then(|| now / full * 100.0)
    })?;
    let volts = f("voltage_now").map(|v| v / 1e6);
    // energy_* in µWh, charge_* in µAh (needs voltage).
    let wh = |e: &str, c: &str| {
        f(e).map(|v| v / 1e6).or_else(|| Some(f(c)? / 1e6 * volts?))
    };
    let energy_now = wh("energy_now", "charge_now");
    let energy_full = wh("energy_full", "charge_full");
    let energy_design = wh("energy_full_design", "charge_full_design");
    let watts = f("power_now")
        .map(|v| v / 1e6)
        .or_else(|| Some(f("current_now")? / 1e6 * volts?))
        .map(f64::abs)
        .filter(|w| *w > 0.0);
    let state = match read_trim(dir.join("status")).as_deref() {
        Some("Charging") => BattState::Charging,
        Some("Discharging") => BattState::Discharging,
        Some("Full") => BattState::Full,
        _ => BattState::Unknown,
    };
    let minutes_left = match (state, energy_now, watts) {
        (BattState::Discharging, Some(e), Some(w)) => Some(e / w * 60.0),
        _ => f("time_to_empty_now").map(|s| s / 60.0),
    };
    let minutes_to_full = match (state, energy_now, energy_full, watts) {
        (BattState::Charging, Some(e), Some(full), Some(w)) => Some((full - e).max(0.0) / w * 60.0),
        _ => f("time_to_full_now").map(|s| s / 60.0),
    };
    let health_pct = match (energy_full, energy_design) {
        (Some(a), Some(b)) if b > 0.0 => Some((a / b * 100.0).min(100.0)),
        _ => None,
    };
    let cycles = read_u64(dir.join("cycle_count")).filter(|c| *c > 0);
    let temp_c = f("temp").map(|t| t / 10.0);
    Some(Battery {
        capacity: capacity.clamp(0.0, 100.0),
        watts,
        state,
        minutes_left,
        minutes_to_full,
        health_pct,
        cycles,
        energy_now_wh: energy_now,
        energy_full_wh: energy_full,
        energy_design_wh: energy_design,
        temp_c,
    })
}

pub struct PowerSampler {
    root: PathBuf,
    rapl_prev: Option<u64>,
    rapl_max: Option<u64>,
}

impl PowerSampler {
    pub fn new(root: &Path) -> Self {
        let rapl_max = read_u64(p(root, "/sys/class/powercap/intel-rapl:0/max_energy_range_uj"));
        PowerSampler { root: root.to_path_buf(), rapl_prev: None, rapl_max }
    }

    pub fn sample(&mut self, dt: f64) -> PowerSnapshot {
        let mut out = PowerSnapshot::default();
        for d in list(p(&self.root, "/sys/class/power_supply"), "") {
            let ty = read_trim(d.join("type")).unwrap_or_default();
            if ty == "Battery" && read_trim(d.join("scope")).as_deref() != Some("Device") {
                if out.battery.is_none() {
                    out.battery = read_battery(&d);
                }
            } else if ty == "Mains" || ty == "USB" || ty == "USB_C" {
                if let Some(on) = read_u64(d.join("online")) {
                    out.ac_online = Some(out.ac_online.unwrap_or(false) || on == 1);
                }
            }
        }
        // RAPL energy counter in µJ; root-only on many distributions.
        if let Some(e) = read_u64(p(&self.root, "/sys/class/powercap/intel-rapl:0/energy_uj")) {
            let prev = self.rapl_prev;
            self.rapl_prev = Some(e);
            out.rapl_watts = match (prev, self.rapl_max) {
                (Some(pv), Some(max)) if e < pv && dt > 0.0 => Some((max - pv + e) as f64 / 1e6 / dt),
                _ => rate(prev, e, dt).map(|uj| uj / 1e6),
            };
        }
        out
    }
}
