#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

using std::wstring;
using std::vector;

#define APP_NAME    L"MagicTool"
#define APP_VERSION L"2.0"

wstring Fmt(const wchar_t* fmt, ...);
wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const wstring& s);
wstring Trim(const wstring& s);
wstring Lower(wstring s);
bool    IStarts(const wstring& s, const wstring& prefix);
bool    IContains(const wstring& s, const wstring& part);
bool    IEquals(const wstring& a, const wstring& b);
vector<wstring> Split(const wstring& s, wchar_t sep);
wstring Join(const vector<wstring>& v, const wstring& sep);
wstring HumanSize(uint64_t bytes);
wstring HumanHours(uint64_t hours);
wstring HumanDuration(uint64_t seconds);
wstring NowStr(const wchar_t* fmt = L"%04d-%02d-%02d %02d:%02d");
wstring ExpandEnv(const wstring& s);
wstring HexBytes(const void* p, size_t n);
vector<uint8_t> UnHex(const wstring& s);
wstring Plural(int64_t n, const wchar_t* one, const wchar_t* few, const wchar_t* many);

wstring ExeDir();
wstring ExePath();
wstring ToolsDir();
wstring DataDir();
wstring BackupDir();
wstring SettingsPath();
wstring ReportsDir();
wstring KnownFolder(const KNOWNFOLDERID& id);
bool    FileExists(const wstring& p);
bool    DirExists(const wstring& p);
bool    DirWritable(const wstring& dir);
bool    WriteFileUtf8(const wstring& path, const std::string& data, bool bom = false);
bool    ReadFileBytes(const wstring& path, std::string& out);
vector<wstring> UserProfiles();
wstring SystemDrive();
wstring WinDir();

bool RegGetStr(HKEY root, const wstring& path, const wstring& name, wstring& out, REGSAM view = 0);
bool RegGetDword(HKEY root, const wstring& path, const wstring& name, DWORD& out, REGSAM view = 0);
bool RegSetDword(HKEY root, const wstring& path, const wstring& name, DWORD v, REGSAM view = 0);
bool RegSetStr(HKEY root, const wstring& path, const wstring& name, const wstring& v, DWORD type = REG_SZ, REGSAM view = 0);
bool RegDelValue(HKEY root, const wstring& path, const wstring& name, REGSAM view = 0);
bool RegKeyExists(HKEY root, const wstring& path, REGSAM view = 0);
vector<wstring> RegSubKeys(HKEY root, const wstring& path, REGSAM view = 0);
vector<wstring> RegValueNames(HKEY root, const wstring& path, REGSAM view = 0);
HKEY    RootFromName(const wstring& n);
wstring RootName(HKEY h);

struct RegSnap {
    HKEY root = HKEY_CURRENT_USER;
    REGSAM view = 0;
    wstring path, name;
    bool exists = false;
    DWORD type = 0;
    vector<uint8_t> data;
};
RegSnap RegTake(HKEY root, const wstring& path, const wstring& name, REGSAM view = 0);
bool    RegRestore(const RegSnap& s);
bool    RegWriteRaw(HKEY root, const wstring& path, const wstring& name, DWORD type, const void* data, DWORD size, REGSAM view = 0);
wstring RegSnapsToStr(const vector<RegSnap>& v);
vector<RegSnap> RegSnapsFromStr(const wstring& s);

enum class Enc { Auto, Utf8, Oem };
using LineFn = std::function<void(const wstring&)>;

int     RunCapture(const wstring& cmdline, const LineFn& onLine, Enc enc = Enc::Auto,
                   std::atomic<bool>* cancel = nullptr, const wchar_t* cwd = nullptr);
wstring RunOutput(const wstring& cmdline, int* code = nullptr, Enc enc = Enc::Auto);

int     RunPS(const wstring& script, const LineFn& onLine, std::atomic<bool>* cancel = nullptr);
wstring PSOutput(const wstring& script, int* code = nullptr);
wstring Sys32(const wchar_t* exe);
bool    ShellOpen(const wstring& file, const wstring& args = L"", const wstring& dir = L"");
bool    IsElevated();

void SetUIWindow(HWND h);
void PostUI(std::function<void()> fn);
void DrainUI();
void Async(std::function<std::function<void()>()> work);
extern bool g_syncMode;
struct ComInit { HRESULT hr; ComInit(); ~ComInit(); };

struct JEntry {
    uint64_t id = 0;
    wstring time, category, title, detail;
    wstring undoKind;
    wstring undoData;
    bool undone = false;
    bool thisSession = false;
};
namespace Journal {
    void   Load();
    void   Save();
    uint64_t Add(const wstring& category, const wstring& title, const wstring& detail,
                 const wstring& undoKind = L"", const wstring& undoData = L"");
    vector<JEntry>& All();
    JEntry* Find(uint64_t id);
    JEntry* LastFor(const wstring& category);
    bool   Undo(const JEntry& e, wstring& err);
    void   MarkUndone(uint64_t id);
}

void NotifyProxyChanged();

bool LicenseActivated();
bool LicenseCheckKey(const wstring& key);
void LicenseActivate();

namespace Settings {
    wstring Get(const wchar_t* key, const wchar_t* def = L"");
    void    Set(const wchar_t* key, const wstring& v);
}

struct SvcState { bool exists = false; DWORD startType = 0; bool delayed = false; DWORD state = 0; wstring display; };
SvcState SvcQuery(const wstring& name);
bool     SvcSetStart(const wstring& name, DWORD startType, bool delayed);
bool     SvcStop(const wstring& name, DWORD waitMs = 15000);
bool     SvcStart(const wstring& name);
wstring  SvcStartName(DWORD t, bool delayed);
