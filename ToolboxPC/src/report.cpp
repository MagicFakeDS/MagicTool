#include "report.h"
#include <wincrypt.h>

static wstring H(const wstring& s) {
    wstring r;
    for (wchar_t c : s) {
        if (c == L'<') r += L"&lt;";
        else if (c == L'>') r += L"&gt;";
        else if (c == L'&') r += L"&amp;";
        else if (c == L'"') r += L"&quot;";
        else if (c == L'\n') r += L"<br>";
        else r += c;
    }
    return r;
}

static wstring LogoBase64() {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!r) return L"";
    HGLOBAL g = LoadResource(nullptr, r);
    DWORD sz = SizeofResource(nullptr, r);
    const BYTE* p = (const BYTE*)LockResource(g);
    DWORD n = 0;
    CryptBinaryToStringW(p, sz, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    wstring s(n, 0);
    CryptBinaryToStringW(p, sz, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &s[0], &n);
    s.resize(n);
    return s;
}

static const wchar_t* SevClass(Sev s) {
    return s == Sev::Ok ? L"ok" : s == Sev::Info ? L"info" : s == Sev::Warn ? L"warn" : s == Sev::Bad ? L"bad" : L"muted";
}

wstring DefaultReportPath() {
    wstring pc = app.si.computerName.empty() ? L"PC" : app.si.computerName;
    for (auto& c : pc) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return ReportsDir() + L"\\Отчёт_" + pc + L"_" + NowStr(L"%04d-%02d-%02d_%02d%02d") + L".html";
}

bool WriteReport(const wstring& path, const ReportOptions& o) {
    auto& si = app.si;
    vector<Finding> f = Diagnose(si, app.devs, app.crashes, app.live);
    int score = HealthScore(f);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wstring date = Fmt(L"%02d.%02d.%04d %02d:%02d", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute);
    wstring scoreCls = score >= 80 ? L"ok" : score >= 55 ? L"warn" : L"bad";
    wstring verdict = score >= 80 ? L"Хорошее состояние" : score >= 55 ? L"Есть замечания" : L"Требуется ремонт";

    wstring h;
    h += L"<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    h += L"<title>Отчёт о компьютере " + H(si.computerName) + L" — MagicTool</title><style>"
         L":root{--a:#14A39A;--b:#2F7DF6;--t:#18202A;--m:#6A7684;--bd:#DCE2E8;--s:#F4F6F9;--ok:#169E6B;--warn:#D98A00;--bad:#E0484D;--info:#2F7DF6}"
         L"*{box-sizing:border-box}body{margin:0;background:linear-gradient(180deg,#EEF2F5,#D9E1E8);font-family:'Segoe UI',Arial,sans-serif;color:var(--t);font-size:14px}"
         L".wrap{max-width:980px;margin:28px auto;background:#fff;border-radius:18px;box-shadow:0 10px 40px rgba(20,30,40,.12);overflow:hidden;border:1px solid var(--bd)}"
         L".top{height:5px;background:linear-gradient(90deg,var(--a),var(--b))}"
         L"header{display:flex;align-items:center;gap:16px;padding:26px 36px 8px}header img{width:46px;height:46px}"
         L".brand b{font-size:20px;letter-spacing:.5px}.brand div{color:var(--a);font-size:11px;font-weight:600;letter-spacing:1px}"
         L".date{margin-left:auto;text-align:right;color:var(--m);font-size:13px}"
         L"h1{font-size:26px;margin:18px 36px 4px}h2{font-size:17px;margin:30px 36px 12px}.sub{color:var(--m);margin:0 36px}"
         L".hero{display:flex;gap:18px;margin:20px 36px}.score{flex:0 0 220px;border:1px solid var(--bd);border-radius:14px;padding:18px;text-align:center}"
         L".score .n{font-size:46px;font-weight:700}.score .v{font-weight:600;margin-top:4px}"
         L".kv{flex:1;border:1px solid var(--bd);border-radius:14px;padding:14px 18px;display:grid;grid-template-columns:150px 1fr;gap:6px 12px;align-content:start}"
         L".kv span{color:var(--m)}.kv b{font-weight:600}"
         L".item{display:flex;gap:12px;align-items:flex-start;margin:0 36px 8px;padding:12px 16px;border-radius:12px;background:var(--s);border-left:4px solid var(--m)}"
         L".item .d{color:var(--m);font-size:13px;margin-top:2px}.item.ok{border-color:var(--ok)}.item.warn{border-color:var(--warn)}.item.bad{border-color:var(--bad)}.item.info{border-color:var(--info)}"
         L"table{width:calc(100% - 72px);margin:0 36px;border-collapse:collapse}th{background:var(--s);color:var(--m);font-size:11px;letter-spacing:.5px;text-align:left;padding:9px 10px}"
         L"td{padding:9px 10px;border-bottom:1px solid var(--s)}.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.info{color:var(--info)}"
         L".pill{display:inline-block;padding:2px 10px;border-radius:20px;font-weight:600;font-size:12px;background:var(--s)}"
         L".bar{height:7px;border-radius:4px;background:#EAEEF2;overflow:hidden;min-width:120px}.bar i{display:block;height:100%;background:linear-gradient(90deg,var(--a),var(--b))}"
         L".note{margin:0 36px;padding:14px 18px;border-radius:12px;background:linear-gradient(90deg,rgba(20,163,154,.08),rgba(47,125,246,.08))}"
         L"footer{margin-top:30px;padding:18px 36px 26px;color:var(--m);font-size:12px;border-top:1px solid var(--s);display:flex;justify-content:space-between}"
         L"@media print{body{background:#fff}.wrap{box-shadow:none;margin:0;border:0}}"
         L"</style></head><body><div class=\"wrap\"><div class=\"top\"></div><header>";
    wstring logo = LogoBase64();
    if (!logo.empty()) h += L"<img src=\"data:image/png;base64," + logo + L"\" alt=\"\">";
    h += L"<div class=\"brand\"><b>MagicTool</b><div>ОТЧЁТ О СОСТОЯНИИ КОМПЬЮТЕРА</div></div><div class=\"date\">" + date;
    if (!o.master.empty()) h += L"<br>Мастер: <b>" + H(o.master) + L"</b>";
    if (!o.contact.empty()) h += L"<br>" + H(o.contact);
    h += L"</div></header>";
    h += L"<h1>" + H(si.computerName) + L"</h1><p class=\"sub\">" + H(si.osName + L" " + si.osDisplay + L" · " + si.cpuName) + L"</p>";

    h += L"<div class=\"hero\"><div class=\"score\"><div class=\"n " + scoreCls + L"\">" + std::to_wstring(score) + L"</div><div style=\"color:#6A7684\">из 100</div>"
         L"<div class=\"v " + scoreCls + L"\">" + verdict + L"</div></div><div class=\"kv\">";
    auto kv = [&](const wstring& k, const wstring& v) { if (!Trim(v).empty()) h += L"<span>" + H(k) + L"</span><b>" + H(v) + L"</b>"; };
    kv(L"Компьютер", Trim(si.manufacturer + L" " + si.model));
    kv(L"Windows", si.osName + L" " + si.osDisplay + L" (сборка " + si.osBuild + L")");
    kv(L"Активация", LicenseStatusText(si.licenseStatus));
    kv(L"Процессор", si.cpuName);
    kv(L"Оперативная память", HumanSize(si.ramTotal));
    for (auto& g : si.gpus) kv(L"Видеокарта", g.name);
    kv(L"Материнская плата", Trim(si.boardMaker + L" " + si.boardModel));
    h += L"</div></div>";

    if (!o.note.empty()) h += L"<h2>Комментарий мастера</h2><div class=\"note\">" + H(o.note) + L"</div>";

    if (o.findings) {
        h += L"<h2>Результаты диагностики</h2>";
        if (f.empty()) h += L"<div class=\"item ok\"><div><b>Серьёзных проблем не найдено</b><div class=\"d\">Компьютер в хорошем состоянии.</div></div></div>";
        for (auto& x : f)
            h += wstring(L"<div class=\"item ") + SevClass(x.sev) + L"\"><div><b>" + H(x.title) + L"</b><div class=\"d\">" + H(x.text) + L"</div></div></div>";
    }

    if (o.disks) {
        h += L"<h2>Накопители</h2><table><tr><th>ДИСК</th><th>ТИП</th><th>ОБЪЁМ</th><th>СОСТОЯНИЕ</th><th>ТЕМПЕРАТУРА</th><th>НАРАБОТКА</th></tr>";
        for (auto& d : si.disks) {
            wstring res = d.health.healthPct >= 0 ? Fmt(L" · ресурс %d%%", d.health.healthPct) : L"";
            h += L"<tr><td><b>" + H(d.model) + L"</b>" + (d.isSystem ? L" <span class=\"pill\">системный</span>" : L"") + L"</td><td>" + H(d.kind()) + L"</td><td>" +
                 HumanSize(d.size) + L"</td><td class=\"" + SevClass(d.health.status) + L"\"><b>" + H(d.health.statusText) + L"</b>" + res + L"</td><td>" +
                 (d.health.tempC > 0 ? Fmt(L"%d °C", d.health.tempC) : L"—") + L"</td><td>" +
                 (d.health.powerOnHours >= 0 ? HumanHours((uint64_t)d.health.powerOnHours) : L"—") + L"</td></tr>";
        }
        h += L"</table><h2>Разделы</h2><table><tr><th>ДИСК</th><th>ЗАПОЛНЕНИЕ</th><th>СВОБОДНО</th><th>ВСЕГО</th></tr>";
        for (auto& v : si.volumes) {
            int used = v.total ? (int)(100 - v.free * 100 / v.total) : 0;
            h += L"<tr><td><b>" + H(v.letter) + L"</b> " + H(v.label) + L"</td><td><div class=\"bar\"><i style=\"width:" + std::to_wstring(used) + L"%\"></i></div></td><td>" +
                 HumanSize(v.free) + L"</td><td>" + HumanSize(v.total) + L"</td></tr>";
        }
        h += L"</table>";
    }

    if (o.hardware) {
        h += L"<h2>Оборудование</h2><table><tr><th>КОМПОНЕНТ</th><th>ХАРАКТЕРИСТИКИ</th></tr>";
        auto row = [&](const wstring& k, const wstring& v) { if (!Trim(v).empty()) h += L"<tr><td>" + H(k) + L"</td><td>" + H(v) + L"</td></tr>"; };
        row(L"Процессор", si.cpuName + Fmt(L" · %d ядер / %d потоков", si.cpuCores, si.cpuThreads));
        for (auto& m : si.ram) row(L"Память · " + m.slot, HumanSize(m.size) + L" " + m.type + (m.speed ? Fmt(L" %d МГц", m.speed) : L"") + L" " + m.maker + L" " + m.part);
        if (si.ramSlots) row(L"Слоты памяти", Fmt(L"занято %d из %d", (int)si.ram.size(), si.ramSlots));
        for (auto& g : si.gpus) row(L"Видеокарта", g.name + (g.vram ? L" · " + HumanSize(g.vram) : L"") + (g.driverVer.empty() ? L"" : L" · драйвер " + g.driverVer + L" от " + g.driverDate));
        row(L"Материнская плата", si.boardMaker + L" " + si.boardModel);
        row(L"BIOS", si.biosVer + L" от " + si.biosDate + (si.uefi ? L" · UEFI" : L" · Legacy"));
        row(L"TPM / Secure Boot", si.tpm + L" / " + (si.secureBoot ? L"включена" : L"выключена"));
        if (si.battery.present) row(L"Батарея", si.battery.wearPct >= 0 ? Fmt(L"износ %d%%", si.battery.wearPct) : L"есть");
        h += L"</table>";
    }

    if (o.journal) {
        vector<JEntry> done;
        for (auto& e : Journal::All()) if (e.thisSession) done.push_back(e);
        if (!done.empty()) {
            h += L"<h2>Что сделано</h2><table><tr><th>ВРЕМЯ</th><th>ДЕЙСТВИЕ</th><th>ПОДРОБНОСТИ</th></tr>";
            for (auto& e : done)
                h += L"<tr><td style=\"white-space:nowrap\">" + H(e.time) + L"</td><td><b>" + H(e.title) + L"</b>" + (e.undone ? L" <span class=\"pill\">отменено</span>" : L"") +
                     L"</td><td style=\"color:#6A7684\">" + H(e.detail) + L"</td></tr>";
            h += L"</table>";
        }
    }
    h += L"<footer><span>Сформировано программой MagicTool " APP_VERSION L"</span><span>" + date + L"</span></footer></div></body></html>";
    return WriteFileUtf8(path, WToUtf8(h), true);
}
