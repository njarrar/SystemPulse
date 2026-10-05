/* Test helper: prints memory figures for running pulse98.exe processes.
 * Working set and private bytes from psapi, plus committed private pages
 * counted with VirtualQueryEx. Under Wine these include Wine's own runtime. */
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) do {
        if (_stricmp(pe.szExeFile, "pulse98.exe") == 0) {
            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            PROCESS_MEMORY_COUNTERS pmc;
            MEMORY_BASIC_INFORMATION mbi;
            unsigned char *p = 0;
            SIZE_T priv = 0, img = 0;
            if (!h) continue;
            GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
            while (VirtualQueryEx(h, p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) priv += mbi.RegionSize;
                if (mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE && mbi.AllocationBase == (void *)0x400000) img += mbi.RegionSize;
                p = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
                if ((ULONG_PTR)p >= 0x7FFF0000UL) break;
            }
            printf("pid %08lX working set %lu KB, pagefile %lu KB, private committed %lu KB, exe image %lu KB\n",
                   pe.th32ProcessID, (unsigned long)(pmc.WorkingSetSize / 1024), (unsigned long)(pmc.PagefileUsage / 1024),
                   (unsigned long)(priv / 1024), (unsigned long)(img / 1024));
            CloseHandle(h);
        }
    } while (Process32Next(snap, &pe));
    return 0;
}
