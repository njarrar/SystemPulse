//! Live telemetry from /proc, /sys and netlink.
//!
//! Each source is read on every poll. A source that does not exist (a
//! desktop has no battery, a container has no hwmon or DRM device) gives
//! None, and the UI shows a dash for it. Nothing here is simulated.

pub mod cpu;
pub mod disk;
pub mod gpu;
pub mod mem;
pub mod net;
pub mod nl80211;
pub mod power;
pub mod procs;
pub mod thermal;
pub mod util;

use std::path::PathBuf;
use std::time::Instant;

pub use cpu::{CoreGroups, CpuSnapshot};
pub use disk::DiskSnapshot;
pub use gpu::GpuSnapshot;
pub use mem::MemSnapshot;
pub use net::NetSnapshot;
pub use power::PowerSnapshot;
pub use procs::AppGroup;
pub use thermal::ThermalSnapshot;

#[derive(Clone, Debug, Default)]
pub struct Snapshot {
    pub cpu: CpuSnapshot,
    pub mem: Option<MemSnapshot>,
    pub power: PowerSnapshot,
    pub thermal: ThermalSnapshot,
    pub gpu: Option<GpuSnapshot>,
    pub disk: Option<DiskSnapshot>,
    pub net: Option<NetSnapshot>,
    pub apps: Vec<AppGroup>,
}

/// Paths are relative to a root so tests can point the sampler at a fake
/// tree. The real app uses "/".
pub struct Sampler {
    pub root: PathBuf,
    cpu: cpu::CpuSampler,
    power: power::PowerSampler,
    thermal: thermal::ThermalSampler,
    gpu: gpu::GpuSampler,
    disk: disk::DiskSampler,
    net: net::NetSampler,
    procs: procs::ProcSampler,
    last: Option<Instant>,
}

impl Sampler {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        let root = root.into();
        Sampler {
            cpu: cpu::CpuSampler::new(&root),
            power: power::PowerSampler::new(&root),
            thermal: thermal::ThermalSampler::new(&root),
            gpu: gpu::GpuSampler::new(&root),
            disk: disk::DiskSampler::new(&root),
            net: net::NetSampler::new(&root),
            procs: procs::ProcSampler::new(&root),
            root,
            last: None,
        }
    }

    pub fn sample(&mut self) -> Snapshot {
        let now = Instant::now();
        let dt = self.last.map(|l| now.duration_since(l).as_secs_f64()).unwrap_or(0.0);
        self.last = Some(now);
        let cpu = self.cpu.sample();
        let total_jiffies = self.cpu.last_total_delta();
        let apps = self.procs.sample(total_jiffies, dt);
        let drm_busy = self.procs.drm_total_busy();
        let mut gpu = self.gpu.sample(dt);
        if let Some(g) = gpu.as_mut() {
            if g.busy_pct.is_none() {
                g.busy_pct = drm_busy;
            }
        }
        Snapshot {
            cpu,
            mem: mem::read(&self.root),
            power: self.power.sample(dt),
            thermal: self.thermal.sample(gpu.as_ref().and_then(|g| g.temp_c)),
            gpu,
            disk: self.disk.sample(dt),
            net: self.net.sample(dt),
            apps,
        }
    }

    /// Sends SIGTERM to every process of an app group. Returns how many
    /// signals were delivered.
    pub fn end_app(&self, pids: &[i32]) -> usize {
        procs::terminate(pids)
    }

    pub fn proc_details(&self, pid: i32) -> Option<procs::ProcStatus> {
        procs::read_status(&self.root, pid)
    }
}
