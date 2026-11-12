#pragma once
#include "common.h"
#include "sysinfo.h"

using namespace Gdiplus;

struct Palette {
    Color bg1, bg2, side, card, surface, surfaceHover, border, text, muted,
          accent, accent2, accentSoft, accentSoft2, onAccent,
          danger, dangerSoft, warn, warnSoft, ok, okSoft, info, infoSoft, shadow, console, consoleText;
};
extern Palette P;
extern bool g_dark;
void  SetTheme(bool dark);
Color Blend(Color a, Color b, float t);
Color Alpha(Color c, int a);
Color SevColor(Sev s);
Color SevSoft(Sev s);

namespace Ico {
    constexpr const wchar_t* Overview = L"\xEC4A";
    constexpr const wchar_t* Chip = L"\xE950";
    constexpr const wchar_t* Disk = L"\xEDA2";
    constexpr const wchar_t* Warning = L"\xE7BA";
    constexpr const wchar_t* Chart = L"\xE9D2";
    constexpr const wchar_t* Broom = L"\xEA99";
    constexpr const wchar_t* Bolt = L"\xE945";
    constexpr const wchar_t* Gears = L"\xE9F5";
    constexpr const wchar_t* Sliders = L"\xE9E9";
    constexpr const wchar_t* Apps = L"\xE71D";
    constexpr const wchar_t* Wrench = L"\xE90F";
    constexpr const wchar_t* Globe = L"\xE774";
    constexpr const wchar_t* Key = L"\xE8D7";
    constexpr const wchar_t* Shield = L"\xEA18";
    constexpr const wchar_t* Report = L"\xE9F9";
    constexpr const wchar_t* Undo = L"\xE7A7";
    constexpr const wchar_t* Sun = L"\xE706";
    constexpr const wchar_t* Moon = L"\xE708";
    constexpr const wchar_t* Min = L"\xE921";
    constexpr const wchar_t* Max = L"\xE922";
    constexpr const wchar_t* Restore = L"\xE923";
    constexpr const wchar_t* Close = L"\xE8BB";
    constexpr const wchar_t* Refresh = L"\xE72C";
    constexpr const wchar_t* Check = L"\xE73E";
    constexpr const wchar_t* Info = L"\xE946";
    constexpr const wchar_t* Error = L"\xEA39";
    constexpr const wchar_t* Done = L"\xE930";
    constexpr const wchar_t* Folder = L"\xE8B7";
    constexpr const wchar_t* Open = L"\xE8E5";
    constexpr const wchar_t* Save = L"\xE74E";
    constexpr const wchar_t* Copy = L"\xE8C8";
    constexpr const wchar_t* Search = L"\xE721";
    constexpr const wchar_t* Play = L"\xE768";
    constexpr const wchar_t* Download = L"\xE896";
    constexpr const wchar_t* Delete = L"\xE74D";
    constexpr const wchar_t* Admin = L"\xE7EF";
    constexpr const wchar_t* Lock = L"\xE72E";
    constexpr const wchar_t* Thermo = L"\xE9CA";
    constexpr const wchar_t* Heart = L"\xE95E";
    constexpr const wchar_t* Memory = L"\xE964";
    constexpr const wchar_t* Monitor = L"\xE7F4";
    constexpr const wchar_t* Battery = L"\xE83F";
    constexpr const wchar_t* Power = L"\xE7E8";
    constexpr const wchar_t* Remote = L"\xE8AF";
    constexpr const wchar_t* Bug = L"\xEBE8";
    constexpr const wchar_t* Cancel = L"\xE711";
    constexpr const wchar_t* Chevron = L"\xE76C";
    constexpr const wchar_t* ChevronDown = L"\xE70D";
    constexpr const wchar_t* ChevronUp = L"\xE70E";
    constexpr const wchar_t* Network = L"\xE968";
    constexpr const wchar_t* Wifi = L"\xE701";
    constexpr const wchar_t* Ethernet = L"\xE839";
    constexpr const wchar_t* Stop = L"\xE71A";
    constexpr const wchar_t* Link = L"\xE71B";
    constexpr const wchar_t* Speed = L"\xEC4A";
    constexpr const wchar_t* Cpu = L"\xE950";
}

enum class Btn { Primary, Secondary, Danger, Ghost, Soft };

Font* FontUI(float size, int weight = 0);
Font* FontIcon(float size);
Font* FontMono(float size);

enum TextFlags {
    T_LEFT = 0, T_CENTER = 1, T_RIGHT = 2,
    T_TOP = 0, T_VCENTER = 4, T_BOTTOM = 8,
    T_WRAP = 16, T_ELLIPSIS = 32,
};

struct Hit {
    RectF r;
    int layer;
    std::function<void()> fn;
    int cursor;
    bool caption;
};

struct ScrollArea { RectF view; float* pos; float content; int layer; float barY = 0, barH = 0; };

struct UICtx {
    Graphics* g = nullptr;
    float W = 0, H = 0;
    float mx = -1, my = -1;
    bool  lmb = false;
    int   layer = 0;
    int   activeLayer = 0;
    float offX = 0, offY = 0;
    vector<RectF> clips;
    vector<Hit> hits;
    vector<RectF> hovers;
    vector<ScrollArea> scrolls;
    vector<int> scrollStack;
    float* dragPos = nullptr;
    float dragStartY = 0, dragStartPos = 0, dragRatio = 0;
    bool animating = false;
    DWORD tick = 0;
    int   focusId = 0;
    wstring* focusStr = nullptr;
    size_t focusMax = 0;
    int   pressedHit = -1;
    RectF pressedRect;
    wstring tooltip;
};
extern UICtx ui;

void BeginFrame(Graphics* g, float W, float H);
void EndFrame();

void PushClip(RectF r);
void PopClip();
void BeginScroll(RectF view, float* pos);
void EndScroll(float contentHeight);
bool Visible(RectF r);

RectF ToWindow(RectF r);
bool Hover(RectF r);
bool Pressed(RectF r);
void OnClick(RectF r, std::function<void()> fn, int cursor = 0, bool caption = false);

void RoundPath(GraphicsPath& p, RectF r, float rad);
void FillRound(RectF r, float rad, Color c);
void FillRoundBrush(RectF r, float rad, Brush* b);
void StrokeRound(RectF r, float rad, Color c, float w = 1.0f);
void FillRect(RectF r, Color c);
void GradientH(RectF r, float rad, Color a, Color b);
void Shadow(RectF r, float rad, float spread = 10);
void Text(const wstring& s, RectF r, Font* f, Color c, int flags = T_LEFT | T_TOP);
float TextW(const wstring& s, Font* f);
float TextH(const wstring& s, Font* f, float width);
void Glyph(const wchar_t* g, RectF r, float size, Color c);
void Line(float x1, float y1, float x2, float y2, Color c, float w = 1.0f);

void  Card(RectF r, bool hover = false, float rad = 12);
void  Button(RectF r, const wstring& text, std::function<void()> fn, Btn style = Btn::Primary,
             const wchar_t* icon = nullptr, bool enabled = true);
float ButtonW(const wstring& text, const wchar_t* icon = nullptr, float fontSize = 13);

float ButtonRight(float right, float y, float h, const wstring& text, std::function<void()> fn,
                  Btn style = Btn::Secondary, const wchar_t* icon = nullptr, bool enabled = true);
void  IconButton(RectF r, const wchar_t* glyph, std::function<void()> fn, Color c, float size = 14,
                 bool danger = false, const wstring& tip = L"");
void  CheckBox(RectF box, bool on, bool enabled = true);
void  Switch(RectF r, bool on, std::function<void()> fn, bool enabled = true);
float Badge(float x, float y, const wstring& text, Sev s, float h = 22, bool rightAlign = false);
void  Progress(RectF r, float frac, Color c, bool gradient = false);
void  Spinner(float cx, float cy, float rad, Color c);
void  Ring(float cx, float cy, float rad, float thick, float frac, Color c, bool gradient);
void  TextBox(int id, RectF r, wstring& value, const wchar_t* placeholder, size_t maxLen = 200, bool mono = false);
void  SevIcon(Sev s, float x, float y, float size);
float SectionTitle(float x, float y, float w, const wstring& title, const wstring& sub = L"");
void  KeyValue(float x, float y, float w, const wstring& k, const wstring& v, float kw = 150, Color vc = Color(0, 0, 0, 0));
void  Tooltip(RectF r, const wstring& tip);

struct ModalBtn { wstring label; Btn style; std::function<void()> fn; };
void Confirm(const wstring& title, const wstring& text, const wstring& okLabel, std::function<void()> onOk,
             bool danger = false, Sev icon = Sev::Warn);
void Message(const wstring& title, const wstring& text, Sev icon = Sev::Info);
void ModalCustom(const wstring& title, const wstring& text, vector<ModalBtn> buttons, Sev icon);
bool ModalOpen();
void CloseModal();
void ModalPrimary();
void DrawModal();
void Toast(const wstring& text, Sev s = Sev::Ok);
void DrawToasts();

void DrawFileIcon(const wstring& iconRef, RectF r, const wchar_t* fallback);

void UI_KeyChar(wchar_t c);
void UI_KeyDown(WPARAM vk);
