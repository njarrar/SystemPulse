@echo off
rem Build pulse98.exe with Visual C++ 6.0.
rem Run VCVARS32.BAT first. Python 3 generates the catalog and the icon.
if not exist build mkdir build
python tools\gen_catalog.py ..\..\locales build\catalog.c || goto fail
python tools\gen_icon.py build\pulse.ico || goto fail
rc /i build /fo build\pulse.res src\pulse.rc || goto fail
cl /nologo /O1 /W3 /MD /DWINVER=0x0410 /D_WIN32_WINDOWS=0x0410 /Isrc /Ibuild /Fobuild\ /Febuild\pulse98.exe ^
   src\pulse.c src\telemetry.c src\text.c src\i18n.c src\plural.c build\catalog.c build\pulse.res ^
   kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib /link /subsystem:windows,4.0 || goto fail
echo Built build\pulse98.exe
goto :eof
:fail
echo Build failed.
exit /b 1
