@echo off
chcp 65001 >nul
cd /d "%~dp0"
where g++ >nul 2>nul || ( echo Не найден g++. Установите WinLibs: winget install BrechtSanders.WinLibs.POSIX.UCRT & exit /b 1 )
echo [1/2] Ресурсы (иконка, логотип, манифест)...
windres -O coff -i res\app.rc -o res\app.res --include-dir res || goto fail
echo [2/2] Компиляция установщика...
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    main.cpp src\ui.cpp src\util.cpp res\app.res -Isrc -o MagicToolSetup.exe ^
    -lgdiplus -lole32 -loleaut32 -lwbemuuid -luuid -lshell32 -lshlwapi -ladvapi32 -lcomctl32 ^
    -ldwmapi -liphlpapi -lws2_32 -lpowrprof -lversion -lpsapi -lcrypt32 -lwinhttp -lgdi32 -lpdh ^
    -luser32 -lsetupapi -lwevtapi -lcomdlg32 -lwininet -s 2> "%TEMP%\magicsetup_build.log"
if not "%errorlevel%"=="0" ( findstr /v /c:"rsrc merge failure" "%TEMP%\magicsetup_build.log" & goto fail )
findstr /v /c:"rsrc merge failure" "%TEMP%\magicsetup_build.log"
echo.
echo Готово: %~dp0MagicToolSetup.exe
exit /b 0
:fail
echo.
echo ОШИБКА СБОРКИ
exit /b 1
