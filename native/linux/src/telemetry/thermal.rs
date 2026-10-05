//! Temperatures and fans from /sys/class/hwmon, with /sys/class/thermal as
//! a fallback for the CPU, and throttle counters from thermal_throttle.

use super::util::{list, p, read_f64, read_trim, read_u64};
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ThermalSnapshot {
    pub cpu_c: Option<f64>,
    pub gpu_c: Option<f64>,
    pub nvme_c: Option<f64>,
    pub battery_c: Option<f64>,
    pub fans_rpm: Vec<u64>,
    /// Package throttle events since Pulse started (Intel only).
    pub throttle_events: Option<u64>,
}

const CPU_CHIPS: &[&str] = &["k10temp", "zenpower", "coretemp", "cpu_thermal", "soc_thermal", "x86_pkg_temp"];
const GPU_CHIPS: &[&str] = &["amdgpu", "radeon", "nouveau", "i915", "xe"];

/// Picks the CPU sensor of a hwmon chip: Tctl/Tdie on AMD, "Package id 0"
/// on Intel, else temp1.
pub fn chip_cpu_temp(dir: &Path) -> Option<f64> {
    let mut first = None;
    for i in 1..=32 {
        let Some(v) = read_f64(dir.join(format!("temp{i}_input"))) else { continue };
        let label = read_trim(dir.join(format!("temp{i}_label"))).unwrap_or_default();
        if label.starts_with("Package") || label == "Tctl" || label == "Tdie" {
            return Some(v / 1000.0);
        }
        first.get_or_insert(v / 1000.0);
    }
    first
}

pub struct ThermalSampler {
    root: PathBuf,
    throttle_base: Option<u64>,
}

impl ThermalSampler {
    pub fn new(root: &Path) -> Self {
        let mut s = ThermalSampler { root: root.to_path_buf(), throttle_base: None };
        s.throttle_base = s.throttle_total();
        s
    }

    fn throttle_total(&self) -> Option<u64> {
        let mut total = None;
        for c in list(p(&self.root, "/sys/devices/system/cpu"), "cpu") {
            if let Some(v) = read_u64(c.join("thermal_throttle/package_throttle_count")) {
                *total.get_or_insert(0) += v;
            }
        }
        total
    }

    pub fn sample(&mut self, gpu_temp: Option<f64>) -> ThermalSnapshot {
        let mut t = ThermalSnapshot::default();
        for d in list(p(&self.root, "/sys/class/hwmon"), "hwmon") {
            let name = read_trim(d.join("name")).unwrap_or_default();
            if CPU_CHIPS.contains(&name.as_str()) && t.cpu_c.is_none() {
                t.cpu_c = chip_cpu_temp(&d);
            } else if GPU_CHIPS.contains(&name.as_str()) && t.gpu_c.is_none() {
                t.gpu_c = read_f64(d.join("temp1_input")).map(|v| v / 1000.0);
            } else if name == "nvme" && t.nvme_c.is_none() {
                t.nvme_c = read_f64(d.join("temp1_input")).map(|v| v / 1000.0);
            } else if name.starts_with("BAT") && t.battery_c.is_none() {
                t.battery_c = read_f64(d.join("temp1_input")).map(|v| v / 1000.0);
            }
            for i in 1..=8 {
                if let Some(rpm) = read_u64(d.join(format!("fan{i}_input"))) {
                    t.fans_rpm.push(rpm);
                }
            }
        }
        if t.cpu_c.is_none() {
            // thermal zones: prefer the package sensor, then ACPI.
            let zones = list(p(&self.root, "/sys/class/thermal"), "thermal_zone");
            for want in ["x86_pkg_temp", "cpu", "soc", "acpitz"] {
                if let Some(z) = zones.iter().find(|z| {
                    read_trim(z.join("type")).map(|ty| ty.starts_with(want)).unwrap_or(false)
                }) {
                    t.cpu_c = read_f64(z.join("temp")).map(|v| v / 1000.0);
                    if t.cpu_c.is_some() {
                        break;
                    }
                }
            }
        }
        if t.gpu_c.is_none() {
            t.gpu_c = gpu_temp;
        }
        if t.battery_c.is_none() {
            for d in list(p(&self.root, "/sys/class/power_supply"), "BAT") {
                if let Some(v) = read_f64(d.join("temp")) {
                    t.battery_c = Some(v / 10.0);
                    break;
                }
            }
        }
        t.throttle_events = match (self.throttle_base, self.throttle_total()) {
            (Some(b), Some(n)) => Some(n.saturating_sub(b)),
            _ => None,
        };
        t
    }
}
