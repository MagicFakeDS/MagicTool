#!/bin/sh
# Тестовая сборка (asInvoker) с параллельной инкрементальной компиляцией: tbx_test.exe (GUI/--shot) и unit.exe
cd "$(dirname "$0")/.."
mkdir -p test/obj
windres -O coff -i res/app_test.rc -o test/app_test.res --include-dir res || exit 1
ls src/*.cpp test/unit.cpp | xargs -P 8 -I{} sh -c '
  f={}; o=test/obj/$(basename $f .cpp).o
  if [ ! -f $o ] || [ $f -nt $o ] || [ -n "$(find src -name "*.h" -newer $o)" ]; then
    g++ -std=c++17 -O2 -municode -c $f -o $o 2>&1 | grep -E "error|escape" ; fi'
LIBS="-lgdiplus -lole32 -loleaut32 -lwbemuuid -luuid -lshell32 -lshlwapi -ladvapi32 -lcomctl32 -ldwmapi -liphlpapi -lws2_32 -lpowrprof -lversion -lpsapi -lcrypt32 -lwinhttp -lgdi32 -luser32 -lsetupapi -lwevtapi -lcomdlg32"
OBJS=$(ls test/obj/*.o | grep -v unit.o)
g++ -municode -mwindows -static $OBJS test/app_test.res -o test/tbx_test.exe $LIBS || exit 1
g++ -municode -static $OBJS test/obj/unit.o test/app_test.res -o test/unit.exe $LIBS || exit 1
echo BUILD_OK
