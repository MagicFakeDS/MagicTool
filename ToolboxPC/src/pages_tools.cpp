#include "app.h"
#include <iphlpapi.h>
#include <icmpapi.h>
#include <winhttp.h>
#include <wincrypt.h>

void CopyText(const wstring& s, const wstring& toast) {
    if (!OpenClipboard(app.hwnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * 2);
    memcpy(GlobalLock(g), s.c_str(), (s.size() + 1) * 2);
    GlobalUnlock(g);
    SetClipboardData(CF_UNICODETEXT, g);
    CloseClipboard();
    if (!toast.empty()) Toast(toast);
}

static const wchar_t* kRestorePointPS = LR"PS(
$k = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SystemRestore'
$old = (Get-ItemProperty $k -Name SystemRestorePointCreationFrequency -ErrorAction SilentlyContinue).SystemRestorePointCreationFrequency
'Включаю защиту системы для диска ' + $env:SystemDrive + '...'
try { Enable-ComputerRestore -Drive ($env:SystemDrive + '\') -ErrorAction Stop } catch { '  не удалось включить: ' + $_.Exception.Message }
try { Set-ItemProperty $k -Name SystemRestorePointCreationFrequency -Value 0 -Type DWord -ErrorAction Stop } catch { }
'Создаю точку восстановления (до минуты)...'
try { Checkpoint-Computer -Description 'MagicTool' -RestorePointType MODIFY_SETTINGS -ErrorAction Stop; 'RESULT OK' }
catch { 'RESULT ERR ' + $_.Exception.Message }
finally {
  if ($old -eq $null) { Remove-ItemProperty $k -Name SystemRestorePointCreationFrequency -ErrorAction SilentlyContinue }
  else { Set-ItemProperty $k -Name SystemRestorePointCreationFrequency -Value $old -ErrorAction SilentlyContinue }
}
)PS";

static bool g_rpCreated = false;
static wstring g_rpTime;

void StartRestorePoint(int page) {
    if (GuardBusy()) return;
    StartTask(L"Точка восстановления", page, [](TaskCtx& c) {
        c.PS(kRestorePointPS);
    }, [](Task& t) {
        bool ok = false;
        wstring err;
        for (auto& l : t.lines) {
            if (l == L"RESULT OK") ok = true;
            if (IStarts(l, L"RESULT ERR")) err = l.substr(11);
        }
        t.ok = ok;
        t.summary = ok ? L"создана" : L"не создана";
        if (ok) {
            g_rpCreated = true;
            g_rpTime = NowStr(L"%04d-%02d-%02d %02d:%02d").substr(11);
            Journal::Add(L"restorepoint", L"Создана точка восстановления «MagicTool»", L"Откат всей системы: rstrui.exe");
        } else Message(L"Точка восстановления не создана", err.empty() ? L"Подробности — в журнале операции." : err, Sev::Warn);
    });
}

float RestorePointBanner(float x, float y, float w, int page) {
    if (g_rpCreated)
        return Banner(x, y, w, Sev::Ok, L"Точка восстановления создана в " + g_rpTime,
                      L"Если что-то пойдёт не так, систему можно откатить целиком через «Восстановление системы».") + 14;
    auto t = LastTaskFor(page);
    if (t && t->running && t->name == L"Точка восстановления")
        return Banner(x, y, w, Sev::Info, L"Создаю точку восстановления…", L"Обычно это занимает до минуты.") + 14;
    return Banner(x, y, w, Sev::Warn, L"Перед изменениями создайте точку восстановления",
                  L"Каждое действие здесь можно отменить и по отдельности, но точка восстановления — дополнительная страховка для всей системы.",
                  L"Создать точку", [page] { StartRestorePoint(page); }) + 14;
}

static wstring Q(const wstring& s) { return L"\"" + s + L"\""; }

static void RunTool(const wstring& name, int page, vector<wstring> cmds, const wstring& journalTitle = L"",
                    std::function<void(Task&)> extra = nullptr) {
    if (GuardBusy()) return;
    StartTask(name, page, [cmds](TaskCtx& c) {
        int last = 0;
        for (auto& cmd : cmds) {
            if (c.Cancelled()) break;
            c.Line(L"> " + cmd);
            last = c.Run(cmd);
            c.Line(Fmt(L"(код завершения %d)", last));
        }
        if (last != 0 && last != 3010) c.Fail(Fmt(L"код %d", last));
    }, [journalTitle, extra](Task& t) {
        if (extra) extra(t);
        if (!journalTitle.empty()) Journal::Add(L"repair", journalTitle, t.summary);
    });
}

static wstring LastMeaningful(const Task& t, const vector<wstring>& keys) {
    for (auto it = t.lines.rbegin(); it != t.lines.rend(); ++it) {
        wstring l = (*it)[0] == L'\r' ? it->substr(1) : *it;
        for (auto& k : keys) if (IContains(l, k)) return Trim(l);
    }
    return L"";
}

struct RepairPage : Page {
    int safeMode = -1;
    RepairPage() : Page(PG_REPAIR, L"Ремонт Windows", L"Проверка и восстановление системных файлов, диска, загрузки и Центра обновления", Ico::Wrench) {}
    void OnShow() override {
        Async([this]() -> std::function<void()> {
            wstring out = RunOutput(Q(Sys32(L"bcdedit.exe")) + L" /enum {current}");
            int sm = IContains(out, L"safeboot") ? 1 : 0;
            return [this, sm] { safeMode = sm; };
        });
    }
    void Header(float right, float y) override {
        ButtonRight(right, y, 36, L"Восстановление системы", [] { OpenSystemTool(Sys32(L"rstrui.exe")); }, Btn::Secondary, Ico::Undo);
    }
    static void FullRepair() {
        Confirm(L"Запустить полную проверку системы?",
                L"1) DISM восстановит хранилище компонентов Windows (скачает исправные файлы с серверов Microsoft — нужен интернет).\n"
                L"2) SFC проверит и заменит повреждённые системные файлы.\n\nЗаймёт 15–40 минут. Файлы и программы клиента не затрагиваются. Компьютером можно пользоваться.",
                L"Запустить", [] {
            RunTool(L"Полная проверка системы", PG_REPAIR,
                    { Q(Sys32(L"dism.exe")) + L" /Online /Cleanup-Image /RestoreHealth", Q(Sys32(L"sfc.exe")) + L" /scannow" },
                    L"Полная проверка системы (DISM + SFC)", [](Task& t) {
                wstring s = LastMeaningful(t, { L"защиты ресурсов", L"Resource Protection", L"нарушений", L"violations" });
                if (!s.empty()) t.summary = s.size() > 120 ? s.substr(0, 117) + L"…" : s;
            });
        }, false, Sev::Info);
    }
    float Body(float x, float y, float w) override {
        y += SectionTitle(x, y, w, L"Системные файлы", L"Рекомендуемый порядок при сбоях Windows: точка восстановления → полная проверка → перезагрузка.");
        float cw = (w - 32) / 3, ch = 184;
        bool busy = TaskBusy();
        ActionCard(RectF(x, y, cw, ch), Ico::Shield, L"Точка восстановления", L"Снимок системы, к которому можно откатиться, если ремонт что-то испортит.",
                   L"Создать", [] { StartRestorePoint(PG_REPAIR); }, Btn::Secondary, !busy, g_rpCreated ? L"создана" : L"", Sev::Ok);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Wrench, L"Полная проверка системы", L"DISM RestoreHealth + SFC. Лечит большинство «странных» сбоев Windows, ошибок обновления и меню Пуск.",
                   L"Запустить", [] { FullRepair(); }, Btn::Primary, !busy, L"рекомендуется", Sev::Info);
        ActionCard(RectF(x + 2 * (cw + 16), y, cw, ch), Ico::Check, L"Проверка файлов (SFC)", L"Только sfc /scannow: быстрая проверка и замена системных файлов из локального хранилища.",
                   L"Запустить", [] { RunTool(L"Проверка файлов SFC", PG_REPAIR, { Q(Sys32(L"sfc.exe")) + L" /scannow" }, L"Проверка системных файлов (SFC)", [](Task& t) {
                       wstring s = LastMeaningful(t, { L"защиты ресурсов", L"Resource Protection" });
                       if (!s.empty()) t.summary = s.size() > 120 ? s.substr(0, 117) + L"…" : s;
                   }); }, Btn::Secondary, !busy);
        y += ch + 16;
        ActionCard(RectF(x, y, cw, ch), Ico::Download, L"Восстановление образа (DISM)", L"DISM /RestoreHealth — чинит само хранилище компонентов, из которого SFC берёт файлы. Нужен интернет.",
                   L"Запустить", [] { RunTool(L"DISM RestoreHealth", PG_REPAIR, { Q(Sys32(L"dism.exe")) + L" /Online /Cleanup-Image /RestoreHealth" }, L"Восстановление образа Windows (DISM)"); },
                   Btn::Secondary, !busy);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Broom, L"Очистка хранилища компонентов", L"Удаляет старые версии обновлений из WinSxS. Освобождает 1–10 ГБ. После неё нельзя удалить уже установленные обновления.",
                   L"Очистить", [] {
                       Confirm(L"Очистить хранилище компонентов?", L"DISM /StartComponentCleanup удалит заменённые версии системных файлов. Занимает 5–20 минут.", L"Очистить", [] {
                           RunTool(L"Очистка WinSxS", PG_REPAIR, { Q(Sys32(L"dism.exe")) + L" /Online /Cleanup-Image /StartComponentCleanup" }, L"Очистка хранилища компонентов (WinSxS)");
                       }, false, Sev::Info);
                   }, Btn::Secondary, !busy);
        ActionCard(RectF(x + 2 * (cw + 16), y, cw, ch), Ico::Refresh, L"Сброс Центра обновления", L"Останавливает службы обновления, переименовывает SoftwareDistribution и catroot2. Лечит зависшие и не устанавливающиеся обновления.",
                   L"Сбросить", [] {
                       Confirm(L"Сбросить Центр обновления?", L"Старые папки будут переименованы (не удалены) — их можно удалить позже. История обновлений в Параметрах очистится.", L"Сбросить", [] {
                           if (GuardBusy()) return;
                           StartTask(L"Сброс Центра обновления", PG_REPAIR, [](TaskCtx& c) {
                               const wchar_t* svcs[] = { L"wuauserv", L"bits", L"cryptsvc", L"msiserver", L"usosvc" };
                               for (auto s : svcs) { c.Line(wstring(L"Останавливаю ") + s + L"…"); SvcStop(s, 20000); }
                               wstring suf = NowStr(L".bak_%04d%02d%02d_%02d%02d%02d");
                               wstring sd = WinDir() + L"\\SoftwareDistribution", cr = Sys32(L"catroot2");
                               c.Line(MoveFileExW(sd.c_str(), (sd + suf).c_str(), 0) ? L"SoftwareDistribution переименована" : Fmt(L"SoftwareDistribution: ошибка %lu", GetLastError()));
                               c.Line(MoveFileExW(cr.c_str(), (cr + suf).c_str(), 0) ? L"catroot2 переименована" : Fmt(L"catroot2: ошибка %lu", GetLastError()));
                               for (auto s : { L"cryptsvc", L"bits", L"wuauserv" }) { c.Line(wstring(L"Запускаю ") + s + L"…"); SvcStart(s); }
                               c.Done(L"готово — проверьте обновления в Параметрах");
                           }, [](Task& t) { Journal::Add(L"repair", L"Сброс компонентов Центра обновления", t.summary); });
                       }, true);
                   }, Btn::Secondary, !busy);
        y += ch + 16;
        ActionCard(RectF(x, y, cw, ch), Ico::Apps, L"Ремонт приложений Windows",
                   L"Заново регистрирует встроенные приложения и плитки «Пуска». Лечит не открывающиеся Пуск, Параметры, Калькулятор, Фотографии. Без перезагрузки.",
                   L"Починить", [] {
                       Confirm(L"Перерегистрировать приложения Windows?",
                               L"Все встроенные приложения заново зарегистрируются для текущих пользователей (PowerShell). Данные не теряются, перезагрузка не нужна. 1–3 минуты.",
                               L"Починить", [] {
                           if (GuardBusy()) return;
                           StartTask(L"Ремонт приложений Windows", PG_REPAIR, [](TaskCtx& c) {
                               const wchar_t* ps =
                                   L"$ErrorActionPreference='SilentlyContinue';$n=0;"
                                   L"Get-AppxPackage -AllUsers | ForEach-Object {"
                                   L"if($_.InstallLocation -and (Test-Path \"$($_.InstallLocation)\\AppxManifest.xml\")){"
                                   L"$n++;'Регистрирую: '+$_.Name;"
                                   L"Add-AppxPackage -DisableDevelopmentMode -Register \"$($_.InstallLocation)\\AppxManifest.xml\"}};"
                                   L"'Обработано приложений: '+$n";
                               if (c.PS(ps) != 0) c.Fail(L"не удалось запустить PowerShell");
                               else c.Done(L"готово — если Пуск ещё не открывается, перезайдите в систему");
                           }, [](Task& t) { Journal::Add(L"repair", L"Ремонт приложений Windows (перерегистрация)", t.summary); });
                       }, false, Sev::Info);
                   }, Btn::Secondary, !busy);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Broom, L"Сбросить кэш Microsoft Store",
                   L"wsreset — очищает кэш магазина. Лечит ошибки загрузки и обновления приложений из Microsoft Store.",
                   L"Сбросить", [] { RunTool(L"Сброс кэша Microsoft Store", PG_REPAIR, { Q(Sys32(L"wsreset.exe")) }, L"Сброс кэша Microsoft Store"); }, Btn::Secondary, !busy);
        y += ch + 16;
        float con = ConsoleBox(x, y, w, 260, PG_REPAIR);
        if (con) y += con + 20;

        y += SectionTitle(x, y, w, L"Диск, память и загрузка");
        ActionCard(RectF(x, y, cw, ch), Ico::Disk, L"Проверка диска C:", L"chkdsk /scan — проверка файловой системы без перезагрузки и без остановки работы.",
                   L"Проверить", [] { RunTool(L"Проверка диска C:", PG_REPAIR, { Q(Sys32(L"chkdsk.exe")) + L" " + SystemDrive() + L" /scan" }, L"Проверка диска C: (chkdsk /scan)"); },
                   Btn::Secondary, !busy);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Power, L"Исправить диск при перезагрузке", L"Помечает диск C: для полной проверки (chkdsk /f) при следующем включении. Проверка идёт до загрузки Windows.",
                   L"Запланировать", [] {
                       Confirm(L"Запланировать проверку диска?", L"При следующей перезагрузке перед стартом Windows запустится проверка диска C:. Она может занять от минуты до часа — не выключайте ПК.", L"Запланировать", [] {
                           RunTool(L"Проверка диска при загрузке", PG_REPAIR, { Q(Sys32(L"fsutil.exe")) + L" dirty set " + SystemDrive() }, L"Запланирована проверка диска C: при перезагрузке");
                       }, false, Sev::Info);
                   }, Btn::Secondary, !busy);
        ActionCard(RectF(x + 2 * (cw + 16), y, cw, ch), Ico::Memory, L"Проверка оперативной памяти", L"Средство проверки памяти Windows. Предложит перезагрузить ПК и проверит ОЗУ до загрузки системы.",
                   L"Открыть", [] { OpenSystemTool(Sys32(L"mdsched.exe")); }, Btn::Secondary);
        y += ch + 16;
        ActionCard(RectF(x, y, cw, ch), Ico::Chip, L"Перезагрузка в BIOS/UEFI", L"Перезагрузит компьютер сразу в настройки BIOS — не нужно ловить клавишу Del/F2.",
                   L"Перезагрузить в BIOS", [] {
                       if (!app.si.uefi && app.si.ready) { Message(L"Недоступно", L"Компьютер загружен в режиме Legacy BIOS — так перезагрузиться в настройки нельзя. Используйте клавишу Del/F2 при включении.", Sev::Info); return; }
                       Confirm(L"Перезагрузить в BIOS?", L"Сохраните открытые документы. Компьютер перезагрузится через 5 секунд.", L"Перезагрузить", [] {
                           RunOutput(Q(Sys32(L"shutdown.exe")) + L" /r /fw /t 5");
                       }, true);
                   }, Btn::Secondary);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Wrench, L"Среда восстановления", L"Перезагрузка в «Особые варианты загрузки»: восстановление при загрузке, откат обновлений, командная строка.",
                   L"Перезагрузить", [] {
                       Confirm(L"Перезагрузить в среду восстановления?", L"Сохраните открытые документы. Компьютер перезагрузится через 5 секунд.", L"Перезагрузить", [] {
                           RunOutput(Q(Sys32(L"shutdown.exe")) + L" /r /o /t 5");
                       }, true);
                   }, Btn::Secondary);
        bool sm = safeMode == 1;
        ActionCard(RectF(x + 2 * (cw + 16), y, cw, ch), Ico::Shield, sm ? L"Выйти из безопасного режима" : L"Безопасный режим",
                   sm ? L"Сейчас Windows настроена загружаться в безопасном режиме. Верните обычную загрузку."
                      : L"Следующие загрузки пойдут в безопасном режиме (для удаления вирусов и драйверов). Выход — этой же кнопкой.",
                   sm ? L"Обычная загрузка" : L"Включить", [this, sm] {
                       wstring cmd = Q(Sys32(L"bcdedit.exe")) + (sm ? L" /deletevalue {current} safeboot" : L" /set {current} safeboot minimal");
                       auto go = [this, cmd, sm] {
                           int code = 0;
                           wstring out = RunOutput(cmd, &code);
                           if (code != 0) { Message(L"Не получилось", Trim(out), Sev::Warn); return; }
                           safeMode = sm ? 0 : 1;
                           if (!sm) Journal::Add(L"safeboot", L"Включена загрузка в безопасном режиме", L"", L"cmd", Q(Sys32(L"bcdedit.exe")) + L" /deletevalue {current} safeboot");
                           else {
                               if (JEntry* prev = Journal::LastFor(L"safeboot")) Journal::MarkUndone(prev->id);
                               Journal::Add(L"safeboot", L"Возвращена обычная загрузка", L"");
                           }
                           Toast(sm ? L"Обычная загрузка восстановлена" : L"Безопасный режим включён — перезагрузите ПК");
                       };
                       if (sm) go();
                       else Confirm(L"Включить безопасный режим?", L"После перезагрузки Windows запустится в безопасном режиме (без сети и лишних драйверов). "
                                    L"ВАЖНО: чтобы выйти, запустите MagicTool в безопасном режиме и нажмите «Обычная загрузка».", L"Включить", go, false, Sev::Warn);
                   }, sm ? Btn::Primary : Btn::Secondary, true, sm ? L"включён" : L"", Sev::Warn);
        y += ch + 24;

        y += SectionTitle(x, y, w, L"Полный сброс Windows",
                          L"Вернуть Windows в исходное состояние. Если запуск блокирует вирус — делайте это из «Среды восстановления» (выше) или с загрузочной флешки MagicTool: там вредонос не работает.");
        ActionCard(RectF(x, y, cw, ch), Ico::Refresh, L"Сбросить этот компьютер",
                   L"Переустановка Windows с выбором: сохранить личные файлы или удалить всё. Установленные программы будут удалены. Откроется мастер Windows.",
                   L"Открыть сброс", [] {
                       Confirm(L"Сбросить Windows в исходное состояние?",
                               L"Откроется мастер «Вернуть компьютер в исходное состояние» — там выберите «Сохранить мои файлы» или «Удалить всё». "
                               L"Все установленные программы будут удалены.\n\nЕсли мастер не открывается (мешает вирус) — используйте «Среду восстановления» выше или загрузочную флешку MagicTool.",
                               L"Открыть", [] { if (!ShellOpen(Sys32(L"systemreset.exe"))) Message(L"Не удалось открыть", L"Запустите из «Среды восстановления» или с флешки.", Sev::Warn); }, true);
                   }, Btn::Danger, true);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Wrench, L"Сброс из среды восстановления",
                   L"Перезагрузка в «Особые варианты загрузки», откуда можно вернуть ПК в исходное состояние, даже если Windows или вирус мешают сделать это обычным способом.",
                   L"Перезагрузить", [] {
                       Confirm(L"Перейти в среду восстановления?", L"Сохраните открытые документы. Компьютер перезагрузится через 5 секунд, дальше выберите «Диагностика» → «Вернуть компьютер в исходное состояние».", L"Перезагрузить", [] {
                           RunOutput(Q(Sys32(L"shutdown.exe")) + L" /r /o /t 5");
                       }, true);
                   }, Btn::Secondary);
        y += ch + 24;

        y += SectionTitle(x, y, w, L"Системные инструменты", L"Быстрый запуск стандартных оснасток Windows");
        struct T { const wchar_t* t; const wchar_t* cmd; const wchar_t* args; };
        static const T tools[] = {
            { L"Диспетчер устройств", L"devmgmt.msc", L"" }, { L"Управление дисками", L"diskmgmt.msc", L"" }, { L"Службы", L"services.msc", L"" },
            { L"Просмотр событий", L"eventvwr.msc", L"" }, { L"Конфигурация системы", L"msconfig.exe", L"" }, { L"Свойства системы", L"sysdm.cpl", L"" },
            { L"Сетевые подключения", L"ncpa.cpl", L"" }, { L"Редактор реестра", L"regedit.exe", L"" }, { L"Планировщик заданий", L"taskschd.msc", L"" },
            { L"Монитор ресурсов", L"resmon.exe", L"" }, { L"Командная строка", L"cmd.exe", L"" }, { L"PowerShell", L"powershell.exe", L"" },
            { L"Панель управления", L"control.exe", L"" }, { L"Параметры Windows", L"ms-settings:", L"" }, { L"Центр обновления", L"ms-settings:windowsupdate", L"" },
            { L"Управление компьютером", L"compmgmt.msc", L"" },
        };
        float bx = x, bw = (w - 3 * 12) / 4;
        int k = 0;
        for (auto& t : tools) {
            float tx = x + (k % 4) * (bw + 12), ty = y + (k / 4) * 46;
            wstring cmd = t.cmd, args = t.args;
            Button(RectF(tx, ty, bw, 38), t.t, [cmd, args] { OpenSystemTool(cmd, args); }, Btn::Secondary, Ico::Open);
            ++k;
        }
        (void)bx;
        y += ((k + 3) / 4) * 46 + 10;
        return y;
    }
};
Page* MakeRepair() { return new RepairPage; }

struct Adapter {
    wstring name, desc, guid, mac, ip, gateway, speed;
    vector<wstring> dns;
    bool up = false, wifi = false, virt = false, dnsStatic = false;
    ULONG index = 0;
};

static vector<Adapter> CollectAdapters() {
    vector<Adapter> r;
    ULONG sz = 32 * 1024;
    vector<BYTE> buf(sz);
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
                                    (PIP_ADAPTER_ADDRESSES)buf.data(), &sz);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(sz);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
                                  (PIP_ADAPTER_ADDRESSES)buf.data(), &sz);
    }
    if (rc != NO_ERROR) return r;
    auto sa = [](SOCKADDR* s) -> wstring {
        wchar_t b[64] = {};
        DWORD n = 64;
        WSAAddressToStringW(s, s->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), nullptr, b, &n);
        return b;
    };
    for (auto a = (PIP_ADAPTER_ADDRESSES)buf.data(); a; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL) continue;
        Adapter d;
        d.name = a->FriendlyName;
        d.desc = a->Description;
        d.index = a->IfIndex;
        d.up = a->OperStatus == IfOperStatusUp;
        d.wifi = a->IfType == IF_TYPE_IEEE80211;
        wstring g = Utf8ToW(a->AdapterName);
        d.guid = g;
        for (auto v : { L"Hyper-V", L"VirtualBox", L"VMware", L"TAP-", L"Wintun", L"WireGuard", L"OpenVPN", L"Virtual", L"Bluetooth", L"Loopback", L"ZeroTier", L"Radmin", L"Hamachi", L"WAN Miniport" })
            if (IContains(d.desc, v) || IContains(d.name, v)) d.virt = true;
        for (ULONG i = 0; i < a->PhysicalAddressLength; ++i) d.mac += Fmt(i ? L"-%02X" : L"%02X", a->PhysicalAddress[i]);
        if (a->FirstUnicastAddress) d.ip = sa(a->FirstUnicastAddress->Address.lpSockaddr);
        if (a->FirstGatewayAddress) d.gateway = sa(a->FirstGatewayAddress->Address.lpSockaddr);
        for (auto s = a->FirstDnsServerAddress; s; s = s->Next) d.dns.push_back(sa(s->Address.lpSockaddr));
        if (a->TransmitLinkSpeed && a->TransmitLinkSpeed != (ULONG64)-1)
            d.speed = a->TransmitLinkSpeed >= 1000000000ULL
                          ? (a->TransmitLinkSpeed % 1000000000ULL ? Fmt(L"%.1f Гбит/с", a->TransmitLinkSpeed / 1e9) : Fmt(L"%llu Гбит/с", a->TransmitLinkSpeed / 1000000000ULL))
                          : Fmt(L"%llu Мбит/с", a->TransmitLinkSpeed / 1000000ULL);
            for (auto& ch : d.speed) if (ch == L'.') ch = L',';
        wstring ns;
        RegGetStr(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + g, L"NameServer", ns);
        d.dnsStatic = !Trim(ns).empty();
        r.push_back(d);
    }
    std::stable_sort(r.begin(), r.end(), [](const Adapter& a, const Adapter& b) {
        if (a.up != b.up) return a.up;
        if (a.virt != b.virt) return !a.virt;
        return !a.gateway.empty() && b.gateway.empty();
    });
    return r;
}

struct NetCheck { bool done = false; int gwMs = -1, inetMs = -1, dnsMs = -1; bool http = false, captive = false; wstring gw; };

static int PingMs(const wstring& host) {
    IN_ADDR addr;
    if (InetPtonW(AF_INET, host.c_str(), &addr) != 1) return -1;
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return -1;
    char data[32] = "toolbox";
    BYTE reply[sizeof(ICMP_ECHO_REPLY) + 64];
    int best = -1;
    for (int i = 0; i < 3; ++i) {
        DWORD n = IcmpSendEcho(h, addr.S_un.S_addr, data, sizeof(data), nullptr, reply, sizeof(reply), 1500);
        if (n > 0) {
            auto r = (PICMP_ECHO_REPLY)reply;
            if (r->Status == IP_SUCCESS && (best < 0 || (int)r->RoundTripTime < best)) best = (int)r->RoundTripTime;
        }
    }
    IcmpCloseHandle(h);
    return best;
}

static NetCheck RunNetCheck(const wstring& gw) {
    NetCheck c;
    c.gw = gw;
    if (!gw.empty()) c.gwMs = PingMs(gw);
    int a = PingMs(L"8.8.8.8"), b = PingMs(L"1.1.1.1");
    c.inetMs = a < 0 ? b : b < 0 ? a : std::min(a, b);
    DWORD t0 = GetTickCount();
    ADDRINFOW* res = nullptr;
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    if (GetAddrInfoW(L"www.msftconnecttest.com", nullptr, nullptr, &res) == 0) { c.dnsMs = (int)(GetTickCount() - t0); FreeAddrInfoW(res); }
    HINTERNET s = WinHttpOpen(L"ToolboxPC", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (s) {
        WinHttpSetTimeouts(s, 4000, 4000, 4000, 4000);
        HINTERNET cn = WinHttpConnect(s, L"www.msftconnecttest.com", 80, 0);
        HINTERNET rq = cn ? WinHttpOpenRequest(cn, L"GET", L"/connecttest.txt", nullptr, nullptr, nullptr, 0) : nullptr;
        if (rq && WinHttpSendRequest(rq, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(rq, nullptr)) {
            char body[256] = {};
            DWORD got = 0;
            WinHttpReadData(rq, body, 255, &got);
            c.http = strstr(body, "Microsoft Connect Test") != nullptr;
            c.captive = !c.http && got > 0;
        }
        if (rq) WinHttpCloseHandle(rq);
        if (cn) WinHttpCloseHandle(cn);
        WinHttpCloseHandle(s);
    }
    c.done = true;
    return c;
}

static const char* kDefaultHosts =
    "# Copyright (c) 1993-2009 Microsoft Corp.\r\n#\r\n# This is a sample HOSTS file used by Microsoft TCP/IP for Windows.\r\n#\r\n"
    "# This file contains the mappings of IP addresses to host names. Each\r\n# entry should be kept on an individual line. The IP address should\r\n"
    "# be placed in the first column followed by the corresponding host name.\r\n# The IP address and the host name should be separated by at least one\r\n# space.\r\n#\r\n"
    "# localhost name resolution is handled within DNS itself.\r\n#\t127.0.0.1       localhost\r\n#\t::1             localhost\r\n";

struct NetworkPage : Page {
    vector<Adapter> ads;
    NetCheck chk;
    bool checking = false;
    vector<wstring> hostsLines;
    NetworkPage() : Page(PG_NETWORK, L"Сеть", L"Проверка интернета, DNS, сброс сетевых настроек, прокси и hosts", Ico::Globe) {}
    void Refresh() {
        ads = CollectAdapters();
        hostsLines.clear();
        std::string h;
        if (ReadFileBytes(Sys32(L"drivers\\etc\\hosts"), h))
            for (auto& l : Split(Utf8ToW(h), L'\n')) {
                wstring t = Trim(l);
                if (!t.empty() && t[0] != L'#' && !IContains(t, L"localhost")) hostsLines.push_back(t);
            }
    }
    void Check() {
        if (checking) return;
        checking = true;
        wstring gw;
        for (auto& a : ads) if (a.up && !a.gateway.empty()) { gw = a.gateway; break; }
        Async([this, gw]() -> std::function<void()> {
            NetCheck c = RunNetCheck(gw);
            return [this, c] { chk = c; checking = false; };
        });
    }
    void OnShow() override { Refresh(); if (!chk.done) Check(); }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Проверить соединение", [this] { Refresh(); Check(); }, Btn::Primary, Ico::Refresh, !checking);
        ButtonRight(x - 10, y, 36, L"Сетевые подключения", [] { OpenSystemTool(L"ncpa.cpl"); }, Btn::Secondary, Ico::Open);
    }
    void SetDns(const wstring& label, const vector<wstring>& servers) {
        vector<Adapter> targets;
        for (auto& a : ads) if (a.up && !a.virt && !a.gateway.empty()) targets.push_back(a);
        if (targets.empty()) { Message(L"Нет активных подключений", L"Не найдено подключённых сетевых адаптеров с выходом в сеть.", Sev::Warn); return; }
        wstring names;
        for (auto& a : targets) names += L"• " + a.name + L"\n";
        Confirm(L"DNS: " + label + L"?", names + L"\nСтарые настройки DNS сохранятся в журнале — вернуть можно на странице «Откат и журнал».", L"Применить", [this, targets, servers, label] {
            wstring undo;
            wstring nsh = Q(Sys32(L"netsh.exe")) + L" interface ipv4 ";
            for (auto& a : targets) {
                if (!undo.empty()) undo += L'\x1E';
                undo += std::to_wstring(a.index) + L'\x1F' + (a.dnsStatic ? L"static" : L"dhcp") + L'\x1F' + Join(a.dns, L",") + L'\x1F' + a.guid;
                wstring idx = std::to_wstring(a.index);
                if (servers.empty()) RunOutput(nsh + L"set dnsservers name=" + idx + L" source=dhcp");
                else for (size_t i = 0; i < servers.size(); ++i) {
                    if (i == 0) RunOutput(nsh + L"set dnsservers name=" + idx + L" source=static address=" + servers[i] + L" register=primary validate=no");
                    else RunOutput(nsh + L"add dnsservers name=" + idx + L" address=" + servers[i] + Fmt(L" index=%d validate=no", (int)i + 1));
                }
            }
            RunOutput(Q(Sys32(L"ipconfig.exe")) + L" /flushdns");
            Journal::Add(L"dns", L"DNS: " + label, servers.empty() ? L"автоматически (DHCP)" : Join(servers, L", "), L"dns", undo);
            Refresh();
            Toast(L"DNS изменён: " + label);
        }, false, Sev::Info);
    }
    void ResetProxy() {
        const wstring is = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
        const wstring conn = is + L"\\Connections";

        vector<RegSnap> s = { RegTake(HKEY_CURRENT_USER, is, L"ProxyEnable"), RegTake(HKEY_CURRENT_USER, is, L"ProxyServer"),
                              RegTake(HKEY_CURRENT_USER, is, L"AutoConfigURL"),
                              RegTake(HKEY_CURRENT_USER, conn, L"DefaultConnectionSettings"),
                              RegTake(HKEY_CURRENT_USER, conn, L"SavedLegacySettings"),
                              RegTake(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Connections", L"WinHttpSettings", KEY_WOW64_64KEY),
                              RegTake(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Connections", L"WinHttpSettings", KEY_WOW64_32KEY) };

        struct PCO { DWORD dwOption; union { DWORD dwValue; LPWSTR pszValue; FILETIME ftValue; } Value; };
        struct PCL { DWORD dwSize; LPWSTR pszConnection; DWORD dwOptionCount; DWORD dwOptionError; PCO* pOptions; };
        typedef BOOL(WINAPI * SetOptFn)(void*, DWORD, void*, DWORD);
        if (HMODULE wi = LoadLibraryW(L"wininet.dll")) {
            if (auto f = (SetOptFn)GetProcAddress(wi, "InternetSetOptionW")) {
                PCO o[1];
                o[0].dwOption = 1;
                o[0].Value.dwValue = 1;
                PCL l{ sizeof(PCL), nullptr, 1, 0, o };
                f(nullptr, 75  , &l, sizeof(l));
            }
            FreeLibrary(wi);
        }
        RegSetDword(HKEY_CURRENT_USER, is, L"ProxyEnable", 0);
        RegDelValue(HKEY_CURRENT_USER, is, L"ProxyServer");
        RegDelValue(HKEY_CURRENT_USER, is, L"AutoConfigURL");
        NotifyProxyChanged();
        RunOutput(Q(Sys32(L"netsh.exe")) + L" winhttp reset proxy");
        Journal::Add(L"proxy", L"Сброшены настройки прокси", app.si.proxyServer, L"reg", RegSnapsToStr(s));
        app.si.proxyOn = false;
        Toast(L"Прокси отключён");
    }
    void ResetHosts() {
        wstring hosts = Sys32(L"drivers\\etc\\hosts");
        wstring bak = BackupDir() + L"\\hosts_" + NowStr(L"%04d%02d%02d_%02d%02d%02d");
        bool had = FileExists(hosts);
        if (had && !CopyFileW(hosts.c_str(), bak.c_str(), FALSE)) { Message(L"Не получилось", L"Не удалось сохранить копию hosts.", Sev::Warn); return; }
        SetFileAttributesW(hosts.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!WriteFileUtf8(hosts, kDefaultHosts)) { Message(L"Не получилось", L"Файл hosts занят или защищён антивирусом.", Sev::Warn); return; }
        RunOutput(Q(Sys32(L"ipconfig.exe")) + L" /flushdns");
        Journal::Add(L"hosts", L"Файл hosts восстановлен по умолчанию", Fmt(L"Убрано записей: %d", (int)hostsLines.size()),
                     had ? L"hosts" : L"", had ? bak : L"");
        app.si.hostsEntries = 0;
        Refresh();
        Toast(L"hosts восстановлен");
    }
    float Tile(RectF r, const wchar_t* icon, const wstring& label, const wstring& value, Sev s) {
        Card(r);
        RectF ib(r.X + 16, r.Y + 18, 36, 36);
        FillRound(ib, 10, SevSoft(s));
        Glyph(icon, ib, 16, SevColor(s));
        Text(label, RectF(r.X + 64, r.Y + 14, r.Width - 74, 18), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
        Text(value, RectF(r.X + 64, r.Y + 34, r.Width - 74, 26), FontUI(16, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        return r.Height;
    }
    float Body(float x, float y, float w) override {
        float tw = (w - 48) / 4;
        auto ms = [](int v) { return v < 0 ? wstring(L"нет ответа") : v == 0 ? wstring(L"< 1 мс") : Fmt(L"%d мс", v); };
        if (checking || !chk.done) {
            for (int i = 0; i < 4; ++i) { Card(RectF(x + i * (tw + 16), y, tw, 72)); }
            Spinner(x + 40, y + 36, 10, P.accent);
            Text(L"Проверяю соединение…", RectF(x + 62, y, tw * 2, 72), FontUI(14), P.muted, T_LEFT | T_VCENTER);
        } else {
            bool inet = chk.http || chk.inetMs >= 0;
            Tile(RectF(x, y, tw, 72), Ico::Globe, L"Интернет", chk.http ? L"работает" : chk.captive ? L"нужен вход (портал)" : inet ? L"частично" : L"нет",
                 chk.http ? Sev::Ok : inet ? Sev::Warn : Sev::Bad);
            Tile(RectF(x + tw + 16, y, tw, 72), Ico::Network, L"Роутер (шлюз)", chk.gw.empty() ? L"нет шлюза" : ms(chk.gwMs),
                 chk.gwMs < 0 ? Sev::Bad : chk.gwMs > 20 ? Sev::Warn : Sev::Ok);
            Tile(RectF(x + 2 * (tw + 16), y, tw, 72), Ico::Speed, L"Пинг до 8.8.8.8", ms(chk.inetMs), chk.inetMs < 0 ? Sev::Bad : chk.inetMs > 80 ? Sev::Warn : Sev::Ok);
            Tile(RectF(x + 3 * (tw + 16), y, tw, 72), Ico::Search, L"DNS", chk.dnsMs < 0 ? L"не работает" : ms(chk.dnsMs), chk.dnsMs < 0 ? Sev::Bad : Sev::Ok);
        }
        y += 88;
        if (chk.done && !checking) {
            if (chk.dnsMs < 0 && chk.inetMs >= 0)
                y += Banner(x, y, w, Sev::Warn, L"Интернет есть, но не работает DNS", L"Сайты не открываются по именам. Поставьте DNS Cloudflare или Google ниже и сбросьте кэш DNS.") + 12;
            else if (!chk.http && chk.inetMs < 0 && chk.gwMs >= 0)
                y += Banner(x, y, w, Sev::Warn, L"Роутер отвечает, интернета нет", L"Проблема у провайдера или в настройках роутера. Перезагрузите роутер, проверьте договор/баланс.") + 12;
            else if (chk.gwMs < 0 && !chk.gw.empty())
                y += Banner(x, y, w, Sev::Bad, L"Нет связи с роутером", L"Проверьте кабель или Wi-Fi, драйвер сетевой карты. Попробуйте «Полный сброс сети».") + 12;
        }
        if (app.si.proxyOn)
            y += Banner(x, y, w, Sev::Warn, L"Включён прокси-сервер: " + app.si.proxyServer, L"Если клиент его не настраивал — это признак рекламного вируса.", L"Отключить прокси", [this] { ResetProxy(); }) + 12;
        if (!hostsLines.empty()) {
            wstring list;
            for (size_t i = 0; i < hostsLines.size() && i < 6; ++i) list += hostsLines[i] + L"\n";
            if (hostsLines.size() > 6) list += Fmt(L"…и ещё %d", (int)hostsLines.size() - 6);
            y += Banner(x, y, w, Sev::Info, Fmt(L"В файле hosts есть записи: %d", (int)hostsLines.size()), Trim(list), L"Сбросить hosts", [this] {
                Confirm(L"Восстановить hosts?", L"Файл hosts станет стандартным. Копия старого сохранится, откатить можно на странице «Откат и журнал».", L"Восстановить", [this] { ResetHosts(); }, false, Sev::Info);
            }) + 12;
        }
        y += 8;

        y += SectionTitle(x, y, w, L"Сетевые адаптеры");
        for (auto& a : ads) {
            if (a.virt && !a.up) continue;
            RectF r(x, y, w, 96);
            FillRound(r, 12, P.surface);
            RectF ib(x + 16, y + 18, 40, 40);
            FillRound(ib, 10, a.up ? Alpha(P.ok, g_dark ? 50 : 30) : P.surfaceHover);
            Glyph(a.wifi ? Ico::Wifi : Ico::Network, ib, 17, a.up ? P.ok : P.muted);
            Text(a.name, RectF(x + 70, y + 12, w * 0.4f, 22), FontUI(14, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(a.desc, RectF(x + 70, y + 34, w * 0.4f, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Badge(x + 70, y + 60, a.up ? L"подключён" + (a.speed.empty() ? L"" : L" · " + a.speed) : L"отключён", a.up ? Sev::Ok : Sev::Unknown, 22);
            if (a.virt) Badge(x + w * 0.4f + 60, y + 14, L"виртуальный", Sev::Info, 22, true);
            float kx = x + w * 0.46f, kw = w * 0.54f - 20;
            KeyValue(kx, y + 12, kw, L"IP-адрес", a.ip, 110);
            KeyValue(kx, y + 34, kw, L"Шлюз", a.gateway, 110);
            KeyValue(kx, y + 56, kw, L"DNS", (a.dns.empty() ? L"" : Join(a.dns, L", ")) + (a.up ? (a.dnsStatic ? L"  (вручную)" : L"  (авто)") : L""), 110);
            KeyValue(kx + kw * 0.62f, y + 12, kw * 0.38f, L"MAC", a.mac, 50);
            y += 104;
        }
        y += 12;

        y += SectionTitle(x, y, w, L"DNS-серверы", L"Применяется ко всем активным подключениям. Если сайты открываются медленно или «не находятся» — поставьте Cloudflare или Google.");
        struct D { const wchar_t* label; vector<wstring> s; };
        vector<D> presets = { { L"Автоматически (от роутера)", {} }, { L"Cloudflare 1.1.1.1", { L"1.1.1.1", L"1.0.0.1" } }, { L"Google 8.8.8.8", { L"8.8.8.8", L"8.8.4.4" } },
                              { L"Яндекс 77.88.8.8", { L"77.88.8.8", L"77.88.8.1" } }, { L"Яндекс Безопасный 77.88.8.88", { L"77.88.8.88", L"77.88.8.2" } },
                              { L"AdGuard (без рекламы)", { L"94.140.14.14", L"94.140.15.15" } } };
        float bx = x;
        for (auto& p : presets) {
            float bw = ButtonW(p.label);
            if (bx + bw > x + w) { bx = x; y += 46; }
            wstring l = p.label;
            auto s = p.s;
            Button(RectF(bx, y, bw, 38), l, [this, l, s] { SetDns(l, s); }, s.empty() ? Btn::Secondary : Btn::Soft);
            bx += bw + 10;
        }
        y += 58;

        y += SectionTitle(x, y, w, L"Исправление сети");
        float cw = (w - 32) / 3, ch = 150;
        bool busy = TaskBusy();
        ActionCard(RectF(x, y, cw, ch), Ico::Refresh, L"Сбросить кэш DNS", L"ipconfig /flushdns — если сайт переехал или открывается «старый».", L"Сбросить",
                   [] { RunTool(L"Сброс кэша DNS", PG_NETWORK, { Q(Sys32(L"ipconfig.exe")) + L" /flushdns" }, L"Сброшен кэш DNS"); }, Btn::Secondary, !busy);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Network, L"Обновить IP-адрес", L"Заново получить адрес от роутера (release/renew). Соединение пропадёт на несколько секунд.", L"Обновить", [] {
            Confirm(L"Обновить IP-адрес?", L"Сеть пропадёт на несколько секунд. Если вы подключены удалённо (RustDesk), связь может прерваться.", L"Обновить", [] {
                RunTool(L"Обновление IP-адреса", PG_NETWORK, { Q(Sys32(L"ipconfig.exe")) + L" /release", Q(Sys32(L"ipconfig.exe")) + L" /renew" }, L"Обновлён IP-адрес (ipconfig /renew)");
            }, false, Sev::Info);
        }, Btn::Secondary, !busy);
        ActionCard(RectF(x + 2 * (cw + 16), y, cw, ch), Ico::Globe, L"Отключить прокси", L"Сбросить прокси Windows и WinHTTP. Часто лечит «интернет есть, а браузер не открывает сайты».", L"Сбросить", [this] {
            Confirm(L"Отключить прокси?", L"Настройки прокси сохранятся в журнале, откатить можно.", L"Отключить", [this] { ResetProxy(); }, false, Sev::Info);
        }, Btn::Secondary);
        y += ch + 16;
        ActionCard(RectF(x, y, cw, ch), Ico::Wrench, L"Полный сброс сети", L"Winsock + TCP/IP + кэш DNS + прокси WinHTTP. Лечит большинство сетевых проблем. Нужна перезагрузка.", L"Сбросить", [] {
            Confirm(L"Выполнить полный сброс сети?", L"Будут сброшены каталог Winsock и стек TCP/IP. VPN-клиенты могут потребовать переустановки. Потребуется перезагрузка. "
                    L"Если вы подключены удалённо — связь прервётся после перезагрузки.", L"Сбросить", [] {
                RunTool(L"Полный сброс сети", PG_NETWORK,
                        { Q(Sys32(L"ipconfig.exe")) + L" /flushdns", Q(Sys32(L"netsh.exe")) + L" winsock reset", Q(Sys32(L"netsh.exe")) + L" int ip reset",
                          Q(Sys32(L"netsh.exe")) + L" winhttp reset proxy" },
                        L"Полный сброс сети (Winsock, TCP/IP)", [](Task& t) { if (t.ok) t.summary = L"готово — перезагрузите компьютер"; });
            }, true);
        }, Btn::Secondary, !busy);
        ActionCard(RectF(x + cw + 16, y, cw, ch), Ico::Shield, L"Сброс брандмауэра", L"Вернуть правила брандмауэра Windows по умолчанию. Программам может понадобиться заново разрешить доступ.", L"Сбросить", [] {
            Confirm(L"Сбросить брандмауэр?", L"Все добавленные правила будут удалены. Программы (игры, мессенджеры) при первом запуске снова спросят доступ к сети.", L"Сбросить", [] {
                RunTool(L"Сброс брандмауэра", PG_NETWORK, { Q(Sys32(L"netsh.exe")) + L" advfirewall reset" }, L"Брандмауэр сброшен по умолчанию");
            }, true);
        }, Btn::Secondary, !busy);
        ActionCard(RectF(x + 2 * (cw + 16), y, cw, ch), Ico::Report, L"Файл hosts", hostsLines.empty() ? L"Файл hosts стандартный — изменений нет." : Fmt(L"Найдено записей: %d. Вирусы прописывают сюда блокировку сайтов антивирусов.", (int)hostsLines.size()),
                   L"Открыть в Блокноте", [] { ShellOpen(Sys32(L"notepad.exe"), Q(Sys32(L"drivers\\etc\\hosts"))); }, Btn::Secondary);
        y += ch + 16;
        float con = ConsoleBox(x, y, w, 220, PG_NETWORK);
        if (con) y += con + 16;
        return y;
    }
};
Page* MakeNetwork() { return new NetworkPage; }

struct ActivationPage : Page {
    wstring key;
    ActivationPage() : Page(PG_ACTIVATION, L"Активация", L"Статус лицензии Windows и активация легальным ключом", Ico::Key) {}
    void OnShow() override { RequestSysInfo(); }
    void Header(float right, float y) override {
        float x = ButtonRight(right, y, 36, L"Параметры активации", [] { OpenSystemTool(L"ms-settings:activation"); }, Btn::Secondary, Ico::Open);
        ButtonRight(x - 10, y, 36, L"Подробный статус", [] {
            RunTool(L"Статус лицензии", PG_ACTIVATION, { Q(Sys32(L"cscript.exe")) + L" //nologo " + Q(Sys32(L"slmgr.vbs")) + L" /dli",
                                                         Q(Sys32(L"cscript.exe")) + L" //nologo " + Q(Sys32(L"slmgr.vbs")) + L" /xpr" });
        }, Btn::Secondary, Ico::Info, !TaskBusy());
    }
    static bool KeyValid(const wstring& k) {
        if (k.size() != 29) return false;
        for (size_t i = 0; i < k.size(); ++i) {
            if (i % 6 == 5) { if (k[i] != L'-') return false; }
            else if (!iswalnum(k[i])) return false;
        }
        return true;
    }
    void Activate() {
        if (GuardBusy()) return;
        wstring k = Trim(key);
        for (auto& c : k) c = towupper(c);
        if (!KeyValid(k)) { Message(L"Неверный формат ключа", L"Ключ должен выглядеть так: XXXXX-XXXXX-XXXXX-XXXXX-XXXXX (25 букв и цифр через дефис).", Sev::Warn); return; }
        Confirm(L"Установить ключ и активировать?", L"Ключ " + k.substr(0, 5) + L"-…-" + k.substr(24) + L" будет установлен штатным средством Windows (slmgr), затем запустится активация через интернет.",
                L"Активировать", [k] {
            wstring slmgr = Q(Sys32(L"cscript.exe")) + L" //nologo " + Q(Sys32(L"slmgr.vbs"));
            RunTool(L"Активация Windows", PG_ACTIVATION, { slmgr + L" /ipk " + k, slmgr + L" /ato" }, L"Активация Windows ключом …" + k.substr(24),
                    [](Task& t) { RequestSysInfo(true); (void)t; });
        }, false, Sev::Info);
    }
    float Body(float x, float y, float w) override {
        auto& si = app.si;
        if (!si.ready) return y + Loading(x, y, w, L"Читаю состояние лицензии…");
        bool ok = si.licenseStatus == 1;
        RectF c(x, y, w, 150);
        Card(c);
        RectF ib(x + 24, y + 26, 56, 56);
        GradientH(ib, 14, ok ? P.ok : P.warn, ok ? Blend(P.ok, P.accent2, 0.5f) : P.danger);
        Glyph(ok ? Ico::Check : Ico::Key, ib, 24, Color(255, 255, 255, 255));
        Text(LicenseStatusText(si.licenseStatus), RectF(x + 100, y + 24, w - 130, 30), FontUI(22, 1), ok ? P.ok : P.warn, T_LEFT | T_VCENTER);
        Text(si.osName + L" · " + si.osDisplay, RectF(x + 100, y + 56, w - 130, 20), FontUI(13), P.muted, T_LEFT | T_VCENTER);
        float kx = x + 100;
        KeyValue(kx, y + 90, w * 0.45f, L"Тип лицензии", si.licenseText, 120);
        KeyValue(kx, y + 114, w * 0.45f, L"Ключ (последние)", si.partialKey.empty() ? L"" : L"…-" + si.partialKey, 120);
        KeyValue(x + w * 0.55f, y + 90, w * 0.43f, L"Издание", si.licenseName, 90);
        y += 166;

        if (!si.oemKey.empty()) {
            wstring ok2 = si.oemKey;
            y += Banner(x, y, w, Sev::Info, L"В BIOS ноутбука/ПК зашит ключ Windows: " + ok2,
                        L"Это лицензия, купленная вместе с компьютером. Её можно установить после переустановки Windows той же редакции.",
                        L"Использовать", [this, ok2] { key = ok2; Toast(L"Ключ из BIOS подставлен в поле"); }) + 16;
        }

        y += SectionTitle(x, y, w, L"Активация ключом", L"Только легальные ключи: из коробки, с наклейки, из письма магазина или из BIOS. Активация идёт штатно через серверы Microsoft.");
        TextBox(601, RectF(x, y, std::min(460.0f, w - 260), 42), key, L"XXXXX-XXXXX-XXXXX-XXXXX-XXXXX", 29, true);
        Button(RectF(x + std::min(460.0f, w - 260) + 12, y + 2, 230, 38), L"Установить и активировать", [this] { Activate(); }, Btn::Primary, Ico::Key, !TaskBusy());
        y += 60;
        y += Banner(x, y, w, Sev::Info, L"Если активация не проходит",
                    L"• После замены материнской платы — войдите в учётную запись Microsoft и запустите «Средство устранения неполадок» в параметрах активации.\n"
                    L"• Ключ Pro не подходит к Домашней и наоборот — сначала нужна смена редакции.\n"
                    L"• Код 0xC004F074 — нет связи с сервером, проверьте интернет и время на ПК.") + 16;
        float con = ConsoleBox(x, y, w, 240, PG_ACTIVATION);
        if (con) y += con + 16;
        return y;
    }
};
Page* MakeActivation() { return new ActivationPage; }

static wstring Sha256File(const wstring& path) {
    wstring out;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    HCRYPTPROV prov = 0;
    HCRYPTHASH h = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
            vector<BYTE> buf(1 << 16);
            DWORD r = 0;
            while (ReadFile(f, buf.data(), (DWORD)buf.size(), &r, nullptr) && r) CryptHashData(h, buf.data(), r, 0);
            BYTE d[32];
            DWORD dl = 32;
            if (CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0)) out = HexBytes(d, dl);
            CryptDestroyHash(h);
        }
        CryptReleaseContext(prov, 0);
    }
    CloseHandle(f);
    return out;
}

struct IntegrityRow { wstring rel; wstring expect; int state = -1; };

static vector<IntegrityRow> LoadIntegrity() {
    vector<IntegrityRow> r;
    std::string raw;
    if (!ReadFileBytes(ToolsDir() + L"\\integrity.txt", raw)) return r;
    for (auto& l : Split(Utf8ToW(raw), L'\n')) {
        wstring t = Trim(l);
        if (t.empty() || t[0] == L'#') continue;
        size_t sp = t.find_first_of(L" \t");
        if (sp == wstring::npos) continue;
        IntegrityRow row;
        row.expect = Lower(t.substr(0, sp));
        row.rel = Trim(t.substr(sp));
        r.push_back(row);
    }
    return r;
}

static wstring ToolPath(const wstring& rel) {

    wstring r = rel;
    for (auto& c : r) if (c == L'/') c = L'\\';
    if (IStarts(r, L"tools\\")) r = r.substr(6);
    return ToolsDir() + L"\\" + r;
}

struct SecurityPage : Page {
    vector<IntegrityRow> integ;
    bool checking = false;
    SecurityPage() : Page(PG_SECURITY, L"Защита и удалёнка", L"Антивирус, проверка на вирусы (AVZ), целостность инструментов и удалённая помощь", Ico::Shield) {}
    void Verify() {
        if (checking) return;
        checking = true;
        auto rows = std::make_shared<vector<IntegrityRow>>(LoadIntegrity());
        Async([this, rows]() -> std::function<void()> {
            for (auto& r : *rows) {
                wstring p = ToolPath(r.rel);
                if (!FileExists(p)) { r.state = 2; continue; }
                r.state = Sha256File(p) == r.expect ? 0 : 1;
            }
            return [this, rows] { integ = *rows; checking = false; };
        });
    }
    void OnShow() override { RequestSysInfo(); if (integ.empty()) Verify(); }
    int StateOf(const wstring& file) {
        for (auto& r : integ) if (IContains(r.rel, file)) return r.state;
        return -1;
    }
    void Launch(const wstring& title, const wstring& path, const wstring& file, const wstring& site) {
        if (!FileExists(path)) {
            Message(title + L" не найден", L"Ожидается файл:\n" + path + L"\n\nСкачайте с официального сайта: " + site + L" и положите в папку tools рядом с программой.", Sev::Warn);
            return;
        }
        int st = StateOf(file);
        auto go = [title, path] {
            wstring dir = path.substr(0, path.find_last_of(L'\\'));
            if (ShellOpen(path, L"", dir)) { Toast(L"Запущен " + title); Journal::Add(L"tool", L"Запущен " + title, L""); }
            else Message(L"Не удалось запустить", path, Sev::Warn);
        };
        if (st == 1) Confirm(L"Файл изменён!", title + L": контрольная сумма SHA-256 не совпадает с эталоном. Файл мог быть подменён вирусом на этом ПК. Рекомендуется не запускать и перекачать набор с чистого компьютера.",
                             L"Всё равно запустить", go, true, Sev::Bad);
        else go();
    }
    float Body(float x, float y, float w) override {
        auto& si = app.si;
        if (!si.ready) return y + Loading(x, y, w, L"Проверяю защиту…");

        bool rt = si.defenderRealtime == 1;
        bool other = false;
        for (auto& a : si.antivirus) if (!IContains(a, L"Defender") && a.find(L"(выключен)") == wstring::npos) other = true;
        float cw = (w - 16) / 2;
        RectF c(x, y, cw, 196);
        Card(c);
        RectF ib(x + 22, y + 22, 44, 44);
        GradientH(ib, 12, rt || other ? P.ok : P.danger, rt || other ? P.accent2 : P.warn);
        Glyph(Ico::Shield, ib, 20, Color(255, 255, 255, 255));
        Text(L"Защитник Windows", RectF(x + 80, y + 20, cw - 100, 24), FontUI(15, 1), P.text, T_LEFT | T_VCENTER);
        Text(rt ? L"Защита в реальном времени включена" : other ? L"Работает сторонний антивирус" : L"Защита в реальном времени выключена",
             RectF(x + 80, y + 44, cw - 100, 20), FontUI(13), rt || other ? P.ok : P.danger, T_LEFT | T_VCENTER);
        float ky = y + 82;
        KeyValue(x + 22, ky, cw - 44, L"Базы обновлены", si.defenderSigAge.empty() ? L"нет данных" : si.defenderSigAge, 150); ky += 24;
        KeyValue(x + 22, ky, cw - 44, L"Антивирусы", si.antivirus.empty() ? L"не найдены" : Join(si.antivirus, L", "), 150); ky += 30;
        bool busy = TaskBusy();
        wstring mp = KnownFolder(FOLDERID_ProgramFiles) + L"\\Windows Defender\\MpCmdRun.exe";
        float bx = x + 22;
        Button(RectF(bx, ky, 150, 34), L"Обновить базы", [mp] { RunTool(L"Обновление баз Защитника", PG_SECURITY, { Q(mp) + L" -SignatureUpdate" }, L"Обновлены базы Защитника Windows"); },
               Btn::Secondary, Ico::Download, !busy && FileExists(mp));
        bx += 160;
        Button(RectF(bx, ky, 170, 34), L"Быстрая проверка", [mp] { RunTool(L"Быстрая проверка Защитником", PG_SECURITY, { Q(mp) + L" -Scan -ScanType 1" }, L"Быстрая проверка Защитником Windows"); },
               Btn::Soft, Ico::Search, !busy && FileExists(mp));
        bx += 180;
        Button(RectF(bx, ky, std::max(60.0f, x + cw - 22 - bx), 34), L"Полная", [mp] {
            Confirm(L"Полная проверка Защитником?", L"Проверяются все файлы на всех дисках. Может занять 1–3 часа, компьютер будет работать медленнее.", L"Запустить", [mp] {
                RunTool(L"Полная проверка Защитником", PG_SECURITY, { Q(mp) + L" -Scan -ScanType 2" }, L"Полная проверка Защитником Windows");
            }, false, Sev::Info);
        }, Btn::Secondary, nullptr, !busy && FileExists(mp));

        RectF a(x + cw + 16, y, cw, 196);
        Card(a);
        wstring avz = ToolsDir() + L"\\avz\\avz4\\avz.exe";
        bool hasAvz = FileExists(avz);
        RectF ab(a.X + 22, y + 22, 44, 44);
        GradientH(ab, 12, P.accent, P.accent2);
        Glyph(Ico::Search, ab, 20, Color(255, 255, 255, 255));
        Text(L"AVZ — лечилка от вирусов", RectF(a.X + 80, y + 20, cw - 100, 24), FontUI(15, 1), P.text, T_LEFT | T_VCENTER);
        int st = StateOf(L"avz.exe");
        Text(!hasAvz ? L"Не найден в папке tools\\avz" : st == 0 ? L"Целостность подтверждена (SHA-256)" : st == 1 ? L"ВНИМАНИЕ: файл изменён!" : L"Готов к запуску",
             RectF(a.X + 80, y + 44, cw - 100, 20), FontUI(13), !hasAvz ? P.warn : st == 1 ? P.danger : P.ok, T_LEFT | T_VCENTER);
        Text(L"Утилита Олега Зайцева: ищет трояны, руткиты, RAT, восстанавливает настройки после вирусов. "
             L"В AVZ: «Файл → Стандартные скрипты» или отметьте диски и нажмите «Пуск».",
             RectF(a.X + 22, y + 80, cw - 44, 56), FontUI(12.5f), P.muted, T_WRAP);
        Button(RectF(a.X + 22, y + 142, 180, 34), L"Запустить AVZ", [this, avz] { Launch(L"AVZ", avz, L"avz.exe", L"http://z-oleg.com/secur/avz/"); }, Btn::Primary, Ico::Play);
        y += 212;

        y += SectionTitle(x, y, w, L"Удалённая помощь", L"Подключайтесь к чужому компьютеру только с согласия человека за ним — он сам видит сессию и сообщает свой ID и пароль.");
        wstring rd = ToolsDir() + L"\\rustdesk\\rustdesk.exe";
        float hc = 170;
        int rst = StateOf(L"rustdesk.exe");
        ActionCard(RectF(x, y, cw, hc), Ico::Monitor, L"RustDesk",
                   L"Бесплатный аналог AnyDesk/TeamViewer, работает через интернет. Свой сервер: Настройки → Сеть → ID-сервер." +
                       wstring(FileExists(rd) ? L"" : L" Файл не найден в tools\\rustdesk."),
                   L"Запустить RustDesk", [this, rd] { Launch(L"RustDesk", rd, L"rustdesk.exe", L"https://rustdesk.com/"); }, Btn::Primary, true,
                   rst == 0 ? L"SHA-256 OK" : rst == 1 ? L"изменён!" : L"", rst == 1 ? Sev::Bad : Sev::Ok);
        ActionCard(RectF(x + cw + 16, y, cw, hc), Ico::Remote, L"Быстрая помощь Windows",
                   L"Встроенная в Windows 10/11 удалённая помощь (нужна учётная запись Microsoft у помогающего). Не требует установки.",
                   L"Открыть", [] {
                       wstring qa = Sys32(L"quickassist.exe");
                       if (FileExists(qa)) OpenSystemTool(qa);
                       else OpenSystemTool(L"ms-quick-assist:");
                   }, Btn::Secondary);
        y += hc + 16;
        {
            wstring sd = ToolsDir() + L"\\sysinternals";
            struct S { const wchar_t* t; const wchar_t* f; const wchar_t* d; };
            S tools[] = { { L"Autoruns", L"Autoruns64.exe", L"Всё, что запускается автоматически: службы, задачи, драйверы — где прячутся трояны и RAT." },
                          { L"Process Explorer", L"procexp64.exe", L"Процессы с проверкой подписи и VirusTotal." },
                          { L"TCPView", L"tcpview64.exe", L"Все сетевые соединения в реальном времени." } };
            float tw3 = (w - 32) / 3;
            for (int i = 0; i < 3; ++i) {
                wstring p = sd + L"\\" + tools[i].f;
                bool has = FileExists(p);
                wstring title = tools[i].t;
                ActionCard(RectF(x + i * (tw3 + 16), y, tw3, 160), Ico::Search, title + L" (Sysinternals)",
                           wstring(tools[i].d) + (has ? L"" : L" Не найден в tools\\sysinternals."),
                           has ? L"Запустить" : L"Скачать с сайта Microsoft", [p, has, title] {
                               if (has) { ShellOpen(p, L"/accepteula", p.substr(0, p.find_last_of(L'\\'))); Journal::Add(L"tool", L"Запущен " + title, L""); }
                               else ShellOpen(L"https://learn.microsoft.com/sysinternals/downloads/sysinternals-suite");
                           }, Btn::Secondary);
            }
            y += 160 + 24;
        }

        y += SectionTitle(x, y, w, L"Целостность инструментов", L"Эталонные SHA-256 из tools\\integrity.txt. На заражённом ПК вирус может подменить файлы на флешке — перед запуском сверяем.");
        if (checking) y += Loading(x, y, w, L"Считаю контрольные суммы…");
        else if (integ.empty()) y += Banner(x, y, w, Sev::Info, L"Файл tools\\integrity.txt не найден", L"Добавьте эталонные хэши: строка «<sha256>  tools/путь/к/файлу.exe».") + 12;
        for (auto& r : integ) {
            RectF rr(x, y, w, 44);
            FillRound(rr, 10, P.surface);
            SevIcon(r.state == 0 ? Sev::Ok : r.state == 1 ? Sev::Bad : Sev::Warn, x + 14, y + 10, 24);
            Text(r.rel, RectF(x + 50, y, w * 0.45f, 44), FontMono(12.5f), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            Text(r.state == 0 ? L"OK" : r.state == 1 ? L"ИЗМЕНЁН — не запускать!" : r.state == 2 ? L"файл не найден" : L"…",
                 RectF(x + w * 0.5f, y, w * 0.48f, 44), FontUI(13, 1), r.state == 0 ? P.ok : r.state == 1 ? P.danger : P.warn, T_RIGHT | T_VCENTER);
            y += 50;
        }
        Button(RectF(x, y + 4, 200, 36), L"Проверить заново", [this] { Verify(); }, Btn::Secondary, Ico::Refresh, !checking);
        y += 56;
        float con = ConsoleBox(x, y, w, 220, PG_SECURITY);
        if (con) y += con + 16;
        return y;
    }
};
Page* MakeSecurity() { return new SecurityPage; }
