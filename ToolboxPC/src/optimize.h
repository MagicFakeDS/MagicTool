#pragma once
#include "common.h"

struct CleanCat {
    wstring id, title, desc, warn;
    bool defOn = true;
    bool on = true;
    bool scanned = false;
    uint64_t bytes = 0;
    uint64_t files = 0;
};
vector<CleanCat> CleanCategories();
void     CleanScan(CleanCat& c);
uint64_t CleanRun(const CleanCat& c, const LineFn& log, uint64_t& filesDeleted);

struct StartupItem {
    wstring name, command, target, location, publisher, desc;
    HKEY root = nullptr;
    REGSAM view = 0;
    wstring regPath;
    wstring approvedPath, approvedName;
    wstring file;
    bool enabled = true;
    bool missing = false;
    bool isFolder = false;
};
vector<StartupItem> CollectStartup();
bool StartupSetEnabled(const StartupItem& it, bool on, wstring& err);
bool StartupDelete(const StartupItem& it, wstring& err);

struct SvcDef {
    wstring name, title, desc, warn;
    DWORD target;
    int risk;
};
vector<SvcDef> ServiceList();
bool ServiceOptimize(const SvcDef& d, wstring& err);
bool ServiceRestore(const SvcDef& d, wstring& err);

struct RegVal { HKEY root; wstring path, name; DWORD type; wstring sval; DWORD dval; bool del = false; };
struct Tweak {
    wstring id, title, desc;
    bool risky = false;
    bool win11only = false;
    bool needExplorer = false;
    vector<RegVal> vals;
    wstring cmdOn, cmdOff;
    std::function<int()> state;
};
vector<Tweak> TweakList();
int  TweakState(const Tweak& t);
bool TweakApply(const Tweak& t, wstring& err);
bool TweakRevert(const Tweak& t, wstring& err);
void RestartExplorer();

struct PowerPlan { GUID guid; wstring name; bool active = false; };
vector<PowerPlan> PowerPlans();
bool PowerActivate(const GUID& g, wstring& err);
wstring GuidStr(const GUID& g);
bool AddUltimatePlan(wstring& err);

struct AppxApp { wstring name, full, family, version, title, hint; bool junk = false; bool on = false; bool known = false; };
vector<AppxApp> CollectAppx();
struct Win32App {
    wstring name, publisher, version, uninstall, quiet, date, key, icon, installLoc;
    HKEY keyRoot = nullptr; REGSAM keyView = 0; wstring keyPath;
    uint64_t size = 0;
    bool junk = false; wstring junkWhy;
    bool system = false;
    bool hasUninstall = true;
};
vector<Win32App> CollectWin32Apps(bool all = false);

struct RegRef { HKEY root; REGSAM view; wstring path; wstring display; };
struct DeepScan {
    vector<RegRef> keys;
    vector<wstring> dirs;
};
DeepScan ScanAppTraces(const Win32App& a);
bool  RegExportKey(const RegRef& k, const wstring& file);
bool  RegDeleteRef(const RegRef& k);
bool  DeleteTree(const wstring& dir);
