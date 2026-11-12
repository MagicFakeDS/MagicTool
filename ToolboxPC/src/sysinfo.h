#pragma once
#include "common.h"

enum class Sev { Ok = 0, Info = 1, Warn = 2, Bad = 3, Unknown = 4 };

enum PageId {
    PG_OVERVIEW, PG_HARDWARE, PG_DISKS, PG_DEVICES, PG_PROCESSES,
    PG_CLEANUP, PG_STARTUP, PG_SERVICES, PG_TWEAKS, PG_APPS,
    PG_REPAIR, PG_NETWORK, PG_ACTIVATION, PG_SECURITY,
    PG_JOURNAL, PG_COUNT
};

struct SmartAttr {
    int id = 0, value = 0, worst = 0, thresh = 0;
    uint64_t raw = 0;
    wstring name;
    bool critical = false;
};

struct DiskHealth {
    bool available = false;
    wstring source;
    Sev status = Sev::Unknown;
    wstring statusText;
    int healthPct = -1;
    int tempC = -1;
    int64_t powerOnHours = -1;
    int64_t powerCycles = -1;
    double writtenTB = -1;
    int64_t reallocated = -1, pending = -1, uncorrectable = -1, crcErrors = -1, mediaErrors = -1, unsafeShutdowns = -1;
    vector<wstring> notes;
    vector<SmartAttr> attrs;
};

struct VolumeInfo {
    wstring letter, label, fs;
    uint64_t total = 0, free = 0;
    int disk = -1;
    bool system = false;
};

struct DiskInfo {
    int number = -1;
    wstring model, serial, firmware, bus;
    uint64_t size = 0;
    int mediaType = 0;
    int busType = 0;
    bool isSystem = false;
    bool removable = false;
    wstring kind() const;
    DiskHealth health;
    vector<wstring> letters;
};

struct RamModule { wstring slot, maker, part; uint64_t size = 0; int speed = 0; wstring type; };

struct GpuInfo {
    wstring name, driverVer, driverDate;
    uint64_t vram = 0;
    LUID luid{};
    bool hasLuid = false;
    bool integrated = false;
    int tempC = -1;
    int driverAgeDays = -1;
};

struct BatteryInfo {
    bool present = false;
    int chargePct = -1;
    bool charging = false;
    uint64_t designMWh = 0, fullMWh = 0;
    int wearPct = -1;
};

struct SysInfo {
    bool ready = false;
    wstring computerName, userName;
    wstring osName, osDisplay, osBuild, osArch, installDate;
    bool win11 = false;
    uint64_t uptimeSec = 0;
    wstring manufacturer, model, boardMaker, boardModel, biosVer, biosDate;
    bool laptop = false, uefi = false, secureBoot = false;
    wstring tpm;
    wstring cpuName, cpuSocket;
    int cpuCores = 0, cpuThreads = 0, cpuMaxMHz = 0;
    uint64_t ramTotal = 0;
    int ramSlots = 0;
    vector<RamModule> ram;
    vector<GpuInfo> gpus;
    BatteryInfo battery;
    vector<DiskInfo> disks;
    vector<VolumeInfo> volumes;
    int thermalC = -1;

    int licenseStatus = -1;
    wstring licenseText, licenseName, partialKey, oemKey;

    vector<wstring> antivirus;
    int defenderOn = -1, defenderRealtime = -1;
    wstring defenderSigAge;

    bool pendingReboot = false;
    wstring powerPlan;
    GUID powerPlanGuid{};
    int startupCount = -1;
    bool proxyOn = false;
    wstring proxyServer;
    int hostsEntries = 0;
};

SysInfo CollectSysInfo();
void    CollectDisks(SysInfo& si);
wstring LicenseStatusText(int st);

struct ProblemDevice { wstring name, id, cls, problem; int code = 0; };
vector<ProblemDevice> CollectProblemDevices();
wstring ConfigErrorText(int code);

struct CrashEvent { wstring time, kind, detail; int64_t sortKey = 0; };
struct CrashInfo { vector<CrashEvent> events; int bsod30 = 0, unexpected30 = 0, appCrash7 = 0; vector<wstring> dumps; };
CrashInfo CollectCrashes();

struct LiveStats {
    double cpuPct = 0;
    uint64_t ramUsed = 0, ramTotal = 0;
    double ramPct = 0;
    vector<int> gpuTemps;
};
void LiveUpdate(LiveStats& s, const SysInfo& si);
int  GpuTemperature(const LUID& luid);

struct Finding { Sev sev; wstring title, text; int page = -1; };
vector<Finding> Diagnose(const SysInfo& si, const vector<ProblemDevice>& devs, const CrashInfo& crashes, const LiveStats& live);
int  HealthScore(const vector<Finding>& f);
