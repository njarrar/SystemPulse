//! Wi-Fi signal over generic netlink (nl80211 GET_STATION), with
//! /proc/net/wireless as a fallback. Plain libc sockets, no extra crates.

use std::time::Duration;

const NETLINK_GENERIC: i32 = 16;
const GENL_ID_CTRL: u16 = 0x10;
const CTRL_CMD_GETFAMILY: u8 = 3;
const CTRL_ATTR_FAMILY_ID: u16 = 1;
const CTRL_ATTR_FAMILY_NAME: u16 = 2;
const NL80211_CMD_GET_STATION: u8 = 17;
const NL80211_ATTR_IFINDEX: u16 = 3;
const NL80211_ATTR_STA_INFO: u16 = 21;
const NL80211_STA_INFO_SIGNAL: u16 = 7;
const NL80211_STA_INFO_TX_BITRATE: u16 = 8;
const NL80211_RATE_INFO_BITRATE: u16 = 1;
const NL80211_RATE_INFO_BITRATE32: u16 = 5;
const NLM_F_REQUEST: u16 = 1;
const NLM_F_ACK: u16 = 4;
const NLM_F_DUMP: u16 = 0x300;
const NLMSG_ERROR: u16 = 2;
const NLMSG_DONE: u16 = 3;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Station {
    pub signal_dbm: Option<i32>,
    /// Transmit bitrate in Mbit/s.
    pub tx_mbps: Option<f64>,
}

fn align4(n: usize) -> usize {
    (n + 3) & !3
}

fn attr(buf: &mut Vec<u8>, ty: u16, data: &[u8]) {
    let len = 4 + data.len();
    buf.extend_from_slice(&(len as u16).to_ne_bytes());
    buf.extend_from_slice(&ty.to_ne_bytes());
    buf.extend_from_slice(data);
    buf.resize(align4(buf.len()), 0);
}

fn message(ty: u16, flags: u16, seq: u32, cmd: u8, attrs: &[u8]) -> Vec<u8> {
    let mut m = Vec::with_capacity(20 + attrs.len());
    let len = 16 + 4 + attrs.len();
    m.extend_from_slice(&(len as u32).to_ne_bytes());
    m.extend_from_slice(&ty.to_ne_bytes());
    m.extend_from_slice(&flags.to_ne_bytes());
    m.extend_from_slice(&seq.to_ne_bytes());
    m.extend_from_slice(&0u32.to_ne_bytes());
    m.extend_from_slice(&[cmd, 1, 0, 0]);
    m.extend_from_slice(attrs);
    m
}

/// Iterates (type, payload) over a netlink attribute block.
pub fn attrs(mut b: &[u8]) -> Vec<(u16, &[u8])> {
    let mut out = Vec::new();
    while b.len() >= 4 {
        let len = u16::from_ne_bytes([b[0], b[1]]) as usize;
        let ty = u16::from_ne_bytes([b[2], b[3]]) & 0x3fff;
        if len < 4 || len > b.len() {
            break;
        }
        out.push((ty, &b[4..len]));
        b = &b[align4(len).min(b.len())..];
    }
    out
}

/// Pulls signal and bitrate out of a NL80211_ATTR_STA_INFO payload.
pub fn parse_sta_info(info: &[u8]) -> Station {
    let mut s = Station::default();
    for (ty, data) in attrs(info) {
        match ty {
            NL80211_STA_INFO_SIGNAL if !data.is_empty() => s.signal_dbm = Some(data[0] as i8 as i32),
            NL80211_STA_INFO_TX_BITRATE => {
                for (rt, rd) in attrs(data) {
                    if rt == NL80211_RATE_INFO_BITRATE32 && rd.len() >= 4 {
                        s.tx_mbps = Some(u32::from_ne_bytes([rd[0], rd[1], rd[2], rd[3]]) as f64 / 10.0);
                    } else if rt == NL80211_RATE_INFO_BITRATE && rd.len() >= 2 && s.tx_mbps.is_none() {
                        s.tx_mbps = Some(u16::from_ne_bytes([rd[0], rd[1]]) as f64 / 10.0);
                    }
                }
            }
            _ => {}
        }
    }
    s
}

struct Sock(i32);

impl Drop for Sock {
    fn drop(&mut self) {
        unsafe { libc::close(self.0) };
    }
}

impl Sock {
    fn open() -> Option<Sock> {
        let fd = unsafe { libc::socket(libc::AF_NETLINK, libc::SOCK_RAW | libc::SOCK_CLOEXEC, NETLINK_GENERIC) };
        if fd < 0 {
            return None;
        }
        let s = Sock(fd);
        let tv = libc::timeval { tv_sec: 0, tv_usec: Duration::from_millis(250).as_micros() as _ };
        unsafe {
            libc::setsockopt(fd, libc::SOL_SOCKET, libc::SO_RCVTIMEO, &tv as *const _ as *const _, std::mem::size_of::<libc::timeval>() as u32);
            let mut sa: libc::sockaddr_nl = std::mem::zeroed();
            sa.nl_family = libc::AF_NETLINK as u16;
            if libc::bind(fd, &sa as *const _ as *const libc::sockaddr, std::mem::size_of::<libc::sockaddr_nl>() as u32) < 0 {
                return None;
            }
        }
        Some(s)
    }

    fn send(&self, msg: &[u8]) -> bool {
        let mut sa: libc::sockaddr_nl = unsafe { std::mem::zeroed() };
        sa.nl_family = libc::AF_NETLINK as u16;
        let n = unsafe {
            libc::sendto(self.0, msg.as_ptr() as *const _, msg.len(), 0, &sa as *const _ as *const libc::sockaddr, std::mem::size_of::<libc::sockaddr_nl>() as u32)
        };
        n == msg.len() as isize
    }

    /// Reads replies until DONE/ERROR (or a non-multipart reply), calling
    /// `f` with each generic netlink attribute block.
    fn recv_all(&self, seq: u32, mut f: impl FnMut(&[u8])) -> bool {
        let mut buf = vec![0u8; 32768];
        for _ in 0..64 {
            let n = unsafe { libc::recv(self.0, buf.as_mut_ptr() as *mut _, buf.len(), 0) };
            if n <= 0 {
                return false;
            }
            let mut b = &buf[..n as usize];
            while b.len() >= 16 {
                let len = u32::from_ne_bytes([b[0], b[1], b[2], b[3]]) as usize;
                let ty = u16::from_ne_bytes([b[4], b[5]]);
                let flags = u16::from_ne_bytes([b[6], b[7]]);
                let mseq = u32::from_ne_bytes([b[8], b[9], b[10], b[11]]);
                if len < 16 || len > b.len() {
                    return false;
                }
                if mseq == seq {
                    if ty == NLMSG_DONE {
                        return true;
                    }
                    if ty == NLMSG_ERROR {
                        let err = i32::from_ne_bytes([b[16], b[17], b[18], b[19]]);
                        return err == 0;
                    }
                    if len >= 20 {
                        f(&b[20..len]);
                    }
                    if flags & 2 == 0 {
                        // not NLM_F_MULTI: single reply
                        return true;
                    }
                }
                b = &b[align4(len).min(b.len())..];
            }
        }
        false
    }
}

/// Resolves the nl80211 family id once, then queries stations.
#[derive(Default)]
pub struct Nl80211 {
    family: Option<Option<u16>>,
    seq: u32,
}

impl Nl80211 {
    fn family(&mut self, s: &Sock) -> Option<u16> {
        if let Some(f) = self.family {
            return f;
        }
        let mut a = Vec::new();
        attr(&mut a, CTRL_ATTR_FAMILY_NAME, b"nl80211\0");
        self.seq += 1;
        let seq = self.seq;
        let mut id = None;
        if s.send(&message(GENL_ID_CTRL, NLM_F_REQUEST | NLM_F_ACK, seq, CTRL_CMD_GETFAMILY, &a)) {
            s.recv_all(seq, |blk| {
                for (t, d) in attrs(blk) {
                    if t == CTRL_ATTR_FAMILY_ID && d.len() >= 2 {
                        id = Some(u16::from_ne_bytes([d[0], d[1]]));
                    }
                }
            });
        }
        self.family = Some(id);
        id
    }

    pub fn station(&mut self, ifindex: u32) -> Option<Station> {
        let s = Sock::open()?;
        let fam = self.family(&s)?;
        let mut a = Vec::new();
        attr(&mut a, NL80211_ATTR_IFINDEX, &ifindex.to_ne_bytes());
        self.seq += 1;
        let seq = self.seq;
        if !s.send(&message(fam, NLM_F_REQUEST | NLM_F_DUMP, seq, NL80211_CMD_GET_STATION, &a)) {
            return None;
        }
        let mut out = None;
        s.recv_all(seq, |blk| {
            for (t, d) in attrs(blk) {
                if t == NL80211_ATTR_STA_INFO && out.is_none() {
                    out = Some(parse_sta_info(d));
                }
            }
        });
        out
    }
}

/// Signal level from /proc/net/wireless ("wlp1s0: 0000   58.  -53.  -256").
pub fn proc_wireless(text: &str, iface: &str) -> Option<i32> {
    text.lines().find_map(|l| {
        let (name, rest) = l.split_once(':')?;
        if name.trim() != iface {
            return None;
        }
        let v: f64 = rest.split_whitespace().nth(2)?.trim_end_matches('.').parse().ok()?;
        Some(v as i32)
    })
}
