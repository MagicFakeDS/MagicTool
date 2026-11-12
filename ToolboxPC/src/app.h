#pragma once
#include "ui.h"

struct Page {
    PageId id;
    const wchar_t* title;
    const wchar_t* sub;
    const wchar_t* icon;
    float scroll = 0;
    Page(PageId i, const wchar_t* t, const wchar_t* s, const wchar_t* ic) : id(i), title(t), sub(s), icon(ic) {}
    virtual ~Page() {}
    virtual void  OnShow() {}
    virtual void  Header(float right, float y) {}
    virtual float Body(float x, float y, float w) = 0;
    virtual bool  Tick() { return false; }
    virtual wstring NavBadge(Sev& s) { return L""; }
};

Page* MakeOverview();
Page* MakeHardware();
Page* MakeDisks();
Page* MakeDevices();
Page* MakeProcesses();
Page* MakeCleanup();
Page* MakeStartup();
Page* MakeServices();
Page* MakeTweaks();
Page* MakeApps();
Page* MakeRepair();
Page* MakeNetwork();
Page* MakeActivation();
Page* MakeSecurity();
Page* MakeJournal();

struct AppState {
    HWND hwnd = nullptr;
    int page = PG_OVERVIEW;
    vector<Page*> pages;
    SysInfo si;
    bool siLoading = false;
    vector<ProblemDevice> devs;
    CrashInfo crashes;
    bool devsLoaded = false, devsLoading = false;
    LiveStats live;
    bool elevated = false;
    bool locked = false;
    Bitmap* logo = nullptr;
};

bool RelaunchAsAdmin();

void ToggleOverlay();
bool OverlayOn();
void CycleOverlayCorner();
extern AppState app;

void Navigate(int page);
void RequestSysInfo(bool force = false);
void RequestDevices(bool force = false);
void Redraw();

struct Task {
    wstring name;
    int page = -1;
    vector<wstring> lines;
    bool running = true;
    bool ok = true;
    wstring summary;
    DWORD t0 = 0, t1 = 0;
    std::atomic<bool> cancel{ false };
};
struct TaskCtx {
    std::shared_ptr<Task> t;
    void Line(const wstring& s);
    int  Run(const wstring& cmd, Enc enc = Enc::Auto);
    int  PS(const wstring& script);
    bool Cancelled() { return t->cancel.load(); }
    void Fail(const wstring& summary);
    void Done(const wstring& summary);
};
std::shared_ptr<Task> StartTask(const wstring& name, int page, std::function<void(TaskCtx&)> body,
                                std::function<void(Task&)> onDone = nullptr);
bool TaskBusy();
std::shared_ptr<Task> CurrentTask();
std::shared_ptr<Task> LastTaskFor(int page);

float ConsoleBox(float x, float y, float w, float h, int page);
bool  GuardBusy();

float Loading(float x, float y, float w, const wstring& text);
float Banner(float x, float y, float w, Sev s, const wstring& title, const wstring& text,
             const wstring& action = L"", std::function<void()> fn = nullptr);
void  StatTile(RectF r, const wchar_t* icon, const wstring& label, const wstring& value, const wstring& sub,
               float frac, Color c);
float ActionCard(RectF r, const wchar_t* icon, const wstring& title, const wstring& text,
                 const wstring& btn, std::function<void()> fn, Btn style = Btn::Secondary, bool enabled = true,
                 const wstring& badge = L"", Sev badgeSev = Sev::Info);
float TableHeader(float x, float y, float w, const vector<std::pair<wstring, float>>& cols);
wstring ShortPath(const wstring& p, size_t max = 70);
void  OpenSystemTool(const wstring& cmd, const wstring& args = L"");

void  StartRestorePoint(int page);
float RestorePointBanner(float x, float y, float w, int page);
void  CopyText(const wstring& s, const wstring& toast = L"Скопировано");
