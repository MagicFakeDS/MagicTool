#include "app.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <shlwapi.h>

static vector<int> MonitorRates(const DEVMODEW& cur) {
    vector<int> all;
    DEVMODEW dm{}; dm.dmSize = sizeof(dm);
    for (int i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
        if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight &&
            dm.dmBitsPerPel == cur.dmBitsPerPel && dm.dmDisplayFrequency > 1 &&
            std::find(all.begin(), all.end(), (int)dm.dmDisplayFrequency) == all.end())
            all.push_back((int)dm.dmDisplayFrequency);
        dm = {}; dm.dmSize = sizeof(dm);
    }
    std::sort(all.begin(), all.end());

    int curHz = (int)cur.dmDisplayFrequency;
    vector<int> rates;
    for (int r : all) {
        bool dup = std::find(all.begin(), all.end(), r + 1) != all.end();
        if (r == curHz || (r >= 50 && !dup)) rates.push_back(r);
    }
    if (rates.empty()) rates = all;
    return rates;
}
static bool MonitorSetRate(int hz) {
    DEVMODEW dm{}; dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) return false;
    dm.dmDisplayFrequency = hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    if (ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL) return false;
    return ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

static wstring BrowseFolder(const wstring& title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = app.hwnd;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return L"";
    wchar_t path[MAX_PATH] = L"";
    bool ok = SHGetPathFromIDListW(id, path);
    CoTaskMemFree(id);
    return ok ? wstring(path) : L"";
}

static float CardHead(RectF c, const wchar_t* icon, const wstring& title, const wstring& right = L"") {
    RectF ib(c.X + 20, c.Y + 18, 32, 32);
    FillRound(ib, 9, Alpha(P.accent, g_dark ? 55 : 32));
    Glyph(icon, ib, 15, g_dark ? Blend(P.accent, Color(255, 255, 255, 255), 0.5f) : P.accent);
    Text(title, RectF(c.X + 62, c.Y + 18, c.Width - 80, 32), FontUI(15, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
    if (!right.empty()) Text(right, RectF(c.X + 62, c.Y + 18, c.Width - 82, 32), FontUI(12.5f), P.muted, T_RIGHT | T_VCENTER);
    return c.Y + 62;
}

static wstring GpuTempText(size_t i) {
    int t = i < app.live.gpuTemps.size() ? app.live.gpuTemps[i] : -1;
    if (t < 0 && i < app.si.gpus.size()) t = app.si.gpus[i].tempC;
    return t > 0 ? Fmt(L"%d °C", t) : L"";
}

static const VolumeInfo* SysVolume() {
    for (auto& v : app.si.volumes) if (v.system) return &v;
    return nullptr;
}

struct OverviewPage : Page {
    OverviewPage() : Page(PG_OVERVIEW, L"Обзор", L"Состояние компьютера клиента и что стоит сделать в первую очередь", Ico::Overview) {}
    void OnShow() override {
        RequestSysInfo();
        RequestDevices();
    }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Обновить", [] { RequestSysInfo(true); RequestDevices(true); }, Btn::Secondary, Ico::Refresh, !app.siLoading);
        x = ButtonRight(x - 10, y, 36, OverlayOn() ? L"Монитор: вкл" : L"Монитор поверх игр",
                        [] { ToggleOverlay(); }, OverlayOn() ? Btn::Primary : Btn::Secondary, Ico::Monitor);
        if (OverlayOn()) ButtonRight(x - 10, y, 36, L"Угол", [] { CycleOverlayCorner(); }, Btn::Secondary);
    }
    bool Tick() override { return true; }
    wstring NavBadge(Sev& s) override {
        if (!app.si.ready || !app.devsLoaded) return L"";
        auto f = Diagnose(app.si, app.devs, app.crashes, app.live);
        int sc = HealthScore(f);
        s = sc >= 80 ? Sev::Ok : sc >= 55 ? Sev::Warn : Sev::Bad;
        return std::to_wstring(sc);
    }
    float Body(float x, float y, float w) override {
        auto& si = app.si;
        if (!si.ready) return y + Loading(x, y, w, L"Собираю сведения о компьютере… Первый запуск WMI может занять до 15–20 секунд.");
        vector<Finding> fnd = Diagnose(si, app.devs, app.crashes, app.live);
        int score = HealthScore(fnd);

        float sh = 214;
        RectF sc(x, y, 300, sh);
        Card(sc);
        Color scC = score >= 80 ? P.ok : score >= 55 ? P.warn : P.danger;
        Ring(sc.X + 84, sc.Y + 92, 52, 11, score / 100.0f, scC, score >= 80);
        Text(std::to_wstring(score), RectF(sc.X + 32, sc.Y + 68, 104, 40), FontUI(32, 2), P.text, T_CENTER | T_VCENTER);
        Text(L"из 100", RectF(sc.X + 32, sc.Y + 106, 104, 16), FontUI(11.5f), P.muted, T_CENTER | T_VCENTER);
        Text(L"Состояние ПК", RectF(sc.X + 156, sc.Y + 34, 130, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
        wstring verdict = score >= 80 ? L"Хорошее" : score >= 55 ? L"Есть замечания" : L"Нужен ремонт";
        Text(verdict, RectF(sc.X + 156, sc.Y + 54, 136, 50), FontUI(17, 1), scC, T_WRAP);
        int bad = 0, warn = 0;
        for (auto& f : fnd) { if (f.sev == Sev::Bad) ++bad; else if (f.sev == Sev::Warn) ++warn; }
        Text(Fmt(L"Проблем: %d\nЗамечаний: %d", bad, warn), RectF(sc.X + 156, sc.Y + 106, 136, 40), FontUI(12.5f), P.muted, T_WRAP);
        if (!app.devsLoaded) Spinner(sc.GetRight() - 24, sc.Y + 24, 7, P.accent);

        float tx = x + 316, tw = (w - 316 - 16) / 2, th = (sh - 16) / 2;
        StatTile(RectF(tx, y, tw, th), Ico::Cpu, L"Загрузка процессора", Fmt(L"%.0f %%", app.live.cpuPct),
                 Fmt(L"%d ядер / %d потоков", si.cpuCores, si.cpuThreads), (float)app.live.cpuPct / 100, app.live.cpuPct > 85 ? P.warn : P.accent);
        StatTile(RectF(tx + tw + 16, y, tw, th), Ico::Memory, L"Оперативная память", Fmt(L"%.0f %%", app.live.ramPct),
                 HumanSize(app.live.ramUsed) + L" из " + HumanSize(app.live.ramTotal), (float)app.live.ramPct / 100, app.live.ramPct > 85 ? P.warn : P.accent2);
        if (auto v = SysVolume()) {
            float used = v->total ? 1.0f - (float)v->free / v->total : 0;
            StatTile(RectF(tx, y + th + 16, tw, th), Ico::Disk, L"Системный диск " + v->letter, HumanSize(v->free) + L" свободно",
                     L"из " + HumanSize(v->total), used, used > 0.9f ? P.danger : used > 0.8f ? P.warn : P.ok);
        }
        {
            wstring t = si.gpus.empty() ? L"" : GpuTempText(0);
            wstring sub = si.gpus.empty() ? L"" : si.gpus[0].name;
            int tv = _wtoi(t.c_str());
            StatTile(RectF(tx + tw + 16, y + th + 16, tw, th), Ico::Thermo, L"Температура видеокарты", t.empty() ? L"нет датчика" : t, sub,
                     t.empty() ? -1 : tv / 100.0f, tv >= 85 ? P.danger : tv >= 75 ? P.warn : P.ok);
        }
        y += sh + 24;

        y += SectionTitle(x, y, w, L"Что найдено", fnd.empty() ? L"" : L"Отсортировано по важности. Кнопка справа открывает раздел, где это исправляется.");
        if (fnd.empty()) {
            y += Banner(x, y, w, Sev::Ok, L"Серьёзных проблем не найдено", L"Компьютер в порядке. Можно сделать профилактическую очистку и проверить автозагрузку.") + 12;
        }
        for (auto& f : fnd) {
            float th2 = TextH(f.text, FontUI(12.5f), w - 220);
            float rh = std::max(60.0f, 34 + th2 + 10);
            RectF r(x, y, w, rh);
            bool hv = Hover(r);
            FillRound(r, 12, hv ? P.surfaceHover : P.surface);
            GradientH(RectF(r.X, r.Y + 10, 3, rh - 20), 1.5f, SevColor(f.sev), SevColor(f.sev));
            SevIcon(f.sev, r.X + 16, r.Y + (rh - 30) / 2, 30);
            Text(f.title, RectF(r.X + 60, r.Y + 10, w - 220, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(f.text, RectF(r.X + 60, r.Y + 33, w - 220, th2 + 4), FontUI(12.5f), P.muted, T_WRAP);
            if (f.page >= 0) {
                int pg = f.page;
                float bw = ButtonW(L"Исправить", Ico::Chevron);
                Button(RectF(r.GetRight() - bw - 14, r.Y + (rh - 34) / 2, bw, 34), L"Перейти", [pg] { Navigate(pg); }, Btn::Soft, Ico::Chevron);
            }
            y += rh + 8;
        }
        y += 16;

        y += SectionTitle(x, y, w, L"Сводка");
        RectF c(x, y, w, 0);
        vector<std::pair<wstring, wstring>> kv = {
            { L"Компьютер", si.computerName + (si.manufacturer.empty() || IContains(si.manufacturer, L"to be filled") ? L"" : L" · " + si.manufacturer + L" " + si.model) },
            { L"Windows", si.osName + L" " + si.osDisplay + L" (" + si.osBuild + L")" },
            { L"Процессор", si.cpuName },
            { L"Память", HumanSize(si.ramTotal) + (si.ram.empty() ? L"" : L" · " + si.ram[0].type + (si.ram[0].speed ? Fmt(L" %d МГц", si.ram[0].speed) : L"")) },
            { L"Видеокарта", si.gpus.empty() ? L"" : si.gpus[0].name + (si.gpus.size() > 1 ? Fmt(L" (+%d)", (int)si.gpus.size() - 1) : L"") },
            { L"Системный диск", [&] { for (auto& d : si.disks) if (d.isSystem) return d.model + L" · " + d.kind() + L" · " + d.health.statusText; return wstring(); }() },
            { L"Материнская плата", si.boardMaker + L" " + si.boardModel },
            { L"Активация", LicenseStatusText(si.licenseStatus) },
            { L"Без перезагрузки", HumanDuration(si.uptimeSec) },
            { L"Электропитание", si.powerPlan },
        };
        float rows = (float)((kv.size() + 1) / 2);
        c.Height = rows * 30 + 36;
        Card(c);
        float cw = (w - 60) / 2;
        for (size_t i = 0; i < kv.size(); ++i) {
            float cx = c.X + 24 + (i % 2) * (cw + 12);
            float cy = c.Y + 18 + (i / 2) * 30;
            KeyValue(cx, cy, cw, kv[i].first, kv[i].second, 140);
        }
        y += c.Height + 10;
        return y;
    }
};
Page* MakeOverview() { return new OverviewPage; }

struct HardwarePage : Page {
    HardwarePage() : Page(PG_HARDWARE, L"Железо", L"Процессор, память, видеокарта, плата, батарея и совместимость с Windows 11", Ico::Chip) {}
    void OnShow() override { RequestSysInfo(); }
    bool Tick() override { return true; }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Диспетчер устройств", [] { OpenSystemTool(L"devmgmt.msc"); }, Btn::Secondary, Ico::Open);
        ButtonRight(x - 10, y, 36, L"Сведения о системе", [] { OpenSystemTool(L"msinfo32.exe"); }, Btn::Secondary, Ico::Info);
    }
    float Body(float x, float y, float w) override {
        auto& si = app.si;
        if (!si.ready) return y + Loading(x, y, w, L"Собираю сведения о железе…");
        float gap = 16, cw = (w - gap) / 2;
        float lh = 26;

        float y0 = y;
        RectF c1(x, y, cw, 62 + 22 + 5 * lh + 10);
        Card(c1);
        float cy = CardHead(c1, Ico::Cpu, L"Процессор");
        Text(si.cpuName, RectF(c1.X + 22, cy - 6, cw - 44, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        cy += 22;
        KeyValue(c1.X + 22, cy, cw - 44, L"Ядра / потоки", Fmt(L"%d / %d", si.cpuCores, si.cpuThreads)); cy += lh;
        KeyValue(c1.X + 22, cy, cw - 44, L"Базовая частота", si.cpuMaxMHz ? Fmt(L"%.2f ГГц", si.cpuMaxMHz / 1000.0) : L""); cy += lh;
        KeyValue(c1.X + 22, cy, cw - 44, L"Сокет", si.cpuSocket); cy += lh;
        KeyValue(c1.X + 22, cy, cw - 44, L"Загрузка сейчас", Fmt(L"%.0f %%", app.live.cpuPct)); cy += lh;
        KeyValue(c1.X + 22, cy, cw - 44, L"Термодатчик платы", si.thermalC > 0 ? Fmt(L"%d °C (ACPI)", si.thermalC) : L"нет данных");

        RectF c2(x + cw + gap, y, cw, c1.Height);
        Card(c2);
        cy = CardHead(c2, Ico::Chip, L"Материнская плата и BIOS");
        KeyValue(c2.X + 22, cy, cw - 44, L"Производитель ПК", si.manufacturer); cy += lh;
        KeyValue(c2.X + 22, cy, cw - 44, L"Модель", si.model); cy += lh;
        KeyValue(c2.X + 22, cy, cw - 44, L"Плата", si.boardMaker + L" " + si.boardModel); cy += lh;
        KeyValue(c2.X + 22, cy, cw - 44, L"Версия BIOS", si.biosVer + (si.biosDate.empty() ? L"" : L" от " + si.biosDate)); cy += lh;
        KeyValue(c2.X + 22, cy, cw - 44, L"Режим загрузки", si.uefi ? L"UEFI" : L"Legacy (BIOS)"); cy += lh;
        y += c1.Height + gap;

        float memRows = (float)std::max<size_t>(1, si.ram.size());
        RectF c3(x, y, w, 62 + 34 + memRows * 30 + 46);
        Card(c3);
        int used = (int)si.ram.size();
        cy = CardHead(c3, Ico::Memory, L"Оперативная память",
                      HumanSize(si.ramTotal) + (si.ramSlots ? Fmt(L"  ·  занято слотов %d из %d", used, si.ramSlots) : L""));
        float tx = c3.X + 22, tw = w - 44;
        cy += TableHeader(tx, cy, tw, { { L"СЛОТ", 12 }, { L"ОБЪЁМ", tw * 0.34f }, { L"ТИП", tw * 0.46f }, { L"ЧАСТОТА", tw * 0.56f }, { L"ПРОИЗВОДИТЕЛЬ / МОДЕЛЬ", tw * 0.68f } });
        for (auto& m : si.ram) {
            Text(m.slot, RectF(tx + 12, cy, tw * 0.33f, 28), FontUI(12.5f), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(HumanSize(m.size), RectF(tx + tw * 0.34f, cy, 120, 28), FontUI(12.5f, 1), P.text, T_LEFT | T_VCENTER);
            Text(m.type, RectF(tx + tw * 0.46f, cy, 100, 28), FontUI(12.5f), P.text, T_LEFT | T_VCENTER);
            Text(m.speed ? Fmt(L"%d МГц", m.speed) : L"—", RectF(tx + tw * 0.56f, cy, 100, 28), FontUI(12.5f), P.text, T_LEFT | T_VCENTER);
            Text(Trim(m.maker + L" " + m.part), RectF(tx + tw * 0.68f, cy, tw * 0.32f, 28), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            cy += 30;
        }
        wstring memNote;
        if (si.ramSlots > used && used > 0) memNote = Fmt(L"Есть свободные слоты (%d) — память можно добавить. ", si.ramSlots - used);
        if (used == 1 && si.ramSlots >= 2) memNote += L"Одна планка работает в одноканальном режиме — вторая такая же даст прирост скорости.";
        if (memNote.empty()) memNote = L"Для апгрейда подбирайте планки того же типа и частоты.";
        Text(memNote, RectF(tx, cy + 6, tw, 30), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
        y += c3.Height + gap;

        for (size_t i = 0; i < si.gpus.size(); ++i) {
            auto& g = si.gpus[i];
            RectF c(x, y, w, 62 + 2 * lh + 14);
            Card(c);
            wstring t = GpuTempText(i);
            cy = CardHead(c, Ico::Monitor, g.name, g.integrated ? L"встроенная" : L"дискретная");
            float hw = (w - 44) / 2;
            KeyValue(c.X + 22, cy, hw, L"Видеопамять", g.vram ? HumanSize(g.vram) : L"общая с ОЗУ");
            KeyValue(c.X + 22 + hw, cy, hw, L"Температура", t.empty() ? L"датчик недоступен" : t,
                     150, _wtoi(t.c_str()) >= 85 ? P.danger : Color(0, 0, 0, 0));
            cy += lh;
            KeyValue(c.X + 22, cy, hw, L"Драйвер", g.driverVer);
            wstring age = g.driverDate + (g.driverAgeDays > 540 ? L"  (старый — стоит обновить)" : L"");
            KeyValue(c.X + 22 + hw, cy, hw, L"Дата драйвера", age, 150, g.driverAgeDays > 540 ? P.warn : Color(0, 0, 0, 0));
            y += c.Height + gap;
        }

        {
            DEVMODEW cur{}; cur.dmSize = sizeof(cur);
            bool okMode = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &cur) != FALSE;
            vector<int> rates = okMode ? MonitorRates(cur) : vector<int>{};
            bool manyRates = rates.size() > 1;
            RectF c(x, y, w, 62.0f + 26 + (manyRates ? 50 : 24));
            Card(c);
            float ccy = CardHead(c, Ico::Monitor, L"Частота обновления экрана",
                                 okMode ? Fmt(L"%d × %d · основной монитор", (int)cur.dmPelsWidth, (int)cur.dmPelsHeight) : L"");
            if (!okMode) {
                Text(L"Не удалось прочитать текущий режим экрана.", RectF(c.X + 22, ccy, w - 44, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
            } else if (!manyRates) {
                Text(Fmt(L"Сейчас %d Гц. Для текущего разрешения другие частоты недоступны.", (int)cur.dmDisplayFrequency),
                     RectF(c.X + 22, ccy, w - 44, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
            } else {
                int curHz = (int)cur.dmDisplayFrequency;
                Text(Fmt(L"Сейчас %d Гц — выберите частоту:", curHz), RectF(c.X + 22, ccy, w - 44, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
                ccy += 28;
                float bx = c.X + 22;
                for (int hz : rates) {
                    wstring lbl = Fmt(L"%d Гц", hz);
                    float bw = ButtonW(lbl);
                    Button(RectF(bx, ccy, bw, 38), lbl, [hz, curHz] {
                        if (hz == curHz) return;
                        Confirm(Fmt(L"Поставить %d Гц?", hz),
                                L"Экран кратко моргнёт. Если картинка не вернётся — выберите прежнюю частоту снова (кнопка останется доступна).",
                                L"Применить", [hz] {
                            if (MonitorSetRate(hz)) Toast(Fmt(L"Частота обновления: %d Гц", hz));
                            else Message(L"Не получилось", L"Монитор или видеодрайвер не принял эту частоту.", Sev::Warn);
                        }, false, Sev::Info);
                    }, hz == curHz ? Btn::Primary : Btn::Soft);
                    bx += bw + 10;
                }
            }
            y += c.Height + gap;
        }

        y0 = y;
        RectF c5(x, y, cw, 62 + 6 * lh + 14);
        Card(c5);
        cy = CardHead(c5, Ico::Info, L"Windows");
        KeyValue(c5.X + 22, cy, cw - 44, L"Система", si.osName); cy += lh;
        KeyValue(c5.X + 22, cy, cw - 44, L"Версия", si.osDisplay + L" · сборка " + si.osBuild); cy += lh;
        KeyValue(c5.X + 22, cy, cw - 44, L"Разрядность", si.osArch); cy += lh;
        KeyValue(c5.X + 22, cy, cw - 44, L"Установлена", si.installDate); cy += lh;
        KeyValue(c5.X + 22, cy, cw - 44, L"Пользователь", si.userName); cy += lh;
        KeyValue(c5.X + 22, cy, cw - 44, L"Работает", HumanDuration(si.uptimeSec) + L" без перезагрузки");

        RectF c6(x + cw + gap, y, cw, c5.Height);
        Card(c6);
        cy = CardHead(c6, Ico::Check, L"Готовность к Windows 11");
        auto req = [&](const wstring& k, bool ok, const wstring& v) {
            SevIcon(ok ? Sev::Ok : Sev::Bad, c6.X + 22, cy + 1, 18);
            Text(k, RectF(c6.X + 50, cy, 160, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
            Text(v, RectF(c6.X + 210, cy, cw - 232, 20), FontUI(13, 1), ok ? P.text : P.danger, T_LEFT | T_VCENTER | T_ELLIPSIS);
            cy += lh;
        };
        uint64_t sysDisk = 0;
        for (auto& d : si.disks) if (d.isSystem) sysDisk = d.size;
        req(L"Режим UEFI", si.uefi, si.uefi ? L"да" : L"Legacy — нужен UEFI");
        req(L"Безопасная загрузка", si.secureBoot, si.secureBoot ? L"включена" : L"выключена (можно включить в BIOS)");
        req(L"TPM", si.tpm == L"2.0", si.tpm == L"2.0" ? L"2.0" : si.tpm + L" — нужен TPM 2.0 (fTPM/PTT в BIOS)");
        req(L"Оперативная память", si.ramTotal >= 4ull << 30, HumanSize(si.ramTotal));
        req(L"Системный диск", sysDisk >= 64ull * 1000 * 1000 * 1000, HumanSize(sysDisk));
        Text(L"Процессор проверяйте по списку Microsoft (Intel 8-го поколения и новее, AMD Ryzen 2000+).",
             RectF(c6.X + 22, cy, cw - 44, 22), FontUI(11.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
        y += c5.Height + gap;

        if (si.battery.present) {
            RectF c7(x, y, w, 62 + 2 * lh + 14);
            Card(c7);
            cy = CardHead(c7, Ico::Battery, L"Батарея ноутбука");
            float hw = (w - 44) / 2;
            KeyValue(c7.X + 22, cy, hw, L"Заряд", si.battery.chargePct >= 0 ? Fmt(L"%d %%", si.battery.chargePct) + (si.battery.charging ? L" (заряжается)" : L"") : L"");
            KeyValue(c7.X + 22 + hw, cy, hw, L"Износ", si.battery.wearPct >= 0 ? Fmt(L"%d %%", si.battery.wearPct) : L"нет данных", 150,
                     si.battery.wearPct >= 40 ? P.danger : Color(0, 0, 0, 0));
            cy += lh;
            KeyValue(c7.X + 22, cy, hw, L"Паспортная ёмкость", si.battery.designMWh ? Fmt(L"%llu мВт·ч", si.battery.designMWh) : L"");
            KeyValue(c7.X + 22 + hw, cy, hw, L"Реальная ёмкость", si.battery.fullMWh ? Fmt(L"%llu мВт·ч", si.battery.fullMWh) : L"");
            y += c7.Height + gap;
        }
        return y;
    }
};
Page* MakeHardware() { return new HardwarePage; }

struct DisksPage : Page {
    std::map<int, bool> expanded;
    DisksPage() : Page(PG_DISKS, L"Диски", L"Здоровье накопителей по SMART, температура, износ и свободное место", Ico::Disk) {}
    void OnShow() override { RequestSysInfo(); }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Обновить", [] { RequestSysInfo(true); }, Btn::Secondary, Ico::Refresh, !app.siLoading);
        ButtonRight(x - 10, y, 36, L"Управление дисками", [] { OpenSystemTool(L"diskmgmt.msc"); }, Btn::Secondary, Ico::Open);
    }
    wstring NavBadge(Sev& s) override {
        int bad = 0;
        Sev worst = Sev::Ok;
        for (auto& d : app.si.disks) if (d.health.status == Sev::Warn || d.health.status == Sev::Bad) { ++bad; worst = std::max(worst, d.health.status); }
        s = worst;
        return bad ? std::to_wstring(bad) : L"";
    }

    static int RunDiskpart(const wstring& script, TaskCtx& c) {
        wchar_t tmp[MAX_PATH] = L"";
        GetTempPathW(MAX_PATH, tmp);
        wstring file = wstring(tmp) + L"tbx_diskpart.txt";
        std::string a;
        a.reserve(script.size());
        for (wchar_t ch : script) a += (char)ch;
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { c.Line(L"Не удалось создать скрипт diskpart"); return -1; }
        DWORD wr = 0;
        WriteFile(h, a.data(), (DWORD)a.size(), &wr, nullptr);
        CloseHandle(h);
        int code = c.Run(L"\"" + Sys32(L"diskpart.exe") + L"\" /s \"" + file + L"\"");
        DeleteFileW(file.c_str());
        return code;
    }

    static void FormatVol(const VolumeInfo& v) {
        if (GuardBusy()) return;
        if (v.system) { Message(L"Нельзя", L"Системный раздел (где стоит Windows) форматировать нельзя.", Sev::Warn); return; }
        Confirm(L"Форматировать " + v.letter + L"?",
                L"ВСЕ данные на " + v.letter + (v.label.empty() ? L"" : L" («" + v.label + L"»)") +
                L" будут стёрты безвозвратно!\nСейчас занято " + HumanSize(v.total - v.free) + L" из " + HumanSize(v.total) +
                L".\n\nБыстрое форматирование в NTFS. Нужны права администратора. Проверьте букву — отменить нельзя.",
                L"Стереть и форматировать", [v] {
            if (GuardBusy()) return;
            wstring letter = v.letter.substr(0, 1);
            wstring label = v.label;
            StartTask(L"Форматирование " + v.letter, PG_DISKS, [letter, label](TaskCtx& c) {
                c.Line(L"Форматирую том " + letter + L": …");
                int code = RunDiskpart(L"select volume " + letter + L"\r\nformat fs=ntfs quick\r\nexit\r\n", c);
                if (code != 0) { c.Fail(L"diskpart вернул ошибку — нужны права администратора"); return; }
                if (!label.empty()) SetVolumeLabelW((letter + L":\\").c_str(), label.c_str());
                c.Done(L"готово — том отформатирован");
            }, [](Task&) { RequestSysInfo(true); });
        }, true);
    }
    static void AssignLetter(const wstring& cur, const wstring& nl) {
        if (GuardBusy()) return;
        StartTask(L"Смена буквы " + cur + L": → " + nl + L":", PG_DISKS, [cur, nl](TaskCtx& c) {
            int code = RunDiskpart(L"select volume " + cur + L"\r\nassign letter=" + nl + L"\r\nexit\r\n", c);
            if (code != 0) c.Fail(L"diskpart вернул ошибку — нужны права администратора");
            else c.Done(L"буква изменена на " + nl + L":");
        }, [](Task&) { RequestSysInfo(true); });
    }

    static void ChangeLetter(const VolumeInfo& v) {
        if (v.system) { Message(L"Нельзя", L"Букву системного раздела (C:) сменить нельзя — на нём работает Windows.", Sev::Warn); return; }
        if (v.letter.empty()) return;
        DWORD mask = GetLogicalDrives();
        wstring cur = v.letter.substr(0, 1);
        vector<ModalBtn> btns;
        int cnt = 0;
        for (wchar_t ch = L'D'; ch <= L'Z' && cnt < 6; ++ch) {
            if (mask & (1u << (ch - L'A'))) continue;
            wstring nl(1, ch);
            btns.push_back({ nl + L":", Btn::Secondary, [cur, nl] { AssignLetter(cur, nl); } });
            ++cnt;
        }
        if (btns.empty()) { Message(L"Нет свободных букв", L"Все буквы дисков заняты.", Sev::Info); return; }
        btns.push_back({ L"Отмена", Btn::Ghost, nullptr });
        ModalCustom(L"Новая буква для " + v.letter, L"Выберите свободную букву диска:", btns, Sev::Info);
    }
    float Metric(float x, float y, float w, const wstring& label, const wstring& value, Color c = Color(0, 0, 0, 0)) {
        FillRound(RectF(x, y, w, 60), 10, P.surface);
        Text(label, RectF(x + 14, y + 9, w - 20, 16), FontUI(11.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
        Text(value.empty() ? L"—" : value, RectF(x + 14, y + 27, w - 20, 24), FontUI(16, 1), c.GetA() ? c : P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        return w;
    }
    float Body(float x, float y, float w) override {
        auto& si = app.si;
        if (!si.ready) return y + Loading(x, y, w, L"Читаю SMART дисков…");
        if (!app.elevated)
            y += Banner(x, y, w, Sev::Warn, L"Программа запущена без прав администратора", L"Часть дисков может не отдать SMART. Перезапустите MagicTool от имени администратора.") + 16;
        for (auto& d : si.disks) {
            auto& h = d.health;
            float notesH = 0;
            for (auto& n : h.notes) notesH += TextH(n, FontUI(12.5f), w - 80) + 8;
            bool exp = expanded[d.number];
            float attrH = exp && !h.attrs.empty() ? 40 + h.attrs.size() * 26.0f : 0;
            float volH = d.letters.empty() ? 0 : d.letters.size() * 34.0f + 6;
            RectF c(x, y, w, 76 + 76 + notesH + volH + 54 + attrH);
            Card(c);

            RectF ib(c.X + 20, c.Y + 20, 40, 40);
            GradientH(ib, 10, Alpha(P.accent, g_dark ? 70 : 40), Alpha(P.accent2, g_dark ? 70 : 40));
            Glyph(Ico::Disk, ib, 18, g_dark ? Color(255, 255, 255, 255) : P.accent2);
            Text(d.model.empty() ? L"Диск " + std::to_wstring(d.number) : d.model, RectF(c.X + 74, c.Y + 18, w - 300, 24), FontUI(16, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            wstring sub = d.kind() + L"  ·  " + HumanSize(d.size) + (d.isSystem ? L"  ·  системный" : L"") +
                          (d.serial.empty() ? L"" : L"  ·  S/N " + d.serial) + (d.firmware.empty() ? L"" : L"  ·  прошивка " + d.firmware);
            Text(sub, RectF(c.X + 74, c.Y + 42, w - 300, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Badge(c.GetRight() - 20, c.Y + 26, h.statusText + (h.source.empty() ? L"" : L" · " + h.source), h.status, 28, true);

            float my = c.Y + 76, mw = (w - 40 - 4 * 12) / 5, mx = c.X + 20;
            Color hc = h.healthPct < 0 ? Color(0, 0, 0, 0) : h.healthPct < 30 ? P.danger : h.healthPct < 70 ? P.warn : P.ok;
            Metric(mx, my, mw, d.mediaType == 3 ? L"Состояние" : L"Остаток ресурса", h.healthPct >= 0 ? Fmt(L"%d %%", h.healthPct) : h.statusText, hc); mx += mw + 12;
            Metric(mx, my, mw, L"Температура", h.tempC > 0 ? Fmt(L"%d °C", h.tempC) : L"", h.tempC >= 60 ? P.danger : h.tempC >= 50 ? P.warn : Color(0, 0, 0, 0)); mx += mw + 12;
            Metric(mx, my, mw, L"Наработка", h.powerOnHours >= 0 ? HumanHours((uint64_t)h.powerOnHours) : L""); mx += mw + 12;
            Metric(mx, my, mw, L"Включений", h.powerCycles >= 0 ? std::to_wstring(h.powerCycles) : L""); mx += mw + 12;
            if (h.writtenTB >= 0) Metric(mx, my, mw, L"Записано", Fmt(L"%.1f ТБ", h.writtenTB));
            else if (h.reallocated >= 0) Metric(mx, my, mw, L"Плохие сектора", std::to_wstring(h.reallocated + std::max<int64_t>(0, h.pending)), h.reallocated + std::max<int64_t>(0, h.pending) > 0 ? P.warn : P.ok);
            else Metric(mx, my, mw, L"Ошибки носителя", h.mediaErrors >= 0 ? std::to_wstring(h.mediaErrors) : L"");
            float ny = my + 76;
            for (auto& n : h.notes) {
                float nh = TextH(n, FontUI(12.5f), w - 80);
                Glyph(h.status == Sev::Ok ? Ico::Info : Ico::Warning, RectF(c.X + 20, ny, 20, 20), 13, h.status == Sev::Ok ? P.info : SevColor(h.status));
                Text(n, RectF(c.X + 48, ny + 1, w - 80, nh + 2), FontUI(12.5f), P.text, T_WRAP);
                ny += nh + 8;
            }

            for (auto& L : d.letters)
                for (auto& v : si.volumes)
                    if (v.letter == L) {
                        float used = v.total ? 1.0f - (float)v.free / v.total : 0;
                        Text(v.letter + (v.label.empty() ? L"" : L"  " + v.label), RectF(c.X + 20, ny, 200, 26), FontUI(13, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
                        Progress(RectF(c.X + 230, ny + 10, w - 520, 8), used, used > 0.9f ? P.danger : used > 0.8f ? P.warn : P.accent, used <= 0.8f);
                        Text(HumanSize(v.free) + L" свободно из " + HumanSize(v.total) + L" · " + v.fs, RectF(c.GetRight() - 270, ny, 250, 26), FontUI(12.5f), P.muted, T_RIGHT | T_VCENTER);
                        ny += 34;
                    }
            ny += 6;
            int num = d.number;
            if (!h.attrs.empty()) {
                Button(RectF(c.X + 20, ny, 230, 34), exp ? L"Скрыть атрибуты SMART" : L"Все атрибуты SMART", [this, num] { expanded[num] = !expanded[num]; },
                       Btn::Soft, exp ? Ico::ChevronUp : Ico::ChevronDown);
            } else Text(h.available ? L"" : L"SMART недоступен для этого накопителя.", RectF(c.X + 20, ny, w - 40, 34), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
            ny += 46;
            if (exp && !h.attrs.empty()) {
                float tw = w - 40;
                bool nv = h.source == L"NVMe";
                ny += TableHeader(c.X + 20, ny, tw, nv ? vector<std::pair<wstring, float>>{ { L"ПАРАМЕТР", 14 }, { L"ЗНАЧЕНИЕ", tw * 0.6f } }
                                                       : vector<std::pair<wstring, float>>{ { L"ID", 14 }, { L"АТРИБУТ", 60 }, { L"ТЕКУЩЕЕ", tw * 0.5f }, { L"ХУДШЕЕ", tw * 0.6f }, { L"ПОРОГ", tw * 0.7f }, { L"RAW", tw * 0.8f } });
                for (auto& a : h.attrs) {
                    bool alarm = (!nv && a.thresh > 0 && a.value <= a.thresh) || (a.critical && a.raw > 0 && !(nv && (a.id == 3 || a.id == 5)) && !(a.id == 5 && nv));
                    if (nv && a.id == 5) alarm = a.raw >= 90;
                    if (nv && a.id == 3) alarm = false;
                    Color tc = alarm ? P.danger : P.text;
                    if (nv) {
                        Text(a.name, RectF(c.X + 34, ny, tw * 0.6f, 26), FontUI(12.5f), tc, T_LEFT | T_VCENTER);
                        Text(std::to_wstring(a.raw), RectF(c.X + 20 + tw * 0.6f, ny, 200, 26), FontUI(12.5f, 1), tc, T_LEFT | T_VCENTER);
                    } else {
                        Text(Fmt(L"%02X", a.id), RectF(c.X + 34, ny, 40, 26), FontMono(12.5f), P.muted, T_LEFT | T_VCENTER);
                        Text(a.name, RectF(c.X + 80, ny, tw * 0.5f - 64, 26), FontUI(12.5f), tc, T_LEFT | T_VCENTER | T_ELLIPSIS);
                        Text(std::to_wstring(a.value), RectF(c.X + 20 + tw * 0.5f, ny, 80, 26), FontUI(12.5f), tc, T_LEFT | T_VCENTER);
                        Text(std::to_wstring(a.worst), RectF(c.X + 20 + tw * 0.6f, ny, 80, 26), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
                        Text(std::to_wstring(a.thresh), RectF(c.X + 20 + tw * 0.7f, ny, 80, 26), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
                        Text(std::to_wstring(a.raw), RectF(c.X + 20 + tw * 0.8f, ny, tw * 0.2f, 26), FontUI(12.5f, 1), tc, T_LEFT | T_VCENTER);
                    }
                    ny += 26;
                }
            }
            c.Height = ny - c.Y + 8;
            y += c.Height + 16;
        }

        y += 8;
        y += SectionTitle(x, y, w, L"Все разделы", L"Буквы дисков, файловая система и заполненность");
        y += TableHeader(x, y, w, { { L"ДИСК", 14 }, { L"МЕТКА", 80 }, { L"ФС", w * 0.36f }, { L"ЗАПОЛНЕНО", w * 0.45f }, { L"СВОБОДНО", w * 0.8f } });
        for (auto& v : si.volumes) {
            float used = v.total ? 1.0f - (float)v.free / v.total : 0;
            RectF rr(x, y - 2, w, 32);
            bool hv = Hover(rr) && !v.letter.empty();
            if (hv) FillRound(rr, 8, P.surfaceHover);
            Text(v.letter + (v.system ? L"  (система)" : L""), RectF(x + 14, y, w * 0.2f, 30), FontUI(13, 1), v.system ? P.muted : P.text, T_LEFT | T_VCENTER);
            Text(v.label.empty() ? L"—" : v.label, RectF(x + 80, y, w * 0.36f - 90, 30), FontUI(12.5f), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(v.fs, RectF(x + w * 0.36f, y, 80, 30), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER);
            Progress(RectF(x + w * 0.45f, y + 12, w * 0.28f, 7), used, used > 0.9f ? P.danger : used > 0.8f ? P.warn : P.accent, used <= 0.8f);
            if (hv) {
                VolumeInfo vc = v;
                float bx = x + w - 4;
                float bw1 = ButtonW(L"Сменить букву");
                Button(RectF(bx - bw1, y, bw1, 28), L"Сменить букву", [vc] { ChangeLetter(vc); }, Btn::Secondary);
                bx -= bw1 + 6;
                if (!v.system) { float bw2 = ButtonW(L"Форматировать"); Button(RectF(bx - bw2, y, bw2, 28), L"Форматировать", [vc] { FormatVol(vc); }, Btn::Danger); }
            } else {
                Text(HumanSize(v.free) + L" из " + HumanSize(v.total), RectF(x + w * 0.76f, y, w * 0.24f, 30), FontUI(12.5f), P.text, T_LEFT | T_VCENTER);
            }
            y += 32;
        }
        y += 8;
        Text(L"Наведите на раздел, чтобы сменить букву или отформатировать. Создание и удаление разделов — кнопка «Управление дисками» вверху.",
             RectF(x, y, w, 18), FontUI(11.5f), P.muted, T_LEFT | T_VCENTER);
        return y + 24;
    }
};
Page* MakeDisks() { return new DisksPage; }

static const wchar_t* kWuDriversScript = LR"PS(
$ErrorActionPreference = 'Stop'
$mu = '7971f918-a847-4430-9279-4a52d1efe18d'
try { (New-Object -ComObject Microsoft.Update.ServiceManager).AddService2($mu, 7, '') | Out-Null } catch { }
$s = New-Object -ComObject Microsoft.Update.Session
$s.ClientApplicationID = 'ToolboxPC'
$q = $s.CreateUpdateSearcher()
$q.ServiceID = $mu
$q.SearchScope = 1
$q.ServerSelection = 3
'Ищу драйверы для этого компьютера в Центре обновления Windows (Microsoft Update)...'
try { $list = @($q.Search("IsInstalled=0 and Type='Driver' and IsHidden=0").Updates) }
catch { 'Ошибка поиска: ' + $_.Exception.Message; exit 2 }
'Найдено драйверов: ' + $list.Count
$i = 0; $ok = 0
foreach ($u in $list) {
    $i++
    '[' + $i + '/' + $list.Count + '] ' + $u.Title
    try {
        try { if (-not $u.EulaAccepted) { $u.AcceptEula() } } catch { }
        $c = New-Object -ComObject Microsoft.Update.UpdateColl
        [void]$c.Add($u)
        $d = $s.CreateUpdateDownloader(); $d.Updates = $c; [void]$d.Download()
        $ins = $s.CreateUpdateInstaller(); $ins.Updates = $c
        $r = $ins.Install().ResultCode
        if ($r -eq 2 -or $r -eq 3) { $ok++; '    установлен' } else { '    не установлен (код ' + $r + ')' }
    } catch { '    ошибка: ' + $_.Exception.Message }
}
'RESULT ' + $ok + ' ' + $list.Count
)PS";

struct DevicesPage : Page {
    DevicesPage() : Page(PG_DEVICES, L"Драйверы и сбои", L"Устройства без драйверов, синие экраны, внезапные выключения и падения программ", Ico::Warning) {}
    void OnShow() override { RequestDevices(); }
    wstring NavBadge(Sev& s) override {
        if (!app.devsLoaded) return L"";
        int n = (int)app.devs.size() + app.crashes.bsod30;
        s = app.crashes.bsod30 ? Sev::Bad : Sev::Warn;
        return n ? std::to_wstring(n) : L"";
    }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Обновить", [] { RequestDevices(true); }, Btn::Secondary, Ico::Refresh, !app.devsLoading);
        ButtonRight(x - 10, y, 36, L"Диспетчер устройств", [] { OpenSystemTool(L"devmgmt.msc"); }, Btn::Secondary, Ico::Open);
    }

    static void BackupDrivers() {
        if (GuardBusy()) return;
        wstring base = KnownFolder(FOLDERID_Desktop);
        if (base.empty()) base = SystemDrive() + L"\\";
        wstring folder = base + L"\\Драйверы-" + NowStr(L"%04d%02d%02d-%02d%02d%02d");
        Confirm(L"Сохранить драйверы этого ПК?",
                L"Все сторонние драйверы будут выгружены в папку:\n" + folder +
                L"\n\nНужны права администратора. Систему это не меняет — только копирует драйверы для будущей переустановки.",
                L"Сохранить", [folder] {
            if (GuardBusy()) return;
            StartTask(L"Резервная копия драйверов", PG_DEVICES, [folder](TaskCtx& c) {
                CreateDirectoryW(folder.c_str(), nullptr);
                c.Line(L"Выгружаю драйверы в " + folder + L" …");
                int code = c.Run(L"\"" + Sys32(L"pnputil.exe") + L"\" /export-driver * \"" + folder + L"\"");
                if (code != 0) { c.Fail(L"не удалось — нужны права администратора (кнопка «Запустить от администратора»)"); return; }
                c.Done(L"готово — папка открыта");
            }, [folder](Task& t) {
                if (t.ok) { ShellOpen(folder); Journal::Add(L"drivers", L"Резервная копия драйверов", folder); }
            });
        }, false, Sev::Info);
    }

    static void RestoreDrivers() {
        if (GuardBusy()) return;
        wstring folder = BrowseFolder(L"Выберите папку с драйверами (.inf)");
        if (folder.empty()) return;
        Confirm(L"Установить драйверы из папки?",
                L"Папка:\n" + folder + L"\n\nБудут установлены все найденные .inf-драйверы (с подпапками) через pnputil. "
                L"Нужны права администратора. Перезагрузка не выполняется автоматически.",
                L"Установить", [folder] {
            if (GuardBusy()) return;
            StartTask(L"Установка драйверов из папки", PG_DEVICES, [folder](TaskCtx& c) {
                c.Line(L"Устанавливаю драйверы из " + folder + L" …");
                int code = c.Run(L"\"" + Sys32(L"pnputil.exe") + L"\" /add-driver \"" + folder + L"\\*.inf\" /subdirs /install");
                if (code != 0 && code != 259 && code != 3010) c.Fail(Fmt(L"код %d — проверьте права администратора и папку", code));
                else c.Done(code == 3010 ? L"готово — часть драйверов применится после перезагрузки" : L"готово");
            }, [folder](Task& t) { if (t.ok) Journal::Add(L"drivers", L"Установка драйверов из папки", folder); RequestDevices(true); });
        }, false, Sev::Info);
    }
    static void InstallDrivers() {
        if (GuardBusy()) return;
        Confirm(L"Установить драйверы из Центра обновления?",
                L"Программа найдёт в Microsoft Update драйверы для устройств этого ПК (сеть, звук, чипсет, видео и т. д.) и установит их. "
                L"Нужен интернет. Это официальные драйверы производителей, проверенные Microsoft. Может занять 5–30 минут.",
                L"Найти и установить", [] {
            StartTask(L"Драйверы из Центра обновления", PG_DEVICES, [](TaskCtx& c) {
                if (c.PS(wstring(kWuDriversScript)) != 0) c.Fail(L"поиск не удался — проверьте интернет и службу «Центр обновления Windows»");
                c.Line(L"Готово. Список устройств обновится автоматически.");
            }, [](Task& t) {
                for (auto& l : t.lines)
                    if (IStarts(l, L"RESULT ")) {
                        auto p = Split(l, L' ');
                        if (p.size() >= 3) t.summary = L"установлено " + p[1] + L" из " + p[2];
                    }
                Journal::Add(L"drivers", L"Установка драйверов из Центра обновления", t.summary);
                RequestDevices(true);
            });
        }, false, Sev::Info);
    }
    float Body(float x, float y, float w) override {
        if (!app.devsLoaded) return y + Loading(x, y, w, L"Проверяю устройства и журнал сбоев…");

        y += SectionTitle(x, y, w, L"Устройства с проблемами", L"Что не так с устройствами по мнению Windows. Код 28 — драйвер не установлен.");
        if (app.devs.empty()) y += Banner(x, y, w, Sev::Ok, L"Все устройства работают", L"Диспетчер устройств не сообщает об ошибках.") + 12;
        else {
            y += Banner(x, y, w, Sev::Info, L"Недостающие драйверы можно поставить автоматически",
                        L"Поиск в Microsoft Update по оборудованию этого ПК — как в установщике MagicTool. Нужен интернет.",
                        L"Установить драйверы", [] { InstallDrivers(); }) + 12;
            for (auto& d : app.devs) {
                RectF r(x, y, w, 70);
                FillRound(r, 12, Hover(r) ? P.surfaceHover : P.surface);
                SevIcon(d.code == 22 ? Sev::Info : Sev::Warn, r.X + 16, r.Y + 20, 30);
                Text(d.name + (d.cls.empty() ? L"" : L"  ·  " + d.cls), RectF(r.X + 60, r.Y + 10, w - 330, 22), FontUI(13.5f, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
                Text(d.problem + Fmt(L" (код %d)", d.code), RectF(r.X + 60, r.Y + 31, w - 330, 18), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
                Text(d.id, RectF(r.X + 60, r.Y + 49, w - 330, 16), FontMono(11), Alpha(P.muted, 200), T_LEFT | T_VCENTER | T_ELLIPSIS);
                wstring id = d.id;
                float bx = r.GetRight() - 14;
                bx = ButtonRight(bx, r.Y + 18, 34, L"Найти в интернете", [id] {
                    wstring q = id;
                    size_t p = q.find(L'\\', q.find(L'\\') + 1);
                    if (p != wstring::npos) q = q.substr(0, p);
                    wchar_t enc[1024]; DWORD n = 1024;
                    UrlEscapeW(q.c_str(), enc, &n, URL_ESCAPE_SEGMENT_ONLY | URL_ESCAPE_PERCENT);
                    ShellOpen(L"https://www.google.com/search?q=" + wstring(enc) + L"+driver");
                }, Btn::Secondary, Ico::Search);
                ButtonRight(bx - 8, r.Y + 18, 34, L"", [id] {
                    if (OpenClipboard(app.hwnd)) {
                        EmptyClipboard();
                        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (id.size() + 1) * 2);
                        memcpy(GlobalLock(g), id.c_str(), (id.size() + 1) * 2);
                        GlobalUnlock(g);
                        SetClipboardData(CF_UNICODETEXT, g);
                        CloseClipboard();
                        Toast(L"ID устройства скопирован");
                    }
                }, Btn::Secondary, Ico::Copy);
                y += 78;
            }
        }
        float ch = ConsoleBox(x, y + 4, w, 240, PG_DEVICES);
        if (ch) y += ch + 16;
        y += 16;

        y += SectionTitle(x, y, w, L"Резервная копия драйверов",
                          L"Сохраните драйверы перед переустановкой Windows и верните их на свежую систему. Нужны права администратора.");
        float dcw = (w - 16) / 2, dch = 150;
        bool dbusy = TaskBusy();
        ActionCard(RectF(x, y, dcw, dch), Ico::Download, L"Сохранить драйверы этого ПК",
                   L"Выгружает все сторонние драйверы в папку на Рабочем столе (pnputil /export-driver). Пригодится после переустановки Windows.",
                   L"Сохранить", [] { BackupDrivers(); }, Btn::Primary, !dbusy);
        ActionCard(RectF(x + dcw + 16, y, dcw, dch), Ico::Apps, L"Установить драйверы из папки",
                   L"Укажите папку с сохранёнными .inf-драйверами — программа установит их все (pnputil /add-driver /install).",
                   L"Выбрать папку", [] { RestoreDrivers(); }, Btn::Secondary, !dbusy);
        y += dch + 20;

        auto& cr = app.crashes;
        y += SectionTitle(x, y, w, L"Журнал сбоев", L"Синие экраны и внезапные выключения за 30 дней, падения программ за неделю");
        float tw = (w - 32) / 3;
        StatTile(RectF(x, y, tw, 104), Ico::Error, L"Синие экраны (BSOD)", std::to_wstring(cr.bsod30), L"за 30 дней", -1, cr.bsod30 ? P.danger : P.ok);
        StatTile(RectF(x + tw + 16, y, tw, 104), Ico::Power, L"Внезапные выключения", std::to_wstring(cr.unexpected30), L"за 30 дней", -1, cr.unexpected30 >= 3 ? P.warn : P.ok);
        StatTile(RectF(x + 2 * (tw + 16), y, tw, 104), Ico::Apps, L"Падения программ", std::to_wstring(cr.appCrash7), L"за 7 дней", -1, cr.appCrash7 > 10 ? P.warn : P.ok);
        y += 120;
        if (!cr.dumps.empty()) {
            y += Banner(x, y, w, Sev::Info, Fmt(L"Найдено дампов памяти: %d", (int)cr.dumps.size()),
                        L"Файлы C:\\Windows\\Minidump помогут точно найти виновный драйвер (например, в BlueScreenView или WinDbg).",
                        L"Открыть папку", [] { ShellOpen(WinDir() + L"\\Minidump"); }) + 12;
        }
        if (cr.events.empty()) y += Banner(x, y, w, Sev::Ok, L"Сбоев не найдено", L"За последний месяц Windows не фиксировала синих экранов и аварийных выключений.") + 12;
        for (auto& e : cr.events) {
            float dh = TextH(e.detail, FontUI(12.5f), w - 260);
            float rh = std::max(52.0f, dh + 32);
            RectF r(x, y, w, rh);
            FillRound(r, 10, P.surface);
            Sev s = e.kind.find(L"BSOD") != wstring::npos ? Sev::Bad : e.kind.find(L"программ") != wstring::npos ? Sev::Info : Sev::Warn;
            SevIcon(s, r.X + 14, r.Y + (rh - 26) / 2, 26);
            Text(e.kind, RectF(r.X + 52, r.Y + 8, w - 260, 20), FontUI(13, 1), P.text, T_LEFT | T_VCENTER);
            Text(e.detail, RectF(r.X + 52, r.Y + 28, w - 260, dh + 4), FontUI(12.5f), P.muted, T_WRAP);
            Text(e.time, RectF(r.GetRight() - 190, r.Y, 176, rh), FontUI(12.5f), P.muted, T_RIGHT | T_VCENTER);
            y += rh + 6;
        }
        return y + 10;
    }
};
Page* MakeDevices() { return new DevicesPage; }

static const wchar_t* kProtected[] = { L"system", L"idle", L"registry", L"memory compression", L"smss.exe", L"csrss.exe", L"wininit.exe",
                                        L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe", L"fontdrvhost.exe", L"dwm.exe",
                                        L"msmpeng.exe", L"lsaiso.exe", L"secure system", L"sgrmbroker.exe", L"system idle process" };

struct ProcGroup {
    wstring name, desc, path;
    int count = 0;
    double cpu = 0;
    uint64_t mem = 0;
    vector<DWORD> pids;
    bool prot = false;
};

struct ProcessesPage : Page {
    vector<ProcGroup> groups;
    std::map<DWORD, uint64_t> lastTimes;
    ULONGLONG lastTick = 0;
    std::map<wstring, wstring> descCache;
    bool byMem = false;
    int tickN = 0;
    double totalCpu = 0;
    ProcessesPage() : Page(PG_PROCESSES, L"Процессы", L"Что сейчас нагружает процессор и память — сгруппировано по программам", Ico::Chart) {}

    wstring Describe(const wstring& path) {
        auto it = descCache.find(path);
        if (it != descCache.end()) return it->second;
        wstring d;
        DWORD hnd = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &hnd);
        if (sz) {
            vector<BYTE> b(sz);
            if (GetFileVersionInfoW(path.c_str(), 0, sz, b.data())) {
                struct LANG { WORD l, c; }* tr = nullptr;
                UINT n = 0;
                if (VerQueryValueW(b.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &n) && n >= sizeof(LANG)) {
                    wchar_t* v = nullptr;
                    UINT vl = 0;
                    if (VerQueryValueW(b.data(), Fmt(L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].l, tr[0].c).c_str(), (void**)&v, &vl) && v) d = Trim(v);
                }
            }
        }
        descCache[path] = d;
        return d;
    }

    void Sample() {
        std::map<wstring, ProcGroup> g;
        ULONGLONG now = GetTickCount64();
        double elapsed = lastTick ? (double)(now - lastTick) * 10000.0 : 0;
        int ncpu = std::max(1, (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        std::map<DWORD, uint64_t> times;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{ sizeof(pe) };
        DWORD self = GetCurrentProcessId();
        totalCpu = 0;
        if (Process32FirstW(snap, &pe)) do {
            if (pe.th32ProcessID == 0) continue;
            wstring name = pe.szExeFile;
            wstring key = Lower(name);
            auto& grp = g[key];
            grp.name = name;
            grp.count++;
            grp.pids.push_back(pe.th32ProcessID);
            for (auto p : kProtected) if (key == p) grp.prot = true;
            if (pe.th32ProcessID == self) grp.prot = true;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) continue;
            FILETIME c, e, k, u;
            if (GetProcessTimes(h, &c, &e, &k, &u)) {
                uint64_t t = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
                times[pe.th32ProcessID] = t;
                auto it = lastTimes.find(pe.th32ProcessID);
                if (it != lastTimes.end() && elapsed > 0 && t >= it->second) {
                    double pct = (double)(t - it->second) / elapsed / ncpu * 100.0;
                    grp.cpu += pct;
                    totalCpu += pct;
                }
            }
            PROCESS_MEMORY_COUNTERS pmc{ sizeof(pmc) };
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) grp.mem += pmc.WorkingSetSize;
            if (grp.path.empty()) {
                wchar_t path[MAX_PATH * 2];
                DWORD n = MAX_PATH * 2;
                if (QueryFullProcessImageNameW(h, 0, path, &n)) { grp.path = path; grp.desc = Describe(path); }
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
        CloseHandle(snap);
        lastTimes = times;
        lastTick = now;
        groups.clear();
        for (auto& kv : g) groups.push_back(kv.second);
        Sort();
    }
    void Sort() {
        std::sort(groups.begin(), groups.end(), [this](const ProcGroup& a, const ProcGroup& b) {
            if (byMem) return a.mem > b.mem;
            if ((int)(a.cpu * 10) != (int)(b.cpu * 10)) return a.cpu > b.cpu;
            return a.mem > b.mem;
        });
    }
    void OnShow() override { Sample(); }
    bool Tick() override {
        if (++tickN % 2 == 0) Sample();
        return true;
    }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Монитор ресурсов", [] { OpenSystemTool(L"resmon.exe"); }, Btn::Secondary, Ico::Open);
        x = ButtonRight(x - 10, y, 36, L"По памяти", [this] { byMem = true; Sort(); }, byMem ? Btn::Soft : Btn::Secondary);
        ButtonRight(x - 6, y, 36, L"По процессору", [this] { byMem = false; Sort(); }, byMem ? Btn::Secondary : Btn::Soft);
    }
    float Body(float x, float y, float w) override {
        uint64_t memTotal = 0;
        for (auto& g : groups) memTotal += g.mem;
        y += Banner(x, y, w, Sev::Info, L"Список обновляется каждые 2 секунды",
                    L"Высокая постоянная загрузка процессора неизвестной программой — повод проверить её антивирусом (AVZ на странице «Защита»). "
                    L"Системные процессы Windows завершить нельзя — они отмечены замком.") + 16;
        float cName = 14, cCnt = w * 0.52f, cCpu = w * 0.62f, cMem = w * 0.78f;
        y += TableHeader(x, y, w, { { L"ПРОГРАММА", cName + 34 }, { L"ПРОЦЕССОВ", cCnt }, { L"ПРОЦЕССОР", cCpu }, { L"ПАМЯТЬ", cMem } });
        int shown = 0;
        for (auto& g : groups) {
            if (++shown > 60) break;
            RectF r(x, y, w, 44);
            bool hv = Hover(r);
            if (hv) FillRound(r, 8, P.surfaceHover);
            wstring title = g.desc.empty() ? g.name : g.desc;
            if (g.prot || g.path.empty()) Glyph(g.prot ? Ico::Lock : Ico::Apps, RectF(x + 10, y, 24, 44), 13, P.muted);
            else DrawFileIcon(g.path, RectF(x + 10, y + 10, 24, 24), Ico::Apps);
            Text(title, RectF(x + 44, y + 4, cCnt - 54, 20), FontUI(13, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(g.name, RectF(x + 44, y + 23, cCnt - 54, 16), FontUI(11.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(std::to_wstring(g.count), RectF(x + cCnt, y, 80, 44), FontUI(13), P.text, T_LEFT | T_VCENTER);
            Color cc = g.cpu > 50 ? P.danger : g.cpu > 15 ? P.warn : P.text;
            Progress(RectF(x + cCpu, y + 30, w * 0.12f, 4), (float)g.cpu / 100, cc, false);
            Text(Fmt(L"%.1f %%", g.cpu), RectF(x + cCpu, y + 4, 100, 24), FontUI(13, 1), cc, T_LEFT | T_VCENTER);
            Color mc = g.mem > 2ull << 30 ? P.warn : P.text;
            Text(HumanSize(g.mem), RectF(x + cMem, y, 110, 44), FontUI(13, 1), mc, T_LEFT | T_VCENTER);
            if (!g.prot && hv) {
                auto pids = g.pids;
                wstring nm = title;
                int cnt = g.count;
                Button(RectF(r.GetRight() - 120, y + 6, 110, 32), L"Завершить", [pids, nm, cnt] {
                    Confirm(L"Завершить «" + nm + L"»?",
                            Fmt(L"Будет закрыто процессов: %d. Несохранённые данные в этой программе пропадут.", cnt), L"Завершить", [pids, nm] {
                        int ok = 0;
                        for (DWORD pid : pids) {
                            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                            if (h) { if (TerminateProcess(h, 1)) ++ok; CloseHandle(h); }
                        }
                        Toast(ok ? L"Завершено: " + nm : L"Не удалось завершить " + nm, ok ? Sev::Ok : Sev::Warn);
                    }, true);
                }, Btn::Danger);
            }
            y += 46;
        }
        return y + 10;
    }
};
Page* MakeProcesses() { return new ProcessesPage; }
