#include "common.h"
#include <wincrypt.h>
#include <powrprof.h>
#include <iphlpapi.h>
#include <cstdarg>
#include <deque>

wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    if (n < 0) { buf[2047] = 0; n = (int)wcslen(buf); }
    return wstring(buf, n);
}

wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WToUtf8(const wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr);
    return r;
}

wstring Trim(const wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n\x00A0");
    if (a == wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n\x00A0");
    return s.substr(a, b - a + 1);
}

wstring Lower(wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

bool IStarts(const wstring& s, const wstring& p) {
    return s.size() >= p.size() &&
           CompareStringOrdinal(s.c_str(), (int)p.size(), p.c_str(), (int)p.size(), TRUE) == CSTR_EQUAL;
}
bool IContains(const wstring& s, const wstring& part) { return Lower(s).find(Lower(part)) != wstring::npos; }
bool IEquals(const wstring& a, const wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

vector<wstring> Split(const wstring& s, wchar_t sep) {
    vector<wstring> r;
    size_t a = 0;
    while (true) {
        size_t b = s.find(sep, a);
        if (b == wstring::npos) { r.push_back(s.substr(a)); break; }
        r.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return r;
}

wstring Join(const vector<wstring>& v, const wstring& sep) {
    wstring r;
    for (size_t i = 0; i < v.size(); ++i) { if (i) r += sep; r += v[i]; }
    return r;
}

static wstring Num1(double v) {
    wstring s = Fmt(v >= 100 ? L"%.0f" : (v >= 10 ? L"%.1f" : L"%.2f"), v);
    if (s.find(L'.') != wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    for (auto& c : s) if (c == L'.') c = L',';
    return s;
}

wstring HumanSize(uint64_t b) {
    const wchar_t* u[] = { L"Б", L"КБ", L"МБ", L"ГБ", L"ТБ" };
    double v = (double)b;
    int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; ++i; }
    if (i == 0) return Fmt(L"%llu Б", (unsigned long long)b);
    return Num1(v) + L" " + u[i];
}

wstring Plural(int64_t n, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
    int64_t a = n < 0 ? -n : n;
    int64_t m10 = a % 10, m100 = a % 100;
    if (m10 == 1 && m100 != 11) return one;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
    return many;
}

wstring HumanHours(uint64_t h) {
    if (h < 48) return Fmt(L"%llu ", (unsigned long long)h) + Plural((int64_t)h, L"час", L"часа", L"часов");
    uint64_t d = h / 24;
    if (d < 60) return Fmt(L"%llu ", (unsigned long long)d) + Plural((int64_t)d, L"день", L"дня", L"дней");
    uint64_t mo = d / 30;
    if (mo < 24) return Fmt(L"%llu мес.", (unsigned long long)mo);
    wstring y = Fmt(L"%.1f", d / 365.0);
    for (auto& c : y) if (c == L'.') c = L',';
    if (y.size() > 2 && y.substr(y.size() - 2) == L",0") y.resize(y.size() - 2);
    return y + L" г.";
}

wstring HumanDuration(uint64_t s) {
    uint64_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d) return Fmt(L"%llu дн. %llu ч", (unsigned long long)d, (unsigned long long)h);
    if (h) return Fmt(L"%llu ч %llu мин", (unsigned long long)h, (unsigned long long)m);
    if (m) return Fmt(L"%llu мин", (unsigned long long)m);
    return Fmt(L"%llu с", (unsigned long long)s);
}

wstring NowStr(const wchar_t* fmt) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return Fmt(fmt, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

wstring ExpandEnv(const wstring& s) {
    if (s.find(L'%') == wstring::npos) return s;
    wchar_t buf[4096];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 4096);
    return (n && n <= 4096) ? wstring(buf) : s;
}

wstring HexBytes(const void* p, size_t n) {
    static const wchar_t* h = L"0123456789abcdef";
    wstring r;
    auto b = (const uint8_t*)p;
    for (size_t i = 0; i < n; ++i) { r += h[b[i] >> 4]; r += h[b[i] & 15]; }
    return r;
}

vector<uint8_t> UnHex(const wstring& s) {
    vector<uint8_t> r;
    auto v = [](wchar_t c) { return c >= L'a' ? c - L'a' + 10 : (c >= L'A' ? c - L'A' + 10 : c - L'0'); };
    for (size_t i = 0; i + 1 < s.size(); i += 2) r.push_back((uint8_t)(v(s[i]) * 16 + v(s[i + 1])));
    return r;
}

wstring ExePath() {
    wchar_t b[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return b;
}
wstring ExeDir() {
    wstring p = ExePath();
    return p.substr(0, p.find_last_of(L'\\'));
}
bool FileExists(const wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
wstring ToolsDir() {
    wstring d = ExeDir() + L"\\tools";
    if (DirExists(d)) return d;
    wstring up = ExeDir();
    up = up.substr(0, up.find_last_of(L'\\')) + L"\\tools";
    if (DirExists(up)) return up;
    return d;
}
wstring KnownFolder(const KNOWNFOLDERID& id) {
    PWSTR p = nullptr;
    wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}
wstring DataDir() {
    wchar_t ov[MAX_PATH] = {};
    wstring d = GetEnvironmentVariableW(L"TOOLBOXPC_DATA", ov, MAX_PATH) ? wstring(ov)
                                                                         : KnownFolder(FOLDERID_ProgramData) + L"\\ToolboxPC";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
wstring BackupDir() {
    wstring d = DataDir() + L"\\backup";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
bool DirWritable(const wstring& dir) {
    wstring t = dir + L"\\.tbx_write_test";
    HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
wstring SettingsPath() {
    static wstring cached;
    if (!cached.empty()) return cached;
    if (DirWritable(ExeDir())) cached = ExeDir() + L"\\settings.ini";
    else {
        wstring d = KnownFolder(FOLDERID_LocalAppData) + L"\\ToolboxPC";
        CreateDirectoryW(d.c_str(), nullptr);
        cached = d + L"\\settings.ini";
    }
    return cached;
}
wstring ReportsDir() {
    if (DirWritable(ExeDir())) {
        wstring d = ExeDir() + L"\\reports";
        CreateDirectoryW(d.c_str(), nullptr);
        if (DirExists(d)) return d;
    }
    return KnownFolder(FOLDERID_Desktop);
}
bool WriteFileUtf8(const wstring& path, const std::string& data, bool bom) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w;
    if (bom) WriteFile(h, "\xEF\xBB\xBF", 3, &w, nullptr);
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr);
    CloseHandle(h);
    return ok && w == data.size();
}
bool ReadFileBytes(const wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 64LL * 1024 * 1024) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD r = 0;
    BOOL ok = sz.QuadPart == 0 || ReadFile(h, &out[0], (DWORD)sz.QuadPart, &r, nullptr);
    CloseHandle(h);
    out.resize(r);
    return ok;
}
wstring WinDir() {
    wchar_t b[MAX_PATH];
    GetWindowsDirectoryW(b, MAX_PATH);
    return b;
}
wstring SystemDrive() { return WinDir().substr(0, 2); }
wstring Sys32(const wchar_t* exe) {
    wchar_t b[MAX_PATH];
    GetSystemDirectoryW(b, MAX_PATH);
    return wstring(b) + L"\\" + exe;
}
vector<wstring> UserProfiles() {
    vector<wstring> r;
    const wstring base = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";
    for (auto& sid : RegSubKeys(HKEY_LOCAL_MACHINE, base)) {
        if (!IStarts(sid, L"S-1-5-21-") || sid.find(L".bak") != wstring::npos) continue;
        wstring p;
        if (RegGetStr(HKEY_LOCAL_MACHINE, base + L"\\" + sid, L"ProfileImagePath", p)) {
            p = ExpandEnv(p);
            if (DirExists(p)) r.push_back(p);
        }
    }
    if (r.empty()) {
        wstring up = KnownFolder(FOLDERID_Profile);
        if (!up.empty()) r.push_back(up);
    }
    return r;
}

static HKEY OpenKey(HKEY root, const wstring& path, REGSAM sam, REGSAM view, bool create = false) {
    HKEY k = nullptr;
    LONG r = create ? RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, sam | view, nullptr, &k, nullptr)
                    : RegOpenKeyExW(root, path.c_str(), 0, sam | view, &k);
    return r == ERROR_SUCCESS ? k : nullptr;
}

bool RegGetStr(HKEY root, const wstring& path, const wstring& name, wstring& out, REGSAM view) {
    HKEY k = OpenKey(root, path, KEY_READ, view);
    if (!k) return false;
    DWORD type = 0, size = 0;
    bool ok = false;
    if (RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ)) {
        vector<wchar_t> b(size / 2 + 2, 0);
        if (RegQueryValueExW(k, name.c_str(), nullptr, &type, (BYTE*)b.data(), &size) == ERROR_SUCCESS) {
            out = b.data();
            ok = true;
        }
    } else if (type == REG_DWORD) {
        DWORD v = 0; size = 4;
        if (RegQueryValueExW(k, name.c_str(), nullptr, &type, (BYTE*)&v, &size) == ERROR_SUCCESS) {
            out = std::to_wstring(v);
            ok = true;
        }
    }
    RegCloseKey(k);
    return ok;
}

bool RegGetDword(HKEY root, const wstring& path, const wstring& name, DWORD& out, REGSAM view) {
    HKEY k = OpenKey(root, path, KEY_READ, view);
    if (!k) return false;
    DWORD type = 0, v = 0, size = 4;
    bool ok = RegQueryValueExW(k, name.c_str(), nullptr, &type, (BYTE*)&v, &size) == ERROR_SUCCESS && type == REG_DWORD;
    if (ok) out = v;
    RegCloseKey(k);
    return ok;
}

bool RegWriteRaw(HKEY root, const wstring& path, const wstring& name, DWORD type, const void* data, DWORD size, REGSAM view) {
    HKEY k = OpenKey(root, path, KEY_SET_VALUE, view, true);
    if (!k) return false;
    bool ok = RegSetValueExW(k, name.c_str(), 0, type, (const BYTE*)data, size) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}
bool RegSetDword(HKEY root, const wstring& path, const wstring& name, DWORD v, REGSAM view) {
    return RegWriteRaw(root, path, name, REG_DWORD, &v, 4, view);
}
bool RegSetStr(HKEY root, const wstring& path, const wstring& name, const wstring& v, DWORD type, REGSAM view) {
    return RegWriteRaw(root, path, name, type, v.c_str(), (DWORD)((v.size() + 1) * 2), view);
}
bool RegDelValue(HKEY root, const wstring& path, const wstring& name, REGSAM view) {
    HKEY k = OpenKey(root, path, KEY_SET_VALUE, view);
    if (!k) return true;
    LONG r = RegDeleteValueW(k, name.c_str());
    RegCloseKey(k);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}
bool RegKeyExists(HKEY root, const wstring& path, REGSAM view) {
    HKEY k = OpenKey(root, path, KEY_READ, view);
    if (k) RegCloseKey(k);
    return k != nullptr;
}
vector<wstring> RegSubKeys(HKEY root, const wstring& path, REGSAM view) {
    vector<wstring> r;
    HKEY k = OpenKey(root, path, KEY_READ, view);
    if (!k) return r;
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD n = 512;
        if (RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        r.push_back(name);
    }
    RegCloseKey(k);
    return r;
}
vector<wstring> RegValueNames(HKEY root, const wstring& path, REGSAM view) {
    vector<wstring> r;
    HKEY k = OpenKey(root, path, KEY_READ, view);
    if (!k) return r;
    wchar_t name[16384];
    for (DWORD i = 0;; ++i) {
        DWORD n = 16384;
        if (RegEnumValueW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        r.push_back(name);
    }
    RegCloseKey(k);
    return r;
}
HKEY RootFromName(const wstring& n) {
    if (n == L"HKLM") return HKEY_LOCAL_MACHINE;
    if (n == L"HKU") return HKEY_USERS;
    if (n == L"HKCR") return HKEY_CLASSES_ROOT;
    return HKEY_CURRENT_USER;
}
wstring RootName(HKEY h) {
    if (h == HKEY_LOCAL_MACHINE) return L"HKLM";
    if (h == HKEY_USERS) return L"HKU";
    if (h == HKEY_CLASSES_ROOT) return L"HKCR";
    return L"HKCU";
}

RegSnap RegTake(HKEY root, const wstring& path, const wstring& name, REGSAM view) {
    RegSnap s;
    s.root = root; s.path = path; s.name = name; s.view = view;
    HKEY k = OpenKey(root, path, KEY_READ, view);
    if (!k) return s;
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS) {
        s.data.resize(size);
        if (RegQueryValueExW(k, name.c_str(), nullptr, &type, size ? s.data.data() : nullptr, &size) == ERROR_SUCCESS) {
            s.exists = true;
            s.type = type;
            s.data.resize(size);
        }
    }
    RegCloseKey(k);
    return s;
}

bool RegRestore(const RegSnap& s) {
    if (!s.exists) return RegDelValue(s.root, s.path, s.name, s.view);
    return RegWriteRaw(s.root, s.path, s.name, s.type, s.data.data(), (DWORD)s.data.size(), s.view);
}

wstring RegSnapsToStr(const vector<RegSnap>& v) {
    wstring r;
    for (auto& s : v) {
        if (!r.empty()) r += L'\x1E';
        r += RootName(s.root) + L'\x1F' + std::to_wstring((unsigned)s.view) + L'\x1F' + s.path + L'\x1F' + s.name + L'\x1F' +
             (s.exists ? L"1" : L"0") + L'\x1F' + std::to_wstring(s.type) + L'\x1F' + HexBytes(s.data.data(), s.data.size());
    }
    return r;
}
vector<RegSnap> RegSnapsFromStr(const wstring& str) {
    vector<RegSnap> r;
    if (str.empty()) return r;
    for (auto& rec : Split(str, L'\x1E')) {
        auto f = Split(rec, L'\x1F');
        if (f.size() < 7) continue;
        RegSnap s;
        s.root = RootFromName(f[0]);
        s.view = (REGSAM)_wtoi(f[1].c_str());
        s.path = f[2]; s.name = f[3];
        s.exists = f[4] == L"1";
        s.type = (DWORD)_wtoi(f[5].c_str());
        s.data = UnHex(f[6]);
        r.push_back(s);
    }
    return r;
}

bool IsElevated() {
    BOOL res = FALSE;
    HANDLE t = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        TOKEN_ELEVATION e; DWORD sz = sizeof(e);
        if (GetTokenInformation(t, TokenElevation, &e, sizeof(e), &sz)) res = e.TokenIsElevated;
        CloseHandle(t);
    }
    return res;
}

bool ShellOpen(const wstring& file, const wstring& args, const wstring& dir) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = file.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

namespace {
struct Decoder {
    Enc enc;
    int mode = 0;
    std::string raw;
    wstring pending;
    const LineFn& onLine;
    explicit Decoder(Enc e, const LineFn& f) : enc(e), onLine(f) {
        if (e != Enc::Auto) mode = 2;
    }
    void Feed(const char* p, size_t n, bool final) {
        raw.append(p, n);
        if (mode == 0) {
            if (raw.size() < 8 && !final) return;
            size_t zeros = 0, chk = std::min<size_t>(raw.size(), 200);
            for (size_t i = 1; i < chk; i += 2) if (raw[i] == 0) ++zeros;
            mode = (zeros * 4 >= chk / 2 && chk >= 2) ? 1 : 2;
        }
        if (mode == 1) {
            size_t even = raw.size() & ~size_t(1);
            size_t start = 0;
            if (even >= 2 && (uint8_t)raw[0] == 0xFF && (uint8_t)raw[1] == 0xFE) start = 2;
            pending.append((const wchar_t*)(raw.data() + start), (even - start) / 2);
            raw.erase(0, even);
        } else {

            size_t cut = final ? raw.size() : raw.find_last_of("\r\n");
            if (cut == std::string::npos) return;
            if (!final) cut += 1;
            std::string part = raw.substr(0, cut);
            raw.erase(0, cut);
            UINT cp = enc == Enc::Utf8 ? CP_UTF8 : GetOEMCP();
            if (part.size() >= 3 && part.compare(0, 3, "\xEF\xBB\xBF") == 0) { part.erase(0, 3); cp = CP_UTF8; }
            int wn = MultiByteToWideChar(cp, 0, part.data(), (int)part.size(), nullptr, 0);
            wstring w(wn, 0);
            if (wn) MultiByteToWideChar(cp, 0, part.data(), (int)part.size(), &w[0], wn);
            pending += w;
        }
        Emit(final);
    }
    void Emit(bool final) {
        size_t pos = 0;
        while (true) {
            size_t e = pending.find_first_of(L"\r\n", pos);
            if (e == wstring::npos) break;
            wstring line = pending.substr(pos, e - pos);
            if (pending[e] == L'\r') {
                if (e + 1 >= pending.size()) { if (!final) break; Out(line, false); pos = e + 1; continue; }
                if (pending[e + 1] == L'\n') { Out(line, false); pos = e + 2; continue; }
                Out(line, true);
                pos = e + 1;
                continue;
            }
            Out(line, false);
            pos = e + 1;
        }
        pending.erase(0, pos);
        if (final && !pending.empty()) { Out(pending, false); pending.clear(); }
    }
    void Out(wstring line, bool progress) {
        while (!line.empty() && (line.back() == L' ' || line.back() == 0)) line.pop_back();
        line.erase(std::remove(line.begin(), line.end(), L'\0'), line.end());
        if (Trim(line).empty()) return;
        if (onLine) onLine(progress ? L"\r" + line : line);
    }
};
}

int RunCapture(const wstring& cmdline, const LineFn& onLine, Enc enc, std::atomic<bool>* cancel, const wchar_t* cwd) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    wstring cl = cmdline;
    BOOL ok = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        if (onLine) onLine(Fmt(L"Не удалось запустить (ошибка %lu): ", GetLastError()) + cmdline);
        return -1;
    }
    Decoder dec(enc, onLine);
    char buf[8192];
    while (true) {
        if (cancel && cancel->load()) { TerminateProcess(pi.hProcess, 1); break; }
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 40) == WAIT_OBJECT_0) {
                if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            }
            continue;
        }
        DWORD r = 0;
        if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof(buf)), &r, nullptr) || r == 0) break;
        dec.Feed(buf, r, false);
    }
    dec.Feed("", 0, true);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return (int)code;
}

wstring RunOutput(const wstring& cmdline, int* code, Enc enc) {
    wstring out;
    int c = RunCapture(cmdline, [&](const wstring& l) { out += (l[0] == L'\r' ? l.substr(1) : l) + L"\n"; }, enc);
    if (code) *code = c;
    return out;
}

static wstring Base64Utf16(const wstring& s) {
    DWORD n = 0;
    const BYTE* p = (const BYTE*)s.data();
    DWORD sz = (DWORD)(s.size() * 2);
    CryptBinaryToStringW(p, sz, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    wstring r(n, 0);
    CryptBinaryToStringW(p, sz, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &r[0], &n);
    r.resize(n);
    return r;
}

int RunPS(const wstring& script, const LineFn& onLine, std::atomic<bool>* cancel) {
    wstring full = L"$ProgressPreference='SilentlyContinue';$ErrorActionPreference='Continue';"
                   L"[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding $false;\n" + script;
    wstring cmd = L"\"" + Sys32(L"WindowsPowerShell\\v1.0\\powershell.exe") +
                  L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + Base64Utf16(full);
    return RunCapture(cmd, onLine, Enc::Utf8, cancel);
}

wstring PSOutput(const wstring& script, int* code) {
    wstring out;
    int c = RunPS(script, [&](const wstring& l) { out += (l[0] == L'\r' ? l.substr(1) : l) + L"\n"; });
    if (code) *code = c;
    return out;
}

static HWND g_uiWnd = nullptr;
static std::mutex g_uiMx;
static std::deque<std::function<void()>> g_uiQueue;
bool g_syncMode = false;

void SetUIWindow(HWND h) { g_uiWnd = h; }

void PostUI(std::function<void()> fn) {
    if (!fn) return;
    if (g_syncMode) { fn(); return; }
    {
        std::lock_guard<std::mutex> lk(g_uiMx);
        g_uiQueue.push_back(std::move(fn));
    }
    if (g_uiWnd) PostMessageW(g_uiWnd, WM_APP + 1, 0, 0);
}

void DrainUI() {
    while (true) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(g_uiMx);
            if (g_uiQueue.empty()) return;
            fn = std::move(g_uiQueue.front());
            g_uiQueue.pop_front();
        }
        fn();
    }
}

ComInit::ComInit() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
ComInit::~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }

void Async(std::function<std::function<void()>()> work) {
    if (g_syncMode) {
        auto k = work();
        if (k) k();
        return;
    }
    std::thread([work] {
        ComInit com;
        std::function<void()> k;
        try { k = work(); } catch (...) {}
        if (k) PostUI(std::move(k));
    }).detach();
}

namespace Settings {
static void Ensure() {
    wstring p = SettingsPath();
    if (!FileExists(p)) {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; WriteFile(h, "\xFF\xFE", 2, &w, nullptr);
            CloseHandle(h);
        }
    }
}
wstring Get(const wchar_t* key, const wchar_t* def) {
    wchar_t b[1024];
    GetPrivateProfileStringW(L"toolbox", key, def, b, 1024, SettingsPath().c_str());
    return b;
}
void Set(const wchar_t* key, const wstring& v) {
    Ensure();
    WritePrivateProfileStringW(L"toolbox", key, v.c_str(), SettingsPath().c_str());
}
}

SvcState SvcQuery(const wstring& name) {
    SvcState s;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return s;
    SC_HANDLE h = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (h) {
        s.exists = true;
        DWORD need = 0;
        QueryServiceConfigW(h, nullptr, 0, &need);
        vector<BYTE> b(need + 16);
        auto cfg = (QUERY_SERVICE_CONFIGW*)b.data();
        if (QueryServiceConfigW(h, cfg, (DWORD)b.size(), &need)) {
            s.startType = cfg->dwStartType;
            if (cfg->lpDisplayName) s.display = cfg->lpDisplayName;
        }
        SERVICE_DELAYED_AUTO_START_INFO d{};
        if (QueryServiceConfig2W(h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (BYTE*)&d, sizeof(d), &need))
            s.delayed = d.fDelayedAutostart != FALSE;
        SERVICE_STATUS st{};
        if (QueryServiceStatus(h, &st)) s.state = st.dwCurrentState;
        CloseServiceHandle(h);
    }
    CloseServiceHandle(scm);
    return s;
}

bool SvcSetStart(const wstring& name, DWORD startType, bool delayed) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE h = OpenServiceW(scm, name.c_str(), SERVICE_CHANGE_CONFIG);
    bool ok = false;
    if (h) {
        ok = ChangeServiceConfigW(h, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
        if (ok && startType == SERVICE_AUTO_START) {
            SERVICE_DELAYED_AUTO_START_INFO d{ delayed ? TRUE : FALSE };
            ChangeServiceConfig2W(h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &d);
        }
        CloseServiceHandle(h);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool SvcStop(const wstring& name, DWORD waitMs) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE h = OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    bool ok = false;
    if (h) {
        SERVICE_STATUS st{};
        ControlService(h, SERVICE_CONTROL_STOP, &st);
        DWORD t0 = GetTickCount();
        while (QueryServiceStatus(h, &st) && st.dwCurrentState != SERVICE_STOPPED && GetTickCount() - t0 < waitMs) Sleep(200);
        ok = st.dwCurrentState == SERVICE_STOPPED;
        CloseServiceHandle(h);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool SvcStart(const wstring& name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE h = OpenServiceW(scm, name.c_str(), SERVICE_START);
    bool ok = false;
    if (h) {
        ok = StartServiceW(h, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
        CloseServiceHandle(h);
    }
    CloseServiceHandle(scm);
    return ok;
}

wstring SvcStartName(DWORD t, bool delayed) {
    switch (t) {
        case SERVICE_AUTO_START: return delayed ? L"Автоматически (отложенный)" : L"Автоматически";
        case SERVICE_DEMAND_START: return L"Вручную";
        case SERVICE_DISABLED: return L"Отключена";
        case SERVICE_BOOT_START: return L"При загрузке";
        case SERVICE_SYSTEM_START: return L"Системная";
    }
    return L"—";
}

namespace Journal {
static vector<JEntry> g_entries;
static bool g_loaded = false;

static wstring Esc(const wstring& s) {
    wstring r;
    for (wchar_t c : s) {
        if (c == L'\\') r += L"\\\\";
        else if (c == L'\t') r += L"\\t";
        else if (c == L'\n') r += L"\\n";
        else if (c == L'\r') r += L"\\r";
        else r += c;
    }
    return r;
}
static wstring Unesc(const wstring& s) {
    wstring r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            r += n == L't' ? L'\t' : n == L'n' ? L'\n' : n == L'r' ? L'\r' : n;
        } else r += s[i];
    }
    return r;
}
static wstring FilePath() { return DataDir() + L"\\journal.txt"; }

void Load() {
    g_entries.clear();
    g_loaded = true;
    std::string raw;
    if (!ReadFileBytes(FilePath(), raw)) return;
    if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
    for (auto& line : Split(Utf8ToW(raw), L'\n')) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        auto f = Split(line, L'\t');
        if (f.size() < 8) continue;
        JEntry e;
        e.id = _wcstoui64(f[0].c_str(), nullptr, 10);
        e.time = Unesc(f[1]); e.category = Unesc(f[2]); e.title = Unesc(f[3]); e.detail = Unesc(f[4]);
        e.undoKind = Unesc(f[5]); e.undoData = Unesc(f[6]); e.undone = f[7] == L"1";
        g_entries.push_back(e);
    }
}
void Save() {
    wstring out;
    for (auto& e : g_entries)
        out += std::to_wstring(e.id) + L"\t" + Esc(e.time) + L"\t" + Esc(e.category) + L"\t" + Esc(e.title) + L"\t" +
               Esc(e.detail) + L"\t" + Esc(e.undoKind) + L"\t" + Esc(e.undoData) + L"\t" + (e.undone ? L"1" : L"0") + L"\n";

    wstring tmp = FilePath() + L".tmp";
    if (WriteFileUtf8(tmp, WToUtf8(out), true))
        MoveFileExW(tmp.c_str(), FilePath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    else
        DeleteFileW(tmp.c_str());
}
uint64_t Add(const wstring& category, const wstring& title, const wstring& detail, const wstring& undoKind, const wstring& undoData) {
    if (!g_loaded) Load();
    JEntry e;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    e.id = ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000;
    if (!g_entries.empty() && e.id <= g_entries.back().id) e.id = g_entries.back().id + 1;
    SYSTEMTIME t; GetLocalTime(&t);
    e.time = Fmt(L"%02d.%02d.%04d %02d:%02d", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute);
    e.category = category; e.title = title; e.detail = detail;
    e.undoKind = undoKind; e.undoData = undoData;
    e.thisSession = true;
    g_entries.push_back(e);
    Save();
    return e.id;
}
vector<JEntry>& All() { if (!g_loaded) Load(); return g_entries; }
JEntry* Find(uint64_t id) {
    for (auto& e : All()) if (e.id == id) return &e;
    return nullptr;
}
JEntry* LastFor(const wstring& category) {
    auto& v = All();
    for (auto it = v.rbegin(); it != v.rend(); ++it)
        if (it->category == category && !it->undone && !it->undoKind.empty()) return &*it;
    return nullptr;
}

void MarkUndone(uint64_t id) {
    if (JEntry* p = Find(id)) { p->undone = true; Save(); }
}

}

void NotifyProxyChanged() {
    typedef BOOL(WINAPI * SetOptFn)(void*, DWORD, void*, DWORD);
    if (HMODULE wi = LoadLibraryW(L"wininet.dll")) {
        if (auto f = (SetOptFn)GetProcAddress(wi, "InternetSetOptionW")) {
            f(nullptr, 39  , nullptr, 0);
            f(nullptr, 37  , nullptr, 0);
        }
        FreeLibrary(wi);
    }
}

static wstring IfIndexByGuid(const wstring& guid) {
    ULONG sz = 32 * 1024;
    vector<BYTE> buf(sz);
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                    (PIP_ADAPTER_ADDRESSES)buf.data(), &sz);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(sz);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                  (PIP_ADAPTER_ADDRESSES)buf.data(), &sz);
    }
    if (rc != NO_ERROR) return L"";
    for (auto a = (PIP_ADAPTER_ADDRESSES)buf.data(); a; a = a->Next)
        if (IEquals(Utf8ToW(a->AdapterName), guid)) return std::to_wstring(a->IfIndex);
    return L"";
}

namespace Journal {
bool Undo(const JEntry& e, wstring& err) {
    bool ok = true;
    if (e.undoKind == L"reg") {
        for (auto& s : RegSnapsFromStr(e.undoData)) if (!RegRestore(s)) { ok = false; err = L"Не удалось записать " + s.path + L"\\" + s.name; }
        if (e.category == L"proxy") NotifyProxyChanged();
    } else if (e.undoKind == L"svc") {
        for (auto& rec : Split(e.undoData, L'\x1E')) {
            auto f = Split(rec, L'\x1F');
            if (f.size() < 4) continue;
            if (!SvcSetStart(f[0], (DWORD)_wtoi(f[1].c_str()), f[2] == L"1")) { ok = false; err = L"Служба " + f[0] + L": нет доступа"; continue; }
            if (f[3] == L"1") SvcStart(f[0]);
        }
    } else if (e.undoKind == L"power") {
        GUID g;
        if (CLSIDFromString(e.undoData.c_str(), &g) == NOERROR) ok = PowerSetActiveScheme(nullptr, &g) == ERROR_SUCCESS;
        else ok = false;
        if (!ok) err = L"Схема электропитания не найдена";
    } else if (e.undoKind == L"cmd") {
        for (auto& c : Split(e.undoData, L'\x1E')) {
            if (c.empty()) continue;
            int code = 0;
            wstring out = RunOutput(c, &code);
            if (code != 0) { ok = false; err = Trim(out).empty() ? Fmt(L"Команда завершилась с кодом %d:\n", code) + c : Trim(out); }
        }
    } else if (e.undoKind == L"dns") {
        for (auto& rec : Split(e.undoData, L'\x1E')) {
            auto f = Split(rec, L'\x1F');
            if (f.size() < 3) continue;
            wstring idx = f[0];
            if (f.size() >= 4 && !f[3].empty()) {
                idx = IfIndexByGuid(f[3]);
                if (idx.empty()) { ok = false; err = L"Сетевой адаптер не найден (возможно, его драйвер переустановлен). Настройте DNS вручную."; continue; }
            }
            wstring nsh = L"\"" + Sys32(L"netsh.exe") + L"\" interface ipv4 ";
            int code = 0;
            auto run = [&](const wstring& c) {
                wstring out = RunOutput(c, &code);
                if (code != 0) { ok = false; err = Trim(out).empty() ? Fmt(L"netsh: код %d", code) : Trim(out); }
            };
            if (f[1] == L"dhcp") run(nsh + L"set dnsservers name=" + idx + L" source=dhcp");
            else {
                auto sv = Split(f[2], L',');
                for (size_t i = 0; i < sv.size(); ++i) {
                    if (sv[i].empty()) continue;
                    if (i == 0) run(nsh + L"set dnsservers name=" + idx + L" source=static address=" + sv[i] + L" register=primary validate=no");
                    else run(nsh + L"add dnsservers name=" + idx + L" address=" + sv[i] + Fmt(L" index=%d validate=no", (int)i + 1));
                }
            }
        }
        RunOutput(L"\"" + Sys32(L"ipconfig.exe") + L"\" /flushdns");
    } else if (e.undoKind == L"hosts") {
        wstring hosts = Sys32(L"drivers\\etc\\hosts");
        SetFileAttributesW(hosts.c_str(), FILE_ATTRIBUTE_NORMAL);
        ok = CopyFileW(e.undoData.c_str(), hosts.c_str(), FALSE) != FALSE;
        if (!ok) err = L"Не удалось восстановить hosts из " + e.undoData;
    } else if (e.undoKind == L"file") {
        auto f = Split(e.undoData, L'\x1F');
        ok = f.size() >= 2 && MoveFileExW(f[0].c_str(), f[1].c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
        if (!ok) err = L"Не удалось вернуть файл";
    } else if (e.undoKind == L"store") {
        ShellOpen(L"ms-windows-store://pdp/?PFN=" + e.undoData);
        return true;
    } else {
        err = L"Для этого действия откат не предусмотрен";
        return false;
    }
    return ok;
}
}
