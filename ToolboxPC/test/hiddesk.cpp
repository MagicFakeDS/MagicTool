#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00

#include <windows.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdio>
using namespace Gdiplus;

static HWND g_wnd = nullptr;
static BOOL CALLBACK Enum(HWND h, LPARAM) {
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"ToolboxPCWindow") == 0) { g_wnd = h; return FALSE; }
    return TRUE;
}
static int Enc(const WCHAR* f, CLSID* c) {
    UINT n = 0, s = 0; GetImageEncodersSize(&n, &s);
    std::vector<BYTE> b(s); auto p = (ImageCodecInfo*)b.data(); GetImageEncoders(n, s, p);
    for (UINT i = 0; i < n; ++i) if (!wcscmp(p[i].MimeType, f)) { *c = p[i].Clsid; return i; }
    return -1;
}
static void Shot(const std::wstring& path) {
    RECT rc; GetClientRect(g_wnd, &rc);
    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, rc.right, rc.bottom);
    SelectObject(mdc, bmp);
    SendMessageW(g_wnd, WM_PRINTCLIENT, (WPARAM)mdc, PRF_CLIENT);
    Bitmap b(bmp, nullptr);
    CLSID png; Enc(L"image/png", &png);
    b.Save(path.c_str(), &png, nullptr);
    DeleteDC(mdc); DeleteObject(bmp); ReleaseDC(nullptr, sdc);
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    GdiplusStartupInput gsi; ULONG_PTR tok; GdiplusStartup(&tok, &gsi, nullptr);
    HDESK desk = CreateDesktopW(L"tbx_hidden_test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk) { wprintf(L"CreateDesktop failed %lu\n", GetLastError()); return 3; }
    if (!SetThreadDesktop(desk)) { wprintf(L"SetThreadDesktop failed %lu\n", GetLastError()); return 4; }
    SetEnvironmentVariableW(L"TOOLBOXPC_SHOT_DIR", argv[2]);
    STARTUPINFOW si{ sizeof(si) };
    si.lpDesktop = (LPWSTR)L"tbx_hidden_test";
    PROCESS_INFORMATION pi{};
    std::wstring cl = L"\"" + std::wstring(argv[1]) + L"\"";
    if (!CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { wprintf(L"CreateProcess %lu\n", GetLastError()); return 5; }
    for (int i = 0; i < 100 && !g_wnd; ++i) { Sleep(100); EnumDesktopWindows(desk, Enum, 0); }
    if (!g_wnd) { wprintf(L"window not found\n"); TerminateProcess(pi.hProcess, 1); return 6; }
    UINT dpi = GetDpiForWindow(g_wnd);
    float s = dpi ? dpi / 96.0f : 1.0f;
    wprintf(L"window found, dpi %u\n", dpi);
    std::wstring script = argv[3], dir = argv[2];
    size_t pos = 0;
    while (pos < script.size()) {
        size_t e = script.find(L';', pos);
        std::wstring cmd = script.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        pos = e == std::wstring::npos ? script.size() : e + 1;
        std::wstring op = cmd.substr(0, cmd.find(L':')), arg = cmd.find(L':') == std::wstring::npos ? L"" : cmd.substr(cmd.find(L':') + 1);
        int a = 0, b = 0, c = 0;
        swscanf(arg.c_str(), L"%d,%d,%d", &a, &b, &c);
        LPARAM lp = MAKELPARAM((int)(a * s), (int)(b * s));
        if (op == L"wait") Sleep(a);
        else if (op == L"move") SendMessageW(g_wnd, WM_MOUSEMOVE, 0, lp);
        else if (op == L"click") {
            SendMessageW(g_wnd, WM_MOUSEMOVE, 0, lp);
            SendMessageW(g_wnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            SendMessageW(g_wnd, WM_LBUTTONUP, 0, lp);
        } else if (op == L"wheel") {
            POINT pt{ (int)(a * s), (int)(b * s) };
            ClientToScreen(g_wnd, &pt);
            SendMessageW(g_wnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)c), MAKELPARAM(pt.x, pt.y));
        } else if (op == L"key") SendMessageW(g_wnd, WM_KEYDOWN, a, 0);
        else if (op == L"char") for (wchar_t ch : arg) SendMessageW(g_wnd, WM_CHAR, ch, 0);
        else if (op == L"shot") {
            static int n = 0;
            ++n;
            SendMessageW(g_wnd, WM_APP + 7, n, 0);
            std::wstring from = dir + L"\\shot_" + std::to_wstring(n) + L".png", to = dir + L"\\" + arg + L".png";
            MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
            wprintf(L"shot %ls\n", arg.c_str());
        }
        else if (op == L"close") PostMessageW(g_wnd, WM_CLOSE, 0, 0);
        else if (op == L"alive") wprintf(L"alive: %d\n", WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT);
    }
    DWORD w = WaitForSingleObject(pi.hProcess, 3000);
    wprintf(L"process exited: %d\n", w == WAIT_OBJECT_0);
    if (w != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    CloseDesktop(desk);
    return 0;
}
