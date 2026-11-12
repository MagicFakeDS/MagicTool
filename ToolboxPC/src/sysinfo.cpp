#include "sysinfo.h"
#include "wmi.h"
#include <winioctl.h>
#include <ntddscsi.h>
#include <dxgi.h>
#include <powrprof.h>
#include <winevt.h>
#include <cstddef>

static void CollectOS(SysInfo& si) {
    wchar_t buf[256]; DWORD n = 256;
    if (GetComputerNameExW(ComputerNameDnsHostname, buf, &n)) si.computerName = buf;
    n = 256;
    if (GetUserNameW(buf, &n)) si.userName = buf;

    const wstring cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    wstring prod, disp, build, edition;
    DWORD ubr = 0, inst = 0;
    RegGetStr(HKEY_LOCAL_MACHINE, cv, L"ProductName", prod);
    if (!RegGetStr(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion", disp)) RegGetStr(HKEY_LOCAL_MACHINE, cv, L"ReleaseId", disp);
    RegGetStr(HKEY_LOCAL_MACHINE, cv, L"CurrentBuild", build);
    RegGetDword(HKEY_LOCAL_MACHINE, cv, L"UBR", ubr);
    int b = _wtoi(build.c_str());
    si.win11 = b >= 22000;

    if (si.win11 && prod.find(L"Windows 10") != wstring::npos) prod.replace(prod.find(L"Windows 10"), 10, L"Windows 11");
    si.osName = prod;
    si.osDisplay = disp;
    si.osBuild = build + (ubr ? Fmt(L".%lu", ubr) : L"");
    SYSTEM_INFO sys; GetNativeSystemInfo(&sys);
    si.osArch = sys.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"64-разрядная" :
                sys.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? L"ARM64" : L"32-разрядная";
    if (RegGetDword(HKEY_LOCAL_MACHINE, cv, L"InstallDate", inst) && inst) {
        LONGLONG ll = Int32x32To64(inst, 10000000) + 116444736000000000LL;
        FILETIME ft{ (DWORD)ll, (DWORD)(ll >> 32) }, lft;
        SYSTEMTIME st;
        FileTimeToLocalFileTime(&ft, &lft);
        FileTimeToSystemTime(&lft, &st);
        si.installDate = Fmt(L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
    }
    si.uptimeSec = GetTickCount64() / 1000;

    FIRMWARE_TYPE ft = FirmwareTypeUnknown;
    GetFirmwareType(&ft);
    si.uefi = ft == FirmwareTypeUefi;
    DWORD sb = 0;
    si.secureBoot = RegGetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", L"UEFISecureBootEnabled", sb) && sb;

    si.tpm = L"неизвестно";
    if (HMODULE tbs = LoadLibraryW(L"tbs.dll")) {
        struct TPMINFO { UINT32 structVersion, tpmVersion, tpmInterfaceType, tpmImpRevision; } ti{ 1, 0, 0, 0 };
        typedef UINT32(WINAPI * GetInfoFn)(UINT32, PVOID);
        auto fn = (GetInfoFn)GetProcAddress(tbs, "Tbsi_GetDeviceInfo");
        if (fn) {
            UINT32 r = fn(sizeof(ti), &ti);
            if (r == 0) si.tpm = ti.tpmVersion == 2 ? L"2.0" : ti.tpmVersion == 1 ? L"1.2" : L"есть";
            else si.tpm = L"нет";
        }
        FreeLibrary(tbs);
    }

    si.pendingReboot =
        RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending") ||
        RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");

    GUID* g = nullptr;
    if (PowerGetActiveScheme(nullptr, &g) == ERROR_SUCCESS && g) {
        si.powerPlanGuid = *g;
        DWORD sz = 0;
        PowerReadFriendlyName(nullptr, g, nullptr, nullptr, nullptr, &sz);
        vector<BYTE> nb(sz + 2, 0);
        if (PowerReadFriendlyName(nullptr, g, nullptr, nullptr, nb.data(), &sz) == ERROR_SUCCESS) si.powerPlan = (wchar_t*)nb.data();
        LocalFree(g);
    }

    const wstring is = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
    DWORD pe = 0;
    wstring ps, pac;
    RegGetDword(HKEY_CURRENT_USER, is, L"ProxyEnable", pe);
    RegGetStr(HKEY_CURRENT_USER, is, L"ProxyServer", ps);
    RegGetStr(HKEY_CURRENT_USER, is, L"AutoConfigURL", pac);
    si.proxyOn = pe != 0 || !pac.empty();
    si.proxyServer = !pac.empty() ? pac : ps;

    std::string hosts;
    if (ReadFileBytes(Sys32(L"drivers\\etc\\hosts"), hosts)) {
        for (auto& l : Split(Utf8ToW(hosts), L'\n')) {
            wstring t = Trim(l);
            if (t.empty() || t[0] == L'#') continue;
            if (IContains(t, L"localhost")) continue;
            si.hostsEntries++;
        }
    }
}

static void CollectHardware(SysInfo& si) {
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT Manufacturer, Model, PCSystemType FROM Win32_ComputerSystem")) {
        si.manufacturer = WStr(r, L"Manufacturer");
        si.model = WStr(r, L"Model");
        si.laptop = WNum(r, L"PCSystemType") == 2;
    }
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT Manufacturer, Product FROM Win32_BaseBoard")) {
        si.boardMaker = WStr(r, L"Manufacturer");
        si.boardModel = WStr(r, L"Product");
    }
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT SMBIOSBIOSVersion, ReleaseDate FROM Win32_BIOS")) {
        si.biosVer = WStr(r, L"SMBIOSBIOSVersion");
        si.biosDate = WmiDate(WStr(r, L"ReleaseDate"));
    }
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT ChassisTypes FROM Win32_SystemEnclosure")) {
        auto it = r.find(L"ChassisTypes");
        if (it != r.end())
            for (double c : it->second.arr)
                if (c == 8 || c == 9 || c == 10 || c == 14 || c == 30 || c == 31 || c == 32) si.laptop = true;
    }

    RegGetStr(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", si.cpuName);
    si.cpuName = Trim(si.cpuName);
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    vector<BYTE> lpi(len);
    if (len && GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)lpi.data(), &len)) {
        for (DWORD off = 0; off < len;) {
            auto p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(lpi.data() + off);
            si.cpuCores++;
            off += p->Size;
        }
    }
    si.cpuThreads = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT MaxClockSpeed, SocketDesignation FROM Win32_Processor")) {
        si.cpuMaxMHz = (int)WNum(r, L"MaxClockSpeed");
        si.cpuSocket = WStr(r, L"SocketDesignation");
        break;
    }

    MEMORYSTATUSEX ms{ sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    ULONGLONG kb = 0;
    si.ramTotal = GetPhysicallyInstalledSystemMemory(&kb) && kb ? kb * 1024 : ms.ullTotalPhys;
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT MemoryDevices FROM Win32_PhysicalMemoryArray"))
        si.ramSlots += (int)WNum(r, L"MemoryDevices");
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT Capacity, ConfiguredClockSpeed, Speed, Manufacturer, PartNumber, DeviceLocator, SMBIOSMemoryType FROM Win32_PhysicalMemory")) {
        RamModule m;
        m.size = (uint64_t)WNum(r, L"Capacity");
        m.speed = (int)WNum(r, L"ConfiguredClockSpeed");
        if (!m.speed) m.speed = (int)WNum(r, L"Speed");
        m.maker = WStr(r, L"Manufacturer");
        m.part = WStr(r, L"PartNumber");
        m.slot = WStr(r, L"DeviceLocator");
        int t = (int)WNum(r, L"SMBIOSMemoryType");
        m.type = t == 34 ? L"DDR5" : t == 26 ? L"DDR4" : t == 24 ? L"DDR3" : t == 35 ? L"LPDDR5" : t == 30 ? L"LPDDR4" : t == 29 ? L"LPDDR3" : L"";
        if (m.maker == L"Unknown" || m.maker.find(L"0000") == 0) m.maker.clear();
        si.ram.push_back(m);
    }

    std::map<wstring, std::pair<wstring, wstring>> drv;
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT Name, DriverVersion, DriverDate FROM Win32_VideoController"))
        drv[WStr(r, L"Name")] = { WStr(r, L"DriverVersion"), WStr(r, L"DriverDate") };
    if (HMODULE dx = LoadLibraryW(L"dxgi.dll")) {
        typedef HRESULT(WINAPI * CreateFn)(REFIID, void**);
        auto create = (CreateFn)GetProcAddress(dx, "CreateDXGIFactory1");
        IDXGIFactory1* f = nullptr;
        if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), (void**)&f))) {
            IDXGIAdapter1* a = nullptr;
            for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d;
                if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.VendorId != 0x1414) {
                    bool dup = false;
                    for (auto& g : si.gpus) if (g.luid.LowPart == d.AdapterLuid.LowPart && g.luid.HighPart == d.AdapterLuid.HighPart) dup = true;
                    if (!dup) {
                        GpuInfo g;
                        g.name = Trim(d.Description);
                        g.vram = d.DedicatedVideoMemory;
                        g.luid = d.AdapterLuid;
                        g.hasLuid = true;
                        g.integrated = d.VendorId == 0x8086 || d.DedicatedVideoMemory < 600ull * 1024 * 1024;
                        auto it = drv.find(g.name);
                        if (it != drv.end()) {
                            g.driverVer = it->second.first;
                            wstring dd = it->second.second;
                            g.driverDate = WmiDate(dd);
                            if (dd.size() >= 8) {
                                SYSTEMTIME now; GetLocalTime(&now);
                                int y = _wtoi(dd.substr(0, 4).c_str()), m = _wtoi(dd.substr(4, 2).c_str());
                                g.driverAgeDays = ((now.wYear - y) * 12 + (now.wMonth - m)) * 30;
                            }
                        }
                        g.tempC = GpuTemperature(g.luid);
                        si.gpus.push_back(g);
                    }
                }
                a->Release();
            }
            f->Release();
        }
        FreeLibrary(dx);
    }
    if (si.gpus.empty())
        for (auto& kv : drv) { GpuInfo g; g.name = kv.first; g.driverVer = kv.second.first; g.driverDate = WmiDate(kv.second.second); si.gpus.push_back(g); }

    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps) && !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255) {
        si.battery.present = true;
        si.battery.chargePct = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : -1;
        si.battery.charging = (ps.BatteryFlag & 8) != 0;
        for (auto& r : WmiQuery(L"ROOT\\WMI", L"SELECT DesignedCapacity FROM BatteryStaticData")) si.battery.designMWh = (uint64_t)WNum(r, L"DesignedCapacity");
        for (auto& r : WmiQuery(L"ROOT\\WMI", L"SELECT FullChargedCapacity FROM BatteryFullChargedCapacity")) si.battery.fullMWh = (uint64_t)WNum(r, L"FullChargedCapacity");
        if (si.battery.designMWh && si.battery.fullMWh)
            si.battery.wearPct = std::max(0, (int)(100 - si.battery.fullMWh * 100 / si.battery.designMWh));
    }

    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT HighPrecisionTemperature, Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation")) {
        double k = WNum(r, L"HighPrecisionTemperature") / 10.0;
        if (k <= 0) k = WNum(r, L"Temperature");
        int c = (int)(k - 273.15 + 0.5);
        if (c > 15 && c < 120) si.thermalC = std::max(si.thermalC, c);
    }
}

wstring LicenseStatusText(int st) {
    switch (st) {
        case 0: return L"Не активирована";
        case 1: return L"Активирована";
        case 2: return L"Пробный период (OOB)";
        case 3: return L"Пробный период (OOT)";
        case 4: return L"Неподлинная копия";
        case 5: return L"Требуется активация (уведомления)";
        case 6: return L"Продлённый пробный период";
    }
    return L"Неизвестно";
}

static void CollectLicense(SysInfo& si) {
    for (auto& r : WmiQuery(L"ROOT\\CIMV2",
             L"SELECT Name, Description, LicenseStatus, PartialProductKey FROM SoftwareLicensingProduct "
             L"WHERE ApplicationID='55c92734-d682-4d71-983e-d6ec3f16059f' AND PartialProductKey IS NOT NULL")) {
        si.licenseStatus = (int)WNum(r, L"LicenseStatus", -1);
        si.licenseName = WStr(r, L"Name");
        si.partialKey = WStr(r, L"PartialProductKey");
        wstring d = WStr(r, L"Description");
        if (d.find(L"OEM") != wstring::npos) si.licenseText = L"OEM (ключ производителя)";
        else if (d.find(L"VOLUME_KMSCLIENT") != wstring::npos) si.licenseText = L"KMS (корпоративная)";
        else if (d.find(L"VOLUME") != wstring::npos) si.licenseText = L"Корпоративная (MAK)";
        else if (d.find(L"RETAIL") != wstring::npos) si.licenseText = L"Retail (коробочная/цифровая)";
        else si.licenseText = d;
        if (si.licenseStatus == 1) break;
    }
    for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT OA3xOriginalProductKey FROM SoftwareLicensingService"))
        si.oemKey = WStr(r, L"OA3xOriginalProductKey");
}

static void CollectSecurity(SysInfo& si) {
    for (auto& r : WmiQuery(L"ROOT\\SecurityCenter2", L"SELECT displayName, productState FROM AntiVirusProduct")) {
        int st = (int)WNum(r, L"productState");
        bool on = ((st >> 12) & 0xF) == 1;
        si.antivirus.push_back(WStr(r, L"displayName") + (on ? L"" : L" (выключен)"));
    }
    for (auto& r : WmiQuery(L"ROOT\\Microsoft\\Windows\\Defender", L"SELECT AntivirusEnabled, RealTimeProtectionEnabled, AntivirusSignatureAge FROM MSFT_MpComputerStatus")) {
        si.defenderOn = (int)WNum(r, L"AntivirusEnabled", -1);
        si.defenderRealtime = (int)WNum(r, L"RealTimeProtectionEnabled", -1);
        if (WHas(r, L"AntivirusSignatureAge")) {
            int d = (int)WNum(r, L"AntivirusSignatureAge");
            if (d > 3650) continue;
            si.defenderSigAge = d == 0 ? L"сегодня" : Fmt(L"%d ", d) + Plural(d, L"день", L"дня", L"дней") + L" назад";
        }
    }
}

static const wchar_t* SmartName(int id) {
    switch (id) {
        case 1: return L"Ошибки чтения";
        case 2: return L"Производительность";
        case 3: return L"Время раскрутки";
        case 4: return L"Запуски/остановки";
        case 5: return L"Переназначенные сектора";
        case 7: return L"Ошибки позиционирования";
        case 8: return L"Скорость позиционирования";
        case 9: return L"Часы работы";
        case 10: return L"Повторы раскрутки";
        case 11: return L"Повторы калибровки";
        case 12: return L"Включения питания";
        case 160: return L"Неисправимые сектора";
        case 161: return L"Резерв блоков";
        case 163: return L"Начальные плохие блоки";
        case 164: case 165: case 166: case 167: return L"Счётчик стираний";
        case 168: return L"Ошибки SATA PHY";
        case 169: return L"Остаток ресурса";
        case 170: return L"Резерв блоков";
        case 171: return L"Ошибки программирования";
        case 172: return L"Ошибки стирания";
        case 173: return L"Износ (выравнивание)";
        case 174: return L"Внезапные отключения";
        case 175: return L"Ошибки программирования";
        case 176: return L"Ошибки стирания";
        case 177: return L"Износ ячеек";
        case 178: return L"Использовано резерва";
        case 179: return L"Использовано резерва";
        case 180: return L"Свободный резерв";
        case 181: return L"Ошибки программирования";
        case 182: return L"Ошибки стирания";
        case 183: return L"Понижение скорости SATA";
        case 184: return L"Сквозные ошибки";
        case 187: return L"Неисправимые ошибки";
        case 188: return L"Таймауты команд";
        case 189: return L"Запись на большой высоте";
        case 190: return L"Температура потока воздуха";
        case 191: return L"Ошибки от ударов";
        case 192: return L"Аварийные выключения";
        case 193: return L"Циклы парковки";
        case 194: return L"Температура";
        case 195: return L"Исправленные ошибки ECC";
        case 196: return L"События переназначения";
        case 197: return L"Нестабильные сектора";
        case 198: return L"Неисправимые сектора";
        case 199: return L"Ошибки интерфейса (CRC)";
        case 200: return L"Ошибки записи";
        case 202: return L"Использовано ресурса";
        case 206: return L"Высота полёта головки";
        case 210: return L"Ошибки RAID";
        case 220: return L"Смещение диска";
        case 222: return L"Часы загрузки";
        case 223: return L"Повторы загрузки";
        case 225: return L"Записано (хост)";
        case 226: return L"Нагрузка";
        case 228: return L"Отключения питания";
        case 230: return L"Амплитуда головки / ресурс";
        case 231: return L"Остаток ресурса SSD";
        case 232: return L"Свободный резерв";
        case 233: return L"Износ NAND";
        case 234: return L"Записано NAND";
        case 235: return L"Хорошие блоки";
        case 240: return L"Время полёта головки";
        case 241: return L"Всего записано";
        case 242: return L"Всего прочитано";
        case 249: return L"Записано NAND";
        case 254: return L"Свободное падение";
    }
    return L"Атрибут производителя";
}

static bool SmartCritical(int id) {
    return id == 5 || id == 10 || id == 184 || id == 187 || id == 196 || id == 197 || id == 198 || id == 199 || id == 160;
}

static HANDLE OpenDisk(int n, bool& rw) {
    wstring p = Fmt(L"\\\\.\\PhysicalDrive%d", n);
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    rw = h != INVALID_HANDLE_VALUE;
    if (!rw) h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    return h;
}

static uint64_t Le(const uint8_t* p, int n) {
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static bool ReadNvme(HANDLE h, DiskHealth& d) {
    const DWORD protOff = 8, protLen = 40, dataLen = 512;
    vector<BYTE> buf(protOff + protLen + dataLen, 0);
    DWORD* q = (DWORD*)buf.data();
    q[0] = 50;
    q[1] = 0;
    DWORD* pd = (DWORD*)(buf.data() + protOff);
    pd[0] = 3;
    pd[1] = 2;
    pd[2] = 2;
    pd[3] = 0;
    pd[4] = protLen;
    pd[5] = dataLen;
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), (DWORD)buf.size(), buf.data(), (DWORD)buf.size(), &ret, nullptr))
        return false;
    DWORD* outProt = (DWORD*)(buf.data() + 8);
    DWORD off = outProt[4], len = outProt[5];
    if (len < 192 || 8 + off + 192 > buf.size()) return false;
    const uint8_t* L = buf.data() + 8 + off;
    d.available = true;
    d.source = L"NVMe";
    int crit = L[0];
    int tempK = (int)Le(L + 1, 2);
    int spare = L[3], spareThr = L[4], used = L[5];
    if (tempK > 200) d.tempC = tempK - 273;
    d.healthPct = std::max(0, 100 - used);
    d.writtenTB = (double)Le(L + 48, 8) * 512000.0 / 1e12;
    d.powerCycles = (int64_t)Le(L + 112, 8);
    d.powerOnHours = (int64_t)Le(L + 128, 8);
    d.unsafeShutdowns = (int64_t)Le(L + 144, 8);
    d.mediaErrors = (int64_t)Le(L + 160, 8);

    auto add = [&](int id, const wchar_t* name, uint64_t raw, bool critical) {
        SmartAttr a; a.id = id; a.name = name; a.raw = raw; a.critical = critical; d.attrs.push_back(a);
    };
    add(1, L"Критические предупреждения", crit, true);
    add(2, L"Температура, °C", d.tempC < 0 ? 0 : d.tempC, false);
    add(3, L"Доступный резерв, %", spare, true);
    add(4, L"Порог резерва, %", spareThr, false);
    add(5, L"Использовано ресурса, %", used, true);
    add(6, L"Прочитано, ГБ", Le(L + 32, 8) * 512000ull / 1000000000ull, false);
    add(7, L"Записано, ГБ", Le(L + 48, 8) * 512000ull / 1000000000ull, false);
    add(8, L"Включения питания", d.powerCycles, false);
    add(9, L"Часы работы", d.powerOnHours, false);
    add(10, L"Небезопасные отключения", d.unsafeShutdowns, false);
    add(11, L"Ошибки носителя", d.mediaErrors, true);
    add(12, L"Записей в журнале ошибок", Le(L + 176, 8), false);

    d.status = Sev::Ok;
    if (crit != 0) { d.status = Sev::Bad; d.notes.push_back(L"Накопитель сообщает о критическом состоянии (Critical Warning). Срочно скопируйте данные."); }
    if (spare < spareThr && spareThr > 0) { d.status = Sev::Bad; d.notes.push_back(L"Резервные блоки исчерпаны — диск близок к отказу."); }
    if (d.mediaErrors > 0) { d.status = std::max(d.status, Sev::Warn); d.notes.push_back(Fmt(L"Есть ошибки носителя (%lld) — следите за диском, сделайте копию важных данных.", (long long)d.mediaErrors)); }
    if (used >= 90) { d.status = std::max(d.status, Sev::Warn); d.notes.push_back(L"Ресурс записи почти выработан."); }
    if (d.tempC >= 70) { d.status = std::max(d.status, Sev::Warn); d.notes.push_back(L"Диск перегревается: нужен радиатор или обдув."); }
    return true;
}

static bool AtaCmd(HANDLE h, BYTE feature, BYTE* out512) {

    {
        SENDCMDINPARAMS in{};
        in.cBufferSize = 512;
        in.irDriveRegs.bFeaturesReg = feature;
        in.irDriveRegs.bSectorCountReg = 1;
        in.irDriveRegs.bSectorNumberReg = 1;
        in.irDriveRegs.bCylLowReg = SMART_CYL_LOW;
        in.irDriveRegs.bCylHighReg = SMART_CYL_HI;
        in.irDriveRegs.bDriveHeadReg = 0xA0;
        in.irDriveRegs.bCommandReg = SMART_CMD;
        vector<BYTE> out(sizeof(SENDCMDOUTPARAMS) + 512, 0);
        DWORD ret = 0;
        if (DeviceIoControl(h, SMART_RCV_DRIVE_DATA, &in, sizeof(in) - 1, out.data(), (DWORD)out.size(), &ret, nullptr) && ret > 16) {
            memcpy(out512, ((SENDCMDOUTPARAMS*)out.data())->bBuffer, 512);
            return true;
        }
    }

    {
        struct { ATA_PASS_THROUGH_EX apt; DWORD pad; BYTE data[512]; } b{};
        b.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
        b.apt.AtaFlags = ATA_FLAGS_DATA_IN | ATA_FLAGS_DRDY_REQUIRED;
        b.apt.DataTransferLength = 512;
        b.apt.TimeOutValue = 5;
        b.apt.DataBufferOffset = offsetof(decltype(b), data);
        b.apt.CurrentTaskFile[0] = feature;
        b.apt.CurrentTaskFile[1] = 1;
        b.apt.CurrentTaskFile[2] = 1;
        b.apt.CurrentTaskFile[3] = SMART_CYL_LOW;
        b.apt.CurrentTaskFile[4] = SMART_CYL_HI;
        b.apt.CurrentTaskFile[5] = 0xA0;
        b.apt.CurrentTaskFile[6] = SMART_CMD;
        DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH, &b, sizeof(b), &b, sizeof(b), &ret, nullptr)) {
            memcpy(out512, b.data, 512);
            return true;
        }
    }
    return false;
}

static bool ReadAta(HANDLE h, DiskHealth& d, bool ssd) {
    BYTE val[512] = {}, thr[512] = {};
    if (!AtaCmd(h, READ_ATTRIBUTES, val)) return false;
    AtaCmd(h, READ_THRESHOLDS, thr);
    d.available = true;
    d.source = L"SMART";
    d.status = Sev::Ok;
    for (int i = 0; i < 30; ++i) {
        const BYTE* a = val + 2 + i * 12;
        if (a[0] == 0) continue;
        SmartAttr s;
        s.id = a[0];
        s.value = a[3];
        s.worst = a[4];
        s.raw = Le(a + 5, 6);
        for (int j = 0; j < 30; ++j) if (thr[2 + j * 12] == a[0]) s.thresh = thr[2 + j * 12 + 1];
        s.name = SmartName(s.id);
        s.critical = SmartCritical(s.id);
        d.attrs.push_back(s);

        uint64_t r32 = s.raw & 0xFFFFFFFF;
        switch (s.id) {
            case 5: d.reallocated = (int64_t)(s.raw & 0xFFFF); break;
            case 9: d.powerOnHours = (int64_t)r32; break;
            case 12: d.powerCycles = (int64_t)r32; break;
            case 194: if ((s.raw & 0xFF) > 0 && (s.raw & 0xFF) < 100) d.tempC = (int)(s.raw & 0xFF); break;
            case 190: if (d.tempC < 0 && (s.raw & 0xFF) > 0 && (s.raw & 0xFF) < 100) d.tempC = (int)(s.raw & 0xFF); break;
            case 197: d.pending = (int64_t)(s.raw & 0xFFFF); break;
            case 198: d.uncorrectable = (int64_t)(s.raw & 0xFFFF); break;
            case 199: d.crcErrors = (int64_t)r32; break;
            case 169:

                if (ssd && s.value == 100 && s.raw > 0 && s.raw <= 100) { if (d.healthPct < 0) d.healthPct = (int)s.raw; break; }
                if (ssd && s.value > 0 && s.value <= 100 && d.healthPct < 0) d.healthPct = s.value;
                break;
            case 177: case 231: case 233:
                if (ssd && s.value > 0 && s.value <= 100 && (d.healthPct < 0 || s.id == 231)) d.healthPct = s.value;
                break;
            case 202:
                if (ssd && s.value > 0 && s.value <= 100 && d.healthPct < 0) d.healthPct = s.value;
                break;
        }
        if (s.thresh > 0 && s.value > 0 && s.value <= s.thresh && s.thresh < 0xFE) {
            d.status = Sev::Bad;
            d.notes.push_back(Fmt(L"Атрибут %02X «%ls» ниже порога производителя — диск неисправен.", s.id, s.name.c_str()));
        }
    }

    if (d.powerOnHours > 300000) d.powerOnHours = -1;
    if (d.writtenTB > 10000) d.writtenTB = -1;

    if (d.reallocated > 0 || d.pending > 0 || d.uncorrectable > 0) {
        d.status = std::max(d.status, Sev::Warn);
        d.notes.push_back(Fmt(L"Есть повреждённые сектора (переназначено %lld, нестабильных %lld, неисправимых %lld). "
                              L"Диск начинает сыпаться — скопируйте данные и планируйте замену.",
                              (long long)std::max<int64_t>(0, d.reallocated), (long long)std::max<int64_t>(0, d.pending),
                              (long long)std::max<int64_t>(0, d.uncorrectable)));
        if ((d.pending > 10 || d.uncorrectable > 10 || d.reallocated > 100)) d.status = Sev::Bad;
    }
    if (d.crcErrors > 0)
        d.notes.push_back(Fmt(L"Ошибки интерфейса CRC: %lld. Обычно виноват SATA-кабель или разъём — замените кабель.", (long long)d.crcErrors));
    if (ssd && d.healthPct >= 0 && d.healthPct <= 10) { d.status = std::max(d.status, Sev::Warn); d.notes.push_back(L"Ресурс SSD почти выработан."); }
    if (d.tempC >= 55 && !ssd) { d.status = std::max(d.status, Sev::Warn); d.notes.push_back(L"Жёсткий диск горячий (выше 55 °C): проверьте охлаждение корпуса."); }
    return true;
}

wstring DiskInfo::kind() const {
    if (busType == 7) return L"USB-накопитель";
    if (busType == 12 || busType == 13) return L"Карта памяти";
    if (busType == 17) return L"NVMe SSD";
    if (mediaType == 4) return busType == 11 ? L"SATA SSD" : L"SSD";
    if (mediaType == 3) return L"HDD";
    if (busType == 11) return L"SATA-диск";
    return L"Диск";
}

static void FinishHealthText(DiskHealth& h) {
    h.statusText = h.status == Sev::Ok ? L"Хорошо" : h.status == Sev::Warn ? L"Внимание" : h.status == Sev::Bad ? L"Плохо" : L"Нет данных";
}

void CollectDisks(SysInfo& si) {
    si.disks.clear();
    si.volumes.clear();
    for (auto& r : WmiQuery(L"ROOT\\Microsoft\\Windows\\Storage",
             L"SELECT DeviceId, FriendlyName, SerialNumber, MediaType, BusType, Size, HealthStatus, FirmwareVersion FROM MSFT_PhysicalDisk")) {
        DiskInfo d;
        d.number = _wtoi(WStr(r, L"DeviceId").c_str());
        d.model = WStr(r, L"FriendlyName");
        d.serial = WStr(r, L"SerialNumber");
        d.firmware = WStr(r, L"FirmwareVersion");
        d.mediaType = (int)WNum(r, L"MediaType");
        d.busType = (int)WNum(r, L"BusType");
        d.size = (uint64_t)WNum(r, L"Size");
        int hs = (int)WNum(r, L"HealthStatus", 5);
        d.health.status = hs == 0 ? Sev::Ok : hs == 1 ? Sev::Warn : hs == 2 ? Sev::Bad : Sev::Unknown;
        d.health.source = L"Windows";
        d.removable = d.busType == 7 || d.busType == 12 || d.busType == 13;
        si.disks.push_back(d);
    }
    if (si.disks.empty()) {
        for (auto& r : WmiQuery(L"ROOT\\CIMV2", L"SELECT Index, Model, Size, InterfaceType, SerialNumber, MediaType FROM Win32_DiskDrive")) {
            DiskInfo d;
            d.number = (int)WNum(r, L"Index");
            d.model = WStr(r, L"Model");
            d.serial = WStr(r, L"SerialNumber");
            d.size = (uint64_t)WNum(r, L"Size");
            wstring it = WStr(r, L"InterfaceType");
            d.busType = it == L"USB" ? 7 : it == L"SCSI" ? 10 : 11;
            d.removable = WStr(r, L"MediaType").find(L"Removable") != wstring::npos || d.busType == 7;
            si.disks.push_back(d);
        }
    }

    std::map<int, WRow> rel;
    for (auto& r : WmiQuery(L"ROOT\\Microsoft\\Windows\\Storage",
             L"SELECT DeviceId, Temperature, Wear, PowerOnHours, ReadErrorsUncorrected, StartStopCycleCount FROM MSFT_StorageReliabilityCounter"))
        rel[_wtoi(WStr(r, L"DeviceId").c_str())] = r;

    for (auto& d : si.disks) {
        bool rw = false;
        HANDLE h = OpenDisk(d.number, rw);
        if (h != INVALID_HANDLE_VALUE) {

            if (d.mediaType == 0) {
                STORAGE_PROPERTY_QUERY q{};
                q.PropertyId = StorageDeviceSeekPenaltyProperty;
                q.QueryType = PropertyStandardQuery;
                DEVICE_SEEK_PENALTY_DESCRIPTOR sp{};
                DWORD ret;
                if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &sp, sizeof(sp), &ret, nullptr))
                    d.mediaType = sp.IncursSeekPenalty ? 3 : 4;
            }
            DiskHealth hl;
            bool got = false;
            if (d.busType == 17) got = ReadNvme(h, hl);
            else if (!d.removable) got = ReadAta(h, hl, d.mediaType == 4);
            if (got) {
                if (d.health.status == Sev::Bad) hl.status = Sev::Bad;
                d.health = hl;
            }
            CloseHandle(h);
        }
        if (!d.health.available) {
            auto it = rel.find(d.number);
            if (it != rel.end()) {
                auto& r = it->second;
                if (WHas(r, L"Temperature") && WNum(r, L"Temperature") > 0) d.health.tempC = (int)WNum(r, L"Temperature");
                if (WHas(r, L"Wear")) d.health.healthPct = 100 - (int)WNum(r, L"Wear");
                if (WHas(r, L"PowerOnHours")) d.health.powerOnHours = (int64_t)WNum(r, L"PowerOnHours");
                if (WHas(r, L"StartStopCycleCount")) d.health.powerCycles = (int64_t)WNum(r, L"StartStopCycleCount");
                d.health.available = true;
            }
            if (d.health.status == Sev::Unknown && !d.removable && !IsElevated())
                d.health.notes.push_back(L"Для чтения SMART запустите программу от имени администратора.");
            if (d.removable && !d.health.available)
                d.health.notes.push_back(L"USB-накопители обычно не отдают SMART через переходник.");
        }
        if (d.health.status == Sev::Bad)
            d.health.notes.insert(d.health.notes.begin(), L"Windows/SMART сообщает о неисправности диска.");
        FinishHealthText(d.health);
    }

    wchar_t drives[512];
    DWORD n = GetLogicalDriveStringsW(511, drives);
    wstring sysd = SystemDrive();
    for (wchar_t* p = drives; p < drives + n && *p; p += wcslen(p) + 1) {
        UINT t = GetDriveTypeW(p);
        if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) continue;
        VolumeInfo v;
        v.letter = wstring(p).substr(0, 2);
        wchar_t label[MAX_PATH] = {}, fs[64] = {};
        if (!GetVolumeInformationW(p, label, MAX_PATH, nullptr, nullptr, nullptr, fs, 64)) continue;
        v.label = label;
        v.fs = fs;
        ULARGE_INTEGER fr, tot;
        if (GetDiskFreeSpaceExW(p, &fr, &tot, nullptr)) { v.total = tot.QuadPart; v.free = fr.QuadPart; }
        v.system = IEquals(v.letter, sysd);
        wstring dev = L"\\\\.\\" + v.letter;
        HANDLE h = CreateFileW(dev.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            BYTE b[256];
            DWORD ret;
            if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, b, sizeof(b), &ret, nullptr)) {
                auto ext = (VOLUME_DISK_EXTENTS*)b;
                if (ext->NumberOfDiskExtents > 0) v.disk = (int)ext->Extents[0].DiskNumber;
            }
            CloseHandle(h);
        }
        for (auto& d : si.disks)
            if (d.number == v.disk) {
                d.letters.push_back(v.letter);
                if (v.system) d.isSystem = true;
            }
        si.volumes.push_back(v);
    }
    std::sort(si.disks.begin(), si.disks.end(), [](const DiskInfo& a, const DiskInfo& b) {
        if (a.isSystem != b.isSystem) return a.isSystem;
        return a.number < b.number;
    });
}

SysInfo CollectSysInfo() {
    SysInfo si;
    CollectOS(si);
    CollectHardware(si);
    CollectDisks(si);
    CollectLicense(si);
    CollectSecurity(si);
    si.ready = true;
    return si;
}

typedef UINT D3DKMT_HANDLE_;
struct KMT_OPENLUID { LUID AdapterLuid; D3DKMT_HANDLE_ hAdapter; };
struct KMT_QUERYINFO { D3DKMT_HANDLE_ hAdapter; UINT Type; void* pPrivateDriverData; UINT PrivateDriverDataSize; };
struct KMT_CLOSE { D3DKMT_HANDLE_ hAdapter; };
struct KMT_PERFDATA {
    UINT32 PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PCIEBandwidth;
    ULONG FanRPM, Power, Temperature;
    UCHAR PowerStateOverride;
};

int GpuTemperature(const LUID& luid) {
    typedef LONG(APIENTRY * OpenFn)(KMT_OPENLUID*);
    typedef LONG(APIENTRY * QueryFn)(const KMT_QUERYINFO*);
    typedef LONG(APIENTRY * CloseFn)(const KMT_CLOSE*);
    static HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    static auto open = (OpenFn)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
    static auto query = (QueryFn)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
    static auto close = (CloseFn)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!open || !query || !close) return -1;
    KMT_OPENLUID o{ luid, 0 };
    if (open(&o) != 0) return -1;
    KMT_PERFDATA pd{};
    KMT_QUERYINFO q{ o.hAdapter, 62  , &pd, sizeof(pd) };
    LONG st = query(&q);
    KMT_CLOSE c{ o.hAdapter };
    close(&c);
    if (st != 0 || pd.Temperature == 0) return -1;
    int t = (int)(pd.Temperature / 10);
    return (t > 5 && t < 130) ? t : -1;
}

void LiveUpdate(LiveStats& s, const SysInfo& si) {
    static ULONGLONG pIdle = 0, pKern = 0, pUser = 0;
    FILETIME fi, fk, fu;
    if (GetSystemTimes(&fi, &fk, &fu)) {
        auto u64 = [](FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; };
        ULONGLONG i = u64(fi), k = u64(fk), u = u64(fu);
        ULONGLONG di = i - pIdle, dk = k - pKern, du = u - pUser;
        if (pKern && dk + du > 0) s.cpuPct = std::min(100.0, std::max(0.0, 100.0 * (double)(dk + du - di) / (double)(dk + du)));
        pIdle = i; pKern = k; pUser = u;
    }
    MEMORYSTATUSEX ms{ sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        s.ramTotal = ms.ullTotalPhys;
        s.ramUsed = ms.ullTotalPhys - ms.ullAvailPhys;
        s.ramPct = ms.dwMemoryLoad;
    }
    s.gpuTemps.clear();
    for (auto& g : si.gpus) s.gpuTemps.push_back(g.hasLuid ? GpuTemperature(g.luid) : -1);
}

wstring ConfigErrorText(int c) {
    switch (c) {
        case 1: return L"Устройство настроено неправильно — переустановите драйвер";
        case 3: return L"Драйвер повреждён или не хватает памяти";
        case 10: return L"Устройство не может запуститься — обычно нужен другой драйвер";
        case 12: return L"Конфликт ресурсов с другим устройством";
        case 14: return L"Требуется перезагрузка";
        case 18: return L"Нужно переустановить драйвер";
        case 19: return L"Повреждены сведения о конфигурации в реестре";
        case 21: return L"Устройство удаляется Windows";
        case 22: return L"Устройство отключено в Диспетчере устройств";
        case 24: return L"Устройство отсутствует или работает неправильно";
        case 28: return L"Драйвер не установлен";
        case 29: return L"Устройство отключено в BIOS/UEFI";
        case 31: return L"Windows не может загрузить драйвер";
        case 32: return L"Служба драйвера отключена";
        case 37: return L"Драйвер вернул ошибку при инициализации";
        case 38: return L"Предыдущий экземпляр драйвера ещё в памяти — перезагрузите ПК";
        case 39: return L"Драйвер повреждён или отсутствует";
        case 40: return L"Ошибка записи драйвера в реестре";
        case 41: return L"Драйвер загружен, но устройство не найдено";
        case 43: return L"Устройство сообщило о сбое и было остановлено";
        case 45: return L"Устройство сейчас не подключено";
        case 47: return L"Устройство подготовлено к безопасному извлечению";
        case 48: return L"Драйвер заблокирован из-за известных проблем";
        case 49: return L"Переполнен системный куст реестра";
        case 52: return L"Драйвер без цифровой подписи";
    }
    return Fmt(L"Код ошибки %d", c);
}

vector<ProblemDevice> CollectProblemDevices() {
    vector<ProblemDevice> r;
    for (auto& row : WmiQuery(L"ROOT\\CIMV2", L"SELECT Name, PNPDeviceID, PNPClass, ConfigManagerErrorCode FROM Win32_PnPEntity WHERE ConfigManagerErrorCode <> 0")) {
        ProblemDevice d;
        d.code = (int)WNum(row, L"ConfigManagerErrorCode");
        if (d.code == 45) continue;
        d.name = WStr(row, L"Name");
        d.id = WStr(row, L"PNPDeviceID");
        d.cls = WStr(row, L"PNPClass");
        if (d.name.empty()) d.name = L"Неизвестное устройство";
        d.problem = ConfigErrorText(d.code);
        r.push_back(d);
    }
    return r;
}

static wstring XmlAttr(const wstring& xml, const wstring& tag, const wstring& attr) {
    size_t p = xml.find(L"<" + tag);
    if (p == wstring::npos) return L"";
    size_t a = xml.find(attr + L"='", p);
    wchar_t q = L'\'';
    if (a == wstring::npos) { a = xml.find(attr + L"=\"", p); q = L'"'; }
    if (a == wstring::npos) return L"";
    a += attr.size() + 2;
    size_t e = xml.find(q, a);
    return e == wstring::npos ? L"" : xml.substr(a, e - a);
}

static vector<wstring> XmlData(const wstring& xml) {
    vector<wstring> r;
    size_t p = 0;
    while ((p = xml.find(L"<Data", p)) != wstring::npos) {
        size_t gt = xml.find(L'>', p);
        if (gt == wstring::npos) break;
        if (xml[gt - 1] == L'/') { r.push_back(L""); p = gt; continue; }
        size_t e = xml.find(L"</Data>", gt);
        if (e == wstring::npos) break;
        r.push_back(xml.substr(gt + 1, e - gt - 1));
        p = e;
    }
    return r;
}

static wstring BugcheckHint(unsigned long code) {
    switch (code) {
        case 0x0A: return L"IRQL_NOT_LESS_OR_EQUAL — обычно драйвер или ОЗУ";
        case 0x19: return L"BAD_POOL_HEADER — драйвер или ОЗУ";
        case 0x1A: return L"MEMORY_MANAGEMENT — часто неисправна оперативная память";
        case 0x1E: return L"KMODE_EXCEPTION_NOT_HANDLED — сбой драйвера";
        case 0x3B: return L"SYSTEM_SERVICE_EXCEPTION — драйвер или системный файл";
        case 0x50: return L"PAGE_FAULT_IN_NONPAGED_AREA — ОЗУ, диск или драйвер";
        case 0x7A: return L"KERNEL_DATA_INPAGE_ERROR — проблемы с диском или кабелем";
        case 0x77: return L"KERNEL_STACK_INPAGE_ERROR — проблемы с диском";
        case 0x7E: return L"SYSTEM_THREAD_EXCEPTION_NOT_HANDLED — сбой драйвера";
        case 0x9F: return L"DRIVER_POWER_STATE_FAILURE — драйвер при сне/пробуждении";
        case 0xBE: return L"ATTEMPTED_WRITE_TO_READONLY_MEMORY — драйвер";
        case 0xC2: return L"BAD_POOL_CALLER — драйвер";
        case 0xD1: return L"DRIVER_IRQL_NOT_LESS_OR_EQUAL — сбой драйвера (часто сеть/Wi-Fi)";
        case 0xEF: return L"CRITICAL_PROCESS_DIED — повреждение системы или диска";
        case 0xF4: return L"CRITICAL_OBJECT_TERMINATION — диск или повреждение системы";
        case 0x101: return L"CLOCK_WATCHDOG_TIMEOUT — процессор (разгон, перегрев, BIOS)";
        case 0x109: return L"CRITICAL_STRUCTURE_CORRUPTION — драйвер или ОЗУ";
        case 0x116: return L"VIDEO_TDR_FAILURE — видеодрайвер или видеокарта";
        case 0x117: return L"VIDEO_TDR_TIMEOUT — видеодрайвер";
        case 0x119: return L"VIDEO_SCHEDULER_INTERNAL_ERROR — видеодрайвер";
        case 0x124: return L"WHEA_UNCORRECTABLE_ERROR — аппаратная ошибка (ЦП, ОЗУ, разгон, питание)";
        case 0x12B: return L"FAULTY_HARDWARE_CORRUPTED_PAGE — неисправна ОЗУ";
        case 0x133: return L"DPC_WATCHDOG_VIOLATION — драйвер (часто SSD/чипсет)";
        case 0x139: return L"KERNEL_SECURITY_CHECK_FAILURE — драйвер или ОЗУ";
        case 0x154: return L"UNEXPECTED_STORE_EXCEPTION — диск";
        case 0x9C: return L"MACHINE_CHECK_EXCEPTION — аппаратная ошибка";
    }
    return L"";
}

static wstring EvtTimeLocal(const wstring& iso, int64_t& key) {

    SYSTEMTIME st{};
    if (swscanf(iso.c_str(), L"%hu-%hu-%huT%hu:%hu:%hu", &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) < 6) return iso;
    FILETIME ft, lft;
    SystemTimeToFileTime(&st, &ft);
    key = ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    return Fmt(L"%02d.%02d.%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
}

static void QueryEvents(const wchar_t* log, const wstring& xpath, int maxN, const std::function<void(const wstring&)>& fn) {
    EVT_HANDLE q = EvtQuery(nullptr, log, xpath.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!q) return;
    EVT_HANDLE ev[16];
    DWORD got = 0;
    int total = 0;
    while (total < maxN && EvtNext(q, 16, ev, 2000, 0, &got)) {
        for (DWORD i = 0; i < got; ++i) {
            DWORD used = 0, props = 0;
            EvtRender(nullptr, ev[i], EvtRenderEventXml, 0, nullptr, &used, &props);
            vector<wchar_t> b(used / 2 + 2, 0);
            if (EvtRender(nullptr, ev[i], EvtRenderEventXml, (DWORD)b.size() * 2, b.data(), &used, &props)) fn(b.data());
            EvtClose(ev[i]);
            ++total;
        }
    }
    EvtClose(q);
}

CrashInfo CollectCrashes() {
    CrashInfo ci;
    const wstring d30 = L"TimeCreated[timediff(@SystemTime) <= 2592000000]";

    QueryEvents(L"System", L"*[System[Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] and EventID=1001 and " + d30 + L"]]", 50,
        [&](const wstring& x) {
            CrashEvent e;
            e.time = EvtTimeLocal(XmlAttr(x, L"TimeCreated", L"SystemTime"), e.sortKey);
            e.kind = L"Синий экран (BSOD)";
            auto d = XmlData(x);
            wstring code = d.empty() ? L"" : d[0];
            unsigned long c = wcstoul(code.c_str(), nullptr, 16);
            wstring hint = BugcheckHint(c);
            e.detail = Fmt(L"Код 0x%08lX", c) + (hint.empty() ? L"" : L" · " + hint);
            ci.events.push_back(e);
            ci.bsod30++;
        });

    QueryEvents(L"System", L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and EventID=41 and " + d30 + L"]]", 50,
        [&](const wstring& x) {
            CrashEvent e;
            e.time = EvtTimeLocal(XmlAttr(x, L"TimeCreated", L"SystemTime"), e.sortKey);
            auto d = XmlData(x);
            unsigned long bc = d.empty() ? 0 : wcstoul(d[0].c_str(), nullptr, 10);
            e.kind = L"Внезапное выключение";
            e.detail = bc ? Fmt(L"После синего экрана (код 0x%lX)", bc)
                          : L"Пропало питание, зависание с перезагрузкой кнопкой или перегрев блока питания";
            ci.events.push_back(e);
            ci.unexpected30++;
        });

    std::map<wstring, int> apps;
    QueryEvents(L"Application", L"*[System[Provider[@Name='Application Error'] and EventID=1000 and TimeCreated[timediff(@SystemTime) <= 604800000]]]", 300,
        [&](const wstring& x) {
            auto d = XmlData(x);
            if (!d.empty() && !d[0].empty()) apps[d[0]]++;
            ci.appCrash7++;
        });
    vector<std::pair<int, wstring>> top;
    for (auto& kv : apps) top.push_back({ kv.second, kv.first });
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 5; ++i) {
        CrashEvent e;
        e.kind = L"Падения программы";
        e.time = L"за 7 дней";
        e.detail = top[i].second + Fmt(L" — %d ", top[i].first) + Plural(top[i].first, L"раз", L"раза", L"раз");
        e.sortKey = -1 - (int64_t)i;
        ci.events.push_back(e);
    }
    std::stable_sort(ci.events.begin(), ci.events.end(), [](const CrashEvent& a, const CrashEvent& b) { return a.sortKey > b.sortKey; });

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((WinDir() + L"\\Minidump\\*.dmp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do ci.dumps.push_back(fd.cFileName); while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return ci;
}

vector<Finding> Diagnose(const SysInfo& si, const vector<ProblemDevice>& devs, const CrashInfo& cr, const LiveStats& live) {
    vector<Finding> f;
    auto add = [&](Sev s, const wstring& t, const wstring& x, int page) { f.push_back({ s, t, x, page }); };

    for (auto& d : si.disks) {
        if (d.health.status == Sev::Bad)
            add(Sev::Bad, L"Диск неисправен: " + d.model, L"SMART сообщает о серьёзных проблемах. Срочно скопируйте данные клиента и замените диск.", PG_DISKS);
        else if (d.health.status == Sev::Warn)
            add(Sev::Warn, L"Диск требует внимания: " + d.model, d.health.notes.empty() ? L"Есть тревожные показатели SMART." : d.health.notes[0], PG_DISKS);
    }
    for (auto& d : si.disks)
        if (d.isSystem && d.mediaType == 3)
            add(Sev::Warn, L"Windows стоит на жёстком диске (HDD)",
                L"Главная причина медленной работы. Замена на SSD ускорит загрузку и программы в 3–5 раз.", PG_DISKS);
    for (auto& v : si.volumes) {
        if (!v.total) continue;
        double freePct = 100.0 * v.free / v.total;
        if (v.system && (v.free < 5ull * 1024 * 1024 * 1024 || freePct < 5))
            add(Sev::Bad, L"Мало места на системном диске " + v.letter, L"Свободно " + HumanSize(v.free) + L". Windows и программы начинают тормозить и не могут обновиться — выполните очистку.", PG_CLEANUP);
        else if (v.system && (v.free < 15ull * 1024 * 1024 * 1024 || freePct < 12))
            add(Sev::Warn, L"Заканчивается место на диске " + v.letter, L"Свободно " + HumanSize(v.free) + L". Рекомендуется очистка.", PG_CLEANUP);
    }
    if (si.ramTotal && si.ramTotal < 6ull * 1024 * 1024 * 1024)
        add(Sev::Warn, L"Мало оперативной памяти: " + HumanSize(si.ramTotal),
            L"Для Windows 10/11 и браузера комфортно от 8 ГБ. Посмотрите свободные слоты на странице «Железо».", PG_HARDWARE);
    if (live.ramPct >= 85)
        add(Sev::Warn, Fmt(L"Память занята на %.0f%%", live.ramPct), L"Посмотрите на странице «Процессы», что её занимает.", PG_PROCESSES);
    if (si.startupCount > 12)
        add(Sev::Warn, Fmt(L"Много программ в автозагрузке: %d", si.startupCount), L"Каждая замедляет включение ПК. Отключите лишние.", PG_STARTUP);
    else if (si.startupCount > 7)
        add(Sev::Info, Fmt(L"Программ в автозагрузке: %d", si.startupCount), L"Проверьте, все ли нужны при старте.", PG_STARTUP);
    if (!devs.empty())
        add(Sev::Warn, Fmt(L"Устройства с проблемами: %d", (int)devs.size()), L"Есть устройства без драйвера или с ошибкой.", PG_DEVICES);
    if (cr.bsod30 > 0)
        add(cr.bsod30 >= 3 ? Sev::Bad : Sev::Warn, Fmt(L"Синие экраны за 30 дней: %d", cr.bsod30), L"Смотрите коды ошибок на странице «Драйверы и сбои».", PG_DEVICES);
    else if (cr.unexpected30 >= 3)
        add(Sev::Warn, Fmt(L"Внезапные выключения за 30 дней: %d", cr.unexpected30), L"Проверьте блок питания, перегрев и розетку.", PG_DEVICES);
    for (size_t i = 0; i < si.gpus.size(); ++i) {
        int t = i < live.gpuTemps.size() && live.gpuTemps[i] > 0 ? live.gpuTemps[i] : si.gpus[i].tempC;
        if (t >= 85) add(Sev::Warn, Fmt(L"Видеокарта горячая: %d °C", t), L"Почистите от пыли, проверьте вентиляторы и термопасту.", PG_HARDWARE);
    }
    for (auto& d : si.disks)
        if (d.health.tempC >= 60) add(Sev::Warn, Fmt(L"Диск нагрет до %d °C", d.health.tempC), d.model + L" — улучшите охлаждение.", PG_DISKS);
    if (si.battery.present && si.battery.wearPct >= 40)
        add(Sev::Warn, Fmt(L"Батарея изношена на %d%%", si.battery.wearPct), L"Ноутбук будет быстро разряжаться — предложите замену батареи.", PG_HARDWARE);
    if (si.licenseStatus >= 0 && si.licenseStatus != 1)
        add(Sev::Info, L"Windows не активирована", LicenseStatusText(si.licenseStatus), PG_ACTIVATION);
    bool avOn = si.defenderRealtime == 1;
    for (auto& a : si.antivirus) if (a.find(L"(выключен)") == wstring::npos) avOn = true;
    if (!avOn && (si.defenderOn >= 0 || !si.antivirus.empty()))
        add(Sev::Bad, L"Антивирусная защита выключена", L"Ни Защитник Windows, ни другой антивирус не защищают ПК в реальном времени.", PG_SECURITY);
    if (si.proxyOn)
        add(Sev::Warn, L"Включён прокси-сервер", L"Прокси: " + si.proxyServer + L". Если клиент его не настраивал — это признак рекламного вируса.", PG_NETWORK);
    if (si.hostsEntries > 0)
        add(Sev::Info, Fmt(L"В файле hosts есть записи: %d", si.hostsEntries), L"Проверьте, не блокируют ли они сайты антивирусов или обновлений.", PG_NETWORK);
    if (IContains(si.powerPlan, L"эконом") || IContains(si.powerPlan, L"saver"))
        add(si.laptop ? Sev::Info : Sev::Warn, L"Схема питания «Экономия энергии»", L"Процессор работает на пониженной частоте. Для ПК лучше «Сбалансированная» или «Высокая производительность».", PG_TWEAKS);
    if (si.uptimeSec > 7 * 86400)
        add(Sev::Info, L"Компьютер не перезагружался " + HumanDuration(si.uptimeSec), L"Перезагрузка освобождает память и завершает установку обновлений.", -1);
    if (si.pendingReboot)
        add(Sev::Info, L"Ожидается перезагрузка", L"Windows установила обновления и ждёт перезагрузки.", -1);
    std::stable_sort(f.begin(), f.end(), [](const Finding& a, const Finding& b) { return (int)a.sev > (int)b.sev; });
    return f;
}

int HealthScore(const vector<Finding>& f) {
    int s = 100;
    for (auto& x : f) s -= x.sev == Sev::Bad ? 20 : x.sev == Sev::Warn ? 8 : x.sev == Sev::Info ? 2 : 0;
    return std::max(5, s);
}
