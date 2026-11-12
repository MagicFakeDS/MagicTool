@echo off
chcp 65001 >nul
cd /d "%~dp0"
where g++ >nul 2>nul || (
    echo Не найден g++. Установите WinLibs: winget install BrechtSanders.WinLibs.POSIX.UCRT
    exit /b 1
)
echo [1/2] Ресурсы (иконка, логотип, манифест)...
windres -O coff -i res\app.rc -o res\app.res --include-dir res || goto :fail
echo [2/2] Компиляция...
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    main.cpp src\ui.cpp src\util.cpp res\app.res -Isrc -o MagicTool.exe ^
    -lgdiplus -lole32 -loleaut32 -lwbemuuid -luuid -lshell32 -lshlwapi -ladvapi32 -lcomctl32 ^
    -ldwmapi -liphlpapi -lws2_32 -lpowrprof -lversion -lpsapi -lcrypt32 -lwinhttp -lgdi32 -lpdh ^
    -luser32 -lsetupapi -lwevtapi -lcomdlg32 -s 2> "%TEMP%\magictool_build.log"
set ERR=%errorlevel%
findstr /v /c:"rsrc merge failure" "%TEMP%\magictool_build.log"
if not "%ERR%"=="0" goto :fail
echo.
echo Готово: %~dp0MagicTool.exe
exit /b 0
:fail
echo.
echo ОШИБКА СБОРКИ
exit /b 1
