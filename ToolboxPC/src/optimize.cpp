#include "optimize.h"
#include <powrprof.h>
#include <shobjidl.h>
#include <shlwapi.h>

struct CleanTarget { wstring dir, mask; int minAgeHours = 0; bool recursive = true; bool removeDirs = true; };

static vector<wstring> ChromiumProfiles(const wstring& userData) {
    vector<wstring> r;
    if (!DirExists(userData)) return r;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((userData + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return r;
    do {
        wstring n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (n == L"Default" || IStarts(n, L"Profile ") || n == L"Guest Profile"))
            r.push_back(userData + L"\\" + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return r;
}

static vector<CleanTarget> Targets(const wstring& id) {
    vector<CleanTarget> t;
    auto profiles = UserProfiles();
    wstring win = WinDir();
    if (id == L"usertemp") {
        for (auto& p : profiles) t.push_back({ p + L"\\AppData\\Local\\Temp", L"*", 24 });
    } else if (id == L"wintemp") {
        t.push_back({ win + L"\\Temp", L"*", 24 });
    } else if (id == L"browser") {
        const wchar_t* chromium[] = { L"Google\\Chrome", L"Microsoft\\Edge", L"Yandex\\YandexBrowser", L"BraveSoftware\\Brave-Browser",
                                      L"Vivaldi", L"Chromium" };
        const wchar_t* sub[] = { L"Cache", L"Code Cache", L"GPUCache", L"DawnCache", L"DawnGraphiteCache", L"DawnWebGPUCache" };
        for (auto& p : profiles) {
            wstring local = p + L"\\AppData\\Local\\";
            for (auto c : chromium) {
                wstring ud = local + c + L"\\User Data";
                for (auto& prof : ChromiumProfiles(ud))
                    for (auto s : sub) t.push_back({ prof + L"\\" + s, L"*", 0 });
                t.push_back({ ud + L"\\ShaderCache", L"*", 0 });
                t.push_back({ ud + L"\\GrShaderCache", L"*", 0 });
            }
            for (auto o : { L"Opera Software\\Opera Stable", L"Opera Software\\Opera GX Stable" }) {
                t.push_back({ local + o + L"\\Cache", L"*", 0 });
                t.push_back({ local + o + L"\\Code Cache", L"*", 0 });
            }
            wstring ff = local + L"Mozilla\\Firefox\\Profiles";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((ff + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
                        t.push_back({ ff + L"\\" + fd.cFileName + L"\\cache2", L"*", 0 });
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    } else if (id == L"wucache") {
        t.push_back({ win + L"\\SoftwareDistribution\\Download", L"*", 0 });
    } else if (id == L"docache") {
        t.push_back({ win + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache", L"*", 0 });
    } else if (id == L"dumps") {
        t.push_back({ win + L"\\Minidump", L"*.dmp", 0, false });
        t.push_back({ win, L"MEMORY.DMP", 0, false, false });
        t.push_back({ win + L"\\LiveKernelReports", L"*", 0 });
        wstring wer = KnownFolder(FOLDERID_ProgramData) + L"\\Microsoft\\Windows\\WER";
        t.push_back({ wer + L"\\ReportArchive", L"*", 0 });
        t.push_back({ wer + L"\\ReportQueue", L"*", 0 });
        t.push_back({ wer + L"\\Temp", L"*", 0 });
        for (auto& p : profiles) {
            t.push_back({ p + L"\\AppData\\Local\\CrashDumps", L"*", 0 });
            t.push_back({ p + L"\\AppData\\Local\\Microsoft\\Windows\\WER", L"*", 0 });
        }
    } else if (id == L"thumbs") {
        for (auto& p : profiles) t.push_back({ p + L"\\AppData\\Local\\Microsoft\\Windows\\Explorer", L"thumbcache_*.db", 0, false, false });
    } else if (id == L"shaders") {
        for (auto& p : profiles) {
            wstring l = p + L"\\AppData\\Local\\";
            for (auto s : { L"D3DSCache", L"NVIDIA\\DXCache", L"NVIDIA\\GLCache", L"AMD\\DxCache", L"AMD\\GLCache", L"AMD\\DxcCache", L"AMD\\VkCache" })
                t.push_back({ l + s, L"*", 0 });
            t.push_back({ p + L"\\AppData\\LocalLow\\NVIDIA\\PerDriverVersion\\DXCache", L"*", 0 });
        }
    } else if (id == L"drvinst") {
        wstring sd = SystemDrive();
        t.push_back({ sd + L"\\NVIDIA", L"*", 0 });
        t.push_back({ sd + L"\\AMD", L"*", 0 });
    }
    return t;
}

vector<CleanCat> CleanCategories() {
    vector<CleanCat> v = {
        { L"usertemp", L"Временные файлы программ", L"Папки Temp всех пользователей. Файлы моложе суток не трогаются — вдруг идёт установка.", L"", true },
        { L"wintemp", L"Временные файлы Windows", L"Папка C:\\Windows\\Temp — остатки установок и обновлений.", L"", true },
        { L"browser", L"Кэш браузеров", L"Chrome, Edge, Яндекс, Opera, Brave, Vivaldi, Firefox. Только кэш: пароли, история, вкладки и вход на сайты сохраняются. Закройте браузеры перед очисткой.", L"", true },
        { L"wucache", L"Кэш Центра обновления", L"Скачанные файлы обновлений Windows (SoftwareDistribution\\Download). Служба обновления будет остановлена на время очистки.", L"", true },
        { L"docache", L"Кэш оптимизации доставки", L"Файлы обновлений, которыми Windows делится с другими ПК.", L"", true },
        { L"drvinst", L"Распакованные установщики драйверов", L"Папки C:\\NVIDIA и C:\\AMD, которые остаются после установки драйверов видеокарты. Часто занимают 1–3 ГБ.", L"", true },
        { L"thumbs", L"Кэш эскизов", L"Миниатюры картинок в Проводнике — создадутся заново. Часть файлов может быть занята Проводником.", L"", true },
        { L"dumps", L"Дампы памяти и отчёты об ошибках", L"Minidump, MEMORY.DMP, отчёты WER.", L"Дампы нужны для поиска причины синих экранов — сначала посмотрите «Драйверы и сбои».", false },
        { L"shaders", L"Кэш шейдеров видеокарты", L"DirectX/NVIDIA/AMD. Иногда лечит артефакты в играх.", L"Первый запуск игр после очистки будет с подтормаживаниями, пока кэш не соберётся заново.", false },
        { L"recycle", L"Корзина", L"Файлы, которые клиент удалил в корзину.", L"Удаление безвозвратно — спросите клиента, нет ли там нужного.", false },
        { L"winold", L"Предыдущая версия Windows (Windows.old)", L"Остаётся после крупного обновления или переустановки поверх. Обычно 10–30 ГБ.", L"После удаления откатиться на прошлую версию Windows будет нельзя. Убедитесь, что из Windows.old не нужно забрать файлы клиента.", false },
    };
    for (auto& c : v) c.on = c.defOn;
    return v;
}

static bool OldEnough(const WIN32_FIND_DATAW& fd, int hours) {
    if (hours <= 0) return true;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG n = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    ULONGLONG w = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
    ULONGLONG c = ((ULONGLONG)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
    ULONGLONG t = std::max(w, c);
    return n > t && (n - t) / 10000000ULL / 3600ULL >= (ULONGLONG)hours;
}

static bool MaskMatch(const wstring& s, const wstring& m) {
    size_t si = 0, mi = 0, star = wstring::npos, mark = 0;
    while (si < s.size()) {
        if (mi < m.size() && (m[mi] == L'?' || towlower(m[mi]) == towlower(s[si]))) { ++si; ++mi; }
        else if (mi < m.size() && m[mi] == L'*') { star = mi++; mark = si; }
        else if (star != wstring::npos) { mi = star + 1; si = ++mark; }
        else return false;
    }
    while (mi < m.size() && m[mi] == L'*') ++mi;
    return mi == m.size();
}

static bool MatchMask(const wstring& name, const wstring& mask) {
    if (mask == L"*") return true;
    return MaskMatch(name, mask);
}

static void Walk(const wstring& dir, const CleanTarget& t, const std::function<void(const wstring&, const WIN32_FIND_DATAW&)>& fn,
                 vector<wstring>* dirs, int depth = 0) {
    if (depth > 40) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        wstring full = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!t.recursive) continue;
            Walk(full, t, fn, dirs, depth + 1);
            if (dirs) dirs->push_back(full);
        } else if (MatchMask(n, t.mask) && OldEnough(fd, t.minAgeHours)) {
            fn(full, fd);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static uint64_t DirSize(const wstring& dir, uint64_t* files) {
    uint64_t total = 0;
    CleanTarget t{ dir, L"*", 0 };
    Walk(dir, t, [&](const wstring&, const WIN32_FIND_DATAW& fd) {
        total += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (files) ++*files;
    }, nullptr);
    return total;
}

void CleanScan(CleanCat& c) {
    c.bytes = 0;
    c.files = 0;
    if (c.id == L"recycle") {
        SHQUERYRBINFO qi{ sizeof(qi) };
        if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &qi))) { c.bytes = (uint64_t)qi.i64Size; c.files = (uint64_t)qi.i64NumItems; }
    } else if (c.id == L"winold") {
        wstring d = SystemDrive() + L"\\Windows.old";
        if (DirExists(d)) c.bytes = DirSize(d, &c.files);
    } else {
        for (auto& t : Targets(c.id)) {
            if (!DirExists(t.dir)) continue;
            Walk(t.dir, t, [&](const wstring&, const WIN32_FIND_DATAW& fd) {
                c.bytes += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                c.files++;
            }, nullptr);
        }
    }
    c.scanned = true;
}

uint64_t CleanRun(const CleanCat& c, const LineFn& log, uint64_t& deleted) {
    uint64_t freed = 0;
    deleted = 0;
    if (c.id == L"recycle") {
        SHQUERYRBINFO qi{ sizeof(qi) };
        SHQueryRecycleBinW(nullptr, &qi);
        if (SUCCEEDED(SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND))) {
            freed = (uint64_t)qi.i64Size;
            deleted = (uint64_t)qi.i64NumItems;
        }
        return freed;
    }
    if (c.id == L"winold") {
        wstring d = SystemDrive() + L"\\Windows.old";
        if (!DirExists(d)) return 0;
        uint64_t before = DirSize(d, nullptr);
        log(L"Получаю права на Windows.old (это может занять несколько минут)…");
        RunCapture(L"\"" + Sys32(L"icacls.exe") + L"\" \"" + d + L"\" /setowner *S-1-5-32-544 /T /C /L /Q", nullptr);
        RunCapture(L"\"" + Sys32(L"icacls.exe") + L"\" \"" + d + L"\" /grant *S-1-5-32-544:F /T /C /L /Q", nullptr);
        log(L"Удаляю Windows.old…");
        RunCapture(L"\"" + Sys32(L"cmd.exe") + L"\" /c rd /s /q \"" + d + L"\"", nullptr);
        uint64_t after = DirExists(d) ? DirSize(d, nullptr) : 0;
        return before > after ? before - after : 0;
    }
    vector<std::pair<wstring, bool>> services;
    if (c.id == L"wucache") services = { { L"wuauserv", false }, { L"bits", false } };
    if (c.id == L"docache") services = { { L"DoSvc", false } };
    for (auto& s : services) {
        s.second = SvcQuery(s.first).state == SERVICE_RUNNING;
        if (s.second) { log(L"Останавливаю службу " + s.first + L"…"); SvcStop(s.first); }
    }
    uint64_t failed = 0;
    for (auto& t : Targets(c.id)) {
        if (!DirExists(t.dir)) continue;
        vector<wstring> dirs;
        Walk(t.dir, t, [&](const wstring& path, const WIN32_FIND_DATAW& fd) {
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(path.c_str())) { freed += sz; ++deleted; }
            else ++failed;
        }, t.removeDirs ? &dirs : nullptr);
        for (auto& d : dirs) RemoveDirectoryW(d.c_str());
    }
    for (auto& s : services) if (s.second) { log(L"Запускаю службу " + s.first + L"…"); SvcStart(s.first); }
    if (failed) log(Fmt(L"  пропущено занятых файлов: %llu", (unsigned long long)failed));
    return freed;
}

static const wstring kApproved = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";

static wstring ExtractTarget(const wstring& cmd) {
    wstring c = Trim(ExpandEnv(cmd));
    if (c.empty()) return L"";
    if (c[0] == L'"') {
        size_t e = c.find(L'"', 1);
        return e == wstring::npos ? c.substr(1) : c.substr(1, e - 1);
    }
    wstring lc = Lower(c);
    size_t p = lc.find(L".exe");
    if (p != wstring::npos) return c.substr(0, p + 4);
    size_t sp = c.find(L' ');
    return sp == wstring::npos ? c : c.substr(0, sp);
}

static void VersionStrings(const wstring& path, wstring& company, wstring& desc) {
    DWORD hnd = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &hnd);
    if (!sz) return;
    vector<BYTE> b(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, b.data())) return;
    struct LANG { WORD l, c; }* tr = nullptr;
    UINT n = 0;
    if (!VerQueryValueW(b.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &n) || n < sizeof(LANG)) return;
    wchar_t* v = nullptr;
    UINT vl = 0;
    if (VerQueryValueW(b.data(), Fmt(L"\\StringFileInfo\\%04x%04x\\CompanyName", tr[0].l, tr[0].c).c_str(), (void**)&v, &vl) && v) company = Trim(v);
    if (VerQueryValueW(b.data(), Fmt(L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].l, tr[0].c).c_str(), (void**)&v, &vl) && v) desc = Trim(v);
}

static bool ApprovedEnabled(HKEY root, const wstring& sub, const wstring& name) {
    RegSnap s = RegTake(root, kApproved + sub, name);
    if (!s.exists || s.data.empty()) return true;
    return (s.data[0] & 1) == 0;
}

static void FillFileInfo(StartupItem& it) {
    if (it.target.empty()) return;
    wstring t = it.target;
    if (t.find(L'\\') == wstring::npos) {
        wchar_t buf[MAX_PATH];
        if (SearchPathW(nullptr, t.c_str(), nullptr, MAX_PATH, buf, nullptr)) t = buf;
    }
    it.target = t;
    it.missing = !FileExists(t);
    if (!it.missing) VersionStrings(t, it.publisher, it.desc);
}

vector<StartupItem> CollectStartup() {
    vector<StartupItem> r;
    struct Loc { HKEY root; REGSAM view; const wchar_t* path; const wchar_t* appr; const wchar_t* label; };
    Loc locs[] = {
        { HKEY_CURRENT_USER, 0, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Run", L"Реестр: текущий пользователь" },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Run", L"Реестр: все пользователи" },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Run32", L"Реестр: все пользователи (32-бит)" },
    };
    for (auto& L : locs) {
        for (auto& name : RegValueNames(L.root, L.path, L.view)) {
            wstring cmd;
            if (!RegGetStr(L.root, L.path, name, cmd, L.view) || Trim(cmd).empty()) continue;
            StartupItem it;
            it.name = name.empty() ? L"(без имени)" : name;
            it.command = cmd;
            it.root = L.root;
            it.view = L.view;
            it.regPath = L.path;
            it.location = L.label;
            it.approvedPath = kApproved + L.appr;
            it.approvedName = name;
            it.enabled = ApprovedEnabled(L.root, L.appr, name);
            it.target = ExtractTarget(cmd);
            FillFileInfo(it);
            r.push_back(it);
        }
    }
    struct Fld { KNOWNFOLDERID id; HKEY root; const wchar_t* label; };
    Fld flds[] = { { FOLDERID_Startup, HKEY_CURRENT_USER, L"Папка «Автозагрузка» пользователя" },
                   { FOLDERID_CommonStartup, HKEY_LOCAL_MACHINE, L"Папка «Автозагрузка» для всех" } };
    for (auto& f : flds) {
        wstring dir = KnownFolder(f.id);
        if (dir.empty()) continue;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            wstring n = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (IEquals(n, L"desktop.ini")) continue;
            StartupItem it;
            it.isFolder = true;
            it.file = dir + L"\\" + n;
            it.name = n.size() > 4 && IEquals(n.substr(n.size() - 4), L".lnk") ? n.substr(0, n.size() - 4) : n;
            it.root = f.root;
            it.location = f.label;
            it.approvedPath = kApproved + L"StartupFolder";
            it.approvedName = n;
            it.enabled = ApprovedEnabled(f.root, L"StartupFolder", n);
            it.target = it.file;
            if (IEquals(n.substr(n.size() >= 4 ? n.size() - 4 : 0), L".lnk")) {
                IShellLinkW* sl = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
                    IPersistFile* pf = nullptr;
                    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
                        if (SUCCEEDED(pf->Load(it.file.c_str(), STGM_READ))) {
                            wchar_t p[MAX_PATH * 2] = {}, a[1024] = {};
                            if (SUCCEEDED(sl->GetPath(p, MAX_PATH * 2, nullptr, SLGP_RAWPATH)) && p[0]) it.target = ExpandEnv(p);
                            sl->GetArguments(a, 1024);
                            it.command = L"\"" + it.target + L"\"" + (a[0] ? L" " + wstring(a) : L"");
                        }
                        pf->Release();
                    }
                    sl->Release();
                }
            }
            if (it.command.empty()) it.command = it.file;
            FillFileInfo(it);
            r.push_back(it);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::stable_sort(r.begin(), r.end(), [](const StartupItem& a, const StartupItem& b) {
        if (a.enabled != b.enabled) return a.enabled;
        return Lower(a.name) < Lower(b.name);
    });
    return r;
}

bool StartupSetEnabled(const StartupItem& it, bool on, wstring& err) {
    RegSnap before = RegTake(it.root, it.approvedPath, it.approvedName);
    BYTE data[12] = {};
    data[0] = on ? 0x02 : 0x03;
    if (!on) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        memcpy(data + 4, &ft, 8);
    }
    if (!RegWriteRaw(it.root, it.approvedPath, it.approvedName, REG_BINARY, data, 12)) {
        err = L"Нет доступа к реестру";
        return false;
    }
    Journal::Add(L"startup", (on ? L"Автозагрузка включена: " : L"Автозагрузка отключена: ") + it.name, it.command,
                 L"reg", RegSnapsToStr({ before }));
    return true;
}

bool StartupDelete(const StartupItem& it, wstring& err) {
    if (it.isFolder) {
        wstring dir = BackupDir() + L"\\startup";
        CreateDirectoryW(dir.c_str(), nullptr);
        wstring dst = dir + L"\\" + NowStr(L"%04d%02d%02d_%02d%02d%02d_") + it.approvedName;
        if (!MoveFileExW(it.file.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED)) { err = L"Не удалось переместить ярлык"; return false; }
        Journal::Add(L"startup", L"Удалено из автозагрузки: " + it.name, L"Ярлык сохранён в " + dst, L"file", dst + L'\x1F' + it.file);
        return true;
    }
    RegSnap v = RegTake(it.root, it.regPath, it.approvedName, it.view);
    RegSnap a = RegTake(it.root, it.approvedPath, it.approvedName);
    if (!RegDelValue(it.root, it.regPath, it.approvedName, it.view)) { err = L"Нет доступа к реестру"; return false; }
    RegDelValue(it.root, it.approvedPath, it.approvedName);
    Journal::Add(L"startup", L"Удалено из автозагрузки: " + it.name, it.command, L"reg", RegSnapsToStr({ v, a }));
    return true;
}

struct SvcDefault { DWORD start; bool delayed; };
static std::map<wstring, SvcDefault> g_svcDefaults;

vector<SvcDef> ServiceList() {
    struct D { const wchar_t* n; const wchar_t* t; const wchar_t* d; const wchar_t* w; DWORD target; int risk; DWORD def; bool delayed; };
    static const D defs[] = {
        { L"DiagTrack", L"Телеметрия Microsoft", L"«Функциональные возможности для подключённых пользователей и телеметрия» — отправка данных о работе ПК в Microsoft.", L"", SERVICE_DISABLED, 0, SERVICE_AUTO_START, false },
        { L"dmwappushservice", L"Маршрутизация push-сообщений WAP", L"Вспомогательная служба телеметрии.", L"", SERVICE_DISABLED, 0, SERVICE_DEMAND_START, false },
        { L"MapsBroker", L"Диспетчер скачанных карт", L"Обновляет офлайн-карты приложения «Карты».", L"", SERVICE_DISABLED, 0, SERVICE_AUTO_START, true },
        { L"RetailDemo", L"Демонстрационный режим магазина", L"Нужна только витринным ПК в магазинах.", L"", SERVICE_DISABLED, 0, SERVICE_DEMAND_START, false },
        { L"Fax", L"Факс", L"Отправка и приём факсов через модем.", L"", SERVICE_DISABLED, 0, SERVICE_DEMAND_START, false },
        { L"WpcMonSvc", L"Родительский контроль", L"Ограничения детских учётных записей Microsoft Family.", L"Не отключайте, если клиент пользуется родительским контролем.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"wisvc", L"Программа предварительной оценки Windows", L"Нужна только участникам Windows Insider.", L"", SERVICE_DISABLED, 0, SERVICE_DEMAND_START, false },
        { L"TrkWks", L"Клиент отслеживания изменившихся связей", L"Следит за ярлыками на файлы в сети. Дома почти никогда не нужна.", L"", SERVICE_DISABLED, 0, SERVICE_AUTO_START, false },
        { L"SysMain", L"SysMain (Superfetch)", L"Заранее загружает программы в память. На SSD почти не даёт выигрыша, но постоянно нагружает диск.", L"На ПК с жёстким диском (HDD) лучше оставить включённой.", SERVICE_DISABLED, 1, SERVICE_AUTO_START, false },
        { L"WSearch", L"Индексатор поиска Windows", L"Индексирует файлы для быстрого поиска. На слабых ПК и HDD сильно грузит диск.", L"Поиск файлов в Пуске и Outlook станет медленнее.", SERVICE_DISABLED, 1, SERVICE_AUTO_START, true },
        { L"WerSvc", L"Отчёты об ошибках Windows", L"Собирает и отправляет отчёты о сбоях программ.", L"Перестанут создаваться отчёты о сбоях для диагностики.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"lfsvc", L"Служба геолокации", L"Определение местоположения для приложений и погоды.", L"Перестанет работать «Найти устройство» и местоположение в приложениях.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"PhoneSvc", L"Телефонная связь", L"Звонки через подключённый телефон.", L"Не отключайте, если клиент звонит через «Связь с телефоном».", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"XblAuthManager", L"Xbox Live: авторизация", L"Вход в Xbox Live для игр.", L"Не отключайте у геймеров с Xbox / Game Pass.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"XblGameSave", L"Xbox Live: сохранения игр", L"Облачные сохранения игр Xbox.", L"Не отключайте у геймеров с Xbox / Game Pass.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"XboxNetApiSvc", L"Xbox Live: сеть", L"Сетевые функции игр Xbox.", L"Не отключайте у геймеров с Xbox / Game Pass.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"XboxGipSvc", L"Xbox: аксессуары", L"Геймпады и аксессуары Xbox.", L"Не отключайте, если есть геймпад Xbox.", SERVICE_DISABLED, 1, SERVICE_DEMAND_START, false },
        { L"Spooler", L"Диспетчер печати", L"Очередь печати.", L"Только если у клиента нет принтера и он не печатает в PDF!", SERVICE_DISABLED, 1, SERVICE_AUTO_START, false },
        { L"edgeupdate", L"Обновление Microsoft Edge", L"Постоянно запущенная служба обновления Edge. В режиме «Вручную» Edge обновится при запуске.", L"", SERVICE_DEMAND_START, 0, SERVICE_AUTO_START, true },
        { L"gupdate", L"Обновление Google", L"Служба обновления Chrome и других программ Google. В режиме «Вручную» обновления продолжат работать.", L"", SERVICE_DEMAND_START, 0, SERVICE_AUTO_START, true },
    };
    vector<SvcDef> r;
    for (auto& d : defs) {
        if (!SvcQuery(d.n).exists) continue;
        r.push_back({ d.n, d.t, d.d, d.w, d.target, d.risk });
        g_svcDefaults[d.n] = { d.def, d.delayed };
    }
    return r;
}

bool ServiceOptimize(const SvcDef& d, wstring& err) {
    SvcState s = SvcQuery(d.name);
    if (!s.exists) { err = L"Служба не найдена"; return false; }
    if (!SvcSetStart(d.name, d.target, false)) { err = L"Нет доступа (нужны права администратора, некоторые службы защищены Windows)"; return false; }
    bool wasRunning = s.state == SERVICE_RUNNING;
    if (d.target == SERVICE_DISABLED && wasRunning) SvcStop(d.name, 8000);
    wstring undo = d.name + L'\x1F' + std::to_wstring(s.startType) + L'\x1F' + (s.delayed ? L"1" : L"0") + L'\x1F' + (wasRunning ? L"1" : L"0");
    Journal::Add(L"svc:" + d.name, L"Служба «" + d.title + L"»: " + SvcStartName(d.target, false),
                 L"Было: " + SvcStartName(s.startType, s.delayed), L"svc", undo);
    return true;
}

bool ServiceRestore(const SvcDef& d, wstring& err) {
    if (JEntry* e = Journal::LastFor(L"svc:" + d.name)) {
        JEntry copy = *e;
        if (!Journal::Undo(copy, err)) return false;
        Journal::MarkUndone(copy.id);
        return true;
    }
    auto it = g_svcDefaults.find(d.name);
    if (it == g_svcDefaults.end()) { err = L"Нет данных для отката"; return false; }
    SvcState s = SvcQuery(d.name);
    if (!SvcSetStart(d.name, it->second.start, it->second.delayed)) { err = L"Нет доступа"; return false; }
    if (it->second.start == SERVICE_AUTO_START) SvcStart(d.name);
    Journal::Add(L"svc-default", L"Служба «" + d.title + L"»: вернул как в Windows по умолчанию",
                 L"Было: " + SvcStartName(s.startType, s.delayed));
    return true;
}

static const GUID kSubVideo = { 0x7516b95f, 0xf776, 0x4464, { 0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99 } };
static const GUID kVideoIdle = { 0x3c0bc021, 0xc8a8, 0x4e07, { 0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e } };
static const GUID kSubSleep = { 0x238c9fa8, 0x0aad, 0x41ed, { 0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20 } };
static const GUID kStandbyIdle = { 0x29f6c1db, 0x86da, 0x48c5, { 0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda } };

static DWORD PowerAc(const GUID& sub, const GUID& set) {
    GUID* act = nullptr;
    DWORD v = (DWORD)-1;
    if (PowerGetActiveScheme(nullptr, &act) == ERROR_SUCCESS) {
        PowerReadACValueIndex(nullptr, act, &sub, &set, &v);
        LocalFree(act);
    }
    return v;
}

static wstring IsoDate(int addDays) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime) + (ULONGLONG)addDays * 864000000000ULL;
    ft.dwLowDateTime = (DWORD)t;
    ft.dwHighDateTime = (DWORD)(t >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    return Fmt(L"%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static RegVal DW(HKEY r, const wstring& p, const wstring& n, DWORD v) { return { r, p, n, REG_DWORD, L"", v }; }
static RegVal SZ(HKEY r, const wstring& p, const wstring& n, const wstring& v) { return { r, p, n, REG_SZ, v, 0 }; }

vector<Tweak> TweakList() {
    const HKEY CU = HKEY_CURRENT_USER, LM = HKEY_LOCAL_MACHINE;
    const wstring adv = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    const wstring cdm = L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
    vector<Tweak> v;
    {
        Tweak t{ L"ext", L"Показывать расширения файлов", L"В Проводнике будут видны .exe, .jpg, .pdf — сложнее подсунуть вирус под видом документа." };
        t.vals = { DW(CU, adv, L"HideFileExt", 0) };
        t.needExplorer = true;
        v.push_back(t);
    }
    {
        Tweak t{ L"hidden", L"Показывать скрытые файлы", L"Удобно мастеру: видны AppData и скрытые папки." };
        t.vals = { DW(CU, adv, L"Hidden", 1) };
        t.needExplorer = true;
        v.push_back(t);
    }
    {
        Tweak t{ L"ads", L"Отключить рекламу и «рекомендации»", L"Убирает рекламу в Пуске, на экране блокировки, советы, автоустановку «предложенных» приложений и напоминания «завершите настройку»." };
        t.vals = { DW(CU, cdm, L"SilentInstalledAppsEnabled", 0), DW(CU, cdm, L"SystemPaneSuggestionsEnabled", 0),
                   DW(CU, cdm, L"SubscribedContent-338388Enabled", 0), DW(CU, cdm, L"SubscribedContent-338389Enabled", 0),
                   DW(CU, cdm, L"SubscribedContent-353694Enabled", 0), DW(CU, cdm, L"SubscribedContent-353696Enabled", 0),
                   DW(CU, cdm, L"SubscribedContent-310093Enabled", 0), DW(CU, cdm, L"SoftLandingEnabled", 0),
                   DW(CU, cdm, L"RotatingLockScreenOverlayEnabled", 0),
                   DW(CU, adv, L"ShowSyncProviderNotifications", 0), DW(CU, adv, L"Start_IrisRecommendations", 0),
                   DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\UserProfileEngagement", L"ScoobeSystemSettingEnabled", 0) };
        v.push_back(t);
    }
    {
        Tweak t{ L"telemetry", L"Минимальная телеметрия", L"Windows отправляет в Microsoft только обязательные диагностические данные, отключён рекламный идентификатор." };
        t.vals = { DW(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 0),
                   DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0),
                   DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy", L"TailoredExperiencesWithDiagnosticDataEnabled", 0) };
        v.push_back(t);
    }
    {
        Tweak t{ L"bing", L"Убрать Bing из поиска в Пуске", L"Поиск в Пуске ищет только на компьютере — быстрее и без лишних результатов из интернета." };
        t.vals = { DW(CU, L"Software\\Policies\\Microsoft\\Windows\\Explorer", L"DisableSearchBoxSuggestions", 1),
                   DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Search", L"BingSearchEnabled", 0) };
        t.needExplorer = true;
        v.push_back(t);
    }
    {
        Tweak t{ L"widgets", L"Отключить виджеты и ленту новостей", L"Убирает виджеты (Windows 11) и «Новости и интересы» (Windows 10) с панели задач — меньше фоновой нагрузки." };
        t.vals = { DW(LM, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0),
                   DW(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Feeds", L"EnableFeeds", 0) };
        t.needExplorer = true;
        v.push_back(t);
    }
    {
        Tweak t{ L"copilot", L"Отключить Copilot", L"Убирает кнопку и боковую панель Copilot." };
        t.vals = { DW(CU, L"Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1),
                   DW(CU, adv, L"ShowCopilotButton", 0) };
        t.needExplorer = true;
        v.push_back(t);
    }
    if (_wtoi([] { wstring b; RegGetStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild", b); return b; }().c_str()) >= 22000) {
        Tweak t{ L"ctxmenu", L"Классическое контекстное меню", L"В Windows 11 правый клик сразу показывает полное меню, без «Показать дополнительные параметры»." };
        wstring key = L"HKCU\\Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}";
        t.cmdOn = L"\"" + Sys32(L"reg.exe") + L"\" add \"" + key + L"\\InprocServer32\" /ve /f";
        t.cmdOff = L"\"" + Sys32(L"reg.exe") + L"\" delete \"" + key + L"\" /f";
        t.state = [] { return RegKeyExists(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32") ? 1 : 0; };
        t.needExplorer = true;
        t.win11only = true;
        v.push_back(t);
    }
    {
        Tweak t{ L"startdelay", L"Без задержки запуска программ при входе", L"Windows не будет ждать ~10 секунд перед запуском программ из автозагрузки." };
        t.vals = { DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 0),
                   DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"WaitForIdleState", 0) };
        v.push_back(t);
    }
    {
        Tweak t{ L"gamedvr", L"Отключить фоновую запись игр (Game DVR)", L"Xbox Game Bar не пишет видео в фоне — плюс несколько FPS в играх." };
        t.vals = { DW(CU, L"System\\GameConfigStore", L"GameDVR_Enabled", 0),
                   DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0),
                   DW(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", L"AllowGameDVR", 0) };
        v.push_back(t);
    }
    {
        Tweak t{ L"sticky", L"Отключить залипание клавиш", L"Пятикратное нажатие Shift больше не вызывает окно «Залипание клавиш» (мешает в играх)." };
        t.vals = { SZ(CU, L"Control Panel\\Accessibility\\StickyKeys", L"Flags", L"506") };
        v.push_back(t);
    }
    {
        Tweak t{ L"mouse", L"Отключить ускорение мыши", L"Курсор движется ровно за рукой — удобно в играх и графике.", true };
        t.vals = { SZ(CU, L"Control Panel\\Mouse", L"MouseSpeed", L"0"), SZ(CU, L"Control Panel\\Mouse", L"MouseThreshold1", L"0"),
                   SZ(CU, L"Control Panel\\Mouse", L"MouseThreshold2", L"0") };
        v.push_back(t);
    }
    {
        Tweak t{ L"fastboot", L"Отключить быстрый запуск", L"«Завершение работы» станет настоящим выключением. Лечит проблемы с драйверами, обновлениями и «вечным» аптаймом. Включение — на пару секунд дольше." };
        t.vals = { DW(LM, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", L"HiberbootEnabled", 0) };
        v.push_back(t);
    }
    {
        Tweak t{ L"hibernate", L"Отключить гибернацию", L"Удаляет файл hiberfil.sys (освобождает несколько ГБ на диске C:). Быстрый запуск тоже отключится.", true };
        t.cmdOn = L"\"" + Sys32(L"powercfg.exe") + L"\" /hibernate off";
        t.cmdOff = L"\"" + Sys32(L"powercfg.exe") + L"\" /hibernate on";
        t.state = [] {
            DWORD v = 1;
            RegGetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power", L"HibernateEnabled", v);
            return v == 0 ? 1 : 0;
        };
        v.push_back(t);
    }
    {
        Tweak t{ L"nosleep", L"Не выключать экран и не засыпать от сети", L"Для стационарного ПК: при питании от розетки экран и сон не отключаются (текущая схема питания)." };
        t.state = [] { return PowerAc(kSubVideo, kVideoIdle) == 0 && PowerAc(kSubSleep, kStandbyIdle) == 0 ? 1 : 0; };
        v.push_back(t);
    }
    {
        Tweak t{ L"bgapps", L"Запретить фоновые приложения", L"Приложения из Microsoft Store не работают в фоне — экономит память и батарею.", true };
        t.vals = { DW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", 1),
                   DW(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsRunInBackground", 2) };
        v.push_back(t);
    }
    {
        Tweak t{ L"wupause", L"Пауза обновлений Windows на 5 недель", L"Обновления не будут скачиваться и ставиться 35 дней. Полезно, если свежее обновление ломает работу.", true };
        const wstring p = L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings";
        wstring now = IsoDate(0), end = IsoDate(35);
        t.vals = { SZ(LM, p, L"PauseUpdatesStartTime", now), SZ(LM, p, L"PauseUpdatesExpiryTime", end),
                   SZ(LM, p, L"PauseFeatureUpdatesStartTime", now), SZ(LM, p, L"PauseFeatureUpdatesEndTime", end),
                   SZ(LM, p, L"PauseQualityUpdatesStartTime", now), SZ(LM, p, L"PauseQualityUpdatesEndTime", end) };
        t.state = [p] {
            wstring e;
            if (!RegGetStr(HKEY_LOCAL_MACHINE, p, L"PauseUpdatesExpiryTime", e) || e.empty()) return 0;
            return e > IsoDate(0) ? 1 : 0;
        };
        v.push_back(t);
    }
    {
        Tweak t{ L"wudrivers", L"Не ставить драйверы через Центр обновления", L"Windows Update перестанет заменять драйверы (например, видеокарты) своими старыми версиями.", true };
        t.vals = { DW(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"ExcludeWUDriversInQualityUpdate", 1) };
        v.push_back(t);
    }
    return v;
}

static bool ValMatches(const RegVal& r) {
    RegSnap s = RegTake(r.root, r.path, r.name);
    if (r.del) return !s.exists;
    if (!s.exists) return false;
    if (r.type == REG_DWORD) return s.type == REG_DWORD && s.data.size() >= 4 && *(DWORD*)s.data.data() == r.dval;
    wstring cur((const wchar_t*)s.data.data(), s.data.size() / 2);
    while (!cur.empty() && cur.back() == 0) cur.pop_back();
    return cur == r.sval;
}

int TweakState(const Tweak& t) {
    if (t.state) return t.state();
    int n = 0;
    for (auto& r : t.vals) n += ValMatches(r) ? 1 : 0;
    if (n == (int)t.vals.size()) return 1;
    return n == 0 ? 0 : -1;
}

static void AfterTweak(const Tweak& t) {
    if (t.id == L"mouse") {
        wstring s; int v[3] = { 6, 10, 1 };
        if (RegGetStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", s)) v[0] = _wtoi(s.c_str());
        if (RegGetStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", s)) v[1] = _wtoi(s.c_str());
        if (RegGetStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", s)) v[2] = _wtoi(s.c_str());
        SystemParametersInfoW(SPI_SETMOUSE, 0, v, SPIF_SENDCHANGE);
    }
    if (t.id == L"sticky") {
        STICKYKEYS sk{ sizeof(sk) };
        SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(sk), &sk, 0);
        wstring s;
        if (RegGetStr(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\StickyKeys", L"Flags", s)) sk.dwFlags = (DWORD)_wtoi(s.c_str());
        SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(sk), &sk, SPIF_SENDCHANGE);
    }
    if (t.needExplorer) SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Policy", SMTO_ABORTIFHUNG, 1000, nullptr);
}

bool TweakApply(const Tweak& t, wstring& err) {
    if (t.id == L"nosleep") {
        GUID* act = nullptr;
        if (PowerGetActiveScheme(nullptr, &act) != ERROR_SUCCESS) { err = L"Не удалось прочитать схему питания"; return false; }
        DWORD vOld = PowerAc(kSubVideo, kVideoIdle), sOld = PowerAc(kSubSleep, kStandbyIdle);
        bool ok = PowerWriteACValueIndex(nullptr, act, &kSubVideo, &kVideoIdle, 0) == ERROR_SUCCESS &&
                  PowerWriteACValueIndex(nullptr, act, &kSubSleep, &kStandbyIdle, 0) == ERROR_SUCCESS;
        PowerSetActiveScheme(nullptr, act);
        wstring g = GuidStr(*act);
        LocalFree(act);
        if (!ok) { err = L"Не удалось изменить схему питания"; return false; }
        wstring pc = L"\"" + Sys32(L"powercfg.exe") + L"\" ";
        wstring undo = pc + L"/setacvalueindex " + g + L" SUB_VIDEO VIDEOIDLE " + std::to_wstring(vOld) + L'\x1E' +
                       pc + L"/setacvalueindex " + g + L" SUB_SLEEP STANDBYIDLE " + std::to_wstring(sOld) + L'\x1E' + pc + L"/setactive " + g;
        if (vOld == (DWORD)-1 || sOld == (DWORD)-1) Journal::Add(L"tweak:" + t.id, L"Твик: " + t.title, L"Прежние значения прочитать не удалось — откат вернёт 10/30 минут");
        else Journal::Add(L"tweak:" + t.id, L"Твик: " + t.title, L"", L"cmd", undo);
        return true;
    }
    if (!t.cmdOn.empty()) {
        int code = 0;
        wstring out = RunOutput(t.cmdOn, &code);
        if (code != 0) { err = Trim(out).empty() ? Fmt(L"Код ошибки %d", code) : Trim(out); return false; }
        Journal::Add(L"tweak:" + t.id, L"Твик: " + t.title, L"", L"cmd", t.cmdOff);
        AfterTweak(t);
        return true;
    }
    vector<RegSnap> snaps;
    for (auto& r : t.vals) snaps.push_back(RegTake(r.root, r.path, r.name));
    for (auto& r : t.vals) {
        bool ok = r.del ? RegDelValue(r.root, r.path, r.name)
                        : r.type == REG_DWORD ? RegSetDword(r.root, r.path, r.name, r.dval) : RegSetStr(r.root, r.path, r.name, r.sval);
        if (!ok) {
            for (auto& s : snaps) RegRestore(s);
            err = L"Нет доступа к " + RootName(r.root) + L"\\" + r.path;
            return false;
        }
    }
    Journal::Add(L"tweak:" + t.id, L"Твик: " + t.title, L"", L"reg", RegSnapsToStr(snaps));
    AfterTweak(t);
    return true;
}

bool TweakRevert(const Tweak& t, wstring& err) {
    if (JEntry* e = Journal::LastFor(L"tweak:" + t.id)) {
        JEntry copy = *e;
        if (!Journal::Undo(copy, err)) return false;
        Journal::MarkUndone(copy.id);
        AfterTweak(t);
        return true;
    }

    if (!t.cmdOff.empty()) {
        int code = 0;
        RunOutput(t.cmdOff, &code);
    } else if (t.id == L"nosleep") {
        wstring pc = L"\"" + Sys32(L"powercfg.exe") + L"\" ";
        RunOutput(pc + L"/change monitor-timeout-ac 10");
        RunOutput(pc + L"/change standby-timeout-ac 30");
    } else if (t.id == L"mouse") {
        RegSetStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"1");
        RegSetStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", L"6");
        RegSetStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", L"10");
    } else if (t.id == L"sticky") {
        RegSetStr(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\StickyKeys", L"Flags", L"510");
    } else if (t.id == L"ext") {
        RegSetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt", 1);
    } else if (t.id == L"hidden") {
        RegSetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Hidden", 2);
    } else if (t.id == L"fastboot") {
        RegSetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", L"HiberbootEnabled", 1);
    } else {
        for (auto& r : t.vals) RegDelValue(r.root, r.path, r.name);
    }
    Journal::Add(L"tweak-default", L"Твик отменён (значения Windows по умолчанию): " + t.title, L"");
    AfterTweak(t);
    return true;
}

void RestartExplorer() {

    vector<DWORD> pids;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD shellPid = 0;
    if (tray) GetWindowThreadProcessId(tray, &shellPid);
    if (shellPid) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, shellPid);
        if (h) { TerminateProcess(h, 1); WaitForSingleObject(h, 3000); CloseHandle(h); }
    }
    for (int i = 0; i < 30; ++i) {
        Sleep(200);
        if (FindWindowW(L"Shell_TrayWnd", nullptr)) return;
    }

    ShellOpen(WinDir() + L"\\explorer.exe");
}

wstring GuidStr(const GUID& g) {
    wchar_t b[64];
    StringFromGUID2(g, b, 64);
    wstring s = b;
    if (s.size() > 2 && s[0] == L'{') s = s.substr(1, s.size() - 2);
    return Lower(s);
}

vector<PowerPlan> PowerPlans() {
    vector<PowerPlan> r;
    GUID* act = nullptr;
    GUID active{};
    if (PowerGetActiveScheme(nullptr, &act) == ERROR_SUCCESS) { active = *act; LocalFree(act); }
    for (ULONG i = 0;; ++i) {
        GUID g;
        DWORD sz = sizeof(g);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, (UCHAR*)&g, &sz) != ERROR_SUCCESS) break;
        PowerPlan p;
        p.guid = g;
        DWORD n = 0;
        PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nullptr, &n);
        vector<BYTE> nb(n + 2, 0);
        if (PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nb.data(), &n) == ERROR_SUCCESS) p.name = (wchar_t*)nb.data();
        p.active = IsEqualGUID(g, active);
        r.push_back(p);
    }
    return r;
}

bool PowerActivate(const GUID& g, wstring& err) {
    GUID* act = nullptr;
    wstring prev;
    if (PowerGetActiveScheme(nullptr, &act) == ERROR_SUCCESS) { prev = L"{" + GuidStr(*act) + L"}"; LocalFree(act); }
    if (PowerSetActiveScheme(nullptr, &g) != ERROR_SUCCESS) { err = L"Windows не разрешила сменить схему"; return false; }
    DWORD n = 0;
    PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nullptr, &n);
    vector<BYTE> nb(n + 2, 0);
    PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nb.data(), &n);
    Journal::Add(L"power", L"Схема электропитания: " + wstring((wchar_t*)nb.data()), L"", L"power", prev);
    return true;
}

bool AddUltimatePlan(wstring& err) {
    int code = 0;
    wstring out = RunOutput(L"\"" + Sys32(L"powercfg.exe") + L"\" -duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61", &code);
    if (code != 0) {
        err = L"На этом компьютере (обычно ноутбуки с Modern Standby) Windows не разрешает добавлять схемы. " + Trim(out);
        return false;
    }
    Journal::Add(L"power-add", L"Добавлена схема «Максимальная производительность»", Trim(out));
    return true;
}

struct AppxKnown { const wchar_t* name; const wchar_t* title; int junk; const wchar_t* hint; };
static const AppxKnown kAppx[] = {
    { L"Microsoft.BingNews", L"Новости", 1, L"" },
    { L"Microsoft.BingWeather", L"Погода", 1, L"" },
    { L"Microsoft.BingSearch", L"Поиск Bing", 1, L"" },
    { L"Microsoft.BingFinance", L"Финансы", 1, L"" },
    { L"Microsoft.BingSports", L"Спорт", 1, L"" },
    { L"Microsoft.News", L"Microsoft Новости", 1, L"" },
    { L"Microsoft.GetHelp", L"Техническая поддержка", 1, L"" },
    { L"Microsoft.Getstarted", L"Советы", 1, L"" },
    { L"Microsoft.MicrosoftSolitaireCollection", L"Microsoft Solitaire Collection", 1, L"" },
    { L"Microsoft.People", L"Люди", 1, L"" },
    { L"Microsoft.MicrosoftOfficeHub", L"Microsoft 365 (реклама Office)", 1, L"Сам Office при этом не удаляется" },
    { L"Microsoft.SkypeApp", L"Skype", 1, L"Skype закрыт Microsoft в 2025 году" },
    { L"Microsoft.WindowsFeedbackHub", L"Центр отзывов", 1, L"" },
    { L"Microsoft.ZuneVideo", L"Кино и ТВ", 1, L"" },
    { L"Microsoft.WindowsMaps", L"Карты", 1, L"" },
    { L"Microsoft.PowerAutomateDesktop", L"Power Automate", 1, L"" },
    { L"Clipchamp.Clipchamp", L"Clipchamp (видеоредактор)", 1, L"" },
    { L"MicrosoftTeams", L"Teams (личный)", 1, L"" },
    { L"MSTeams", L"Microsoft Teams", 0, L"Удаляйте, только если клиент не работает в Teams" },
    { L"Microsoft.549981C3F5F10", L"Cortana", 1, L"" },
    { L"Microsoft.MixedReality.Portal", L"Портал смешанной реальности", 1, L"" },
    { L"Microsoft.Microsoft3DViewer", L"Средство 3D-просмотра", 1, L"" },
    { L"Microsoft.Print3D", L"Print 3D", 1, L"" },
    { L"Microsoft.MSPaint", L"Paint 3D", 1, L"Обычный Paint не затрагивается" },
    { L"Microsoft.Wallet", L"Кошелёк", 1, L"" },
    { L"Microsoft.Messaging", L"Сообщения", 1, L"" },
    { L"Microsoft.OneConnect", L"Мобильные тарифы", 1, L"" },
    { L"Microsoft.Windows.DevHome", L"Dev Home", 1, L"" },
    { L"Microsoft.MicrosoftJournal", L"Microsoft Journal", 1, L"" },
    { L"MicrosoftCorporationII.MicrosoftFamily", L"Семья Microsoft", 1, L"" },
    { L"Microsoft.Copilot", L"Copilot", 1, L"" },
    { L"Microsoft.Todos", L"Microsoft To Do", 0, L"Список дел — спросите клиента" },
    { L"Microsoft.YourPhone", L"Связь с телефоном", 0, L"Спросите, пользуется ли клиент" },
    { L"microsoft.windowscommunicationsapps", L"Почта и Календарь", 0, L"Устаревшее приложение, но может использоваться" },
    { L"Microsoft.OutlookForWindows", L"Outlook (новый)", 0, L"Спросите клиента" },
    { L"Microsoft.ZuneMusic", L"Медиаплеер / Groove", 0, L"В Windows 11 это основной плеер музыки и видео" },
    { L"Microsoft.GamingApp", L"Xbox", 0, L"Нужен для Game Pass" },
    { L"Microsoft.XboxApp", L"Xbox Console Companion", 0, L"" },
    { L"Microsoft.XboxGamingOverlay", L"Xbox Game Bar", 0, L"Оверлей и запись игр (Win+G)" },
    { L"Microsoft.XboxSpeechToTextOverlay", L"Xbox: распознавание речи", 0, L"" },
    { L"Microsoft.Xbox.TCUI", L"Xbox TCUI", 0, L"Нужен для игр Xbox / Game Pass" },
    { L"Microsoft.XboxIdentityProvider", L"Xbox: вход в учётную запись", 0, L"Нужен для игр Xbox / Game Pass" },
    { L"MicrosoftWindows.Client.WebExperience", L"Виджеты Windows", 0, L"Вместо удаления можно отключить виджеты на странице «Твики»" },
    { L"MicrosoftWindows.CrossDevice", L"Связь между устройствами", 0, L"" },
    { L"Microsoft.WindowsSoundRecorder", L"Звукозапись", 0, L"" },
    { L"Microsoft.WindowsAlarms", L"Часы и будильник", 0, L"" },
    { L"Microsoft.MicrosoftStickyNotes", L"Записки", 0, L"" },
    { L"MicrosoftCorporationII.QuickAssist", L"Быстрая помощь", 0, L"Полезно для удалённой помощи" },
};

struct AppxPattern { const wchar_t* part; const wchar_t* title; int junk; };
static const AppxPattern kAppxPat[] = {
    { L"king.com", L"Игры King (Candy Crush и др.)", 1 }, { L"Disney", L"Disney+", 1 }, { L"TikTok", L"TikTok", 1 },
    { L"Facebook", L"Facebook", 1 }, { L"Instagram", L"Instagram", 1 }, { L"LinkedIn", L"LinkedIn", 1 },
    { L"PrimeVideo", L"Amazon Prime Video", 1 }, { L"Twitter", L"Twitter / X", 1 }, { L"McAfee", L"McAfee", 1 },
    { L"Booking", L"Booking.com", 1 }, { L"PicsArt", L"PicsArt", 1 }, { L"Hulu", L"Hulu", 1 },
    { L"AdobePhotoshopExpress", L"Photoshop Express", 1 }, { L"WildTangent", L"WildTangent Games", 1 },
    { L"BubbleWitch", L"Bubble Witch Saga", 1 }, { L"MarchofEmpires", L"March of Empires", 1 }, { L"Spotify", L"Spotify", 0 },
    { L"Netflix", L"Netflix", 0 }, { L"Duolingo", L"Duolingo", 0 }, { L"ExpressVPN", L"ExpressVPN", 1 },
};

static const wchar_t* kAppxHide[] = {
    L"Microsoft.WindowsStore", L"Microsoft.StorePurchaseApp", L"Microsoft.DesktopAppInstaller", L"Microsoft.WindowsCalculator",
    L"Microsoft.Windows.Photos", L"Microsoft.WindowsCamera", L"Microsoft.WindowsNotepad", L"Microsoft.Paint", L"Microsoft.ScreenSketch",
    L"Microsoft.WindowsTerminal", L"Microsoft.SecHealthUI", L"Microsoft.VCLibs", L"Microsoft.UI.Xaml", L"Microsoft.NET.",
    L"Microsoft.WindowsAppRuntime", L"Microsoft.Services.Store", L"Microsoft.HEIF", L"Microsoft.HEVC", L"Microsoft.VP9", L"Microsoft.WebMedia",
    L"Microsoft.WebpImage", L"Microsoft.RawImage", L"Microsoft.AV1", L"Microsoft.MPEG2", L"Microsoft.AVCEncoder", L"Microsoft.DolbyAudio",
    L"Microsoft.Windows.ShellExperienceHost", L"Microsoft.Windows.StartMenuExperienceHost", L"Microsoft.LanguageExperiencePack",
    L"Microsoft.ApplicationCompatibilityEnhancements", L"Microsoft.Winget", L"Microsoft.OneDriveSync", L"MicrosoftWindows.Client.Core",
    L"Microsoft.WindowsAppSDK", L"Microsoft.Edge", L"Microsoft.MicrosoftEdge", L"windows.", L"Windows.", L"MicrosoftWindows.Client.CBS",
    L"Microsoft.AAD.", L"Microsoft.AccountsControl", L"Microsoft.Windows.", L"NcsiUwpApp", L"Microsoft.XboxGameCallableUI",
    L"RealtekSemiconductorCorp", L"NVIDIACorp", L"AdvancedMicroDevicesInc", L"AppUp.Intel", L"DolbyLaboratories", L"WavesAudio",
    L"Microsoft.Office.ActionsServer", L"Microsoft.OneDrive", L"Microsoft.Widgets.", L"MicrosoftWindows.LKG", L"Microsoft.StartExperiencesApp",
    L"MicrosoftWindows.Narrator", L"MicrosoftCorporationII.WinAppRuntime", L"Microsoft.WinAppRuntime", L"Microsoft.GamingServices",
    L"MicrosoftWindows.Ai", L"Microsoft.Windows.Ai", L"Microsoft.MicrosoftEdge", L"MicrosoftWindows.UndockedDevKit", L"Microsoft.Ink.",
};

static wstring AppxDisplayName(const wstring& full) {
    wstring v;
    if (!RegGetStr(HKEY_CLASSES_ROOT, L"Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages\\" + full, L"DisplayName", v)) return L"";
    if (!v.empty() && v[0] == L'@') {
        wchar_t buf[512] = {};
        if (SUCCEEDED(SHLoadIndirectString(v.c_str(), buf, 512, nullptr))) v = buf;
        else v.clear();
    }
    if (IStarts(v, L"ms-resource:")) v.clear();
    return Trim(v);
}

vector<AppxApp> CollectAppx() {
    vector<AppxApp> r;
    wstring out = PSOutput(
        L"try { $p = Get-AppxPackage -AllUsers -ErrorAction Stop } catch { $p = Get-AppxPackage }\n"
        L"$p | Where-Object { -not $_.IsFramework -and -not $_.NonRemovable -and $_.SignatureKind -ne 'System' } | "
        L"Sort-Object Name -Unique | ForEach-Object { 'APP|' + $_.Name + '|' + $_.PackageFullName + '|' + $_.PackageFamilyName + '|' + $_.Version }");
    for (auto& line : Split(out, L'\n')) {
        if (!IStarts(line, L"APP|")) continue;
        auto f = Split(Trim(line), L'|');
        if (f.size() < 5) continue;
        AppxApp a;
        a.name = f[1]; a.full = f[2]; a.family = f[3]; a.version = f[4];
        bool hide = false;
        for (auto h : kAppxHide) if (IStarts(a.name, h)) hide = true;
        if (hide) continue;
        a.title = a.name;
        for (auto& k : kAppx) if (IEquals(a.name, k.name)) { a.title = k.title; a.junk = k.junk != 0; a.hint = k.hint; a.known = true; }
        if (!a.known)
            for (auto& k : kAppxPat) if (IContains(a.name, k.part)) { a.title = k.title; a.junk = k.junk != 0; a.known = true; }
        if (!a.known) {
            wstring dn = AppxDisplayName(a.full);
            if (!dn.empty()) a.title = dn;
            else {
                size_t dot = a.name.find_last_of(L'.');
                if (dot != wstring::npos && dot + 1 < a.name.size() && !iswdigit(a.name[dot + 1])) a.title = a.name.substr(dot + 1);
            }
        }
        a.on = a.junk;
        r.push_back(a);
    }
    std::stable_sort(r.begin(), r.end(), [](const AppxApp& a, const AppxApp& b) {
        if (a.junk != b.junk) return a.junk;
        return Lower(a.title) < Lower(b.title);
    });
    return r;
}

struct JunkWord { const wchar_t* word; const wchar_t* why; };
static const JunkWord kJunk[] = {
    { L"McAfee", L"Пробный антивирус от производителя ноутбука — конфликтует с Защитником и тормозит" },
    { L"Norton", L"Пробный антивирус — после пробного периода только мешает" },
    { L"Avast", L"Бесплатный антивирус с навязчивой рекламой" },
    { L"AVG ", L"Бесплатный антивирус с навязчивой рекламой" },
    { L"Avira", L"Бесплатный антивирус с рекламой" },
    { L"360 Total Security", L"Сомнительный «оптимизатор»-антивирус" },
    { L"Baidu", L"Нежелательная программа" },
    { L"Mail.Ru", L"Часто ставится в нагрузку к другим программам" },
    { L"Агент Mail", L"Часто ставится в нагрузку к другим программам" },
    { L"Амиго", L"Браузер, который ставится в нагрузку" },
    { L"Amigo", L"Браузер, который ставится в нагрузку" },
    { L"Спутник", L"Ставится в нагрузку" },
    { L"Элементы Яндекса", L"Панель, которая ставится в нагрузку" },
    { L"Яндекс.Элементы", L"Панель, которая ставится в нагрузку" },
    { L"Менеджер браузеров", L"Ставится в нагрузку и меняет настройки браузера" },
    { L"Browser Manager", L"Ставится в нагрузку и меняет настройки браузера" },
    { L"Driver Booster", L"«Обновлятор» драйверов — часто ставит неподходящие драйверы" },
    { L"DriverPack", L"Ставит лишние программы вместе с драйверами" },
    { L"Driver Easy", L"«Обновлятор» драйверов с рекламой" },
    { L"DriverMax", L"«Обновлятор» драйверов с рекламой" },
    { L"Reimage", L"Платный «ремонтник» с запугиванием" },
    { L"Restoro", L"Платный «ремонтник» с запугиванием" },
    { L"Advanced SystemCare", L"«Оптимизатор» с рекламой, сам нагружает систему" },
    { L"IObit", L"Программы IObit часто ставятся в нагрузку" },
    { L"WinZip Driver", L"«Обновлятор» драйверов с рекламой" },
    { L"Auslogics", L"«Оптимизатор», часто ставится в нагрузку" },
    { L"PC Accelerate", L"Нежелательная программа" },
    { L"SpeedUpMyPC", L"Нежелательная программа" },
    { L"MyPC Backup", L"Нежелательная программа" },
    { L"ByteFence", L"Нежелательная программа" },
    { L"Segurazo", L"Нежелательная программа (SAntivirus)" },
    { L"SAntivirus", L"Нежелательная программа" },
    { L"Wajam", L"Рекламное ПО" },
    { L"Search Protect", L"Рекламное ПО, меняет поиск" },
    { L"WebCompanion", L"Рекламное ПО, меняет поиск" },
    { L"Web Companion", L"Рекламное ПО, меняет поиск" },
    { L"Hola", L"VPN, который использует ПК как узел сети" },
    { L"Kometa", L"Рекламный браузер" },
    { L"Orbitum", L"Рекламный браузер" },
    { L"Torch", L"Рекламный браузер" },
    { L"Uran", L"Рекламный браузер" },
    { L"Opera Browser Assistant", L"Ставится в нагрузку" },
    { L"CCleaner", L"Часто ставится в нагрузку, предлагает лишнее" },
};

vector<Win32App> CollectWin32Apps(bool all) {
    vector<Win32App> r;
    struct L { HKEY root; REGSAM view; };
    L locs[] = { { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY }, { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY }, { HKEY_CURRENT_USER, 0 } };
    const wstring base = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    std::map<wstring, bool> seen;
    for (auto& l : locs) {
        for (auto& k : RegSubKeys(l.root, base, l.view)) {
            wstring p = base + L"\\" + k;
            Win32App a;
            if (!RegGetStr(l.root, p, L"DisplayName", a.name, l.view) || Trim(a.name).empty()) continue;
            DWORD sc = 0;
            bool sysComp = RegGetDword(l.root, p, L"SystemComponent", sc, l.view) && sc;
            wstring parent, rt;
            bool child = RegGetStr(l.root, p, L"ParentKeyName", parent, l.view) && !parent.empty();
            bool upd = RegGetStr(l.root, p, L"ReleaseType", rt, l.view) && (rt == L"Update" || rt == L"Hotfix" || rt == L"Security Update");
            if (!all && (sysComp || child || upd)) continue;
            bool hasU = RegGetStr(l.root, p, L"UninstallString", a.uninstall, l.view) && !Trim(a.uninstall).empty();
            if (!all && !hasU) continue;
            a.hasUninstall = hasU;
            a.system = sysComp || child || upd || !hasU;
            RegGetStr(l.root, p, L"QuietUninstallString", a.quiet, l.view);
            RegGetStr(l.root, p, L"Publisher", a.publisher, l.view);
            RegGetStr(l.root, p, L"DisplayVersion", a.version, l.view);
            RegGetStr(l.root, p, L"DisplayIcon", a.icon, l.view);
            RegGetStr(l.root, p, L"InstallLocation", a.installLoc, l.view);
            wstring d;
            if (RegGetStr(l.root, p, L"InstallDate", d, l.view) && d.size() == 8) a.date = d.substr(6, 2) + L"." + d.substr(4, 2) + L"." + d.substr(0, 4);
            DWORD es = 0;
            if (RegGetDword(l.root, p, L"EstimatedSize", es, l.view)) a.size = (uint64_t)es * 1024;
            a.keyRoot = l.root; a.keyView = l.view; a.keyPath = p;
            a.key = RootName(l.root) + L"\\" + p;
            a.name = Trim(a.name);
            wstring id = Lower(a.name + L"|" + a.version + L"|" + (a.system ? L"s" : L""));
            if (seen[id]) continue;
            seen[id] = true;
            for (auto& j : kJunk) if (IContains(a.name, j.word) || IContains(a.publisher, j.word)) { a.junk = true; a.junkWhy = j.why; break; }
            r.push_back(a);
        }
    }
    std::stable_sort(r.begin(), r.end(), [](const Win32App& a, const Win32App& b) {
        if (a.junk != b.junk) return a.junk;
        return Lower(a.name) < Lower(b.name);
    });
    return r;
}
