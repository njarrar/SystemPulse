#!/usr/bin/env python3
"""Fails if pulse.exe imports a DLL or function that Windows XP SP2 lacks,
or if its PE header asks for a newer OS than 5.1.

Usage: check_xp_imports.py pulse.exe [objdump]
"""
import re, subprocess, sys

XP_DLLS = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll', 'advapi32.dll', 'msvcrt.dll',
           'psapi.dll', 'iphlpapi.dll', 'gdiplus.dll', 'comctl32.dll', 'ole32.dll', 'uxtheme.dll'}
# Common Vista+ functions that toolchains pull in by accident.
NEWER = {'GetTickCount64', 'InitializeConditionVariable', 'SleepConditionVariableCS', 'SleepConditionVariableSRW',
         'WakeConditionVariable', 'WakeAllConditionVariable', 'InitializeSRWLock', 'AcquireSRWLockExclusive',
         'ReleaseSRWLockExclusive', 'AcquireSRWLockShared', 'ReleaseSRWLockShared', 'TryAcquireSRWLockExclusive',
         'InitOnceExecuteOnce', 'InitOnceBeginInitialize', 'InitOnceComplete', 'GetThreadId', 'GetProcessId',
         'QueryFullProcessImageNameW', 'QueryFullProcessImageNameA', 'InitializeCriticalSectionEx',
         'CreateEventExW', 'CreateMutexExW', 'CreateSemaphoreExW', 'FlsAlloc', 'FlsGetValue', 'FlsSetValue', 'FlsFree',
         'GetFileInformationByHandleEx', 'SetFileInformationByHandle', 'CompareStringEx', 'LCMapStringEx',
         'GetLocaleInfoEx', 'GetUserDefaultLocaleName', 'LocaleNameToLCID', 'ResolveLocaleName', 'GetDpiForWindow',
         'SetProcessDPIAware', 'SetThreadDescription', 'GetSystemTimePreciseAsFileTime', 'RegGetValueW', 'RegGetValueA',
         'ChangeWindowMessageFilter', 'DwmExtendFrameIntoClientArea', 'GetIfTable2', 'GetIfEntry2', 'K32GetProcessMemoryInfo',
         'K32EnumProcesses', 'K32GetModuleFileNameExW', 'K32GetPerformanceInfo', 'GetNumaHighestNodeNumber',
         'GetLogicalProcessorInformation', 'GdipDrawImageFX', 'GdipCreateEffect'}

def main():
    exe = sys.argv[1]
    objdump = sys.argv[2] if len(sys.argv) > 2 else 'i686-w64-mingw32-objdump'
    out = subprocess.run([objdump, '-p', exe], capture_output=True, text=True, check=True).stdout
    bad = []
    for key in ('MajorOSystemVersion', 'MajorSubsystemVersion'):
        m = re.search(key + r'\s+(\d+)', out)
        if m and int(m.group(1)) > 5:
            bad.append('%s is %s (XP needs 5)' % (key, m.group(1)))
    dll = None
    count = 0
    for line in out.splitlines():
        m = re.match(r'\s*DLL Name: (\S+)', line)
        if m:
            dll = m.group(1).lower()
            if dll not in XP_DLLS:
                bad.append('imports %s' % dll)
            continue
        m = re.match(r'\s+[0-9a-f]+\s+\d+\s+(\w+)$', line)
        if dll and m:
            count += 1
            if m.group(1) in NEWER:
                bad.append('%s!%s is newer than XP' % (dll, m.group(1)))
    for b in bad:
        print('FAIL', b)
    print('%d imports checked, %d problems' % (count, len(bad)))
    sys.exit(1 if bad else 0)

main()
