/* Pulse telemetry for Windows XP SP2/SP3. */
#include "telemetry.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <winioctl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ---- NT native API (loaded with GetProcAddress) ------------------------ */
typedef LONG (WINAPI *NtQSI_t)(ULONG, PVOID, ULONG, PULONG);
typedef struct {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, Reserved1[2];
    ULONG Reserved2;
} SPPI;
#define SystemProcessorPerformanceInformation 8
#define SystemProcessInformation 5
/* SYSTEM_PROCESS_INFORMATION, 32-bit layout, fields up to the VM counters */
typedef struct {
    ULONG NextEntryOffset, NumberOfThreads;
    LARGE_INTEGER Reserved[3];
    LARGE_INTEGER CreateTime, UserTime, KernelTime;
    USHORT NameLength, NameMax;
    WCHAR *ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId, InheritedFromUniqueProcessId;
    ULONG HandleCount, SessionId;
    ULONG_PTR PageDirectoryBase;
    SIZE_T PeakVirtualSize, VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize;
} SPROC;

typedef BOOL (WINAPI *GetPerfInfo_t)(PPERFORMANCE_INFORMATION, DWORD);

static NtQSI_t pNtQSI;
static GetPerfInfo_t pGetPerfInfo;
static SPPI g_prev_cpu[64];
static int g_have_prev_cpu;

typedef struct {
    DWORD pid;
    HANDLE h;
    ULONGLONG last_k, last_u, last_io_r, last_io_w;
    int seen, have;
    double cpu, user_share;
    double ws_mb;
    WCHAR name[64];
} ProcSlot;

#define MAX_PROC 1024
static ProcSlot g_proc[MAX_PROC];
static int g_nproc;
static DWORD g_last_tick, g_last_hist_tick;
static ULONGLONG g_disk_r, g_disk_w, g_net_in, g_net_out;
static int g_have_disk, g_have_net, g_net_index = -1;
static double g_acc[9];
static int g_acc_n;
static unsigned g_rng = 12345;

/* ended apps, for "Restore ended apps" */
static WCHAR g_ended_path[32][MAX_PATH];
static WCHAR g_ended_name[32][64];
static int g_nended;

static double frand(void) { g_rng = g_rng * 1103515245u + 12345u; return ((g_rng >> 8) & 0xFFFF) / 65535.0; }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static ULONGLONG ft64(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static void push_spark(float *a, int *n, float v, int cap) {
    if (*n < cap) { a[(*n)++] = v; return; }
    memmove(a, a + 1, (cap - 1) * sizeof(float));
    a[cap - 1] = v;
}
static void push_hist(Hist *h, float v) { push_spark(h->v, &h->n, v, HIST_N); }

static const WCHAR *SYSTEM_NAMES[] = {
    L"system", L"smss.exe", L"csrss.exe", L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe",
    L"spoolsv.exe", L"alg.exe", L"wuauclt.exe", L"winedevice.exe", L"plugplay.exe", L"rpcss.exe",
    L"explorer.exe", L"wineboot.exe", L"conhost.exe", L"start.exe", L"tabtip.exe", 0
};
static int is_system_name(const WCHAR *n) {
    int k;
    for (k = 0; SYSTEM_NAMES[k]; k++) if (!_wcsicmp(n, SYSTEM_NAMES[k])) return 1;
    return 0;
}

/* ---- static facts ------------------------------------------------------ */
static void read_gpu(Telemetry *t) {
    DISPLAY_DEVICEW dd;
    DWORD i;
    t->gpu_name[0] = 0;
    t->gpu_vram[0] = 0;
    for (i = 0; ; i++) {
        memset(&dd, 0, sizeof dd);
        dd.cb = sizeof dd;
        if (!EnumDisplayDevicesW(NULL, i, &dd, 0)) break;
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE || !t->gpu_name[0]) {
            lstrcpynW(t->gpu_name, dd.DeviceString, 128);
            /* DeviceKey = \Registry\Machine\System\...; read MemorySize */
            if (!_wcsnicmp(dd.DeviceKey, L"\\Registry\\Machine\\", 18)) {
                HKEY k;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, dd.DeviceKey + 18, 0, KEY_READ, &k) == ERROR_SUCCESS) {
                    DWORD mem = 0, sz = sizeof mem, type = 0;
                    if (RegQueryValueExW(k, L"HardwareInformation.MemorySize", 0, &type, (BYTE *)&mem, &sz) == ERROR_SUCCESS && mem)
                        _snwprintf(t->gpu_vram, 32, L"%u MB", (unsigned)(mem >> 20));
                    RegCloseKey(k);
                }
            }
            if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) break;
        }
    }
}

void tel_init(Telemetry *t) {
    SYSTEM_INFO si;
    WCHAR sysdir[MAX_PATH];
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    HMODULE ps = LoadLibraryW(L"psapi.dll");
    memset(t, 0, sizeof *t);
    pNtQSI = nt ? (NtQSI_t)GetProcAddress(nt, "NtQuerySystemInformation") : 0;
    pGetPerfInfo = ps ? (GetPerfInfo_t)GetProcAddress(ps, "GetPerformanceInfo") : 0;
    GetSystemInfo(&si);
    t->ncpu = (int)si.dwNumberOfProcessors;
    if (t->ncpu < 1) t->ncpu = 1;
    if (t->ncpu > 64) t->ncpu = 64;
    GetSystemDirectoryW(sysdir, MAX_PATH);
    t->drive[0] = sysdir[0] ? sysdir[0] : L'C';
    t->drive[1] = L':';
    t->drive[2] = 0;
    {
        WCHAR root[4] = { t->drive[0], L':', L'\\', 0 };
        if (!GetVolumeInformationW(root, 0, 0, 0, 0, 0, t->fs, 16)) lstrcpyW(t->fs, L"NTFS");
    }
    read_gpu(t);
    t->temp_c = 33; t->gpu_temp_c = 46; t->gpu_pct = 12; t->watts = 9.4;
    g_rng = GetTickCount() | 1;
}

/* ---- CPU ----------------------------------------------------------------- */
static void sample_cpu(Telemetry *t) {
    SPPI cur[64];
    ULONG got = 0;
    int k, n = t->ncpu;
    double tot_idle = 0, tot_k = 0, tot_u = 0;
    if (!pNtQSI || pNtQSI(SystemProcessorPerformanceInformation, cur, sizeof(SPPI) * n, &got) != 0) return;
    if (got / sizeof(SPPI) < (ULONG)n) n = (int)(got / sizeof(SPPI));
    if (g_have_prev_cpu) {
        for (k = 0; k < n; k++) {
            double di = (double)(cur[k].IdleTime.QuadPart - g_prev_cpu[k].IdleTime.QuadPart);
            double dk = (double)(cur[k].KernelTime.QuadPart - g_prev_cpu[k].KernelTime.QuadPart);
            double du = (double)(cur[k].UserTime.QuadPart - g_prev_cpu[k].UserTime.QuadPart);
            double tot = dk + du;
            if (k < 2) t->core[k] = tot > 0 ? clampd(100.0 * (1.0 - di / tot), 0, 100) : 0;
            tot_idle += di; tot_k += dk; tot_u += du;
        }
        if (n == 1) t->core[1] = t->core[0];
        if (tot_k + tot_u > 0) {
            double tot = tot_k + tot_u;
            t->cpu = clampd(100.0 * (1.0 - tot_idle / tot), 0, 100);
            t->user = clampd(100.0 * tot_u / tot, 0, 100);
            t->sys = clampd(t->cpu - t->user, 0, 100);
        }
    }
    memcpy(g_prev_cpu, cur, sizeof(SPPI) * n);
    g_have_prev_cpu = 1;
}

/* ---- memory -------------------------------------------------------------- */
static void sample_mem(Telemetry *t) {
    MEMORYSTATUSEX ms;
    const double GB = 1024.0 * 1024.0 * 1024.0;
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms)) return;
    t->mem_total_gb = ms.ullTotalPhys / GB;
    t->mem_free_gb = ms.ullAvailPhys / GB;
    t->mem_used_gb = t->mem_total_gb - t->mem_free_gb;
    t->mem_pct = t->mem_total_gb > 0 ? 100.0 * t->mem_used_gb / t->mem_total_gb : 0;
    t->commit_gb = (ms.ullTotalPageFile - ms.ullAvailPageFile) / GB;
    t->commit_limit_gb = ms.ullTotalPageFile / GB;
    if (pGetPerfInfo) {
        PERFORMANCE_INFORMATION pi;
        memset(&pi, 0, sizeof pi);
        pi.cb = sizeof pi;
        if (pGetPerfInfo(&pi, sizeof pi)) {
            double pg = (double)pi.PageSize;
            t->mem_kernel_gb = pi.KernelTotal * pg / GB;
            t->mem_cache_gb = pi.SystemCache * pg / GB;
            t->commit_gb = pi.CommitTotal * pg / GB;
            t->commit_limit_gb = pi.CommitLimit * pg / GB;
            t->process_count = (int)pi.ProcessCount;
            t->thread_count = (int)pi.ThreadCount;
        }
    }
    /* keep the three segments inside "used" */
    if (t->mem_kernel_gb + t->mem_cache_gb > t->mem_used_gb) {
        double s = t->mem_used_gb / (t->mem_kernel_gb + t->mem_cache_gb + 1e-9);
        t->mem_kernel_gb *= s * 0.9; t->mem_cache_gb *= s * 0.9;
    }
    t->pagefile_gb = t->commit_limit_gb - t->mem_total_gb;
    if (t->pagefile_gb < 0) t->pagefile_gb = 0;
}

/* ---- disk ---------------------------------------------------------------- */
static void sample_disk(Telemetry *t, double dt, ULONGLONG proc_io_r, ULONGLONG proc_io_w) {
    WCHAR root[4] = { t->drive[0], L':', L'\\', 0 };
    WCHAR dev[16];
    ULARGE_INTEGER avail, total, freeb;
    HANDLE h;
    DISK_PERFORMANCE dp;
    DWORD ret = 0;
    int ok = 0;
    if (GetDiskFreeSpaceExW(root, &avail, &total, &freeb)) {
        t->disk_total_gb = total.QuadPart / 1073741824.0;
        t->disk_free_gb = freeb.QuadPart / 1073741824.0;
        t->disk_pct = t->disk_total_gb > 0 ? 100.0 * (1.0 - t->disk_free_gb / t->disk_total_gb) : 0;
    }
    _snwprintf(dev, 16, L"\\\\.\\%c:", t->drive[0]);
    h = CreateFileW(dev, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) h = CreateFileW(L"\\\\.\\PhysicalDrive0", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (h != INVALID_HANDLE_VALUE) {
        memset(&dp, 0, sizeof dp);
        ok = DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, 0, 0, &dp, sizeof dp, &ret, 0) && ret >= sizeof dp - sizeof dp.StorageManagerName;
        CloseHandle(h);
    }
    if (ok) {
        ULONGLONG r = (ULONGLONG)dp.BytesRead.QuadPart, w = (ULONGLONG)dp.BytesWritten.QuadPart;
        if (g_have_disk == 1 && dt > 0) {
            t->disk_read_mbs = (r - g_disk_r) / 1048576.0 / dt;
            t->disk_write_mbs = (w - g_disk_w) / 1048576.0 / dt;
        }
        g_disk_r = r; g_disk_w = w; g_have_disk = 1;
        t->disk_io_source = 1;
    } else {
        /* Fallback: sum of per-process read and write transfer counts. */
        if (g_have_disk == 2 && dt > 0 && proc_io_r >= g_disk_r && proc_io_w >= g_disk_w) {
            t->disk_read_mbs = (proc_io_r - g_disk_r) / 1048576.0 / dt;
            t->disk_write_mbs = (proc_io_w - g_disk_w) / 1048576.0 / dt;
        }
        g_disk_r = proc_io_r; g_disk_w = proc_io_w; g_have_disk = 2;
        t->disk_io_source = 2;
    }
    if (t->disk_read_mbs < 0) t->disk_read_mbs = 0;
    if (t->disk_write_mbs < 0) t->disk_write_mbs = 0;
}

/* ---- network --------------------------------------------------------------- */
static void sample_net(Telemetry *t, double dt) {
    static BYTE buf[32768];
    MIB_IFTABLE *tab = (MIB_IFTABLE *)buf;
    ULONG sz = sizeof buf;
    DWORD k;
    int best = -1;
    double best_bytes = -1;
    if (GetIfTable(tab, &sz, FALSE) != NO_ERROR) { t->net_ok = 0; return; }
    for (k = 0; k < tab->dwNumEntries; k++) {
        MIB_IFROW *r = &tab->table[k];
        double bytes;
        if (r->dwType == MIB_IF_TYPE_LOOPBACK) continue;
        if (r->dwOperStatus < MIB_IF_OPER_STATUS_CONNECTED) continue;
        bytes = (double)r->dwInOctets + r->dwOutOctets;
        if (bytes > best_bytes) { best_bytes = bytes; best = (int)k; }
    }
    if (best < 0) { t->net_ok = 0; return; }
    {
        MIB_IFROW *r = &tab->table[best];
        t->net_ok = 1;
        t->net_type = (int)r->dwType;
        t->net_speed_mbps = r->dwSpeed / 1e6;
        if (g_have_net && g_net_index == (int)r->dwIndex && dt > 0) {
            /* 32-bit counters wrap; unsigned subtraction handles one wrap */
            DWORD din = r->dwInOctets - (DWORD)g_net_in, dout = r->dwOutOctets - (DWORD)g_net_out;
            t->net_down_kbs = din / 1024.0 / dt;
            t->net_up_kbs = dout / 1024.0 / dt;
            t->net_today_down_mb += din / 1048576.0;
            t->net_today_up_mb += dout / 1048576.0;
        }
        g_net_in = r->dwInOctets; g_net_out = r->dwOutOctets;
        g_net_index = (int)r->dwIndex;
        g_have_net = 1;
    }
}

/* ---- processes ------------------------------------------------------------- */
static ProcSlot *slot_for(DWORD pid) {
    int k;
    for (k = 0; k < g_nproc; k++) if (g_proc[k].pid == pid) return &g_proc[k];
    if (g_nproc >= MAX_PROC) return 0;
    memset(&g_proc[g_nproc], 0, sizeof(ProcSlot));
    g_proc[g_nproc].pid = pid;
    return &g_proc[g_nproc++];
}

/* Per-process CPU times and working sets for every process in one call.
   GetProcessTimes needs a handle, which protected processes refuse. */
static BYTE *g_spi;
static ULONG g_spi_size;
static int spi_times(DWORD pid, ULONGLONG *k, ULONGLONG *u, double *ws_mb) {
    BYTE *p = g_spi;
    if (!p) return 0;
    for (;;) {
        SPROC *e = (SPROC *)p;
        if ((DWORD)(ULONG_PTR)e->UniqueProcessId == pid) {
            *k = (ULONGLONG)e->KernelTime.QuadPart; *u = (ULONGLONG)e->UserTime.QuadPart;
            *ws_mb = e->WorkingSetSize / 1048576.0;
            return 1;
        }
        if (!e->NextEntryOffset) return 0;
        p += e->NextEntryOffset;
    }
}
static void spi_refresh(void) {
    ULONG need = 0;
    LONG st;
    if (!pNtQSI) return;
    for (int tries = 0; tries < 4; tries++) {
        if (!g_spi) { g_spi_size = g_spi_size ? g_spi_size : 65536; g_spi = (BYTE *)malloc(g_spi_size); if (!g_spi) return; }
        st = pNtQSI(SystemProcessInformation, g_spi, g_spi_size, &need);
        if (st == 0) return;
        free(g_spi); g_spi = 0;
        g_spi_size = (need > g_spi_size ? need : g_spi_size * 2) + 16384;
    }
}

static void sample_procs(Telemetry *t, double dt, ULONGLONG *io_r, ULONGLONG *io_w, int sim_hog) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0);
    PROCESSENTRY32W pe;
    THREADENTRY32 te;
    int k, j;
    DWORD now = GetTickCount();
    AppGroup old[MAX_GROUPS];
    int nold = t->ngroups;
    double wall100 = dt * 1e7 * t->ncpu;  /* 100 ns units across all CPUs */
    *io_r = *io_w = 0;
    memcpy(old, t->groups, sizeof(AppGroup) * nold);
    if (snap == INVALID_HANDLE_VALUE) return;
    spi_refresh();
    for (k = 0; k < g_nproc; k++) g_proc[k].seen = 0;
    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) do {
        ProcSlot *s;
        if (pe.th32ProcessID == 0) continue;
        s = slot_for(pe.th32ProcessID);
        if (!s) continue;
        s->seen = 1;
        lstrcpynW(s->name, pe.szExeFile, 64);
        if (!s->h) s->h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        {
            FILETIME c, e, kt, ut;
            ULONGLONG kk = 0, uu = 0;
            double ws = 0;
            int have = spi_times(pe.th32ProcessID, &kk, &uu, &ws);
            if (have) s->ws_mb = ws;
            else if (s->h && GetProcessTimes(s->h, &c, &e, &kt, &ut)) { kk = ft64(kt); uu = ft64(ut); have = 1; }
            if (have) {
                if (s->have && wall100 > 0) {
                    double dk = (double)(kk - s->last_k), du = (double)(uu - s->last_u);
                    s->cpu = clampd(100.0 * (dk + du) / wall100, 0, 100);
                    s->user_share = dk + du > 0 ? 100.0 * du / (dk + du) : s->user_share;
                }
                s->last_k = kk; s->last_u = uu; s->have = 1;
            }
        }
        if (s->h) {
            PROCESS_MEMORY_COUNTERS pmc;
            IO_COUNTERS io;
            pmc.cb = sizeof pmc;
            if (!g_spi && GetProcessMemoryInfo(s->h, &pmc, sizeof pmc)) s->ws_mb = pmc.WorkingSetSize / 1048576.0;
            if (GetProcessIoCounters(s->h, &io)) { *io_r += io.ReadTransferCount; *io_w += io.WriteTransferCount; }
        }
    } while (Process32NextW(snap, &pe));

    /* drop exited processes */
    for (k = 0; k < g_nproc; ) {
        if (!g_proc[k].seen) {
            if (g_proc[k].h) CloseHandle(g_proc[k].h);
            g_proc[k] = g_proc[--g_nproc];
        } else k++;
    }

    /* group by exe name */
    t->ngroups = 0;
    for (k = 0; k < g_nproc; k++) {
        ProcSlot *s = &g_proc[k];
        AppGroup *g = 0;
        for (j = 0; j < t->ngroups; j++) if (!_wcsicmp(t->groups[j].name, s->name)) { g = &t->groups[j]; break; }
        if (!g) {
            if (t->ngroups >= MAX_GROUPS) continue;
            g = &t->groups[t->ngroups++];
            memset(g, 0, sizeof *g);
            lstrcpynW(g->name, s->name, 64);
            g->pid = s->pid;
            g->system = is_system_name(s->name);
        }
        if (s->cpu > 0 && (g->procs == 0 || s->cpu >= g->cpu)) g->pid = s->pid;
        g->user_share = (g->user_share * g->cpu + s->user_share * s->cpu) / (g->cpu + s->cpu + 1e-9);
        g->cpu += s->cpu;
        g->mem_mb += s->ws_mb;
        g->procs++;
    }
    /* thread counts from the thread walk */
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) do {
        for (k = 0; k < g_nproc; k++) if (g_proc[k].pid == te.th32OwnerProcessID) {
            for (j = 0; j < t->ngroups; j++) if (!_wcsicmp(t->groups[j].name, g_proc[k].name)) { t->groups[j].threads++; break; }
            break;
        }
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);

    /* hog tracking: > 50% of total CPU for over 2 minutes */
    for (j = 0; j < t->ngroups; j++) {
        AppGroup *g = &t->groups[j];
        for (k = 0; k < nold; k++) if (!old[k].sim && !_wcsicmp(old[k].name, g->name)) { g->hog_since = old[k].hog_since; g->below = old[k].below; break; }
        /* one short dip (a busy disk, a delayed timer) does not restart the
           2-minute clock; three samples in a row under 50% do */
        if (g->cpu > 50) { g->below = 0; if (!g->hog_since) g->hog_since = now ? now : 1; }
        else if (++g->below >= 3) g->hog_since = 0;
        g->hog = g->hog_since && now - g->hog_since >= 120000;
    }
    /* "Simulate CPU hog": one synthetic row, clearly flagged as simulated */
    if (sim_hog && t->ngroups < MAX_GROUPS) {
        AppGroup *g = &t->groups[t->ngroups++];
        double prev = 54.1;
        for (k = 0; k < nold; k++) if (old[k].sim) prev = old[k].cpu;
        memset(g, 0, sizeof *g);
        lstrcpyW(g->name, L"demo-load.exe");
        g->procs = 12; g->threads = 71; g->pid = 1048;
        g->cpu = clampd(prev + (frand() - 0.5) * 3 + (54.1 - prev) * 0.3, 51, 60);
        g->user_share = 66;
        g->mem_mb = 94;
        g->hog = 1; g->sim = 1; g->system = 1;
        g->hog_since = now - 130000;
    }
}

static Hist *find_track(Telemetry *t, const WCHAR *name) {
    int k;
    for (k = 0; k < t->ntrack; k++) if (!_wcsicmp(t->track_name[k], name)) return &t->track[k];
    return 0;
}
Hist *tel_app_hist(Telemetry *t, const WCHAR *name) { return find_track(t, name); }

static void track_apps(Telemetry *t) {
    int k, j;
    /* drop tracks for groups that are gone */
    for (k = 0; k < t->ntrack; ) {
        int alive = 0;
        for (j = 0; j < t->ngroups; j++) if (!_wcsicmp(t->groups[j].name, t->track_name[k])) { alive = 1; break; }
        if (!alive) {
            t->ntrack--;
            if (k != t->ntrack) { memcpy(t->track_name[k], t->track_name[t->ntrack], sizeof t->track_name[0]); t->track[k] = t->track[t->ntrack]; }
        } else k++;
    }
    for (j = 0; j < t->ngroups; j++) {
        Hist *h = find_track(t, t->groups[j].name);
        if (!h && t->ntrack < MAX_TRACK && (t->groups[j].cpu > 0.05 || t->groups[j].sim)) {
            lstrcpynW(t->track_name[t->ntrack], t->groups[j].name, 64);
            memset(&t->track[t->ntrack], 0, sizeof(Hist));
            h = &t->track[t->ntrack++];
        }
        if (h) push_hist(h, (float)t->groups[j].cpu);
    }
}

/* ---- simulated sensors --------------------------------------------------- */
static void sample_sim(Telemetry *t, int sim_charging) {
    SYSTEM_POWER_STATUS ps;
    double target_t = 31 + t->cpu * 0.32, target_w = 7.6 + t->cpu * 0.11;
    t->temp_c += (target_t - t->temp_c) * 0.25 + (frand() - 0.5) * 0.6;
    t->watts += (target_w - t->watts) * 0.3 + (frand() - 0.5) * 0.8;
    t->watts = clampd(t->watts, 3, 40);
    t->gpu_pct = clampd(t->gpu_pct + (frand() - 0.5) * 7 + (14 - t->gpu_pct) * 0.3, 2, 60);
    t->gpu_temp_c = clampd(t->gpu_temp_c + (frand() - 0.5) * 0.8 + (44 + t->gpu_pct * 0.3 - t->gpu_temp_c) * 0.2, 35, 90);
    if (GetSystemPowerStatus(&ps) && !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255 && ps.BatteryLifePercent <= 100) {
        t->batt_sim = 0;
        t->no_batt = 0;
        t->batt_pct = ps.BatteryLifePercent;
        t->charging = ps.ACLineStatus == 1;
        t->batt_min_left = ps.BatteryLifeTime != (DWORD)-1 ? (int)(ps.BatteryLifeTime / 60) : -1;
    } else if (sim_charging) {
        t->batt_sim = 1;
        t->no_batt = 0;
        t->batt_pct = 84;
        t->charging = 1;
        t->batt_min_left = -1;
    } else {
        t->batt_sim = 0;
        t->no_batt = 1;
        t->batt_pct = 0;
        t->charging = 0;
        t->batt_min_left = -1;
    }
}

void tel_sample(Telemetry *t, int sim_hog, int sim_charging) {
    DWORD now = GetTickCount();
    double dt = g_last_tick ? (now - g_last_tick) / 1000.0 : 0;
    ULONGLONG io_r, io_w;
    int k;
    g_last_tick = now;
    sample_cpu(t);
    sample_mem(t);
    sample_procs(t, dt, &io_r, &io_w, sim_hog);
    if (sim_hog) {
        /* the simulated hog adds its share to the totals so every card agrees */
        for (k = 0; k < t->ngroups; k++) if (t->groups[k].sim) {
            double add = t->groups[k].cpu;
            t->cpu = clampd(t->cpu + add, 0, 100);
            t->user = clampd(t->user + add * 0.66, 0, 100);
            t->sys = clampd(t->cpu - t->user, 0, 100);
            t->core[0] = clampd(t->core[0] + add * 1.22, 0, 100);
            t->core[1] = clampd(t->core[1] + add * 0.58, 0, 100);
        }
    }
    sample_disk(t, dt, io_r, io_w);
    sample_net(t, dt);
    sample_sim(t, sim_charging);
    if (!dt) return;  /* first call only primes the counters */

    {
        float *arr[5] = { t->sp_cpu, t->sp_mem, t->sp_nrg, t->sp_thm, t->sp_gpu };
        float val[5] = { (float)t->cpu, (float)t->mem_pct, (float)t->watts, (float)t->temp_c, (float)t->gpu_pct };
        int n0 = t->sp_n;
        for (k = 0; k < 5; k++) { t->sp_n = n0; push_spark(arr[k], &t->sp_n, val[k], SPARK_N); }
    }

    /* 10-minute histories: average of the samples in each 5 s slot */
    g_acc[0] += t->cpu; g_acc[1] += t->mem_pct; g_acc[2] += t->watts; g_acc[3] += t->temp_c; g_acc[4] += t->gpu_pct;
    g_acc[5] += t->disk_read_mbs; g_acc[6] += t->disk_write_mbs; g_acc[7] += t->net_down_kbs; g_acc[8] += t->net_up_kbs;
    g_acc_n++;
    if (!g_last_hist_tick || now - g_last_hist_tick >= 5000) {
        double n = g_acc_n;
        push_hist(&t->h_cpu, (float)(g_acc[0] / n)); push_hist(&t->h_mem, (float)(g_acc[1] / n));
        push_hist(&t->h_nrg, (float)(g_acc[2] / n)); push_hist(&t->h_thm, (float)(g_acc[3] / n));
        push_hist(&t->h_gpu, (float)(g_acc[4] / n)); push_hist(&t->h_rd, (float)(g_acc[5] / n));
        push_hist(&t->h_wr, (float)(g_acc[6] / n)); push_hist(&t->h_dn, (float)(g_acc[7] / n));
        push_hist(&t->h_up, (float)(g_acc[8] / n));
        memset(g_acc, 0, sizeof g_acc); g_acc_n = 0;
        track_apps(t);
        g_last_hist_tick = now;
    }
}

/* Fills the 10-minute buffers with a random walk around the current values.
   Only used by --seed-history so screenshots show a full chart. */
void tel_seed_history(Telemetry *t) {
    struct { Hist *h; double base, vol, lo, hi; } s[9] = {
        { &t->h_cpu, t->cpu, 6, 1, 100 }, { &t->h_mem, t->mem_pct, 1.2, 1, 100 }, { &t->h_nrg, t->watts, 1.6, 3, 40 },
        { &t->h_thm, t->temp_c, 0.7, 25, 100 }, { &t->h_gpu, t->gpu_pct, 7, 0, 100 }, { &t->h_rd, t->disk_read_mbs + 2, 3, 0, 500 },
        { &t->h_wr, t->disk_write_mbs + 1, 1.5, 0, 500 }, { &t->h_dn, t->net_down_kbs + 40, 30, 0, 1e6 }, { &t->h_up, t->net_up_kbs + 8, 6, 0, 1e6 } };
    int k, i;
    for (k = 0; k < 9; k++) {
        double v = s[k].base;
        int keep = s[k].h->n;
        float tail[HIST_N];
        memcpy(tail, s[k].h->v, sizeof(float) * keep);
        s[k].h->n = 0;
        for (i = 0; i < HIST_N - keep; i++) {
            v += (frand() - 0.5) * s[k].vol + (s[k].base - v) * 0.12;
            push_hist(s[k].h, (float)clampd(v, s[k].lo, s[k].hi));
        }
        for (i = 0; i < keep; i++) push_hist(s[k].h, tail[i]);
    }
    for (k = 0; k < t->ngroups; k++) {
        Hist *h = find_track(t, t->groups[k].name);
        double base = t->groups[k].cpu, v;
        if (!h && t->ntrack < MAX_TRACK) {
            lstrcpynW(t->track_name[t->ntrack], t->groups[k].name, 64);
            memset(&t->track[t->ntrack], 0, sizeof(Hist));
            h = &t->track[t->ntrack++];
        }
        if (!h) continue;
        h->n = 0;
        v = base;
        for (i = 0; i < HIST_N; i++) {
            v += (frand() - 0.5) * (base * 0.2 + 0.3) + (base - v) * 0.15;
            push_hist(h, (float)clampd(v, 0, 100));
        }
    }
}

/* ---- end and restore ------------------------------------------------------ */
int tel_end_group(Telemetry *t, const WCHAR *name) {
    int k, ended = 0;
    WCHAR path[MAX_PATH] = { 0 };
    (void)t;
    for (k = 0; k < g_nproc; k++) {
        HANDLE h;
        if (_wcsicmp(g_proc[k].name, name)) continue;
        if (g_proc[k].pid == GetCurrentProcessId()) continue;
        if (!path[0] && g_proc[k].h) GetModuleFileNameExW(g_proc[k].h, 0, path, MAX_PATH);
        h = OpenProcess(PROCESS_TERMINATE, FALSE, g_proc[k].pid);
        if (h) { if (TerminateProcess(h, 1)) ended++; CloseHandle(h); }
    }
    if (ended && g_nended < 32) {
        lstrcpynW(g_ended_name[g_nended], name, 64);
        lstrcpynW(g_ended_path[g_nended], path, MAX_PATH);
        g_nended++;
    }
    return ended;
}

int tel_restore(void) {
    int k, started = 0;
    for (k = 0; k < g_nended; k++) {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        if (!g_ended_path[k][0]) continue;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (CreateProcessW(g_ended_path[k], 0, 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            started++;
        }
    }
    g_nended = 0;
    return started;
}
int tel_ended_count(void) { return g_nended; }
