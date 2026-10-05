/*
 * Telemetry for Windows 9x with clean fallbacks for NT hosts.
 *
 * 9x path:  HKEY_DYN_DATA\PerfStats (StartStat / StatData / StopStat),
 *           GlobalMemoryStatus, RSRC32.DLL _MyGetFreeSystemResources32@4,
 *           Toolhelp32 process list.
 * NT path:  NtQuerySystemInformation (CPU times, process list with CPU and
 *           working set), psapi GetPerformanceInfo, iphlpapi GetIfTable.
 * Every API beyond Win95 kernel32/user32/gdi32/advapi32 is loaded with
 * GetProcAddress, so one exe runs on 98 SE, ME, NT4, 2000, XP and Wine.
 */
#ifndef PULSE_TELEMETRY_H
#define PULSE_TELEMETRY_H

#include <windows.h>

#define TM_MAX_APPS 48
#define TM_NAME 64

/* Where a reading came from. */
enum { SRC_NONE = 0, SRC_LIVE = 1, SRC_SIM = 2 };

typedef struct {
    char name[TM_NAME];     /* display name, UTF-8 */
    char exe[TM_NAME];      /* image file name, UTF-8, used as the key */
    int procs;              /* processes with this image name */
    int threads;
    double cpu;             /* percent of total CPU, -1 when unknown */
    double mem;             /* working set bytes, -1 when unknown */
    DWORD pid;              /* first process id */
    DWORD pids[8];
    int npids;
} TmApp;

typedef struct {
    /* What this host offers */
    int is_nt;
    int has_dyn;            /* HKEY_DYN_DATA PerfStats answered */
    int has_rsrc;           /* RSRC32 thunk found */
    int has_ntq;            /* NtQuerySystemInformation works */
    int has_th32;           /* Toolhelp32 */
    int has_psapi;
    int has_iphlp;
    int has_dun;            /* Dial-Up Adapter counters */
    int has_power;

    /* CPU */
    int ncpu;
    char cpu_model[64];
    double cpu, cpu_user, cpu_sys;
    int cpu_src, split_src;

    /* Memory (bytes) */
    DWORD mem_load;
    double phys_total, phys_avail, page_total, page_avail;
    double kernel, cache, swap_used, swap_total;
    int memdetail_src, swap_is_commit;
    int res_free[3];        /* system, GDI, USER percent free */
    int res_src;

    /* Disk */
    char drive[4];
    char fs[16];
    double disk_total, disk_free;
    double disk_read, disk_write;   /* bytes per second */
    int disk_src, diskio_src;

    /* Power */
    int ac_online, batt_pct, batt_secs, has_batt;

    /* Network */
    double net_down, net_up;        /* bytes per second */
    double link_bps;
    int net_src;

    /* GPU (name is real; load and temperature are simulated) */
    char gpu_name[128];
    double gpu_load, gpu_temp, cpu_temp;

    /* Processes */
    TmApp apps[TM_MAX_APPS];
    int napps;
    int proc_src;           /* SRC_LIVE when the list itself is real */
    int proc_cpu;           /* per-process CPU available */
} Telemetry;

void tm_init(Telemetry *t);
void tm_sample(Telemetry *t, double seconds_since_last);
void tm_shutdown(Telemetry *t);
int  tm_end_process(Telemetry *t, const TmApp *a);
/* Full image path seen for this image name (ANSI), for relaunching. */
int  tm_app_path(const char *exe, char *out, int cap);

#endif
