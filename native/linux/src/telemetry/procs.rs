//! Processes from /proc/[pid]/stat, grouped by app. A process inside a
//! systemd app scope (app-gnome-org.gnome.Nautilus-2221.scope, as GNOME
//! starts apps) is grouped by that app id; others by binary name.
//! GPU share per app comes from DRM fdinfo engine times.

use super::util::{kv_kb, p, read};
use std::collections::{HashMap, HashSet};
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AppGroup {
    pub key: String,
    pub name: String,
    pub pids: Vec<i32>,
    /// Percent of total CPU (all cores = 100).
    pub cpu: f64,
    pub mem_bytes: u64,
    pub gpu: Option<f64>,
    pub threads: u64,
    /// Share of the group's CPU time spent in user mode, 0..1.
    pub user_frac: Option<f64>,
    pub main_pid: i32,
    #[doc(hidden)]
    pub du: u64,
    #[doc(hidden)]
    pub ds: u64,
    pub uid: u32,
}

impl AppGroup {
    pub fn procs(&self) -> usize {
        self.pids.len()
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct StatLine {
    pub comm: String,
    pub ppid: i32,
    pub flags: u64,
    pub ticks: u64,
    pub utime: u64,
    pub stime: u64,
    pub threads: u64,
    pub start: u64,
    pub rss_pages: u64,
}

pub fn parse_stat(text: &str) -> Option<StatLine> {
    let open = text.find('(')?;
    let close = text.rfind(')')?;
    let comm = text[open + 1..close].to_string();
    let f: Vec<&str> = text[close + 1..].split_whitespace().collect();
    let n = |i: usize| f.get(i).and_then(|x| x.parse::<u64>().ok()).unwrap_or(0);
    if f.len() < 22 {
        return None;
    }
    Some(StatLine {
        comm,
        ppid: f[1].parse().unwrap_or(0),
        flags: n(6),
        ticks: n(11) + n(12),
        utime: n(11),
        stime: n(12),
        threads: n(17),
        start: n(19),
        rss_pages: n(21),
    })
}

/// App id from a cgroup v2 path, if the process runs in an app scope.
pub fn app_id_from_cgroup(text: &str) -> Option<String> {
    let path = text.lines().find_map(|l| l.strip_prefix("0::"))?;
    let leaf = path.rsplit('/').find(|s| s.starts_with("app-"))?;
    let mut s = leaf.strip_prefix("app-")?;
    for suf in [".scope", ".service"] {
        s = s.strip_suffix(suf).unwrap_or(s);
    }
    for pre in ["gnome-", "flatpak-", "dbus-:1.2-", "dbus-", "snap-", "kde-", "glib-"] {
        if let Some(x) = s.strip_prefix(pre) {
            s = x;
            break;
        }
    }
    // "@<hash>" template instances and a trailing "-<pid>" are not part
    // of the id.
    let s = s.split('@').next().unwrap_or(s);
    let s = match s.rsplit_once('-') {
        Some((a, b)) if b.chars().all(|c| c.is_ascii_digit()) && !a.is_empty() => a,
        _ => s,
    };
    // systemd escapes "-" as "\x2d".
    let s = s.replace("\\x2d", "-");
    (!s.is_empty() && s != "autostart" && !s.starts_with("org.gnome.Terminal") && !s.starts_with("org.gnome.Console") && !s.starts_with("org.gnome.Ptyxis")).then_some(s)
}

/// Name= from the [Desktop Entry] group of a .desktop file.
pub fn desktop_name(text: &str) -> Option<String> {
    let mut in_entry = false;
    for l in text.lines() {
        let l = l.trim();
        if l.starts_with('[') {
            in_entry = l == "[Desktop Entry]";
        } else if in_entry {
            if let Some(v) = l.strip_prefix("Name=") {
                return Some(v.to_string());
            }
        }
    }
    None
}

fn app_dirs() -> Vec<PathBuf> {
    let mut v = Vec::new();
    let home = std::env::var("HOME").unwrap_or_default();
    let data_home = std::env::var("XDG_DATA_HOME").unwrap_or_else(|_| format!("{home}/.local/share"));
    v.push(PathBuf::from(&data_home).join("applications"));
    v.push(PathBuf::from(&data_home).join("flatpak/exports/share/applications"));
    v.push(PathBuf::from("/var/lib/flatpak/exports/share/applications"));
    v.push(PathBuf::from("/var/lib/snapd/desktop/applications"));
    let dirs = std::env::var("XDG_DATA_DIRS").unwrap_or_else(|_| "/usr/local/share:/usr/share".into());
    for d in dirs.split(':').filter(|d| !d.is_empty()) {
        v.push(PathBuf::from(d).join("applications"));
    }
    v
}

#[derive(Clone)]
struct Meta {
    start: u64,
    key: String,
    uid: u32,
}

#[derive(Default, Clone)]
struct DrmState {
    fds: Vec<String>,
    /// engine time per (client id, engine) in ns
    last: HashMap<(String, String), u64>,
}

pub struct ProcSampler {
    root: PathBuf,
    meta: HashMap<i32, Meta>,
    prev_ticks: HashMap<i32, (u64, u64)>,
    names: HashMap<String, String>,
    dirs: Vec<PathBuf>,
    page: u64,
    drm: HashMap<i32, DrmState>,
    drm_scan: u32,
    drm_total: Option<f64>,
    own_pid: i32,
}

impl ProcSampler {
    pub fn new(root: &Path) -> Self {
        ProcSampler {
            root: root.to_path_buf(),
            meta: HashMap::new(),
            prev_ticks: HashMap::new(),
            names: HashMap::new(),
            dirs: app_dirs(),
            page: unsafe { libc::sysconf(libc::_SC_PAGESIZE) }.max(4096) as u64,
            drm: HashMap::new(),
            drm_scan: 0,
            drm_total: None,
            own_pid: std::process::id() as i32,
        }
    }

    pub fn drm_total_busy(&self) -> Option<f64> {
        self.drm_total
    }

    fn display_name(&mut self, key: &str) -> String {
        if let Some(n) = self.names.get(key) {
            return n.clone();
        }
        let mut name = None;
        for d in &self.dirs {
            for cand in [format!("{key}.desktop"), format!("{}.desktop", key.to_lowercase())] {
                if let Some(t) = read(d.join(&cand)) {
                    name = desktop_name(&t);
                    break;
                }
            }
            if name.is_some() {
                break;
            }
        }
        // org.gnome.Nautilus with no desktop file: show "Nautilus".
        let name = name.unwrap_or_else(|| {
            if key.matches('.').count() >= 2 {
                key.rsplit('.').next().unwrap_or(key).to_string()
            } else {
                key.to_string()
            }
        });
        self.names.insert(key.to_string(), name.clone());
        name
    }

    fn key_for(&self, pid: i32, st: &StatLine) -> String {
        let dir = p(&self.root, &format!("/proc/{pid}"));
        if let Some(id) = read(dir.join("cgroup")).and_then(|c| app_id_from_cgroup(&c)) {
            return id;
        }
        if let Ok(exe) = std::fs::read_link(dir.join("exe")) {
            let s = exe.to_string_lossy();
            let s = s.trim_end_matches(" (deleted)");
            if let Some(base) = s.rsplit('/').next().filter(|b| !b.is_empty()) {
                // interpreters: keep the process name (python3 -> script)
                let interp = ["python", "python3", "perl", "node", "ruby", "bash", "sh", "java"];
                if !interp.iter().any(|i| base == *i || base.starts_with(&format!("{i}."))) {
                    return base.to_string();
                }
            }
        }
        st.comm.clone()
    }

    fn drm_fds(&self, pid: i32) -> Vec<String> {
        let dir = p(&self.root, &format!("/proc/{pid}/fd"));
        let Ok(rd) = std::fs::read_dir(&dir) else { return Vec::new() };
        rd.flatten()
            .filter(|e| {
                std::fs::read_link(e.path())
                    .map(|l| l.to_string_lossy().starts_with("/dev/dri/"))
                    .unwrap_or(false)
            })
            .map(|e| e.file_name().to_string_lossy().to_string())
            .collect()
    }

    /// Returns per-engine busy ns deltas summed over the pid's DRM clients.
    fn drm_sample(&mut self, pid: i32, rescan: bool) -> HashMap<String, u64> {
        let mut st = self.drm.remove(&pid).unwrap_or_default();
        if rescan {
            st.fds = self.drm_fds(pid);
        }
        let mut out: HashMap<String, u64> = HashMap::new();
        let mut seen = HashSet::new();
        let mut now = HashMap::new();
        for fd in &st.fds {
            let Some(t) = read(p(&self.root, &format!("/proc/{pid}/fdinfo/{fd}"))) else { continue };
            let client = t
                .lines()
                .find_map(|l| l.strip_prefix("drm-client-id:"))
                .map(|x| x.trim().to_string())
                .unwrap_or_else(|| fd.clone());
            if !seen.insert(client.clone()) {
                continue;
            }
            for l in t.lines() {
                if let Some(rest) = l.strip_prefix("drm-engine-") {
                    if let Some((eng, v)) = rest.split_once(':') {
                        if eng.starts_with("capacity") {
                            continue;
                        }
                        let ns: u64 = v.split_whitespace().next().and_then(|x| x.parse().ok()).unwrap_or(0);
                        let k = (client.clone(), eng.to_string());
                        if let Some(prev) = st.last.get(&k) {
                            *out.entry(eng.to_string()).or_default() += ns.saturating_sub(*prev);
                        }
                        now.insert(k, ns);
                    }
                }
            }
        }
        st.last = now;
        if !st.fds.is_empty() {
            self.drm.insert(pid, st);
        }
        out
    }

    pub fn sample(&mut self, total_jiffies: u64, dt: f64) -> Vec<AppGroup> {
        let Ok(rd) = std::fs::read_dir(p(&self.root, "/proc")) else { return Vec::new() };
        let rescan = self.drm_scan % 10 == 0;
        self.drm_scan = self.drm_scan.wrapping_add(1);
        let mut groups: HashMap<String, AppGroup> = HashMap::new();
        let mut alive = HashSet::new();
        let mut gpu_engines: HashMap<String, u64> = HashMap::new();
        for e in rd.flatten() {
            let Ok(pid) = e.file_name().to_string_lossy().parse::<i32>() else { continue };
            let Some(st) = read(e.path().join("stat")).and_then(|t| parse_stat(&t)) else { continue };
            // kernel threads have PF_KTHREAD set
            if st.flags & 0x0020_0000 != 0 || pid == 0 {
                continue;
            }
            alive.insert(pid);
            let meta = match self.meta.get(&pid) {
                Some(m) if m.start == st.start => m.clone(),
                _ => {
                    let uid = e.metadata().map(|m| m.uid()).unwrap_or(0);
                    let m = Meta { start: st.start, key: self.key_for(pid, &st), uid };
                    self.meta.insert(pid, m.clone());
                    self.prev_ticks.remove(&pid);
                    m
                }
            };
            let prev = self.prev_ticks.insert(pid, (st.utime, st.stime));
            let (du, ds) = match prev {
                Some((pu, ps)) => (st.utime.saturating_sub(pu), st.stime.saturating_sub(ps)),
                None => (0, 0),
            };
            let cpu = if total_jiffies > 0 { (du + ds) as f64 / total_jiffies as f64 * 100.0 } else { 0.0 };
            let gpu = if rescan || self.drm.contains_key(&pid) {
                let eng = self.drm_sample(pid, rescan);
                for (k, v) in &eng {
                    *gpu_engines.entry(k.clone()).or_default() += v;
                }
                eng.values().max().map(|ns| if dt > 0.0 { *ns as f64 / (dt * 1e9) * 100.0 } else { 0.0 })
            } else {
                None
            };
            let g = groups.entry(meta.key.clone()).or_insert_with(|| AppGroup {
                key: meta.key.clone(),
                main_pid: pid,
                uid: meta.uid,
                ..Default::default()
            });
            g.pids.push(pid);
            g.cpu += cpu;
            g.du += du;
            g.ds += ds;
            g.mem_bytes += st.rss_pages * self.page;
            g.threads += st.threads;
            if let Some(x) = gpu {
                g.gpu = Some(g.gpu.unwrap_or(0.0) + x);
            }
            if pid < g.main_pid {
                g.main_pid = pid;
            }
        }
        self.meta.retain(|k, _| alive.contains(k));
        self.prev_ticks.retain(|k, _| alive.contains(k));
        self.drm.retain(|k, _| alive.contains(k));
        self.drm_total = if dt > 0.0 && !self.drm.is_empty() {
            gpu_engines.values().max().map(|ns| (*ns as f64 / (dt * 1e9) * 100.0).min(100.0))
        } else {
            None
        };
        let mut out: Vec<AppGroup> = groups.into_values().collect();
        for g in &mut out {
            g.name = self.display_name(&g.key);
            g.cpu = g.cpu.min(100.0);
            g.gpu = g.gpu.map(|x| x.min(100.0));
            g.user_frac = (g.du + g.ds > 0).then(|| g.du as f64 / (g.du + g.ds) as f64);
            g.pids.sort();
        }
        out.retain(|g| !(g.pids.len() == 1 && g.pids[0] == self.own_pid && g.cpu < 0.05));
        out
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ProcStatus {
    pub threads: Option<u64>,
    pub rss_bytes: Option<u64>,
    pub uid: Option<u32>,
}

pub fn parse_status(text: &str) -> ProcStatus {
    ProcStatus {
        threads: kv_kb(text, "Threads"),
        rss_bytes: kv_kb(text, "VmRSS").map(|k| k * 1024),
        uid: text
            .lines()
            .find_map(|l| l.strip_prefix("Uid:"))
            .and_then(|v| v.split_whitespace().next()?.parse().ok()),
    }
}

pub fn read_status(root: &Path, pid: i32) -> Option<ProcStatus> {
    read(p(root, &format!("/proc/{pid}/status"))).map(|t| parse_status(&t))
}

/// SIGTERM to each pid. Returns how many were signalled.
pub fn terminate(pids: &[i32]) -> usize {
    pids.iter()
        .filter(|&&pid| pid > 1 && unsafe { libc::kill(pid, libc::SIGTERM) } == 0)
        .count()
}

/// What it takes to start an app again: argv and working directory of
/// the group's root process (the one whose parent is outside the group).
#[derive(Clone, Debug, PartialEq)]
pub struct Launch {
    pub name: String,
    pub argv: Vec<String>,
    pub cwd: Option<PathBuf>,
}

pub fn launch_info(root: &Path, name: &str, pids: &[i32]) -> Option<Launch> {
    let parent = |pid: i32| read(p(root, &format!("/proc/{pid}/stat"))).and_then(|t| parse_stat(&t)).map(|s| s.ppid);
    let main = pids.iter().copied().find(|&pid| parent(pid).map(|pp| !pids.contains(&pp)).unwrap_or(false)).or(pids.first().copied())?;
    let raw = std::fs::read(p(root, &format!("/proc/{main}/cmdline"))).ok()?;
    let argv: Vec<String> = raw.split(|b| *b == 0).filter(|s| !s.is_empty()).map(|s| String::from_utf8_lossy(s).to_string()).collect();
    if argv.is_empty() {
        return None;
    }
    let cwd = std::fs::read_link(p(root, &format!("/proc/{main}/cwd"))).ok();
    Some(Launch { name: name.to_string(), argv, cwd })
}

/// Starts an app again in its own session, detached from Pulse.
pub fn relaunch(l: &Launch) -> bool {
    use std::os::unix::process::CommandExt;
    let mut cmd = std::process::Command::new(&l.argv[0]);
    cmd.args(&l.argv[1..])
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    if let Some(d) = l.cwd.as_ref().filter(|d| d.is_dir()) {
        cmd.current_dir(d);
    }
    unsafe {
        cmd.pre_exec(|| {
            libc::setsid();
            Ok(())
        });
    }
    match cmd.spawn() {
        Ok(mut child) => {
            std::thread::spawn(move || child.wait());
            true
        }
        Err(e) => {
            eprintln!("pulse: could not restart {}: {e}", l.name);
            false
        }
    }
}
