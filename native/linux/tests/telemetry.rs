//! Parser and sampler tests against a fake /proc and /sys tree, plus one
//! smoke run against the real system.

use pulse::telemetry::*;
use std::fs;
use std::path::{Path, PathBuf};

fn w(root: &Path, rel: &str, body: &str) {
    let p = root.join(rel.trim_start_matches('/'));
    fs::create_dir_all(p.parent().unwrap()).unwrap();
    fs::write(p, body).unwrap();
}

fn fake_root(tag: &str) -> PathBuf {
    let r = std::env::temp_dir().join(format!("pulse-fake-{tag}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&r);
    fs::create_dir_all(&r).unwrap();
    r
}

#[test]
fn cpu_ticks_and_hybrid_groups() {
    let r = fake_root("cpu");
    for c in 0..4 {
        w(&r, &format!("/sys/devices/system/cpu/cpu{c}/acpi_cppc/highest_perf"), if c < 2 { "196" } else { "120" });
        w(&r, &format!("/sys/devices/system/cpu/cpu{c}/topology/core_id"), &c.to_string());
        w(&r, &format!("/sys/devices/system/cpu/cpu{c}/topology/physical_package_id"), "0");
    }
    w(&r, "/proc/cpuinfo", "vendor_id\t: AuthenticAMD\nmodel name\t: AMD Ryzen 5 PRO 8540U\n");
    w(&r, "/proc/loadavg", "1.25 0.80 0.50 1/300 1234\n");
    let stat = |b: u64| {
        format!(
            "cpu  {} 0 {} {} 0 0 0 0 0 0\ncpu0 {} 0 0 {} 0 0 0 0 0 0\ncpu1 {} 0 0 {} 0 0 0 0 0 0\ncpu2 0 0 {} {} 0 0 0 0 0 0\ncpu3 0 0 0 {} 0 0 0 0 0 0\n",
            b * 2, b, 1000 + b, b, 250, b, 250, b, 250, 250 + b
        )
    };
    w(&r, "/proc/stat", &stat(0));
    let mut s = cpu::CpuSampler::new(&r);
    s.sample();
    w(&r, "/proc/stat", &stat(100));
    let snap = s.sample();
    assert!(snap.valid);
    match &snap.groups {
        CoreGroups::Hybrid { amd, a, b } => {
            assert!(*amd);
            assert_eq!(a, &vec![0, 1]);
            assert_eq!(b, &vec![2, 3]);
        }
        g => panic!("{g:?}"),
    }
    assert_eq!(snap.threads, 4);
    assert_eq!(snap.physical_cores, 4);
    assert_eq!(snap.load1, Some(1.25));
    assert!((snap.group_a - 100.0).abs() < 0.01, "{}", snap.group_a);
    assert!((snap.group_b - 50.0).abs() < 0.01, "{}", snap.group_b);
    assert!((snap.total - 300.0 / 400.0 * 100.0).abs() < 0.01, "{}", snap.total);
    assert_eq!(snap.model.as_deref(), Some("AMD Ryzen 5 PRO 8540U"));
}

#[test]
fn memory_segments_add_up() {
    let m = mem::parse(
        "MemTotal: 16000000 kB\nMemFree: 1000000 kB\nMemAvailable: 6000000 kB\nBuffers: 500000 kB\nCached: 4000000 kB\nSwapTotal: 8000000 kB\nSwapFree: 7500000 kB\nZswap: 1200000 kB\n",
        None,
    )
    .unwrap();
    assert_eq!(m.used, 10_000_000 * 1024);
    assert_eq!(m.app + m.buffers + m.compressed, m.used);
    assert_eq!(m.compressed_kind, "zswap");
    assert_eq!(m.swap_used, 500_000 * 1024);
    assert_eq!(mem::parse_psi("some avg10=0.40 avg60=0.10 avg300=0.00 total=1\nfull avg10=0.00"), Some(0.4));
}

#[test]
fn battery_and_hwmon() {
    let r = fake_root("power");
    let b = "/sys/class/power_supply/BAT0";
    w(&r, &format!("{b}/type"), "Battery");
    w(&r, &format!("{b}/capacity"), "84");
    w(&r, &format!("{b}/status"), "Discharging");
    w(&r, &format!("{b}/power_now"), "14800000");
    w(&r, &format!("{b}/energy_now"), "39466000");
    w(&r, &format!("{b}/energy_full"), "69000000");
    w(&r, &format!("{b}/energy_full_design"), "72600000");
    w(&r, &format!("{b}/cycle_count"), "118");
    w(&r, "/sys/class/power_supply/AC/type", "Mains");
    w(&r, "/sys/class/power_supply/AC/online", "0");
    let mut ps = power::PowerSampler::new(&r);
    let p = ps.sample(1.5);
    let bat = p.battery.unwrap();
    assert_eq!(bat.capacity, 84.0);
    assert_eq!(bat.watts, Some(14.8));
    assert_eq!(bat.state, power::BattState::Discharging);
    assert_eq!(bat.cycles, Some(118));
    assert!((bat.minutes_left.unwrap() - 160.0).abs() < 0.1);
    assert!((bat.health_pct.unwrap() - 95.04).abs() < 0.01);
    assert_eq!(p.ac_online, Some(false));

    w(&r, "/sys/class/hwmon/hwmon0/name", "k10temp");
    w(&r, "/sys/class/hwmon/hwmon0/temp1_input", "52125");
    w(&r, "/sys/class/hwmon/hwmon0/temp1_label", "Tctl");
    w(&r, "/sys/class/hwmon/hwmon1/name", "nvme");
    w(&r, "/sys/class/hwmon/hwmon1/temp1_input", "29000");
    w(&r, "/sys/class/hwmon/hwmon2/name", "thinkpad");
    w(&r, "/sys/class/hwmon/hwmon2/fan1_input", "1050");
    let t = thermal::ThermalSampler::new(&r).sample(Some(42.0));
    assert_eq!(t.cpu_c, Some(52.125));
    assert_eq!(t.nvme_c, Some(29.0));
    assert_eq!(t.gpu_c, Some(42.0));
    assert_eq!(t.fans_rpm, vec![1050]);
}

#[test]
fn amdgpu_sysfs() {
    let r = fake_root("gpu");
    let d = "/sys/class/drm/card0/device";
    w(&r, &format!("{d}/vendor"), "0x1002");
    w(&r, &format!("{d}/device"), "0x15bf");
    w(&r, &format!("{d}/gpu_busy_percent"), "12");
    w(&r, &format!("{d}/mem_info_vram_used"), "1288490188");
    w(&r, &format!("{d}/mem_info_vram_total"), "4294967296");
    w(&r, &format!("{d}/pp_dpm_sclk"), "0: 800Mhz\n1: 2700Mhz *\n");
    w(&r, &format!("{d}/hwmon/hwmon3/temp1_input"), "42000");
    w(&r, &format!("{d}/hwmon/hwmon3/temp1_label"), "edge");
    w(&r, &format!("{d}/hwmon/hwmon3/power1_average"), "9600000");
    w(&r, "/usr/share/hwdata/pci.ids", "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n\t15bf  Phoenix1 [Radeon 780M]\n");
    let g = gpu::GpuSampler::new(&r).sample(1.5).unwrap();
    assert_eq!(g.name, "AMD Radeon 780M");
    assert_eq!(g.busy_pct, Some(12.0));
    assert_eq!(g.clock_mhz, Some(2700.0));
    assert_eq!(g.temp_c, Some(42.0));
    assert_eq!(g.power_w, Some(9.6));
}

#[test]
fn disk_net_and_procs_parsers() {
    assert_eq!(
        disk::root_mount("/dev/nvme0n1p2 / btrfs rw 0 0\ntmpfs /tmp tmpfs rw 0 0\n"),
        Some(("btrfs".into(), "/dev/nvme0n1p2".into()))
    );
    let ds = "259 0 nvme0n1 10 0 200 0 5 0 100 0 0 0 0\n259 1 nvme0n1p1 10 0 200 0 5 0 100 0 0 0 0\n7 0 loop0 1 0 50 0 0 0 0 0 0 0 0\n";
    assert_eq!(disk::parse_diskstats(ds, None), (200 * 512, 100 * 512));
    let nd = "Inter-|\n face |\n    lo: 10 1 0 0 0 0 0 0 10 1 0 0 0 0 0 0\nwlp1s0: 5000 9 0 0 0 0 0 0 700 4 0 0 0 0 0 0\n";
    let rows = net::parse_net_dev(nd);
    assert_eq!(rows[1], ("wlp1s0".into(), 5000, 700));
    assert_eq!(net::default_iface("Iface\tDestination\nwlp1s0\t00000000\t0102A8C0\n"), Some("wlp1s0".into()));
    assert_eq!(nl80211::proc_wireless("Inter-|\n face |\nwlp1s0: 0000   57.  -53.  -256        0      0\n", "wlp1s0"), Some(-53));

    let st = procs::parse_stat("2316 (tracker-miner-f) S 1 2316 2316 0 -1 4194560 100 0 0 0 900 300 0 0 20 0 14 0 5000 100000 2000 18446744073709551615").unwrap();
    assert_eq!(st.comm, "tracker-miner-f");
    assert_eq!(st.ticks, 1200);
    assert_eq!(st.threads, 14);
    assert_eq!(st.rss_pages, 2000);
    assert_eq!(
        procs::app_id_from_cgroup("0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-gnome-org.gnome.Nautilus-2221.scope\n"),
        Some("org.gnome.Nautilus".into())
    );
    assert_eq!(
        procs::app_id_from_cgroup("0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-flatpak-com.spotify.Client-5512.scope\n"),
        Some("com.spotify.Client".into())
    );
    assert_eq!(procs::app_id_from_cgroup("0::/user.slice/user-1000.slice/session-2.scope\n"), None);
    assert_eq!(procs::desktop_name("[Desktop Entry]\nName=Files\n[Desktop Action new]\nName=New\n"), Some("Files".into()));
    let s = procs::parse_status("Name:\tx\nUid:\t1000\t1000\t1000\t1000\nVmRSS:\t  2048 kB\nThreads:\t7\n");
    assert_eq!((s.threads, s.rss_bytes, s.uid), (Some(7), Some(2048 * 1024), Some(1000)));
}

#[test]
fn nl80211_station_attributes() {
    // STA_INFO with SIGNAL (-53) and TX_BITRATE { BITRATE32 = 12010 }
    let mut info = Vec::new();
    info.extend_from_slice(&5u16.to_ne_bytes());
    info.extend_from_slice(&7u16.to_ne_bytes());
    info.extend_from_slice(&[(-53i8) as u8, 0, 0, 0]);
    let mut rate = Vec::new();
    rate.extend_from_slice(&8u16.to_ne_bytes());
    rate.extend_from_slice(&5u16.to_ne_bytes());
    rate.extend_from_slice(&12010u32.to_ne_bytes());
    info.extend_from_slice(&((4 + rate.len()) as u16).to_ne_bytes());
    info.extend_from_slice(&8u16.to_ne_bytes());
    info.extend_from_slice(&rate);
    let st = nl80211::parse_sta_info(&info);
    assert_eq!(st.signal_dbm, Some(-53));
    assert_eq!(st.tx_mbps, Some(1201.0));
}

#[test]
fn real_system_smoke() {
    let mut s = Sampler::new("/");
    s.sample();
    std::thread::sleep(std::time::Duration::from_millis(300));
    let snap = s.sample();
    assert!(snap.cpu.valid, "needs /proc/stat");
    assert!(snap.mem.is_some(), "needs /proc/meminfo");
    assert!(!snap.apps.is_empty(), "needs /proc/<pid>");
    let total: f64 = snap.apps.iter().map(|a| a.cpu).sum();
    assert!(total <= 100.5, "{total}");
}

#[test]
fn ending_an_app_sends_sigterm() {
    use std::os::unix::process::ExitStatusExt;
    let mut child = std::process::Command::new("sleep").arg("30").spawn().unwrap();
    let pid = child.id() as i32;
    assert_eq!(procs::terminate(&[pid]), 1);
    let status = child.wait().unwrap();
    assert_eq!(status.signal(), Some(libc_sigterm()));
    // pid 1 and below are never signalled
    assert_eq!(procs::terminate(&[0, 1, -1]), 0);
}

fn libc_sigterm() -> i32 {
    15
}

#[test]
fn launch_info_reads_argv_and_cwd() {
    let mut child = std::process::Command::new("sleep").arg("30").current_dir("/tmp").spawn().unwrap();
    let pid = child.id() as i32;
    let l = procs::launch_info(std::path::Path::new("/"), "sleep", &[pid]).unwrap();
    assert_eq!(l.argv, vec!["sleep".to_string(), "30".to_string()]);
    assert_eq!(l.cwd.as_deref(), Some(std::path::Path::new("/tmp")));
    procs::terminate(&[pid]);
    child.wait().unwrap();
}
