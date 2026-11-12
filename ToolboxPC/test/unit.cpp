#include "../src/app.h"
#include "../src/optimize.h"
#include "../src/report.h"

static wstring out;
static int fails = 0;
static void Check(bool ok, const wstring& what) {
    out += (ok ? L"[OK]   " : L"[FAIL] ") + what + L"\n";
    if (!ok) ++fails;
}
static void Log(const wstring& s) { out += L"       " + s + L"\n"; }

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR tok;
    Gdiplus::GdiplusStartup(&tok, &gsi, nullptr);
    g_syncMode = true;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wstring data = wstring(tmp) + L"tbx_unit_data";
    CreateDirectoryW(data.c_str(), nullptr);
    DeleteFileW((data + L"\\journal.txt").c_str());
    SetEnvironmentVariableW(L"TOOLBOXPC_DATA", data.c_str());
    Check(DataDir() == data, L"DataDir переопределяется для тестов: " + DataDir());

    const wstring key = L"Software\\ToolboxPC_UnitTest";
    RegSnap s0 = RegTake(HKEY_CURRENT_USER, key, L"Val");
    Check(!s0.exists, L"тестового значения нет");
    RegSetDword(HKEY_CURRENT_USER, key, L"Val", 5);
    uint64_t id = Journal::Add(L"test", L"Тест\tс табом\nи переводом", L"детали", L"reg", RegSnapsToStr({ s0 }));
    Journal::Load();
    JEntry* e = Journal::Find(id);
    Check(e && e->title == L"Тест\tс табом\nи переводом", L"журнал: сохранение и чтение со спецсимволами");
    wstring err;
    Check(e && Journal::Undo(*e, err), L"откат значения DWORD");
    Check(!RegTake(HKEY_CURRENT_USER, key, L"Val").exists, L"после отката значения снова нет");
    RegSetStr(HKEY_CURRENT_USER, key, L"Str", L"было");
    RegSnap s1 = RegTake(HKEY_CURRENT_USER, key, L"Str");
    RegSetStr(HKEY_CURRENT_USER, key, L"Str", L"стало");
    RegRestore(RegSnapsFromStr(RegSnapsToStr({ s1 }))[0]);
    wstring v;
    RegGetStr(HKEY_CURRENT_USER, key, L"Str", v);
    Check(v == L"было", L"снимок REG_SZ через строку: " + v);
    if (e) Journal::MarkUndone(e->id);
    Journal::Load();
    Check(Journal::Find(id) && Journal::Find(id)->undone, L"отметка «отменено» сохраняется");
    {
        JEntry bad;
        bad.undoKind = L"cmd";
        bad.undoData = L"cmd.exe /c exit 3";
        wstring e2;
        Check(!Journal::Undo(bad, e2), L"неудачная команда отката даёт ошибку: " + e2);
        JEntry dns;
        dns.undoKind = L"dns";
        dns.undoData = L"9999\x1F" L"dhcp\x1F\x1F{00000000-0000-0000-0000-000000000000}";
        Check(!Journal::Undo(dns, e2), L"откат DNS для пропавшего адаптера даёт ошибку (netsh не запускается)");
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    Check(!RegKeyExists(HKEY_CURRENT_USER, key), L"тестовый ключ удалён");

    wstring o1 = RunOutput(L"cmd.exe /c echo Привет-мир");
    Check(o1.find(L"Привет-мир") != wstring::npos, L"cmd (OEM 866): " + Trim(o1));
    wstring o2 = PSOutput(L"'Проверка UTF-8: ёжик'");
    Check(o2.find(L"ёжик") != wstring::npos, L"PowerShell (UTF-8): " + Trim(o2));
    wstring o3 = RunOutput(L"\"" + Sys32(L"sfc.exe") + L"\" /?");
    bool cyr = false;
    for (wchar_t c : o3) if (c >= 0x410 && c <= 0x44F) cyr = true;
    Check(cyr, L"sfc (UTF-16): " + Trim(o3).substr(0, 60));
    wstring o4 = RunOutput(L"\"" + Sys32(L"cscript.exe") + L"\" //nologo \"" + Sys32(L"slmgr.vbs") + L"\" /dli");
    cyr = false;
    for (wchar_t c : o4) if (c >= 0x410 && c <= 0x44F) cyr = true;
    Check(cyr && o4.find(L'\xFFFD') == wstring::npos, L"slmgr /dli: " + Trim(o4).substr(0, 80));
    wstring o5 = RunOutput(L"\"" + Sys32(L"dism.exe") + L"\" /?");
    cyr = false;
    for (wchar_t c : o5) if (c >= 0x410 && c <= 0x44F) cyr = true;
    Check(cyr, L"dism (OEM): " + Trim(o5).substr(0, 60));
    int lines = 0;
    RunCapture(L"cmd.exe /c \"for /l %i in (1,1,300) do @echo строка %i\"", [&](const wstring&) { ++lines; });
    Check(lines == 300, Fmt(L"построчный вывод: %d строк", lines));

    auto cats = CleanCategories();
    for (auto& c : cats) if (c.id == L"usertemp" || c.id == L"browser" || c.id == L"recycle") { CleanScan(c); Log(c.title + L": " + HumanSize(c.bytes)); }
    Check(cats.size() == 11, L"категории очистки");
    auto st = CollectStartup();
    Check(!st.empty(), Fmt(L"автозагрузка: %d записей", (int)st.size()));
    for (auto& s : st) Log((s.enabled ? L"[вкл] " : L"[выкл] ") + s.name + L" → " + s.target + (s.missing ? L" (нет файла)" : L""));
    auto sv = ServiceList();
    Check(sv.size() >= 10, Fmt(L"службы в списке: %d", (int)sv.size()));
    auto tw = TweakList();
    for (auto& t : tw) Log(Fmt(L"твик %-12ls состояние %d", t.id.c_str(), TweakState(t)));
    Check(tw.size() >= 15, Fmt(L"твиков: %d", (int)tw.size()));
    auto pp = PowerPlans();
    Check(!pp.empty(), Fmt(L"схемы питания: %d", (int)pp.size()));
    for (auto& p : pp) Log(p.name + (p.active ? L" (активна)" : L"") + L" " + GuidStr(p.guid));
    auto ax = CollectAppx();
    Check(!ax.empty(), Fmt(L"встроенные приложения: %d", (int)ax.size()));
    for (auto& a : ax) if (!a.known) Log(L"appx: " + a.title + L"  [" + a.name + L"]");
    auto w32 = CollectWin32Apps();
    int junk = 0;
    for (auto& p : w32) if (p.junk) { ++junk; Log(L"мусор: " + p.name + L" — " + p.junkWhy); }
    Check(w32.size() > 5, Fmt(L"программы: %d, подозрительных %d", (int)w32.size(), junk));

    app.si = CollectSysInfo();
    app.devs = CollectProblemDevices();
    app.crashes = CollectCrashes();
    LiveUpdate(app.live, app.si);
    ReportOptions ro;
    ro.master = L"Тестовый мастер";
    ro.note = L"Комментарий <проверка экранирования> & «кавычки»";
    wstring rp = data + L"\\report.html";
    Check(WriteReport(rp, ro), L"отчёт записан: " + rp);
    std::string html;
    ReadFileBytes(rp, html);
    Check(html.size() > 20000 && html.find("&lt;проверка") != std::string::npos, Fmt(L"отчёт: %d байт, экранирование", (int)html.size()));

    out += fails ? Fmt(L"\nПРОВАЛЕНО: %d\n", fails) : L"\nВСЁ ОК\n";
    WriteFileUtf8(L"unit.txt", WToUtf8(out), true);
    return fails;
}
