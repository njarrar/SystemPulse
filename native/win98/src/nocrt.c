/*
 * Entry point and the few memory routines GCC may call, for the build that
 * links no C runtime at all (mingw-w64 with -nostdlib). The VC6 build links
 * the normal CRT and leaves this file out.
 */
#ifdef PULSE_NOCRT
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    if (p < q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

void WINAPI WinMainCRTStartup(void)
{
    LPSTR cmd = GetCommandLineA();
    /* Skip the program name, as the CRT would. */
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; }
    else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    ExitProcess((UINT)WinMain(GetModuleHandleA(0), 0, cmd, SW_SHOWDEFAULT));
}
#else
typedef int pulse_nocrt_unused;
#endif
