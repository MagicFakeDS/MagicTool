#pragma once
#include "common.h"

struct WVal {
    bool null = true;
    wstring s;
    double n = 0;
    vector<double> arr;
    vector<wstring> sarr;
};
using WRow = std::map<wstring, WVal>;

vector<WRow> WmiQuery(const wchar_t* ns, const wstring& wql);

wstring  WStr(const WRow& r, const wchar_t* key);
double   WNum(const WRow& r, const wchar_t* key, double def = 0);
bool     WHas(const WRow& r, const wchar_t* key);
wstring  WmiDate(const wstring& cim);
