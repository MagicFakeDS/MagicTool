#include "wmi.h"
#include <wbemidl.h>
#include <oleauto.h>

static void FillVal(WVal& v, VARIANT& var) {
    v.null = (var.vt == VT_NULL || var.vt == VT_EMPTY);
    if (v.null) return;
    if (var.vt & VT_ARRAY) {
        SAFEARRAY* sa = var.parray;
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(sa, 1, &lo);
        SafeArrayGetUBound(sa, 1, &hi);
        VARTYPE vt = var.vt & ~VT_ARRAY;
        for (LONG i = lo; i <= hi; ++i) {
            if (vt == VT_BSTR) {
                BSTR b = nullptr;
                if (SUCCEEDED(SafeArrayGetElement(sa, &i, &b)) && b) { v.sarr.push_back(b); SysFreeString(b); }
            } else {
                VARIANT el; VariantInit(&el);
                el.vt = vt;
                if (SUCCEEDED(SafeArrayGetElement(sa, &i, &el.bVal))) {
                    VARIANT d; VariantInit(&d);
                    if (SUCCEEDED(VariantChangeType(&d, &el, 0, VT_R8))) v.arr.push_back(d.dblVal);
                }
            }
        }
        for (auto& x : v.sarr) { if (!v.s.empty()) v.s += L", "; v.s += x; }
        return;
    }
    if (var.vt == VT_BSTR) {
        v.s = var.bstrVal ? var.bstrVal : L"";
        wchar_t* end = nullptr;
        double d = wcstod(v.s.c_str(), &end);
        if (end && *end == 0 && !v.s.empty()) v.n = d;
        return;
    }
    if (var.vt == VT_BOOL) {
        v.n = var.boolVal ? 1 : 0;
        v.s = var.boolVal ? L"True" : L"False";
        return;
    }
    VARIANT d; VariantInit(&d);
    if (SUCCEEDED(VariantChangeType(&d, &var, 0, VT_R8))) {
        v.n = d.dblVal;
        v.s = Fmt(L"%.0f", v.n);
        if (v.n != (double)(long long)v.n) v.s = Fmt(L"%g", v.n);
    }
}

vector<WRow> WmiQuery(const wchar_t* ns, const wstring& wql) {
    vector<WRow> rows;
    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&loc)))
        return rows;
    IWbemServices* svc = nullptr;
    BSTR bns = SysAllocString(ns);
    HRESULT hr = loc->ConnectServer(bns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(bns);
    if (SUCCEEDED(hr)) {
        CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        IEnumWbemClassObject* en = nullptr;
        BSTR lang = SysAllocString(L"WQL");
        BSTR q = SysAllocString(wql.c_str());
        hr = svc->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
        SysFreeString(lang);
        SysFreeString(q);
        if (SUCCEEDED(hr) && en) {
            while (true) {
                IWbemClassObject* obj = nullptr;
                ULONG got = 0;
                if (en->Next(15000, 1, &obj, &got) != WBEM_S_NO_ERROR || !got) break;
                WRow row;
                obj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
                BSTR name = nullptr;
                VARIANT var;
                VariantInit(&var);
                while (obj->Next(0, &name, &var, nullptr, nullptr) == WBEM_S_NO_ERROR) {
                    WVal v;
                    FillVal(v, var);
                    row[name] = v;
                    SysFreeString(name);
                    VariantClear(&var);
                }
                obj->EndEnumeration();
                obj->Release();
                rows.push_back(std::move(row));
            }
            en->Release();
        }
        svc->Release();
    }
    loc->Release();
    return rows;
}

wstring WStr(const WRow& r, const wchar_t* key) {
    auto it = r.find(key);
    return it == r.end() || it->second.null ? L"" : Trim(it->second.s);
}
double WNum(const WRow& r, const wchar_t* key, double def) {
    auto it = r.find(key);
    return it == r.end() || it->second.null ? def : it->second.n;
}
bool WHas(const WRow& r, const wchar_t* key) {
    auto it = r.find(key);
    return it != r.end() && !it->second.null;
}
wstring WmiDate(const wstring& c) {
    if (c.size() < 8) return c;
    return c.substr(6, 2) + L"." + c.substr(4, 2) + L"." + c.substr(0, 4);
}
