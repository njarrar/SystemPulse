//! CPU load from /proc/stat, core classes from sysfs, load from /proc/loadavg.

use super::util::{list, p, read, read_trim, read_u64};
use std::collections::HashSet;
use std::path::{Path, PathBuf};

/// How the cores split into the two bars of the CPU card.
#[derive(Clone, Debug, PartialEq)]
pub enum CoreGroups {
    /// Hybrid parts. `amd` picks the Zen 4 / Zen 4c labels, else P / E.
    Hybrid { amd: bool, a: Vec<usize>, b: Vec<usize> },
    /// Uniform cores split in two halves, labelled by core range.
    Halves { a: Vec<usize>, b: Vec<usize> },
    /// One logical CPU: the bars show User and Kernel.
    Single,
}

impl Default for CoreGroups {
    fn default() -> Self {
        CoreGroups::Single
    }
}

#[derive(Clone, Debug, Default)]
pub struct CpuSnapshot {
    pub valid: bool,
    pub total: f64,
    pub user: f64,
    pub system: f64,
    pub idle: f64,
    pub group_a: f64,
    pub group_b: f64,
    pub groups: CoreGroups,
    pub physical_cores: usize,
    pub threads: usize,
    pub model: Option<String>,
    pub load1: Option<f64>,
    pub per_core: Vec<f64>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Ticks {
    pub user: u64,
    pub system: u64,
    pub idle: u64,
    pub total: u64,
}

/// Parses one "cpuN ..." line of /proc/stat.
pub fn parse_ticks(line: &str) -> Option<(String, Ticks)> {
    let mut it = line.split_whitespace();
    let name = it.next()?.to_string();
    if !name.starts_with("cpu") {
        return None;
    }
    let v: Vec<u64> = it.filter_map(|x| x.parse().ok()).collect();
    if v.len() < 4 {
        return None;
    }
    let g = |i: usize| v.get(i).copied().unwrap_or(0);
    // user nice system idle iowait irq softirq steal guest guest_nice
    // guest time is already counted in user, so it is not added twice.
    let user = g(0) + g(1);
    let system = g(2) + g(5) + g(6) + g(7);
    let idle = g(3) + g(4);
    Some((name, Ticks { user, system, idle, total: user + system + idle }))
}

pub fn parse_stat(text: &str) -> (Option<Ticks>, Vec<(usize, Ticks)>) {
    let mut all = None;
    let mut cores = Vec::new();
    for l in text.lines() {
        if let Some((name, t)) = parse_ticks(l) {
            if name == "cpu" {
                all = Some(t);
            } else if let Ok(n) = name[3..].parse::<usize>() {
                cores.push((n, t));
            }
        }
    }
    (all, cores)
}

fn busy_pct(prev: &Ticks, cur: &Ticks) -> (f64, f64, f64) {
    let dt = cur.total.saturating_sub(prev.total) as f64;
    if dt <= 0.0 {
        return (0.0, 0.0, 0.0);
    }
    let u = cur.user.saturating_sub(prev.user) as f64 / dt * 100.0;
    let s = cur.system.saturating_sub(prev.system) as f64 / dt * 100.0;
    (u + s, u, s)
}

fn parse_cpu_list(s: &str) -> Vec<usize> {
    let mut v = Vec::new();
    for part in s.trim().split(',') {
        if let Some((a, b)) = part.split_once('-') {
            if let (Ok(a), Ok(b)) = (a.trim().parse::<usize>(), b.trim().parse::<usize>()) {
                v.extend(a..=b);
            }
        } else if let Ok(a) = part.trim().parse() {
            v.push(a);
        }
    }
    v
}

/// Finds core classes. Intel hybrid parts list them under
/// /sys/devices/cpu_core and cpu_atom; AMD Zen 4 + Zen 4c (and others)
/// show it through acpi_cppc/highest_perf.
pub fn detect_groups(root: &Path, cpus: &[usize], vendor_amd: bool) -> CoreGroups {
    if cpus.len() <= 1 {
        return CoreGroups::Single;
    }
    let core = read(p(root, "/sys/devices/cpu_core/cpus")).map(|s| parse_cpu_list(&s));
    let atom = read(p(root, "/sys/devices/cpu_atom/cpus")).map(|s| parse_cpu_list(&s));
    if let (Some(a), Some(b)) = (core, atom) {
        if !a.is_empty() && !b.is_empty() {
            return CoreGroups::Hybrid { amd: false, a, b };
        }
    }
    let perf: Vec<(usize, u64)> = cpus
        .iter()
        .filter_map(|&c| {
            read_u64(p(root, &format!("/sys/devices/system/cpu/cpu{c}/acpi_cppc/highest_perf"))).map(|v| (c, v))
        })
        .collect();
    if perf.len() == cpus.len() {
        let max = perf.iter().map(|x| x.1).max().unwrap_or(0);
        let min = perf.iter().map(|x| x.1).min().unwrap_or(0);
        // Preferred-core boost gives a few points of spread on uniform
        // parts; a real second class sits well below (Zen 4c, E-cores).
        if max > 0 && (max - min) as f64 / max as f64 > 0.15 {
            let cut = (max + min) / 2;
            let a: Vec<usize> = perf.iter().filter(|x| x.1 > cut).map(|x| x.0).collect();
            let b: Vec<usize> = perf.iter().filter(|x| x.1 <= cut).map(|x| x.0).collect();
            return CoreGroups::Hybrid { amd: vendor_amd, a, b };
        }
    }
    let half = cpus.len().div_ceil(2);
    CoreGroups::Halves { a: cpus[..half].to_vec(), b: cpus[half..].to_vec() }
}

pub struct CpuSampler {
    root: PathBuf,
    prev_all: Option<Ticks>,
    prev_cores: Vec<(usize, Ticks)>,
    last_delta: u64,
    groups: Option<CoreGroups>,
    physical: usize,
    model: Option<String>,
    vendor_amd: bool,
}

impl CpuSampler {
    pub fn new(root: &Path) -> Self {
        let info = read(p(root, "/proc/cpuinfo")).unwrap_or_default();
        let field = |k: &str| {
            info.lines().find_map(|l| {
                let (a, b) = l.split_once(':')?;
                (a.trim() == k).then(|| b.trim().to_string())
            })
        };
        let model = field("model name").or_else(|| field("Model")).or_else(|| field("Hardware"));
        let vendor_amd = field("vendor_id").map(|v| v == "AuthenticAMD").unwrap_or(false);
        let mut seen = HashSet::new();
        for c in list(p(root, "/sys/devices/system/cpu"), "cpu") {
            let pk = read_trim(c.join("topology/physical_package_id"));
            let co = read_trim(c.join("topology/core_id"));
            if let (Some(pk), Some(co)) = (pk, co) {
                seen.insert((pk, co));
            }
        }
        CpuSampler {
            root: root.to_path_buf(),
            prev_all: None,
            prev_cores: Vec::new(),
            last_delta: 0,
            groups: None,
            physical: seen.len(),
            model,
            vendor_amd,
        }
    }

    pub fn last_total_delta(&self) -> u64 {
        self.last_delta
    }

    pub fn sample(&mut self) -> CpuSnapshot {
        let mut s = CpuSnapshot::default();
        let Some(text) = read(p(&self.root, "/proc/stat")) else {
            return s;
        };
        let (all, cores) = parse_stat(&text);
        let ids: Vec<usize> = cores.iter().map(|c| c.0).collect();
        if self.groups.is_none() {
            self.groups = Some(detect_groups(&self.root, &ids, self.vendor_amd));
        }
        let groups = self.groups.clone().unwrap_or_default();
        s.threads = ids.len().max(1);
        s.physical_cores = if self.physical > 0 { self.physical } else { s.threads };
        s.model = self.model.clone();
        s.load1 = read(p(&self.root, "/proc/loadavg"))
            .and_then(|l| l.split_whitespace().next().and_then(|x| x.parse().ok()));
        if let (Some(prev), Some(cur)) = (self.prev_all, all) {
            let (t, u, sy) = busy_pct(&prev, &cur);
            s.valid = true;
            s.total = t.clamp(0.0, 100.0);
            s.user = u.clamp(0.0, 100.0);
            s.system = sy.clamp(0.0, 100.0);
            s.idle = (100.0 - s.total).max(0.0);
            self.last_delta = cur.total.saturating_sub(prev.total);
            for (id, t) in &cores {
                let pct = self
                    .prev_cores
                    .iter()
                    .find(|x| x.0 == *id)
                    .map(|(_, pt)| busy_pct(pt, t).0)
                    .unwrap_or(0.0);
                s.per_core.push(pct);
            }
            let avg = |set: &[usize]| {
                let v: Vec<f64> = cores
                    .iter()
                    .zip(&s.per_core)
                    .filter(|((id, _), _)| set.contains(id))
                    .map(|(_, p)| *p)
                    .collect();
                if v.is_empty() { 0.0 } else { v.iter().sum::<f64>() / v.len() as f64 }
            };
            match &groups {
                CoreGroups::Hybrid { a, b, .. } | CoreGroups::Halves { a, b } => {
                    s.group_a = avg(a);
                    s.group_b = avg(b);
                }
                CoreGroups::Single => {
                    s.group_a = s.user;
                    s.group_b = s.system;
                }
            }
        }
        s.groups = groups;
        self.prev_all = all;
        self.prev_cores = cores;
        s
    }
}
