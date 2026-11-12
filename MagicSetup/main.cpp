#include "common.h"
#include "ui.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <wincrypt.h>

using namespace Gdiplus;

static const wchar_t* BASE_URL = L"https://github.com/MagicFakeDS/MagicTool/releases/latest/download/";

static const float TITLE_H = 54;
static HWND g_hwnd = nullptr;
static float g_dpi = 1.0f;
static Bitmap* g_logo = nullptr;

struct Comp { wstring name, file, sha, desc; long size = 0; bool sel = true; };
static vector<Comp> g_comps;
static bool g_mLoading = false, g_mError = false;
static wstring g_mErrText;

static wstring g_dir;
static bool g_desk = true, g_start = true;
static int g_phase = 0;
static bool g_uninst = false;
static vector<wstring> g_log;
static bool g_running = false;
static float g_scroll = 0;
static wstring g_status;
static float g_prog = -1.0f;
static std::atomic<bool> g_cancel{ false };

void Redraw() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

static Bitmap* LoadPngResource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return nullptr;
    IStream* s = SHCreateMemStream((const BYTE*)LockResource(LoadResource(nullptr, r)), SizeofResource(nullptr, r));
    if (!s) return nullptr;
    Bitmap* b = Bitmap::FromStream(s); s->Release();
    if (b && b->GetLastStatus() != Ok) { delete b; b = nullptr; }
    return b;
}
static void Log(const wstring& s) { PostUI([s] { g_log.push_back(s); g_scroll = 1e9f; Redraw(); }); }
static void SetStat(const wstring& s, float p = -1.0f) { PostUI([s, p] { g_status = s; g_prog = p; Redraw(); }); }

struct Url { wstring host, path; INTERNET_PORT port = 443; bool https = true; };
static bool ParseUrl(const wstring& url, Url& u) {
    URL_COMPONENTSW c{ sizeof(c) };
    wchar_t host[256] = L"", path[4096] = L"";
    c.lpszHostName = host; c.dwHostNameLength = 255;
    c.lpszUrlPath = path; c.dwUrlPathLength = 4095;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &c)) return false;
    u.host = host; u.path = path; u.port = c.nPort; u.https = (c.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}
static bool HttpGet(const wstring& url, std::function<bool(const BYTE*, DWORD)> sink,
                    std::function<void(DWORD, DWORD)> prog, std::atomic<bool>* cancel) {
    Url u; if (!ParseUrl(url, u)) return false;
    HINTERNET hs = WinHttpOpen(L"MagicToolSetup/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) return false;
    DWORD toR = 30000, toS = 30000, toW = 30000, toC = 30000;
    WinHttpSetTimeouts(hs, toR, toC, toS, toW);
    bool ok = false;
    HINTERNET hc = WinHttpConnect(hs, u.host.c_str(), u.port, 0);
    if (hc) {
        DWORD flags = u.https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hr = WinHttpOpenRequest(hc, L"GET", u.path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (hr) {
            if (WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(hr, nullptr)) {
                DWORD code = 0, len = sizeof(code);
                WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
                if (code >= 200 && code < 300) {
                    DWORD total = 0, l2 = sizeof(total);
                    WinHttpQueryHeaders(hr, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &total, &l2, WINHTTP_NO_HEADER_INDEX);
                    DWORD got = 0; ok = true;
                    for (;;) {
                        if (cancel && *cancel) { ok = false; break; }
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(hr, &avail)) { ok = false; break; }
                        if (avail == 0) break;
                        std::vector<BYTE> buf(avail);
                        DWORD rd = 0;
                        if (!WinHttpReadData(hr, buf.data(), avail, &rd)) { ok = false; break; }
                        if (rd == 0) break;
                        if (!sink(buf.data(), rd)) { ok = false; break; }
                        got += rd;
                        if (prog) prog(got, total);
                    }
                }
            }
            WinHttpCloseHandle(hr);
        }
        WinHttpCloseHandle(hc);
    }
    WinHttpCloseHandle(hs);
    return ok;
}
static bool HttpGetText(const wstring& url, wstring& out) {
    std::string acc;
    bool ok = HttpGet(url, [&](const BYTE* p, DWORD n) { acc.append((const char*)p, n); return true; }, nullptr, nullptr);
    if (!ok) return false;
    int wn = MultiByteToWideChar(CP_UTF8, 0, acc.data(), (int)acc.size(), nullptr, 0);
    out.assign(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, acc.data(), (int)acc.size(), &out[0], wn);
    return true;
}
static bool HttpDownload(const wstring& url, const wstring& path,
                         std::function<void(DWORD, DWORD)> prog, std::atomic<bool>* cancel) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = HttpGet(url, [&](const BYTE* p, DWORD n) {
        DWORD wr = 0; return WriteFile(f, p, n, &wr, nullptr) && wr == n;
    }, prog, cancel);
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}
static wstring Sha256File(const wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    wstring out;
    HCRYPTPROV prov = 0; HCRYPTHASH h = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
            BYTE buf[65536]; DWORD rd = 0;
            while (ReadFile(f, buf, sizeof(buf), &rd, nullptr) && rd > 0) CryptHashData(h, buf, rd, 0);
            BYTE d[32]; DWORD dl = sizeof(d);
            if (CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0)) out = HexBytes(d, dl);
            CryptDestroyHash(h);
        }
        CryptReleaseContext(prov, 0);
    }
    CloseHandle(f);
    return out;
}

static bool MakeShortcut(const wstring& lnk, const wstring& target, const wstring& desc, const wstring& workdir) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(workdir.c_str());
    sl->SetDescription(desc.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr; bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
    sl->Release();
    return ok;
}
static wstring SelfPath() { wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH); return b; }
static wstring StartMenuDir() { return KnownFolder(FOLDERID_CommonPrograms) + L"\\MagicTool"; }

static void SetUninstallReg(const wstring& dir, const wstring& exe) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MagicTool",
                        0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    auto S = [&](const wchar_t* n, const wstring& v) { RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2)); };
    auto D = [&](const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, sizeof(v)); };
    S(L"DisplayName", L"MagicTool Suite");
    S(L"DisplayVersion", L"1.0");
    S(L"Publisher", L"MagicFakeDS");
    S(L"InstallLocation", dir);
    S(L"DisplayIcon", exe);
    S(L"UninstallString", L"\"" + dir + L"\\uninstall.exe\" /uninstall");
    D(L"NoModify", 1); D(L"NoRepair", 1); D(L"EstimatedSize", 4000);
    RegCloseKey(k);
}

static bool DoInstall(const wstring& dir, bool desk, bool start) {
    vector<Comp> sel;
    for (auto& c : g_comps) if (c.sel) sel.push_back(c);
    if (sel.empty()) { Log(L"ОШИБКА: не выбран ни один компонент"); return false; }

    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    wstring sm = StartMenuDir();
    SHCreateDirectoryExW(nullptr, sm.c_str(), nullptr);
    wstring firstExe;
    vector<wstring> deskLnks;

    int idx = 0;
    for (auto& c : sel) {
        idx++;
        wstring url = wstring(BASE_URL) + c.file;
        wstring part = wstring(tmp) + c.file + L".part";
        Log(Fmt(L"[%d/%d] Скачиваю %ls…", idx, (int)sel.size(), c.name.c_str()));
        bool dok = HttpDownload(url, part, [&](DWORD got, DWORD total) {
            float p = total ? (float)got / (float)total : -1.0f;
            SetStat(Fmt(L"Скачиваю %ls — %d%%", c.name.c_str(), total ? (int)(p * 100) : 0), p);
        }, &g_cancel);
        if (!dok) { Log(L"ОШИБКА: не удалось скачать " + c.name + L" (проверьте интернет)"); return false; }

        SetStat(L"Проверяю контрольную сумму…", -1.0f);
        Log(L"Проверяю контрольную сумму…");
        wstring got = Sha256File(part);
        if (_wcsicmp(got.c_str(), c.sha.c_str()) != 0) {
            DeleteFileW(part.c_str());
            Log(L"ОШИБКА: контрольная сумма " + c.name + L" не совпала — файл повреждён или подменён");
            return false;
        }
        wstring dest = dir + L"\\" + c.file;
        DeleteFileW(dest.c_str());
        if (!MoveFileW(part.c_str(), dest.c_str())) { CopyFileW(part.c_str(), dest.c_str(), FALSE); DeleteFileW(part.c_str()); }
        if (!FileExists(dest)) { Log(L"ОШИБКА: не удалось записать " + c.file); return false; }
        if (firstExe.empty()) firstExe = dest;

        MakeShortcut(sm + L"\\" + c.name + L".lnk", dest, c.name, dir);
        if (desk) {
            wstring dl = KnownFolder(FOLDERID_PublicDesktop) + L"\\" + c.name + L".lnk";
            MakeShortcut(dl, dest, c.name, dir);
            deskLnks.push_back(dl);
        }
        Log(c.name + L" — установлен ✔");
    }

    {
        HANDLE h = CreateFileW((dir + L"\\installed.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            std::string all;
            for (auto& l : deskLnks) {
                int n = WideCharToMultiByte(CP_UTF8, 0, l.c_str(), (int)l.size(), nullptr, 0, nullptr, nullptr);
                std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, l.c_str(), (int)l.size(), &s[0], n, nullptr, nullptr);
                all += s; all += "\n";
            }
            DWORD wr = 0; WriteFile(h, all.data(), (DWORD)all.size(), &wr, nullptr); CloseHandle(h);
        }
    }
    MakeShortcut(sm + L"\\Удалить MagicTool.lnk", dir + L"\\uninstall.exe", L"Удалить MagicTool", dir);
    CopyFileW(SelfPath().c_str(), (dir + L"\\uninstall.exe").c_str(), FALSE);
    SetUninstallReg(dir, firstExe);
    g_dir = dir;
    Log(L"Установка завершена ✔");
    return true;
}

static bool DoUninstall() {
    wstring dir = ExeDir();
    Log(L"Удаляю ярлыки…");

    HANDLE h = CreateFileW((dir + L"\\installed.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        std::string data; char buf[4096]; DWORD rd = 0;
        while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) data.append(buf, rd);
        CloseHandle(h);
        size_t pos = 0;
        while (pos < data.size()) {
            size_t nl = data.find('\n', pos);
            std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? data.size() : nl + 1;
            if (line.empty()) continue;
            int wn = MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(), nullptr, 0);
            wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(), &w[0], wn);
            DeleteFileW(w.c_str());
        }
    }
    wstring sm = StartMenuDir();
    RunCapture(L"\"" + Sys32(L"cmd.exe") + L"\" /c rd /s /q \"" + sm + L"\"", nullptr, Enc::Oem, nullptr);
    Log(L"Удаляю запись из «Программы и компоненты»…");
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MagicTool", KEY_WOW64_64KEY, 0);
    Log(L"Удаляю файлы программы…");
    wstring cmd = L"/c timeout /t 2 /nobreak >nul & rd /s /q \"" + dir + L"\"";
    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
    Log(L"Готово — программа удалена ✔");
    return true;
}

static void LoadManifest() {
    if (g_mLoading) return;
    g_mLoading = true; g_mError = false; Redraw();
    Async([]() -> std::function<void()> {
        wstring text;
        bool ok = HttpGetText(wstring(BASE_URL) + L"manifest.txt", text);
        auto comps = std::make_shared<vector<Comp>>();
        bool parsed = false;
        if (ok) {
            for (auto& line : Split(text, L'\n')) {
                wstring t = Trim(line);
                if (t.empty() || t[0] == L'#') continue;
                auto f = Split(t, L'|');
                if (f.size() >= 6) {
                    Comp c; c.name = Trim(f[0]); c.file = Trim(f[1]); c.sha = Trim(f[2]);
                    c.size = _wtol(f[3].c_str()); c.desc = Trim(f[4]); c.sel = Trim(f[5]) == L"1";
                    comps->push_back(c); parsed = true;
                }
            }
        }
        return [comps, ok, parsed] {
            g_mLoading = false;
            if (ok && parsed) { g_comps = *comps; g_mError = false; }
            else { g_mError = true; g_mErrText = ok ? L"Список компонентов пуст или повреждён." : L"Нет связи с GitHub. Проверьте интернет."; }
            Redraw();
        };
    });
}

static void StartInstall() {
    if (g_running) return;
    g_running = true; g_phase = 1; g_log.clear(); g_scroll = 1e9f; g_cancel = false; Redraw();
    wstring dir = g_dir; bool desk = g_desk, start = g_start;
    Async([dir, desk, start]() -> std::function<void()> {
        bool ok = DoInstall(dir, desk, start);
        return [ok] { g_running = false; g_phase = ok ? 2 : 3; Redraw(); };
    });
}
static void StartUninstall() {
    if (g_running) return;
    g_running = true; g_phase = 1; g_log.clear(); Redraw();
    Async([]() -> std::function<void()> {
        bool ok = DoUninstall();
        return [ok] { g_running = false; g_phase = ok ? 2 : 3; Redraw(); };
    });
}

static wstring BrowseFolder(const wstring& title) {
    BROWSEINFOW bi{}; bi.hwndOwner = g_hwnd; bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return L"";
    wchar_t p[MAX_PATH] = L""; bool ok = SHGetPathFromIDListW(id, p); CoTaskMemFree(id);
    return ok ? wstring(p) : L"";
}

static void DrawBackground(float W, float H) {
    LinearGradientBrush bg(RectF(0, 0, W, H + 1), P.bg1, P.bg2, LinearGradientModeVertical);
    ui.g->FillRectangle(&bg, RectF(0, 0, W, H));
    auto glow = [&](float cx, float cy, float r, Color c, int a) {
        GraphicsPath p; p.AddEllipse(cx - r, cy - r, r * 2, r * 2);
        PathGradientBrush b(&p); b.SetCenterColor(Alpha(c, a));
        Color s = Alpha(c, 0); int n = 1; b.SetSurroundColors(&s, &n); ui.g->FillPath(&b, &p);
    };
    glow(W * 0.9f, -30, 440, P.accent2, g_dark ? 66 : 46);
    glow(-30, H * 0.4f, 420, P.accent, g_dark ? 52 : 40);
}
static void DrawTitleBar(float W) {
    if (g_logo) ui.g->DrawImage(g_logo, RectF(20, 12, 30, 30));
    Text(g_uninst ? L"Удаление MagicTool" : L"Установка MagicTool Suite", RectF(60, 10, 360, 22), FontUI(16, 2), P.text, T_LEFT | T_VCENTER);
    Text(L"Онлайн-установщик — качает выбранное с GitHub", RectF(61, 31, 360, 14), FontUI(10, 1), P.accent, T_LEFT | T_VCENTER);
    float bw = 46, x = W - bw;
    IconButton(RectF(x, 0, bw, 34), Ico::Close, [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }, P.text, 11, true); x -= bw;
    IconButton(RectF(x, 0, bw, 34), Ico::Min, [] { ShowWindow(g_hwnd, SW_MINIMIZE); }, P.text, 11);
}
static float Loading2(float x, float y, float w, const wstring& t) {
    Spinner(x + 14, y + 20, 10, P.accent);
    Text(t, RectF(x + 36, y + 6, w - 40, 28), FontUI(14), P.muted, T_LEFT | T_VCENTER);
    return 48;
}
static void DrawProgress(float x, float& y, float w) {
    if (g_prog < 0) return;
    RectF bar(x, y, w, 10);
    FillRound(bar, 5, P.surface);
    if (g_prog > 0) { RectF fl(x, y, w * std::min(1.0f, g_prog), 10); GradientH(fl, 5, P.accent, P.accent2); }
    y += 22;
}
static void DrawLog(float x, float& y, float w, float h) {
    RectF box(x, y, w, h);
    FillRound(box, 12, g_dark ? Color(255, 12, 14, 19) : Color(255, 20, 24, 32));
    RectF inner(box.X + 16, box.Y + 14, box.Width - 32, h - 28);
    Font* mf = FontMono(11.5f); float lh = 17;
    int vis = (int)(inner.Height / lh), total = (int)g_log.size(), start = std::max(0, total - vis);
    float ty = inner.Y;
    for (int i = start; i < total; ++i) {
        Color c = IContains(g_log[i], L"ОШИБ") ? P.danger : Color(255, 200, 210, 220);
        Text(g_log[i], RectF(inner.X, ty, inner.Width, lh), mf, c, T_LEFT | T_VCENTER | T_ELLIPSIS);
        ty += lh;
    }
    y += h + 16;
}
static float Banner2(float x, float y, float w, const wstring& title, const wstring& sub) {
    RectF r(x, y, w, 64);
    FillRound(r, 12, SevSoft(Sev::Bad)); StrokeRound(r, 12, Alpha(SevColor(Sev::Bad), 70));
    SevIcon(Sev::Bad, x + 16, y + 18, 28);
    Text(title, RectF(x + 56, y + 12, w - 72, 20), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER);
    Text(sub, RectF(x + 56, y + 34, w - 72, 18), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
    return 80;
}

static float Body(float x, float y, float w) {
    if (g_phase == 1) {
        y += Loading2(x, y, w, g_uninst ? L"Удаляю…" : (g_status.empty() ? L"Работаю…" : g_status));
        DrawProgress(x, y, w);
        DrawLog(x, y, w, 290);
        return y;
    }
    if (g_phase == 2) {
        RectF ic(x + w / 2 - 34, y + 10, 68, 68);
        GradientH(ic, 16, P.accent, P.accent2); Glyph(Ico::Check, ic, 34, Color(255, 255, 255, 255));
        Text(g_uninst ? L"MagicTool удалён" : L"Готово!", RectF(x, y + 92, w, 28), FontUI(20, 2), P.text, T_CENTER | T_VCENTER);
        Text(g_uninst ? L"Папка программы будет удалена через пару секунд." : L"Ярлыки созданы на Рабочем столе и в меню «Пуск».",
             RectF(x, y + 124, w, 20), FontUI(13), P.muted, T_CENTER | T_VCENTER);
        y += 164;
        float bw = 260, bx = x + (w - bw) / 2;
        if (!g_uninst) {
            Button(RectF(bx, y, bw, 42), L"Закрыть", [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }, Btn::Primary);
            y += 52;
        } else {
            Button(RectF(bx, y, bw, 42), L"Закрыть", [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }, Btn::Secondary);
            y += 52;
        }
        return y;
    }
    if (g_phase == 3) {
        y += Banner2(x, y, w, L"Не удалось завершить", L"Подробности в журнале ниже.");
        DrawLog(x, y, w, 240);
        Button(RectF(x, y, 160, 42), L"Закрыть", [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }, Btn::Secondary);
        return y + 52;
    }

    if (g_uninst) {
        RectF r(x, y, w, 96);
        FillRound(r, 12, P.surface);
        Glyph(Ico::Warning, RectF(r.X + 20, r.Y + 34, 28, 28), 18, P.warn);
        Text(L"Удалить MagicTool с этого компьютера?", RectF(r.X + 64, r.Y + 20, w - 80, 22), FontUI(15, 1), P.text, T_LEFT | T_VCENTER);
        Text(L"Будут удалены файлы, ярлыки и запись в «Программах и компонентах».", RectF(r.X + 64, r.Y + 48, w - 80, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
        y += 112;
        Button(RectF(x, y, w, 48), L"Удалить MagicTool", [] { StartUninstall(); }, Btn::Danger, Ico::Delete);
        return y + 56;
    }

    y += SectionTitle(x, y, w, L"Что установить", L"Отметьте программы — установщик скачает только их с GitHub.");
    if (g_mLoading) { y += Loading2(x, y, w, L"Загружаю список компонентов…"); return y + 8; }
    if (g_mError) {
        y += Banner2(x, y, w, L"Не удалось получить список", g_mErrText);
        Button(RectF(x, y, 180, 42), L"Повторить", [] { LoadManifest(); }, Btn::Secondary, Ico::Refresh);
        return y + 52;
    }
    for (size_t i = 0; i < g_comps.size(); i++) {
        Comp& c = g_comps[i];
        RectF r(x, y, w, 64); bool hv = Hover(r);
        FillRound(r, 10, c.sel ? Blend(P.accent, P.surface, g_dark ? 0.14f : 0.07f) : hv ? P.surfaceHover : P.surface);
        if (c.sel) StrokeRound(r, 10, Alpha(P.accent, 110));
        CheckBox(RectF(r.X + 16, r.Y + 22, 20, 20), c.sel);
        Text(c.name, RectF(r.X + 48, r.Y + 10, w - 170, 20), FontUI(14, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        Text(c.desc, RectF(r.X + 48, r.Y + 32, w - 170, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
        Text(Fmt(L"%.1f МБ", c.size / 1048576.0), RectF(r.GetRight() - 96, r.Y, 84, 64), FontUI(12), P.muted, T_RIGHT | T_VCENTER);
        bool* p = &c.sel; OnClick(r, [p] { *p = !*p; Redraw(); });
        y += 72;
    }
    y += 4;
    y += SectionTitle(x, y, w, L"Папка установки");
    {
        RectF r(x, y, w - 150, 44);
        FillRound(r, 10, P.surface); StrokeRound(r, 10, P.border);
        Text(g_dir, RectF(r.X + 14, r.Y, r.Width - 24, 44), FontUI(13), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        Button(RectF(x + w - 140, y, 140, 44), L"Обзор…", [] { wstring d = BrowseFolder(L"Куда установить"); if (!d.empty()) { g_dir = d + L"\\MagicTool"; Redraw(); } }, Btn::Secondary, Ico::Folder);
        y += 58;
    }
    y += SectionTitle(x, y, w, L"Ярлыки");
    auto chk = [&](bool& v, const wstring& label) {
        RectF r(x, y, w, 46); bool hv = Hover(r);
        FillRound(r, 10, v ? Blend(P.accent, P.surface, g_dark ? 0.14f : 0.07f) : hv ? P.surfaceHover : P.surface);
        if (v) StrokeRound(r, 10, Alpha(P.accent, 110));
        CheckBox(RectF(r.X + 16, r.Y + 13, 20, 20), v);
        Text(label, RectF(r.X + 48, r.Y, w - 60, 46), FontUI(13.5f), P.text, T_LEFT | T_VCENTER);
        bool* p = &v; OnClick(r, [p] { *p = !*p; Redraw(); });
        y += 54;
    };
    chk(g_desk, L"Ярлык на Рабочем столе");
    chk(g_start, L"Ярлык в меню «Пуск»");
    y += 8;
    bool any = false; for (auto& c : g_comps) if (c.sel) any = true;
    Button(RectF(x, y, w, 50), L"Установить", [] { StartInstall(); }, Btn::Primary, Ico::Download, any);
    return y + 60;
}

static void Render(Graphics& g, float W, float H) {
    BeginFrame(&g, W, H);
    DrawBackground(W, H);
    DrawTitleBar(W);
    float cx = std::max(28.0f, (W - 660) / 2), cw = std::min(W - 56, 660.0f);
    RectF view(0, TITLE_H + 6, W, H - TITLE_H - 10);
    BeginScroll(view, &g_scroll);
    float bottom = Body(cx, view.Y + 16, cw);
    EndScroll(bottom - view.Y + 24);
    DrawToasts();
    DrawModal();
    EndFrame();
}

static HDC g_memDC = nullptr; static HBITMAP g_dib = nullptr; static int g_dibW = 0, g_dibH = 0; static bool g_animTimer = false;
static int HitAt(float x, float y) {
    for (int i = (int)ui.hits.size() - 1; i >= 0; --i) { auto& h = ui.hits[i]; if (h.layer == ui.activeLayer && x >= h.r.X && x < h.r.GetRight() && y >= h.r.Y && y < h.r.GetBottom()) return i; }
    return -1;
}
static void UpdateDpi(HWND h) { UINT d = GetDpiForWindow(h); g_dpi = d ? d / 96.0f : 1.0f; }
static void Paint(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    int w = std::max(1L, rc.right), hh = std::max(1L, rc.bottom);
    if (w != g_dibW || hh != g_dibH || !g_memDC) {
        if (g_memDC) DeleteDC(g_memDC); if (g_dib) DeleteObject(g_dib);
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits; g_dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); g_memDC = CreateCompatibleDC(dc); SelectObject(g_memDC, g_dib); g_dibW = w; g_dibH = hh;
    }
    { Graphics g(g_memDC); g.ScaleTransform(g_dpi, g_dpi); Render(g, w / g_dpi, hh / g_dpi); }
    BitBlt(dc, 0, 0, w, hh, g_memDC, 0, 0, SRCCOPY); EndPaint(h, &ps);
    if (ui.animating && !g_animTimer) { SetTimer(h, 2, 16, nullptr); g_animTimer = true; }
    if (!ui.animating && g_animTimer) { KillTimer(h, 2); g_animTimer = false; }
}
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_NCCALCSIZE: if (wp) { if (IsZoomed(h)) { auto p = (NCCALCSIZE_PARAMS*)lp; UINT d = GetDpiForWindow(h); int fx = GetSystemMetricsForDpi(SM_CXFRAME, d) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, d); int fy = GetSystemMetricsForDpi(SM_CYFRAME, d) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, d); p->rgrc[0].left += fx; p->rgrc[0].right -= fx; p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy; } return 0; } break;
        case WM_NCHITTEST: { POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt); RECT rc; GetClientRect(h, &rc); int b = (int)(6 * g_dpi);
            if (!IsZoomed(h)) { bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, bt = pt.y >= rc.bottom - b; if (t && l) return HTTOPLEFT; if (t && r) return HTTOPRIGHT; if (bt && l) return HTBOTTOMLEFT; if (bt && r) return HTBOTTOMRIGHT; if (l) return HTLEFT; if (r) return HTRIGHT; if (t) return HTTOP; if (bt) return HTBOTTOM; }
            float x = pt.x / g_dpi, y = pt.y / g_dpi; if (y < TITLE_H && HitAt(x, y) < 0 && !ModalOpen()) return HTCAPTION; return HTCLIENT; }
        case WM_GETMINMAXINFO: { auto mm = (MINMAXINFO*)lp; mm->ptMinTrackSize.x = (LONG)(620 * g_dpi); mm->ptMinTrackSize.y = (LONG)(560 * g_dpi); return 0; }
        case WM_DPICHANGED: { g_dpi = HIWORD(wp) / 96.0f; RECT* r = (RECT*)lp; SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); Redraw(); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: Paint(h); return 0;
        case WM_SIZE: Redraw(); return 0;
        case WM_MOUSEMOVE: { ui.mx = GET_X_LPARAM(lp) / g_dpi; ui.my = GET_Y_LPARAM(lp) / g_dpi; if (ui.dragPos) { *ui.dragPos = std::max(0.0f, ui.dragStartPos + (ui.my - ui.dragStartY) * ui.dragRatio); Redraw(); return 0; } TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 }; TrackMouseEvent(&tme); Redraw(); return 0; }
        case WM_MOUSELEAVE: ui.mx = ui.my = -1; Redraw(); return 0;
        case WM_LBUTTONDOWN: { ui.mx = GET_X_LPARAM(lp) / g_dpi; ui.my = GET_Y_LPARAM(lp) / g_dpi; ui.lmb = true; SetCapture(h);
            for (int i = (int)ui.scrolls.size() - 1; i >= 0; --i) { auto& s = ui.scrolls[i]; if (s.layer != ui.activeLayer || s.content <= s.view.Height + 1) continue;
                if (ui.mx >= s.view.GetRight() - 14 && ui.mx < s.view.GetRight() && ui.my >= s.view.Y && ui.my < s.view.GetBottom()) { float trackH = s.view.Height - 8; float maxPos = s.content - s.view.Height; if (ui.my < s.barY || ui.my > s.barY + s.barH) *s.pos = std::max(0.0f, std::min(maxPos, (ui.my - s.view.Y - s.barH / 2) / std::max(1.0f, trackH - s.barH) * maxPos)); ui.dragPos = s.pos; ui.dragStartY = ui.my; ui.dragStartPos = *s.pos; ui.dragRatio = maxPos / std::max(1.0f, trackH - s.barH); Redraw(); return 0; } }
            int hit = HitAt(ui.mx, ui.my); ui.pressedHit = hit; if (hit >= 0) ui.pressedRect = ui.hits[hit].r; if (hit < 0 || ui.hits[hit].cursor != 1) ui.focusId = 0; Redraw(); return 0; }
        case WM_LBUTTONUP: { ui.lmb = false; ReleaseCapture(); if (ui.dragPos) { ui.dragPos = nullptr; ui.pressedHit = -1; Redraw(); return 0; } float x = GET_X_LPARAM(lp) / g_dpi, y = GET_Y_LPARAM(lp) / g_dpi; int hit = HitAt(x, y); if (hit >= 0 && ui.pressedHit >= 0 && ui.hits[hit].r.Equals(ui.pressedRect)) { auto fn = ui.hits[hit].fn; if (fn) fn(); } ui.pressedHit = -1; Redraw(); return 0; }
        case WM_MOUSEWHEEL: { POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt); float x = pt.x / g_dpi, y = pt.y / g_dpi; int d = GET_WHEEL_DELTA_WPARAM(wp); for (int i = (int)ui.scrolls.size() - 1; i >= 0; --i) { auto& s = ui.scrolls[i]; if (s.layer != ui.activeLayer) continue; if (x >= s.view.X && x < s.view.GetRight() && y >= s.view.Y && y < s.view.GetBottom() && s.content > s.view.Height) { *s.pos = std::max(0.0f, std::min(*s.pos - d / 120.0f * 80.0f, s.content - s.view.Height)); break; } } Redraw(); return 0; }
        case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) { int hit = HitAt(ui.mx, ui.my); SetCursor(LoadCursorW(nullptr, hit < 0 ? IDC_ARROW : ui.hits[hit].cursor == 1 ? IDC_IBEAM : IDC_HAND)); return TRUE; } break;
        case WM_TIMER: Redraw(); return 0;
        case WM_APP + 1: DrainUI(); Redraw(); return 0;
        case WM_CLOSE: if (g_running) { g_cancel = true; return 0; } DestroyWindow(h); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi; ULONG_PTR gtok; GdiplusStartup(&gtok, &gsi, nullptr);

    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) if (wstring(argv[i]) == L"/uninstall") g_uninst = true;

    g_dir = g_uninst ? ExeDir() : KnownFolder(FOLDERID_ProgramFiles) + L"\\MagicTool";
    g_logo = LoadPngResource(101);
    DWORD lt = 1; RegGetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", lt);
    SetTheme(lt == 0);

    WNDCLASSEXW wc{ sizeof(wc) }; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0); wc.lpszClassName = L"MagicSetupWindow"; RegisterClassExW(&wc);

    HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY); MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96; if (HMODULE sh = LoadLibraryW(L"shcore.dll")) { typedef HRESULT(WINAPI * F)(HMONITOR, int, UINT*, UINT*); if (auto f = (F)GetProcAddress(sh, "GetDpiForMonitor")) f(mon, 0, &dx, &dy); }
    g_dpi = dx / 96.0f;
    int ww = (int)(740 * g_dpi), wh = (int)(700 * g_dpi);
    RECT wa = mi.rcWork; int wx = wa.left + ((wa.right - wa.left) - ww) / 2, wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"MagicTool Setup", WS_OVERLAPPEDWINDOW, wx, wy, ww, wh, nullptr, nullptr, inst, nullptr);
    g_hwnd = h; SetUIWindow(h); UpdateDpi(h);
    MARGINS mg{ 0, 0, 1, 0 }; DwmExtendFrameIntoClientArea(h, &mg);
    BOOL dk = g_dark; DwmSetWindowAttribute(h, 20, &dk, sizeof(dk));
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(h, show); UpdateWindow(h);

    if (!g_uninst) LoadManifest();

    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    GdiplusShutdown(gtok);
    return 0;
}
