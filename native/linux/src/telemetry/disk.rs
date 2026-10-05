//! Root filesystem from statvfs("/") and /proc/mounts, throughput from
//! /proc/diskstats.

use super::util::{p, rate, read};
use std::ffi::CString;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct DiskSnapshot {
    pub total: u64,
    pub free: u64,
    pub used: u64,
    pub fstype: String,
    pub device: String,
    pub read_bps: Option<f64>,
    pub write_bps: Option<f64>,
}

impl DiskSnapshot {
    pub fn used_pct(&self) -> f64 {
        if self.total == 0 { 0.0 } else { self.used as f64 / self.total as f64 * 100.0 }
    }
}

/// Filesystem type and device of the last "/" entry in /proc/mounts.
pub fn root_mount(mounts: &str) -> Option<(String, String)> {
    mounts
        .lines()
        .filter_map(|l| {
            let f: Vec<&str> = l.split_whitespace().collect();
            (f.len() >= 3 && f[1] == "/").then(|| (f[2].to_string(), f[0].to_string()))
        })
        .last()
}

/// Sums sectors read and written over whole disks. `whole` says which
/// names are whole disks (from /sys/block); without it, partitions are
/// guessed from the name.
pub fn parse_diskstats(text: &str, whole: Option<&[String]>) -> (u64, u64) {
    let mut r = 0;
    let mut w = 0;
    for l in text.lines() {
        let f: Vec<&str> = l.split_whitespace().collect();
        if f.len() < 10 {
            continue;
        }
        let name = f[2];
        if name.starts_with("loop") || name.starts_with("ram") || name.starts_with("zram") || name.starts_with("dm-") || name.starts_with("md") || name.starts_with("sr") {
            continue;
        }
        let is_whole = match whole {
            Some(list) => list.iter().any(|x| x == name),
            None => {
                let partition = (name.starts_with("nvme") || name.starts_with("mmcblk")) && name.contains('p') && name.rsplit('p').next().map(|x| x.chars().all(|c| c.is_ascii_digit())).unwrap_or(false)
                    || (!name.starts_with("nvme") && !name.starts_with("mmcblk") && name.ends_with(|c: char| c.is_ascii_digit()));
                !partition
            }
        };
        if !is_whole {
            continue;
        }
        r += f[5].parse::<u64>().unwrap_or(0);
        w += f[9].parse::<u64>().unwrap_or(0);
    }
    (r * 512, w * 512)
}

pub struct DiskSampler {
    root: PathBuf,
    prev: Option<(u64, u64)>,
    whole: Option<Vec<String>>,
}

impl DiskSampler {
    pub fn new(root: &Path) -> Self {
        let whole = std::fs::read_dir(p(root, "/sys/block"))
            .ok()
            .map(|rd| rd.flatten().map(|e| e.file_name().to_string_lossy().to_string()).collect());
        DiskSampler { root: root.to_path_buf(), prev: None, whole }
    }

    pub fn sample(&mut self, dt: f64) -> Option<DiskSnapshot> {
        let path = CString::new(self.root.to_string_lossy().as_bytes()).ok()?;
        let mut st: libc::statvfs = unsafe { std::mem::zeroed() };
        if unsafe { libc::statvfs(path.as_ptr(), &mut st) } != 0 {
            return None;
        }
        let bs = st.f_frsize as u64;
        let total = st.f_blocks as u64 * bs;
        let free = st.f_bavail as u64 * bs;
        // used as df shows it: blocks minus free (reserved blocks count as used)
        let used = total.saturating_sub(st.f_bfree as u64 * bs);
        let (fstype, device) = read(p(&self.root, "/proc/mounts"))
            .and_then(|m| root_mount(&m))
            .unwrap_or_default();
        let mut d = DiskSnapshot { total, free, used, fstype, device, ..Default::default() };
        if let Some(text) = read(p(&self.root, "/proc/diskstats")) {
            let (r, w) = parse_diskstats(&text, self.whole.as_deref());
            d.read_bps = rate(self.prev.map(|x| x.0), r, dt);
            d.write_bps = rate(self.prev.map(|x| x.1), w, dt);
            self.prev = Some((r, w));
        }
        Some(d)
    }
}
