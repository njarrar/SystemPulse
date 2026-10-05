//! GPU from /sys/class/drm (amdgpu: gpu_busy_percent, mem_info_vram_*,
//! pp_dpm_sclk, hwmon power and temperature; i915/xe: gt frequency) and
//! from NVML (libnvidia-ml.so.1, loaded at run time) on NVIDIA.
//! When the driver has no busy counter, the sampler fills utilization from
//! the per-process DRM fdinfo engine times (see procs.rs).

use super::util::{list, p, read, read_f64, read_trim, read_u64};
use std::ffi::{c_char, c_int, c_uint, c_void, CStr};
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct GpuSnapshot {
    pub name: String,
    pub driver: String,
    pub busy_pct: Option<f64>,
    pub vram_used: Option<u64>,
    pub vram_total: Option<u64>,
    pub clock_mhz: Option<f64>,
    pub power_w: Option<f64>,
    pub temp_c: Option<f64>,
    pub hotspot_c: Option<f64>,
}

/// Active level of pp_dpm_sclk ("1: 2700Mhz *").
pub fn parse_dpm(text: &str) -> Option<f64> {
    let l = text.lines().find(|l| l.trim_end().ends_with('*'))?;
    let v = l.split_whitespace().nth(1)?;
    let num: String = v.chars().take_while(|c| c.is_ascii_digit() || *c == '.').collect();
    num.parse().ok()
}

/// Looks a PCI id up in the system pci.ids database.
pub fn pci_name(root: &Path, vendor: &str, device: &str) -> Option<String> {
    let v = vendor.trim_start_matches("0x").to_lowercase();
    let d = device.trim_start_matches("0x").to_lowercase();
    for f in ["/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"] {
        let Some(text) = read(p(root, f)) else { continue };
        let mut in_vendor = false;
        let mut vendor_name = String::new();
        for line in text.lines() {
            if line.starts_with('#') || line.is_empty() {
                continue;
            }
            if !line.starts_with('\t') {
                in_vendor = line.starts_with(&v);
                if in_vendor {
                    vendor_name = line[v.len()..].trim().to_string();
                }
            } else if in_vendor && !line.starts_with("\t\t") && line[1..].starts_with(&d) {
                let dev = line[1 + d.len()..].trim();
                // "Phoenix1" style code names get the bracketed marketing
                // name when there is one: "... [Radeon 780M]".
                if let (Some(a), Some(b)) = (dev.rfind('['), dev.rfind(']')) {
                    if b > a {
                        let known = vendor_label(&format!("0x{v}"));
                        let short = if known != "GPU" { known } else { vendor_name.split_whitespace().next().unwrap_or("") };
                        return Some(format!("{short} {}", &dev[a + 1..b]).trim().to_string());
                    }
                }
                return Some(dev.to_string());
            }
        }
    }
    None
}

fn vendor_label(v: &str) -> &'static str {
    match v {
        "0x1002" => "AMD",
        "0x8086" => "Intel",
        "0x10de" => "NVIDIA",
        "0x13b5" => "Arm",
        "0x5143" => "Qualcomm",
        _ => "GPU",
    }
}

pub struct GpuSampler {
    card: Option<PathBuf>,
    name: String,
    driver: String,
    nvml: Option<Nvml>,
}

impl GpuSampler {
    pub fn new(root: &Path) -> Self {
        let mut s = GpuSampler { card: None, name: String::new(), driver: String::new(), nvml: None };
        // Prefer the card with a busy counter (the discrete or APU amdgpu),
        // else the first card with a PCI device.
        let cards: Vec<PathBuf> = list(p(root, "/sys/class/drm"), "card")
            .into_iter()
            .filter(|c| c.file_name().map(|n| n.to_string_lossy()[4..].chars().all(|x| x.is_ascii_digit())).unwrap_or(false))
            .filter(|c| c.join("device").exists())
            .collect();
        let card = cards
            .iter()
            .find(|c| c.join("device/gpu_busy_percent").exists())
            .or_else(|| cards.first())
            .cloned();
        if let Some(c) = &card {
            let dev = c.join("device");
            let vendor = read_trim(dev.join("vendor")).unwrap_or_default();
            let device = read_trim(dev.join("device")).unwrap_or_default();
            s.driver = std::fs::read_link(dev.join("driver"))
                .ok()
                .and_then(|l| l.file_name().map(|x| x.to_string_lossy().to_string()))
                .unwrap_or_default();
            s.name = read_trim(dev.join("product_name"))
                .or_else(|| pci_name(root, &vendor, &device))
                .unwrap_or_else(|| format!("{} {}", vendor_label(&vendor), device.trim_start_matches("0x")));
            if s.driver == "nvidia" || vendor == "0x10de" {
                s.nvml = Nvml::load();
            }
        } else {
            s.nvml = Nvml::load();
        }
        s.card = card;
        s
    }

    pub fn sample(&mut self, _dt: f64) -> Option<GpuSnapshot> {
        if let Some(n) = &self.nvml {
            if let Some(g) = n.sample() {
                return Some(g);
            }
        }
        let card = self.card.as_ref()?;
        let dev = card.join("device");
        let mut g = GpuSnapshot { name: self.name.clone(), driver: self.driver.clone(), ..Default::default() };
        g.busy_pct = read_f64(dev.join("gpu_busy_percent"));
        g.vram_used = read_u64(dev.join("mem_info_vram_used"));
        g.vram_total = read_u64(dev.join("mem_info_vram_total"));
        g.clock_mhz = read(dev.join("pp_dpm_sclk"))
            .and_then(|t| parse_dpm(&t))
            .or_else(|| read_f64(card.join("gt_act_freq_mhz")))
            .or_else(|| read_f64(card.join("gt_cur_freq_mhz")))
            .or_else(|| {
                list(card.join("device/tile0"), "gt")
                    .first()
                    .and_then(|gt| read_f64(gt.join("freq0/act_freq")))
            });
        for h in list(dev.join("hwmon"), "hwmon") {
            for i in 1..=4 {
                let lab = read_trim(h.join(format!("temp{i}_label"))).unwrap_or_default();
                if let Some(v) = read_f64(h.join(format!("temp{i}_input"))) {
                    match lab.as_str() {
                        "junction" => g.hotspot_c = Some(v / 1000.0),
                        "mem" => {}
                        _ => {
                            g.temp_c.get_or_insert(v / 1000.0);
                        }
                    }
                }
            }
            g.power_w = read_f64(h.join("power1_average"))
                .or_else(|| read_f64(h.join("power1_input")))
                .map(|uw| uw / 1e6);
        }
        Some(g)
    }
}

// ---------------------------------------------------------------------------
// NVML, loaded with dlopen so the binary has no NVIDIA dependency.
// ---------------------------------------------------------------------------

#[repr(C)]
struct NvmlUtil {
    gpu: c_uint,
    memory: c_uint,
}

#[repr(C)]
struct NvmlMem {
    total: u64,
    free: u64,
    used: u64,
}

type FInit = unsafe extern "C" fn() -> c_int;
type FHandle = unsafe extern "C" fn(c_uint, *mut *mut c_void) -> c_int;
type FUtil = unsafe extern "C" fn(*mut c_void, *mut NvmlUtil) -> c_int;
type FTemp = unsafe extern "C" fn(*mut c_void, c_int, *mut c_uint) -> c_int;
type FName = unsafe extern "C" fn(*mut c_void, *mut c_char, c_uint) -> c_int;
type FMem = unsafe extern "C" fn(*mut c_void, *mut NvmlMem) -> c_int;
type FPower = unsafe extern "C" fn(*mut c_void, *mut c_uint) -> c_int;
type FClock = unsafe extern "C" fn(*mut c_void, c_int, *mut c_uint) -> c_int;

struct Nvml {
    dev: *mut c_void,
    name: String,
    util: FUtil,
    temp: FTemp,
    mem: FMem,
    power: FPower,
    clock: FClock,
}

impl Nvml {
    fn load() -> Option<Nvml> {
        unsafe {
            let lib = libc::dlopen(c"libnvidia-ml.so.1".as_ptr(), libc::RTLD_NOW | libc::RTLD_LOCAL);
            if lib.is_null() {
                return None;
            }
            let sym = |n: &CStr| {
                let s = libc::dlsym(lib, n.as_ptr());
                (!s.is_null()).then_some(s)
            };
            let init: FInit = std::mem::transmute(sym(c"nvmlInit_v2")?);
            let handle: FHandle = std::mem::transmute(sym(c"nvmlDeviceGetHandleByIndex_v2")?);
            let name_f: FName = std::mem::transmute(sym(c"nvmlDeviceGetName")?);
            if init() != 0 {
                return None;
            }
            let mut dev = std::ptr::null_mut();
            if handle(0, &mut dev) != 0 {
                return None;
            }
            let mut buf = [0 as c_char; 96];
            let name = if name_f(dev, buf.as_mut_ptr(), 96) == 0 {
                CStr::from_ptr(buf.as_ptr()).to_string_lossy().to_string()
            } else {
                "NVIDIA GPU".into()
            };
            Some(Nvml {
                dev,
                name,
                util: std::mem::transmute(sym(c"nvmlDeviceGetUtilizationRates")?),
                temp: std::mem::transmute(sym(c"nvmlDeviceGetTemperature")?),
                mem: std::mem::transmute(sym(c"nvmlDeviceGetMemoryInfo")?),
                power: std::mem::transmute(sym(c"nvmlDeviceGetPowerUsage")?),
                clock: std::mem::transmute(sym(c"nvmlDeviceGetClockInfo")?),
            })
        }
    }

    fn sample(&self) -> Option<GpuSnapshot> {
        unsafe {
            let mut u = NvmlUtil { gpu: 0, memory: 0 };
            let busy = ((self.util)(self.dev, &mut u) == 0).then_some(u.gpu as f64);
            let mut t: c_uint = 0;
            let temp = ((self.temp)(self.dev, 0, &mut t) == 0).then_some(t as f64);
            let mut m = NvmlMem { total: 0, free: 0, used: 0 };
            let mem_ok = (self.mem)(self.dev, &mut m) == 0;
            let mut mw: c_uint = 0;
            let power = ((self.power)(self.dev, &mut mw) == 0).then_some(mw as f64 / 1000.0);
            let mut mhz: c_uint = 0;
            let clock = ((self.clock)(self.dev, 0, &mut mhz) == 0).then_some(mhz as f64);
            Some(GpuSnapshot {
                name: self.name.clone(),
                driver: "nvidia".into(),
                busy_pct: busy,
                vram_used: mem_ok.then_some(m.used),
                vram_total: mem_ok.then_some(m.total),
                clock_mhz: clock,
                power_w: power,
                temp_c: temp,
                hotspot_c: None,
            })
        }
    }
}
