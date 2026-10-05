/* Test helper: keeps every CPU busy for N seconds (default 900) at a duty
   of D percent (default 60), so the hog alert can be checked against a real
   process. Usage: spin.exe [seconds] [duty] */
#include <windows.h>
#include <stdlib.h>
static DWORD g_busy = 30, g_idle = 20;
static DWORD WINAPI burn(LPVOID p) {
    DWORD end = GetTickCount() + (DWORD)(size_t)p;
    while (GetTickCount() < end) {
        DWORD t = GetTickCount();
        while (GetTickCount() - t < g_busy) { }
        if (g_idle) Sleep(g_idle);
    }
    return 0;
}
int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR cmd, int show) {
    SYSTEM_INFO si;
    DWORD ms = (cmd && *cmd ? (DWORD)atoi(cmd) : 900) * 1000;
    HANDLE h[64];
    DWORD i;
    if (cmd && *cmd) {
        char *sp = cmd;
        while (*sp && *sp != ' ') sp++;
        if (*sp) { int duty = atoi(sp + 1); if (duty > 0 && duty <= 100) { g_busy = (DWORD)duty / 2; g_idle = (DWORD)(100 - duty) / 2; } }
    }
    GetSystemInfo(&si);
    for (i = 0; i < si.dwNumberOfProcessors && i < 64; i++) h[i] = CreateThread(0, 0, burn, (LPVOID)(size_t)ms, 0, 0);
    WaitForMultipleObjects(i, h, TRUE, INFINITE);
    return 0;
}
