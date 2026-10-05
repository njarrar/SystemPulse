//! Memory from /proc/meminfo, zswap from /sys/module/zswap, pressure from
//! /proc/pressure/memory.

use super::util::{kv_kb, p, read_trim};
use std::path::Path;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct MemSnapshot {
    pub total: u64,
    pub used: u64,
    pub app: u64,
    pub buffers: u64,
    pub compressed: u64,
    /// "zswap" or "ZRAM": what holds the compressed segment.
    pub compressed_kind: &'static str,
    pub free: u64,
    pub swap_total: u64,
    pub swap_used: u64,
    pub psi_some_avg10: Option<f64>,
}

impl MemSnapshot {
    pub fn used_pct(&self) -> f64 {
        if self.total == 0 { 0.0 } else { self.used as f64 / self.total as f64 * 100.0 }
    }
}

/// Bytes. Segments add up to `used`: App + Buffers + compressed pool.
pub fn parse(meminfo: &str, zram_compr: Option<u64>) -> Option<MemSnapshot> {
    let k = |key: &str| kv_kb(meminfo, key).map(|v| v * 1024);
    let total = k("MemTotal")?;
    let avail = k("MemAvailable").unwrap_or_else(|| {
        k("MemFree").unwrap_or(0) + k("Buffers").unwrap_or(0) + k("Cached").unwrap_or(0)
    });
    let used = total.saturating_sub(avail);
    let buffers = k("Buffers").unwrap_or(0).min(used);
    let (comp, kind) = match (k("Zswap"), zram_compr) {
        (Some(z), _) if z > 0 => (z, "zswap"),
        (_, Some(z)) => (z, "ZRAM"),
        (Some(z), None) => (z, "zswap"),
        (None, None) => (0, "zswap"),
    };
    let comp = comp.min(used - buffers);
    let swap_total = k("SwapTotal").unwrap_or(0);
    let swap_used = swap_total.saturating_sub(k("SwapFree").unwrap_or(0));
    Some(MemSnapshot {
        total,
        used,
        app: used - buffers - comp,
        buffers,
        compressed: comp,
        compressed_kind: kind,
        free: total - used,
        swap_total,
        swap_used,
        psi_some_avg10: None,
    })
}

pub fn parse_psi(text: &str) -> Option<f64> {
    let line = text.lines().find(|l| l.starts_with("some"))?;
    line.split_whitespace()
        .find_map(|f| f.strip_prefix("avg10="))
        .and_then(|v| v.parse().ok())
}

pub fn read(root: &Path) -> Option<MemSnapshot> {
    let text = super::util::read(p(root, "/proc/meminfo"))?;
    // zswap pool size shows in meminfo on kernels 5.19+; ZRAM keeps its own
    // compressed size in mm_stat (field 2).
    let zswap_on = read_trim(p(root, "/sys/module/zswap/parameters/enabled"))
        .map(|v| v == "Y" || v == "1")
        .unwrap_or(false);
    let zram = if zswap_on {
        None
    } else {
        super::util::read(p(root, "/sys/block/zram0/mm_stat"))
            .and_then(|s| s.split_whitespace().nth(1).and_then(|x| x.parse().ok()))
    };
    let mut m = parse(&text, zram)?;
    m.psi_some_avg10 = super::util::read(p(root, "/proc/pressure/memory")).and_then(|t| parse_psi(&t));
    Some(m)
}
