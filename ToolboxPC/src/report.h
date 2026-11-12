#pragma once
#include "app.h"

struct ReportOptions {
    wstring master, contact, note;
    bool hardware = true, disks = true, findings = true, journal = true;
};
wstring DefaultReportPath();
bool    WriteReport(const wstring& path, const ReportOptions& o);
