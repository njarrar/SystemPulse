//! Network throughput from /proc/net/dev for the default-route interface,
//! link details from /sys/class/net, Wi-Fi signal over nl80211.

use super::nl80211::{proc_wireless, Nl80211};
use super::util::{p, rate, read, read_trim, read_u64};
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct NetSnapshot {
    pub iface: String,
    pub wireless: bool,
    pub up: bool,
    pub down_bps: Option<f64>,
    pub up_bps: Option<f64>,
    pub rx_total: u64,
    pub tx_total: u64,
    pub link_mbps: Option<f64>,
    pub signal_dbm: Option<i32>,
}

/// (iface, rx bytes, tx bytes) rows of /proc/net/dev.
pub fn parse_net_dev(text: &str) -> Vec<(String, u64, u64)> {
    text.lines()
        .skip(2)
        .filter_map(|l| {
            let (name, rest) = l.split_once(':')?;
            let f: Vec<u64> = rest.split_whitespace().filter_map(|x| x.parse().ok()).collect();
            (f.len() >= 9).then(|| (name.trim().to_string(), f[0], f[8]))
        })
        .collect()
}

/// Interface of the default route in /proc/net/route.
pub fn default_iface(route: &str) -> Option<String> {
    route.lines().skip(1).find_map(|l| {
        let f: Vec<&str> = l.split_whitespace().collect();
        (f.len() > 2 && f[1] == "00000000").then(|| f[0].to_string())
    })
}

pub struct NetSampler {
    root: PathBuf,
    prev: Option<(String, u64, u64)>,
    nl: Nl80211,
    since_start: Option<(u64, u64)>,
}

impl NetSampler {
    pub fn new(root: &Path) -> Self {
        NetSampler { root: root.to_path_buf(), prev: None, nl: Nl80211::default(), since_start: None }
    }

    pub fn sample(&mut self, dt: f64) -> Option<NetSnapshot> {
        let rows = parse_net_dev(&read(p(&self.root, "/proc/net/dev"))?);
        let def = read(p(&self.root, "/proc/net/route")).and_then(|r| default_iface(&r));
        let pick = def
            .and_then(|d| rows.iter().find(|r| r.0 == d).cloned())
            .or_else(|| {
                rows.iter()
                    .filter(|r| r.0 != "lo")
                    .max_by_key(|r| r.1 + r.2)
                    .cloned()
            })?;
        let (iface, rx, tx) = pick;
        let same = self.prev.as_ref().map(|p| p.0 == iface).unwrap_or(false);
        let (prx, ptx) = if same { (self.prev.as_ref().map(|p| p.1), self.prev.as_ref().map(|p| p.2)) } else { (None, None) };
        self.prev = Some((iface.clone(), rx, tx));
        let base = *self.since_start.get_or_insert((rx, tx));
        let sys = p(&self.root, &format!("/sys/class/net/{iface}"));
        let wireless = sys.join("wireless").exists() || sys.join("phy80211").exists();
        let up = read_trim(sys.join("operstate")).map(|s| s == "up" || s == "unknown").unwrap_or(true);
        let mut n = NetSnapshot {
            iface: iface.clone(),
            wireless,
            up,
            down_bps: rate(prx, rx, dt),
            up_bps: rate(ptx, tx, dt),
            rx_total: rx.saturating_sub(base.0),
            tx_total: tx.saturating_sub(base.1),
            link_mbps: read_u64(sys.join("speed")).filter(|s| *s > 0 && *s < 1_000_000).map(|s| s as f64),
            signal_dbm: None,
        };
        if wireless {
            if let Some(idx) = read_u64(sys.join("ifindex")) {
                if let Some(st) = self.nl.station(idx as u32) {
                    n.signal_dbm = st.signal_dbm;
                    n.link_mbps = st.tx_mbps.or(n.link_mbps);
                }
            }
            if n.signal_dbm.is_none() {
                n.signal_dbm = read(p(&self.root, "/proc/net/wireless")).and_then(|t| proc_wireless(&t, &iface));
            }
        }
        Some(n)
    }
}
