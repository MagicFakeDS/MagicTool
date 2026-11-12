#include "common.h"
#include "optimize.h"

static bool EqFold(const wstring& a, const wstring& b) {
    return a.size() == b.size() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static wstring RealPath(HKEY root, REGSAM view, wstring path) {
    if (root == HKEY_LOCAL_MACHINE && (view & KEY_WOW64_32KEY)) {
        wstring low = Lower(path);
        if (low.rfind(L"software\\", 0) == 0 && low.rfind(L"software\\wow6432node", 0) != 0)
            path = L"SOFTWARE\\WOW6432Node\\" + path.substr(9);
    }
    return path;
}

static bool KeyExists(HKEY root, const wstring& path) {
    HKEY h;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}

static wstring ExeName(wstring s) {
    s = Trim(s);
    if (s.empty()) return L"";
    if (s[0] == L'"') { size_t e = s.find(L'"', 1); s = (e == wstring::npos) ? s.substr(1) : s.substr(1, e - 1); }
    else { size_t sp = s.find(L' '); if (sp != wstring::npos) s = s.substr(0, sp); }
    size_t slash = s.find_last_of(L"\\/");
    wstring name = slash == wstring::npos ? s : s.substr(slash + 1);
    size_t comma = name.find_last_of(L',');
    if (comma != wstring::npos && IContains(name.substr(comma), L".") == false) name = name.substr(0, comma);
    return Lower(name).size() && IContains(name, L".exe") ? name : (name.empty() ? L"" : name);
}

static wstring ExeDir(wstring s) {
    s = Trim(s);
    if (s.empty()) return L"";
    if (s[0] == L'"') { size_t e = s.find(L'"', 1); s = (e == wstring::npos) ? s.substr(1) : s.substr(1, e - 1); }
    else { size_t sp = s.find(L' '); if (sp != wstring::npos) s = s.substr(0, sp); }
    size_t comma = s.find_last_of(L',');
    size_t slash = s.find_last_of(L"\\/");
    if (comma != wstring::npos && comma > slash) s = s.substr(0, comma);
    slash = s.find_last_of(L"\\/");
    return slash == wstring::npos ? L"" : s.substr(0, slash);
}

static bool IsProtectedDir(const wstring& d) {
    wstring n = Lower(Trim(d));
    while (!n.empty() && (n.back() == L'\\' || n.back() == L'/')) n.pop_back();
    if (n.size() <= 3) return true;
    wstring sd = Lower(SystemDrive());
    static const wchar_t* roots[] = { L"windows", L"program files", L"program files (x86)",
                                      L"programdata", L"users", L"windows\\system32", L"windows\\syswow64" };
    for (auto rt : roots) if (n == sd + L"\\" + rt) return true;

    if (n.rfind(sd + L"\\windows", 0) == 0) return true;
    return false;
}

static bool UnderAppRoot(const wstring& d) {

    wstring n = Lower(d);
    wstring roots[] = {
        Lower(KnownFolder(FOLDERID_ProgramFiles)), Lower(KnownFolder(FOLDERID_ProgramFilesX86)),
        Lower(KnownFolder(FOLDERID_ProgramData)),  Lower(KnownFolder(FOLDERID_LocalAppData)),
        Lower(KnownFolder(FOLDERID_RoamingAppData)),
    };
    for (auto& r : roots) if (!r.empty() && n.rfind(r + L"\\", 0) == 0) return true;
    return false;
}

DeepScan ScanAppTraces(const Win32App& a) {
    DeepScan s;
    auto addKey = [&](HKEY root, REGSAM view, const wstring& path) {
        wstring real = RealPath(root, view, path);
        if (!KeyExists(root, real)) return;
        for (auto& k : s.keys) if (k.root == root && EqFold(k.path, real)) return;
        s.keys.push_back({ root, 0, real, RootName(root) + L"\\" + real });
    };
    auto addDir = [&](wstring d, bool trust) {
        d = Trim(ExpandEnv(d));
        while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
        if (d.empty() || !DirExists(d) || IsProtectedDir(d)) return;
        if (!trust && !UnderAppRoot(d)) return;
        for (auto& x : s.dirs) if (EqFold(x, d)) return;
        s.dirs.push_back(d);
    };

    if (a.keyRoot) addKey(a.keyRoot, a.keyView, a.keyPath);

    addDir(a.installLoc, true);
    addDir(ExeDir(a.icon), false);
    addDir(ExeDir(a.quiet.empty() ? a.uninstall : a.quiet), false);

    wstring name = a.name, pub = Trim(a.publisher);

    struct SR { HKEY root; REGSAM view; const wchar_t* base; };
    SR softs[] = {
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"SOFTWARE" },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"SOFTWARE\\WOW6432Node" },
        { HKEY_CURRENT_USER,  0,               L"SOFTWARE" },
    };
    for (auto& sr : softs) {
        for (auto& sub : RegSubKeys(sr.root, sr.base, sr.view))
            if (EqFold(sub, name)) addKey(sr.root, sr.view, wstring(sr.base) + L"\\" + sub);
        if (!pub.empty()) {
            wstring pbase = wstring(sr.base) + L"\\" + pub;
            for (auto& sub : RegSubKeys(sr.root, pbase, sr.view))
                if (EqFold(sub, name)) addKey(sr.root, sr.view, pbase + L"\\" + sub);
        }
    }

    wstring exe = ExeName(a.icon.empty() ? (a.quiet.empty() ? a.uninstall : a.quiet) : a.icon);
    if (!exe.empty() && IContains(exe, L".exe")) {
        addKey(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + exe);
        addKey(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + exe);
    }

    wstring starts[] = { KnownFolder(FOLDERID_CommonPrograms), KnownFolder(FOLDERID_Programs) };
    for (auto& st : starts) {
        if (st.empty()) continue;
        addDir(st + L"\\" + name, true);
        if (!pub.empty()) addDir(st + L"\\" + pub, true);
    }
    wstring roots[] = { KnownFolder(FOLDERID_ProgramFiles), KnownFolder(FOLDERID_ProgramFilesX86),
                        KnownFolder(FOLDERID_ProgramData), KnownFolder(FOLDERID_LocalAppData),
                        KnownFolder(FOLDERID_RoamingAppData) };
    for (auto& rt : roots) {
        if (rt.empty()) continue;
        addDir(rt + L"\\" + name, true);
        if (!pub.empty()) addDir(rt + L"\\" + pub + L"\\" + name, true);
    }
    return s;
}

bool RegExportKey(const RegRef& k, const wstring& file) {
    int code = 0;
    RunOutput(L"\"" + Sys32(L"reg.exe") + L"\" export \"" + k.display + L"\" \"" + file + L"\" /y", &code);
    return code == 0 || FileExists(file);
}

bool RegDeleteRef(const RegRef& k) {
    LSTATUS s = RegDeleteTreeW(k.root, k.path.c_str());
    return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
}

bool DeleteTree(const wstring& dir) {
    if (dir.empty() || !DirExists(dir)) return true;
    auto rd = [&] { RunCapture(L"\"" + Sys32(L"cmd.exe") + L"\" /c rd /s /q \"" + dir + L"\"", nullptr); };
    rd();
    if (!DirExists(dir)) return true;

    RunCapture(L"\"" + Sys32(L"takeown.exe") + L"\" /f \"" + dir + L"\" /r /d y", nullptr);
    RunCapture(L"\"" + Sys32(L"icacls.exe") + L"\" \"" + dir + L"\" /grant *S-1-5-32-544:F /t /c /q", nullptr);
    rd();
    return !DirExists(dir);
}
