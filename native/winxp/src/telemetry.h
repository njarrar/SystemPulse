/* Pulse telemetry for Windows XP. Every reading here comes from an XP API
   unless its field comment says "simulated". */
#ifndef PULSE_TELEMETRY_H
#define PULSE_TELEMETRY_H

#include <windows.h>

#define SPARK_N 60      /* 1 min at 1.5 s */
#define HIST_N 121      /* 10 min at 5 s */
#define MAX_GROUPS 256
#define MAX_TRACK 48    /* per-app 10-minute histories */

typedef struct {
    WCHAR name[64];         /* exe name, e.g. svchost.exe */
    int procs, threads;
    DWORD pid;              /* busiest process in the group */
    double cpu;             /* % of total CPU */
    double user_share;      /* 0..100, user part of the group's CPU time */
    double mem_mb;          /* working set */
    int gpu;                /* always 0: XP has no per-process GPU counters */
    DWORD hog_since;        /* GetTickCount when cpu went above 50%, 0 if not */
    int below;              /* samples in a row under 50% */
    int hog;                /* above 50% of total CPU for over 2 min */
    int sim;                /* simulated row from the "Simulate CPU hog" switch */
    int system;             /* service or system process */
} AppGroup;

typedef struct {
    float v[HIST_N];
    int n;
} Hist;

typedef struct {
    /* CPU: NtQuerySystemInformation(SystemProcessorPerformanceInformation) */
    int ncpu;
    double cpu, user, sys;          /* % of total */
    double core[2];                 /* Core 0 / Core 1 */
    /* Memory: GlobalMemoryStatusEx + GetPerformanceInfo */
    double mem_pct, mem_total_gb, mem_used_gb, mem_kernel_gb, mem_cache_gb, mem_free_gb;
    double commit_gb, commit_limit_gb, pagefile_gb;
    /* Disk: GetDiskFreeSpaceExW + IOCTL_DISK_PERFORMANCE */
    WCHAR drive[4];                 /* "C:" */
    WCHAR fs[16];                   /* NTFS */
    double disk_total_gb, disk_free_gb, disk_pct;
    double disk_read_mbs, disk_write_mbs;
    int disk_io_source;             /* 0 none, 1 IOCTL_DISK_PERFORMANCE, 2 process I/O counters */
    /* Network: GetIfTable */
    int net_ok, net_type;           /* MIB_IF_TYPE_* */
    double net_speed_mbps;
    double net_down_kbs, net_up_kbs;
    double net_today_down_mb, net_today_up_mb;
    /* Battery: GetSystemPowerStatus. A PC without a battery reports
       no_batt = 1, unless the Charging demo switch is on, which shows a
       demo battery (batt_sim = 1). */
    int no_batt, batt_sim, charging, batt_pct, batt_min_left;
    double watts;                   /* simulated: XP has no power meter API */
    /* Thermal and GPU load: simulated, XP has no standard API for either */
    double temp_c, gpu_temp_c, gpu_pct;
    WCHAR gpu_name[128];            /* EnumDisplayDevicesW (real) */
    WCHAR gpu_vram[32];             /* registry HardwareInformation.MemorySize (real when present) */
    /* Processes: Toolhelp32 + GetProcessTimes + GetProcessMemoryInfo */
    AppGroup groups[MAX_GROUPS];
    int ngroups;
    int process_count, thread_count;

    /* 1-minute sparklines */
    float sp_cpu[SPARK_N], sp_mem[SPARK_N], sp_nrg[SPARK_N], sp_thm[SPARK_N], sp_gpu[SPARK_N];
    int sp_n;
    /* 10-minute histories */
    Hist h_cpu, h_mem, h_nrg, h_thm, h_gpu, h_rd, h_wr, h_dn, h_up;
    WCHAR track_name[MAX_TRACK][64];
    Hist track[MAX_TRACK];
    int ntrack;
} Telemetry;

void tel_init(Telemetry *t);
void tel_sample(Telemetry *t, int sim_hog, int sim_charging);
void tel_seed_history(Telemetry *t);
Hist *tel_app_hist(Telemetry *t, const WCHAR *name);
/* Ends every process in the group. Saves image paths for restore. Returns
   the number of processes ended. */
int tel_end_group(Telemetry *t, const WCHAR *name);
int tel_restore(void);
int tel_ended_count(void);

#endif
