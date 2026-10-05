//! Small helpers for reading sysfs and procfs files.

use std::fs;
use std::path::{Path, PathBuf};

pub fn p(root: &Path, rel: &str) -> PathBuf {
    root.join(rel.trim_start_matches('/'))
}

pub fn read(path: impl AsRef<Path>) -> Option<String> {
    fs::read_to_string(path).ok()
}

pub fn read_trim(path: impl AsRef<Path>) -> Option<String> {
    read(path).map(|s| s.trim().to_string()).filter(|s| !s.is_empty())
}

pub fn read_f64(path: impl AsRef<Path>) -> Option<f64> {
    read_trim(path)?.parse().ok()
}

pub fn read_u64(path: impl AsRef<Path>) -> Option<u64> {
    read_trim(path)?.parse().ok()
}

/// Sorted entries of a directory whose names start with `prefix`.
pub fn list(dir: impl AsRef<Path>, prefix: &str) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = fs::read_dir(dir)
        .map(|rd| {
            rd.flatten()
                .filter(|e| e.file_name().to_string_lossy().starts_with(prefix))
                .map(|e| e.path())
                .collect()
        })
        .unwrap_or_default();
    v.sort_by(|a, b| natural(a).cmp(&natural(b)));
    v
}

fn natural(p: &Path) -> (String, u64) {
    let s = p.file_name().map(|x| x.to_string_lossy().to_string()).unwrap_or_default();
    let digits: String = s.chars().rev().take_while(|c| c.is_ascii_digit()).collect::<Vec<_>>().into_iter().rev().collect();
    let head = s[..s.len() - digits.len()].to_string();
    (head, digits.parse().unwrap_or(0))
}

/// Value of a "Key:   123 kB" line.
pub fn kv_kb(text: &str, key: &str) -> Option<u64> {
    text.lines().find_map(|l| {
        let (k, v) = l.split_once(':')?;
        if k.trim() == key {
            v.split_whitespace().next()?.parse().ok()
        } else {
            None
        }
    })
}

/// Rate in units per second from two counters, None on wrap or no time.
pub fn rate(prev: Option<u64>, cur: u64, dt: f64) -> Option<f64> {
    let prev = prev?;
    if dt <= 0.0 || cur < prev {
        return None;
    }
    Some((cur - prev) as f64 / dt)
}
