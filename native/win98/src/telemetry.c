/*
 * Telemetry sources. See telemetry.h for the order of preference.
 * C89, no CRT.
 */
#include "telemetry.h"
#include "text.h"

#ifndef HKEY_DYN_DATA
#define HKEY_DYN_DATA ((HKEY)0x80000006UL)
#endif

/* ---- small helpers ------------------------------------------------------ */

static void s_copy(char *d, const char *s, int cap)
{
    int k = 0;
    while (s && s[k] && k < cap - 1) { d[k] = s[k]; k++; }
    d[k] = 0;
}

static int s_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == *b;
}

static const char *base_name(const char *path)
{
    const char *b = path, *p;
    for (p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    return b;
}

static double li(const BYTE *p)
{
    const DWORD *d = (const DWORD *)p;
    return (double)d[1] * 4294967296.0 + (double)d[0];
}

static unsigned long g_seed = 0x2545F491UL;
static double rnd(void)  /* 0..1 */
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (double)((g_seed >> 8) & 0xFFFF) / 65535.0;
}

static double walk(double v, double base, double vol, double lo, double hi)
{
    v += (rnd() - 0.5) * vol + (base - v) * 0.3;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

/* ---- HKEY_DYN_DATA PerfStats -------------------------------------------- */

typedef struct {
    const char *name;
    int started;
    int differentiate;   /* raw value is a running total */
    DWORD last;
    int have_last;
} PerfCounter;

enum { PC_CPU, PC_LOCKED, PC_DISKCACHE, PC_DUN_RX, PC_DUN_TX, PC_DUN_SPEED, PC_FS_READ, PC_FS_WRITE, PC_COUNT };

static PerfCounter g_pc[PC_COUNT] = {
    { "KERNEL\\CPUUsage", 0, 0, 0, 0 },
    { "VMM\\cpgLocked", 0, 0, 0, 0 },
    { "VMM\\cpgDiskcache", 0, 0, 0, 0 },
    { "Dial-Up Adapter\\TotalBytesRecvd", 0, 0, 0, 0 },
    { "Dial-Up Adapter\\TotalBytesXmit", 0, 0, 0, 0 },
    { "Dial-Up Adapter\\ConnectSpeed", 0, 0, 0, 0 },
    { "VFAT\\BReadsSec", 0, 0, 0, 0 },
    { "VFAT\\BWritesSec", 0, 0, 0, 0 }
};

static int perf_query(const char *key, const char *name, DWORD *out)
{
    HKEY k;
    DWORD type = 0, size = sizeof(DWORD), v = 0;
    LONG r;
    if (RegOpenKeyExA(HKEY_DYN_DATA, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    r = RegQueryValueExA(k, name, 0, &type, (BYTE *)&v, &size);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || size < sizeof(DWORD)) return 0;
    if (out) *out = v;
    return 1;
}

static int perf_differentiate(const char *name)
{
    char path[160] = "System\\CurrentControlSet\\Control\\PerfStats\\Enum\\";
    char val[16];
    DWORD size = sizeof(val), type;
    HKEY k;
    int n = 0, res = 0;
    while (path[n]) n++;
    s_copy(path + n, name, (int)sizeof(path) - n);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExA(k, "Differentiate", 0, &type, (BYTE *)val, &size) == ERROR_SUCCESS)
        res = (val[0] == 'T' || val[0] == 't');
    RegCloseKey(k);
    return res;
}

static void perf_start_all(Telemetry *t)
{
    int k;
    for (k = 0; k < PC_COUNT; k++) {
        g_pc[k].started = perf_query("PerfStats\\StartStat", g_pc[k].name, 0);
        if (g_pc[k].started) g_pc[k].differentiate = perf_differentiate(g_pc[k].name);
    }
    t->has_dyn = g_pc[PC_CPU].started && perf_query("PerfStats\\StatData", g_pc[PC_CPU].name, 0);
    t->has_dun = g_pc[PC_DUN_RX].started && g_pc[PC_DUN_TX].started;
}

/* Value now; for running totals, the rate per second since the last call. */
static int perf_value(int idx, double secs, double *out)
{
    PerfCounter *c = &g_pc[idx];
    DWORD raw;
    if (!c->started || !perf_query("PerfStats\\StatData", c->name, &raw)) return 0;
    if (!c->differentiate) { *out = (double)raw; return 1; }
    if (!c->have_last || secs <= 0) { c->last = raw; c->have_last = 1; *out = 0; return 1; }
    *out = (double)(DWORD)(raw - c->last) / secs;
    c->last = raw;
    return 1;
}

static int perf_total_rate(int idx, double secs, double *out)
{
    /* For counters that are running totals even when Differentiate is unset. */
    PerfCounter *c = &g_pc[idx];
    DWORD raw;
    if (!c->started || !perf_query("PerfStats\\StatData", c->name, &raw)) return 0;
    if (!c->have_last || secs <= 0) { c->last = raw; c->have_last = 1; *out = 0; return 1; }
    *out = (double)(DWORD)(raw - c->last) / secs;
    c->last = raw;
    return 1;
}

/* ---- dynamic APIs --------------------------------------------------------- */

typedef LONG (WINAPI *NtQSI_t)(ULONG, PVOID, ULONG, PULONG);
typedef DWORD (WINAPI *Rsrc_t)(UINT);
typedef HANDLE (WINAPI *Snap_t)(DWORD, DWORD);
typedef BOOL (WINAPI *P32_t)(HANDLE, void *);
typedef BOOL (WINAPI *DiskEx_t)(LPCSTR, ULARGE_INTEGER *, ULARGE_INTEGER *, ULARGE_INTEGER *);
typedef BOOL (WINAPI *Power_t)(SYSTEM_POWER_STATUS *);
typedef DWORD (WINAPI *IfTable_t)(void *, ULONG *, BOOL);
typedef BOOL (WINAPI *PerfInfo_t)(void *, DWORD);
typedef DWORD (WINAPI *ModName_t)(HANDLE, HMODULE, LPSTR, DWORD);
typedef BOOL (WINAPI *EnumDD_t)(LPCSTR, DWORD, void *, DWORD);
typedef DWORD (WINAPI *VerSize_t)(LPCSTR, LPDWORD);
typedef BOOL (WINAPI *VerInfo_t)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *VerQuery_t)(LPCVOID, LPCSTR, LPVOID *, PUINT);

static NtQSI_t pNtQSI;
static Rsrc_t pRsrc;
static Snap_t pSnap;
static P32_t pP32First, pP32Next;
static DiskEx_t pDiskEx;
static Power_t pPower;
static IfTable_t pIfTable;
static PerfInfo_t pPerfInfo;
static ModName_t pModName;
static VerSize_t pVerSize;
static VerInfo_t pVerInfo;
static VerQuery_t pVerQuery;
static HMODULE hRsrc, hIphlp, hPsapi, hVersion;

typedef struct {
    DWORD dwSize, cntUsage, th32ProcessID, th32DefaultHeapID, th32ModuleID, cntThreads, th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    CHAR szExeFile[MAX_PATH];
} PE32A;

typedef struct {
    DWORD cb;
    CHAR DeviceName[32];
    CHAR DeviceString[128];
    DWORD StateFlags;
    CHAR DeviceID[128];
    CHAR DeviceKey[128];
} DispDevA;

typedef struct {
    DWORD cb;
    DWORD CommitTotal, CommitLimit, CommitPeak, PhysicalTotal, PhysicalAvailable,
           SystemCache, KernelTotal, KernelPaged, KernelNonpaged, PageSize;
    DWORD HandleCount, ProcessCount, ThreadCount;
} PerfInfo;

#define IFROW_SIZE 860
#define NTQ_PROC_BUF_MAX (1024 * 1024)

static BYTE *g_procbuf;
static ULONG g_procbuf_size;
static BYTE *g_ifbuf;
static ULONG g_ifbuf_size;

/* ---- CPU ------------------------------------------------------------------ */

static double g_last_idle, g_last_kernel, g_last_user;
static int g_have_cpu;

static int ntq_cpu(Telemetry *t, double *dtotal_out)
{
    BYTE buf[48 * 32];
    ULONG got = 0;
    int k, n;
    double idle = 0, kern = 0, user = 0, di, dk, du, tot;
    if (!pNtQSI) return 0;
    if (pNtQSI(8, buf, (ULONG)(48 * (t->ncpu > 32 ? 32 : t->ncpu)), &got) < 0 || got < 48) return 0;
    n = (int)(got / 48);
    for (k = 0; k < n; k++) {
        idle += li(buf + k * 48);
        kern += li(buf + k * 48 + 8);
        user += li(buf + k * 48 + 16);
    }
    if (g_have_cpu) {
        di = idle - g_last_idle; dk = kern - g_last_kernel; du = user - g_last_user;
        tot = dk + du;
        if (tot > 0) {
            t->cpu = 100.0 * (1.0 - di / tot);
            t->cpu_user = 100.0 * du / tot;
            t->cpu_sys = 100.0 * (dk - di) / tot;
            if (t->cpu < 0) t->cpu = 0;
            if (t->cpu_sys < 0) t->cpu_sys = 0;
        }
        *dtotal_out = tot;
    } else *dtotal_out = 0;
    g_last_idle = idle; g_last_kernel = kern; g_last_user = user;
    g_have_cpu = 1;
    return 1;
}

/* ---- process names --------------------------------------------------------- */

typedef struct { char exe[TM_NAME]; char name[TM_NAME]; char path[MAX_PATH]; } NameCache;
static NameCache g_names[64];
static int g_nnames;

static void file_description(const char *path, char *out, int cap)
{
    DWORD handle = 0, size;
    BYTE *buf;
    WORD *tr;
    UINT len = 0;
    char *desc = 0, q[64];
    out[0] = 0;
    if (!hVersion) {
        hVersion = LoadLibraryA("version.dll");
        if (hVersion) {
            pVerSize = (VerSize_t)GetProcAddress(hVersion, "GetFileVersionInfoSizeA");
            pVerInfo = (VerInfo_t)GetProcAddress(hVersion, "GetFileVersionInfoA");
            pVerQuery = (VerQuery_t)GetProcAddress(hVersion, "VerQueryValueA");
        }
    }
    if (!pVerSize || !pVerInfo || !pVerQuery) return;
    size = pVerSize(path, &handle);
    if (!size || size > 65536) return;
    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) return;
    if (pVerInfo(path, 0, size, buf) &&
        pVerQuery(buf, "\\VarFileInfo\\Translation", (LPVOID *)&tr, &len) && len >= 4) {
        wsprintfA(q, "\\StringFileInfo\\%04x%04x\\FileDescription", tr[0], tr[1]);
        if (pVerQuery(buf, q, (LPVOID *)&desc, &len) && desc && len > 1) acp_to_utf8(desc, out, cap);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    /* Drop trailing spaces some vendors leave in. */
    {
        int n = 0;
        while (out[n]) n++;
        while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
    }
}

/* exe: UTF-8 image name; path: full ANSI path or NULL; pid for NT lookups */
static const char *friendly(const char *exe, const char *path, DWORD pid)
{
    int k;
    char full[MAX_PATH];
    NameCache *c;
    for (k = 0; k < g_nnames; k++) if (s_ieq(g_names[k].exe, exe)) return g_names[k].name;
    if (g_nnames >= 64) return exe;
    c = &g_names[g_nnames++];
    s_copy(c->exe, exe, TM_NAME);
    c->name[0] = 0;
    full[0] = 0;
    if (path) s_copy(full, path, MAX_PATH);
    else if (pModName && pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (h) { if (!pModName(h, 0, full, MAX_PATH)) full[0] = 0; CloseHandle(h); }
    }
    s_copy(c->path, full, MAX_PATH);
    if (full[0]) file_description(full, c->name, TM_NAME);
    if (!c->name[0]) {
        /* Image name without ".exe" */
        int n;
        s_copy(c->name, exe, TM_NAME);
        n = 0; while (c->name[n]) n++;
        if (n > 4 && s_ieq(c->name + n - 4, ".exe")) c->name[n - 4] = 0;
    }
    return c->name;
}

/* ---- process list ------------------------------------------------------------ */

typedef struct { DWORD pid; double time; int seen; } ProcTime;
static ProcTime g_pt[256];
static int g_npt;

static TmApp *app_slot(Telemetry *t, const char *exe)
{
    int k;
    for (k = 0; k < t->napps; k++) if (s_ieq(t->apps[k].exe, exe)) return &t->apps[k];
    if (t->napps >= TM_MAX_APPS) return 0;
    k = t->napps++;
    s_copy(t->apps[k].exe, exe, TM_NAME);
    t->apps[k].name[0] = 0;
    t->apps[k].procs = 0;
    t->apps[k].threads = 0;
    t->apps[k].cpu = 0;
    t->apps[k].mem = 0;
    t->apps[k].pid = 0;
    t->apps[k].npids = 0;
    return &t->apps[k];
}

static void app_add(TmApp *a, DWORD pid, int threads, double cpu, double mem)
{
    if (!a->procs) a->pid = pid;
    a->procs++;
    a->threads += threads;
    if (cpu >= 0 && a->cpu >= 0) a->cpu += cpu; else a->cpu = -1;
    if (mem >= 0 && a->mem >= 0) a->mem += mem; else a->mem = -1;
    if (a->npids < 8) a->pids[a->npids++] = pid;
}

static int ntq_procs(Telemetry *t, double dtotal)
{
    ULONG need = 0;
    LONG st;
    BYTE *p;
    int k, guard = 0;
    DWORD self = GetCurrentProcessId();
    if (!pNtQSI) return 0;
    for (;;) {
        if (!g_procbuf) {
            if (!g_procbuf_size) g_procbuf_size = 64 * 1024;
            g_procbuf = (BYTE *)VirtualAlloc(0, g_procbuf_size, MEM_COMMIT, PAGE_READWRITE);
            if (!g_procbuf) return 0;
        }
        st = pNtQSI(5, g_procbuf, g_procbuf_size, &need);
        if (st == (LONG)0xC0000004L && g_procbuf_size < NTQ_PROC_BUF_MAX && guard++ < 6) {
            VirtualFree(g_procbuf, 0, MEM_RELEASE);
            g_procbuf = 0;
            g_procbuf_size *= 2;
            continue;
        }
        break;
    }
    if (st < 0) return 0;
    for (k = 0; k < g_npt; k++) g_pt[k].seen = 0;
    t->napps = 0;
    p = g_procbuf;
    for (;;) {
        DWORD next = *(DWORD *)p;
        DWORD threads = *(DWORD *)(p + 4);
        double time = li(p + 40) + li(p + 48);
        USHORT nlen = *(USHORT *)(p + 56);
        WCHAR *nbuf = *(WCHAR **)(p + 60);
        DWORD pid = *(DWORD *)(p + 68);
        double ws = (double)*(DWORD *)(p + 104);
        if (pid != 0 && nbuf && nlen) {
            char exe[TM_NAME];
            double cpu = 0;
            int j, found = -1;
            TmApp *a;
            w_to_utf8(nbuf, nlen / 2, exe, TM_NAME);
            for (j = 0; j < g_npt; j++) if (g_pt[j].pid == pid) { found = j; break; }
            if (found >= 0) {
                if (dtotal > 0 && time >= g_pt[found].time) cpu = 100.0 * (time - g_pt[found].time) / dtotal;
                g_pt[found].time = time; g_pt[found].seen = 1;
            } else if (g_npt < 256) {
                g_pt[g_npt].pid = pid; g_pt[g_npt].time = time; g_pt[g_npt].seen = 1; g_npt++;
            }
            if (cpu > 100) cpu = 100;
            a = app_slot(t, exe);
            if (a) {
                if (!a->procs) s_copy(a->name, friendly(exe, 0, pid == self ? 0 : pid), TM_NAME);
                app_add(a, pid, (int)threads, cpu, ws);
            }
        }
        if (!next) break;
        p += next;
    }
    /* Forget ended processes. */
    for (k = 0; k < g_npt; ) {
        if (!g_pt[k].seen) g_pt[k] = g_pt[--g_npt];
        else k++;
    }
    return 1;
}

static int th32_procs(Telemetry *t)
{
    HANDLE snap;
    PE32A pe;
    if (!pSnap || !pP32First || !pP32Next) return 0;
    snap = pSnap(2 /* TH32CS_SNAPPROCESS */, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    t->napps = 0;
    pe.dwSize = sizeof(pe);
    if (pP32First(snap, &pe)) {
        do {
            char exe[TM_NAME];
            TmApp *a;
            if (pe.th32ProcessID == 0) continue;
            acp_to_utf8(base_name(pe.szExeFile), exe, TM_NAME);
            a = app_slot(t, exe);
            if (a) {
                /* 9x gives the full path, NT only the name. */
                if (!a->procs) s_copy(a->name, friendly(exe, base_name(pe.szExeFile) != pe.szExeFile ? pe.szExeFile : 0, 0), TM_NAME);
                app_add(a, pe.th32ProcessID, (int)pe.cntThreads, -1, -1);
            }
            pe.dwSize = sizeof(pe);
        } while (pP32Next(snap, &pe));
    }
    CloseHandle(snap);
    return 1;
}

/* ---- network --------------------------------------------------------------- */

static double g_last_in, g_last_out;
static int g_have_net;

static int iphlp_net(Telemetry *t, double secs)
{
    ULONG size = g_ifbuf_size;
    DWORD n, k;
    double in = 0, out = 0, speed = 0;
    if (!pIfTable) return 0;
    if (!g_ifbuf || pIfTable(g_ifbuf, &size, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        if (g_ifbuf) HeapFree(GetProcessHeap(), 0, g_ifbuf);
        if (size < 4 + IFROW_SIZE * 4) size = 4 + IFROW_SIZE * 4;
        g_ifbuf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
        g_ifbuf_size = size;
        if (!g_ifbuf || pIfTable(g_ifbuf, &size, FALSE) != NO_ERROR) return 0;
    }
    n = *(DWORD *)g_ifbuf;
    for (k = 0; k < n && 4 + (k + 1) * IFROW_SIZE <= g_ifbuf_size; k++) {
        BYTE *row = g_ifbuf + 4 + k * IFROW_SIZE;
        DWORD type = *(DWORD *)(row + 516);
        DWORD sp = *(DWORD *)(row + 524);
        if (type == 24) continue;               /* loopback */
        in += *(DWORD *)(row + 552);
        out += *(DWORD *)(row + 576);
        if (sp > speed) speed = sp;
    }
    if (g_have_net && secs > 0) {
        double d1 = in - g_last_in, d2 = out - g_last_out;
        if (d1 < 0) d1 += 4294967296.0;
        if (d2 < 0) d2 += 4294967296.0;
        t->net_down = d1 / secs;
        t->net_up = d2 / secs;
    }
    g_last_in = in; g_last_out = out; g_have_net = 1;
    t->link_bps = speed;
    return 1;
}

/* ---- NT disk I/O --------------------------------------------------------------- */

static double g_last_rd, g_last_wr;
static int g_have_io;

static int ntq_io(Telemetry *t, double secs)
{
    BYTE buf[512];
    ULONG got = 0;
    double rd, wr;
    if (!pNtQSI || pNtQSI(2, buf, sizeof(buf), &got) < 0 || got < 32) return 0;
    rd = li(buf + 8); wr = li(buf + 16);
    if (g_have_io && secs > 0) {
        t->disk_read = (rd - g_last_rd) / secs;
        t->disk_write = (wr - g_last_wr) / secs;
        if (t->disk_read < 0) t->disk_read = 0;
        if (t->disk_write < 0) t->disk_write = 0;
    }
    g_last_rd = rd; g_last_wr = wr; g_have_io = 1;
    return 1;
}

/* ---- init ------------------------------------------------------------------------ */

static void read_cpu_model(Telemetry *t)
{
    HKEY k;
    DWORD size = sizeof(t->cpu_model), type;
    t->cpu_model[0] = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExA(k, "ProcessorNameString", 0, &type, (BYTE *)t->cpu_model, &size) != ERROR_SUCCESS) {
            size = sizeof(t->cpu_model);
            if (RegQueryValueExA(k, "Identifier", 0, &type, (BYTE *)t->cpu_model, &size) != ERROR_SUCCESS)
                t->cpu_model[0] = 0;
        }
        RegCloseKey(k);
    }
    t->cpu_model[sizeof(t->cpu_model) - 1] = 0;
    /* Collapse runs of spaces that some BIOS strings carry. */
    {
        char *s = t->cpu_model, *d = t->cpu_model;
        while (*s == ' ') s++;
        while (*s) { if (!(*s == ' ' && (d == t->cpu_model || d[-1] == ' '))) *d++ = *s; s++; }
        while (d > t->cpu_model && d[-1] == ' ') d--;
        *d = 0;
    }
}

static void read_gpu_name(Telemetry *t)
{
    EnumDD_t pEnum = (EnumDD_t)GetProcAddress(GetModuleHandleA("user32.dll"), "EnumDisplayDevicesA");
    DispDevA dd;
    DWORD k;
    s_copy(t->gpu_name, "VGA", sizeof(t->gpu_name));
    if (!pEnum) return;
    for (k = 0; k < 8; k++) {
        dd.cb = sizeof(dd);
        if (!pEnum(0, k, &dd, 0)) break;
        if (dd.DeviceString[0] && ((dd.StateFlags & 4) || k == 0)) {
            acp_to_utf8(dd.DeviceString, t->gpu_name, sizeof(t->gpu_name));
            if (dd.StateFlags & 4) break;
        }
    }
}

void tm_init(Telemetry *t)
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    SYSTEM_INFO si;
    char win[MAX_PATH];
    BYTE probe[48 * 32];
    ULONG got;

    ZeroMemory(t, sizeof(*t));
    t->is_nt = (GetVersion() & 0x80000000UL) == 0;
    GetSystemInfo(&si);
    t->ncpu = (int)si.dwNumberOfProcessors;
    if (t->ncpu < 1) t->ncpu = 1;
    read_cpu_model(t);
    read_gpu_name(t);
    g_seed ^= GetTickCount();

    perf_start_all(t);

    hRsrc = LoadLibraryA("RSRC32.DLL");
    if (hRsrc) pRsrc = (Rsrc_t)GetProcAddress(hRsrc, "_MyGetFreeSystemResources32@4");
    t->has_rsrc = pRsrc != 0;

    if (ntdll) pNtQSI = (NtQSI_t)GetProcAddress(ntdll, "NtQuerySystemInformation");
    if (pNtQSI && pNtQSI(8, probe, (ULONG)(48 * (t->ncpu > 32 ? 32 : t->ncpu)), &got) < 0) pNtQSI = 0;
    t->has_ntq = pNtQSI != 0;

    pSnap = (Snap_t)GetProcAddress(k32, "CreateToolhelp32Snapshot");
    pP32First = (P32_t)GetProcAddress(k32, "Process32First");
    pP32Next = (P32_t)GetProcAddress(k32, "Process32Next");
    t->has_th32 = pSnap && pP32First && pP32Next;

    pDiskEx = (DiskEx_t)GetProcAddress(k32, "GetDiskFreeSpaceExA");
    pPower = (Power_t)GetProcAddress(k32, "GetSystemPowerStatus");
    t->has_power = pPower != 0;

    hPsapi = LoadLibraryA("psapi.dll");
    if (hPsapi) {
        pPerfInfo = (PerfInfo_t)GetProcAddress(hPsapi, "GetPerformanceInfo");
        pModName = (ModName_t)GetProcAddress(hPsapi, "GetModuleFileNameExA");
    }
    t->has_psapi = pPerfInfo != 0;

    if (!t->has_dun) {
        hIphlp = LoadLibraryA("iphlpapi.dll");
        if (hIphlp) pIfTable = (IfTable_t)GetProcAddress(hIphlp, "GetIfTable");
    }
    t->has_iphlp = pIfTable != 0;

    if (GetWindowsDirectoryA(win, MAX_PATH) >= 2 && win[1] == ':') { t->drive[0] = win[0]; t->drive[1] = ':'; t->drive[2] = 0; }
    else s_copy(t->drive, "C:", 4);

    t->cpu_temp = 40; t->gpu_temp = 45; t->gpu_load = 12;
    tm_sample(t, 0);
}

void tm_sample(Telemetry *t, double secs)
{
    MEMORYSTATUS ms;
    double v, dtotal = 0;

    /* CPU */
    t->cpu_src = SRC_NONE; t->split_src = SRC_NONE;
    if (t->has_dyn && perf_value(PC_CPU, secs, &v)) {
        t->cpu = v;
        t->cpu_src = SRC_LIVE;
        /* 9x gives the total only; split it the way System Monitor users read it. */
        t->cpu_user = t->cpu * 0.66;
        t->cpu_sys = t->cpu - t->cpu_user;
        t->split_src = SRC_SIM;
    } else if (ntq_cpu(t, &dtotal)) {
        t->cpu_src = SRC_LIVE;
        t->split_src = SRC_LIVE;
    } else {
        t->cpu = walk(t->cpu, 16, 6, 2, 95);
        t->cpu_user = t->cpu * 0.66;
        t->cpu_sys = t->cpu - t->cpu_user;
        t->cpu_src = t->split_src = SRC_SIM;
    }

    /* Memory */
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    t->mem_load = ms.dwMemoryLoad;
    t->phys_total = (double)ms.dwTotalPhys;
    t->phys_avail = (double)ms.dwAvailPhys;
    t->page_total = (double)ms.dwTotalPageFile;
    t->page_avail = (double)ms.dwAvailPageFile;
    t->memdetail_src = SRC_NONE;
    t->kernel = t->cache = 0;
    if (t->has_dyn) {
        double locked, cache;
        double pages = t->phys_total / 4096.0;
        if (perf_value(PC_LOCKED, secs, &locked) && perf_value(PC_DISKCACHE, secs, &cache)) {
            /* These counters hold pages on some builds and bytes on others. */
            t->kernel = locked > pages ? locked : locked * 4096.0;
            t->cache = cache > pages ? cache : cache * 4096.0;
            t->memdetail_src = SRC_LIVE;
        }
    }
    if (t->memdetail_src == SRC_NONE && pPerfInfo) {
        PerfInfo pi;
        pi.cb = sizeof(pi);
        if (pPerfInfo(&pi, sizeof(pi)) && (pi.KernelTotal || pi.SystemCache)) {
            t->kernel = (double)pi.KernelTotal * (double)pi.PageSize;
            t->cache = (double)pi.SystemCache * (double)pi.PageSize;
            t->memdetail_src = SRC_LIVE;
        }
    }
    if (t->page_avail > t->page_total) t->page_avail = t->page_total;
    if (t->is_nt) {
        /* NT reports the commit limit here, not a swap file. */
        t->swap_is_commit = 1;
        t->swap_total = t->page_total;
        t->swap_used = t->page_total - t->page_avail;
    } else {
        t->swap_is_commit = 0;
        t->swap_total = t->page_total;
        t->swap_used = t->page_total - t->page_avail;
    }

    /* System resources (9x only) */
    if (pRsrc) {
        t->res_free[0] = (int)pRsrc(0);
        t->res_free[1] = (int)pRsrc(1);
        t->res_free[2] = (int)pRsrc(2);
        t->res_src = SRC_LIVE;
    } else t->res_src = SRC_NONE;

    /* Disk space */
    {
        char root[4];
        root[0] = t->drive[0]; root[1] = ':'; root[2] = '\\'; root[3] = 0;
        t->disk_src = SRC_NONE;
        if (pDiskEx) {
            ULARGE_INTEGER avail, total, freeb;
            if (pDiskEx(root, &avail, &total, &freeb)) {
                t->disk_total = (double)total.HighPart * 4294967296.0 + (double)total.LowPart;
                t->disk_free = (double)freeb.HighPart * 4294967296.0 + (double)freeb.LowPart;
                t->disk_src = SRC_LIVE;
            }
        }
        if (t->disk_src == SRC_NONE) {
            DWORD spc, bps, fc, tc;
            if (GetDiskFreeSpaceA(root, &spc, &bps, &fc, &tc)) {
                t->disk_total = (double)tc * spc * bps;
                t->disk_free = (double)fc * spc * bps;
                t->disk_src = SRC_LIVE;
            }
        }
        if (!t->fs[0]) {
            DWORD serial, maxlen, flags;
            if (!GetVolumeInformationA(root, 0, 0, &serial, &maxlen, &flags, t->fs, sizeof(t->fs))) t->fs[0] = 0;
        }
    }

    /* Disk throughput */
    t->diskio_src = SRC_NONE;
    if (t->has_dyn) {
        double r, w;
        if (perf_value(PC_FS_READ, secs, &r) && perf_value(PC_FS_WRITE, secs, &w)) {
            t->disk_read = r; t->disk_write = w; t->diskio_src = SRC_LIVE;
        }
    }
    if (t->diskio_src == SRC_NONE && ntq_io(t, secs)) t->diskio_src = SRC_LIVE;

    /* Power */
    if (pPower) {
        SYSTEM_POWER_STATUS ps;
        if (pPower(&ps)) {
            t->ac_online = ps.ACLineStatus == 1;
            t->has_batt = !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255 && ps.BatteryLifePercent <= 100;
            t->batt_pct = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : 0;
            t->batt_secs = ps.BatteryLifeTime == (DWORD)-1 ? -1 : (int)ps.BatteryLifeTime;
        }
    }

    /* Network */
    t->net_src = SRC_NONE;
    if (t->has_dun) {
        double rx, tx, sp;
        if (perf_total_rate(PC_DUN_RX, secs, &rx) && perf_total_rate(PC_DUN_TX, secs, &tx)) {
            t->net_down = rx; t->net_up = tx;
            if (perf_value(PC_DUN_SPEED, secs, &sp)) t->link_bps = sp;
            t->net_src = SRC_LIVE;
        }
    }
    if (t->net_src == SRC_NONE && iphlp_net(t, secs)) t->net_src = SRC_LIVE;

    /* Processes */
    t->proc_src = SRC_NONE;
    t->proc_cpu = 0;
    if (ntq_procs(t, dtotal)) { t->proc_src = SRC_LIVE; t->proc_cpu = g_have_cpu && dtotal > 0; }
    else if (th32_procs(t)) t->proc_src = SRC_LIVE;
    else t->napps = 0;

    /* Simulated readings: no 9x API reports these. */
    t->cpu_temp = walk(t->cpu_temp, 33 + t->cpu * 0.3, 0.6, 28, 90);
    t->gpu_load = walk(t->gpu_load, 14, 7, 2, 60);
    t->gpu_temp = walk(t->gpu_temp, 45 + t->gpu_load * 0.2, 0.5, 30, 85);
}

int tm_end_process(Telemetry *t, const TmApp *a)
{
    int k, ok = 0;
    DWORD self = GetCurrentProcessId();
    (void)t;
    for (k = 0; k < a->npids; k++) {
        HANDLE h;
        if (a->pids[k] == self) continue;
        h = OpenProcess(PROCESS_TERMINATE, FALSE, a->pids[k]);
        if (h) { if (TerminateProcess(h, 1)) ok = 1; CloseHandle(h); }
    }
    return ok;
}

void tm_shutdown(Telemetry *t)
{
    int k;
    for (k = 0; k < PC_COUNT; k++)
        if (g_pc[k].started) perf_query("PerfStats\\StopStat", g_pc[k].name, 0);
    if (g_procbuf) VirtualFree(g_procbuf, 0, MEM_RELEASE);
    if (g_ifbuf) HeapFree(GetProcessHeap(), 0, g_ifbuf);
    if (hRsrc) FreeLibrary(hRsrc);
    if (hIphlp) FreeLibrary(hIphlp);
    if (hPsapi) FreeLibrary(hPsapi);
    if (hVersion) FreeLibrary(hVersion);
    (void)t;
}

int tm_app_path(const char *exe, char *out, int cap)
{
    int k;
    out[0] = 0;
    for (k = 0; k < g_nnames; k++)
        if (s_ieq(g_names[k].exe, exe)) { s_copy(out, g_names[k].path, cap); break; }
    return out[0] != 0;
}
