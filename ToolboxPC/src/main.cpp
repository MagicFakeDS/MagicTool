#include "app.h"
#include "report.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <pdh.h>
#include <pdhmsg.h>

AppState app;

static HWND g_overlay = nullptr;
static void SaveWinPlacement(HWND h);

static const float TITLE_H = 54;
static const float SIDE_W = 238;

static vector<std::shared_ptr<Task>> g_tasks;

void TaskCtx::Line(const wstring& s) {
    auto tt = t;
    PostUI([tt, s] {
        auto& L = tt->lines;
        if (!s.empty() && s[0] == L'\r' && !L.empty() && !L.back().empty() && L.back()[0] == L'\r') L.back() = s;
        else L.push_back(s);
        if (L.size() > 3000) L.erase(L.begin(), L.begin() + 500);
        Redraw();
    });
}
int TaskCtx::Run(const wstring& cmd, Enc enc) {
    return RunCapture(cmd, [this](const wstring& l) { Line(l); }, enc, &t->cancel);
}
int TaskCtx::PS(const wstring& script) {
    return RunPS(script, [this](const wstring& l) { Line(l); }, &t->cancel);
}
void TaskCtx::Fail(const wstring& s) { t->ok = false; t->summary = s; }
void TaskCtx::Done(const wstring& s) { t->summary = s; }

std::shared_ptr<Task> StartTask(const wstring& name, int page, std::function<void(TaskCtx&)> body, std::function<void(Task&)> onDone) {
    auto t = std::make_shared<Task>();
    t->name = name;
    t->page = page;
    t->t0 = GetTickCount();
    g_tasks.push_back(t);
    if (g_tasks.size() > 30) g_tasks.erase(g_tasks.begin());
    Redraw();
    Async([t, body, onDone]() -> std::function<void()> {
        TaskCtx ctx{ t };
        try { body(ctx); } catch (...) { ctx.Fail(L"Внутренняя ошибка"); }
        return [t, onDone] {
            t->running = false;
            t->t1 = GetTickCount();
            if (t->cancel) { t->ok = false; if (t->summary.empty()) t->summary = L"Отменено"; }
            if (t->summary.empty()) t->summary = t->ok ? L"Готово" : L"Завершено с ошибкой";
            Toast(t->name + L": " + t->summary, t->ok ? Sev::Ok : Sev::Warn);
            if (onDone) onDone(*t);
            Redraw();
        };
    });
    return t;
}
bool TaskBusy() {
    for (auto& t : g_tasks) if (t->running) return true;
    return false;
}
std::shared_ptr<Task> CurrentTask() {
    for (auto& t : g_tasks) if (t->running) return t;
    return nullptr;
}
std::shared_ptr<Task> LastTaskFor(int page) {
    for (auto it = g_tasks.rbegin(); it != g_tasks.rend(); ++it) if ((*it)->page == page) return *it;
    return nullptr;
}
bool GuardBusy() {
    auto t = CurrentTask();
    if (!t) return false;
    Message(L"Дождитесь завершения", L"Сейчас выполняется «" + t->name + L"». Одновременно запускается только одна операция, чтобы они не мешали друг другу.", Sev::Info);
    return true;
}

static std::map<int, float> g_consoleScroll;
static std::map<int, size_t> g_consoleLines;

float ConsoleBox(float x, float y, float w, float h, int page) {
    auto t = LastTaskFor(page);
    if (!t) return 0;
    RectF r(x, y, w, h);
    FillRound(r, 12, P.console);

    float hx = x + 16;
    if (t->running) Spinner(hx + 8, y + 22, 7, P.accent);
    else SevIcon(t->ok ? Sev::Ok : Sev::Warn, hx, y + 12, 20);
    DWORD el = ((t->running ? GetTickCount() : t->t1) - t->t0) / 1000;
    wstring head = t->name + L"  ·  " + (t->running ? L"выполняется " : t->summary + L"  ·  ") + HumanDuration(el);
    Text(head, RectF(hx + 28, y + 10, w - 200, 24), FontUI(13, 1), Color(255, 230, 240, 238), T_LEFT | T_VCENTER | T_ELLIPSIS);
    float bx = x + w - 12;
    auto tt = t;
    if (t->running) {
        float bw = ButtonW(L"Остановить", Ico::Stop);
        Button(RectF(bx - bw, y + 8, bw, 28), L"Остановить", [tt] {
            Confirm(L"Остановить операцию?", L"«" + tt->name + L"» будет прервана. Для DISM и SFC это безопасно, но проверку придётся запустить заново.",
                    L"Остановить", [tt] { tt->cancel = true; }, true);
        }, Btn::Danger, Ico::Stop);
    } else {
        float bw = ButtonW(L"Копировать", Ico::Copy);
        Button(RectF(bx - bw, y + 8, bw, 28), L"Копировать", [tt] {
            wstring all;
            for (auto& l : tt->lines) all += (l.size() && l[0] == L'\r' ? l.substr(1) : l) + L"\r\n";
            if (OpenClipboard(app.hwnd)) {
                EmptyClipboard();
                HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (all.size() + 1) * 2);
                memcpy(GlobalLock(g), all.c_str(), (all.size() + 1) * 2);
                GlobalUnlock(g);
                SetClipboardData(CF_UNICODETEXT, g);
                CloseClipboard();
                Toast(L"Журнал операции скопирован");
            }
        }, Btn::Secondary, Ico::Copy);
    }

    RectF view(x + 6, y + 44, w - 12, h - 52);
    float lh = 18;
    float content = t->lines.size() * lh + 8;
    float& sc = g_consoleScroll[page];
    if (g_consoleLines[page] != t->lines.size()) {
        g_consoleLines[page] = t->lines.size();
        sc = std::max(0.0f, content - view.Height);
    }
    BeginScroll(view, &sc);
    Font* f = FontMono(12.5f);
    for (size_t i = 0; i < t->lines.size(); ++i) {
        RectF lr(view.X + 10, view.Y + 4 + i * lh, view.Width - 20, lh);
        if (!Visible(lr)) continue;
        wstring s = t->lines[i];
        if (!s.empty() && s[0] == L'\r') s.erase(0, 1);
        Color c = P.consoleText;
        if (IContains(s, L"ошибк") || IContains(s, L"error") || IContains(s, L"не удалось")) c = Color(255, 255, 140, 130);
        Text(s, lr, f, c, T_LEFT | T_VCENTER | T_ELLIPSIS);
    }
    if (t->lines.empty()) Text(t->running ? L"Запуск…" : L"(нет вывода)", RectF(view.X + 10, view.Y + 4, 300, lh), f, Color(255, 120, 140, 140), T_LEFT | T_VCENTER);
    EndScroll(content);
    return h;
}

float Loading(float x, float y, float w, const wstring& text) {
    Spinner(x + 14, y + 20, 10, P.accent);
    Text(text, RectF(x + 36, y + 6, w - 40, 28), FontUI(14), P.muted, T_LEFT | T_VCENTER);
    return 48;
}

float Banner(float x, float y, float w, Sev s, const wstring& title, const wstring& text, const wstring& action, std::function<void()> fn) {
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

void StatTile(RectF r, const wchar_t* icon, const wstring& label, const wstring& value, const wstring& sub, float frac, Color c) {
    Card(r);
    RectF ib(r.X + 18, r.Y + 18, 38, 38);
    FillRound(ib, 10, Alpha(c, g_dark ? 50 : 30));
    Glyph(icon, ib, 17, c);
    Text(label, RectF(r.X + 68, r.Y + 16, r.Width - 80, 18), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
    Text(value, RectF(r.X + 68, r.Y + 34, r.Width - 80, 26), FontUI(19, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
    if (frac >= 0) Progress(RectF(r.X + 18, r.GetBottom() - 34, r.Width - 36, 6), frac, c);
    Text(sub, RectF(r.X + 18, r.GetBottom() - 24, r.Width - 36, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
}

float ActionCard(RectF r, const wchar_t* icon, const wstring& title, const wstring& text, const wstring& btn,
                 std::function<void()> fn, Btn style, bool enabled, const wstring& badge, Sev badgeSev) {
    bool hv = Hover(r);
    Card(r, hv);
    RectF ib(r.X + 18, r.Y + 18, 40, 40);
    GradientH(ib, 10, Alpha(P.accent, g_dark ? 70 : 40), Alpha(P.accent2, g_dark ? 70 : 40));
    Glyph(icon, ib, 18, g_dark ? Color(255, 255, 255, 255) : Blend(P.accent2, Color(255, 0, 0, 0), 0.8f));
    float tx = r.X + 72, tw = r.Width - 90;
    float th = std::min(40.0f, TextH(title, FontUI(14, 1), tw));
    Text(title, RectF(tx, r.Y + 17, tw, th + 2), FontUI(14, 1), P.text, T_WRAP | T_ELLIPSIS);
    float dy = r.Y + 17 + th + 5;
    Text(text, RectF(tx, dy, tw, r.GetBottom() - 56 - dy), FontUI(12.5f), P.muted, T_WRAP | T_ELLIPSIS);
    float bw = 0;
    if (!btn.empty()) {
        bw = ButtonW(btn);
        Button(RectF(tx, r.GetBottom() - 50, bw, 34), btn, fn, style, nullptr, enabled);
    }
    if (!badge.empty()) Badge(tx + bw + 10, r.GetBottom() - 44, badge, badgeSev, 22);
    return r.Height;
}

float TableHeader(float x, float y, float w, const vector<std::pair<wstring, float>>& cols) {
    FillRound(RectF(x, y, w, 30), 8, P.surface);
    for (auto& c : cols) Text(c.first, RectF(x + c.second, y, 400, 30), FontUI(11.5f, 1), P.muted, T_LEFT | T_VCENTER);
    return 34;
}

wstring ShortPath(const wstring& p, size_t max) {
    if (p.size() <= max) return p;
    return p.substr(0, max / 2 - 2) + L"…" + p.substr(p.size() - max / 2 + 1);
}

void OpenSystemTool(const wstring& cmd, const wstring& args) {
    if (!ShellOpen(cmd, args)) Toast(L"Не удалось открыть " + cmd, Sev::Warn);
}

void RequestSysInfo(bool force) {
    if (app.siLoading || (app.si.ready && !force)) return;
    app.siLoading = true;
    Async([]() -> std::function<void()> {
        auto si = std::make_shared<SysInfo>(CollectSysInfo());
        return [si] {
            int startup = app.si.startupCount;
            app.si = *si;
            if (app.si.startupCount < 0) app.si.startupCount = startup;
            app.siLoading = false;
            LiveUpdate(app.live, app.si);
            Redraw();
        };
    });
}

void RequestDevices(bool force) {
    if (app.devsLoading || (app.devsLoaded && !force)) return;
    app.devsLoading = true;
    Async([]() -> std::function<void()> {
        auto d = std::make_shared<vector<ProblemDevice>>(CollectProblemDevices());
        auto c = std::make_shared<CrashInfo>(CollectCrashes());
        return [d, c] {
            app.devs = *d;
            app.crashes = *c;
            app.devsLoaded = true;
            app.devsLoading = false;
            Redraw();
        };
    });
}

void Redraw() {
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void Navigate(int page) {
    if (page < 0 || page >= PG_COUNT) return;
    app.page = page;
    ui.focusId = 0;
    app.pages[page]->OnShow();
    Redraw();
}

static void PaintBg(Graphics& g, float W, float H) {
    LinearGradientBrush bg(RectF(0, 0, W, H + 1), P.bg1, P.bg2, LinearGradientModeVertical);
    g.FillRectangle(&bg, RectF(0, 0, W, H));

    auto glow = [&](float cx, float cy, float r, Color c, int a) {
        GraphicsPath p;
        p.AddEllipse(cx - r, cy - r, r * 2, r * 2);
        PathGradientBrush b(&p);
        b.SetCenterColor(Alpha(c, a));
        Color sur = Alpha(c, 0);
        int n = 1;
        b.SetSurroundColors(&sur, &n);
        g.FillPath(&b, &p);
    };
    glow(W * 0.92f, -40, 420, P.accent2, g_dark ? 60 : 38);
    glow(60, H + 40, 380, P.accent, g_dark ? 45 : 30);
}

static Bitmap* g_bgCache = nullptr;
static int g_bgCacheW = 0, g_bgCacheH = 0;
static bool g_bgCacheDark = false;

static void DrawBackground(float W, float H) {
    int iw = std::max(1, (int)(W + 0.5f)), ih = std::max(1, (int)(H + 0.5f));
    if (!g_bgCache || g_bgCacheW != iw || g_bgCacheH != ih || g_bgCacheDark != g_dark) {
        delete g_bgCache;
        g_bgCache = new Bitmap(iw, ih, PixelFormat32bppPARGB);
        Graphics g(g_bgCache);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        PaintBg(g, (float)iw, (float)ih);
        g_bgCacheW = iw; g_bgCacheH = ih; g_bgCacheDark = g_dark;
    }
    ui.g->DrawImage(g_bgCache, RectF(0, 0, W, H), 0, 0, (REAL)iw, (REAL)ih, UnitPixel);
}

static void DrawTitleBar(float W) {
    if (app.logo) ui.g->DrawImage(app.logo, RectF(20, 12, 30, 30));
    Text(L"MagicTool", RectF(60, 10, 200, 20), FontUI(16, 2), P.text, T_LEFT | T_VCENTER);
    Text(L"ИНСТРУМЕНТ МАСТЕРА · " APP_VERSION, RectF(61, 30, 220, 14), FontUI(10, 1), P.accent, T_LEFT | T_VCENTER);

    float bw = 46, bh = 34;
    float x = W - bw;
    bool maxed = IsZoomed(app.hwnd);
    IconButton(RectF(x, 0, bw, bh), Ico::Close, [] { PostMessageW(app.hwnd, WM_CLOSE, 0, 0); }, P.text, 11, true);
    x -= bw;
    IconButton(RectF(x, 0, bw, bh), maxed ? Ico::Restore : Ico::Max, [] {
        ShowWindow(app.hwnd, IsZoomed(app.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    }, P.text, 11);
    x -= bw;
    IconButton(RectF(x, 0, bw, bh), Ico::Min, [] { ShowWindow(app.hwnd, SW_MINIMIZE); }, P.text, 11);
    x -= 12;

    x -= 36;
    IconButton(RectF(x, 9, 36, 36), g_dark ? Ico::Sun : Ico::Moon, [] {
        SetTheme(!g_dark);
        Settings::Set(L"theme", g_dark ? L"dark" : L"light");
        BOOL d = g_dark;
        DwmSetWindowAttribute(app.hwnd, 20, &d, sizeof(d));
        Redraw();
    }, P.text, 15, false, g_dark ? L"Светлая тема" : L"Тёмная тема");
    x -= 10;

    if (app.elevated) x -= Badge(x, 16, L"Администратор", Sev::Ok, 24, true);
    else {

        float bw = Badge(x, 16, L"Запустить от администратора", Sev::Warn, 24, true);
        RectF br(x - bw, 16, bw, 24);
        OnClick(br, [] { if (!RelaunchAsAdmin()) Toast(L"Не удалось перезапустить от администратора", Sev::Warn); });
        Tooltip(br, L"Нажмите, чтобы перезапустить MagicTool с правами администратора");
        x -= bw;
    }
    x -= 8;
    if (!app.si.computerName.empty()) {
        Font* f = FontUI(12.5f, 1);
        wstring t = app.si.computerName;
        float w = TextW(t, f) + 40;
        x -= w;
        FillRound(RectF(x, 16, w, 24), 12, Alpha(P.card, g_dark ? 160 : 200));
        StrokeRound(RectF(x, 16, w, 24), 12, P.border);
        Glyph(Ico::Monitor, RectF(x + 8, 16, 18, 24), 12, P.muted);
        Text(t, RectF(x + 28, 16, w - 32, 24), f, P.text, T_LEFT | T_VCENTER);
    }
}

static void DrawSidebar(float H) {
    static float sideScroll = 0;
    const wchar_t* groups[] = { L"АНАЛИЗ", L"ОПТИМИЗАЦИЯ", L"ИНСТРУМЕНТЫ", L"ИТОГ" };
    int groupStart[] = { PG_OVERVIEW, PG_CLEANUP, PG_REPAIR, PG_JOURNAL };
    float x = 12, w = SIDE_W - 20;
    auto cur = CurrentTask();
    float bottomH = cur ? 74 : 0;
    RectF view(0, TITLE_H + 4, SIDE_W, H - TITLE_H - 10 - bottomH);
    BeginScroll(view, &sideScroll);
    float y = TITLE_H + 6;
    int gi = 0;
    for (int i = 0; i < PG_COUNT; ++i) {
        if (gi < 4 && i == groupStart[gi]) {
            y += gi ? 12 : 2;
            Text(groups[gi], RectF(x + 14, y, w, 18), FontUI(10.5f, 1), Alpha(P.muted, 220), T_LEFT | T_VCENTER);
            y += 22;
            ++gi;
        }
        Page* p = app.pages[i];
        RectF r(x, y, w, 34);
        bool sel = app.page == i;
        bool hv = Hover(r);
        if (sel) {
            GradientH(r, 9, Alpha(P.accent, g_dark ? 70 : 42), Alpha(P.accent2, g_dark ? 45 : 26));
            GradientH(RectF(r.X, r.Y + 8, 3, r.Height - 16), 1.5f, P.accent, P.accent2);
        } else if (hv) FillRound(r, 9, Alpha(P.card, g_dark ? 120 : 170));
        Color fg = sel ? (g_dark ? Color(255, 255, 255, 255) : Blend(P.accent2, Color(255, 10, 20, 40), 0.55f)) : P.text;
        Glyph(p->icon, RectF(r.X + 10, r.Y, 24, r.Height), 15, sel ? (g_dark ? Blend(P.accent, Color(255, 255, 255, 255), 0.6f) : P.accent) : P.muted);
        Text(p->title, RectF(r.X + 42, r.Y, w - 90, r.Height), FontUI(13.5f, sel ? 1 : 0), fg, T_LEFT | T_VCENTER | T_ELLIPSIS);
        Sev bs = Sev::Info;
        wstring badge = p->NavBadge(bs);
        if (!badge.empty()) {
            Font* bf = FontUI(11, 1);
            float bw = std::max(22.0f, TextW(badge, bf) + 12);
            RectF br(r.GetRight() - bw - 8, r.Y + 8, bw, 18);
            FillRound(br, 9, SevSoft(bs));
            Text(badge, br, bf, SevColor(bs), T_CENTER | T_VCENTER);
        }
        OnClick(r, [i] { Navigate(i); });
        y += 36;
    }
    EndScroll(y - (TITLE_H + 4) + 10);
    if (cur) {
        RectF r(x, H - 12 - 66, w, 62);
        bool hv = Hover(r);
        Card(r, hv, 12);
        Spinner(r.X + 24, r.Y + 31, 9, P.accent);
        Text(L"Выполняется", RectF(r.X + 44, r.Y + 11, w - 52, 18), FontUI(11.5f), P.muted, T_LEFT | T_VCENTER);
        Text(cur->name, RectF(r.X + 44, r.Y + 29, w - 52, 20), FontUI(13, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        int pg = cur->page;
        OnClick(r, [pg] { Navigate(pg); });
    }
}

static void DrawContent(float W, float H) {
    Page* p = app.pages[app.page];
    RectF card(SIDE_W, TITLE_H + 4, W - SIDE_W - 14, H - TITLE_H - 18);
    Shadow(card, 18, 16);
    FillRound(card, 18, P.card);
    StrokeRound(card, 18, P.border);
    {
        GraphicsPath clip;
        RoundPath(clip, card, 18);
        Region old;
        ui.g->GetClip(&old);
        ui.g->SetClip(&clip, CombineModeIntersect);
        LinearGradientBrush gb(RectF(card.X, card.Y, card.Width, 4), P.accent, P.accent2, LinearGradientModeHorizontal);
        ui.g->FillRectangle(&gb, RectF(card.X, card.Y, card.Width, 4));
        ui.g->SetClip(&old);
    }
    float px = card.X + 30, pw = card.Width - 60;
    Text(p->title, RectF(px, card.Y + 22, pw * 0.6f, 32), FontUI(24, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
    Text(p->sub, RectF(px, card.Y + 56, pw * 0.62f, 20), FontUI(13), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
    p->Header(card.GetRight() - 30, card.Y + 26);
    RectF view(card.X + 4, card.Y + 92, card.Width - 8, card.Height - 96);
    BeginScroll(view, &p->scroll);
    float bottom = p->Body(px, view.Y + 6, pw);
    EndScroll(bottom - view.Y + 26);
}

bool RelaunchAsAdmin() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {

        if (app.hwnd) DestroyWindow(app.hwnd); else PostQuitMessage(0);
        return true;
    }
    return false;
}

static wstring g_licKey;
static bool g_licBad = false;

static void TryUnlock() {
    if (LicenseCheckKey(g_licKey)) {
        LicenseActivate();
        app.locked = false;
        g_licBad = false;
        g_licKey.clear();
        ui.focusId = 0;
        Toast(L"Лицензия активирована");
    } else {
        g_licBad = true;
    }
    Redraw();
}

static void DrawLockScreen(float W, float H) {
    float cw = 480, ch = 320;
    float cx = (W - cw) / 2, cy = (H - ch) / 2 + 10;
    RectF card(cx, cy, cw, ch);
    Shadow(card, 18, 22);
    Card(card, false, 18);
    GradientH(RectF(cx, cy, cw, 4), 2, P.accent, P.accent2);
    if (app.logo) ui.g->DrawImage(app.logo, RectF(cx + cw / 2 - 27, cy + 30, 54, 54));
    Text(L"MagicTool", RectF(cx, cy + 92, cw, 26), FontUI(19, 2), P.text, T_CENTER | T_VCENTER);
    Text(L"Введите лицензионный ключ мастера", RectF(cx, cy + 122, cw, 20), FontUI(13), P.muted, T_CENTER | T_VCENTER);
    float fx = cx + 44, fw = cw - 88;
    TextBox(9001, RectF(fx, cy + 156, fw, 46), g_licKey, L"XXXX-XXXX-XXXX-XXXX-XXXX-XXXX", 60, true);
    if (g_licBad)
        Text(L"Неверный ключ. Проверьте раскладку и регистр.", RectF(cx, cy + 206, cw, 18), FontUI(12), P.danger, T_CENTER | T_VCENTER);
    Button(RectF(fx, cy + 232, fw, 46), L"Разблокировать", [] { TryUnlock(); }, Btn::Primary);
    Text(L"Ключ запрашивается один раз на этом компьютере.", RectF(cx, cy + 290, cw, 16), FontUI(11), P.muted, T_CENTER | T_VCENTER);
}

static void Render(Graphics& g, float W, float H) {
    BeginFrame(&g, W, H);
    DrawBackground(W, H);
    DrawTitleBar(W);
    if (app.locked) {
        DrawLockScreen(W, H);
    } else {
        DrawSidebar(H);
        DrawContent(W, H);
    }
    DrawToasts();
    DrawModal();
    EndFrame();
}

static float g_dpi = 1.0f;
static HDC g_memDC = nullptr;
static HBITMAP g_dib = nullptr;
static int g_dibW = 0, g_dibH = 0;
static int g_hoverHit = -1;
static bool g_animTimer = false;

static void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int w = std::max(1L, rc.right), hh = std::max(1L, rc.bottom);
    if (w != g_dibW || hh != g_dibH || !g_memDC) {
        if (g_memDC) DeleteDC(g_memDC);
        if (g_dib) DeleteObject(g_dib);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -hh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits;
        g_dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        g_memDC = CreateCompatibleDC(dc);
        SelectObject(g_memDC, g_dib);
        g_dibW = w; g_dibH = hh;
    }
    {
        Graphics g(g_memDC);
        g.ScaleTransform(g_dpi, g_dpi);
        Render(g, w / g_dpi, hh / g_dpi);
    }
    BitBlt(dc, 0, 0, w, hh, g_memDC, 0, 0, SRCCOPY);
    EndPaint(h, &ps);
    if (ui.animating && !g_animTimer) { SetTimer(h, 2, 16, nullptr); g_animTimer = true; }
    if (!ui.animating && g_animTimer) { KillTimer(h, 2); g_animTimer = false; }
}

static int HitAt(float x, float y) {
    for (int i = (int)ui.hits.size() - 1; i >= 0; --i) {
        auto& h = ui.hits[i];
        if (h.layer == ui.activeLayer && x >= h.r.X && x < h.r.GetRight() && y >= h.r.Y && y < h.r.GetBottom()) return i;
    }
    return -1;
}

static void UpdateDpi(HWND h) {
    UINT dpi = GetDpiForWindow(h);
    g_dpi = dpi ? dpi / 96.0f : 1.0f;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_NCCALCSIZE:
            if (wp) {
                if (IsZoomed(h)) {
                    auto p = (NCCALCSIZE_PARAMS*)lp;
                    UINT dpi = GetDpiForWindow(h);
                    int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
                    p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(h, &pt);
            RECT rc; GetClientRect(h, &rc);
            int b = (int)(6 * g_dpi);
            if (!IsZoomed(h)) {
                bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, bt = pt.y >= rc.bottom - b;
                if (t && l) return HTTOPLEFT;
                if (t && r) return HTTOPRIGHT;
                if (bt && l) return HTBOTTOMLEFT;
                if (bt && r) return HTBOTTOMRIGHT;
                if (l) return HTLEFT;
                if (r) return HTRIGHT;
                if (t) return HTTOP;
                if (bt) return HTBOTTOM;
            }
            float x = pt.x / g_dpi, y = pt.y / g_dpi;
            if (y < TITLE_H && HitAt(x, y) < 0 && !ModalOpen()) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = (LONG)(1060 * g_dpi);
            mm->ptMinTrackSize.y = (LONG)(680 * g_dpi);
            return 0;
        }
        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp) / 96.0f;
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Redraw();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(h);
            return 0;
        case WM_APP + 7: {
            wchar_t dir[MAX_PATH] = {};
            if (!GetEnvironmentVariableW(L"TOOLBOXPC_SHOT_DIR", dir, MAX_PATH)) return 0;
            RECT rc;
            GetClientRect(h, &rc);
            Bitmap bmp(std::max(1L, rc.right), std::max(1L, rc.bottom), PixelFormat32bppARGB);
            {
                Graphics g(&bmp);
                g.ScaleTransform(g_dpi, g_dpi);
                Render(g, rc.right / g_dpi, rc.bottom / g_dpi);
            }
            CLSID png;
            extern int GetPngClsid(CLSID*);
            if (GetPngClsid(&png) >= 0) bmp.Save(Fmt(L"%ls\\shot_%d.png", dir, (int)wp).c_str(), &png, nullptr);
            return 1;
        }
        case WM_SIZE:
            Redraw();
            return 0;
        case WM_MOUSEMOVE: {
            ui.mx = GET_X_LPARAM(lp) / g_dpi;
            ui.my = GET_Y_LPARAM(lp) / g_dpi;
            if (ui.dragPos) {
                *ui.dragPos = std::max(0.0f, ui.dragStartPos + (ui.my - ui.dragStartY) * ui.dragRatio);
                Redraw();
                return 0;
            }
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);

            uint64_t sig = 1469598103934665603ULL;
            for (size_t i = 0; i < ui.hovers.size(); ++i) {
                auto& r = ui.hovers[i];
                if (ui.mx >= r.X && ui.mx < r.GetRight() && ui.my >= r.Y && ui.my < r.GetBottom()) sig = (sig ^ (i + 1)) * 1099511628211ULL;
            }
            int hit = HitAt(ui.mx, ui.my);
            sig = (sig ^ (uint64_t)(hit + 2)) * 1099511628211ULL;
            static uint64_t lastSig = 0;
            if (sig != lastSig || ui.lmb) { lastSig = sig; g_hoverHit = hit; Redraw(); }
            return 0;
        }
        case WM_MOUSELEAVE:
            ui.mx = ui.my = -1;
            g_hoverHit = -1;
            Redraw();
            return 0;
        case WM_LBUTTONDOWN: {
            ui.mx = GET_X_LPARAM(lp) / g_dpi;
            ui.my = GET_Y_LPARAM(lp) / g_dpi;
            ui.lmb = true;
            SetCapture(h);
            for (int i = (int)ui.scrolls.size() - 1; i >= 0; --i) {
                auto& s = ui.scrolls[i];
                if (s.layer != ui.activeLayer || s.content <= s.view.Height + 1) continue;
                if (ui.mx >= s.view.GetRight() - 12 && ui.mx < s.view.GetRight() && ui.my >= s.view.Y && ui.my < s.view.GetBottom()) {
                    float trackH = s.view.Height - 8;
                    float maxPos = s.content - s.view.Height;
                    if (ui.my < s.barY || ui.my > s.barY + s.barH)
                        *s.pos = std::max(0.0f, std::min(maxPos, (ui.my - s.view.Y - s.barH / 2) / std::max(1.0f, trackH - s.barH) * maxPos));
                    ui.dragPos = s.pos;
                    ui.dragStartY = ui.my;
                    ui.dragStartPos = *s.pos;
                    ui.dragRatio = maxPos / std::max(1.0f, trackH - s.barH);
                    Redraw();
                    return 0;
                }
            }
            int hit = HitAt(ui.mx, ui.my);
            ui.pressedHit = hit;
            if (hit >= 0) ui.pressedRect = ui.hits[hit].r;
            if (hit < 0 || ui.hits[hit].cursor != 1) ui.focusId = 0;
            Redraw();
            return 0;
        }
        case WM_LBUTTONUP: {
            ui.lmb = false;
            ReleaseCapture();
            if (ui.dragPos) { ui.dragPos = nullptr; ui.pressedHit = -1; Redraw(); return 0; }
            float x = GET_X_LPARAM(lp) / g_dpi, y = GET_Y_LPARAM(lp) / g_dpi;
            int hit = HitAt(x, y);
            if (hit >= 0 && ui.pressedHit >= 0 && ui.hits[hit].r.Equals(ui.pressedRect)) {
                auto fn = ui.hits[hit].fn;
                if (fn) fn();
            }
            ui.pressedHit = -1;
            Redraw();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(h, &pt);
            float x = pt.x / g_dpi, y = pt.y / g_dpi;
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            for (int i = (int)ui.scrolls.size() - 1; i >= 0; --i) {
                auto& s = ui.scrolls[i];
                if (s.layer != ui.activeLayer) continue;
                if (x >= s.view.X && x < s.view.GetRight() && y >= s.view.Y && y < s.view.GetBottom() && s.content > s.view.Height) {
                    *s.pos -= d / 120.0f * 80.0f;
                    *s.pos = std::max(0.0f, std::min(*s.pos, s.content - s.view.Height));
                    break;
                }
            }
            Redraw();
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                int hit = HitAt(ui.mx, ui.my);
                SetCursor(LoadCursorW(nullptr, hit < 0 ? IDC_ARROW : ui.hits[hit].cursor == 1 ? IDC_IBEAM : IDC_HAND));
                return TRUE;
            }
            break;
        case WM_CHAR:
            UI_KeyChar((wchar_t)wp);
            Redraw();
            return 0;
        case WM_KEYDOWN:
            if (app.locked && wp == VK_RETURN) { TryUnlock(); return 0; }
            if (ModalOpen()) {
                if (wp == VK_ESCAPE) { CloseModal(); Redraw(); return 0; }
                if (wp == VK_RETURN) { ModalPrimary(); Redraw(); return 0; }
            }
            UI_KeyDown(wp);
            Redraw();
            return 0;
        case WM_HOTKEY:
            if (wp == 1) { ToggleOverlay(); Redraw(); }
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                if (app.page == PG_OVERVIEW || app.page == PG_HARDWARE) LiveUpdate(app.live, app.si);
                if (app.pages[app.page]->Tick() || TaskBusy()) Redraw();
            } else Redraw();
            return 0;
        case WM_APP + 1:
            DrainUI();
            Redraw();
            return 0;
        case WM_CLOSE:
            if (TaskBusy() && !ModalOpen()) {
                Confirm(L"Операция ещё идёт", L"Если закрыть программу, запущенная проверка (например, DISM или SFC) продолжит работу в фоне, но её результат вы не увидите. Закрыть?",
                        L"Закрыть", [h] { DestroyWindow(h); }, true);
                Redraw();
                return 0;
            }
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            SaveWinPlacement(h);
            UnregisterHotKey(h, 1);
            if (g_overlay) { DestroyWindow(g_overlay); g_overlay = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static Bitmap* LoadPngResource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return nullptr;
    HGLOBAL g = LoadResource(nullptr, r);
    DWORD sz = SizeofResource(nullptr, r);
    IStream* s = SHCreateMemStream((const BYTE*)LockResource(g), sz);
    if (!s) return nullptr;
    Bitmap* b = Bitmap::FromStream(s);
    s->Release();
    return b;
}

static void InitPages() {
    app.pages = { MakeOverview(), MakeHardware(), MakeDisks(), MakeDevices(), MakeProcesses(),
                  MakeCleanup(), MakeStartup(), MakeServices(), MakeTweaks(), MakeApps(),
                  MakeRepair(), MakeNetwork(), MakeActivation(), MakeSecurity(),
                  MakeJournal() };
}

static bool SystemDark() {
    DWORD v = 1;
    RegGetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", v);
    return v == 0;
}

static int GetEncoderClsid(const WCHAR* format, CLSID* clsid) {
    UINT n = 0, s = 0;
    GetImageEncodersSize(&n, &s);
    if (!s) return -1;
    vector<BYTE> buf(s);
    auto p = (ImageCodecInfo*)buf.data();
    GetImageEncoders(n, s, p);
    for (UINT j = 0; j < n; ++j) if (wcscmp(p[j].MimeType, format) == 0) { *clsid = p[j].Clsid; return (int)j; }
    return -1;
}

int GetPngClsid(CLSID* c) { return GetEncoderClsid(L"image/png", c); }

static int ShotMode(int argc, wchar_t** argv) {
    wstring dir = L".", theme = L"both";
    int only = -1, w = 1280, h = 800;
    float scale = 1.0f;
    vector<std::pair<float, float>> clicks;
    float scrollY = 0;
    for (int i = 1; i < argc; ++i) {
        wstring a = argv[i];
        auto next = [&]() -> wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--shot") dir = next();
        else if (a == L"--theme") theme = next();
        else if (a == L"--page") only = _wtoi(next().c_str());
        else if (a == L"--size") { auto s = Split(next(), L'x'); w = _wtoi(s[0].c_str()); h = s.size() > 1 ? _wtoi(s[1].c_str()) : h; }
        else if (a == L"--scale") scale = (float)_wtof(next().c_str());
        else if (a == L"--click") { auto s = Split(next(), L','); if (s.size() == 2) clicks.push_back({ (float)_wtof(s[0].c_str()), (float)_wtof(s[1].c_str()) }); }
        else if (a == L"--scroll") scrollY = (float)_wtof(next().c_str());
    }
    g_syncMode = true;
    app.locked = false;
    CreateDirectoryW(dir.c_str(), nullptr);
    app.si = CollectSysInfo();
    app.devs = CollectProblemDevices();
    app.crashes = CollectCrashes();
    app.devsLoaded = true;
    LiveUpdate(app.live, app.si);
    Sleep(300);
    LiveUpdate(app.live, app.si);
    CLSID png;
    GetEncoderClsid(L"image/png", &png);
    vector<bool> themes;
    if (theme == L"both" || theme == L"light") themes.push_back(false);
    if (theme == L"both" || theme == L"dark") themes.push_back(true);
    for (bool dark : themes) {
        SetTheme(dark);
        for (int i = 0; i < PG_COUNT; ++i) {
            if (only >= 0 && i != only) continue;
            app.page = i;
            app.pages[i]->OnShow();
            app.pages[i]->scroll = scrollY;
            int pw = (int)(w * scale), ph = (int)(h * scale);
            Bitmap bmp(pw, ph, PixelFormat32bppARGB);
            auto render = [&] {
                Graphics g(&bmp);
                g.ScaleTransform(scale, scale);
                Render(g, (float)w, (float)h);
            };
            render();
            render();
            for (auto& c : clicks) {
                ui.mx = c.first; ui.my = c.second;
                int hit = HitAt(c.first, c.second);
                if (hit >= 0 && ui.hits[hit].fn) { auto fn = ui.hits[hit].fn; fn(); }
                render();
                render();
            }
            wstring file = dir + Fmt(L"\\%02d_%ls.png", i, dark ? L"dark" : L"light");
            bmp.Save(file.c_str(), &png, nullptr);
        }
    }
    return 0;
}

static int g_ovCorner = 1;
static const int OV_LW = 256, OV_LH = 214;

static void RoundFill(Graphics& g, RectF r, float rad, const Color& c) {
    GraphicsPath p;
    p.AddArc(r.X, r.Y, rad, rad, 180, 90);
    p.AddArc(r.GetRight() - rad, r.Y, rad, rad, 270, 90);
    p.AddArc(r.GetRight() - rad, r.GetBottom() - rad, rad, rad, 0, 90);
    p.AddArc(r.X, r.GetBottom() - rad, rad, rad, 90, 90);
    p.CloseFigure();
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

static PDH_HQUERY g_pdhQ = nullptr;
static PDH_HCOUNTER g_cGpu = nullptr, g_cDisk = nullptr, g_cNet = nullptr;
static void PdhStart() {
    if (g_pdhQ) return;
    if (PdhOpenQueryW(nullptr, 0, &g_pdhQ) != ERROR_SUCCESS) { g_pdhQ = nullptr; return; }
    PdhAddEnglishCounterW(g_pdhQ, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &g_cGpu);
    PdhAddEnglishCounterW(g_pdhQ, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_cDisk);
    PdhAddEnglishCounterW(g_pdhQ, L"\\Network Interface(*)\\Bytes Total/sec", 0, &g_cNet);
    PdhCollectQueryData(g_pdhQ);
}
static void PdhStop() {
    if (g_pdhQ) { PdhCloseQuery(g_pdhQ); g_pdhQ = nullptr; g_cGpu = g_cDisk = g_cNet = nullptr; }
}
static double PdhSum(PDH_HCOUNTER c) {
    if (!c) return -1;
    DWORD sz = 0, n = 0;
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &sz, &n, nullptr) != PDH_MORE_DATA) return -1;
    vector<BYTE> buf(sz);
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &sz, &n, items) != ERROR_SUCCESS) return -1;
    double s = 0;
    for (DWORD i = 0; i < n; i++) if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) s += items[i].FmtValue.doubleValue;
    return s;
}
static double PdhOne(PDH_HCOUNTER c) {
    if (!c) return -1;
    PDH_FMT_COUNTERVALUE v;
    if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, nullptr, &v) != ERROR_SUCCESS) return -1;
    return v.doubleValue;
}

static void RenderOverlay() {
    if (!g_overlay) return;
    LiveUpdate(app.live, app.si);
    double gpuPct = -1, diskPct = -1, netBps = -1;
    if (g_pdhQ && PdhCollectQueryData(g_pdhQ) == ERROR_SUCCESS) {
        gpuPct = PdhSum(g_cGpu);  if (gpuPct > 100) gpuPct = 100;
        diskPct = PdhOne(g_cDisk); if (diskPct > 100) diskPct = 100;
        netBps = PdhSum(g_cNet);
    }
    float s = g_dpi > 0 ? g_dpi : 1.0f;
    int W = (int)(OV_LW * s), H = (int)(OV_LH * s);

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, W, H);
    HBITMAP oldb = (HBITMAP)SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.ScaleTransform(s, s);

        SolidBrush bg(Color(255, 15, 17, 23));
        g.FillRectangle(&bg, 0, 0, OV_LW, OV_LH);
        LinearGradientBrush top(RectF(0, 0, (REAL)OV_LW, 3), P.accent, P.accent2, LinearGradientModeHorizontal);
        g.FillRectangle(&top, 0, 0, OV_LW, 3);

        SolidBrush tb(P.text), mut(P.muted);
        g.DrawString(L"MagicTool · монитор", -1, FontUI(11.5f, 1), PointF(16, 11), &tb);

        StringFormat sfRight; sfRight.SetAlignment(StringAlignmentFar);
        const float LX = 16, RX = OV_LW - 16.0f;
        auto metric = [&](float y, const wstring& label, const wstring& val, Color vc, float frac) {
            g.DrawString(label.c_str(), -1, FontUI(12.5f, 1), PointF(LX, y), &mut);
            SolidBrush vb(vc);
            g.DrawString(val.c_str(), -1, FontUI(12.5f, 1), RectF(70, y, RX - 70, 18), &sfRight, &vb);
            if (frac >= 0) {
                RoundFill(g, RectF(LX, y + 20, RX - LX, 4), 2, Alpha(P.text, 28));
                if (frac > 0.01f) RoundFill(g, RectF(LX, y + 20, (RX - LX) * std::min(1.0f, frac), 4), 2, vc);
            }
        };
        auto& si = app.si;
        auto loadCol = [&](double p) { return p >= 90 ? P.danger : p >= 70 ? P.warn : P.accent; };
        auto tempCol = [&](int t) { return t >= 85 ? P.danger : t >= 70 ? P.warn : P.ok; };

        float cpu = (float)app.live.cpuPct, ram = (float)app.live.ramPct;
        wstring cpuSub = si.cpuMaxMHz ? Fmt(L"  ·  %.2f ГГц", si.cpuMaxMHz / 1000.0) : L"";
        metric(34, L"ЦП", Fmt(L"%.0f %%", cpu) + cpuSub, loadCol(cpu), cpu / 100);
        metric(64, L"ОЗУ", Fmt(L"%.0f %%  ·  %s", ram, (HumanSize(app.live.ramUsed) + L" / " + HumanSize(app.live.ramTotal)).c_str()), loadCol(ram), ram / 100);
        int gt = app.live.gpuTemps.empty() ? -1 : app.live.gpuTemps[0];
        wstring gv = (gpuPct >= 0 ? Fmt(L"%.0f %%", gpuPct) : L"—") + (gt > 0 ? Fmt(L"  ·  %d °C", gt) : L"");
        metric(94, L"GPU", gv, gpuPct >= 0 ? loadCol(gpuPct) : tempCol(gt), gpuPct >= 0 ? (float)gpuPct / 100 : -1);
        metric(124, L"Диск", diskPct >= 0 ? Fmt(L"%.0f %%", diskPct) : L"—", diskPct >= 0 ? loadCol(diskPct) : P.text, diskPct >= 0 ? (float)diskPct / 100 : -1);

        wstring net = L"—";
        if (netBps >= 0) net = netBps >= 1048576 ? Fmt(L"%.1f МБ/с", netBps / 1048576.0) : Fmt(L"%.0f КБ/с", netBps / 1024.0);
        g.DrawString(L"Сеть", -1, FontUI(12.5f, 1), PointF(LX, 156), &mut);
        { SolidBrush nb(P.text); g.DrawString(net.c_str(), -1, FontUI(12.5f, 1), PointF(70, 156), &nb); }
        g.DrawString(L"Плата", -1, FontUI(12.5f, 1), PointF(150, 156), &mut);
        { SolidBrush pb(si.thermalC >= 70 ? P.warn : P.text); wstring pt = si.thermalC > 0 ? Fmt(L"%d °C", si.thermalC) : L"—"; g.DrawString(pt.c_str(), -1, FontUI(12.5f, 1), PointF(196, 156), &pb); }

        if (!si.gpus.empty()) {
            StringFormat ell; ell.SetTrimming(StringTrimmingEllipsisCharacter); ell.SetFormatFlags(StringFormatFlagsNoWrap);
            SolidBrush gn(Alpha(P.muted, 210));
            g.DrawString(si.gpus[0].name.c_str(), -1, FontUI(11), RectF(LX, 178, RX - LX, 16), &ell, &gn);
        }
        SolidBrush hint(Alpha(P.muted, 150));
        g.DrawString(L"без FPS · не внедряется в игру", -1, FontUI(9.5f), PointF(LX, 196), &hint);
    }
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int m = (int)(14 * s);
    int px = (g_ovCorner == 1 || g_ovCorner == 3) ? wa.right - W - m : wa.left + m;
    int py = (g_ovCorner >= 2) ? wa.bottom - H - m : wa.top + m;
    SetWindowPos(g_overlay, HWND_TOPMOST, px, py, W, H, SWP_NOACTIVATE);
    HDC wdc = GetDC(g_overlay);
    BitBlt(wdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    ReleaseDC(g_overlay, wdc);

    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_TIMER) { RenderOverlay(); return 0; }
    if (m == WM_DESTROY) { KillTimer(h, 7); return 0; }
    return DefWindowProcW(h, m, wp, lp);
}

void ToggleOverlay() {
    if (g_overlay) { DestroyWindow(g_overlay); g_overlay = nullptr; PdhStop(); return; }
    static bool reg = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"ToolboxOverlay";
        RegisterClassExW(&wc);
        reg = true;
    }
    float s = g_dpi > 0 ? g_dpi : 1.0f;
    int W = (int)(OV_LW * s), H = (int)(OV_LH * s);
    g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                L"ToolboxOverlay", L"", WS_POPUP, 0, 0, W, H, nullptr, nullptr, inst, nullptr);
    if (!g_overlay) return;
    SetWindowRgn(g_overlay, CreateRoundRectRgn(0, 0, W + 1, H + 1, (int)(18 * s), (int)(18 * s)), TRUE);
    SetLayeredWindowAttributes(g_overlay, 0, 238, LWA_ALPHA);
    ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
    PdhStart();
    SetTimer(g_overlay, 7, 1000, nullptr);
    RequestSysInfo(true);
    RenderOverlay();
}
bool OverlayOn() { return g_overlay != nullptr; }
void CycleOverlayCorner() { g_ovCorner = (g_ovCorner + 1) % 4; RenderOverlay(); }

static bool RestoreWinPlacement(HWND h) {
    wstring s = Settings::Get(L"win");
    if (s.empty()) return false;
    int x = 0, y = 0, w = 0, hh = 0, mx = 0;
    if (swscanf(s.c_str(), L"%d,%d,%d,%d,%d", &x, &y, &w, &hh, &mx) < 4 || w < 500 || hh < 400) return false;
    WINDOWPLACEMENT wp{ sizeof(wp) };
    wp.rcNormalPosition = { x, y, x + w, y + hh };
    wp.showCmd = mx ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    return SetWindowPlacement(h, &wp) != FALSE;
}
static void SaveWinPlacement(HWND h) {
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (!GetWindowPlacement(h, &wp)) return;
    RECT& r = wp.rcNormalPosition;
    int mx = (wp.showCmd == SW_SHOWMAXIMIZED || IsZoomed(h)) ? 1 : 0;
    Settings::Set(L"win", Fmt(L"%d,%d,%d,%d,%d", r.left, r.top, r.right - r.left, r.bottom - r.top, mx));
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    GdiplusStartupInput gsi;
    ULONG_PTR gtok;
    GdiplusStartup(&gtok, &gsi, nullptr);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    app.elevated = IsElevated();
    app.locked = false;
    app.logo = LoadPngResource(101);
    Journal::Load();
    wstring th = Settings::Get(L"theme", L"");
    SetTheme(th.empty() ? SystemDark() : th == L"dark");
    InitPages();

    for (int i = 1; i < argc; ++i) {
        wstring a = argv[i];
        if (a == L"--shot") return ShotMode(argc, argv);
        if (a == L"--report" && i + 1 < argc) {

            g_syncMode = true;
            app.si = CollectSysInfo();
            app.devs = CollectProblemDevices();
            app.crashes = CollectCrashes();
            LiveUpdate(app.live, app.si);
            ReportOptions o;
            o.master = Settings::Get(L"master");
            o.contact = Settings::Get(L"contact");
            return WriteReport(argv[i + 1], o) ? 0 : 1;
        }
    }

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"ToolboxPCWindow";
    RegisterClassExW(&wc);

    HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    typedef HRESULT(WINAPI * GetDpiFn)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
        if (auto f = (GetDpiFn)GetProcAddress(sh, "GetDpiForMonitor")) f(mon, 0, &dpiX, &dpiY);
    g_dpi = dpiX / 96.0f;
    RECT wa = mi.rcWork;
    int ww = std::min((int)(1320 * g_dpi), (int)(wa.right - wa.left) - 40);
    int wh = std::min((int)(860 * g_dpi), (int)(wa.bottom - wa.top) - 40);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2, wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    HWND h = CreateWindowExW(0, wc.lpszClassName, L"MagicTool — инструмент мастера", WS_OVERLAPPEDWINDOW,
                             wx, wy, ww, wh, nullptr, nullptr, inst, nullptr);
    app.hwnd = h;
    SetUIWindow(h);
    UpdateDpi(h);
    MARGINS mg{ 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(h, &mg);
    BOOL dark = g_dark;
    DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    if (!RestoreWinPlacement(h)) ShowWindow(h, show);
    UpdateWindow(h);
    RegisterHotKey(h, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M');
    SetTimer(h, 1, 1000, nullptr);
    Navigate(PG_OVERVIEW);
    if (app.locked) ui.focusId = 9001;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(gtok);
    return 0;
}
