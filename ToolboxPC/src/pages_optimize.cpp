#include "app.h"
#include "optimize.h"
#include <commdlg.h>
#include <cwctype>
#include <shlwapi.h>

static Bitmap* AppIconRes(int id) {
    static std::map<int, Bitmap*> cache;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    Bitmap* b = nullptr;
    if (HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA)) {
        HGLOBAL g = LoadResource(nullptr, r);
        DWORD sz = SizeofResource(nullptr, r);
        if (IStream* s = SHCreateMemStream((const BYTE*)LockResource(g), sz)) {
            b = Bitmap::FromStream(s);
            s->Release();
            if (b && b->GetLastStatus() != Ok) { delete b; b = nullptr; }
        }
    }
    cache[id] = b;
    return b;
}
static int UsefulIcon(const wchar_t* id) {
    struct { const wchar_t* k; int r; } m[] = {
        { L"Brave", 110 }, { L"Google.Chrome", 111 }, { L"Mozilla.Firefox", 112 }, { L"Telegram", 113 },
        { L"Discord", 114 }, { L"Valve.Steam", 115 }, { L"Yandex.Music", 116 }, { L"7zip", 117 },
        { L"RARLab", 118 }, { L"VideoLAN", 119 },
    };
    for (auto& e : m) if (IContains(id, e.k)) return e.r;
    return 0;
}

static wstring NowStamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    return Fmt(L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}
static wstring SafeName(const wstring& s) {
    wstring r;
    for (wchar_t c : s) r += (iswalnum(c) || c == L'-' || c == L'_') ? c : L'_';
    if (r.size() > 40) r.resize(40);
    return r.empty() ? L"app" : r;
}

static wstring MsiNoReboot(wstring cmd) {
    if (!IContains(cmd, L"msiexec")) return cmd;
    size_t p = Lower(cmd).find(L"/i{");
    if (p != wstring::npos) cmd[p + 1] = L'X';
    if (!IContains(cmd, L"/norestart")) cmd += L" /norestart";
    if (!IContains(cmd, L"reboot=")) cmd += L" REBOOT=ReallySuppress";
    return cmd;
}

static void ConfigureBrave(TaskCtx& c) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Policies\\BraveSoftware\\Brave", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS) {
        c.Line(L"  не удалось записать настройки Brave");
        return;
    }
    auto dw = [&](const wchar_t* n, DWORD v) { RegSetValueExW(h, n, 0, REG_DWORD, (const BYTE*)&v, sizeof(v)); };
    dw(L"BraveRewardsDisabled", 1);
    dw(L"BraveWalletDisabled", 1);
    dw(L"BraveVPNDisabled", 1);
    dw(L"BraveAIChatEnabled", 0);
    dw(L"TorDisabled", 1);
    dw(L"MetricsReportingEnabled", 0);
    dw(L"BackgroundModeEnabled", 0);
    dw(L"DefaultBrowserSettingEnabled", 0);
    RegCloseKey(h);
    c.Line(L"  Brave настроен: убраны Rewards, Wallet, VPN, Tor, AI, фоновый режим и телеметрия.");
}

static wstring FindRarKey() {
    wstring cand[] = { ExeDir() + L"\\data\\rarreg.key", ExeDir() + L"\\rarreg.key",
                       ExeDir() + L"\\tools\\rarreg.key", DataDir() + L"\\rarreg.key" };
    for (auto& p : cand) if (FileExists(p)) return p;
    return L"";
}

static int DeployRarKey(const wstring& key) {
    int n = 0;
    wstring dirs[] = { KnownFolder(FOLDERID_ProgramFiles) + L"\\WinRAR",
                       KnownFolder(FOLDERID_ProgramFilesX86) + L"\\WinRAR" };
    for (auto& d : dirs)
        if (DirExists(d) && CopyFileW(key.c_str(), (d + L"\\rarreg.key").c_str(), FALSE)) ++n;
    return n;
}

static float RowText(float x, float y, float w, const wstring& title, const wstring& sub, const wstring& warn) {
    Text(title, RectF(x, y, w, 20), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
    float h = 22;
    if (!sub.empty()) {
        float sh = TextH(sub, FontUI(12.5f), w);
        Text(sub, RectF(x, y + h, w, sh + 2), FontUI(12.5f), P.muted, T_WRAP);
        h += sh + 2;
    }
    if (!warn.empty()) {
        float wh = TextH(warn, FontUI(12.5f), w - 20);
        Glyph(Ico::Warning, RectF(x - 2, y + h + 1, 16, 16), 11, P.warn);
        Text(warn, RectF(x + 18, y + h, w - 20, wh + 2), FontUI(12.5f), P.warn, T_WRAP);
        h += wh + 2;
    }
    return h;
}

static float RowTextH(float w, const wstring& sub, const wstring& warn) {
    float h = 22;
    if (!sub.empty()) h += TextH(sub, FontUI(12.5f), w) + 2;
    if (!warn.empty()) h += TextH(warn, FontUI(12.5f), w - 20) + 2;
    return h;
}

struct CleanupPage : Page {
    vector<CleanCat> cats = CleanCategories();
    bool scanning = false, scannedOnce = false;
    CleanupPage() : Page(PG_CLEANUP, L"Очистка", L"Временные файлы, кэши, остатки обновлений и установщиков", Ico::Broom) {}
    void OnShow() override { if (!scannedOnce) Scan(); }
    void Scan() {
        if (scanning) return;
        scanning = true;
        auto copy = std::make_shared<vector<CleanCat>>(cats);
        Async([this, copy]() -> std::function<void()> {
            for (auto& c : *copy) CleanScan(c);
            return [this, copy] {
                for (size_t i = 0; i < cats.size() && i < copy->size(); ++i) {
                    cats[i].bytes = (*copy)[i].bytes;
                    cats[i].files = (*copy)[i].files;
                    cats[i].scanned = true;
                }
                scanning = false;
                scannedOnce = true;
            };
        });
    }
    uint64_t Selected() const {
        uint64_t s = 0;
        for (auto& c : cats) if (c.on) s += c.bytes;
        return s;
    }
    void Clean() {
        if (GuardBusy()) return;
        vector<CleanCat> sel;
        wstring names, warns;
        for (auto& c : cats) if (c.on && (c.bytes > 0 || !c.scanned)) {
            sel.push_back(c);
            names += L"• " + c.title + (c.scanned ? L" — " + HumanSize(c.bytes) : L"") + L"\n";
            if (!c.warn.empty()) warns += L"\n" + c.title + L": " + c.warn;
        }
        if (sel.empty()) { Message(L"Нечего очищать", L"Отметьте хотя бы одну категорию с найденными файлами.", Sev::Info); return; }
        Confirm(L"Удалить выбранные файлы?", names + (warns.empty() ? L"" : L"\nВнимание:" + warns) + L"\n\nЗанятые программами файлы будут пропущены. Удаление необратимо.",
                L"Очистить", [this, sel] {
            StartTask(L"Очистка диска", PG_CLEANUP, [sel](TaskCtx& c) {
                uint64_t total = 0, files = 0;
                wstring parts;
                for (auto& cat : sel) {
                    if (c.Cancelled()) break;
                    c.Line(L"Очищаю: " + cat.title + L"…");
                    uint64_t del = 0;
                    uint64_t freed = CleanRun(cat, [&](const wstring& l) { c.Line(l); }, del);
                    c.Line(L"  освобождено " + HumanSize(freed) + Fmt(L", удалено файлов: %llu", (unsigned long long)del));
                    total += freed;
                    files += del;
                    if (freed) parts += (parts.empty() ? L"" : L", ") + cat.title + L" " + HumanSize(freed);
                }
                c.Line(L"Итого освобождено: " + HumanSize(total));
                c.Done(L"освобождено " + HumanSize(total));
                PostUI([total, parts] { Journal::Add(L"cleanup", L"Очистка диска: освобождено " + HumanSize(total), parts); });
            }, [this](Task&) { Scan(); RequestSysInfo(true); });
        }, false, Sev::Warn);
    }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Очистить выбранное", [this] { Clean(); }, Btn::Primary, Ico::Broom, !scanning && !TaskBusy());
        ButtonRight(x - 10, y, 36, L"Анализ", [this] { Scan(); }, Btn::Secondary, Ico::Refresh, !scanning);
    }
    float Body(float x, float y, float w) override {

        RectF s(x, y, w, 96);
        Card(s);
        Text(L"Можно освободить (отмечено)", RectF(x + 24, y + 18, 400, 18), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
        if (scanning) {
            Spinner(x + 38, y + 60, 11, P.accent);
            Text(L"Считаю размер…", RectF(x + 60, y + 44, 300, 32), FontUI(16, 1), P.text, T_LEFT | T_VCENTER);
        } else Text(HumanSize(Selected()), RectF(x + 24, y + 40, 400, 40), FontUI(28, 2), P.text, T_LEFT | T_VCENTER);
        uint64_t all = 0;
        for (auto& c : cats) all += c.bytes;
        Text(L"Всего найдено: " + HumanSize(all), RectF(x + w - 324, y + 18, 300, 18), FontUI(12.5f), P.muted, T_RIGHT | T_VCENTER);
        for (auto& v : app.si.volumes)
            if (v.system) Text(L"Свободно на " + v.letter + L" " + HumanSize(v.free), RectF(x + w - 324, y + 44, 300, 30), FontUI(15, 1),
                               v.free < 15ull << 30 ? P.warn : P.ok, T_RIGHT | T_VCENTER);
        y += 112;

        for (auto& c : cats) {
            float tw = w - 220;
            float h = std::max(64.0f, RowTextH(tw, c.desc, c.warn) + 22);
            RectF r(x, y, w, h);
            bool hv = Hover(r);
            FillRound(r, 12, c.on ? (hv ? Blend(P.accent, P.surface, 0.12f) : Blend(P.accent, P.surface, 0.07f)) : hv ? P.surfaceHover : P.surface);
            if (c.on) StrokeRound(r, 12, Alpha(P.accent, 90));
            CheckBox(RectF(x + 18, y + 20, 20, 20), c.on);
            RowText(x + 54, y + 11, tw, c.title, c.desc, c.warn);
            wstring sz = !c.scanned ? (scanning ? L"…" : L"—") : c.bytes ? HumanSize(c.bytes) : L"пусто";
            Text(sz, RectF(r.GetRight() - 170, y, 150, h), FontUI(15, 1), c.bytes > (1ull << 30) ? P.accent : c.bytes ? P.text : P.muted, T_RIGHT | T_VCENTER);
            CleanCat* pc = &c;
            OnClick(r, [pc] { pc->on = !pc->on; });
            y += h + 8;
        }
        y += 8;
        float ch = ConsoleBox(x, y, w, 220, PG_CLEANUP);
        if (ch) y += ch + 16;
        y += Banner(x, y, w, Sev::Info, L"Ещё больше места",
                    L"Очистка хранилища компонентов Windows (WinSxS) и отключение гибернации освобождают ещё 2–10 ГБ.",
                    L"Ремонт Windows", [] { Navigate(PG_REPAIR); }) + 10;
        return y;
    }
};
Page* MakeCleanup() { return new CleanupPage; }

struct StartupPage : Page {
    vector<StartupItem> items;
    bool loading = false, loaded = false;
    StartupPage() : Page(PG_STARTUP, L"Автозагрузка", L"Программы, которые запускаются вместе с Windows", Ico::Bolt) {}
    void Load() {
        if (loading) return;
        loading = true;
        Async([this]() -> std::function<void()> {
            auto v = std::make_shared<vector<StartupItem>>(CollectStartup());
            return [this, v] {
                items = *v;
                loading = false;
                loaded = true;
                int n = 0;
                for (auto& i : items) if (i.enabled) ++n;
                app.si.startupCount = n;
            };
        });
    }
    void OnShow() override { Load(); }
    wstring NavBadge(Sev& s) override {
        if (!loaded) return L"";
        int n = 0;
        for (auto& i : items) if (i.enabled) ++n;
        s = n > 12 ? Sev::Warn : Sev::Info;
        return std::to_wstring(n);
    }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Обновить", [this] { Load(); }, Btn::Secondary, Ico::Refresh, !loading);
        ButtonRight(x - 10, y, 36, L"Диспетчер задач", [] { OpenSystemTool(L"taskmgr.exe", L"/0 /startup"); }, Btn::Secondary, Ico::Open);
    }
    float Body(float x, float y, float w) override {
        if (!loaded) return y + Loading(x, y, w, L"Читаю автозагрузку…");
        y += Banner(x, y, w, Sev::Info, L"Отключение безопасно и обратимо",
                    L"Это тот же переключатель, что в Диспетчере задач. Программа останется установленной, просто не будет стартовать вместе с Windows. "
                    L"Антивирус, драйверы звука и тачпада лучше оставить включёнными.") + 16;
        int en = 0;
        for (auto& i : items) if (i.enabled) ++en;
        y += SectionTitle(x, y, w, Fmt(L"Включено: %d из %d", en, (int)items.size()));
        for (size_t idx = 0; idx < items.size(); ++idx) {
            auto& it = items[idx];
            RectF r(x, y, w, 70);
            bool hv = Hover(r);
            FillRound(r, 12, hv ? P.surfaceHover : P.surface);
            StartupItem copy = it;
            Switch(RectF(x + 18, y + 24, 42, 22), it.enabled, [this, copy] {
                wstring err;
                if (StartupSetEnabled(copy, !copy.enabled, err)) {
                    for (auto& i : items)
                        if (i.root == copy.root && i.approvedPath == copy.approvedPath && i.approvedName == copy.approvedName) i.enabled = !copy.enabled;
                    int n = 0;
                    for (auto& i : items) if (i.enabled) ++n;
                    app.si.startupCount = n;
                    Toast((copy.enabled ? L"Отключено: " : L"Включено: ") + copy.name);
                } else Message(L"Не получилось", err, Sev::Warn);
            });
            wstring title = it.desc.empty() || IEquals(it.desc, it.name) ? it.name : it.name + L"  ·  " + it.desc;
            float tw = w - 420;
            DrawFileIcon(it.target, RectF(x + 76, y + 19, 32, 32), Ico::Apps);
            Text(title, RectF(x + 120, y + 10, tw, 20), FontUI(13.5f, 1), it.enabled ? P.text : P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text((it.publisher.empty() ? L"" : it.publisher + L"  ·  ") + it.location, RectF(x + 120, y + 30, tw, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(it.command, RectF(x + 120, y + 48, tw, 16), FontMono(11), Alpha(P.muted, 190), T_LEFT | T_VCENTER | T_ELLIPSIS);
            float bx = r.GetRight() - 14;
            IconButton(RectF(bx - 36, y + 17, 36, 36), Ico::Delete, [this, copy] {
                Confirm(L"Удалить «" + copy.name + L"» из автозагрузки?",
                        copy.missing ? L"Файл программы не найден — запись осталась от удалённой программы и её можно смело удалить."
                                     : L"Запись будет удалена (не программа!). Вернуть можно на странице «Откат и журнал». Обычно достаточно просто выключить переключатель.",
                        L"Удалить запись", [this, copy] {
                    wstring err;
                    if (StartupDelete(copy, err)) { Toast(L"Удалено из автозагрузки: " + copy.name); Load(); }
                    else Message(L"Не получилось", err, Sev::Warn);
                }, true);
            }, P.muted, 14, false, L"Удалить запись");
            bx -= 44;
            if (!it.missing) {
                wstring tgt = it.target;
                IconButton(RectF(bx - 36, y + 17, 36, 36), Ico::Folder, [tgt] { ShellOpen(L"explorer.exe", L"/select,\"" + tgt + L"\""); }, P.muted, 14, false, L"Показать файл");
                bx -= 44;
            }
            if (it.missing) Badge(bx, y + 24, L"файл не найден", Sev::Warn, 24, true);
            else if (!it.enabled) Badge(bx, y + 24, L"отключено", Sev::Unknown, 24, true);
            y += 78;
        }
        if (items.empty()) y += Banner(x, y, w, Sev::Ok, L"Автозагрузка пуста", L"") + 10;
        y += 8;
        Text(L"Задачи планировщика и службы здесь не показаны — для глубокой проверки используйте Autoruns (Sysinternals).",
             RectF(x, y, w, 20), FontUI(12), P.muted, T_LEFT | T_VCENTER);
        return y + 30;
    }
};
Page* MakeStartup() { return new StartupPage; }

struct ServicesPage : Page {
    vector<SvcDef> defs;
    std::map<wstring, SvcState> st;
    bool loaded = false;
    ServicesPage() : Page(PG_SERVICES, L"Службы", L"Отключение лишних служб Windows — с подсказками и откатом", Ico::Gears) {}
    void Refresh() {
        if (defs.empty()) defs = ServiceList();
        for (auto& d : defs) st[d.name] = SvcQuery(d.name);
        loaded = true;
    }
    void OnShow() override { Refresh(); }
    bool IsOptimized(const SvcDef& d) { return st[d.name].startType == d.target; }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Отключить безопасные", [this] {
            vector<SvcDef> todo;
            wstring list;
            for (auto& d : defs) if (d.risk == 0 && !IsOptimized(d)) { todo.push_back(d); list += L"• " + d.title + L"\n"; }
            if (todo.empty()) { Message(L"Уже сделано", L"Все безопасные к отключению службы уже настроены.", Sev::Ok); return; }
            Confirm(L"Настроить службы?", list + L"\nЭто службы без риска для обычного домашнего ПК. Каждое изменение можно откатить.", L"Применить", [this, todo] {
                int ok = 0;
                wstring err;
                for (auto& d : todo) if (ServiceOptimize(d, err)) ++ok;
                Refresh();
                Toast(Fmt(L"Изменено служб: %d", ok), ok == (int)todo.size() ? Sev::Ok : Sev::Warn);
            }, false, Sev::Info);
        }, Btn::Primary, Ico::Gears);
        ButtonRight(x - 10, y, 36, L"Все службы", [] { OpenSystemTool(L"services.msc"); }, Btn::Secondary, Ico::Open);
    }
    float Body(float x, float y, float w) override {
        y += RestorePointBanner(x, y, w, PG_SERVICES);
        y += 6;
        for (auto& d : defs) {
            auto& s = st[d.name];
            bool opt = IsOptimized(d);
            float tw = w - 400;
            float h = std::max(76.0f, RowTextH(tw, d.desc, d.risk ? d.warn : L"") + 22);
            RectF r(x, y, w, h);
            FillRound(r, 12, Hover(r) ? P.surfaceHover : P.surface);
            SevIcon(opt ? Sev::Ok : d.risk ? Sev::Unknown : Sev::Info, x + 16, y + (h - 30) / 2, 30);
            RowText(x + 62, y + 11, tw, d.title, d.desc, d.risk ? d.warn : L"");
            float cx = r.GetRight() - 320;
            Text(SvcStartName(s.startType, s.delayed), RectF(cx, y + 14, 178, 20), FontUI(13, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(s.state == SERVICE_RUNNING ? L"работает" : L"остановлена", RectF(cx, y + 34, 150, 18), FontUI(12), s.state == SERVICE_RUNNING ? P.ok : P.muted, T_LEFT | T_VCENTER);
            Text(d.name, RectF(cx, y + 52, 150, 16), FontMono(11), Alpha(P.muted, 190), T_LEFT | T_VCENTER);
            SvcDef copy = d;
            if (opt) Button(RectF(r.GetRight() - 136, y + (h - 34) / 2, 122, 34), L"Вернуть", [this, copy] {
                wstring err;
                if (ServiceRestore(copy, err)) { Refresh(); Toast(L"Служба возвращена: " + copy.title); }
                else Message(L"Не получилось", err, Sev::Warn);
            }, Btn::Secondary, Ico::Undo);
            else Button(RectF(r.GetRight() - 136, y + (h - 34) / 2, 122, 34), copy.target == SERVICE_DISABLED ? L"Отключить" : L"Вручную", [this, copy] {
                auto go = [this, copy] {
                    wstring err;
                    if (ServiceOptimize(copy, err)) { Refresh(); Toast(L"Готово: " + copy.title); }
                    else Message(L"Не получилось", err, Sev::Warn);
                };
                if (copy.risk) Confirm(L"Отключить «" + copy.title + L"»?", copy.warn + L"\n\nВернуть можно кнопкой «Вернуть» или на странице «Откат и журнал».", L"Отключить", go, false, Sev::Warn);
                else go();
            }, copy.risk ? Btn::Secondary : Btn::Soft);
            y += h + 8;
        }
        return y + 10;
    }
};
Page* MakeServices() { return new ServicesPage; }

struct TweaksPage : Page {
    vector<Tweak> tweaks;
    vector<int> states;
    vector<PowerPlan> plans;
    bool explorerPending = false;
    TweaksPage() : Page(PG_TWEAKS, L"Твики и питание", L"Настройки Windows для скорости и удобства — каждую можно вернуть обратно", Ico::Sliders) {}
    void Refresh() {
        tweaks = TweakList();
        states.clear();
        for (auto& t : tweaks) states.push_back(TweakState(t));
        plans = PowerPlans();
    }
    void OnShow() override { Refresh(); }
    void Header(float right, float y) override {
        ButtonRight(right, y, 36, L"Параметры питания", [] { OpenSystemTool(L"control.exe", L"powercfg.cpl"); }, Btn::Secondary, Ico::Open);
    }
    void Toggle(size_t i) {
        Tweak t = tweaks[i];
        bool on = states[i] != 1;
        auto go = [this, t, on] {
            wstring err;
            bool ok = on ? TweakApply(t, err) : TweakRevert(t, err);
            if (!ok) { Message(L"Не получилось", err, Sev::Warn); return; }
            if (t.needExplorer) explorerPending = true;
            Refresh();
            Toast((on ? L"Применено: " : L"Возвращено: ") + t.title);
        };
        if (on && t.risky) Confirm(L"Применить «" + t.title + L"»?", t.desc + L"\n\nНастройка помечена «!» — убедитесь, что она подходит клиенту. Вернуть можно тем же переключателем.", L"Применить", go, false, Sev::Warn);
        else go();
    }
    float Body(float x, float y, float w) override {
        y += RestorePointBanner(x, y, w, PG_TWEAKS);
        if (explorerPending)
            y += Banner(x, y, w, Sev::Info, L"Часть настроек применится после перезапуска Проводника",
                        L"Панель задач и окна папок на секунду пропадут и появятся снова. Открытые программы не закроются.",
                        L"Перезапустить Проводник", [this] { explorerPending = false; RestartExplorer(); Toast(L"Проводник перезапущен"); }) + 12;

        y += SectionTitle(x, y, w, L"Схема электропитания", L"«Высокая производительность» держит частоту процессора — для игровых и рабочих ПК. На ноутбуке от батареи лучше «Сбалансированная».");
        float pw = (w - 16 * 2) / 3;
        int i = 0;
        for (auto& p : plans) {
            float px = x + (i % 3) * (pw + 16), py = y + (i / 3) * 74;
            RectF r(px, py, pw, 62);
            bool hv = Hover(r);
            if (p.active) {
                GradientH(r, 12, Alpha(P.accent, g_dark ? 70 : 40), Alpha(P.accent2, g_dark ? 60 : 32));
                StrokeRound(r, 12, Alpha(P.accent, 150), 1.4f);
            } else {
                FillRound(r, 12, hv ? P.surfaceHover : P.surface);
                StrokeRound(r, 12, P.border);
            }

            SolidBrush rb(p.active ? P.accent : P.card);
            ui.g->FillEllipse(&rb, px + 18, py + 21, 20.0f, 20.0f);
            Pen rp(p.active ? P.accent : Blend(P.muted, P.card, 0.6f), 1.4f);
            ui.g->DrawEllipse(&rp, px + 18, py + 21, 20.0f, 20.0f);
            if (p.active) { SolidBrush wb(Color(255, 255, 255, 255)); ui.g->FillEllipse(&wb, px + 24, py + 27, 8.0f, 8.0f); }
            Text(p.name, RectF(px + 50, py + 10, pw - 60, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(p.active ? L"используется сейчас" : L"нажмите, чтобы включить", RectF(px + 50, py + 32, pw - 60, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER);
            if (!p.active) {
                GUID g = p.guid;
                OnClick(r, [this, g] {
                    wstring err;
                    if (PowerActivate(g, err)) { Refresh(); RequestSysInfo(true); Toast(L"Схема питания изменена"); }
                    else Message(L"Не получилось", err, Sev::Warn);
                });
            }
            ++i;
        }
        y += ((i + 2) / 3) * 74;
        bool hasUlt = false;
        for (auto& p : plans) if (IContains(p.name, L"Максимальная") || IContains(p.name, L"Ultimate")) hasUlt = true;
        if (!hasUlt) {
            Button(RectF(x, y, ButtonW(L"Добавить «Максимальная производительность»", Ico::Bolt), 36), L"Добавить «Максимальная производительность»", [this] {
                wstring err;
                if (AddUltimatePlan(err)) { Refresh(); Toast(L"Схема добавлена — выберите её выше"); }
                else Message(L"Не получилось", err, Sev::Warn);
            }, Btn::Soft, Ico::Bolt);
            y += 48;
        }
        y += 16;

        y += SectionTitle(x, y, w, L"Настройки Windows", L"Переключатель включает настройку, повторное нажатие возвращает как было. Знак «!» — подумайте, нужно ли это клиенту.");
        float cw = (w - 16) / 2;
        float colY[2] = { y, y };
        for (size_t k = 0; k < tweaks.size(); ++k) {
            auto& t = tweaks[k];
            int col = colY[0] <= colY[1] ? 0 : 1;
            float cx = x + col * (cw + 16), cy = colY[col];
            float th = TextH(t.desc, FontUI(12.5f), cw - 110);
            float h = std::max(78.0f, th + 46);
            RectF r(cx, cy, cw, h);
            bool on = states[k] == 1;
            bool hv = Hover(r);
            FillRound(r, 12, on ? (g_dark ? Blend(P.accent, P.surface, 0.12f) : Blend(P.accent, P.card, 0.07f)) : hv ? P.surfaceHover : P.surface);
            StrokeRound(r, 12, on ? Alpha(P.accent, 110) : P.border);
            float tx = cx + 18;
            Text(t.title, RectF(tx, cy + 12, cw - 120, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            if (t.risky) {
                float tw0 = std::min(TextW(t.title, FontUI(13.5f, 1)), cw - 120);
                Text(L"!", RectF(tx + tw0 + 6, cy + 12, 14, 22), FontUI(15, 2), P.danger, T_LEFT | T_VCENTER);
            }
            Text(t.desc, RectF(tx, cy + 36, cw - 110, th + 4), FontUI(12.5f), P.muted, T_WRAP);
            size_t idx = k;
            Switch(RectF(r.GetRight() - 62, cy + 16, 44, 24), on, [this, idx] { Toggle(idx); });
            if (states[k] == -1) Text(L"частично", RectF(r.GetRight() - 80, cy + 42, 70, 16), FontUI(11), P.warn, T_RIGHT | T_VCENTER);
            colY[col] += h + 12;
        }
        y = std::max(colY[0], colY[1]);
        return y + 10;
    }
};
Page* MakeTweaks() { return new TweaksPage; }

static void InstallFromFile() {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"Установщики (*.exe, *.msi)\0*.exe;*.msi\0Все файлы\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    wstring dir = ExeDir();
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrTitle = L"Выберите установщик программы";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    wstring p = file;
    wstring d = p.substr(0, p.find_last_of(L'\\'));
    if (ShellOpen(p, L"", d)) {
        Journal::Add(L"install", L"Запущена установка: " + p.substr(p.find_last_of(L'\\') + 1), p);
        Toast(L"Установщик запущен — следуйте его окну");
    } else Message(L"Не удалось запустить", p, Sev::Warn);
}

struct AppsPage : Page {
    int tab = 2;
    vector<AppxApp> appx;
    vector<Win32App> progs;
    bool appxLoading = false, appxLoaded = false, progsLoaded = false;
    wstring filter;
    bool showOther = false;
    bool showSystem = false;
    struct Useful { const wchar_t* cat; const wchar_t* id; const wchar_t* name; const wchar_t* desc; bool on; };
    vector<Useful> useful = {

        { L"Браузеры", L"Brave.Brave",               L"Brave",           L"Браузер с блокировкой рекламы",   false },
        { L"Браузеры", L"Google.Chrome",             L"Google Chrome",   L"Популярный браузер",              false },
        { L"Браузеры", L"Mozilla.Firefox",           L"Mozilla Firefox", L"Браузер",                         false },

        { L"Связь", L"Telegram.TelegramDesktop",     L"Telegram",        L"Мессенджер",                      false },
        { L"Связь", L"Discord.Discord",              L"Discord",         L"Голосовой чат для игр",           false },
        { L"Связь", L"WhatsApp.WhatsApp",            L"WhatsApp",        L"Мессенджер",                      false },
        { L"Связь", L"Zoom.Zoom",                    L"Zoom",            L"Видеозвонки и конференции",        false },

        { L"Система и диагностика", L"Microsoft.PowerToys",            L"PowerToys",        L"Набор утилит Microsoft",          false },
        { L"Система и диагностика", L"voidtools.Everything",           L"Everything",       L"Мгновенный поиск файлов",         false },
        { L"Система и диагностика", L"CrystalDewWorld.CrystalDiskInfo",L"CrystalDiskInfo",  L"Здоровье SSD/HDD (SMART)",        false },
        { L"Система и диагностика", L"REALiX.HWiNFO",                  L"HWiNFO",           L"Полный мониторинг железа",        false },
        { L"Система и диагностика", L"CPUID.CPU-Z",                    L"CPU-Z",            L"Инфо о процессоре и памяти",      false },
        { L"Система и диагностика", L"TechPowerUp.GPU-Z",              L"GPU-Z",            L"Инфо о видеокарте",               false },
        { L"Система и диагностика", L"AntibodySoftware.WizTree",       L"WizTree",          L"Что занимает место на диске",     false },
        { L"Система и диагностика", L"Rufus.Rufus",                    L"Rufus",            L"Запись загрузочных флешек",       false },
        { L"Система и диагностика", L"Microsoft.Sysinternals.ProcessExplorer", L"Process Explorer", L"Продвинутый диспетчер задач", false },

        { L"Мониторинг и запись", L"Guru3D.Afterburner",   L"MSI Afterburner", L"Разгон и мониторинг (FPS/темп)",  false },
        { L"Мониторинг и запись", L"OBSProject.OBSStudio",  L"OBS Studio",     L"Запись экрана и стримы",          false },

        { L"Медиа", L"VideoLAN.VLC",   L"VLC",          L"Видеоплеер",                      false },
        { L"Медиа", L"Yandex.Music",   L"Яндекс Музыка", L"Музыкальный сервис",             false },
        { L"Медиа", L"Spotify.Spotify", L"Spotify",     L"Музыкальный сервис",              false },

        { L"Файлы и документы", L"7zip.7zip",                  L"7-Zip",       L"Архиватор",                        false },
        { L"Файлы и документы", L"RARLab.WinRAR",              L"WinRAR",      L"Архиватор — с вашим ключом",       false },
        { L"Файлы и документы", L"qBittorrent.qBittorrent",    L"qBittorrent", L"Торрент-клиент",                   false },
        { L"Файлы и документы", L"Notepad++.Notepad++",        L"Notepad++",   L"Текстовый редактор",               false },
        { L"Файлы и документы", L"Adobe.Acrobat.Reader.64-bit",L"Acrobat Reader", L"Просмотр PDF",                  false },

        { L"Компоненты для игр и программ", L"Microsoft.VCRedist.2015+.x64",       L"Visual C++ 2015–2022", L"Нужен многим играм и программам", false },
        { L"Компоненты для игр и программ", L"Microsoft.DotNet.DesktopRuntime.8",  L".NET Desktop Runtime 8", L"Среда выполнения .NET",        false },
        { L"Компоненты для игр и программ", L"Microsoft.DirectX",                  L"DirectX (June 2010)",  L"Компоненты DirectX для игр",     false },

        { L"Игры и удалёнка", L"Valve.Steam",                 L"Steam",       L"Игровая платформа",                false },
        { L"Игры и удалёнка", L"EpicGames.EpicGamesLauncher", L"Epic Games",  L"Игровая платформа",                false },
        { L"Игры и удалёнка", L"AnyDesk.AnyDesk",             L"AnyDesk",     L"Удалённый доступ",                 false },
    };
    AppsPage() : Page(PG_APPS, L"Приложения", L"Установка полезных программ и удаление лишних", Ico::Apps) {}
    void UpgradeAll() {
        if (GuardBusy()) return;
        Confirm(L"Обновить все программы?",
                L"winget обновит все программы, для которых вышли новые версии (нужен интернет). Идёт тихо и по очереди, может занять время. Прогресс — в консоли ниже.",
                L"Обновить", [] {
            if (GuardBusy()) return;
            StartTask(L"Обновление программ (winget)", PG_APPS, [](TaskCtx& c) {
                wstring wg = ExpandEnv(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\winget.exe");
                if (!FileExists(wg)) wg = L"winget";
                int code = c.Run(L"\"" + wg + L"\" upgrade --all --silent --accept-source-agreements --accept-package-agreements --include-unknown");
                c.Done(Fmt(L"winget завершился (код 0x%X)", (unsigned)code));
            }, [](Task& t) { Journal::Add(L"install", L"Обновление всех программ (winget upgrade --all)", t.summary); });
        }, false, Sev::Info);
    }
    void InstallUseful() {
        if (GuardBusy()) return;
        vector<Useful> sel;
        wstring list;
        for (auto& u : useful) if (u.on) { sel.push_back(u); list += L"• " + wstring(u.name) + L"\n"; }
        if (sel.empty()) { Message(L"Ничего не выбрано", L"Отметьте программы, которые нужно установить.", Sev::Info); return; }
        Confirm(Fmt(L"Установить программы (%d)?", (int)sel.size()),
                list + L"\nСтавятся тихо и по очереди через winget (нужен интернет). Прогресс — в консоли ниже.",
                L"Установить", [this, sel] {
            StartTask(L"Установка программ", PG_APPS, [sel](TaskCtx& c) {
                wstring wg = ExpandEnv(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\winget.exe");
                if (!FileExists(wg)) wg = L"winget";
                int ok = 0, fail = 0;
                for (auto& u : sel) {
                    if (c.Cancelled()) break;
                    c.Line(L"");
                    c.Line(L"=== Устанавливаю: " + wstring(u.name) + L" ===");
                    wstring cmd = L"\"" + wg + L"\" install --id " + wstring(u.id) +
                                  L" -e --silent --accept-source-agreements --accept-package-agreements";
                    int code = c.Run(cmd, Enc::Auto);
                    bool good = (code == 0) || (code == (int)0x8A15002B);
                    if (good) {
                        ++ok; c.Line(L"OK: " + wstring(u.name)); Journal::Add(L"install", L"Установлена программа: " + wstring(u.name), u.id);
                        if (wstring(u.id) == L"RARLab.WinRAR") {
                            wstring key = FindRarKey();
                            if (key.empty()) c.Line(L"  rarreg.key не найден рядом с программой — ключ не установлен");
                            else { int m = DeployRarKey(key); c.Line(m ? Fmt(L"  ключ WinRAR установлен (%d)", m) : L"  папка WinRAR не найдена — ключ не скопирован"); }
                        }
                        if (wstring(u.id) == L"Brave.Brave") ConfigureBrave(c);
                    }
                    else { ++fail; c.Line(Fmt(L"Ошибка (код 0x%X): ", (unsigned)code) + u.name); }
                }
                if (fail) c.Fail(Fmt(L"установлено %d, с ошибкой %d", ok, fail));
                else c.Done(Fmt(L"установлено %d", ok));
            });
        }, false, Sev::Info);
    }
    void LoadAppx() {
        if (appxLoading) return;
        appxLoading = true;
        Async([this]() -> std::function<void()> {
            auto v = std::make_shared<vector<AppxApp>>(CollectAppx());
            return [this, v] { appx = *v; appxLoading = false; appxLoaded = true; };
        });
    }
    void LoadProgs() {
        progs = CollectWin32Apps(showSystem);
        progsLoaded = true;
    }

    void DeepRemove(const Win32App& a) {
        if (GuardBusy()) return;
        DeepScan sc = ScanAppTraces(a);
        wstring list;
        if (a.hasUninstall) list += L"• штатный деинсталлятор программы\n";
        for (auto& k : sc.keys) list += L"• реестр: " + k.display + L"\n";
        for (auto& d : sc.dirs) list += L"• папка: " + d + L"\n";
        if (list.empty()) list = L"• явных следов не найдено\n";
        wstring warn = a.system ? L"\n⚠ Это системный компонент — удаление может повредить Windows.\n" : L"";
        Confirm(L"Снести «" + a.name + L"» полностью?",
                L"Будет удалено:\n" + list + warn +
                L"\nВетки реестра сохранятся в .reg (папка backup в %ProgramData%\\ToolboxPC). Удаление файлов необратимо."
                L"\n\n✔ TOOLBOX сам компьютер НЕ перезагружает. Если деинсталлятор программы попросит перезагрузку — откажитесь и перезагрузите вручную позже. На всякий случай сохраните открытые документы.",
                L"Снести полностью", [this, a] {
            StartTask(L"Глубокое удаление: " + a.name, PG_APPS, [a](TaskCtx& c) {
                if (a.hasUninstall) {
                    c.Line(L"Запускаю штатный деинсталлятор (без перезагрузки)…");
                    wstring cmd = MsiNoReboot(a.quiet.empty() ? a.uninstall : a.quiet);
                    if (IContains(cmd, L"msiexec") && !IContains(cmd, L"/quiet") && !IContains(cmd, L"/qn")) cmd += L" /quiet";
                    c.Run(cmd);
                }

                DeepScan sc = ScanAppTraces(a);
                wstring stamp = NowStamp();
                wstring bdir = BackupDir() + L"\\uninstall-" + SafeName(a.name) + L"-" + stamp;
                CreateDirectoryW(bdir.c_str(), nullptr);
                int rk = 0, rd = 0, i = 0;
                for (auto& k : sc.keys) {
                    if (c.Cancelled()) break;
                    wstring file = bdir + Fmt(L"\\key%02d.reg", ++i);
                    RegExportKey(k, file);
                    if (RegDeleteRef(k)) { ++rk; c.Line(L"реестр удалён: " + k.display); }
                    else c.Line(L"реестр не удалён (нет прав?): " + k.display);
                }
                for (auto& d : sc.dirs) {
                    if (c.Cancelled()) break;
                    if (DeleteTree(d)) { ++rd; c.Line(L"папка удалена: " + d); }
                    else c.Line(L"папка занята/не удалена: " + d);
                }
                Journal::Add(L"deep-uninstall", L"Полное удаление: " + a.name,
                             Fmt(L"реестр %d, папки %d · бэкап: ", rk, rd) + bdir);
                c.Done(Fmt(L"снесено: реестр %d, папки %d", rk, rd));
            }, [this](Task&) { LoadProgs(); });
        }, true);
    }
    void OnShow() override {
        if (!appxLoaded) LoadAppx();
        LoadProgs();
    }
    wstring NavBadge(Sev& s) override {
        int n = 0;
        for (auto& p : progs) if (p.junk) ++n;
        for (auto& a : appx) if (a.junk) ++n;
        s = Sev::Info;
        return n ? std::to_wstring(n) : L"";
    }
    void RemoveAppx() {
        if (GuardBusy()) return;
        vector<AppxApp> sel;
        wstring list;
        for (auto& a : appx) if (a.on) { sel.push_back(a); list += L"• " + a.title + L"\n"; }
        if (sel.empty()) { Message(L"Ничего не выбрано", L"Отметьте приложения, которые нужно удалить.", Sev::Info); return; }
        Confirm(Fmt(L"Удалить приложения (%d)?", (int)sel.size()), list +
                L"\nПриложения удаляются для всех пользователей и не вернутся у новых. Вернуть любое можно бесплатно из Microsoft Store (ссылки сохранятся в журнале).",
                L"Удалить", [this, sel] {
            StartTask(L"Удаление встроенных приложений", PG_APPS, [sel](TaskCtx& c) {

                const wchar_t* body =
                    L"foreach ($n in $names) {\n"
                    L"  'Удаляю ' + $n\n"
                    L"  foreach ($p in @(Get-AppxPackage -AllUsers -Name $n)) { try { Remove-AppxPackage -Package $p.PackageFullName -AllUsers -ErrorAction Stop } "
                    L"catch { try { Remove-AppxPackage -Package $p.PackageFullName -ErrorAction Stop } catch { '  ошибка: ' + $_.Exception.Message } } }\n"
                    L"  try { Get-AppxProvisionedPackage -Online | Where-Object DisplayName -eq $n | Remove-AppxProvisionedPackage -Online -ErrorAction Stop | Out-Null } catch { }\n"
                    L"  if (@(Get-AppxPackage -AllUsers -Name $n).Count -eq 0) { 'OK|' + $n } else { 'FAIL|' + $n }\n"
                    L"}\n";
                for (size_t i = 0; i < sel.size() && !c.Cancelled(); i += 20) {
                    wstring names;
                    for (size_t j = i; j < sel.size() && j < i + 20; ++j) names += (names.empty() ? L"'" : L",'") + sel[j].name + L"'";
                    if (c.PS(L"$names = @(" + names + L")\n" + body) == -1) c.Line(L"Не удалось запустить PowerShell");
                }
            }, [this, sel](Task& t) {
                int ok = 0;
                for (auto& l : t.lines) {
                    if (IStarts(l, L"OK|")) {
                        wstring n = l.substr(3);
                        for (auto& a : sel)
                            if (a.name == n) Journal::Add(L"appx", L"Удалено приложение: " + a.title, a.name, L"store", a.family);
                        ++ok;
                    }
                }
                int fail = (int)sel.size() - ok;
                t.summary = Fmt(L"удалено %d", ok) + (fail ? Fmt(L", не удалось %d", fail) : L"");
                t.ok = fail == 0;
                appxLoaded = false;
                LoadAppx();
            });
        }, true);
    }
    void Uninstall(const Win32App& a) {
        Confirm(L"Удалить «" + a.name + L"»?", L"Откроется штатный деинсталлятор программы — дальше следуйте его окну. "
                L"Откатить удаление нельзя (только установить программу заново). "
                L"Если он предложит перезагрузку — лучше откажитесь и перезагрузите вручную позже.", L"Запустить удаление", [this, a] {
            STARTUPINFOW si{ sizeof(si) };
            PROCESS_INFORMATION pi{};
            wstring cmd = MsiNoReboot(ExpandEnv(a.uninstall));
            if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                Journal::Add(L"uninstall", L"Запущено удаление программы: " + a.name, a.publisher + L" " + a.version);
                Toast(L"Открыт деинсталлятор: " + a.name);
            } else Message(L"Не удалось запустить", L"Команда: " + cmd, Sev::Warn);
        }, true);
    }
    void Header(float right, float y) override {
        if (tab == 0) {
            int n = 0;
            for (auto& a : appx) if (a.on) ++n;
            float x = ButtonRight(right, y, 36, Fmt(L"Удалить отмеченные (%d)", n), [this] { RemoveAppx(); }, Btn::Danger, Ico::Delete, n > 0 && !TaskBusy());
            ButtonRight(x - 10, y, 36, L"Обновить", [this] { LoadAppx(); }, Btn::Secondary, Ico::Refresh, !appxLoading);
        } else if (tab == 1) {
            float x = ButtonRight(right, y, 36, L"Программы и компоненты", [] { OpenSystemTool(L"control.exe", L"appwiz.cpl"); }, Btn::Secondary, Ico::Open);
            x = ButtonRight(x - 10, y, 36, L"Установить из файла", [] { InstallFromFile(); }, Btn::Secondary, Ico::Download);
            x = ButtonRight(x - 10, y, 36, showSystem ? L"Скрыть системные" : L"Показать системные",
                            [this] { showSystem = !showSystem; LoadProgs(); }, showSystem ? Btn::Primary : Btn::Secondary, Ico::Shield);
            ButtonRight(x - 10, y, 36, L"Обновить", [this] { LoadProgs(); }, Btn::Secondary, Ico::Refresh);
        } else {
            int n = 0;
            for (auto& u : useful) if (u.on) ++n;
            float bx = ButtonRight(right, y, 36, Fmt(L"Установить выбранные (%d)", n), [this] { InstallUseful(); }, Btn::Primary, Ico::Download, n > 0 && !TaskBusy());
            ButtonRight(bx - 10, y, 36, L"Обновить все программы", [this] { UpgradeAll(); }, Btn::Secondary, Ico::Refresh, !TaskBusy());
        }
    }
    float Body(float x, float y, float w) override {

        struct Tb { const wchar_t* label; int idx; };
        Tb tabs[] = { { L"Полезные программы", 2 }, { L"Встроенные приложения Windows", 0 } };
        float tx = x;
        for (auto& t : tabs) {
            float tw = TextW(t.label, FontUI(13.5f, 1)) + 34;
            RectF r(tx, y, tw, 38);
            if (tab == t.idx) { GradientH(r, 10, P.accent, P.accent2); Text(t.label, r, FontUI(13.5f, 1), Color(255, 255, 255, 255), T_CENTER | T_VCENTER); }
            else { FillRound(r, 10, Hover(r) ? P.surfaceHover : P.surface); Text(t.label, r, FontUI(13.5f, 1), P.text, T_CENTER | T_VCENTER); }
            int id = t.idx; OnClick(r, [this, id] { tab = id; });
            tx += tw + 8;
        }
        y += 54;
        if (tab == 0) {
            if (!appxLoaded) return y + Loading(x, y, w, L"Получаю список приложений (PowerShell)…");
            y += Banner(x, y, w, Sev::Info, L"Мусорные приложения отмечены автоматически",
                        L"Системные компоненты (Магазин, Фотографии, Калькулятор, кодеки) в списке не показываются. Всё удалённое можно бесплатно вернуть из Microsoft Store.") + 14;
            float cw = (w - 16) / 2;
            int i = 0, other = 0;
            float rowH = 58;
            for (auto& a : appx) if (!a.known && !a.junk && !a.on) ++other;
            for (auto& a : appx) {
                if (!showOther && !a.known && !a.junk && !a.on) continue;
                float cx = x + (i % 2) * (cw + 16), cy = y + (i / 2) * (rowH + 8);
                RectF r(cx, cy, cw, rowH);
                bool hv = Hover(r);
                FillRound(r, 10, a.on ? Blend(P.danger, P.surface, g_dark ? 0.12f : 0.06f) : hv ? P.surfaceHover : P.surface);
                if (a.on) StrokeRound(r, 10, Alpha(P.danger, 90));
                CheckBox(RectF(cx + 16, cy + 19, 20, 20), a.on);
                Text(a.title, RectF(cx + 50, cy + 8, cw - 140, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
                Text(a.hint.empty() ? a.name : a.hint, RectF(cx + 50, cy + 30, cw - 66, 18), FontUI(12), a.hint.empty() ? P.muted : P.warn, T_LEFT | T_VCENTER | T_ELLIPSIS);
                if (a.junk) Badge(r.GetRight() - 12, cy + 10, L"мусор", Sev::Warn, 22, true);
                wstring nm = a.name;
                OnClick(r, [this, nm] { for (auto& x : appx) if (x.name == nm) x.on = !x.on; });
                ++i;
            }
            y += ((i + 1) / 2) * (rowH + 8) + 8;
            if (other > 0) {
                wstring lbl = showOther ? L"Скрыть остальные приложения" : Fmt(L"Показать остальные приложения (%d)", other);
                Button(RectF(x, y, ButtonW(lbl, Ico::ChevronDown), 36), lbl, [this] { showOther = !showOther; }, Btn::Soft, showOther ? Ico::ChevronUp : Ico::ChevronDown);
                y += 52;
            }
            float ch = ConsoleBox(x, y, w, 220, PG_APPS);
            if (ch) y += ch + 16;
            return y;
        }
        if (tab == 2) {
            y += Banner(x, y, w, Sev::Info, L"Полезные программы",
                        L"Отметьте нужные и нажмите «Установить выбранные» вверху. Ставятся тихо через winget — нужен интернет.") + 14;
            auto brandColor = [](const wchar_t* id) -> Color {
                struct { const wchar_t* k; BYTE r, g, b; } m[] = {
                    { L"Brave", 251, 84, 43 }, { L"Google.Chrome", 26, 115, 232 }, { L"Mozilla.Firefox", 255, 113, 57 },
                    { L"Telegram", 34, 158, 217 }, { L"Discord", 88, 101, 242 }, { L"Valve.Steam", 27, 40, 56 },
                    { L"Yandex", 252, 63, 29 }, { L"7zip", 60, 60, 66 }, { L"RARLab", 150, 40, 40 }, { L"VideoLAN", 255, 136, 0 },
                    { L"WhatsApp", 37, 211, 102 }, { L"Zoom", 45, 140, 255 }, { L"PowerToys", 15, 23, 42 },
                    { L"Everything", 52, 120, 246 }, { L"CrystalDisk", 37, 99, 235 }, { L"HWiNFO", 0, 120, 120 },
                    { L"CPU-Z", 107, 33, 168 }, { L"GPU-Z", 51, 65, 85 }, { L"WizTree", 16, 122, 110 },
                    { L"Rufus", 180, 70, 20 }, { L"ProcessExplorer", 20, 110, 200 }, { L"Afterburner", 185, 28, 28 },
                    { L"OBS", 48, 46, 49 }, { L"Spotify", 29, 185, 84 }, { L"qBittorrent", 47, 103, 186 },
                    { L"Notepad", 132, 204, 22 }, { L"Acrobat", 236, 28, 36 }, { L"VCRedist", 104, 33, 122 },
                    { L"DotNet", 81, 43, 212 }, { L"DirectX", 16, 124, 16 }, { L"Epic", 42, 42, 42 }, { L"AnyDesk", 239, 68, 59 },
                };
                for (auto& e : m) if (IContains(id, e.k)) return Color(255, e.r, e.g, e.b);
                return P.accent;
            };
            float cw = (w - 16) / 2;
            float rowH = 58;
            const wchar_t* curCat = nullptr;
            float catTop = y;
            int colInCat = 0;
            auto flush = [&] { if (curCat && colInCat > 0) y = catTop + (float)((colInCat + 1) / 2) * (rowH + 8); };
            for (auto& u : useful) {
                if (!curCat || wcscmp(curCat, u.cat) != 0) {
                    flush();
                    y += 6;
                    y += SectionTitle(x, y, w, u.cat);
                    catTop = y; colInCat = 0; curCat = u.cat;
                }
                float cx = x + (colInCat % 2) * (cw + 16), cy = catTop + (colInCat / 2) * (rowH + 8);
                RectF r(cx, cy, cw, rowH);
                bool hv = Hover(r);
                FillRound(r, 10, u.on ? Blend(P.accent, P.surface, g_dark ? 0.14f : 0.07f) : hv ? P.surfaceHover : P.surface);
                if (u.on) StrokeRound(r, 10, Alpha(P.accent, 110));
                CheckBox(RectF(cx + 14, cy + 19, 20, 20), u.on);
                RectF ic(cx + 44, cy + 15, 28, 28);
                Bitmap* logo = AppIconRes(UsefulIcon(u.id));
                if (logo) {
                    float iw = (float)logo->GetWidth(), ih = (float)logo->GetHeight();
                    float s = std::min(ic.Width / iw, ic.Height / ih);
                    float dw = iw * s, dh = ih * s;
                    ui.g->DrawImage(logo, RectF(ic.X + (ic.Width - dw) / 2, ic.Y + (ic.Height - dh) / 2, dw, dh));
                } else {
                    FillRound(ic, 7, brandColor(u.id));
                    Text(wstring(1, (wchar_t)towupper(u.name[0])), ic, FontUI(15, 2), Color(255, 255, 255, 255), T_CENTER | T_VCENTER);
                }
                Text(u.name, RectF(cx + 82, cy + 8, cw - 98, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
                Text(u.desc, RectF(cx + 82, cy + 30, cw - 98, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
                const wchar_t* id = u.id;
                OnClick(r, [this, id] { for (auto& z : useful) if (z.id == id) z.on = !z.on; });
                ++colInCat;
            }
            flush();
            y += 14;
            float ch = ConsoleBox(x, y, w, 240, PG_APPS);
            if (ch) y += ch + 16;
            return y;
        }

        TextBox(501, RectF(x, y, 360, 38), filter, L"Поиск по названию или издателю…", 80);
        Glyph(Ico::Search, RectF(x + 326, y, 30, 38), 13, P.muted);
        int junk = 0;
        for (auto& p : progs) if (p.junk) ++junk;
        Text(Fmt(L"Программ: %d", (int)progs.size()) + (junk ? Fmt(L"  ·  подозрительных: %d", junk) : L""), RectF(x + 380, y, w - 380, 38), FontUI(13), P.muted, T_LEFT | T_VCENTER);
        y += 54;
        for (auto& p : progs) {
            if (!filter.empty() && !IContains(p.name, filter) && !IContains(p.publisher, filter)) continue;
            float h = 58;
            RectF r(x, y, w, h);
            if (!Visible(r)) { y += h + 8; continue; }
            bool hv = Hover(r);
            Color bg = p.junk ? Blend(P.warn, P.surface, g_dark ? 0.13f : 0.07f) : hv ? P.surfaceHover : P.surface;
            FillRound(r, 12, bg);
            if (p.junk) StrokeRound(r, 12, Alpha(P.warn, 95));
            else if (hv) StrokeRound(r, 12, Alpha(P.accent, 45));

            RectF ic(r.X + 12, r.Y + 11, 36, 36);
            FillRound(ic, 9, Alpha(P.card, g_dark ? 150 : 210));
            DrawFileIcon(p.icon, RectF(ic.X + 6, ic.Y + 6, 24, 24), Ico::Apps);
            float nx = r.X + 62, nw = w * 0.5f;
            Text(p.name, RectF(nx, r.Y + 9, nw, 20), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            if (p.junk) {
                Glyph(Ico::Warning, RectF(nx, r.Y + 33, 15, 15), 11, P.warn);
                Text(p.junkWhy, RectF(nx + 20, r.Y + 32, nw - 20, 16), FontUI(12), P.warn, T_LEFT | T_VCENTER | T_ELLIPSIS);
            } else {
                wstring sub = p.publisher;
                if (!p.version.empty()) sub += (sub.empty() ? L"" : L"  ·  ") + p.version;
                Text(sub, RectF(nx, r.Y + 32, nw, 16), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            }
            if (hv || p.junk) {
                Win32App copy = p;
                float bx = r.GetRight() - 12;
                float w1 = ButtonW(L"С хвостами");
                Button(RectF(bx - w1, r.Y + 13, w1, 32), L"С хвостами", [this, copy] { DeepRemove(copy); }, Btn::Danger);
                bx -= w1 + 8;
                if (copy.hasUninstall) {
                    float w2 = ButtonW(L"Удалить");
                    Button(RectF(bx - w2, r.Y + 13, w2, 32), L"Удалить", [this, copy] { Uninstall(copy); }, Btn::Secondary);
                }
            } else {
                Text(p.size ? HumanSize(p.size) : L"", RectF(r.GetRight() - 310, r.Y, 150, h), FontUI(13, 1), P.text, T_RIGHT | T_VCENTER);
                Text(p.date, RectF(r.GetRight() - 150, r.Y, 134, h), FontUI(12.5f), P.muted, T_RIGHT | T_VCENTER);
            }
            y += h + 8;
        }
        return y + 10;
    }
};
Page* MakeApps() { return new AppsPage; }
