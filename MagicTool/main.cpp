#include "common.h"
#include "ui.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <commdlg.h>

using namespace Gdiplus;

static const float TITLE_H = 54;

static HWND g_hwnd = nullptr;
static float g_dpi = 1.0f;
static Bitmap* g_logo = nullptr;
static int g_hoverHit = -1;

struct UsbDisk { int number = -1; long gb = 0; wstring name; };
static vector<UsbDisk> g_disks;
static bool g_disksLoading = false;
static int g_sel = -1;
static vector<wstring> g_isos;

static bool g_edPro = true, g_edHome = true, g_edEdu = false, g_edEnt = false;
static int g_compress = 0;
static vector<wstring> g_log;
static bool g_running = false;
static float g_scroll = 0;
static float g_scrollAdmin = 0;
static int g_tab = 0;
static std::atomic<bool> g_cancel{ false };
static wstring g_script;
static bool g_adk = false;

void Redraw() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

static Bitmap* LoadPngResource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return nullptr;
    HGLOBAL g = LoadResource(nullptr, r);
    IStream* s = SHCreateMemStream((const BYTE*)LockResource(g), SizeofResource(nullptr, r));
    if (!s) return nullptr;
    Bitmap* b = Bitmap::FromStream(s);
    s->Release();
    if (b && b->GetLastStatus() != Ok) { delete b; b = nullptr; }
    return b;
}

static wstring FindScript() {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wstring dir = exe; size_t p = dir.find_last_of(L"\\/"); if (p != wstring::npos) dir = dir.substr(0, p);
    for (int i = 0; i < 6; ++i) {
        wstring c = dir + L"\\WinInstaller\\make-usb.ps1";
        if (FileExists(c)) return c;
        if (FileExists(dir + L"\\make-usb.ps1")) return dir + L"\\make-usb.ps1";
        size_t q = dir.find_last_of(L"\\/"); if (q == wstring::npos) break; dir = dir.substr(0, q);
    }
    return L"";
}

static bool AdkPresent() {
    wchar_t pf[MAX_PATH] = L"";
    ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%", pf, MAX_PATH);
    wstring pe = wstring(pf) + L"\\Windows Kits\\10\\Assessment and Deployment Kit\\Windows Preinstallation Environment\\amd64\\en-us\\winpe.wim";
    if (FileExists(pe)) return true;

    return FileExists(L"C:\\PCInstallerADK\\Windows Kits\\10\\Assessment and Deployment Kit\\Windows Preinstallation Environment\\amd64\\en-us\\winpe.wim");
}

static void Log(const wstring& s) {
    PostUI([s] { g_log.push_back(s); if (g_log.size() > 4000) g_log.erase(g_log.begin(), g_log.begin() + 500); g_scroll = 1e9f; Redraw(); });
}

static void RefreshDisks() {
    if (g_disksLoading) return;
    g_disksLoading = true;
    Redraw();
    Async([]() -> std::function<void()> {
        auto out = std::make_shared<vector<UsbDisk>>();
        const wchar_t* ps =
            L"Get-Disk | Where-Object { $_.BusType -eq 'USB' } | ForEach-Object { '{0}|{1}|{2}' -f $_.Number, [math]::Round($_.Size/1GB), $_.FriendlyName }";
        RunPS(ps, [&](const wstring& line) {
            auto parts = Split(Trim(line), L'|');
            if (parts.size() >= 3) { UsbDisk d; d.number = _wtoi(parts[0].c_str()); d.gb = _wtol(parts[1].c_str()); d.name = Trim(parts[2]); out->push_back(d); }
        }, nullptr);
        return [out] {
            g_disks = *out;
            g_disksLoading = false;
            if (g_sel >= 0) { bool ok = false; for (auto& d : g_disks) if (d.number == g_sel) ok = true; if (!ok) g_sel = -1; }
            Redraw();
        };
    });
}

static void PickIso() {
    wchar_t buf[8192] = L"";
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Образы Windows (*.iso)\0*.iso\0Все файлы\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 8192;
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    vector<wstring> res;
    wstring dir = buf;
    wchar_t* p = buf + dir.size() + 1;
    if (*p == 0) { res.push_back(dir); }
    else { while (*p) { res.push_back(dir + L"\\" + p); p += wcslen(p) + 1; } }
    g_isos = res;
    Redraw();
}

static wstring Q(const wstring& s) { return L"\"" + s + L"\""; }

static void StartBuild() {
    if (g_running) return;
    if (g_sel < 0) { Message(L"Выберите флешку", L"Отметьте флешку в списке выше.", Sev::Warn); return; }
    if (g_script.empty()) { Message(L"Не найден make-usb.ps1", L"Положите MagicTool рядом с папкой WinInstaller.", Sev::Bad); return; }
    wstring name = L"диск " + std::to_wstring(g_sel);
    long gb = 0; for (auto& d : g_disks) if (d.number == g_sel) { name = d.name; gb = d.gb; }
    Confirm(L"Сделать загрузочную флешку?",
            L"ВСЕ ДАННЫЕ на «" + name + L"» (" + std::to_wstring(gb) + L" ГБ) будут стёрты!\n\n"
            L"На флешку запишутся WinPE-установщик Windows, TOOLBOX PC и программы. Это займёт 20–60 минут. Не вынимайте флешку.",
            L"Стереть и записать", [name] {
        g_running = true; g_cancel = false; g_log.clear(); g_scroll = 1e9f; Redraw();
        int disk = g_sel; wstring script = g_script; vector<wstring> isos = g_isos;

        wstring eds;
        auto addEd = [&](bool on, const wchar_t* id) { if (on) { if (!eds.empty()) eds += L","; eds += id; } };
        addEd(g_edPro, L"Professional"); addEd(g_edHome, L"Core"); addEd(g_edEdu, L"Education"); addEd(g_edEnt, L"Enterprise");
        if (eds.empty()) eds = L"Professional,Core";
        wstring comp = g_compress == 1 ? L"max" : L"recovery";
        Async([disk, script, isos, eds, comp]() -> std::function<void()> {
            wstring cmd = L"powershell -NoProfile -ExecutionPolicy Bypass -File " + Q(script) +
                          L" -DiskNumber " + std::to_wstring(disk) + L" -Yes -Editions " + eds + L" -Compress " + comp;
            if (!isos.empty()) {
                wstring list; for (auto& i : isos) { if (!list.empty()) list += L","; list += i; }
                cmd += L" -Iso " + Q(list);
            }
            Log(L"> " + cmd);
            int code = RunCapture(cmd, [](const wstring& l) { Log(l[0] == L'\r' ? l.substr(1) : l); }, Enc::Utf8, &g_cancel);
            return [code] {
                g_running = false;
                if (code == 0) Toast(L"Готово! Флешка записана.", Sev::Ok);
                else Toast(Fmt(L"Завершено с кодом %d — см. журнал", code), Sev::Warn);
                Redraw();
            };
        });
    }, true);
}

static float Loading(float x, float y, float w, const wstring& text) {
    Spinner(x + 14, y + 20, 10, P.accent);
    Text(text, RectF(x + 36, y + 6, w - 40, 28), FontUI(14), P.muted, T_LEFT | T_VCENTER);
    return 48;
}
static float Banner(float x, float y, float w, Sev s, const wstring& title, const wstring& text,
                    const wstring& action = L"", std::function<void()> fn = nullptr) {
    float bw = action.empty() ? 0 : ButtonW(action) + 16;
    float tw = w - 70 - bw;
    float th = text.empty() ? 0 : TextH(text, FontUI(12.5f), tw);
    float h = std::max(58.0f, 20 + 20 + th + (text.empty() ? 0 : 4));
    RectF r(x, y, w, h);
    FillRound(r, 12, SevSoft(s));
    StrokeRound(r, 12, Alpha(SevColor(s), 60));
    SevIcon(s, x + 16, y + (h - 28) / 2, 28);
    float ty = text.empty() ? y + (h - 20) / 2 : y + 11;
    Text(title, RectF(x + 56, ty, tw, 20), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
    if (!text.empty()) Text(text, RectF(x + 56, ty + 22, tw, th + 4), FontUI(12.5f), P.muted, T_WRAP);
    if (!action.empty()) Button(RectF(x + w - bw, y + (h - 34) / 2, bw - 14, 34), action, fn, Btn::Secondary);
    return h;
}

static void DrawTechArt(float W, float H) {
    Graphics& g = *ui.g;
    Color c1 = Alpha(P.accent, g_dark ? 46 : 60);
    Color c2 = Alpha(P.accent2, g_dark ? 46 : 60);
    Pen pen(c1, g_dark ? 2.2f : 2.4f); pen.SetLineJoin(LineJoinRound);
    Pen pen2(c2, g_dark ? 2.2f : 2.4f); pen2.SetLineJoin(LineJoinRound);

    float bx = W - 330; if (bx < W * 0.46f) bx = W * 0.46f;
    float by = H - 250;

    {
        float cx = bx + 150, cy = by + 70, r = 300;
        GraphicsPath gp; gp.AddEllipse(cx - r, cy - r, r * 2, r * 2);
        PathGradientBrush gb(&gp);
        gb.SetCenterColor(Alpha(P.accent, g_dark ? 26 : 34));
        Color sur = Alpha(P.accent, 0); int n = 1; gb.SetSurroundColors(&sur, &n);
        g.FillPath(&gb, &gp);
    }

    GraphicsPath mon; RoundPath(mon, RectF(bx, by, 200, 128), 12);
    { LinearGradientBrush sb(RectF(bx, by, 200, 128), Alpha(P.accent, g_dark ? 16 : 24), Alpha(P.accent2, g_dark ? 10 : 16), LinearGradientModeForwardDiagonal); g.FillPath(&sb, &mon); }
    g.DrawPath(&pen, &mon);

    GraphicsPath usb; RoundPath(usb, RectF(bx + 84, by + 40, 32, 58), 6); g.DrawPath(&pen2, &usb);
    g.DrawLine(&pen2, bx + 84, by + 50, bx + 116, by + 50);
    g.DrawLine(&pen2, bx + 93, by + 40, bx + 93, by + 28);
    g.DrawLine(&pen2, bx + 107, by + 40, bx + 107, by + 28);
    g.DrawLine(&pen2, bx + 88, by + 28, bx + 112, by + 28);

    g.DrawLine(&pen, bx + 100, by + 128, bx + 100, by + 150);
    GraphicsPath base; RoundPath(base, RectF(bx + 72, by + 150, 56, 9), 4); g.DrawPath(&pen, &base);

    GraphicsPath tower; RoundPath(tower, RectF(bx + 224, by - 14, 76, 182), 12);
    { LinearGradientBrush tb(RectF(bx + 224, by - 14, 76, 182), Alpha(P.accent, g_dark ? 14 : 20), Alpha(P.accent2, g_dark ? 8 : 12), LinearGradientModeVertical); g.FillPath(&tb, &tower); }
    g.DrawPath(&pen, &tower);
    for (int i = 0; i < 3; i++) g.DrawLine(&pen, bx + 242, by + 4 + i * 11, bx + 282, by + 4 + i * 11);
    SolidBrush pw(c2); g.FillEllipse(&pw, bx + 256, by + 46, 9.0f, 9.0f);

    Pen pct(Alpha(P.accent, g_dark ? 40 : 55), 1.7f); pct.SetLineJoin(LineJoinRound);
    float ly = by + 198;
    PointF trace[] = { PointF(bx - 70, ly), PointF(bx + 40, ly), PointF(bx + 70, ly + 34), PointF(bx + 170, ly + 34) };
    g.DrawLines(&pct, trace, 4);
    g.DrawLine(&pct, bx + 110, by - 40, bx + 110, by - 80);
    SolidBrush nd(Alpha(P.accent, g_dark ? 70 : 95));
    for (auto& p : { trace[0], trace[3], PointF(bx + 110, by - 80) }) g.FillEllipse(&nd, p.X - 4, p.Y - 4, 8.0f, 8.0f);
}

static void DrawBackground(float W, float H) {
    LinearGradientBrush bg(RectF(0, 0, W, H + 1), P.bg1, P.bg2, LinearGradientModeVertical);
    ui.g->FillRectangle(&bg, RectF(0, 0, W, H));

    auto glow = [&](float cx, float cy, float r, Color c, int a) {
        GraphicsPath p; p.AddEllipse(cx - r, cy - r, r * 2, r * 2);
        PathGradientBrush b(&p);
        b.SetCenterColor(Alpha(c, a));
        Color sur = Alpha(c, 0); int n = 1; b.SetSurroundColors(&sur, &n);
        ui.g->FillPath(&b, &p);
    };
    glow(W * 0.88f, -30, 460, P.accent2, g_dark ? 70 : 52);
    glow(-40, H * 0.35f, 420, P.accent, g_dark ? 55 : 44);
    glow(W * 0.35f, H + 60, 480, P.accent2, g_dark ? 40 : 34);

    DrawTechArt(W, H);
}

static void DrawTitleBar(float W) {
    if (g_logo) ui.g->DrawImage(g_logo, RectF(20, 12, 30, 30));
    Text(L"MagicTool", RectF(60, 10, 240, 22), FontUI(16, 2), P.text, T_LEFT | T_VCENTER);
    Text(L"Загрузочная флешка Windows одной кнопкой", RectF(61, 31, 320, 14), FontUI(10, 1), P.accent, T_LEFT | T_VCENTER);

    float bw = 46, bh = 34, x = W - bw;
    bool maxed = IsZoomed(g_hwnd);
    IconButton(RectF(x, 0, bw, bh), Ico::Close, [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }, P.text, 11, true); x -= bw;
    IconButton(RectF(x, 0, bw, bh), maxed ? Ico::Restore : Ico::Max, [] { ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); }, P.text, 11); x -= bw;
    IconButton(RectF(x, 0, bw, bh), Ico::Min, [] { ShowWindow(g_hwnd, SW_MINIMIZE); }, P.text, 11); x -= 12;
    x -= 36;
    IconButton(RectF(x, 9, 36, 36), g_dark ? Ico::Sun : Ico::Moon, [] {
        SetTheme(!g_dark); Settings::Set(L"theme", g_dark ? L"dark" : L"light");
        BOOL d = g_dark; DwmSetWindowAttribute(g_hwnd, 20, &d, sizeof(d)); Redraw();
    }, P.text, 15);
    x -= 10;
    x -= Badge(x, 16, IsElevated() ? L"Администратор" : L"Нужны права администратора", IsElevated() ? Sev::Ok : Sev::Warn, 24, true);
}

static void LaunchTool(const wchar_t* exe, const wchar_t* args) {
    ShellOpen(exe, args ? args : L"");
}

struct AdminTool { const wchar_t* name; const wchar_t* exe; const wchar_t* args; const wchar_t* icon; };
static float AdminBody(float x, float y, float w) {
    y += Banner(x, y, w, Sev::Info, L"Инструменты системного администратора",
                L"Быстрый запуск стандартных оснасток и консолей Windows — все открываются с правами администратора.") + 14;
    static const AdminTool tools[] = {
        { L"Управление компьютером", L"compmgmt.msc", L"", Ico::Monitor },
        { L"Службы", L"services.msc", L"", Ico::Gears },
        { L"Диспетчер устройств", L"devmgmt.msc", L"", Ico::Chip },
        { L"Управление дисками", L"diskmgmt.msc", L"", Ico::Disk },
        { L"Просмотр событий", L"eventvwr.msc", L"", Ico::Info },
        { L"Планировщик заданий", L"taskschd.msc", L"", Ico::Gears },
        { L"Локальные пользователи", L"lusrmgr.msc", L"", Ico::Admin },
        { L"Групповая политика", L"gpedit.msc", L"", Ico::Sliders },
        { L"Политика безопасности", L"secpol.msc", L"", Ico::Shield },
        { L"Брандмауэр (доп.)", L"wf.msc", L"", Ico::Shield },
        { L"Редактор реестра", L"regedit.exe", L"", Ico::Wrench },
        { L"Конфигурация системы", L"msconfig.exe", L"", Ico::Sliders },
        { L"Монитор ресурсов", L"resmon.exe", L"", Ico::Speed },
        { L"Диспетчер задач", L"taskmgr.exe", L"", Ico::Chart },
        { L"Командная строка", L"cmd.exe", L"", Ico::Apps },
        { L"PowerShell", L"powershell.exe", L"", Ico::Apps },
        { L"Сетевые подключения", L"control.exe", L"ncpa.cpl", Ico::Globe },
        { L"Программы и компоненты", L"control.exe", L"appwiz.cpl", Ico::Apps },
        { L"Свойства системы", L"control.exe", L"sysdm.cpl", Ico::Info },
        { L"Панель управления", L"control.exe", L"", Ico::Gears },
    };
    int n = (int)(sizeof(tools) / sizeof(tools[0]));
    int cols = w > 820 ? 3 : 2;
    float gap = 14, tw = (w - gap * (cols - 1)) / cols, th = 72;
    for (int i = 0; i < n; i++) {
        float tx = x + (i % cols) * (tw + gap), ty = y + (i / cols) * (th + gap);
        RectF r(tx, ty, tw, th);
        bool hv = Hover(r);
        Card(r, hv);
        RectF ib(r.X + 16, r.Y + (th - 36) / 2, 36, 36);
        GradientH(ib, 9, Alpha(P.accent, g_dark ? 70 : 42), Alpha(P.accent2, g_dark ? 70 : 42));
        Glyph(tools[i].icon, ib, 17, g_dark ? Color(255, 255, 255, 255) : P.accent2);
        Text(tools[i].name, RectF(r.X + 64, r.Y, tw - 76, th), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        const wchar_t* e = tools[i].exe; const wchar_t* a = tools[i].args;
        OnClick(r, [e, a] { LaunchTool(e, a); });
    }
    y += (float)((n + cols - 1) / cols) * (th + gap) + 8;
    return y;
}

static float FlashBody(float x, float y, float w) {

    if (!g_adk)
        y += Banner(x, y, w, Sev::Warn, L"Не найден Windows ADK + WinPE add-on",
                    L"Для сборки загрузочной флешки нужен Windows ADK с компонентом WinPE. Установите с сайта Microsoft (learn.microsoft.com → ADK).",
                    L"Открыть сайт", [] { ShellOpen(L"https://learn.microsoft.com/windows-hardware/get-started/adk-install"); }) + 14;
    if (g_script.empty())
        y += Banner(x, y, w, Sev::Bad, L"Не найден make-usb.ps1", L"Положите MagicTool рядом с папкой WinInstaller (в папке programm).") + 14;

    bool busy = g_running;

    y += SectionTitle(x, y, w, L"1. Выберите флешку", L"Показаны только USB-накопители. ВСЕ данные на выбранной флешке будут стёрты.");
    {
        float bx = ButtonRight(x + w, y - 44, 34, L"Обновить", [] { RefreshDisks(); }, Btn::Secondary, Ico::Refresh, !g_disksLoading && !busy);
        (void)bx;
    }
    if (g_disksLoading) y += Loading(x, y, w, L"Ищу USB-накопители…");
    else if (g_disks.empty()) y += Banner(x, y, w, Sev::Info, L"Флешки не найдены", L"Вставьте USB-флешку (от 16 ГБ) и нажмите «Обновить».") + 8;
    else {
        for (auto& d : g_disks) {
            RectF r(x, y, w, 60);
            bool sel = g_sel == d.number, hv = Hover(r);
            FillRound(r, 12, sel ? Blend(P.accent, P.surface, g_dark ? 0.18f : 0.10f) : hv ? P.surfaceHover : P.surface);
            if (sel) StrokeRound(r, 12, P.accent, 1.6f);
            RectF ic(r.X + 14, r.Y + 14, 32, 32);
            GradientH(ic, 9, Alpha(P.accent, g_dark ? 70 : 40), Alpha(P.accent2, g_dark ? 70 : 40));
            Glyph(Ico::Disk, ic, 16, g_dark ? Color(255, 255, 255, 255) : P.accent2);
            Text(d.name, RectF(r.X + 60, r.Y + 11, w - 160, 20), FontUI(14, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(Fmt(L"USB · %ld ГБ · диск %d", d.gb, d.number), RectF(r.X + 60, r.Y + 32, w - 160, 16), FontUI(12), P.muted, T_LEFT | T_VCENTER);
            if (sel) { Glyph(Ico::Check, RectF(r.GetRight() - 44, r.Y + 18, 24, 24), 15, P.accent); }
            int num = d.number;
            if (!busy) OnClick(r, [num] { g_sel = num; Redraw(); });
            y += 68;
        }
    }
    y += 8;

    y += SectionTitle(x, y, w, L"2. Образ Windows (ISO)", L"Укажите ISO Windows 11 и/или 10 с сайта Microsoft. Можно выбрать два — обе системы на одной флешке.");
    {
        ButtonRight(x + w, y - 44, 34, L"Выбрать ISO…", [] { PickIso(); }, Btn::Secondary, Ico::Open, !busy);
    }
    if (g_isos.empty())
        y += Banner(x, y, w, Sev::Info, L"ISO не выбран", L"Без ISO флешка соберётся только с WinPE — установить Windows с неё не получится, пока не добавите образ.") + 8;
    else {
        for (auto& iso : g_isos) {
            RectF r(x, y, w, 40);
            FillRound(r, 9, P.surface);
            Glyph(Ico::Download, RectF(r.X + 12, r.Y + 8, 22, 22), 13, P.accent);
            Text(iso, RectF(r.X + 44, r.Y, w - 60, 40), FontUI(12.5f), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            y += 46;
        }
        if (!busy) { Button(RectF(x, y, ButtonW(L"Очистить список"), 32), L"Очистить список", [] { g_isos.clear(); Redraw(); }, Btn::Soft); y += 42; }
    }
    y += 8;

    y += SectionTitle(x, y, w, L"3. Какие редакции Windows положить", L"Отметьте только нужные — так флешка не раздувается лишними версиями.");
    {
        struct E { const wchar_t* name; bool* on; };
        E eds[] = { { L"Windows Pro", &g_edPro }, { L"Домашняя (Home)", &g_edHome }, { L"Для образования", &g_edEdu }, { L"Корпоративная", &g_edEnt } };
        float colw = (w - 16) / 2, rh = 46;
        for (int i = 0; i < 4; i++) {
            float ex = x + (i % 2) * (colw + 16), ey = y + (i / 2) * (rh + 10);
            RectF r(ex, ey, colw, rh);
            bool on = *eds[i].on, hv = Hover(r);
            FillRound(r, 10, on ? Blend(P.accent, P.surface, g_dark ? 0.16f : 0.08f) : hv ? P.surfaceHover : P.surface);
            if (on) StrokeRound(r, 10, Alpha(P.accent, 110));
            CheckBox(RectF(ex + 14, ey + 13, 20, 20), on, !busy);
            Text(eds[i].name, RectF(ex + 46, ey, colw - 56, rh), FontUI(13.5f), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            bool* p = eds[i].on;
            if (!busy) OnClick(r, [p] { *p = !*p; Redraw(); });
        }
        y += 2 * (rh + 10) + 6;
    }

    y += SectionTitle(x, y, w, L"Сжатие образа Windows");
    {
        const wchar_t* opt[] = { L"Меньше места на флешке", L"Меньше памяти при сборке" };
        const wchar_t* sub[] = { L"компактный образ (рекомендуется)", L"нужно меньше ОЗУ, образ крупнее" };
        float colw = (w - 16) / 2, rh = 60;
        for (int i = 0; i < 2; i++) {
            RectF r(x + i * (colw + 16), y, colw, rh);
            bool on = g_compress == i, hv = Hover(r);
            FillRound(r, 10, on ? Blend(P.accent, P.surface, g_dark ? 0.16f : 0.08f) : hv ? P.surfaceHover : P.surface);
            if (on) StrokeRound(r, 10, Alpha(P.accent, 120));
            float cx = r.X + 24, cy = r.Y + rh / 2;
            StrokeRound(RectF(cx - 9, cy - 9, 18, 18), 9, on ? P.accent : P.border, 1.6f);
            if (on) FillRound(RectF(cx - 4, cy - 4, 8, 8), 4, P.accent);
            Text(opt[i], RectF(r.X + 46, r.Y + 11, colw - 56, 18), FontUI(13, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(sub[i], RectF(r.X + 46, r.Y + 31, colw - 56, 16), FontUI(11.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            int v = i; if (!busy) OnClick(r, [v] { g_compress = v; Redraw(); });
        }
        y += rh + 14;
    }

    y += Banner(x, y, w, Sev::Info, L"Драйверы уже встроены",
                L"В образ попадают драйверы сети и контроллеров дисков (Intel/AMD RST и VMD, LAN, Wi-Fi) из папки Drivers — интернет и диски видны сразу при установке Windows.") + 8;

    wstring btn = busy ? L"Идёт запись…" : L"Сделать загрузочную флешку";
    Button(RectF(x, y, w, 50), btn, [] { StartBuild(); }, Btn::Primary, Ico::Download, !busy && g_sel >= 0 && !g_script.empty());
    y += 62;

    if (!g_log.empty() || busy) {
        y += SectionTitle(x, y, w, L"Журнал");
        float boxH = 260;
        RectF box(x, y, w, boxH);
        FillRound(box, 12, g_dark ? Color(255, 12, 14, 19) : Color(255, 20, 24, 32));
        if (busy) Spinner(box.X + 20, box.Y + 22, 7, P.accent);
        Text(busy ? L"Выполняется…" : L"Готово", RectF(box.X + (busy ? 36 : 16), box.Y + 12, w - 60, 18), FontUI(12, 1), busy ? P.accent : P.muted, T_LEFT | T_VCENTER);
        RectF inner(box.X + 16, box.Y + 38, box.Width - 32, boxH - 50);
        Font* mf = FontMono(11.5f);
        float lh = 16;
        int visible = (int)(inner.Height / lh);
        int total = (int)g_log.size();
        int start = std::max(0, total - visible);
        float ty = inner.Y;
        for (int i = start; i < total; ++i) {
            Color c = IContains(g_log[i], L"ОШИБ") || IContains(g_log[i], L"error") ? P.danger : Color(255, 200, 210, 220);
            Text(g_log[i], RectF(inner.X, ty, inner.Width, lh), mf, c, T_LEFT | T_VCENTER | T_ELLIPSIS);
            ty += lh;
        }
        y += boxH + 16;
    }
    return y;
}

static void Render(Graphics& g, float W, float H) {
    BeginFrame(&g, W, H);
    DrawBackground(W, H);
    DrawTitleBar(W);
    float cx = std::max(24.0f, (W - 940) / 2);
    float cw = std::min(W - 48, 940.0f);

    const wchar_t* tabs[] = { L"Загрузочная флешка", L"Инструменты администратора" };
    float tx = cx;
    for (int i = 0; i < 2; i++) {
        float tabW = TextW(tabs[i], FontUI(13.5f, 1)) + 36;
        RectF r(tx, TITLE_H + 10, tabW, 38);
        if (g_tab == i) { GradientH(r, 10, P.accent, P.accent2); Text(tabs[i], r, FontUI(13.5f, 1), Color(255, 255, 255, 255), T_CENTER | T_VCENTER); }
        else { FillRound(r, 10, Hover(r) ? P.surfaceHover : Alpha(P.surface, 200)); Text(tabs[i], r, FontUI(13.5f, 1), P.text, T_CENTER | T_VCENTER); }
        int v = i; OnClick(r, [v] { g_tab = v; Redraw(); });
        tx += tabW + 8;
    }
    RectF view(0, TITLE_H + 56, W, H - TITLE_H - 58);
    BeginScroll(view, g_tab == 0 ? &g_scroll : &g_scrollAdmin);
    float bottom = g_tab == 0 ? FlashBody(cx, view.Y + 12, cw) : AdminBody(cx, view.Y + 12, cw);
    EndScroll(bottom - view.Y + 24);
    DrawToasts();
    DrawModal();
    EndFrame();
}

static HDC g_memDC = nullptr;
static HBITMAP g_dib = nullptr;
static int g_dibW = 0, g_dibH = 0;
static bool g_animTimer = false;

static int HitAt(float x, float y) {
    for (int i = (int)ui.hits.size() - 1; i >= 0; --i) {
        auto& h = ui.hits[i];
        if (h.layer == ui.activeLayer && x >= h.r.X && x < h.r.GetRight() && y >= h.r.Y && y < h.r.GetBottom()) return i;
    }
    return -1;
}
static void UpdateDpi(HWND h) { UINT dpi = GetDpiForWindow(h); g_dpi = dpi ? dpi / 96.0f : 1.0f; }

static void Paint(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    int w = std::max(1L, rc.right), hh = std::max(1L, rc.bottom);
    if (w != g_dibW || hh != g_dibH || !g_memDC) {
        if (g_memDC) DeleteDC(g_memDC);
        if (g_dib) DeleteObject(g_dib);
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits; g_dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        g_memDC = CreateCompatibleDC(dc); SelectObject(g_memDC, g_dib); g_dibW = w; g_dibH = hh;
    }
    { Graphics g(g_memDC); g.ScaleTransform(g_dpi, g_dpi); Render(g, w / g_dpi, hh / g_dpi); }
    BitBlt(dc, 0, 0, w, hh, g_memDC, 0, 0, SRCCOPY);
    EndPaint(h, &ps);
    if (ui.animating && !g_animTimer) { SetTimer(h, 2, 16, nullptr); g_animTimer = true; }
    if (!ui.animating && g_animTimer) { KillTimer(h, 2); g_animTimer = false; }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_NCCALCSIZE:
            if (wp) {
                if (IsZoomed(h)) {
                    auto p = (NCCALCSIZE_PARAMS*)lp; UINT dpi = GetDpiForWindow(h);
                    int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    p->rgrc[0].left += fx; p->rgrc[0].right -= fx; p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
            RECT rc; GetClientRect(h, &rc); int b = (int)(6 * g_dpi);
            if (!IsZoomed(h)) {
                bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, bt = pt.y >= rc.bottom - b;
                if (t && l) return HTTOPLEFT;  if (t && r) return HTTOPRIGHT;
                if (bt && l) return HTBOTTOMLEFT; if (bt && r) return HTBOTTOMRIGHT;
                if (l) return HTLEFT; if (r) return HTRIGHT; if (t) return HTTOP; if (bt) return HTBOTTOM;
            }
            float x = pt.x / g_dpi, y = pt.y / g_dpi;
            if (y < TITLE_H && HitAt(x, y) < 0 && !ModalOpen()) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: { auto mm = (MINMAXINFO*)lp; mm->ptMinTrackSize.x = (LONG)(720 * g_dpi); mm->ptMinTrackSize.y = (LONG)(560 * g_dpi); return 0; }
        case WM_DPICHANGED: { g_dpi = HIWORD(wp) / 96.0f; RECT* r = (RECT*)lp; SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); Redraw(); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: Paint(h); return 0;
        case WM_SIZE: Redraw(); return 0;
        case WM_MOUSEMOVE: {
            ui.mx = GET_X_LPARAM(lp) / g_dpi; ui.my = GET_Y_LPARAM(lp) / g_dpi;
            if (ui.dragPos) { *ui.dragPos = std::max(0.0f, ui.dragStartPos + (ui.my - ui.dragStartY) * ui.dragRatio); Redraw(); return 0; }
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 }; TrackMouseEvent(&tme);
            Redraw(); return 0;
        }
        case WM_MOUSELEAVE: ui.mx = ui.my = -1; g_hoverHit = -1; Redraw(); return 0;
        case WM_LBUTTONDOWN: {
            ui.mx = GET_X_LPARAM(lp) / g_dpi; ui.my = GET_Y_LPARAM(lp) / g_dpi; ui.lmb = true; SetCapture(h);
            for (int i = (int)ui.scrolls.size() - 1; i >= 0; --i) {
                auto& s = ui.scrolls[i];
                if (s.layer != ui.activeLayer || s.content <= s.view.Height + 1) continue;
                if (ui.mx >= s.view.GetRight() - 14 && ui.mx < s.view.GetRight() && ui.my >= s.view.Y && ui.my < s.view.GetBottom()) {
                    float trackH = s.view.Height - 8; float maxPos = s.content - s.view.Height;
                    if (ui.my < s.barY || ui.my > s.barY + s.barH)
                        *s.pos = std::max(0.0f, std::min(maxPos, (ui.my - s.view.Y - s.barH / 2) / std::max(1.0f, trackH - s.barH) * maxPos));
                    ui.dragPos = s.pos; ui.dragStartY = ui.my; ui.dragStartPos = *s.pos; ui.dragRatio = maxPos / std::max(1.0f, trackH - s.barH);
                    Redraw(); return 0;
                }
            }
            int hit = HitAt(ui.mx, ui.my); ui.pressedHit = hit;
            if (hit >= 0) ui.pressedRect = ui.hits[hit].r;
            if (hit < 0 || ui.hits[hit].cursor != 1) ui.focusId = 0;
            Redraw(); return 0;
        }
        case WM_LBUTTONUP: {
            ui.lmb = false; ReleaseCapture();
            if (ui.dragPos) { ui.dragPos = nullptr; ui.pressedHit = -1; Redraw(); return 0; }
            float x = GET_X_LPARAM(lp) / g_dpi, y = GET_Y_LPARAM(lp) / g_dpi; int hit = HitAt(x, y);
            if (hit >= 0 && ui.pressedHit >= 0 && ui.hits[hit].r.Equals(ui.pressedRect)) { auto fn = ui.hits[hit].fn; if (fn) fn(); }
            ui.pressedHit = -1; Redraw(); return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
            float x = pt.x / g_dpi, y = pt.y / g_dpi; int d = GET_WHEEL_DELTA_WPARAM(wp);
            for (int i = (int)ui.scrolls.size() - 1; i >= 0; --i) {
                auto& s = ui.scrolls[i];
                if (s.layer != ui.activeLayer) continue;
                if (x >= s.view.X && x < s.view.GetRight() && y >= s.view.Y && y < s.view.GetBottom() && s.content > s.view.Height) {
                    *s.pos = std::max(0.0f, std::min(*s.pos - d / 120.0f * 80.0f, s.content - s.view.Height)); break;
                }
            }
            Redraw(); return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { int hit = HitAt(ui.mx, ui.my); SetCursor(LoadCursorW(nullptr, hit < 0 ? IDC_ARROW : ui.hits[hit].cursor == 1 ? IDC_IBEAM : IDC_HAND)); return TRUE; }
            break;
        case WM_CHAR: UI_KeyChar((wchar_t)wp); Redraw(); return 0;
        case WM_KEYDOWN: UI_KeyDown(wp); Redraw(); return 0;
        case WM_TIMER: Redraw(); return 0;
        case WM_APP + 1: DrainUI(); Redraw(); return 0;
        case WM_CLOSE:
            if (g_running && !ModalOpen()) {
                Confirm(L"Запись ещё идёт", L"Если закрыть, запись флешки прервётся и флешка останется недоделанной. Закрыть?", L"Закрыть", [h] { g_cancel = true; DestroyWindow(h); }, true);
                Redraw(); return 0;
            }
            DestroyWindow(h); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static int GetPngClsid(CLSID* c) {
    UINT n = 0, sz = 0; GetImageEncodersSize(&n, &sz); if (!sz) return -1;
    auto* info = (ImageCodecInfo*)malloc(sz); GetImageEncoders(n, sz, info);
    int r = -1;
    for (UINT i = 0; i < n; ++i) if (wcscmp(info[i].MimeType, L"image/png") == 0) { *c = info[i].Clsid; r = i; break; }
    free(info); return r;
}

extern bool g_syncMode;

static int ShotMode(const wstring& dir) {
    g_syncMode = true;
    CreateDirectoryW(dir.c_str(), nullptr);
    g_disks = { { 2, 29, L"SanDisk Ultra USB 3.0" }, { 4, 58, L"Kingston DataTraveler" } };
    g_sel = 2;
    g_isos = { L"D:\\ISO\\Win11_24H2_Russian_x64.iso" };
    g_adk = true;
    CLSID png; GetPngClsid(&png);
    for (bool dark : { true, false }) {
        SetTheme(dark);
        int w = 1000, hh = 1120;
        Bitmap bmp(w, hh, PixelFormat32bppARGB);
        auto render = [&] { Graphics g(&bmp); Render(g, (float)w, (float)hh); };
        render(); render();
        bmp.Save((dir + Fmt(L"\\magictool_%ls.png", dark ? L"dark" : L"light")).c_str(), &png, nullptr);
    }
    SetTheme(true); g_tab = 1;
    { int w = 1000, hh = 1120; Bitmap bmp(w, hh, PixelFormat32bppARGB); { Graphics g(&bmp); Render(g, (float)w, (float)hh); } { Graphics g(&bmp); Render(g, (float)w, (float)hh); } bmp.Save((dir + L"\\magictool_admin.png").c_str(), &png, nullptr); }
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi; ULONG_PTR gtok; GdiplusStartup(&gtok, &gsi, nullptr);

    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_script = FindScript();
    g_adk = AdkPresent();
    g_logo = LoadPngResource(101);
    wstring th = Settings::Get(L"theme", L""); SetTheme(th != L"light");

    for (int i = 1; i < argc; ++i) if (wstring(argv[i]) == L"--shot" && i + 1 < argc) return ShotMode(argv[i + 1]);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"MagicToolWindow"; RegisterClassExW(&wc);

    HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    if (HMODULE sh = LoadLibraryW(L"shcore.dll")) { typedef HRESULT(WINAPI * F)(HMONITOR, int, UINT*, UINT*); if (auto f = (F)GetProcAddress(sh, "GetDpiForMonitor")) f(mon, 0, &dpiX, &dpiY); }
    g_dpi = dpiX / 96.0f;
    RECT wa = mi.rcWork;
    int ww = std::min((int)(1000 * g_dpi), (int)(wa.right - wa.left) - 40);
    int wh = std::min((int)(820 * g_dpi), (int)(wa.bottom - wa.top) - 40);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2, wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    HWND h = CreateWindowExW(0, wc.lpszClassName, L"MagicTool", WS_OVERLAPPEDWINDOW, wx, wy, ww, wh, nullptr, nullptr, inst, nullptr);
    g_hwnd = h; SetUIWindow(h); UpdateDpi(h);
    MARGINS mg{ 0, 0, 1, 0 }; DwmExtendFrameIntoClientArea(h, &mg);
    BOOL dark = g_dark; DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(h, show); UpdateWindow(h);
    RefreshDisks();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    GdiplusShutdown(gtok);
    return 0;
}
