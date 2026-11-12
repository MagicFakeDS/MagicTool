#include "ui.h"
#include <cmath>

UICtx ui;
Palette P;
bool g_dark = false;

static float g_scale = 1.0f;
static std::map<float*, float> g_scrollContent;

static Color Hex(unsigned v, int a = 255) { return Color((BYTE)a, (BYTE)(v >> 16), (BYTE)(v >> 8), (BYTE)v); }
Color Blend(Color a, Color b, float t) {
    return Color((BYTE)(a.GetA() * t + b.GetA() * (1 - t)), (BYTE)(a.GetR() * t + b.GetR() * (1 - t)),
                 (BYTE)(a.GetG() * t + b.GetG() * (1 - t)), (BYTE)(a.GetB() * t + b.GetB() * (1 - t)));
}
Color Alpha(Color c, int a) { return Color((BYTE)a, c.GetR(), c.GetG(), c.GetB()); }

void SetTheme(bool dark) {
    g_dark = dark;
    Color a = Hex(0x14A39A), a2 = Hex(0x2F7DF6);
    if (!dark) {
        P.bg1 = Hex(0xEEF2F5); P.bg2 = Hex(0xD9E1E8); P.side = Hex(0xFFFFFF, 0);
        P.card = Hex(0xFFFFFF); P.surface = Hex(0xF4F6F9); P.surfaceHover = Hex(0xEAEEF2);
        P.border = Hex(0xDCE2E8); P.text = Hex(0x18202A); P.muted = Hex(0x6A7684);
        P.accent = a; P.accent2 = a2;
        P.accentSoft = Blend(a, Hex(0xFFFFFF), 0.11f); P.accentSoft2 = Blend(a2, Hex(0xFFFFFF), 0.10f);
        P.onAccent = Hex(0xFFFFFF);
        P.danger = Hex(0xE0484D); P.dangerSoft = Hex(0xFDECEC);
        P.warn = Hex(0xD98A00); P.warnSoft = Hex(0xFFF4DD);
        P.ok = Hex(0x169E6B); P.okSoft = Hex(0xE3F6EE);
        P.info = a2; P.infoSoft = Blend(a2, Hex(0xFFFFFF), 0.10f);
        P.shadow = Color(22, 20, 30, 40);
        P.console = Hex(0x111820); P.consoleText = Hex(0xCFE3DD);
    } else {
        Color card = Hex(0x171D24);
        P.bg1 = Hex(0x0A0E13); P.bg2 = Hex(0x141B24); P.side = Hex(0x000000, 0);
        P.card = card; P.surface = Hex(0x1F262E); P.surfaceHover = Hex(0x29313B);
        P.border = Hex(0x2F3943); P.text = Hex(0xE8EDF2); P.muted = Hex(0x8F9BA8);
        P.accent = a; P.accent2 = a2;
        P.accentSoft = Blend(a, card, 0.22f); P.accentSoft2 = Blend(a2, card, 0.22f);
        P.onAccent = Hex(0xFFFFFF);
        P.danger = Hex(0xF0565B); P.dangerSoft = Blend(Hex(0xF0565B), card, 0.16f);
        P.warn = Hex(0xF2A93B); P.warnSoft = Blend(Hex(0xF2A93B), card, 0.15f);
        P.ok = Hex(0x2EC287); P.okSoft = Blend(Hex(0x2EC287), card, 0.15f);
        P.info = Hex(0x5B9BFF); P.infoSoft = Blend(Hex(0x5B9BFF), card, 0.16f);
        P.shadow = Color(70, 0, 0, 0);
        P.console = Hex(0x0B1015); P.consoleText = Hex(0xCFE3DD);
    }
}

Color SevColor(Sev s) {
    switch (s) {
        case Sev::Ok: return P.ok;
        case Sev::Info: return P.info;
        case Sev::Warn: return P.warn;
        case Sev::Bad: return P.danger;
        default: return P.muted;
    }
}
Color SevSoft(Sev s) {
    switch (s) {
        case Sev::Ok: return P.okSoft;
        case Sev::Info: return P.infoSoft;
        case Sev::Warn: return P.warnSoft;
        case Sev::Bad: return P.dangerSoft;
        default: return P.surface;
    }
}

static wstring g_iconFamily;
Font* FontUI(float size, int weight) {
    static std::map<std::pair<int, int>, Font*> cache;
    auto key = std::make_pair((int)(size * 10), weight);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Font* f;
    if (weight == 1) f = new Font(L"Segoe UI Semibold", size, FontStyleRegular, UnitPixel);
    else if (weight == 2) f = new Font(L"Segoe UI", size, FontStyleBold, UnitPixel);
    else f = new Font(L"Segoe UI", size, FontStyleRegular, UnitPixel);
    if (f->GetLastStatus() != Ok) { delete f; f = new Font(L"Segoe UI", size, weight ? FontStyleBold : FontStyleRegular, UnitPixel); }
    cache[key] = f;
    return f;
}
Font* FontIcon(float size) {
    static std::map<int, Font*> cache;
    if (g_iconFamily.empty()) {
        FontFamily fl(L"Segoe Fluent Icons");
        g_iconFamily = fl.GetLastStatus() == Ok && fl.IsAvailable() ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }
    int k = (int)(size * 10);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    Font* f = new Font(g_iconFamily.c_str(), size, FontStyleRegular, UnitPixel);
    cache[k] = f;
    return f;
}
Font* FontMono(float size) {
    static std::map<int, Font*> cache;
    int k = (int)(size * 10);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    Font* f = new Font(L"Consolas", size, FontStyleRegular, UnitPixel);
    cache[k] = f;
    return f;
}

static void ApplyTransform() {
    Matrix m(g_scale, 0, 0, g_scale, ui.offX * g_scale, ui.offY * g_scale);
    ui.g->SetTransform(&m);
    RectF c = ui.clips.back();
    ui.g->SetClip(RectF(c.X - ui.offX, c.Y - ui.offY, c.Width, c.Height));
}

void BeginFrame(Graphics* g, float W, float H) {
    ui.g = g;
    ui.W = W; ui.H = H;
    REAL el[6];
    Matrix m; g->GetTransform(&m); m.GetElements(el);
    g_scale = el[0] > 0 ? el[0] : 1.0f;
    ui.hits.clear();
    ui.hovers.clear();
    ui.scrolls.clear();
    ui.scrollStack.clear();
    ui.clips.assign(1, RectF(0, 0, W, H));
    ui.offX = ui.offY = 0;
    ui.layer = 0;
    ui.activeLayer = ModalOpen() ? 1 : 0;
    ui.animating = false;
    ui.tick = GetTickCount();
    ui.tooltip.clear();
    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetPixelOffsetMode(PixelOffsetModeHalf);
    g->SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g->SetInterpolationMode(InterpolationModeHighQualityBicubic);
    ApplyTransform();
}

void PushClip(RectF r) {
    RectF w(r.X + ui.offX, r.Y + ui.offY, r.Width, r.Height);
    RectF top = ui.clips.back();
    RectF::Intersect(w, w, top);
    if (w.Width < 0) w.Width = 0;
    if (w.Height < 0) w.Height = 0;
    ui.clips.push_back(w);
    ApplyTransform();
}
void PopClip() {
    if (ui.clips.size() > 1) ui.clips.pop_back();
    ApplyTransform();
}

RectF ToWindow(RectF r) { return RectF(r.X + ui.offX, r.Y + ui.offY, r.Width, r.Height); }

bool Visible(RectF r) {
    RectF w = ToWindow(r), c = ui.clips.back();
    return w.GetRight() > c.X && w.X < c.GetRight() && w.GetBottom() > c.Y && w.Y < c.GetBottom();
}

void BeginScroll(RectF view, float* pos) {
    float content = g_scrollContent.count(pos) ? g_scrollContent[pos] : 0;
    float maxPos = std::max(0.0f, content - view.Height);
    if (*pos > maxPos) *pos = maxPos;
    if (*pos < 0) *pos = 0;
    ui.scrolls.push_back({ ToWindow(view), pos, content, ui.layer });
    ui.scrollStack.push_back((int)ui.scrolls.size() - 1);
    PushClip(view);
    ui.offY -= *pos;
    ApplyTransform();
}

void EndScroll(float contentHeight) {
    if (ui.scrollStack.empty()) return;
    int idx = ui.scrollStack.back();
    ui.scrollStack.pop_back();
    ScrollArea& sa = ui.scrolls[idx];
    ui.offY += *sa.pos;
    PopClip();
    g_scrollContent[sa.pos] = contentHeight;
    sa.content = contentHeight;
    RectF v(sa.view.X - ui.offX, sa.view.Y - ui.offY, sa.view.Width, sa.view.Height);
    if (contentHeight > v.Height + 1) {
        float trackH = v.Height - 8;
        float barH = std::max(30.0f, trackH * v.Height / contentHeight);
        float maxPos = contentHeight - v.Height;
        float y = v.Y + 4 + (trackH - barH) * (*sa.pos / maxPos);
        sa.barY = y + ui.offY;
        sa.barH = barH;
        bool active = ui.dragPos == sa.pos;
        bool nearBar = ui.mx >= sa.view.GetRight() - 14 && ui.mx < sa.view.GetRight() && ui.my >= sa.view.Y && ui.my < sa.view.GetBottom();
        float R = v.GetRight() - 3;
        float bw = (active || nearBar) ? 8.0f : 5.0f;

        FillRound(RectF(R - bw + (bw - 3) / 2, v.Y + 4, 3, trackH), 1.5f, Alpha(P.muted, g_dark ? 26 : 22));

        Color thumb = active ? P.accent : nearBar ? Blend(P.accent, P.muted, 0.5f) : Alpha(P.muted, g_dark ? 150 : 125);
        FillRound(RectF(R - bw, y, bw, barH), bw / 2, thumb);
        if (nearBar) ui.hovers.push_back(RectF(sa.view.GetRight() - 14, sa.view.Y, 14, sa.view.Height));
    }
}

bool Hover(RectF r) {
    if (ui.layer != ui.activeLayer) return false;
    RectF w = ToWindow(r), c = ui.clips.back();
    {
        RectF v = w;
        RectF::Intersect(v, v, c);
        if (v.Width > 0 && v.Height > 0) ui.hovers.push_back(v);
    }
    return ui.mx >= w.X && ui.mx < w.GetRight() && ui.my >= w.Y && ui.my < w.GetBottom() &&
           ui.mx >= c.X && ui.mx < c.GetRight() && ui.my >= c.Y && ui.my < c.GetBottom();
}
bool Pressed(RectF r) { return ui.lmb && Hover(r); }

void OnClick(RectF r, std::function<void()> fn, int cursor, bool caption) {
    RectF w = ToWindow(r), c = ui.clips.back();
    RectF::Intersect(w, w, c);
    if (w.Width <= 0 || w.Height <= 0) return;
    ui.hits.push_back({ w, ui.layer, std::move(fn), cursor, caption });
}

void RoundPath(GraphicsPath& p, RectF r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}
void FillRound(RectF r, float rad, Color c) {
    SolidBrush b(c);
    FillRoundBrush(r, rad, &b);
}
void FillRoundBrush(RectF r, float rad, Brush* b) {
    GraphicsPath p;
    RoundPath(p, r, rad);
    ui.g->FillPath(b, &p);
}
void StrokeRound(RectF r, float rad, Color c, float w) {
    GraphicsPath p;
    RectF rr(r.X + w / 2, r.Y + w / 2, r.Width - w, r.Height - w);
    RoundPath(p, rr, rad);
    Pen pen(c, w);
    ui.g->DrawPath(&pen, &p);
}
void FillRect(RectF r, Color c) {
    SolidBrush b(c);
    ui.g->FillRectangle(&b, r);
}
void GradientH(RectF r, float rad, Color a, Color b) {
    RectF gr(r.X - 1, r.Y, r.Width + 2, r.Height);
    LinearGradientBrush br(gr, a, b, LinearGradientModeHorizontal);
    FillRoundBrush(r, rad, &br);
}
void Shadow(RectF r, float rad, float spread) {
    int steps = 5;
    for (int i = steps; i >= 1; --i) {
        float e = spread * i / steps;
        int a = (int)(P.shadow.GetA() * (1.0f - (float)(i - 1) / steps) / 3.0f);
        FillRound(RectF(r.X - e * 0.6f, r.Y - e * 0.3f + spread * 0.25f, r.Width + e * 1.2f, r.Height + e * 1.1f), rad + e, Alpha(P.shadow, a));
    }
}
void Line(float x1, float y1, float x2, float y2, Color c, float w) {
    Pen p(c, w);
    ui.g->DrawLine(&p, x1, y1, x2, y2);
}

static StringFormat* MakeFormat(int flags) {
    static std::map<int, StringFormat*> cache;
    auto it = cache.find(flags);
    if (it != cache.end()) return it->second;
    StringFormat* f = StringFormat::GenericTypographic()->Clone();
    int ff = StringFormatFlagsMeasureTrailingSpaces;
    if (!(flags & T_WRAP)) ff |= StringFormatFlagsNoWrap;
    else if (flags & T_ELLIPSIS) ff |= StringFormatFlagsLineLimit;
    f->SetFormatFlags(ff);
    f->SetAlignment((flags & T_CENTER) ? StringAlignmentCenter : (flags & T_RIGHT) ? StringAlignmentFar : StringAlignmentNear);
    f->SetLineAlignment((flags & T_VCENTER) ? StringAlignmentCenter : (flags & T_BOTTOM) ? StringAlignmentFar : StringAlignmentNear);
    f->SetTrimming((flags & T_ELLIPSIS) ? StringTrimmingEllipsisCharacter : (flags & T_WRAP) ? StringTrimmingWord : StringTrimmingNone);
    cache[flags] = f;
    return f;
}

void Text(const wstring& s, RectF r, Font* f, Color c, int flags) {
    if (s.empty()) return;
    SolidBrush b(c);
    if (!(flags & T_WRAP) && !(flags & T_ELLIPSIS) && r.Width < 1) r.Width = 10000;
    ui.g->DrawString(s.c_str(), (INT)s.size(), f, r, MakeFormat(flags), &b);
}
float TextW(const wstring& s, Font* f) {
    if (s.empty()) return 0;
    RectF out;
    ui.g->MeasureString(s.c_str(), (INT)s.size(), f, PointF(0, 0), MakeFormat(0), &out);
    return out.Width;
}
float TextH(const wstring& s, Font* f, float width) {
    if (s.empty()) return 0;
    RectF out;
    ui.g->MeasureString(s.c_str(), (INT)s.size(), f, RectF(0, 0, width, 100000), MakeFormat(T_WRAP), &out);
    return out.Height;
}
void Glyph(const wchar_t* gl, RectF r, float size, Color c) {
    SolidBrush b(c);
    TextRenderingHint old = ui.g->GetTextRenderingHint();
    ui.g->SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    ui.g->DrawString(gl, (INT)wcslen(gl), FontIcon(size), r, MakeFormat(T_CENTER | T_VCENTER), &b);
    ui.g->SetTextRenderingHint(old);
}

void Card(RectF r, bool hover, float rad) {
    Shadow(r, rad, hover ? 14.0f : 10.0f);
    FillRound(r, rad, P.card);
    StrokeRound(r, rad, hover ? Blend(P.accent, P.border, 0.45f) : P.border, 1.0f);
}

float ButtonW(const wstring& text, const wchar_t* icon, float fs) {
    float w = TextW(text, FontUI(fs, 1)) + 32;
    if (icon) w += text.empty() ? 4 : 22;
    return std::max(w, text.empty() ? 36.0f : 84.0f);
}

void Button(RectF r, const wstring& text, std::function<void()> fn, Btn style, const wchar_t* icon, bool enabled) {
    bool hv = enabled && Hover(r);
    bool pr = hv && ui.lmb;
    float rad = 8;
    Color fg;
    if (!enabled) {
        FillRound(r, rad, P.surface);
        StrokeRound(r, rad, P.border);
        fg = Alpha(P.muted, 150);
    } else switch (style) {
        case Btn::Primary: {
            Color a = P.accent, b = P.accent2;
            if (hv) { a = Blend(a, Color(255, 255, 255, 255), 0.88f); b = Blend(b, Color(255, 255, 255, 255), 0.88f); }
            if (pr) { a = Blend(a, Color(255, 0, 0, 0), 0.9f); b = Blend(b, Color(255, 0, 0, 0), 0.9f); }
            GradientH(r, rad, a, b);
            fg = P.onAccent;
            break;
        }
        case Btn::Danger:
            FillRound(r, rad, hv ? P.danger : P.dangerSoft);
            fg = hv ? Color(255, 255, 255, 255) : P.danger;
            break;
        case Btn::Ghost:
            if (hv) FillRound(r, rad, P.surfaceHover);
            fg = P.accent;
            break;
        case Btn::Soft:
            FillRound(r, rad, hv ? Blend(P.accent, P.accentSoft, 0.25f) : P.accentSoft);
            fg = g_dark ? Blend(P.accent, Color(255, 255, 255, 255), 0.75f) : Blend(P.accent, Color(255, 0, 0, 0), 0.8f);
            break;
        default:
            FillRound(r, rad, hv ? P.surfaceHover : P.surface);
            StrokeRound(r, rad, hv ? Blend(P.accent, P.border, 0.35f) : P.border);
            fg = P.text;
    }
    Font* f = FontUI(13, 1);
    float tw = text.empty() ? 0 : TextW(text, f);
    float iw = icon ? 14 : 0;
    float gap = (icon && !text.empty()) ? 8 : 0;
    float x = r.X + (r.Width - tw - iw - gap) / 2;
    if (icon) Glyph(icon, RectF(x - 2, r.Y, iw + 4, r.Height), 13, fg);
    if (!text.empty()) Text(text, RectF(x + iw + gap, r.Y, tw + 2, r.Height), f, fg, T_LEFT | T_VCENTER);
    if (enabled && fn) OnClick(r, fn);
}

float ButtonRight(float right, float y, float h, const wstring& text, std::function<void()> fn, Btn style, const wchar_t* icon, bool enabled) {
    float w = ButtonW(text, icon);
    Button(RectF(right - w, y, w, h), text, fn, style, icon, enabled);
    return right - w;
}

void IconButton(RectF r, const wchar_t* glyph, std::function<void()> fn, Color c, float size, bool danger, const wstring& tip) {
    bool hv = Hover(r);
    if (hv) FillRound(r, 8, danger ? P.danger : P.surfaceHover);
    Glyph(glyph, r, size, hv && danger ? Color(255, 255, 255, 255) : c);
    if (fn) OnClick(r, fn);
    if (!tip.empty()) Tooltip(r, tip);
}

void CheckBox(RectF b, bool on, bool enabled) {
    if (on) {
        GradientH(b, 5, enabled ? P.accent : P.muted, enabled ? P.accent2 : P.muted);
        Glyph(Ico::Check, b, 11, Color(255, 255, 255, 255));
    } else {
        FillRound(b, 5, P.card);
        StrokeRound(b, 5, Hover(b) && enabled ? P.accent : Blend(P.muted, P.card, 0.6f), 1.3f);
    }
}

void Switch(RectF r, bool on, std::function<void()> fn, bool enabled) {
    bool hv = enabled && Hover(r);
    if (on) GradientH(r, r.Height / 2, enabled ? P.accent : P.muted, enabled ? P.accent2 : P.muted);
    else {
        FillRound(r, r.Height / 2, hv ? P.surfaceHover : P.surface);
        StrokeRound(r, r.Height / 2, Blend(P.muted, P.card, 0.55f), 1.2f);
    }
    float d = r.Height - 6;
    float x = on ? r.GetRight() - d - 3 : r.X + 3;
    SolidBrush kb(on ? Color(255, 255, 255, 255) : Blend(P.muted, P.card, 0.85f));
    ui.g->FillEllipse(&kb, x, r.Y + 3, d, d);
    if (enabled && fn) OnClick(r, fn);
}

float Badge(float x, float y, const wstring& text, Sev s, float h, bool rightAlign) {
    Font* f = FontUI(12, 1);
    float w = TextW(text, f) + 18;
    if (rightAlign) x -= w;
    FillRound(RectF(x, y, w, h), h / 2, SevSoft(s));
    Text(text, RectF(x, y, w, h), f, SevColor(s), T_CENTER | T_VCENTER);
    return w;
}

void Progress(RectF r, float frac, Color c, bool gradient) {
    frac = std::max(0.0f, std::min(1.0f, frac));
    FillRound(r, r.Height / 2, g_dark ? P.surfaceHover : P.surfaceHover);
    if (frac <= 0) return;
    RectF f(r.X, r.Y, std::max(r.Height, r.Width * frac), r.Height);
    if (gradient) GradientH(f, r.Height / 2, P.accent, P.accent2);
    else FillRound(f, r.Height / 2, c);
}

void Spinner(float cx, float cy, float rad, Color c) {
    ui.animating = true;
    float a = (float)((ui.tick / 3) % 360);
    Pen bg(Alpha(c, 50), rad * 0.28f);
    ui.g->DrawEllipse(&bg, cx - rad, cy - rad, rad * 2, rad * 2);
    Pen p(c, rad * 0.28f);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    ui.g->DrawArc(&p, cx - rad, cy - rad, rad * 2, rad * 2, a, 100);
}

void Ring(float cx, float cy, float rad, float thick, float frac, Color c, bool gradient) {
    Pen bg(g_dark ? P.surfaceHover : P.surfaceHover, thick);
    ui.g->DrawEllipse(&bg, cx - rad, cy - rad, rad * 2, rad * 2);
    if (frac <= 0) return;
    RectF br(cx - rad - thick, cy - rad - thick, rad * 2 + thick * 2, rad * 2 + thick * 2);
    LinearGradientBrush gb(br, P.accent, P.accent2, LinearGradientModeForwardDiagonal);
    Pen p(c, thick);
    if (gradient) p.SetBrush(&gb);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    ui.g->DrawArc(&p, cx - rad, cy - rad, rad * 2, rad * 2, -90, 360 * std::min(1.0f, frac));
}

void TextBox(int id, RectF r, wstring& value, const wchar_t* placeholder, size_t maxLen, bool mono) {
    bool focus = ui.focusId == id;
    bool hv = Hover(r);
    FillRound(r, 8, focus ? P.card : P.surface);
    StrokeRound(r, 8, focus ? P.accent : hv ? Blend(P.accent, P.border, 0.4f) : P.border, focus ? 1.6f : 1.0f);
    Font* f = mono ? FontMono(14) : FontUI(13.5f);
    RectF tr(r.X + 12, r.Y, r.Width - 24, r.Height);
    PushClip(tr);
    float tw = TextW(value, f);
    float shift = std::max(0.0f, tw - tr.Width + 4);
    if (value.empty() && placeholder && !focus) Text(placeholder, tr, FontUI(13.5f), P.muted, T_LEFT | T_VCENTER);
    else Text(value, RectF(tr.X - shift, tr.Y, tw + 10, tr.Height), f, P.text, T_LEFT | T_VCENTER);
    if (focus) {
        ui.animating = true;
        if ((ui.tick / 530) % 2 == 0) Line(tr.X - shift + tw + 1, r.Y + 9, tr.X - shift + tw + 1, r.GetBottom() - 9, P.accent, 1.4f);
        ui.focusStr = &value;
        ui.focusMax = maxLen;
    }
    PopClip();
    wstring* pv = &value;
    OnClick(r, [id, pv, maxLen] { ui.focusId = id; ui.focusStr = pv; ui.focusMax = maxLen; }, 1);
}

void SevIcon(Sev s, float x, float y, float size) {
    const wchar_t* g = s == Sev::Ok ? Ico::Done : s == Sev::Bad ? Ico::Error : s == Sev::Warn ? Ico::Warning : Ico::Info;
    SolidBrush b(SevSoft(s));
    ui.g->FillEllipse(&b, x, y, size, size);
    Glyph(g, RectF(x, y, size, size), size * 0.5f, SevColor(s));
}

float SectionTitle(float x, float y, float w, const wstring& title, const wstring& sub) {
    Text(title, RectF(x, y, w, 22), FontUI(15, 1), P.text, T_LEFT | T_VCENTER);
    if (sub.empty()) return 30;
    float h = TextH(sub, FontUI(12.5f), w);
    Text(sub, RectF(x, y + 24, w, h + 2), FontUI(12.5f), P.muted, T_WRAP);
    return 30 + h + 4;
}

void KeyValue(float x, float y, float w, const wstring& k, const wstring& v, float kw, Color vc) {
    Text(k, RectF(x, y, kw, 20), FontUI(12.5f), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
    Text(v.empty() ? L"—" : v, RectF(x + kw, y, w - kw, 20), FontUI(13, 1), vc.GetA() ? vc : P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
}

static RectF g_tipRect;
void Tooltip(RectF r, const wstring& tip) {
    if (Hover(r)) { ui.tooltip = tip; g_tipRect = ToWindow(r); }
}

static void DrawTooltip() {
    if (ui.tooltip.empty()) return;
    ui.offX = ui.offY = 0;
    ui.clips.assign(1, RectF(0, 0, ui.W, ui.H));
    ApplyTransform();
    Font* f = FontUI(12);
    float w = std::min(360.0f, TextW(ui.tooltip, f) + 20);
    float h = TextH(ui.tooltip, f, w - 20) + 12;
    float x = std::min(ui.W - w - 8, std::max(8.0f, g_tipRect.X + g_tipRect.Width / 2 - w / 2));
    float y = g_tipRect.GetBottom() + 6;
    if (y + h > ui.H - 8) y = g_tipRect.Y - h - 6;
    RectF r(x, y, w, h);
    Shadow(r, 6, 6);
    FillRound(r, 6, g_dark ? Color(255, 46, 54, 64) : Color(255, 30, 38, 48));
    Text(ui.tooltip, RectF(x + 10, y + 6, w - 20, h - 12), f, Color(255, 240, 244, 248), T_WRAP);
}

void EndFrame() {
    DrawTooltip();
}

struct ModalState {
    bool open = false;
    wstring title, text;
    vector<ModalBtn> buttons;
    Sev icon = Sev::Info;
};
static ModalState g_modal;

bool ModalOpen() { return g_modal.open; }
void CloseModal() { g_modal.open = false; }
void ModalPrimary() {
    if (!g_modal.open || g_modal.buttons.empty()) return;
    auto fn = g_modal.buttons.back().fn;
    g_modal.open = false;
    if (fn) fn();
}

void ModalCustom(const wstring& title, const wstring& text, vector<ModalBtn> buttons, Sev icon) {
    g_modal.open = true;
    g_modal.title = title;
    g_modal.text = text;
    g_modal.buttons = std::move(buttons);
    g_modal.icon = icon;
    ui.focusId = 0;
}

void Confirm(const wstring& title, const wstring& text, const wstring& okLabel, std::function<void()> onOk, bool danger, Sev icon) {
    ModalCustom(title, text, { { L"Отмена", Btn::Secondary, nullptr }, { okLabel, danger ? Btn::Danger : Btn::Primary, std::move(onOk) } }, icon);
}

void Message(const wstring& title, const wstring& text, Sev icon) {
    ModalCustom(title, text, { { L"Понятно", Btn::Primary, nullptr } }, icon);
}

void DrawModal() {
    if (!g_modal.open) return;
    ui.layer = 1;
    ui.offX = ui.offY = 0;
    ui.clips.assign(1, RectF(0, 0, ui.W, ui.H));
    ApplyTransform();
    FillRect(RectF(0, 0, ui.W, ui.H), Color(g_dark ? 150 : 110, 6, 10, 16));
    OnClick(RectF(0, 0, ui.W, ui.H), [] {});
    float w = 540;
    Font* tf = FontUI(13.5f);
    float th = TextH(g_modal.text, tf, w - 112);
    float h = std::max(170.0f, 96 + th + 64);
    RectF r((ui.W - w) / 2, (ui.H - h) / 2, w, h);
    Shadow(r, 16, 24);
    FillRound(r, 16, P.card);
    StrokeRound(r, 16, P.border);
    GradientH(RectF(r.X + 16, r.Y, r.Width - 32, 3), 1.5f, P.accent, P.accent2);
    SevIcon(g_modal.icon, r.X + 28, r.Y + 30, 40);
    Text(g_modal.title, RectF(r.X + 84, r.Y + 28, w - 112, 26), FontUI(18, 1), P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
    Text(g_modal.text, RectF(r.X + 84, r.Y + 62, w - 112, th + 4), tf, P.muted, T_WRAP);
    float x = r.GetRight() - 24;
    for (int i = (int)g_modal.buttons.size() - 1; i >= 0; --i) {
        auto b = g_modal.buttons[i];
        float bw = ButtonW(b.label);
        x -= bw;
        auto fn = b.fn;
        Button(RectF(x, r.GetBottom() - 60, bw, 38), b.label, [fn] { CloseModal(); if (fn) fn(); }, b.style);
        x -= 10;
    }
    ui.layer = 0;
}

struct ToastItem { wstring text; Sev sev; DWORD until; };
static vector<ToastItem> g_toasts;

void Toast(const wstring& text, Sev s) {
    g_toasts.push_back({ text, s, GetTickCount() + 4500 });
    if (g_toasts.size() > 3) g_toasts.erase(g_toasts.begin());
}

void DrawToasts() {
    DWORD now = GetTickCount();
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(), [now](const ToastItem& t) { return (int)(t.until - now) <= 0; }), g_toasts.end());
    if (g_toasts.empty()) return;
    ui.animating = true;
    ui.offX = ui.offY = 0;
    ui.clips.assign(1, RectF(0, 0, ui.W, ui.H));
    ApplyTransform();
    float y = ui.H - 24;
    for (int i = (int)g_toasts.size() - 1; i >= 0; --i) {
        auto& t = g_toasts[i];
        Font* f = FontUI(13, 1);
        float w = std::min(560.0f, TextW(t.text, f) + 70);
        float h = 46;
        y -= h;
        RectF r(ui.W - w - 24, y, w, h);
        Shadow(r, 12, 12);
        FillRound(r, 12, P.card);
        StrokeRound(r, 12, Blend(SevColor(t.sev), P.border, 0.5f));
        SevIcon(t.sev, r.X + 12, r.Y + 11, 24);
        Text(t.text, RectF(r.X + 46, r.Y, w - 58, h), f, P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
        y -= 10;
    }
}

static Bitmap* IconToBitmap(HICON hi) {
    ICONINFO ii{};
    if (!GetIconInfo(hi, &ii)) return nullptr;
    Bitmap* res = nullptr;
    BITMAP bm{};
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm)) {
        int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        vector<DWORD> px((size_t)w * h), mask((size_t)w * h);
        HDC dc = GetDC(nullptr);
        GetDIBits(dc, ii.hbmColor, 0, h, px.data(), &bi, DIB_RGB_COLORS);
        bool alpha = false;
        for (DWORD p : px) if (p >> 24) { alpha = true; break; }
        if (!alpha && ii.hbmMask) {
            GetDIBits(dc, ii.hbmMask, 0, h, mask.data(), &bi, DIB_RGB_COLORS);
            for (size_t i = 0; i < px.size(); ++i) px[i] = (px[i] & 0xFFFFFF) | ((mask[i] & 0xFFFFFF) ? 0 : 0xFF000000u);
        }
        ReleaseDC(nullptr, dc);
        res = new Bitmap(w, h, PixelFormat32bppARGB);
        BitmapData bd;
        Rect rc(0, 0, w, h);
        if (res->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
            for (int y = 0; y < h; ++y) memcpy((BYTE*)bd.Scan0 + y * bd.Stride, &px[(size_t)y * w], (size_t)w * 4);
            res->UnlockBits(&bd);
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return res;
}

void DrawFileIcon(const wstring& ref, RectF r, const wchar_t* fallback) {
    static std::map<wstring, Bitmap*> cache;
    Bitmap* b = nullptr;
    auto it = cache.find(ref);
    if (it != cache.end()) b = it->second;
    else {
        wstring path = ExpandEnv(Trim(ref));
        int index = 0;
        size_t comma = path.find_last_of(L',');
        if (comma != wstring::npos && comma > 2) {
            wstring tail = Trim(path.substr(comma + 1));
            if (!tail.empty() && (iswdigit(tail[0]) || tail[0] == L'-')) { index = _wtoi(tail.c_str()); path = Trim(path.substr(0, comma)); }
        }
        if (path.size() > 1 && path.front() == L'"') path = path.substr(1, path.find(L'"', 1) - 1);
        HICON hi = nullptr;
        if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (index != 0 || IEquals(path.substr(path.size() >= 4 ? path.size() - 4 : 0), L".ico")) ExtractIconExW(path.c_str(), index, &hi, nullptr, 1);
            if (!hi) {
                SHFILEINFOW sfi{};
                if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) hi = sfi.hIcon;
            }
        }
        if (hi) { b = IconToBitmap(hi); DestroyIcon(hi); }
        cache[ref] = b;
    }
    if (b) ui.g->DrawImage(b, r);
    else Glyph(fallback, r, r.Height * 0.6f, P.muted);
}

void UI_KeyChar(wchar_t c) {
    if (!ui.focusId || !ui.focusStr) return;
    if (c == 8) { if (!ui.focusStr->empty()) ui.focusStr->pop_back(); return; }
    if (c == 22 || c == 13 || c == 27 || c < 32) return;
    if (ui.focusStr->size() < ui.focusMax) *ui.focusStr += c;
}

void UI_KeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        if (ModalOpen()) CloseModal();
        else ui.focusId = 0;
        return;
    }
    if (vk == VK_RETURN && ModalOpen()) {
        auto b = g_modal.buttons.empty() ? ModalBtn{} : g_modal.buttons.back();
        CloseModal();
        if (b.fn) b.fn();
        return;
    }
    if (!ui.focusId || !ui.focusStr) return;
    if (vk == VK_RETURN || vk == VK_TAB) { ui.focusId = 0; return; }
    if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (OpenClipboard(nullptr)) {
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h) {
                if (auto p = (const wchar_t*)GlobalLock(h)) {
                    wstring s = Trim(p);
                    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) { return c < 32; }), s.end());
                    for (wchar_t c : s) if (ui.focusStr->size() < ui.focusMax) *ui.focusStr += c;
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
        }
    }
    if (vk == VK_DELETE && (GetKeyState(VK_CONTROL) & 0x8000)) ui.focusStr->clear();
}
