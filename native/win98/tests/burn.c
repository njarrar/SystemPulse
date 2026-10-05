/* Test helper: keeps every CPU busy so Pulse's hog alert can be checked. */
#include <windows.h>

static DWORD WINAPI spin(LPVOID p)
{
    volatile unsigned long x = 0;
    (void)p;
    for (;;) x++;
    return 0;
}

int main(void)
{
    SYSTEM_INFO si;
    DWORD k, id;
    GetSystemInfo(&si);
    for (k = 1; k < si.dwNumberOfProcessors; k++) CreateThread(0, 0, spin, 0, 0, &id);
    spin(0);
    return 0;
}
