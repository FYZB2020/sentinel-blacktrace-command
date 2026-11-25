// Sentinel Defense Grid - GDI renderer.
// Everything is generated procedurally; no image, video, or font assets are loaded.
#include "render.h"
#include "tracefx.h"

#include <gdiplus.h>
#include <cstdlib>
#include <cwctype>
#include <cstring>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

namespace {

const wchar_t* StateName(FWState s) {
    switch (s) {
    case FWState::Secure:    return L"SECURE";
    case FWState::Degraded:  return L"DEGRADED";
    case FWState::Critical:  return L"CRITICAL";
    case FWState::Offline:   return L"OFFLINE";
    case FWState::Rebuilding:return L"REBUILDING";
    }
    return L"UNKNOWN";
}

COLORREF StateColor(FWState s) {
    switch (s) {
    case FWState::Secure:    return COL_GREEN;
    case FWState::Degraded:  return COL_AMBER;
    case FWState::Critical:  return COL_RED;
    case FWState::Offline:   return COL_DARKRED;
    case FWState::Rebuilding:return COL_CYAN;
    }
    return COL_TEXT;
}

const wchar_t* ThreatName(int th) {
    switch (th) {
    case 0: return L"NOMINAL";
    case 1: return L"GUARDED";
    case 2: return L"ELEVATED";
    default:return L"CRITICAL";
    }
}

COLORREF ThreatColor(int th) {
    switch (th) {
    case 0: return COL_GREEN;
    case 1: return COL_AMBER;
    case 2: return COL_AMBER;
    default:return COL_RED;
    }
}

void FormatNum(uint64_t n, wchar_t* out, size_t cch) {
    if (n >= 1000000ull) {
        swprintf_s(out, cch, L"%.2fM", (double)n / 1000000.0);
    } else if (n >= 1000ull) {
        swprintf_s(out, cch, L"%.1fK", (double)n / 1000.0);
    } else {
        swprintf_s(out, cch, L"%llu", (unsigned long long)n);
    }
}

bool HasCJK(const wchar_t* s) {
    if (!s) return false;
    for (; *s; s++) {
        unsigned int c = (unsigned int)*s;
        if ((c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
            (c >= 0xFF00 && c <= 0xFFEF)) {
            return true;
        }
    }
    return false;
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(HWND h, int w, int hgt, bool prim) {
    hwnd = h;
    primary = prim;
    Resize(w, hgt);
    return memDC != nullptr;
}

bool Renderer::ScreenToVirtual(int sx, int sy, float& vx, float& vy) const {
    if (outW <= 0 || outH <= 0 || scale <= 0.0f) return false;
    float ix = (float)sx * (float)W / (float)outW;
    float iy = (float)sy * (float)H / (float)outH;
    vx = (ix - (float)ox) / scale;
    vy = (iy - (float)oy) / scale;
    return true;
}

bool Renderer::MakeDib(int w, int h, HDC& dc, HBITMAP& bmp, void** bits) {
    dc = nullptr;
    bmp = nullptr;
    if (bits) *bits = nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
    if (!bmp) {
        DeleteDC(dc);
        dc = nullptr;
        return false;
    }
    SelectObject(dc, bmp);
    return true;
}

void Renderer::DestroyCache(CacheSurface& c) {
    if (c.dc) { DeleteDC(c.dc); c.dc = nullptr; }
    if (c.bmp) { DeleteObject(c.bmp); c.bmp = nullptr; }
    c.bits = nullptr;
    c.w = c.h = 0;
    c.id = -1;
}

void Renderer::DestroyCaches() {
    DestroyCache(mainCache_);
    DestroyCache(eventCache_);
    DestroyCache(servicesCache_);
    for (auto& c : popupCache_) DestroyCache(c);
}

bool Renderer::EnsureCache(CacheSurface& c, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (c.dc && c.w == w && c.h == h) return true;
    DestroyCache(c);
    if (!MakeDib(w, h, c.dc, c.bmp, &c.bits)) {
        DestroyCache(c);
        return false;
    }
    c.w = w;
    c.h = h;
    return true;
}

void Renderer::DrawCachedWindow(CacheSurface& c, int x, int y, int w, int h,
                                bool refresh, void (Renderer::*draw)(Sim&), Sim& sim) {
    int pw = PX((float)(x + w)) - PX((float)x);
    int ph = PY((float)(y + h)) - PY((float)y);
    if (refresh || !c.dc) {
        (this->*draw)(sim);
        if (EnsureCache(c, pw, ph)) {
            BitBlt(c.dc, 0, 0, pw, ph, memDC, PX((float)x), PY((float)y), SRCCOPY);
        }
    } else {
        Fill(x + 4, y + 4, w, h, RGB(0, 0, 0));
        BitBlt(memDC, PX((float)x), PY((float)y), pw, ph, c.dc, 0, 0, SRCCOPY);
    }
}

void Renderer::ShutdownBuffers() {
    DestroyCaches();
    DestroyRainAtlas();
    auto del = [](HDC& dc, HBITMAP& bmp) {
        if (dc) {
            DeleteDC(dc);
            dc = nullptr;
        }
        if (bmp) {
            DeleteObject(bmp);
            bmp = nullptr;
        }
    };
    del(memDC, memBmp);       memBits = nullptr;
    del(scratchDC, scratchBmp); scratchBits = nullptr;
    del(bgDC, bgBmp);         bgBits = nullptr;
    del(redDC, redBmp);
    del(blackDC, blackBmp);
    del(whiteDC, whiteBmp);
}

void Renderer::ShutdownGdi() {
    for (auto& kv : brushCache) DeleteObject(kv.second);
    brushCache.clear();
    for (auto& kv : penCache) DeleteObject(kv.second);
    penCache.clear();
    for (auto& f : fonts) {
        if (f) { DeleteObject(f); f = nullptr; }
    }
    if (fontCN) { DeleteObject(fontCN); fontCN = nullptr; }
    if (fontCNHead) { DeleteObject(fontCNHead); fontCNHead = nullptr; }
    if (fontCNTerm) { DeleteObject(fontCNTerm); fontCNTerm = nullptr; }
    if (rainFont) { DeleteObject(rainFont); rainFont = nullptr; }
    if (fontUI) { DeleteObject(fontUI); fontUI = nullptr; }
    if (fontUIHead) { DeleteObject(fontUIHead); fontUIHead = nullptr; }
}

void Renderer::Resize(int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    outW = w;
    outH = h;
    int iw = (std::max)(640, (int)lroundf((float)w * renderScale_));
    int ih = (std::max)(360, (int)lroundf((float)h * renderScale_));
    if (iw == W && ih == H && memDC) return;
    ShutdownBuffers();
    W = iw;
    H = ih;
    scale = std::min((float)W / (float)VW, (float)H / (float)VH);
    if (scale < 0.25f) scale = 0.25f;
    ox = (W - (int)lroundf(VW * scale)) / 2;
    oy = (H - (int)lroundf(VH * scale)) / 2;

    void* b = nullptr;
    bool ok = MakeDib(W, H, memDC, memBmp, &memBits) &&
              MakeDib(W, H, scratchDC, scratchBmp, &scratchBits) &&
              MakeDib(W, H, bgDC, bgBmp, &bgBits);
    if (!ok) {
        ShutdownBuffers();
        W = H = 0;
        return;
    }
    if (MakeDib(1, 1, redDC, redBmp, &b) && b) ((uint32_t*)b)[0] = 0xFFFF0000u;
    if (MakeDib(1, 1, blackDC, blackBmp, &b) && b) ((uint32_t*)b)[0] = 0xFF000000u;
    if (MakeDib(1, 1, whiteDC, whiteBmp, &b) && b) ((uint32_t*)b)[0] = 0xFFFFFFFFu;

    BuildFonts();
    BuildBackground();
    BuildRainAtlas();
}

void Renderer::BuildFonts() {
    const int sizes[10] = {9, 10, 11, 11, 13, 24, 40, 12, 28, 18};
    const int weights[10] = {400, 400, 400, 700, 700, 700, 700, 400, 700, 700};
    for (auto& f : fonts) {
        if (f) { DeleteObject(f); f = nullptr; }
    }
    for (int i = 0; i < 10; i++) {
        int hgt = std::max(8, (int)lroundf(sizes[i] * scale));
        fonts[i] = CreateFontW(-hgt, 0, 0, 0, weights[i], FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                               L"Cascadia Mono");
        if (!fonts[i]) {
            fonts[i] = CreateFontW(-hgt, 0, 0, 0, weights[i], FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                                   L"Consolas");
        }
    }
    if (fontCN) { DeleteObject(fontCN); fontCN = nullptr; }
    if (fontCNHead) { DeleteObject(fontCNHead); fontCNHead = nullptr; }
    if (fontCNTerm) { DeleteObject(fontCNTerm); fontCNTerm = nullptr; }
    if (rainFont) { DeleteObject(rainFont); rainFont = nullptr; }

    int cnH = std::max(8, (int)lroundf(12 * scale));
    fontCN = CreateFontW(-cnH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS,
                         L"Microsoft YaHei UI");
    fontCNHead = CreateFontW(-std::max(8, (int)lroundf(14 * scale)), 0, 0, 0, FW_BOLD,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    fontCNTerm = CreateFontW(-std::max(8, (int)lroundf(9 * scale)), 0, 0, 0, FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    rainFont = CreateFontW(-std::max(8, (int)lroundf(9 * scale)), 0, 0, 0, FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                           FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
    if (!rainFont) {
        rainFont = CreateFontW(-std::max(8, (int)lroundf(9 * scale)), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                               FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    fontUI = CreateFontW(-std::max(8, (int)lroundf(10 * scale)), 0, 0, 0, FW_NORMAL,
                         FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                         VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    fontUIHead = CreateFontW(-std::max(8, (int)lroundf(11 * scale)), 0, 0, 0, FW_SEMIBOLD,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
}

void Renderer::BuildBackground() {
    if (!bgDC) return;
    RECT full{0, 0, W, H};
    FillRect(bgDC, &full, Br(RGB(1, 4, 3)));

    // v1.3: a terminal wall, not a Windows desktop. Dark green phosphor,
    // faint dot grid, horizontal scanlines, no wallpaper logo.
    const int bands = 64;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)(bands - 1);
        COLORREF c = Blend(RGB(4, 14, 9), RGB(0, 2, 2), t);
        int y0 = (int)((float)H * i / bands);
        int y1 = (int)((float)H * (i + 1) / bands);
        RECT r{0, y0, W, y1};
        HBRUSH b = CreateSolidBrush(c);
        FillRect(bgDC, &r, b);
        DeleteObject(b);
    }

    // sparse phosphor dots
    int step = std::max(8, PS(26));
    for (int y = 0; y < H; y += step) {
        for (int x = 0; x < W; x += step) {
            RECT r{x, y, x + std::max(1, PS(1)), y + std::max(1, PS(1))};
            FillRect(bgDC, &r, Br(RGB(8, 28, 18)));
        }
    }
    // CRT scanlines
    for (int y = 0; y < H; y += std::max(2, PS(3))) {
        RECT r{0, y, W, y + 1};
        FillRect(bgDC, &r, Br(RGB(1, 6, 4)));
    }
    // top/bottom darker vignette edges
    for (int i = 0; i < PS(40); i++) {
        int a = (int)(40.0f * (1.0f - (float)i / (float)std::max(1, PS(40))));
        RECT rt{0, i, W, i + 1};
        RECT rb{0, H - i - 1, W, H - i};
        FillRect(bgDC, &rt, Br(Blend(COL_BLACK, RGB(1, 6, 4), a / 40.0f)));
        FillRect(bgDC, &rb, Br(Blend(COL_BLACK, RGB(1, 6, 4), a / 40.0f)));
    }
}

void Renderer::DestroyRainAtlas() {
    auto del = [](HDC& dc, HBITMAP& bmp) {
        if (dc) {
            DeleteDC(dc);
            dc = nullptr;
        }
        if (bmp) { DeleteObject(bmp); bmp = nullptr; }
    };
    del(rainMaskDC, rainMaskBmp);
    rainMaskBits = nullptr;
    for (int i = 0; i < 4; i++) {
        del(rainColorDC[i], rainColorBmp[i]);
        rainColorBits[i] = nullptr;
    }
    rainAtlasReady = false;
}

void Renderer::BuildRainAtlas() {
    DestroyRainAtlas();
    if (!rainFont) return;
    rainCellW = TW(L"W", 1) + PS(2);
    rainCellH = PS(14);
    if (rainCellW < 2 || rainCellH < 2) return;

    const int cols = 16;
    const int rows = 4;
    const int aw = cols * rainCellW;
    const int ah = rows * rainCellH;
    const wchar_t* glyphs = RainGlyphs();

    if (!MakeDib(aw, ah, rainMaskDC, rainMaskBmp, &rainMaskBits)) return;
    RECT r{0, 0, aw, ah};
    FillRect(rainMaskDC, &r, Br(COL_WHITE));
    HFONT old = (HFONT)SelectObject(rainMaskDC, rainFont);
    SetBkMode(rainMaskDC, TRANSPARENT);
    SetTextColor(rainMaskDC, RGB(0, 0, 0));
    for (int i = 0; i < RAIN_GLYPH_COUNT; i++) {
        wchar_t ch = glyphs[i];
        TextOutW(rainMaskDC, (i % cols) * rainCellW, (i / cols) * rainCellH, &ch, 1);
    }
    SelectObject(rainMaskDC, old);

    const COLORREF colors[4] = {COL_DIM, COL_GREEN, COL_WHITE, COL_RED};
    bool ok = true;
    for (int c = 0; c < 4; c++) {
        if (!MakeDib(aw, ah, rainColorDC[c], rainColorBmp[c], &rainColorBits[c])) {
            ok = false;
            break;
        }
        FillRect(rainColorDC[c], &r, Br(COL_BLACK));
        old = (HFONT)SelectObject(rainColorDC[c], rainFont);
        SetBkMode(rainColorDC[c], TRANSPARENT);
        SetTextColor(rainColorDC[c], colors[c]);
        for (int i = 0; i < RAIN_GLYPH_COUNT; i++) {
            wchar_t ch = glyphs[i];
            TextOutW(rainColorDC[c], (i % cols) * rainCellW, (i / cols) * rainCellH, &ch, 1);
        }
        SelectObject(rainColorDC[c], old);
    }
    if (!ok) {
        DestroyRainAtlas();
        return;
    }
    rainAtlasReady = true;
}

int Renderer::PX(float x) const { return ox + (int)lroundf(x * scale); }
int Renderer::PY(float y) const { return oy + (int)lroundf(y * scale); }
int Renderer::PS(float v) const { return std::max(1, (int)lroundf(v * scale)); }

HBRUSH Renderer::Br(COLORREF c) {
    auto it = brushCache.find(c);
    if (it != brushCache.end()) return it->second;
    HBRUSH b = CreateSolidBrush(c);
    brushCache[c] = b;
    return b;
}

HPEN Renderer::Pen(COLORREF c, int w) {
    auto key = std::make_pair(c, w);
    auto it = penCache.find(key);
    if (it != penCache.end()) return it->second;
    HPEN p = CreatePen(PS_SOLID, std::max(1, w), c);
    penCache[key] = p;
    return p;
}

void Renderer::Fill(int x, int y, int w, int h, COLORREF c) {
    if (!memDC || w <= 0 || h <= 0) return;
    RECT r{PX((float)x), PY((float)y), PX((float)(x + w)), PY((float)(y + h))};
    FillRect(memDC, &r, Br(c));
}

void Renderer::Frame(int x, int y, int w, int h, COLORREF c) {
    if (!memDC || w <= 0 || h <= 0) return;
    RECT r{PX((float)x), PY((float)y), PX((float)(x + w)), PY((float)(y + h))};
    FrameRect(memDC, &r, Br(c));
}

void Renderer::Line(int x1, int y1, int x2, int y2, COLORREF c, int width) {
    if (!memDC) return;
    int pw = std::max(1, (int)lroundf((float)width * scale));
    HPEN p = Pen(c, pw);
    HGDIOBJ old = SelectObject(memDC, p);
    MoveToEx(memDC, PX((float)x1), PY((float)y1), nullptr);
    LineTo(memDC, PX((float)x2), PY((float)y2));
    SelectObject(memDC, old);
}

void Renderer::Text(int x, int y, const wchar_t* s, COLORREF c, int fid) {
    if (!memDC || !s || !*s) return;
    if (fid < 0 || fid > 9) fid = 1;
    HFONT old = (HFONT)SelectObject(memDC, fonts[fid]);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    TextOutW(memDC, PX((float)x), PY((float)y), s, (int)wcslen(s));
    SelectObject(memDC, old);
}

int Renderer::TW(const wchar_t* s, int fid) {
    if (!memDC || !s) return 0;
    if (fid < 0 || fid > 9) fid = 1;
    HFONT old = (HFONT)SelectObject(memDC, fonts[fid]);
    SIZE sz{0, 0};
    GetTextExtentPoint32W(memDC, s, (int)wcslen(s), &sz);
    SelectObject(memDC, old);
    return sz.cx;
}

void Renderer::TextR(int xRight, int y, const wchar_t* s, COLORREF c, int fid) {
    if (!memDC || !s || !*s) return;
    if (fid < 0 || fid > 9) fid = 1;
    HFONT old = (HFONT)SelectObject(memDC, fonts[fid]);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    int w = 0;
    SIZE sz{0, 0};
    GetTextExtentPoint32W(memDC, s, (int)wcslen(s), &sz);
    w = sz.cx;
    TextOutW(memDC, PX((float)xRight) - w, PY((float)y), s, (int)wcslen(s));
    SelectObject(memDC, old);
}

void Renderer::TextC(int cx, int y, const wchar_t* s, COLORREF c, int fid) {
    if (!memDC || !s || !*s) return;
    if (fid < 0 || fid > 9) fid = 1;
    HFONT old = (HFONT)SelectObject(memDC, fonts[fid]);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    SIZE sz{0, 0};
    GetTextExtentPoint32W(memDC, s, (int)wcslen(s), &sz);
    TextOutW(memDC, PX((float)cx) - sz.cx / 2, PY((float)y), s, (int)wcslen(s));
    SelectObject(memDC, old);
}

void Renderer::TextTyped(int x, int y, const wchar_t* s, COLORREF c, int fid, float chars) {
    if (!memDC || !s || !*s) return;
    if (fid < 0 || fid > 9) fid = 1;
    int len = (int)wcslen(s);
    int n = (int)clampf(chars, 0.0f, (float)len);
    if (n <= 0) return;
    HFONT old = (HFONT)SelectObject(memDC, fonts[fid]);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    TextOutW(memDC, PX((float)x), PY((float)y), s, n);
    SelectObject(memDC, old);
}

void Renderer::TextTypedFont(int x, int y, const wchar_t* s, COLORREF c, HFONT f, float chars) {
    if (!memDC || !s || !*s || !f) return;
    int len = (int)wcslen(s);
    int n = (int)clampf(chars, 0.0f, (float)len);
    if (n <= 0) return;
    HFONT old = (HFONT)SelectObject(memDC, f);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    TextOutW(memDC, PX((float)x), PY((float)y), s, n);
    SelectObject(memDC, old);
}

void Renderer::TextUI(int x, int y, const wchar_t* s, COLORREF c, HFONT f) {
    if (!memDC || !s || !*s || !f) return;
    HFONT old = (HFONT)SelectObject(memDC, f);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    TextOutW(memDC, PX((float)x), PY((float)y), s, (int)wcslen(s));
    SelectObject(memDC, old);
}

void Renderer::TextUIR(int xRight, int y, const wchar_t* s, COLORREF c, HFONT f) {
    if (!memDC || !s || !*s || !f) return;
    HFONT old = (HFONT)SelectObject(memDC, f);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    SIZE sz{0, 0};
    GetTextExtentPoint32W(memDC, s, (int)wcslen(s), &sz);
    TextOutW(memDC, PX((float)xRight) - sz.cx, PY((float)y), s, (int)wcslen(s));
    SelectObject(memDC, old);
}

int Renderer::TWUI(const wchar_t* s, HFONT f) {
    if (!memDC || !s || !f) return 0;
    HFONT old = (HFONT)SelectObject(memDC, f);
    SIZE sz{0, 0};
    GetTextExtentPoint32W(memDC, s, (int)wcslen(s), &sz);
    SelectObject(memDC, old);
    return sz.cx;
}

void Renderer::Panel(int x, int y, int w, int h, const wchar_t* title, COLORREF accent) {
    Fill(x, y, w, h, COL_PANEL);
    Frame(x, y, w, h, COL_BORDER);
    Fill(x, y, 2, h, Blend(accent, COL_BLACK, 0.35f));
    if (title && *title) {
        Text(x + 10, y + 6, title, accent, 4);
        Line(x + 8, y + 28, x + w - 8, y + 28, Blend(accent, COL_BLACK, 0.55f), 1);
    }
}

void Renderer::Bar(int x, int y, int w, int h, float pct, COLORREF c) {
    pct = clampf(pct, 0.0f, 1.0f);
    Fill(x, y, w, h, RGB(8, 18, 14));
    if (pct > 0.001f) Fill(x, y, std::max(1, (int)lroundf(w * pct)), h, c);
    if (h > 2) Frame(x, y, w, h, COL_BORDER);
}

// ---------------------------------------------------------------------------
// v1.2 Windows desktop / console UI
// ---------------------------------------------------------------------------

void Renderer::WindowFrame(int x, int y, int w, int h, const wchar_t* title,
                           COLORREF accent, bool active) {
    Fill(x + 4, y + 4, w, h, RGB(0, 0, 0));
    Fill(x, y, w, h, RGB(9, 12, 18));
    Frame(x, y, w, h, RGB(62, 68, 82));
    Fill(x + 1, y + 1, w - 2, 26, active ? RGB(30, 35, 47) : RGB(21, 24, 32));
    Fill(x + 1, y + 27, w - 2, 1, accent);
    Fill(x + 2, y, 2, h, accent);

    HFONT tf = HasCJK(title) ? fontCN : fontUIHead;
    TextUI(x + 10, y + 6, title, RGB(220, 228, 240), tf);
    TextUIR(x + w - 10, y + 6, L"— □ ×", RGB(150, 160, 176), fontUI);
    Fill(x + 1, y + 28, w - 2, h - 29, RGB(2, 4, 7));
}

void Renderer::DrawTaskbar(Sim& sim) {
    Fill(0, 672, VW, 48, RGB(13, 16, 22));
    Fill(0, 672, VW, 1, RGB(52, 58, 72));

    // Start button: four panes.
    COLORREF blue = RGB(70, 150, 220);
    Fill(12, 682, 14, 14, blue);
    Fill(30, 682, 14, 14, blue);
    Fill(12, 700, 14, 14, blue);
    Fill(30, 700, 14, 14, blue);

    // Search box.
    Fill(58, 682, 210, 28, RGB(28, 32, 41));
    Frame(58, 682, 210, 28, RGB(55, 62, 76));
    TextUI(70, 688, L"Search", RGB(150, 158, 172), fontCN);

    // Taskbar app buttons.
    Fill(286, 682, 32, 28, RGB(24, 28, 37));
    TextUI(294, 687, L">_", RGB(110, 210, 160), fontUI);
    Fill(326, 682, 32, 28, RGB(24, 28, 37));
    TextUI(334, 687, L"#", RGB(220, 190, 90), fontUI);
    Fill(366, 682, 32, 28, RGB(24, 28, 37));
    TextUI(374, 687, L"#", RGB(110, 180, 220), fontUI);

    TextUI(470, 690, L"SENTINEL v1.2", RGB(62, 72, 88), fontUI);

    wchar_t stats[128];
    if (sim.sysCpuPct > 0.0f) {
        wchar_t hstats[128];
        swprintf_s(hstats, L"HOST CPU %.0f%%  RAM %.0f%%", sim.sysCpuPct, sim.memPct);
        TextUI(700, 681, hstats, RGB(70, 80, 96), fontUI);
        swprintf_s(stats, L"SENTINEL CPU %.1f%%  RAM %.0f MB", sim.procCpuPct, sim.procMemMB);
        TextUI(700, 697, stats, RGB(90, 100, 116), fontUI);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t tbuf[32];
    swprintf_s(tbuf, L"%02d:%02d", st.wHour, st.wMinute);
    TextUIR(1248, 679, tbuf, RGB(225, 232, 242), fontUIHead);
    wchar_t dbuf[32];
    swprintf_s(dbuf, L"%04d/%02d/%02d", st.wYear, st.wMonth, st.wDay);
    TextUIR(1248, 699, dbuf, RGB(150, 158, 172), fontUI);

    if (sim.ThreatLevel() >= 3 && fmodf((float)sim.time, 1.0f) < 0.55f) {
        TextUI(640, 690, L"! SYSTEM UNDER ATTACK", RGB(255, 60, 70), fontCN);
    }
}

void Renderer::DrawDesktop(Sim& sim) {
    wchar_t wm[128];
    swprintf_s(wm, L"SENTINEL DEFENSE GRID  //  v1.2");
    TextUI(1030, 640, wm, RGB(46, 54, 70), fontUI);
    (void)sim;
}

void Renderer::DrawMainConsole(Sim& sim) {
    const int x = 18, y = 20, w = 790, h = 636;
    WindowFrame(x, y, w, h, L"C:\\Windows\\System32\\cmd.exe", RGB(72, 88, 112), true);
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)(x + 2)), PY((float)(y + 29)),
                      PX((float)(x + w - 2)), PY((float)(y + h - 2)));

    static const wchar_t* art[5] = {
        L"  ####  ######  #    #  #####  ######  #    #  ######  #",
        L" ##     ##      ##   ##    ##     ##   ##   ##  ##      ##",
        L"  ####  #####   ## # ##    ##     ##   ###  ##  #####   ##",
        L"     ## ##      ##   ##    ##     ##   ## # ##  ##      ##",
        L"  ####  ######  ##   ##    ##     ##   ##  ##   ######  ######"
    };
    for (int i = 0; i < 5; i++) Text(x + 14, y + 36 + i * 12, art[i], RGB(44, 112, 78), 0);
    Text(x + 14, y + 102, L"SENTINEL CONTAINMENT CONSOLE // ROOT SESSION", RGB(80, 100, 120), 0);
    Line(x + 12, y + 116, x + w - 12, y + 116, RGB(40, 55, 68), 1);

    int total = (int)sim.log.entries.size();
    int maxLines = 30;
    int start = (std::max)(0, total - maxLines);
    int shown = total - start;
    int baseY = y + 124;
    for (int i = 0; i < shown; i++) {
        const LogEntry& e = sim.log.entries[(size_t)(start + i)];
        int ly = baseY + i * 15;
        COLORREF sc = e.sev == Severity::Crit ? RGB(255, 90, 96)
                     : (e.sev == Severity::Warn ? RGB(240, 190, 90) : RGB(130, 180, 155));
        Text(x + 14, ly, e.time.c_str(), RGB(70, 92, 84), 0);
        Text(x + 82, ly, e.module.c_str(), e.module == L"CMD" ? RGB(100, 210, 230) : sc, 0);
        float chars = (i == shown - 1) ? e.age * 95.0f : 10000.0f;
        if (HasCJK(e.text.c_str())) {
            TextTypedFont(x + 128, ly - 1, e.text.c_str(), RGB(210, 225, 215), fontCN, chars);
        } else {
            TextTyped(x + 128, ly, e.text.c_str(), RGB(196, 214, 202), 0, chars);
        }
    }

    // Scrollbar.
    Fill(x + w - 7, y + 30, 3, h - 34, RGB(20, 24, 31));
    int trackY = y + 30, trackH = h - 34;
    float frac = total > maxLines ? (float)maxLines / (float)total : 1.0f;
    int thumbH = (std::max)(12, (int)(trackH * frac));
    int thumbY = trackY + (total > maxLines ? (int)(trackH * (float)start / (float)total) : 0);
    Fill(x + w - 7, thumbY, 3, thumbH, RGB(80, 90, 108));

    bool blink = fmodf((float)sim.time, 1.0f) < 0.62f;
    Text(x + 14, y + h - 28, L"C:\\Windows\\system32>", RGB(130, 230, 180), 0);
    if (blink) Text(x + 218, y + h - 28, L"_", RGB(230, 255, 240), 0);
    TextUIR(x + w - 14, y + h - 28, L"[ENTER] EMERGENCY OVERRIDE", RGB(255, 180, 60), fontUI);
    RestoreDC(memDC, -1);
}

void Renderer::DrawEventWindow(Sim& sim) {
    const int x = 824, y = 20, w = 438, h = 300;
    WindowFrame(x, y, w, h, L"Event Viewer", RGB(85, 105, 145), true);
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)(x + 2)), PY((float)(y + 29)),
                      PX((float)(x + w - 2)), PY((float)(y + h - 2)));
    Text(x + 12, y + 38, L"LEVEL   TIME       SOURCE  EVENT", RGB(110, 125, 150), 0);
    Line(x + 10, y + 52, x + w - 10, y + 52, RGB(42, 52, 68), 1);

    int total = (int)sim.log.entries.size();
    int start = (std::max)(0, total - 12);
    int shown = total - start;
    for (int i = 0; i < shown; i++) {
        const LogEntry& e = sim.log.entries[(size_t)(start + i)];
        int ly = y + 60 + i * 17;
        const wchar_t* lv = e.sev == Severity::Crit ? L"ERROR" : (e.sev == Severity::Warn ? L"WARN" : L"INFO");
        COLORREF lc = e.sev == Severity::Crit ? RGB(255, 90, 96)
                     : (e.sev == Severity::Warn ? RGB(240, 190, 90) : RGB(130, 180, 155));
        TextUI(x + 12, ly, lv, lc, fontCN);
        Text(x + 60, ly, e.time.c_str(), RGB(120, 138, 160), 0);
        Text(x + 126, ly, e.module.c_str(), RGB(150, 170, 195), 0);
        wchar_t line[96];
        size_t n = (std::min)((size_t)54, e.text.size());
        wcsncpy_s(line, 96, e.text.c_str(), n);
        line[n] = 0;
        if (e.text.size() > n) { line[n - 1] = L'…'; }
        TextUI(x + 184, ly - 1, line, RGB(200, 212, 222), HasCJK(line) ? fontCN : fontUI);
    }
    TextUIR(x + w - 12, y + h - 24, L"PS C:\\Users\\Public>_", RGB(120, 140, 160), fontUI);
    RestoreDC(memDC, -1);
}

void Renderer::DrawServicesWindow(Sim& sim) {
    const int x = 824, y = 336, w = 438, h = 320;
    WindowFrame(x, y, w, h, L"Services", RGB(85, 105, 145), true);
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)(x + 2)), PY((float)(y + 29)),
                      PX((float)(x + w - 2)), PY((float)(y + h - 2)));
    Text(x + 12, y + 38, L"SERVICE NAME      STATE       INTEGRITY", RGB(110, 125, 150), 0);
    Line(x + 10, y + 52, x + w - 10, y + 52, RGB(42, 52, 68), 1);

    for (int i = 0; i < 6; i++) {
        const Firewall& fw = sim.firewalls[i];
        int ly = y + 62 + i * 40;
        const wchar_t* st = fw.state == FWState::Secure ? L"RUNNING"
                          : fw.state == FWState::Degraded ? L"DEGRADED"
                          : fw.state == FWState::Critical ? L"CRITICAL"
                          : fw.state == FWState::Offline ? L"STOPPED" : L"START_PENDING";
        COLORREF sc = StateColor(fw.state);
        wchar_t nm[40];
        swprintf_s(nm, L"Sentinel%s", fw.name.c_str());
        TextUI(x + 12, ly, nm, RGB(210, 220, 230), fontUI);
        Text(x + 12, ly + 16, st, sc, 0);
        wchar_t pct[16];
        swprintf_s(pct, L"%d%%", (int)lroundf(fw.display));
        TextR(x + 350, ly + 6, pct, sc, 0);
        const wchar_t* badge = fw.state == FWState::Secure ? L"[OK]"
                             : fw.state == FWState::Rebuilding ? L"[..]"
                             : fw.state == FWState::Offline ? L"[XX]" : L"[!!]";
        TextR(x + w - 12, ly + 6, badge, sc, 0);
    }
    Text(x + 12, y + h - 24, L"sc>", RGB(120, 140, 160), 0);
    RestoreDC(memDC, -1);
}

void Renderer::DrawPopupOne(const Popup& p, int px, int py) {
    int pw = (int)p.w;
    int ph = (int)p.h;
    COLORREF accent = p.sev == Severity::Crit ? RGB(220, 52, 62)
                    : (p.sev == Severity::Warn ? RGB(230, 168, 48) : RGB(70, 190, 210));

    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)px), PY((float)py),
                      PX((float)(px + pw)), PY((float)(py + ph)));
    WindowFrame(px, py, pw, ph, p.title.c_str(), accent, true);

    // Split the batch output into at most five console lines.
    wchar_t lines[5][160] = {};
    int lineCount = 0;
    const wchar_t* s = p.body.c_str();
    while (*s && lineCount < 5) {
        const wchar_t* nl = wcschr(s, L'\n');
        size_t n = nl ? (size_t)(nl - s) : wcslen(s);
        if (n > 150) n = 150;
        wcsncpy_s(lines[lineCount], 160, s, n);
        lines[lineCount][n] = 0;
        lineCount++;
        if (!nl) break;
        s = nl + 1;
    }
    for (int i = 0; i < lineCount; i++) {
        bool warn = wcsstr(lines[i], L"ERROR") || wcsstr(lines[i], L"DENIED") ||
                    wcsstr(lines[i], L"WARN") || wcsstr(lines[i], L"TERMINATE");
        COLORREF lc = warn ? RGB(245, 180, 80) : RGB(205, 218, 210);
        int ly = py + 36 + i * 18;
        if (HasCJK(lines[i])) TextUI(px + 12, ly - 1, lines[i], lc, fontCN);
        else Text(px + 12, ly, lines[i], lc, 0);
    }
    TextUIR(px + pw - 12, py + 34, p.tag.c_str(), accent, fontUI);
    Text(px + 12, py + ph - 24, L"C:\\Windows\\system32>_", RGB(120, 220, 170), 0);
    RestoreDC(memDC, -1);
}

void Renderer::DrawPopups(Sim& sim) {
    for (int i = 0; i < (int)sim.popups.items.size(); i++) {
        const Popup& p = sim.popups.items[i];
        if (!p.active) continue;
        int px = (int)(p.x + (1.0f - p.slide) * 26.0f);
        int py = (int)p.y;
        int pw = PX(p.w) - PX(0.0f);
        int ph = PY(p.h) - PY(0.0f);
        CacheSurface& c = popupCache_[i];
        if (c.id != p.id) {
            DrawPopupOne(p, px, py);
            if (EnsureCache(c, pw, ph)) {
                BitBlt(c.dc, 0, 0, pw, ph, memDC, PX((float)px), PY((float)py), SRCCOPY);
                c.id = p.id;
            }
        } else {
            Fill(px + 4, py + 4, (int)p.w, (int)p.h, RGB(0, 0, 0));
            BitBlt(memDC, PX((float)px), PY((float)py), pw, ph, c.dc, 0, 0, SRCCOPY);
        }
    }
}


// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// v1.3 terminal wall
// ---------------------------------------------------------------------------
static bool HasCJKTerm(const wchar_t* s) {
    if (!s) return false;
    for (; *s; ++s) {
        if ((unsigned)*s >= 0x2E80u) return true;
    }
    return false;
}

void Renderer::TextTerm(int x, int y, const wchar_t* s, COLORREF c, bool cn) {
    HFONT f = cn ? (fontCNTerm ? fontCNTerm : fontCN)
                 : (fonts[1] ? fonts[1] : fontCN);
    if (!f) return;
    HFONT old = (HFONT)SelectObject(memDC, f);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, c);
    TextOutW(memDC, PX((float)x), PY((float)y), s, (int)wcslen(s));
    SelectObject(memDC, old);
}

void Renderer::TextDecode(Sim& sim, int x, int y, const wchar_t* s, COLORREF c, int fid, int channel) {
    if (!s || !*s) return;
    if (sim.cfg.motion == L"reduced" || sim.motion.phase <= 0.0f) {
        Text(x, y, s, c, fid);
        return;
    }
    const int len = (std::min)(127, (int)wcslen(s));
    const float progress = sim.motion.PhaseProgress();
    const int resolved = (std::min)(len, (int)(progress * (len + 5)) - channel % 3);
    wchar_t scrambled[128];
    const uint32_t tick = (uint32_t)(sim.time * 18.0);
    for (int i = 0; i < len; ++i) {
        wchar_t ch = s[i];
        if (i >= resolved && ch != L' ' && ch != L'\t')
            ch = TraceFX::Glyph((uint32_t)(i + 1) * 7919u + tick + (uint32_t)channel * 131u);
        scrambled[i] = ch;
    }
    scrambled[len] = 0;
    Text(x, y, scrambled, Blend(c, COL_BLACK, 0.52f), fid);
    if (resolved > 0) TextTyped(x, y, s, c, fid, (float)resolved);
}

void Renderer::TextDecodeC(Sim& sim, int cx, int y, const wchar_t* s, COLORREF c, int fid, int channel) {
    const int virtualWidth = scale > 0.0f ? (int)(TW(s, fid) / scale) : TW(s, fid);
    TextDecode(sim, cx - virtualWidth / 2, y, s, c, fid, channel);
}

void Renderer::PacketLine(Sim& sim, int x1, int y1, int x2, int y2, float t, COLORREF c) {
    const bool reduced = sim.cfg.motion == L"reduced";
    t = reduced ? 0.72f : clampf(t, 0.0f, 1.0f);
    const int trails = reduced ? 0 : 3;
    for (int j = trails; j >= 0; --j) {
        float u = t - j * 0.035f;
        if (u < 0.0f || u > 1.0f) continue;
        int px = (int)lerpf((float)x1, (float)x2, u);
        int py = (int)lerpf((float)y1, (float)y2, u);
        int size = j == 0 ? 5 : 3;
        float strength = 1.0f - j * 0.20f;
        Fill(px - size / 2, py - size / 2, size, size, Blend(COL_BLACK, c, strength));
    }
    if (!reduced && t > 0.90f) {
        int r = 5 + (int)((t - 0.90f) * 90.0f);
        float fade = 1.0f - (t - 0.90f) * 6.0f;
        float quantized = (float)(std::max)(0, (std::min)(5, (int)(fade * 5.0f))) / 5.0f;
        Frame(x2 - r, y2 - r, r * 2, r * 2, Blend(COL_BLACK, c, quantized));
    }
}

void Renderer::DrawTraceCommand(Sim& sim, int x, int y, int w, int channel) {
    static const wchar_t* commands[12] = {
        L"scan --surface edge --passive",
        L"probe --handshake --timeout=300ms",
        L"privesc --audit drivers --read-only",
        L"credguard --revoke stale-tickets",
        L"trace --route smb,winrm --depth=4",
        L"baseline --diff autostart --signed",
        L"egress --seal dlp-07 --capture",
        L"isolate --segment quarantine --now",
        L"counter --kill-chain --preserve-evidence",
        L"rollback --snapshot verified --staged",
        L"harden --policy baseline --enforce",
        L"custody --seal incident-0x7F3A"
    };
    static const wchar_t* compact[12] = {
        L"scan --edge --passive", L"probe --handshake", L"privesc --audit",
        L"credguard --revoke", L"trace --route --depth=4", L"baseline --diff",
        L"egress --seal dlp-07", L"isolate --segment", L"counter --kill-chain",
        L"rollback --staged", L"harden --enforce", L"custody --seal"
    };
    static const wchar_t* labels[4] = { L"TRACE", L"EXEC", L"VERIFY", L"DUMP" };
    const int index = (std::max)(0, (std::min)(11, sim.opIndex));
    const wchar_t* command = w < 320 ? compact[index] : commands[index];
    const COLORREF accent = sim.CurrentAct().accent;
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)x), PY((float)y), PX((float)(x + w)), PY((float)(y + 18)));
    Fill(x, y, w, 18, RGB(2, 11, 8));
    Fill(x, y, 2, 18, Blend(accent, COL_BLACK, 0.20f));
    Text(x + 7, y + 4, labels[channel & 3], Blend(accent, COL_BLACK, 0.20f), 0);
    TextDecode(sim, x + 58, y + 4, command, COL_TEXT, 0, channel + index);
    RestoreDC(memDC, -1);
}

void Renderer::DrawHexTape(Sim& sim, int x, int y, int w, int rows, int channel) {
    rows = (std::max)(1, (std::min)(6, rows));
    const int height = 18 + rows * 13;
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)x), PY((float)y), PX((float)(x + w)), PY((float)(y + height)));
    Fill(x, y, w, height, RGB(1, 6, 5));
    Frame(x, y, w, height, RGB(11, 38, 29));
    wchar_t title[48];
    swprintf_s(title, L"MEM // CHANNEL %02X // LIVE", channel & 0xFF);
    TextTerm(x + 6, y + 4, title, COL_DIM);
    const int hot = sim.cfg.motion == L"reduced" ? 0 :
                    ((int)(sim.time * 4.0) + channel) % rows;
    if (hot >= 0) Fill(x + 3, y + 17 + hot * 13, w - 6, 12, RGB(4, 22, 16));
    const int bytes = w >= 420 ? 12 : (w >= 280 ? 8 : 6);
    const uint32_t tick = sim.cfg.motion == L"reduced" ? 0u : (uint32_t)(sim.time * 3.0);
    for (int row = 0; row < rows; ++row) {
        wchar_t line[128];
        uint32_t offset = (uint32_t)(sim.opIndex + 1) * 0x1000u + row * 0x20u +
                          ((uint32_t)sim.packets & 0x0FC0u);
        int used = swprintf_s(line, L"%04X:%04X  ", (offset >> 16) & 0xFFFFu, offset & 0xFFFFu);
        for (int col = 0; col < bytes; ++col) {
            unsigned char b = TraceFX::Byte(tick + (uint32_t)sim.opIndex * 17u, channel + row, col);
            used += swprintf_s(line + used, _countof(line) - used, L"%02X ", b);
        }
        TextTerm(x + 6, y + 18 + row * 13, line,
                 row == hot ? COL_TEXT : RGB(37, 91, 66));
    }
    RestoreDC(memDC, -1);
}

void Renderer::DrawTargetBrackets(Sim& sim, int x, int y, int w, int h, COLORREF c) {
    const bool reduced = sim.cfg.motion == L"reduced";
    int pulse = reduced ? 2 : (int)((sinf((float)sim.time * 3.2f) * 0.5f + 0.5f) * 4.0f);
    COLORREF qc = Blend(COL_BLACK, c, 0.48f + pulse * 0.10f);
    int len = 12 + pulse * 2;
    Line(x, y, x + len, y, qc, 1); Line(x, y, x, y + len, qc, 1);
    Line(x + w - len, y, x + w, y, qc, 1); Line(x + w, y, x + w, y + len, qc, 1);
    Line(x, y + h - len, x, y + h, qc, 1); Line(x, y + h, x + len, y + h, qc, 1);
    Line(x + w - len, y + h, x + w, y + h, qc, 1); Line(x + w, y + h - len, x + w, y + h, qc, 1);
}

void Renderer::DrawStatusBar(Sim& sim) {
    const COLORREF actColor = sim.CurrentAct().accent;
    Fill(0, 0, VW, 44, RGB(2, 9, 7));
    Fill(0, 0, 4, 44, actColor);
    Fill(0, 42, VW, 2, Blend(actColor, COL_BLACK, 0.58f));
    TextTerm(16, 7, L"SENTINEL // BLACKTRACE COMMAND", COL_GREEN);
    TextTerm(16, 22, L"v2.3.1 // FUSION EDITION // LIVE", COL_DIM);
    DrawActBadge(sim);

    // Fourteen-beat timeline: the boundary engagement occupies phases 05 and 06.
    for (int i = 0; i < 14; i++) {
        int x = 548 + i * 9;
        COLORREF c = i == sim.opIndex ? COL_WHITE
                   : (i < sim.opIndex ? Blend(actColor, COL_BLACK, 0.42f) : RGB(12, 34, 23));
        int height = i == sim.opIndex ? 14 : 9;
        Fill(x, 21 - height, 7, height, c);
        if (i != sim.opIndex) Frame(x, 21 - height, 7, height, RGB(8, 32, 21));
    }

    wchar_t stats[160];
    swprintf_s(stats, L"HOST CPU %.0f%%  RAM %.0f%%", sim.sysCpuPct, sim.memPct);
    TextTerm(700, 7, stats, COL_DIM);
    swprintf_s(stats, L"GRID %d%%  OFF %d  REB %d", (int)sim.AvgIntegrity(),
               sim.CountState(FWState::Offline), sim.CountState(FWState::Rebuilding));
    TextTerm(700, 22, stats, actColor);

    TextTerm(1080, 7, L"ZERO TRUST // PERIMETER ACTIVE", COL_DIM);
    wchar_t net[96];
    swprintf_s(net, L"PKT %llu  BLOCK %llu  ENC %llu",
               (unsigned long long)sim.packets, (unsigned long long)sim.blocked,
               (unsigned long long)sim.encrypted);
    TextTerm(1080, 22, net, RGB(32, 88, 60));
}

void Renderer::DrawActBadge(Sim& sim) {
    const ActDef& a = sim.CurrentAct();
    wchar_t act[96];
    swprintf_s(act, L"%ls // %ls", a.tag, a.cn);
    TextTerm(300, 7, act, a.accent, true);
    wchar_t phase[96];
    swprintf_s(phase, L"PHASE %02d/14 // %ls", sim.opIndex + 1, sim.OpPhaseName());
    TextTerm(300, 22, phase, COL_TEXT);
}

void Renderer::DrawNodeSidebar(Sim& sim) {
    const int x = 18, y = 52, w = 190, h = 636;
    Fill(x, y, w, h, RGB(1, 6, 4));
    Frame(x, y, w, h, RGB(16, 58, 37));
    Fill(x, y, w, 18, RGB(3, 14, 8));
    TextTerm(x + 8, y + 4, L"NETWORK NODES // 14", COL_TEXT);
    TextTerm(x + 8, y + 22, L"TRACE // LIVE", COL_DIM);

    static const wchar_t* names[10] = {
        L"edge-01", L"relay-02", L"vault-03", L"core-04", L"dns-05",
        L"gw-06", L"proxy-07", L"db-08", L"log-09", L"auth-10"
    };
    static const wchar_t* ips[10] = {
        L"198.51.100.23", L"203.0.113.77", L"192.0.2.41", L"198.51.100.9",
        L"203.0.113.18", L"192.0.2.88", L"198.51.100.62", L"203.0.113.5",
        L"192.0.2.14", L"198.51.100.130"
    };
    for (int i = 0; i < 10; i++) {
        int ry = y + 40 + i * 29;
        const Firewall& fw = sim.firewalls[i % 6];
        COLORREF c = StateColor(fw.state);
        const wchar_t* st = fw.state == FWState::Offline ? L"[XX]" :
                            fw.state == FWState::Critical ? L"[!!]" :
                            fw.state == FWState::Rebuilding ? L"[..]" : L"[OK]";
        TextTerm(x + 8, ry, names[i], COL_TEXT);
        TextTerm(x + 126, ry, st, c);
        TextTerm(x + 8, ry + 11, ips[i], RGB(34, 92, 62));
        Fill(x + 8, ry + 22, (int)(clampf(fw.integrity / 100.0f, 0.0f, 1.0f) * 172.0f), 1,
             Blend(c, RGB(2, 20, 12), 0.55f));
    }

    TextTerm(x + 8, y + 342, L"DIRECTOR PHASE", COL_DIM);
    wchar_t ph[64];
    swprintf_s(ph, L"%02d/14 %ls", sim.opIndex + 1, sim.OpPhaseName());
    TextTerm(x + 8, y + 356, ph, COL_AMBER);

    const int gx = x + 8, gy = y + 380, gw = w - 16, gh = 80;
    Fill(gx, gy, gw, gh, RGB(0, 5, 3));
    Frame(gx, gy, gw, gh, RGB(12, 44, 28));
    TextTerm(gx + 6, gy + 4, L"ROUTE TRACE // 08 LINKS", COL_DIM);
    struct P { int x, y; };
    const P p[7] = {
        {gx + 24, gy + 36}, {gx + 76, gy + 25}, {gx + 146, gy + 38},
        {gx + 42, gy + 53}, {gx + 108, gy + 49}, {gx + 22, gy + 58},
        {gx + 150, gy + 56}
    };
    const int edges[8][2] = {
        {0, 1}, {1, 2}, {0, 3}, {1, 4}, {3, 4}, {3, 5}, {4, 6}, {2, 6}
    };
    for (int i = 0; i < 8; i++) {
        const P& a = p[edges[i][0]];
        const P& b = p[edges[i][1]];
        Line(a.x, a.y, b.x, b.y, RGB(20, 72, 46), 1);
        if ((i + sim.opIndex) % 2 == 0) {
            COLORREF pc = edges[i][1] == 4 ? COL_RED : COL_CYAN;
            PacketLine(sim, a.x, a.y, b.x, b.y,
                       TraceFX::Cycle(sim.time, 0.48f, TraceFX::Unit(i * 97u + 11u)), pc);
        }
    }
    for (int i = 0; i < 7; i++) {
        COLORREF c = (i == 4) ? COL_RED : (i == 2 ? COL_AMBER : COL_GREEN);
        Fill(p[i].x - 2, p[i].y - 2, 5, 5, c);
    }
    DrawTargetBrackets(sim, p[4].x - 8, p[4].y - 8, 16, 16, COL_RED);
    TextTerm(gx + 6, gy + gh - 14, L"TLS // AES-256 // 443", RGB(32, 100, 66));

    Fill(x + 8, y + 468, w - 16, 112, RGB(0, 6, 4));
    Frame(x + 8, y + 468, w - 16, 112, RGB(12, 44, 28));
    TextTerm(x + 14, y + 474, L"INCIDENT // LIVE SNAPSHOT", COL_DIM);
    wchar_t telemetry[5][64];
    swprintf_s(telemetry[0], L"RISK %03d // PHASE %02d", sim.DamageLevel(), sim.opIndex + 1);
    swprintf_s(telemetry[1], L"NODES %02d/14 // ONLINE", 14 - sim.CountState(FWState::Offline));
    swprintf_s(telemetry[2], L"GRID %d%% // OFF %d REB %d", (int)sim.AvgIntegrity(),
               sim.CountState(FWState::Offline), sim.CountState(FWState::Rebuilding));
    swprintf_s(telemetry[3], L"DEF %d/6 // %03d%%", ((int)sim.campaign.defense % 6) + 1,
               (int)(sim.campaign.defenseProgress * 100.0f));
    swprintf_s(telemetry[4], L"PKT %llu // BLK %llu", (unsigned long long)sim.packets,
               (unsigned long long)sim.blocked);
    for (int i = 0; i < 5; i++)
        TextTerm(x + 14, y + 490 + i * 16, telemetry[i], i == 0 ? COL_AMBER : COL_TEXT);
    DrawPhaseBrief(sim, x + 8, y + 590, w - 16, 2);
}

void Renderer::DrawTerminal(Sim& sim) {
    const int x = 218, y = 52, w = 620, h = 424;
    const COLORREF accent = sim.CurrentAct().accent;
    Fill(x, y, w, h, RGB(1, 7, 4));
    Frame(x, y, w, h, RGB(18, 64, 41));
    Fill(x, y, 3, h, Blend(accent, COL_BLACK, 0.25f));
    Fill(x, y, w, 18, RGB(3, 15, 9));
    TextTerm(x + 8, y + 4, L"root@sentinel:/var/log/sentinel# tail -f incident.log", COL_TEXT);
    TextTerm(x + w - 48, y + 4, L"LIVE", COL_RED);

    // Decoded command and incident headline lead; raw logs remain secondary.
    TextDecode(sim, x + 12, y + 24, L"INCIDENT // 0x7F3A", accent, 5);
    DrawTraceCommand(sim, x + 314, y + 25, w - 326, 0);
    TextTerm(x + 14, y + 56, sim.phaseBrief[0].c_str(), COL_AMBER, true);
    for (int i = 0; i < 6; ++i) {
        const Firewall& fw = sim.firewalls[i];
        int bx = x + 14 + i * 98;
        Fill(bx, y + 76, 88, 3, RGB(9, 28, 19));
        Fill(bx, y + 76, (int)(88.0f * fw.integrity / 100.0f), 3, StateColor(fw.state));
    }

    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)(x + 2)), PY((float)(y + 88)),
                      PX((float)(x + w - 8)), PY((float)(y + h - 26)));
    const int lineH = 15;
    const int firstLine = y + 92;
    const int maxLines = 20;
    int total = (int)sim.log.entries.size();
    int start = (std::max)(0, total - maxLines);
    int row = 0;
    for (int i = start; i < total; ++i) {
        const LogEntry& e = sim.log.entries[(size_t)i];
        int ly = firstLine + row * lineH;
        COLORREF c = COL_TEXT;
        if (e.sev == Severity::Warn) c = COL_AMBER;
        else if (e.sev == Severity::Crit) c = COL_RED;
        if (e.age < 0.4f) c = Blend(c, COL_WHITE, 0.45f);
        if (e.ghost) {
            COLORREF gc = RGB(126, 224, 186);
            float vis = e.age * 34.0f;
            if (HasCJKTerm(e.text.c_str()))
                TextTypedFont(x + 112, ly, e.text.c_str(), gc, fontCNTerm, vis);
            else
                TextTyped(x + 112, ly, e.text.c_str(), gc, 0, vis);
            row++;
            continue;
        }
        TextTerm(x + 8, ly, e.time.c_str(), RGB(36, 100, 68));
        TextTerm(x + 60, ly, e.module.c_str(), RGB(52, 154, 101));
        TextTerm(x + 112, ly, e.text.c_str(), c, HasCJKTerm(e.text.c_str()));
        row++;
    }
    RestoreDC(memDC, -1);

    TextTerm(x + 10, y + h - 20, L"root@sentinel:/ops# _", COL_GREEN);
    TextTerm(x + w - 202, y + h - 20, L"[ENTER] EMERGENCY OVERRIDE", COL_AMBER);
    Fill(x + w - 5, y + 88, 2, h - 112, RGB(8, 30, 20));
    float frac = total > 0 ? (float)maxLines / (float)total : 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    Fill(x + w - 5, y + 88, 2, (std::max)(4, (int)((h - 112) * frac)), RGB(40, 120, 78));

    // A permanent task substrate stays useful when the intermittent floating consoles are absent.
    Fill(x, 484, w, 204, RGB(1, 5, 4));
    Frame(x, 484, w, 204, RGB(12, 44, 28));
    TextTerm(x + 8, 488, L"OPERATOR JOBS // INDEPENDENT DOCK 02", COL_DIM);
    DrawTraceCommand(sim, x + 350, 484, w - 362, 1);
    static const wchar_t* jobs[3] = { L"TRACE BUS", L"EVIDENCE SEAL", L"ROLLBACK QUEUE" };
    static const wchar_t* commands[3] = { L"trace --follow --vector", L"verify --chain --seal", L"restore --layer --verified" };
    for (int i = 0; i < 3; ++i) {
        int jx = x + 10 + i * 202;
        float activity = 0.24f + 0.72f * TraceFX::Cycle(sim.time, 0.16f + i * 0.07f, i * 0.29f);
        Fill(jx, 516, 190, 112, RGB(0, 7, 5));
        Frame(jx, 516, 190, 112, RGB(13, 48, 34));
        TextTerm(jx + 7, 524, jobs[i], i == 1 ? COL_CYAN : COL_DIM);
        wchar_t id[48];
        swprintf_s(id, L"JOB 0x%04X // SLOT %02d", 0x7F3A + i * 0x21, i + 1);
        TextTerm(jx + 7, 544, id, COL_TEXT);
        TextTerm(jx + 7, 564, commands[i], COL_DIM);
        Bar(jx + 8, 588, 172, 4, activity, i == 1 ? COL_CYAN : COL_GREEN);
        wchar_t state[48];
        swprintf_s(state, L"%03d%% // %ls", (int)(activity * 100.0f), activity > 0.75f ? L"RUNNING" : L"LISTENING");
        TextTerm(jx + 7, 602, state, COL_TEXT);
    }
    TextTerm(x + 12, 652, L"COMMAND HISTORY // persistent substrate // popup-independent", COL_DIM);
    wchar_t dockHash[96];
    swprintf_s(dockHash, L"BUS %04X  //  ACK %04X  //  QUEUE %02d  //  CHANNEL LIVE",
               TraceFX::Hash32((uint32_t)(sim.time * 3.0)) & 0xFFFFu,
               TraceFX::Hash32((uint32_t)(sim.time * 2.0) + 71u) & 0xFFFFu, 3 + sim.opIndex % 7);
    TextTerm(x + 12, 670, dockHash, COL_GREEN);
}

void Renderer::DrawLogStream(Sim& sim, int x, int y, int w, int h, const wchar_t* title,
                             int maxLines, LogStream filter) {
    Fill(x, y, w, h, RGB(1, 7, 4));
    Frame(x, y, w, h, RGB(18, 64, 41));
    Fill(x, y, w, 18, RGB(3, 15, 9));
    TextTerm(x + 8, y + 4, title, COL_GREEN);
    TextTerm(x + w - 48, y + 4, L"LIVE", COL_RED);
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)(x + 2)), PY((float)(y + 22)),
                      PX((float)(x + w - 4)), PY((float)(y + h - 4)));
    const int lineH = 15;
    int total = (int)sim.log.entries.size();
    // Find the latest matching page in bounded history; bursts cannot starve channels.
    int start = 0;
    int remaining = maxLines;
    for (int i = total - 1; i >= 0 && remaining > 0; --i) {
        const LogEntry& e = sim.log.entries[(size_t)i];
        if (filter == LogStream::Both || e.stream == LogStream::Both || e.stream == filter) {
            start = i;
            --remaining;
        }
    }
    int row = 0;
    for (int i = start; i < total; i++) {
        const LogEntry& e = sim.log.entries[(size_t)i];
        if (filter != LogStream::Both && e.stream != LogStream::Both && e.stream != filter) continue;
        int ly = y + 24 + row * lineH;
        if (ly + lineH > y + h - 2) break;
        COLORREF c = e.sev == Severity::Crit ? COL_RED : (e.sev == Severity::Warn ? COL_AMBER : COL_TEXT);
        if (e.ghost) {
            COLORREF gc = RGB(126, 224, 186);
            float vis = e.age * 34.0f;
            if (HasCJKTerm(e.text.c_str()))
                TextTypedFont(x + 112, ly, e.text.c_str(), gc, fontCNTerm, vis);
            else
                TextTyped(x + 112, ly, e.text.c_str(), gc, 0, vis);
            row++;
            continue;
        }
        TextTerm(x + 8, ly, e.time.c_str(), RGB(36, 100, 68));
        TextTerm(x + 60, ly, e.module.c_str(), RGB(52, 154, 101));
        TextTerm(x + 112, ly, e.text.c_str(), c, HasCJKTerm(e.text.c_str()));
        row++;
    }
    RestoreDC(memDC, -1);
}

void Renderer::DrawNodeTable(Sim& sim, int x, int y, int w, int h, int rows) {
    Fill(x, y, w, h, RGB(1, 7, 4));
    Frame(x, y, w, h, RGB(18, 64, 41));
    Fill(x, y, w, 18, RGB(3, 15, 9));
    TextTerm(x + 8, y + 4, L"TARGET QUEUE // NODES", COL_GREEN);
    int rowH = (h - 24) / (rows > 0 ? rows : 1);
    if (rowH < 18) rowH = 18;
    for (int i = 0; i < rows && i < 14; i++) {
        const NodeInfo& n = sim.nodes[i];
        int ry = y + 24 + i * rowH;
        COLORREF c = n.online ? (n.risk > 0.7f ? COL_RED : COL_TEXT) : COL_DIM;
        TextTerm(x + 6, ry, n.name.c_str(), c);
        TextTerm(x + 68, ry, n.ip.c_str(), RGB(36, 100, 68));
        TextTerm(x + 150, ry, n.role.c_str(), RGB(52, 154, 101));
        wchar_t risk[24];
        swprintf_s(risk, L"%02d", (int)(n.risk * 100.0f));
        TextTerm(x + w - 24, ry, risk, c);
    }
}

void Renderer::DrawPhaseBrief(Sim& sim, int x, int y, int w, int lines) {
    Fill(x, y, w, 16 + lines * 14, RGB(0, 6, 4));
    Frame(x, y, w, 16 + lines * 14, RGB(12, 44, 28));
    TextTerm(x + 6, y + 3, L"PHASE BRIEF // CURRENT DIRECTIVE", COL_DIM);
    for (int i = 0; i < lines && i < 3; i++) {
        TextTerm(x + 6, y + 18 + i * 14, sim.phaseBrief[i].c_str(),
                 i == 0 ? COL_AMBER : COL_TEXT, HasCJKTerm(sim.phaseBrief[i].c_str()));
    }
}

void Renderer::DrawSplitOps(Sim& sim) {
    DrawLogStream(sim, 18, 52, 600, 350, L"ATTACK CHANNEL // UNTRUSTED", 20, LogStream::Attack);
    DrawLogStream(sim, 630, 52, 632, 350, L"DEFENSE CHANNEL // VERIFIED", 20, LogStream::Defense);
    DrawTraceCommand(sim, 310, 52, 250, 0);
    DrawTraceCommand(sim, 940, 52, 250, 1);
    Line(624, 76, 624, 396, RGB(18, 62, 46), 1);
    const bool defense = sim.act == Act::Countermeasure;
    for (int i = 0; i < 3; ++i) {
        int py = 154 + i * 102;
        float t = TraceFX::Cycle(sim.time, 0.56f + i * 0.11f, i * 0.31f);
        PacketLine(sim, defense ? 638 : 610, py, defense ? 610 : 638, py, t,
                   defense ? COL_CYAN : COL_RED);
        TextC(624, py + 8, defense ? L"ACK" : L"INJ", defense ? COL_CYAN : COL_DIM, 0);
    }

    // The attack rail always has a structured route below its variable-volume log.
    Fill(18, 416, 600, 216, RGB(0, 6, 4));
    Frame(18, 416, 600, 216, RGB(18, 62, 42));
    TextTerm(28, 424, L"INGRESS LADDER // LIVE ROUTE", COL_RED);
    TextTerm(398, 424, L"SOURCE > RELAY > TARGET > VAULT", COL_DIM);
    const int routeNodes[4] = { 0, 1, 13, 2 };
    for (int i = 0; i < 4; ++i) {
        int rx = 32 + i * 144;
        int node = routeNodes[i];
        COLORREF c = i == 3 ? COL_RED : (i == 2 ? COL_AMBER : COL_TEXT);
        Fill(rx, 452, 124, 64, Blend(COL_BLACK, c, 0.06f));
        Frame(rx, 452, 124, 64, Blend(COL_BLACK, c, 0.44f));
        TextC(rx + 62, 458, sim.nodes[node].name.c_str(), c, 0);
        TextC(rx + 62, 476, sim.nodes[node].ip.c_str(), COL_DIM, 0);
        wchar_t risk[32];
        swprintf_s(risk, L"HOP %02d // RISK %03d", i + 1, (int)(sim.nodes[node].risk * 100.0f));
        TextC(rx + 62, 496, risk, c, 0);
        if (i < 3) PacketLine(sim, rx + 124, 484, rx + 144, 484,
                              TraceFX::Cycle(sim.time, 0.88f, i * 0.27f), COL_RED);
    }
    TextTerm(30, 532, L"ACTIVE SIGNATURES // SCROLLING EVIDENCE", COL_DIM);
    DrawHexTape(sim, 30, 552, 564, 3, 0);
    wchar_t attackFoot[112];
    swprintf_s(attackFoot, L"ROUTE 0x7F3A // 4 HOPS // %ls // RETRY %02d",
               sim.OpPhaseName(), (int)(sim.time * 2.0) % 60);
    TextTerm(30, 610, attackFoot, COL_AMBER);

    // Defense no longer depends on slow log production to fill its lower half.
    Fill(630, 416, 632, 216, RGB(0, 6, 4));
    Frame(630, 416, 632, 216, RGB(18, 62, 42));
    TextTerm(640, 424, L"DEFENSE VERIFICATION MATRIX // PERSISTENT", COL_CYAN);
    TextTerm(1080, 424, L"ACK // HASH // STATE", COL_DIM);
    for (int i = 0; i < 6; ++i) {
        const Firewall& fw = sim.firewalls[i];
        int y = 450 + i * 24;
        COLORREF c = fw.state == FWState::Offline ? COL_RED :
                     (fw.state == FWState::Rebuilding ? COL_CYAN : COL_GREEN);
        wchar_t row[112];
        swprintf_s(row, L"L%02d %-12.12ls  %03d%%  %04X", i + 1, fw.name.c_str(),
                   (int)fw.integrity, TraceFX::Hash32((uint32_t)(i * 97 + (int)(sim.time * 2.0))) & 0xFFFFu);
        TextTerm(642, y, row, c);
        Bar(980, y + 3, 168, 4, fw.integrity / 100.0f, c);
        TextTerm(1170, y, fw.state == FWState::Rebuilding ? L"VERIFY" : L"ACK", c);
    }
    wchar_t defenseFoot[112];
    swprintf_s(defenseFoot, L"CHAIN VERIFIED // STAGE %d/6 // %03d%% // HASH %04X",
               ((int)sim.campaign.defense % 6) + 1, (int)(sim.campaign.defenseProgress * 100.0f),
               TraceFX::Hash32((uint32_t)(sim.time * 4.0) + 0x7F3Au) & 0xFFFFu);
    TextTerm(642, 606, defenseFoot, COL_CYAN);

    DrawPhaseBrief(sim, 18, 636, 600, 2);
    DrawPhaseBrief(sim, 630, 636, 632, 2);
}

void Renderer::DrawWarRoom(Sim& sim) {
    DrawNodeTable(sim, 18, 52, 232, 580, 14);
    Fill(270, 52, 560, 580, RGB(1, 6, 6));
    Frame(270, 52, 560, 580, RGB(16, 54, 52));
    Fill(270, 52, 560, 18, RGB(3, 13, 13));
    TextTerm(278, 56, L"THEATER MAP // STRATEGIC RISK", COL_CYAN);
    DrawTraceCommand(sim, 552, 52, 266, 2);

    // Fixed cartography: this scene explains relationships; BOUNDARY owns orbital combat.
    static const int px[14] = { 340, 480, 640, 750, 316, 445, 570, 690, 780, 360, 490, 630, 740, 570 };
    static const int py[14] = { 174, 146, 166, 218, 292, 258, 226, 300, 364, 420, 452, 428, 470, 354 };
    for (int i = 0; i < 6; ++i) Line(292, 126 + i * 76, 808, 126 + i * 76, RGB(6, 27, 23), 1);
    for (int i = 0; i < 7; ++i) Line(306 + i * 80, 106, 306 + i * 80, 510, RGB(6, 25, 22), 1);

    static const int edges[18][2] = {
        {0,1},{1,2},{2,3},{0,4},{1,5},{2,6},{3,7},{4,5},{5,6},{6,7},
        {4,9},{5,10},{6,13},{7,8},{9,10},{10,11},{11,12},{8,12}
    };
    int hottest = 0;
    for (int i = 1; i < 14; ++i) if (sim.nodes[i].risk > sim.nodes[hottest].risk) hottest = i;
    for (int e = 0; e < 18; ++e) {
        int a = edges[e][0], b = edges[e][1];
        float risk = (sim.nodes[a].risk + sim.nodes[b].risk) * 0.5f;
        COLORREF c = risk > 0.70f ? COL_RED : (risk > 0.44f ? COL_AMBER : RGB(37, 103, 78));
        Line(px[a], py[a], px[b], py[b], Blend(c, COL_BLACK, 0.55f), 1);
        if ((e + sim.opIndex) % 3 == 0)
            PacketLine(sim, px[a], py[a], px[b], py[b], TraceFX::Cycle(sim.time, 0.26f, e * 0.071f), c);
    }
    for (int i = 0; i < 14; ++i) {
        COLORREF c = !sim.nodes[i].online ? COL_DIM :
                     (sim.nodes[i].risk > 0.72f ? COL_RED : (sim.nodes[i].risk > 0.45f ? COL_AMBER : COL_GREEN));
        Fill(px[i] - 4, py[i] - 4, 9, 9, c);
        Frame(px[i] - 8, py[i] - 8, 17, 17, Blend(c, COL_BLACK, 0.40f));
        wchar_t label[56];
        swprintf_s(label, L"N%02d %03d", i + 1, (int)(sim.nodes[i].risk * 100.0f));
        TextTerm(px[i] + 10, py[i] - 7, label, c);
        if (i == hottest) DrawTargetBrackets(sim, px[i] - 16, py[i] - 16, 32, 32, COL_RED);
    }
    wchar_t lock[112];
    swprintf_s(lock, L"STRATEGIC LOCK // N%02d // RISK %03d // %04X", hottest + 1,
               (int)(sim.nodes[hottest].risk * 100.0f), TraceFX::Hash32((uint32_t)sim.opIndex + 0x7F3Au) & 0xFFFFu);
    TextTerm(282, 78, lock, COL_RED);
    TextTerm(292, 494, L"FIXED TOPOLOGY // 14 NODES // 18 ROUTES // NO ORBITAL DRIFT", COL_DIM);

    Fill(292, 522, 516, 86, RGB(0, 7, 5));
    Frame(292, 522, 516, 86, RGB(15, 54, 39));
    TextTerm(302, 530, L"THEATER SUMMARY // PROPAGATION CONTROL", COL_DIM);
    wchar_t mapStats[128];
    swprintf_s(mapStats, L"GRID %d%%  //  OFF %d  //  REB %d  //  PRIORITY N%02d",
               (int)sim.AvgIntegrity(), sim.CountState(FWState::Offline),
               sim.CountState(FWState::Rebuilding), hottest + 1);
    TextTerm(302, 552, mapStats, COL_TEXT);
    TextTerm(302, 574, sim.phaseBrief[1].c_str(), COL_AMBER, true);
    TextTerm(302, 592, L"ROLE // STRATEGIC RELATIONSHIP MAP", COL_CYAN);

    DrawLogStream(sim, 850, 52, 412, 580, L"REMOTE STREAMS // EVIDENCE", 32, LogStream::Evidence);
    DrawPhaseBrief(sim, 18, 636, 232, 2);
    DrawPhaseBrief(sim, 850, 636, 412, 2);
}

void Renderer::DrawFileTree(Sim& sim, int x, int y, int w, int h) {
    Fill(x, y, w, h, RGB(1, 7, 4));
    Frame(x, y, w, h, RGB(18, 64, 41));
    TextTerm(x + 8, y + 6, L"/evidence/", COL_AMBER);
    int row = 0;
    for (const auto& f : sim.files.files) {
        const wchar_t* st = L"OK";
        COLORREF c = COL_TEXT;
        switch (f.status) {
        case FileStatus::Modified: st = L"MODIFIED"; c = COL_AMBER; break;
        case FileStatus::Deleted:  st = L"DELETED";  c = COL_RED;   break;
        case FileStatus::Restored: st = L"RESTORED"; c = COL_CYAN;  break;
        default: break;
        }
        const wchar_t* name = wcsrchr(f.path.c_str(), L'\\');
        if (!name) name = wcsrchr(f.path.c_str(), L'/');
        name = name ? name + 1 : f.path.c_str();
        wchar_t line[128];
        swprintf_s(line, L"  %-14.14ls %ls", name, st);
        TextTerm(x + 8, y + 28 + row * 17, line, c);
        row++;
    }
    TextTerm(x + 8, y + 28 + row * 17 + 6, L"/snapshots/ 6 points", COL_DIM);
    TextTerm(x + 8, y + 28 + row * 17 + 23, L"/quarantine/ 0x7F3A", COL_DIM);

    int cy = y + 190;
    TextTerm(x + 8, cy, L"/custody/ // chain", COL_AMBER);
    for (int i = 0; i < 7; i++) {
        const MonitoredFile& f = sim.files.files[i % 6];
        wchar_t line[128];
        int tm = (int)sim.time + i * 37;
        swprintf_s(line, L"  %02d:%02d  %-10.10ls %ls",
                   (tm / 60) % 100, tm % 60,
                   f.hash.c_str(), f.status == FileStatus::Deleted ? L"sealed" : L"verified");
        TextTerm(x + 8, cy + 18 + i * 16, line,
                 f.status == FileStatus::Deleted ? COL_RED : RGB(92, 156, 118));
    }
    TextTerm(x + 8, cy + 136, L"/artifacts/ // index", COL_AMBER);
    for (int i = 0; i < 8; i++) {
        const MonitoredFile& f = sim.files.files[i % 6];
        const wchar_t* base = wcsrchr(f.path.c_str(), L'\\');
        base = base ? base + 1 : f.path.c_str();
        wchar_t line[128];
        swprintf_s(line, L"  %-12.12ls %ls", base, f.hash.c_str());
        TextTerm(x + 8, cy + 154 + i * 16, line, RGB(74, 134, 100));
    }
    int sealed = 0, pending = 0;
    for (const auto& f : sim.files.files) {
        if (f.status == FileStatus::Deleted) sealed++;
        if (f.status == FileStatus::Modified || f.status == FileStatus::Restored) pending++;
    }
    wchar_t integrity[96];
    swprintf_s(integrity, L"%d objects / %d sealed / %d pending", 6, sealed, pending);
    TextTerm(x + 8, y + h - 44, L"EVIDENCE INTEGRITY", COL_DIM);
    TextTerm(x + 8, y + h - 28, integrity, COL_AMBER);
}

void Renderer::DrawForensic(Sim& sim) {
    DrawFileTree(sim, 18, 52, 240, 636);
    DrawHexTape(sim, 26, 520, 224, 5, 3);
    DrawLogStream(sim, 270, 52, 560, 580, L"EVIDENCE TIMELINE // CHAIN OF CUSTODY", 32, LogStream::Evidence);
    DrawTraceCommand(sim, 490, 52, 280, 3);
    DrawLogStream(sim, 850, 52, 412, 580, L"EXFIL LEDGER // SEALED", 32, LogStream::Ledger);
    DrawTraceCommand(sim, 1000, 52, 202, 0);
    DrawPhaseBrief(sim, 270, 636, 560, 2);
    DrawPhaseBrief(sim, 850, 636, 412, 2);
}

void Renderer::DrawContainment(Sim& sim) {
    for (int i = 0; i < 6; i++) {
        int x = 38 + i * 202;
        const COLORREF sc = StateColor(sim.firewalls[i].state);
        Fill(x, 70, 180, 560, RGB(1, 7, 4));
        Frame(x, 70, 180, 560, RGB(18, 64, 41));
        Fill(x, 70, 180, 3, sc);
        SaveDC(memDC);
        IntersectClipRect(memDC, PX((float)(x + 2)), PY(72.0f),
                          PX((float)(x + 178)), PY(628.0f));
        wchar_t b[96];
        swprintf_s(b, L"L%02d %ls", i + 1, sim.firewalls[i].name.c_str());
        TextTerm(x + 10, 80, b, COL_TEXT);
        wchar_t bigPct[16];
        swprintf_s(bigPct, L"%d%%", (int)sim.firewalls[i].integrity);
        TextR(x + 168, 96, bigPct, sc, 5);
        TextTerm(x + 10, 126, StateName(sim.firewalls[i].state), sc);
        int fill = (int)(sim.firewalls[i].integrity / 5.0f);
        wchar_t bar[24];
        for (int k = 0; k < 20; k++) bar[k] = k < fill ? L'#' : L'.';
        bar[20] = 0;
        TextTerm(x + 10, 146, bar, sc);
        wchar_t pct[64];
        swprintf_s(pct, L"queue=%d  eta=%02ds", (int)sim.firewalls[i].repairRate, 12 + i * 3);
        TextTerm(x + 10, 164, pct, COL_DIM);

        const wchar_t* vec[6] = { L"MULTI-STAGE PAYLOAD", L"LATERAL PROBE", L"KERNEL ESCALATION",
                                  L"EGRESS TUNNEL", L"VOLUME ENCRYPTION", L"PERSISTENCE" };
        swprintf_s(b, L"VECTOR // %ls", vec[i]);
        TextTerm(x + 10, 180, b, COL_AMBER);
        int shown = 0;
        int totalFeed = (int)sim.layerFeed[i].size();
        for (int k = totalFeed - 1; k >= 0 && shown < 8; --k) {
            TextTerm(x + 10, 198 + shown * 14, sim.layerFeed[i][(size_t)k].c_str(),
                     RGB(88, 150, 112));
            shown++;
        }
        TextTerm(x + 10, 330, L"QUEUE // REPAIR QUEUE", COL_DIM);
        wchar_t q[96];
        swprintf_s(q, L"hash %08X  eta %02ds", (unsigned)(sim.time * 7919.0 + i * 131.0),
                   (int)(12 + i * 3));
        TextTerm(x + 10, 348, q, RGB(74, 134, 100));
        const wchar_t* actLine[6] = { L"ROLLBACK REGISTERED", L"ISOLATION POLICY PUSHED", L"KERNEL PATCH PENDING",
                                      L"EGRESS TUNNEL BLOCKED", L"SHADOW COPY SEALED", L"PERSISTENCE CLEARED" };
        TextTerm(x + 10, 364, actLine[i], RGB(74, 134, 100), true);
        TextTerm(x + 10, 500, L"AUTO RESPONSE", COL_DIM);
        TextTerm(x + 10, 520, sim.summary[2].c_str(), COL_AMBER, true);
        RestoreDC(memDC, -1);
    }
    const wchar_t* step = L"DETECT";
    switch (sim.campaign.defense) {
    case DefenseStep::Detect: step = L"DETECT"; break;
    case DefenseStep::Isolate: step = L"ISOLATE"; break;
    case DefenseStep::Snapshot: step = L"SNAPSHOT"; break;
    case DefenseStep::Rollback: step = L"ROLLBACK"; break;
    case DefenseStep::Rebuild: step = L"REBUILD"; break;
    case DefenseStep::Verify: step = L"VERIFY"; break;
    default: break;
    }
    wchar_t pipeline[128];
    swprintf_s(pipeline, L"AUTO-DEFENSE // STAGE %d/6 // %ls // %d%%",
               ((int)sim.campaign.defense % 6) + 1, step,
               (int)(sim.campaign.defenseProgress * 100.0f));
    TextTerm(38, 620, pipeline, COL_AMBER);
    const int activeStage = (int)sim.campaign.defense % 6;
    for (int i = 0; i < 6; ++i) {
        const int nx = 128 + i * 202;
        if (i < 5) {
            Line(nx + 5, 638, nx + 197, 638, RGB(15, 55, 39), 1);
            if (i == (std::min)(activeStage, 4)) {
                PacketLine(sim, nx + 5, 638, nx + 197, 638,
                           sim.campaign.defenseProgress, COL_CYAN);
            }
        }
        COLORREF nc = i < activeStage ? COL_GREEN : (i == activeStage ? COL_AMBER : RGB(24, 62, 44));
        Fill(nx - 3, 635, 7, 7, nc);
        if (i == activeStage) DrawTargetBrackets(sim, nx - 9, 629, 18, 18, nc);
    }
    DrawSituationStrip(sim, 18, 648, 1244, 2);
}

void Renderer::DrawBlackBox(Sim& sim) {
    Fill(0, 44, VW, 652, RGB(0, 2, 2));
    const COLORREF accent = sim.act == Act::Intrusion ? COL_CYAN :
                            (sim.act == Act::Countermeasure ? COL_GREEN : COL_RED);
    const int goal = (int)sim.campaign.goal % (int)AttackGoal::Count;
    static const wchar_t* goalNames[6] = {
        L"DATA EXFIL", L"RANSOMWARE", L"CREDENTIAL DUMP", L"HASHRATE HIJACK", L"PERSISTENCE", L"AVAILABILITY"
    };
    static const wchar_t* techniques[6] = {
        L"T1041 // C2 EGRESS", L"T1486 // VOLUME ENCRYPTION", L"T1003.001 // CREDENTIAL DUMP",
        L"T1496 // RESOURCE HIJACK", L"T1071 // COVERT CHANNEL", L"T1489 // SERVICE STOP"
    };
    static const wchar_t* headlines[3][6] = {
        { L"ANOMALOUS EGRESS ESTABLISHED", L"VOLUME ENCRYPTION IDENTIFIED", L"PRIVILEGED SESSION REPLAY", L"ANOMALOUS COMPUTE USAGE IDENTIFIED", L"COVERT PERSISTENCE SIGNAL", L"CRITICAL SERVICE AVAILABILITY DROP" },
        { L"DATA BOUNDARY FAILING", L"ENCRYPTION SPREAD CROSSING BOUNDS", L"CREDENTIAL CHAIN ABUSED", L"COMPUTE RESOURCES HIJACKED", L"COVERT CHANNEL STILL BEACONING", L"SERVICE OUTAGE SPREADING" },
        { L"EGRESS CHANNEL CONVERGING", L"SHADOW ROLLBACK IN PROGRESS", L"ANOMALOUS TICKETS REVOKING", L"MALICIOUS COMPUTE UNLOADING", L"PERSISTENCE ITEMS BEING REMOVED", L"CORE SERVICES RECOVERING" }
    };
    static const wchar_t* subheads[6] = {
        L"EGRESS CONTROL // CHANNEL CONVERGENCE", L"ROLLBACK CHAIN // VOLUME RECOVERY",
        L"SESSION CONTROL // TOKEN REVOCATION", L"RESOURCE CONTROL // PAYLOAD UNLOAD",
        L"RESIDENCE CONTROL // COVERT TRACE", L"AVAILABILITY CONTROL // SERVICE RESTORE"
    };
    const int actIndex = (int)sim.act;
    TextC(640, 60, L"LAST SIGNAL // INCIDENT 0x7F3A // ADAPTIVE CLASS", COL_DIM, 1);
    TextDecodeC(sim, 640, 92, headlines[actIndex][goal], accent, 6);
    TextDecodeC(sim, 640, 138, subheads[goal], Blend(accent, COL_BLACK, 0.24f), 1, 2);
    DrawTargetBrackets(sim, 350, 80, 580, 76, accent);

    // A live incident waveform turns the former empty center into a signal-convergence display.
    Fill(300, 166, 680, 148, RGB(0, 6, 5));
    Frame(300, 166, 680, 148, Blend(accent, COL_BLACK, 0.66f));
    TextTerm(310, 174, L"SIGNAL COLLAPSE // LIVE EVIDENCE", accent);
    TextTerm(310, 194, L"SRC DC:A6:32:55:91:7F // NODE vault-03", COL_DIM);
    wchar_t channel[96];
    swprintf_s(channel, L"CH 0x7F3A // %ls // RISK %03d", techniques[goal], sim.DamageLevel());
    TextTerm(660, 194, channel, COL_DIM);
    for (int i = 0; i < 6; ++i) Line(318, 218 + i * 15, 962, 218 + i * 15, RGB(7, 30, 23), 1);
    for (int i = 0; i < 9; ++i) Line(326 + i * 78, 208, 326 + i * 78, 300, RGB(6, 24, 20), 1);
    const float motionTime = sim.cfg.motion == L"reduced" ? 1.0f : (float)sim.time;
    for (int lane = 0; lane < 3; ++lane) {
        for (int i = 0; i < 96; ++i) {
            float x0 = (float)i / 96.0f;
            float x1 = (float)(i + 1) / 96.0f;
            float convergence = sim.act == Act::Countermeasure ? (0.28f + x0 * 0.72f) : 1.0f;
            float y0 = sinf(x0 * (18.0f + goal * 3.0f) + motionTime * (2.8f + lane)) * (16.0f - lane * 3.0f);
            float y1 = sinf(x1 * (18.0f + goal * 3.0f) + motionTime * (2.8f + lane)) * (16.0f - lane * 3.0f);
            y0 += (TraceFX::Unit((uint32_t)(i * 79 + goal * 991 + lane * 37)) - 0.5f) * 13.0f;
            y1 += (TraceFX::Unit((uint32_t)((i + 1) * 79 + goal * 991 + lane * 37)) - 0.5f) * 13.0f;
            int py0 = 252 + lane * 13 + (int)(y0 * convergence);
            int py1 = 252 + lane * 13 + (int)(y1 * convergence);
            Line(322 + i * 6, py0, 322 + (i + 1) * 6, py1,
                 lane == 0 ? accent : Blend(accent, COL_BLACK, 0.44f + lane * 0.15f), 1);
        }
    }
    PacketLine(sim, 322, 292, 958, 292, TraceFX::Cycle(sim.time, 0.92f, goal * 0.13f), accent);
    TextTerm(310, 296, L"RAW SIGNAL", COL_DIM);
    TextTerm(884, 296, sim.act == Act::Countermeasure ? L"CONVERGING" : L"UNTRUSTED", accent);

    // Three fixed incident cards keep the summary dense even when log volume is low.
    const int cardX[3] = { 300, 528, 756 };
    const wchar_t* cardTitle[3] = { L"01 // SOURCE", L"02 // VECTOR", L"03 // RESPONSE" };
    for (int i = 0; i < 3; ++i) {
        Fill(cardX[i], 328, 210, 126, RGB(1, 8, 6));
        Frame(cardX[i], 328, 210, 126, RGB(15, 53, 39));
        TextTerm(cardX[i] + 10, 336, cardTitle[i], i == 1 ? accent : COL_DIM);
    }
    Text(312, 360, L"vault-03", COL_WHITE, 4);
    TextTerm(312, 394, L"192.0.2.77 // REMOTE", COL_TEXT);
    TextTerm(312, 414, L"MAC DC:A6:32:55:91:7F", COL_DIM);
    TextTerm(312, 434, L"SESSION 0x7F3A // ACTIVE", COL_CYAN);

    TextC(633, 360, goalNames[goal], accent, 3);
    TextC(633, 394, techniques[goal], COL_TEXT, 0);
    wchar_t metric[112];
    switch (sim.campaign.goal) {
    case AttackGoal::Ransomware: swprintf_s(metric, L"VOLUME ENCRYPT %d%% // ROLLBACK POINTS %d", (int)sim.campaign.encryptPct, sim.CountState(FWState::Rebuilding)); break;
    case AttackGoal::CredentialTheft: swprintf_s(metric, L"CREDENTIALS %d SETS // PRIVILEGED SESSIONS 2", sim.campaign.credentials); break;
    case AttackGoal::CryptoMiner: swprintf_s(metric, L"ANOMALOUS COMPUTE %.0f%% // PAYLOAD ACTIVE", sim.campaign.minerPct); break;
    case AttackGoal::Espionage: swprintf_s(metric, L"PERSISTENCE %d ITEMS // COVERT CHANNELS 2", sim.campaign.documents + 4); break;
    case AttackGoal::Sabotage: swprintf_s(metric, L"INTERRUPTIONS %d // CORE AVAILABILITY REVIEW", sim.campaign.servicesDown); break;
    default: swprintf_s(metric, L"EGRESS %.1f MB // C2 MARKED", sim.campaign.dataMB); break;
    }
    TextC(633, 420, metric, COL_AMBER, 1);

    static const wchar_t* defenseNames[6] = { L"DETECT", L"ISOLATE", L"SNAPSHOT", L"ROLLBACK", L"REBUILD", L"VERIFY" };
    const int defense = (int)sim.campaign.defense % 6;
    TextC(861, 358, defenseNames[defense], COL_CYAN, 4);
    wchar_t response[80];
    swprintf_s(response, L"STAGE %d/6 // %.0f%%", defense + 1, sim.campaign.defenseProgress * 100.0f);
    TextC(861, 394, response, COL_TEXT, 1);
    Bar(774, 420, 174, 5, sim.campaign.defenseProgress, COL_CYAN);
    TextC(861, 436, L"POLICY // REVIEW + SEAL", COL_DIM, 0);

    // Persistent six-stage response chain; every cell has content before and after activation.
    Fill(300, 468, 680, 84, RGB(0, 6, 5));
    Frame(300, 468, 680, 84, RGB(15, 53, 39));
    TextTerm(310, 474, L"RESPONSE CHAIN // VERIFIED TRANSITIONS", COL_DIM);
    for (int i = 0; i < 6; ++i) {
        int x = 316 + i * 108;
        COLORREF c = i < defense ? COL_GREEN : (i == defense ? accent : RGB(32, 72, 55));
        Fill(x, 500, 94, 32, Blend(COL_BLACK, c, i == defense ? 0.13f : 0.05f));
        Frame(x, 500, 94, 32, Blend(COL_BLACK, c, 0.54f));
        TextC(x + 47, 505, defenseNames[i], c, 0);
        wchar_t state[24];
        swprintf_s(state, i < defense ? L"VERIFIED" : (i == defense ? L"%03d%%" : L"QUEUED"), (int)(sim.campaign.defenseProgress * 100.0f));
        TextC(x + 47, 519, state, c, 0);
        if (i < 5) PacketLine(sim, x + 94, 516, x + 108, 516, TraceFX::Cycle(sim.time, 0.7f, i * 0.17f), c);
    }
    TextC(640, 566, sim.summary[3].c_str(), COL_TEXT, 1);
    TextC(640, 588, sim.summary[4].c_str(), COL_AMBER, 1);
    TextC(640, 612, L"[ENTER] MANUAL OVERRIDE // HUMAN TAKEOVER", accent, 2);
    DrawTraceCommand(sim, 490, 634, 300, 2);

    // Left host rail: fixed state, integrity bars and memory evidence.
    Fill(24, 52, 246, 580, RGB(1, 5, 4));
    Frame(24, 52, 246, 580, RGB(28, 55, 43));
    TextTerm(36, 60, L"HOST // MEMORY // LIVE", COL_DIM);
    wchar_t hostLines[6][96];
    swprintf_s(hostLines[0], L"ACT // %ls", sim.CurrentAct().cn);
    swprintf_s(hostLines[1], L"PHASE %02d/14 // %ls", sim.opIndex + 1, sim.OpPhaseName());
    swprintf_s(hostLines[2], L"NODES %d/14 // ONLINE", 14 - sim.CountState(FWState::Offline));
    swprintf_s(hostLines[3], L"INTEGRITY %d%% // RISK %d", (int)sim.AvgIntegrity(), sim.DamageLevel());
    swprintf_s(hostLines[4], L"CYCLE %02d // INCIDENT 0x7F3A", sim.cycle);
    swprintf_s(hostLines[5], L"TRACE // ROOT SESSION");
    for (int i = 0; i < 6; ++i) TextTerm(36, 88 + i * 18, hostLines[i], i == 3 ? accent : COL_TEXT, true);
    TextTerm(36, 210, L"SIX LAYERS // INTEGRITY", COL_DIM);
    for (int i = 0; i < 6; ++i) {
        const Firewall& fw = sim.firewalls[i];
        int y = 238 + i * 38;
        COLORREF c = fw.state == FWState::Offline ? COL_RED : (fw.state == FWState::Rebuilding ? COL_CYAN : COL_GREEN);
        wchar_t line[72];
        swprintf_s(line, L"L%02d %-10.10ls %03d%%", i + 1, fw.name.c_str(), (int)fw.integrity);
        TextTerm(36, y, line, c);
        Bar(38, y + 17, 216, 4, fw.integrity / 100.0f, c);
    }
    TextTerm(36, 482, L"MEMORY CHANNEL 05 // EVIDENCE", COL_DIM);
    DrawHexTape(sim, 36, 506, 220, 7, 5);

    // Right channel rail adapts its metrics to the selected incident class.
    Fill(1010, 52, 246, 580, RGB(1, 5, 4));
    Frame(1010, 52, 246, 580, RGB(28, 55, 43));
    TextTerm(1022, 60, L"EGRESS // CHANNEL // LIVE", COL_DIM);
    wchar_t channelLines[7][96];
    swprintf_s(channelLines[0], L"EXFIL %.1f MB", sim.campaign.dataMB);
    swprintf_s(channelLines[1], L"ENCRYPT %d%% // MINER %.0f%%", (int)sim.campaign.encryptPct, sim.campaign.minerPct);
    swprintf_s(channelLines[2], L"CRED %d // DOCS %d", sim.campaign.credentials, sim.campaign.documents);
    swprintf_s(channelLines[3], L"SERVICES DOWN %d", sim.campaign.servicesDown);
    swprintf_s(channelLines[4], L"C2 %ls", sim.campaign.c2.c_str());
    swprintf_s(channelLines[5], L"RISK %d/100 // QUEUE %02d", sim.DamageLevel(), 5 + sim.opIndex);
    swprintf_s(channelLines[6], L"CHANNEL // AES-256-GCM");
    for (int i = 0; i < 7; ++i) TextTerm(1022, 88 + i * 19, channelLines[i], i == 5 ? COL_RED : COL_TEXT);
    TextTerm(1022, 238, L"REMOTE NODES // LIVE RISK", COL_DIM);
    for (int i = 0; i < 6; ++i) {
        int node = i + 8;
        int y = 266 + i * 36;
        COLORREF c = sim.nodes[node].online ? (sim.nodes[node].risk > 0.7f ? COL_RED : COL_GREEN) : COL_DIM;
        wchar_t line[84];
        swprintf_s(line, L"N%02d %-8.8ls %03d", node + 1, sim.nodes[node].role.c_str(), (int)(sim.nodes[node].risk * 100.0f));
        TextTerm(1022, y, line, c);
        Bar(1024, y + 16, 216, 3, sim.nodes[node].risk, c);
    }
    TextTerm(1022, 494, L"CHANNEL 06 // HASH TAPE", COL_DIM);
    DrawHexTape(sim, 1022, 518, 220, 6, 6);

    DrawPhaseBrief(sim, 300, 652, 680, 1);
}

void Renderer::DrawPane(Sim& sim, const Popup& p, int index) {
    int x = (int)p.x;
    int y = (int)p.y;
    int w = (int)p.w;
    int h = (int)p.h;
    float slide = sim.cfg.motion == L"reduced" ? 1.0f : easeOutCubic(p.slide);
    x += (int)((1.0f - slide) * 30.0f);
    y += (int)((1.0f - slide) * 7.0f);
    bool focused = (sim.focusBox == index);

    COLORREF accent = p.aborted ? COL_RED : (p.kind == 0 ? COL_RED : COL_GREEN);
    Fill(x + 4, y + 5, w, h, COL_BLACK);
    Fill(x, y, w, h, RGB(1, 7, 4));
    Frame(x, y, w, h, focused ? COL_CYAN : Blend(accent, RGB(20, 20, 20), 0.45f));
    Fill(x, y, 2, h, Blend(accent, COL_BLACK, 0.25f));
    Fill(x, y, w, 18, RGB(3, 15, 9));
    TextTerm(x + 8, y + 4, p.title.c_str(), accent);
    TextTerm(x + w - 62, y + 4, p.tag.c_str(), COL_DIM);

    const ConsoleBoxState& bs = sim.boxState[index];
    int page = bs.page < 0 ? 0 : (bs.page > 2 ? 2 : bs.page);
    const wchar_t* tabs[3] = { L"[1] LOG", L"[2] FILES", L"[3] JOB" };
    for (int i = 0; i < 3; i++) {
        int tx = x + 8 + i * 72;
        TextTerm(tx, y + 20, tabs[i], i == page ? COL_WHITE : RGB(46, 120, 84));
    }
    Line(x + 4, y + 36, x + w - 4, y + 36, RGB(14, 48, 32), 1);

    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)(x + 2)), PY((float)(y + 38)),
                      PX((float)(x + w - 4)), PY((float)(y + h - 46)));
    const int lineH = 12;
    int maxLines = (h - 96) / lineH;
    if (maxLines < 4) maxLines = 4;
    int total = (int)p.lines.size();
    int start = (std::max)(0, total - maxLines);
    int row = 0;
    if (page == 1) {
        for (int i = 0; i < 6; i++) {
            const auto& f = sim.files.files[i];
            const wchar_t* st = L"OK";
            COLORREF c = RGB(150, 220, 180);
            if (f.status == FileStatus::Modified) { st = L"MODIFIED"; c = COL_AMBER; }
            else if (f.status == FileStatus::Deleted) { st = L"DELETED"; c = COL_RED; }
            else if (f.status == FileStatus::Restored) { st = L"RESTORED"; c = COL_CYAN; }
            const wchar_t* name = wcsrchr(f.path.c_str(), L'\\');
            if (!name) name = wcsrchr(f.path.c_str(), L'/');
            name = name ? name + 1 : f.path.c_str();
            wchar_t line[128];
            swprintf_s(line, L"%-16.16ls %ls", name, st);
            TextTerm(x + 8, y + 42 + i * lineH, line, c);
        }
        TextTerm(x + 8, y + 42 + 6 * lineH, L"/snapshots/  6 restore points", COL_DIM);
        TextTerm(x + 8, y + 42 + 7 * lineH, L"/quarantine/ 0x7F3A", COL_DIM);
        TextTerm(x + 8, y + 42 + 8 * lineH, L"/custody/    7 sealed entries", COL_DIM);
        TextTerm(x + 8, y + 42 + 9 * lineH, L"/artifacts/  memory.raw pcap/", COL_DIM);
    } else if (page == 2) {
        TextTerm(x + 8, y + 42, L"JOB         TARGET              STATE", COL_DIM);
        TextTerm(x + 8, y + 56, L"0x7F3A      egress_filter       running", COL_TEXT);
        TextTerm(x + 8, y + 70, L"0x7F3B      kernel_watch        queued", COL_AMBER);
        TextTerm(x + 8, y + 84, L"0x7F3C      snapshot_rollback   blocked", COL_RED);
        TextTerm(x + 8, y + 98, L"0x7F3D      custody_export      sealed", COL_CYAN);
        wchar_t jobline[128];
        swprintf_s(jobline, L"queue=%d  progress=%d%%  abort=%ls",
                   3 + (int)(sim.time / 30.0f) % 9,
                   (int)(clampf(p.progress, 0.0f, 1.0f) * 100.0f),
                   p.aborted ? L"YES" : L"armed");
        TextTerm(x + 8, y + 118, jobline, COL_DIM);
        TextTerm(x + 8, y + 132, L"scope: kernel memory + egress", COL_DIM);
        TextTerm(x + 8, y + 146, L"hash: 7F3A-1984-CC21-9B04", COL_DIM);
        TextTerm(x + 8, y + 160, L"operator: root@sentinel", COL_DIM);
    } else {
        for (int i = start; i < total; ++i) {
            int ly = y + 42 + row * lineH;
            if (ly + lineH > y + h - 48) break;
            const std::wstring& line = p.lines[(size_t)i];
            COLORREF c = RGB(172, 240, 200);
            if (line.rfind(L"root@", 0) == 0) c = COL_GREEN;
            else if (line.rfind(L"[!]", 0) == 0 || line.rfind(L"[!!]", 0) == 0) c = COL_RED;
            else if (line.rfind(L"[+]", 0) == 0) c = COL_CYAN;
            else if (line.rfind(L"[done]", 0) == 0) c = COL_GREEN;
            else if (line.rfind(L"q --", 0) == 0) c = COL_RED;
            TextTerm(x + 8, ly, line.c_str(), c, HasCJKTerm(line.c_str()));
            row++;
        }
    }
    RestoreDC(memDC, -1);

    if (focused) {
        wchar_t inp[120];
        swprintf_s(inp, L"> %ls", bs.input.c_str());
        TextTerm(x + 8, y + h - 38, inp, COL_WHITE);
        TextTerm(x + 8 + (int)bs.input.size() * 7 + 10, y + h - 38,
                 (fmodf((float)sim.time, 1.0f) < 0.5f) ? L"_" : L" ", COL_CYAN);
    } else {
        TextTerm(x + 8, y + h - 38, L"> click to focus // Ctrl+Enter override", RGB(36, 92, 64));
    }

    // ASCII hash progress bar, not a GUI progress widget
    wchar_t bar[64];
    int cells = 20;
    int fill = (int)(clampf(p.progress, 0.0f, 1.0f) * cells);
    for (int i = 0; i < cells; i++) bar[i] = i < fill ? L'#' : L'.';
    bar[cells] = 0;
    wchar_t line[128];
    swprintf_s(line, L"[%ls] %3d%% %ls", bar, (int)(p.progress * 100.0f),
               p.aborted ? L" q -- KILLED" : L" q=abort");
    TextTerm(x + 8, y + h - 20, line, p.aborted ? COL_RED : COL_GREEN);
    DrawPaneDamage(sim, p, x, y, w, h);
}

void Renderer::DrawPaneDamage(Sim& sim, const Popup& p, int x, int y, int w, int h) {
    (void)sim;
    if (p.damage <= 0.12f) return;
    COLORREF c = p.damage > 0.62f ? COL_RED : COL_AMBER;
    Frame(x, y, w, h, c);
    Fill(x, y, w, 18, Blend(c, RGB(20, 0, 0), 0.55f));
    TextTerm(x + w - 126, y + 4, p.damage > 0.62f ? L"!! SIGNAL LOST" : L"!! DEGRADED", c);
    if (p.damage > 0.3f) {
        int n = p.damage > 0.62f ? 3 : 1;
        for (int i = 0; i < n; i++) {
            int yy = y + 52 + (i * 57) % (h - 76);
            Line(x + 6, yy, x + w - 6, yy + 3, Blend(c, COL_RED, 0.55f), 1);
        }
    }
    if (p.damage > 0.55f) {
        Fill(x + 6, y + 42, w - 12, 2, Blend(COL_RED, COL_BLACK, 0.2f));
        Fill(x + 6, y + h - 62, w - 12, 2, Blend(COL_RED, COL_BLACK, 0.2f));
    }
}

void Renderer::DrawFooter(Sim& sim) {
    Fill(0, 696, VW, 24, RGB(2, 9, 7));
    Fill(0, 696, VW, 1, RGB(15, 52, 39));
    TextTerm(12, 701, L"SENTINEL // DEFENSE GRID ACTIVE", COL_DIM);

    std::wstring track = sim.musicTrack;
    if (track.size() > 28) track = track.substr(0, 27) + L"…";
    wchar_t music[128];
    if (sim.musicCount > 0 && !track.empty())
        swprintf_s(music, L"MUSIC %02d/%02d // %ls", sim.musicIndex, sim.musicCount, track.c_str());
    else
        swprintf_s(music, L"MUSIC // NO ACTIVE TRACK");
    TextTerm(330, 701, music, RGB(54, 120, 91));

    Fill(748, 697, 272, 22, RGB(3, 15, 13));
    Line(748, 697, 748, 718, Blend(COL_CYAN, COL_BLACK, 0.45f), 1);
    TextTerm(760, 701, L"[M / F8] NEXT TRACK", COL_CYAN);
    TextTerm(1040, 701, L"[ENTER] OVERRIDE", COL_AMBER);
}

void Renderer::DrawSignature(Sim& sim) {
    (void)sim;
    // Persistent artist mark: a quiet footer credit, not a badge or plate.
    TextUIR(1262, 701, L"// 原罪知鸮", RGB(168, 142, 88), fontCNTerm);
}

void Renderer::DrawMotionOverlay(Sim& sim) {    const bool reduced = sim.cfg.motion == L"reduced";
    const COLORREF accent = sim.CurrentAct().accent;
    // Quantized blends bound the GDI brush/pen caches during long screen-saver runs.
    auto qblend = [](COLORREF a, COLORREF b, float t) {
        t = lroundf(clampf(t, 0.0f, 1.0f) * 12.0f) / 12.0f;
        return Blend(a, b, t);
    };

    if (sim.motion.phase > 0.0f) {
        float p = sim.motion.PhaseProgress();
        float envelope = sinf(3.1415926f * p);
        if (!reduced) {
            int sy = 46 + (int)(easeOutCubic(p) * 638.0f);
            for (int i = 0; i < 5; ++i) {
                float strength = envelope * (1.0f - i * 0.16f);
                Line(18, sy - i * 5, 1262, sy - i * 5,
                     qblend(COL_BLACK, accent, 0.10f + strength * 0.36f), 1);
            }
        }
        wchar_t label[96];
        swprintf_s(label, L"BLACKTRACE // BUS 7F3A // RELOCK %02d", sim.opIndex + 1);
        int lw = 340;
        Fill(640 - lw / 2, 47, lw, 19, qblend(COL_BLACK, RGB(3, 15, 12), envelope));
        Line(640 - lw / 2, 47, 640 - lw / 2 + (int)(lw * p), 47, accent, 1);
        TextC(640, 50, label, qblend(COL_DIM, COL_WHITE, envelope * 0.65f), 0);
        for (int i = 0; i < 24; ++i) {
            COLORREF segment = i <= (int)(p * 23.0f) ? accent : RGB(10, 35, 25);
            Fill(640 - 120 + i * 10, 68, 7, 1, segment);
        }
    }

    if (sim.motion.theme > 0.0f && !reduced) {
        float p = easeOutCubic(sim.motion.ThemeProgress());
        int sx = (int)(p * VW);
        Line(sx, 44, sx, 696, qblend(COL_BLACK, COL_CYAN, 0.48f), 1);
        Line(sx - 9, 44, sx - 9, 696, qblend(COL_BLACK, COL_CYAN, 0.16f), 1);
    }

    if (sim.motion.pulse > 0.01f) {
        int half = (int)(VW * 0.5f * sim.motion.pulse);
        Line(640 - half, 694, 640 + half, 694, qblend(COL_BLACK, accent, sim.motion.pulse), 1);
    }
}

void Renderer::DrawSituationStrip(Sim& sim, int x, int y, int w, int lines) {
    if (lines <= 0) return;
    int h = lines * 15 + 6;
    Fill(x, y, w, h, RGB(1, 6, 4));
    Frame(x, y, w, h, RGB(16, 54, 34));
    for (int i = 0; i < lines && i < 5; i++) {
        const wchar_t* s = sim.summary[i].c_str();
        TextTerm(x + 6, y + 4 + i * 15, s, i == 0 ? COL_AMBER : COL_TEXT, HasCJKTerm(s));
    }
}

void Renderer::DrawTop(Sim& sim) {
    Fill(20, 16, 1240, 36, COL_PANEL);
    Frame(20, 16, 1240, 36, COL_BORDER);
    Text(30, 24, L"SENTINEL DEFENSE GRID // v2.3.1", COL_GREEN, 4);

    int th = sim.ThreatLevel();
    COLORREF tc = ThreatColor(th);
    bool blink = th >= 2 && fmodf((float)sim.time, 0.8f) < 0.4f;
    COLORREF badge = blink ? Blend(tc, COL_BLACK, 0.55f) : tc;
    Fill(408, 22, 170, 24, badge);
    Frame(408, 22, 170, 24, Blend(badge, COL_WHITE, 0.2f));
    wchar_t tb[40];
    swprintf_s(tb, L"THREAT: %ls", ThreatName(th));
    TextC(493, 26, tb, blink ? COL_WHITE : COL_BLACK, 1);

    Text(610, 27, L"ACTIVE COUNTERMEASURES // CORE ISOLATED", COL_DIM, 0);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t clk[64];
    swprintf_s(clk, L"%04d-%02d-%02d  %02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    TextR(1248, 26, clk, COL_DIM, 1);

    int avg = (int)lroundf(sim.AvgIntegrity());
    COLORREF ac = avg < 35 ? COL_RED : (avg < 60 ? COL_AMBER : COL_GREEN);
    Bar(20, 56, 1240, 3, avg / 100.0f, ac);
}

void Renderer::DrawLeft(Sim& sim) {
    Panel(20, 64, 252, 556, L"DEFENSE LAYERS // 06", COL_GREEN);
    const int x = 20;
    const int base = 64 + 40;
    const int rowH = 68;

    for (int i = 0; i < 6; i++) {
        const Firewall& fw = sim.firewalls[i];
        int y = base + i * rowH;
        COLORREF sc = StateColor(fw.state);
        wchar_t idx[8];
        swprintf_s(idx, L"L%02d", i + 1);
        Text(x + 10, y + 2, idx, COL_DIM, 0);
        Text(x + 44, y, fw.name.c_str(), fw.state == FWState::Offline ? COL_DIM : COL_TEXT, 1);
        wchar_t pct[8];
        swprintf_s(pct, L"%d%%", (int)lroundf(fw.display));
        TextR(x + 242, y, pct, sc, 1);
        Text(x + 10, y + 20, StateName(fw.state), sc, 0);
        Bar(x + 44, y + 40, 198, 6, fw.display / 100.0f, sc);
        if (fw.state == FWState::Rebuilding) {
            Bar(x + 44, y + 50, 198, 2, clampf(fw.integrity / 100.0f, 0.0f, 1.0f), COL_CYAN);
        } else if (fw.state == FWState::Offline) {
            Bar(x + 44, y + 50, 198, 2, 0.0f, COL_DARKRED);
        }
        if (fw.flash > 0.01f) {
            Fill(x + 1, y + 2, 2, rowH - 8, Blend(COL_RED, COL_BLACK, 1.0f - fw.flash));
        }
        Line(x + 8, y + rowH - 6, x + 244, y + rowH - 6, COL_BORDER, 1);
    }

    int sy = 64 + 556 - 78;
    wchar_t buf[32];
    Text(x + 10, sy, L"PACKETS", COL_DIM, 0);
    FormatNum(sim.packets, buf, 32);
    TextR(x + 242, sy, buf, COL_TEXT, 0);
    Text(x + 10, sy + 18, L"BLOCKED", COL_DIM, 0);
    FormatNum(sim.blocked, buf, 32);
    TextR(x + 242, sy + 18, buf, COL_TEXT, 0);
    Text(x + 10, sy + 36, L"ENCRYPT ATT", COL_DIM, 0);
    FormatNum(sim.encrypted, buf, 32);
    TextR(x + 242, sy + 36, buf, sim.encrypted > 0 ? COL_RED : COL_DIM, 0);
    Text(x + 10, sy + 54, L"LAYERS ONLINE", COL_DIM, 0);
    swprintf_s(buf, L"%d / 6", 6 - sim.CountState(FWState::Offline));
    TextR(x + 242, sy + 54, buf, COL_TEXT, 0);
}

void Renderer::DrawCore(Sim& sim) {
    Panel(284, 64, 628, 320, L"SENTINEL CORE // INTEGRITY", COL_GREEN);
    const int cx = 474;
    const int cy = 244;
    float scan = fmodf((float)sim.time * 0.7f, 6.2831853f);
    Line(cx, cy, cx + (int)lroundf(cosf(scan) * 122.0f), cy + (int)lroundf(sinf(scan) * 122.0f),
         Blend(COL_CYAN, COL_BLACK, 0.35f), 1);

    for (int i = 0; i < 6; i++) {
        const Firewall& fw = sim.firewalls[i];
        float r = 40.0f + i * 16.0f;
        COLORREF base = StateColor(fw.state);
        int pts = 0;
        for (float a = 0.0f; a < 6.2831853f; a += 0.04f) {
            float da = a - fw.breachStart;
            while (da > 3.14159265f) da -= 6.2831853f;
            while (da < -3.14159265f) da += 6.2831853f;
            if (fw.breachWidth > 0.01f && fabsf(da) < fw.breachWidth * 0.5f) continue;
            float px = cx + cosf(a) * r;
            float py = cy + sinf(a) * r;
            float sd = a - scan;
            while (sd > 3.14159265f) sd -= 6.2831853f;
            while (sd < -3.14159265f) sd += 6.2831853f;
            COLORREF c = base;
            if (fabsf(sd) < 0.18f) c = COL_WHITE;
            else if (fabsf(sd) < 0.50f) c = Blend(base, COL_CYAN, 0.5f);
            int ix = (int)lroundf(px);
            int iy = (int)lroundf(py);
            Fill(ix, iy, 1, 1, c);
            if ((pts % 12) == 0) Text(ix - 3, iy - 7, (i % 2) ? L"#" : L"=", c, 0);
            pts++;
        }
    }

    int avg = (int)lroundf(sim.AvgIntegrity());
    COLORREF ac = avg < 35 ? COL_RED : (avg < 60 ? COL_AMBER : COL_GREEN);
    wchar_t big[8];
    swprintf_s(big, L"%d%%", avg);
    TextC(cx, cy - 24, big, ac, 5);
    TextC(cx, cy + 14, L"CORE INTEGRITY", COL_DIM, 1);
    Bar(cx - 70, cy + 36, 140, 4, avg / 100.0f, ac);

    Line(284 + 350, 64 + 42, 284 + 350, 64 + 278, COL_BORDER, 1);

    const wchar_t* labels[6] = {
        L"THREAT", L"ONLINE", L"REBUILDING", L"OFFLINE", L"PACKETS DROP", L"ENCRYPT ATT"
    };
    wchar_t vals[6][32];
    swprintf_s(vals[0], L"%ls", ThreatName(sim.ThreatLevel()));
    swprintf_s(vals[1], L"%d / 6", 6 - sim.CountState(FWState::Offline));
    swprintf_s(vals[2], L"%d", sim.CountState(FWState::Rebuilding));
    swprintf_s(vals[3], L"%d", sim.CountState(FWState::Offline));
    FormatNum(sim.blocked, vals[4], 32);
    FormatNum(sim.encrypted, vals[5], 32);
    int rx = 284 + 365;
    int ry = 64 + 54;
    for (int i = 0; i < 6; i++) {
        Text(rx, ry + i * 30, labels[i], COL_DIM, 1);
        COLORREF vc = COL_TEXT;
        if (i == 0) vc = ThreatColor(sim.ThreatLevel());
        else if (i == 3 && sim.CountState(FWState::Offline) > 0) vc = COL_RED;
        else if (i == 2 && sim.CountState(FWState::Rebuilding) > 0) vc = COL_CYAN;
        else if (i == 5 && sim.encrypted > 0) vc = COL_RED;
        TextR(284 + 608, ry + i * 30, vals[i], vc, 3);
    }
}

void Renderer::DrawInjection(Sim& sim) {
    Panel(284, 396, 628, 224, L"PAYLOAD INJECTION // KERNEL MEMORY", COL_GREEN);
    const int x = 284;
    const int y = 396;
    const int baseY = y + 40;
    const int lh = 15;

    static const wchar_t* kCode[10] = {
        L"NTSTATUS KernelProbe(ULONG64 addr) {",
        L"  PEPROCESS proc = PsGetCurrentProcess();",
        L"  if (!MmIsAddressValid((PVOID)addr)) return 0xC0000005;",
        L"  ULONG64 ctx = *(ULONG64*)addr;",
        L"  for (int i = 0; i < ctx->len; i++) {",
        L"    payload[i] ^= key[i % 16];",
        L"  }",
        L"  return STATUS_SUCCESS;",
        L"}",
        L"// PATCH_LEVEL IRQL REACHED"
    };

    int cw = TW(L"0000000000", 0) / 10;
    if (cw < 1) cw = 1;

    auto drawTok = [&](int px, int py, int pos, const wchar_t* s, int len, COLORREF c) {
        HFONT old = (HFONT)SelectObject(memDC, fonts[0]);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, c);
        TextOutW(memDC, PX((float)px) + pos * cw, PY((float)py), s, len);
        SelectObject(memDC, old);
    };

    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)x), PY((float)(y + 30)), PX((float)(x + 628)), PY((float)(y + 224)));
    for (int i = 0; i < 10; i++) {
        wchar_t num[8];
        swprintf_s(num, L"%02d", i + 1);
        Text(x + 10, baseY + i * lh, num, COL_DIM, 0);
        bool corrupt = (sim.injection.phase == 2) && (i % 3 == 0);
        Text(x + 34, baseY + i * lh, kCode[i], COL_TEXT, 0);
        // Syntax overlay; the base line is already drawn in the normal text color.
        int len = (int)wcslen(kCode[i]);
        int pos = 0;
        const wchar_t* p = kCode[i];
        while (*p) {
            if (iswalpha(*p) || *p == L'_') {
                const wchar_t* start = p;
                while (*p && (iswalnum(*p) || *p == L'_')) p++;
                int n = (int)(p - start);
                bool kw = (n == 2 && wcsncmp(start, L"if", 2) == 0) ||
                          (n == 3 && wcsncmp(start, L"for", 3) == 0) ||
                          (n == 6 && wcsncmp(start, L"return", 6) == 0) ||
                          (n == 7 && wcsncmp(start, L"ULONG64", 7) == 0) ||
                          (n == 8 && wcsncmp(start, L"NTSTATUS", 8) == 0) ||
                          (n == 9 && wcsncmp(start, L"PEPROCESS", 9) == 0) ||
                          (n == 14 && wcsncmp(start, L"STATUS_SUCCESS", 14) == 0);
                if (kw) drawTok(x + 34, baseY + i * lh, pos, start, n, COL_CYAN);
                pos += n;
            } else if (iswdigit(*p)) {
                const wchar_t* start = p;
                while (*p && (iswalnum(*p) || *p == L'x' || *p == L'X')) p++;
                int n = (int)(p - start);
                drawTok(x + 34, baseY + i * lh, pos, start, n, COL_AMBER);
                pos += n;
            } else {
                p++;
                pos++;
            }
        }
        if (corrupt) {
            for (int k = 0; k < 3; k++) {
                int at = (int)((i * 37 + k * 53 + (int)(sim.time * 12.0)) % std::max(1, len));
                drawTok(x + 34, baseY + i * lh, at, L"?", 1, COL_RED);
            }
        }
    }

    int lastY = baseY + 10 * lh;
    if (sim.injection.phase == 1) {
        TextTyped(x + 34, lastY, L"> 0x90 0x90 0xCC 0x7F3A 0xDEAD 0xBEEF 0x9C41 0xFFFF",
                  COL_RED, 0, sim.injection.payloadVisible);
    } else if (sim.injection.phase == 2) {
        if (fmodf((float)sim.time, 0.4f) < 0.25f)
            Text(x + 34, lastY, L"!! CODE INTEGRITY FAILURE !!", COL_RED, 1);
    } else if (sim.injection.phase == 3) {
        Text(x + 34, lastY, L"PATCHING KERNEL PAGES ...", COL_GREEN, 0);
        Bar(x + 34, lastY + 16, 300, 4, clampf(sim.injection.repairVisible / 60.0f, 0.0f, 1.0f), COL_GREEN);
        if (sim.injection.repairVisible >= 60.0f)
            TextR(x + 618, lastY, L"QUARANTINED", COL_GREEN, 0);
    }

    const wchar_t* st = L"IDLE";
    COLORREF sc = COL_DIM;
    if (sim.injection.phase == 1) { st = L"INJECT"; sc = COL_RED; }
    else if (sim.injection.phase == 2) { st = L"CORRUPT"; sc = COL_RED; }
    else if (sim.injection.phase == 3) { st = L"REPAIR"; sc = COL_GREEN; }
    TextR(x + 618, y + 6, st, sc, 0);
    RestoreDC(memDC, -1);
}

void Renderer::DrawLog(Sim& sim) {
    Panel(924, 64, 336, 360, L"EVENT STREAM // LIVE", COL_GREEN);
    const int x = 924;
    const int y = 64;
    SaveDC(memDC);
    IntersectClipRect(memDC, PX((float)x), PY((float)(y + 32)), PX((float)(x + 336)), PY((float)(y + 360)));

    int n = (int)sim.log.entries.size();
    int start = std::max(0, n - 18);
    int lineH = 16;
    int baseY = y + 40;
    int textX = x + 136;
    int charW = std::max(1, TW(L"0", 0));
    int maxChars = (x + 328 - textX) / charW;
    if (maxChars < 4) maxChars = 4;
    if (maxChars > 126) maxChars = 126;

    for (int i = start; i < n; i++) {
        const LogEntry& e = sim.log.entries[i];
        int ly = baseY + (i - start) * lineH;
        COLORREF c = e.sev == Severity::Crit ? COL_RED : (e.sev == Severity::Warn ? COL_AMBER : COL_TEXT);
        Text(x + 8, ly, e.time.c_str(), COL_DIM, 0);
        const wchar_t* sev = e.sev == Severity::Crit ? L"CRT" : (e.sev == Severity::Warn ? L"WRN" : L"INF");
        Text(x + 56, ly, sev, c, 0);
        Text(x + 84, ly, e.module.c_str(), COL_DIM, 0);

        int len = (int)e.text.size();
        wchar_t tbuf[128];
        const wchar_t* draw = e.text.c_str();
        bool truncated = len > maxChars;
        if (truncated) {
            wcsncpy_s(tbuf, 128, draw, (size_t)(maxChars - 1));
            tbuf[maxChars - 1] = L'\u2026';
            tbuf[maxChars] = 0;
            draw = tbuf;
        }

        if (i == n - 1 && !e.corrupt) {
            float vis = clampf(e.age * 55.0f, 0.0f, (float)len);
            if (truncated && vis >= (float)maxChars) {
                Text(textX, ly, draw, c, 0);
            } else {
                TextTyped(textX, ly, e.text.c_str(), c, 0, vis);
            }
        } else {
            Text(textX, ly, draw, e.corrupt ? COL_RED : c, 0);
        }
        if (e.corrupt && len < 120) {
            wchar_t buf[128];
            wcsncpy_s(buf, 128, draw, 127);
            int blen = (int)wcslen(buf);
            for (int k = 0; k < 3; k++) {
                int at = (int)((i * 17 + k * 31) % std::max(1, blen));
                buf[at] = L'?';
            }
            Text(textX, ly, buf, COL_RED, 0);
        }
    }
    RestoreDC(memDC, -1);
    bool on = fmodf((float)sim.time, 1.2f) < 0.85f;
    Text(x + 10, y + 342, L"AWAITING OPERATOR INPUT", on ? COL_DIM : Blend(COL_DIM, COL_BLACK, 0.6f), 0);
}

void Renderer::DrawFiles(Sim& sim) {
    Panel(924, 436, 336, 184, L"KEY FILES // INTEGRITY", COL_GREEN);
    const int x = 924;
    const int baseY = 436 + 38;
    const int rowH = 23;
    for (int i = 0; i < 6; i++) {
        const MonitoredFile& f = sim.files.files[i];
        int y = baseY + i * rowH;
        COLORREF c = f.status == FileStatus::Deleted ? COL_RED :
                     (f.status == FileStatus::Modified ? COL_AMBER :
                      (f.status == FileStatus::Restored ? COL_CYAN : COL_TEXT));
        wchar_t path[64];
        wcsncpy_s(path, f.path.c_str(), 56);
        if (f.path.size() > 56) { path[53] = L'.'; path[54] = L'.'; path[55] = L'.'; path[56] = 0; }
        Text(x + 8, y + 1, path, f.status == FileStatus::Deleted ? COL_DIM : c, 0);
        const wchar_t* st = L"OK";
        if (f.status == FileStatus::Modified) st = L"MODIFIED";
        else if (f.status == FileStatus::Deleted) st = L"DELETED";
        else if (f.status == FileStatus::Restored) st = L"RESTORED";
        TextR(x + 328, y + 1, st, c, 0);
        Text(x + 8, y + 11, f.hash.c_str(), COL_DIM, 0);
        if (f.status == FileStatus::Deleted || f.status == FileStatus::Restored || f.status == FileStatus::Modified) {
            Bar(x + 8, y + 19, 320, 2, f.progress / 100.0f, c);
        }
        Line(x + 8, y + rowH - 2, x + 328, y + rowH - 2, COL_BORDER, 1);
    }
}

void Renderer::DrawBottom(Sim& sim) {
    Fill(20, 632, 1240, 72, COL_PANEL);
    Frame(20, 632, 1240, 72, COL_BORDER);

    wchar_t n1[32], n2[32], n3[32];
    FormatNum(sim.packets, n1, 32);
    FormatNum(sim.blocked, n2, 32);
    FormatNum(sim.encrypted, n3, 32);
    wchar_t tick[512];
    swprintf_s(tick,
               L"PACKETS %ls   BLOCKED %ls   NODES %d/14   ENCRYPT ATTEMPTS %ls   THREAT %ls   CYCLE %02d   //  SENTINEL DEFENSE GRID ACTIVE  //  ",
               n1, n2, 14, n3, ThreatName(sim.ThreatLevel()), sim.cycle);

    SaveDC(memDC);
    IntersectClipRect(memDC, PX(20.0f), PY(636.0f), PX(900.0f), PY(700.0f));
    float off = fmodf((float)sim.time * 45.0f, 1400.0f);
    Text((int)(20.0f - off), 640, tick, COL_DIM, 1);
    Text((int)(20.0f - off + 1400.0f), 640, tick, COL_DIM, 1);
    RestoreDC(memDC, -1);

    wchar_t wave[41];
    static const wchar_t* ramp = L" .:-=+*#%@";
    for (int i = 0; i < 40; i++) {
        float v = sinf((float)i * 0.6f + (float)sim.time * 6.0f);
        int idx = (int)((v + 1.0f) * 0.5f * 9.0f);
        if (idx < 0) idx = 0;
        if (idx > 9) idx = 9;
        wave[i] = ramp[idx];
    }
    wave[40] = 0;
    Text(910, 640, wave, sim.ThreatLevel() >= 2 ? COL_RED : COL_GREEN, 1);

    Text(24, 666, L"COUNTERMEASURES ACTIVE // TRACE CHANNEL SECURE", COL_DIM, 0);
    if (sim.sysCpuPct > 0.0f) {
        wchar_t host[160];
        swprintf_s(host,
                   L"HOST CPU %.0f%%  RAM %.0f%%   SENTINEL CPU %.1f%%  MEM %.0f MB",
                   sim.sysCpuPct, sim.memPct, sim.procCpuPct, sim.procMemMB);
        Text(24, 684, host, RGB(38, 92, 66), 0);
    }
    bool on = fmodf((float)sim.time, 1.2f) < 0.8f;
    COLORREF hc = on ? COL_AMBER : Blend(COL_AMBER, COL_BLACK, 0.6f);
    TextR(1250, 666, L"[ENTER] EMERGENCY OVERRIDE", hc, 0);
}

void Renderer::DrawRain(Sim& sim) {
    if (!rainAtlasReady || rainCellW <= 0 || rainCellH <= 0) return;
    const float cellW = (float)rainCellW / scale;
    const float cellH = (float)rainCellH / scale;
    auto covered = [](float x, float y, float w, float h) {
        return (x >= 18.0f && y >= 20.0f && x + w <= 808.0f && y + h <= 656.0f) ||
               (x >= 824.0f && y >= 20.0f && x + w <= 1262.0f && y + h <= 320.0f) ||
               (x >= 824.0f && y >= 336.0f && x + w <= 1262.0f && y + h <= 656.0f) ||
               (y >= 672.0f && y + h <= 720.0f);
    };
    for (const RainColumn& c : sim.rain.cols) {
        int x = PX(c.x);
        int yBase = PY(c.y);
        int n = (int)clampf(c.length, 1.0f, 32.0f);
        for (int k = 0; k < n; k++) {
            int y = yBase - k * rainCellH;
            if (y + rainCellH < 0 || y > H) continue;
            if (covered(c.x, c.y - k * cellH, cellW, cellH)) continue;
            int g = c.glyph[k];
            if (g < 0 || g >= RAIN_GLYPH_COUNT) g = 0;
            int sx = (g % 16) * rainCellW;
            int sy = (g / 16) * rainCellH;
            int atlas = (k == 0) ? 2 : (c.red ? 3 : (k < 4 ? 1 : 0));
            BitBlt(memDC, x, y, rainCellW, rainCellH, rainMaskDC, sx, sy, SRCAND);
            BitBlt(memDC, x, y, rainCellW, rainCellH, rainColorDC[atlas], sx, sy, SRCPAINT);
        }
    }
}

void Renderer::DrawAmbient(Sim& sim) {
    int count = 64;
    if (sim.cfg.dust == L"off") count = 0;
    else if (sim.cfg.dust == L"low") count = 32;
    else if (sim.cfg.dust == L"high") count = 96;
    COLORREF c = RGB(18, 58, 40);
    if (sim.theme == Theme::SplitOps) c = RGB(20, 62, 52);
    else if (sim.theme == Theme::WarRoom) c = RGB(28, 54, 58);
    else if (sim.theme == Theme::Forensic) c = RGB(44, 48, 34);
    else if (sim.theme == Theme::Containment) c = RGB(18, 66, 34);
    else if (sim.theme == Theme::BlackBox) c = RGB(18, 20, 18);
    float speed = 10.0f + (float)sim.ThreatLevel() * 4.0f;
    for (int i = 0; i < count; i++) {
        float x = std::fmod((float)(i * 73) + (float)sim.time * speed, 1280.0f);
        float y = std::fmod((float)(i * 151) + (float)sim.time * (speed * 0.37f), 720.0f);
        if (sim.theme == Theme::WarRoom && (i % 7) == 0) {
            int cx = PX(x), cy = PY(y);
            int r = PS(9.0f + (float)(i % 5) * 4.0f);
            Arc(memDC, cx - r, cy - r, cx + r, cy + r, cx - r, cy, cx + r, cy);
        } else {
            SetPixelV(memDC, PX(x), PY(y), c);
            if ((i % 9) == 0) SetPixelV(memDC, PX(x) + 1, PY(y), c);
        }
    }
}

void Renderer::DrawGlitchText(int x, int y, const wchar_t* s, int maxChars, int fid, COLORREF c, float intensity, uint32_t seed) {
    if (!s || maxChars <= 0) return;
    wchar_t buf[96];
    int n = 0;
    for (; s[n] && n < maxChars && n < 95; n++) {
        wchar_t ch = s[n];
        uint32_t h = seed + (uint32_t)n * 2654435761u;
        if (intensity > 0.55f && (h % 100) < 8) ch = L'#';
        else if (intensity > 0.35f && (h % 100) < 12) ch = L'?';
        else if (intensity > 0.7f && (h % 100) < 8) ch = 0x2588;
        buf[n] = ch;
    }
    buf[n] = 0;
    int jitter = (int)(intensity * 3.0f);
    int jx = jitter ? (int)(seed % (uint32_t)(jitter * 2 + 1)) - jitter : 0;
    int jy = jitter ? (int)((seed >> 8) % (uint32_t)(jitter * 2 + 1)) - jitter : 0;
    Text(x + jx - 1, y + jy, buf, Blend(c, COL_RED, 0.55f), fid);
    Text(x + jx + 1, y + jy, buf, Blend(c, COL_CYAN, 0.45f), fid);
    Text(x + jx, y + jy, buf, c, fid);
}

void Renderer::DrawTakeover(Sim& sim) {
    if (sim.fx.takeover == Takeover::None) return;
    float a = clampf(sim.fx.takeoverDuration > 0.0f ? sim.fx.takeoverTimer / sim.fx.takeoverDuration : 1.0f, 0.0f, 1.0f);
    int alpha = (int)(210.0f * a);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)alpha, 0};
    AlphaBlend(memDC, 0, 0, W, H, blackDC, 0, 0, 1, 1, bf);
    const wchar_t* msg = L"WARNING";
    COLORREF col = COL_RED;
    if (sim.fx.takeover == Takeover::Failure) msg = L"SYSTEM FAILURE";
    else if (sim.fx.takeover == Takeover::Disconnected) { msg = L"DISCONNECTED"; col = COL_AMBER; }
    DrawGlitchText(640 - TW(msg, 7) / 2, 330, msg, 64, 7, col, 0.8f, sim.fx.glitchSeed);
    TextC(640, 392, L"CONTAINMENT CONTROL // MANUAL OVERRIDE REQUIRED", COL_DIM, 1);
}

void Renderer::DrawBanner(Sim& sim) {
    if (sim.fx.banner.empty() || sim.fx.bannerT <= 0.0f) return;
    float t = sim.fx.bannerDur - sim.fx.bannerT;
    float a = 1.0f;
    if (t < 0.25f) a = t / 0.25f;
    if (sim.fx.bannerT < 0.25f) a = sim.fx.bannerT / 0.25f;
    a = clampf(a, 0.0f, 1.0f);
    Fill(0, 250, 1280, 64, Blend(COL_DARKRED, COL_BLACK, 0.25f));
    Line(0, 250, 1280, 250, COL_RED, 1);
    Line(0, 313, 1280, 313, COL_RED, 1);
    COLORREF wht = Blend(COL_BLACK, COL_WHITE, a);
    COLORREF red = Blend(COL_BLACK, COL_RED, a);
    COLORREF cy = Blend(COL_BLACK, COL_CYAN, a);
    TextC(638, 262, sim.fx.banner.c_str(), red, 6);
    TextC(642, 262, sim.fx.banner.c_str(), cy, 6);
    TextC(640, 262, sim.fx.banner.c_str(), wht, 6);
}

void Renderer::DrawBoot(Sim& sim) {
    float t = (float)sim.time;
    if (t >= 1.0f) return;
    int alpha = 255;
    if (t > 0.55f) alpha = (int)(255.0f * (1.0f - smoothstepf(0.55f, 1.0f, t)));
    if (alpha > 0) {
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)alpha, 0};
        AlphaBlend(memDC, 0, 0, W, H, blackDC, 0, 0, 1, 1, bf);
    }
    if (t < 0.12f) {
        int fa = (int)(255.0f * (1.0f - t / 0.12f));
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)fa, 0};
        AlphaBlend(memDC, 0, 0, W, H, whiteDC, 0, 0, 1, 1, bf);
    }
    if (t < 0.10f) {
        int lw = (int)(easeOutCubic(t / 0.10f) * 640.0f);
        Fill(640 - lw / 2, 360, lw, 2, COL_WHITE);
    }
    if (t > 0.08f) {
        static const wchar_t* lines[5] = {
            L"SENTINEL BIOS v4.9.0 // BLACKTRACE BOOT CHAIN",
            L"MEMORY CHECK .......... 32768 MB OK",
            L"DEFENSE LAYERS ........ 2/6 -- BREACH",
            L"ACTIVE VECTOR: KERNEL / EGRESS / DATA VAULT",
            L"TRACE DIRECTIVE: ISOLATE / PRESERVE / RECOVER"
        };
        int n = std::min(5, 1 + (int)((t - 0.08f) / 0.10f));
        if (alpha > 70) {
            for (int i = 0; i < n; i++) {
                COLORREF c = (i == 4) ? COL_RED : (i == 2 ? COL_AMBER : COL_GREEN);
                Text(80, 110 + i * 24, lines[i], c, 1);
            }
        }
    }
}

void Renderer::DrawModal(Sim& sim) {
    if (!sim.modal.open) return;
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 170, 0};
    AlphaBlend(memDC, 0, 0, W, H, blackDC, 0, 0, 1, 1, bf);

    Fill(320, 165, 640, 390, RGB(8, 2, 3));
    Frame(320, 165, 640, 390, COL_RED);
    Line(320, 165, 380, 165, COL_RED, 2);
    Line(320, 165, 320, 205, COL_RED, 2);
    Line(960, 165, 900, 165, COL_RED, 2);
    Line(960, 165, 960, 205, COL_RED, 2);
    Line(320, 555, 380, 555, COL_RED, 2);
    Line(320, 555, 320, 515, COL_RED, 2);
    Line(960, 555, 900, 555, COL_RED, 2);
    Line(960, 555, 960, 515, COL_RED, 2);

    Text(340, 176, L"SENTINEL // CONTAINMENT CONTROL", COL_RED, 4);
    Text(340, 198, L"INCIDENT 0x7F3A // CLASS: CRITICAL", COL_DIM, 0);
    Line(340, 218, 940, 218, Blend(COL_RED, COL_BLACK, 0.4f), 1);

    int mm = (int)sim.modal.countdown / 60;
    int ss = (int)sim.modal.countdown % 60;
    wchar_t cd[16];
    swprintf_s(cd, L"%02d:%02d", mm, ss);
    TextR(940, 174, cd, COL_RED, 5);
    TextR(940, 202, L"TIME TO LOCKDOWN", COL_DIM, 0);

    struct MLine { const wchar_t* s; bool cn; bool bold; float delay; };
    static const MLine lines[7] = {
        {L"KERNEL INTEGRITY FAILURE.", false, true, 0.00f},
        {L"PRIVILEGE ESCALATION CONFIRMED.", false, true, 0.12f},
        {L"Kernel integrity verification failed; unauthorized privilege escalation detected.", true, false, 0.24f},
        {L"2 of 6 defense layers offline; 3 rebuilding.", true, false, 0.36f},
        {L"Volume encryption has started on 3 protected volumes.", true, false, 0.48f},
        {L"The system will enter an unrecoverable lockdown state when the countdown ends.", true, false, 0.60f},
        {L"Enter the 4–32 character emergency override code to abort the lockdown sequence.", true, false, 0.72f}
    };
    for (int i = 0; i < 7; i++) {
        if (sim.modal.bodyReveal <= lines[i].delay) continue;
        int ly = 232 + i * 22;
        float chars = (sim.modal.bodyReveal - lines[i].delay) * 55.0f;
        if (lines[i].cn) {
            TextTypedFont(340, ly, lines[i].s, COL_TEXT, fontCN, chars);
        } else {
            TextTyped(340, ly, lines[i].s, COL_WHITE, lines[i].bold ? 3 : 1, chars);
        }
    }

    const int ix = 340, iy = 386, iw = 400, chh = 46;
    int inLen = (int)sim.modal.input.size();
    bool active = sim.modal.lockout <= 0.0f && sim.modal.phase == 0;
    Fill(ix, iy, iw, chh, RGB(4, 8, 6));
    Frame(ix, iy, iw, chh, active ? COL_CYAN : COL_BORDER);
    if (inLen > 0) {
        wchar_t mask[40];
        int n = inLen > 32 ? 32 : inLen;
        for (int j = 0; j < n; j++) mask[j] = L'*';
        mask[n] = 0;
        Text(ix + 12, iy + 12, mask, COL_WHITE, 3);
    } else if (active && fmodf((float)sim.time, 1.0f) < 0.5f) {
        Text(ix + 12, iy + 12, L"_", COL_CYAN, 3);
    }
    wchar_t lenbuf[24];
    swprintf_s(lenbuf, L"%d/32", inLen);
    TextR(ix + iw - 12, iy + 16, lenbuf, COL_DIM, 1);

    wchar_t att[32];
    swprintf_s(att, L"ATTEMPTS %02d/03", sim.modal.attempts);
    Text(770, 392, att, COL_DIM, 0);
    Text(770, 412, L"[ENTER] SUBMIT", COL_GREEN, 3);
    Text(770, 434, L"[ESC] RETURN", COL_DIM, 3);
    if (sim.modal.lockout > 0.0f) {
        Text(770, 456, L"INPUT LOCKED", COL_RED, 3);
        Bar(770, 476, 160, 4, sim.modal.lockout / 5.0f, COL_RED);
    }
    TextC(640, 500, L"CTRL+SHIFT+Q // EMERGENCY EXIT", COL_AMBER, 0);
    TextC(640, 520, L"SENTINEL CONTAINMENT PROTOCOL // 0x7F3A", COL_DIM, 0);
    if (sim.modal.countdown <= 0.0f)
        TextC(640, 536, L"LOCKDOWN COMPLETE // OVERRIDE STILL AVAILABLE", COL_RED, 0);

    if (sim.modal.phase == 2) {
        Fill(340, 290, 600, 64, RGB(20, 3, 5));
        Frame(340, 290, 600, 64, COL_RED);
        TextC(640, 300, L"OVERRIDE REJECTED", COL_RED, 5);
        TextC(640, 332, L"ATTEMPT LOGGED // COUNTDOWN REDUCED", COL_AMBER, 1);
    } else if (sim.modal.phase == 1) {
        Fill(320, 165, 640, 390, RGB(2, 14, 8));
        Frame(320, 165, 640, 390, COL_GREEN);
        TextC(640, 250, L"OVERRIDE ACCEPTED", COL_GREEN, 6);
        TextC(640, 306, L"SIGNATURE VALID // OVERRIDE ACCEPTED", COL_TEXT, 3);
        TextC(640, 336, L"TERMINATING CONTAINMENT SEQUENCE...", COL_DIM, 1);
        Bar(440, 380, 400, 6, 1.0f - sim.modal.phaseTimer / 1.2f, COL_GREEN);
    }
}

void Renderer::DrawCorruption(Sim& sim) {
    const bool reduced = sim.cfg.motion == L"reduced";
    const float tear = sim.fx.tear * (reduced ? 0.18f : 1.0f);
    const float noise = sim.fx.staticNoise * (reduced ? 0.22f : 1.0f);

    // Red damage tint over the whole frame (capped low, never strobe).
    if (sim.fx.screenDamage > 0.02f) {
        int alpha = (int)clampf(sim.fx.screenDamage * (reduced ? 22.0f : 45.0f), 0.0f, 45.0f);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)alpha, 0};
        AlphaBlend(memDC, 0, 0, W, H, redDC, 0, 0, 1, 1, bf);
        COLORREF rc = Blend(COL_RED, COL_BLACK, 0.45f);
        Fill(0, 0, VW, 2, rc);
        Fill(0, VH - 2, VW, 2, rc);
        Fill(0, 0, 2, VH, rc);
        Fill(VW - 2, 0, 2, VH, rc);
    }

    if (tear > 0.02f && scratchDC) {
        BitBlt(scratchDC, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        int bands = std::min(reduced ? 2 : 6, 1 + (int)(tear * 6.0f));
        for (int i = 0; i < bands; i++) {
            int y = rand() % std::max(1, H);
            int h = PS(3.0f + (float)(rand() % (reduced ? 5 : 12)));
            int dx = (rand() % 2 ? 1 : -1) * PS(2.0f + (float)(rand() % (reduced ? 3 : 7)));
            if (y + h > H) h = H - y;
            if (h <= 0) continue;
            BitBlt(memDC, 0, y, W, h, scratchDC, dx, y, SRCCOPY);
            if (dx > 0) PatBlt(memDC, 0, y, dx, h, BLACKNESS);
            else if (dx < 0) PatBlt(memDC, W + dx, y, -dx, h, BLACKNESS);
        }
    }

    if (noise > 0.02f) {
        int count = std::min(reduced ? 110 : 500, (int)(noise * 500.0f));
        for (int i = 0; i < count; i++) {
            int x = rand() % std::max(1, W);
            int y = rand() % std::max(1, H);
            COLORREF c = (i % 7 == 0) ? COL_RED : (i % 3 ? RGB(36, 86, 64) : RGB(12, 28, 20));
            SetPixelV(memDC, x, y, c);
        }
    }
}

void Renderer::DrawActCurtain(Sim& sim) {
    if (sim.fx.actCurtainTimer <= 0.0f) return;
    float p = sim.fx.actCurtainDuration > 0.0f ? (sim.fx.actCurtainTimer / sim.fx.actCurtainDuration) : 0.0f;
    int alpha = (int)(185.0f * clampf(p, 0.0f, 1.0f));
    BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)alpha, 0};
    AlphaBlend(memDC, 0, 0, W, H, blackDC, 0, 0, 1, 1, bf);
    const ActDef& a = sim.CurrentAct();
    wchar_t tag[32];
    swprintf_s(tag, L"ACT %d", sim.actIndex + 1);
    TextC(640, 286, tag, a.accent, 5);
    TextC(640, 336, a.cn, COL_WHITE, 5);
    TextC(640, 388, L"PHASE TRANSITION // HOLD MONITORING", COL_DIM, 2);
}

void Renderer::DrawEffects(Sim& sim) {
    DrawCorruption(sim);

    if (sim.ThreatLevel() >= 3 || sim.fx.glitch > 0.45f) {
        COLORREF rc = Blend(COL_RED, COL_BLACK, 0.45f);
        Fill(0, 0, VW, 2, rc);
        Fill(0, VH - 2, VW, 2, rc);
        Fill(0, 0, 2, VH, rc);
        Fill(VW - 2, 0, 2, VH, rc);
        for (int i = 0; i < 20; i++) {
            int sx = i * 64;
            Fill(sx, VH - 8, 32, 2, i % 2 ? Blend(COL_AMBER, COL_BLACK, 0.4f) : rc);
        }
    }

    if (sim.fx.brownout > 0.01f) {
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)(clampf(sim.fx.brownout, 0.0f, 1.0f) * 255.0f), 0};
        AlphaBlend(memDC, 0, 0, W, H, blackDC, 0, 0, 1, 1, bf);
    }
    if (sim.fx.flash > 0.01f) {
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)(clampf(sim.fx.flash, 0.0f, 1.0f) * 150.0f), 0};
        AlphaBlend(memDC, 0, 0, W, H, redDC, 0, 0, 1, 1, bf);
    }
    if (sim.fx.scanY > -20.0f && sim.fx.scanY < (float)VH + 20.0f) {
        int sy = PY(sim.fx.scanY);
        int sh = PS(18.0f);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 28, 0};
        AlphaBlend(memDC, 0, sy, W, sh, whiteDC, 0, 0, 1, 1, bf);
    }
}

void Renderer::DrawDebug(Sim& sim) {
    if (!sim.debugOverlay) return;
    Panel(20, 500, 430, 112, L"DEBUG // F1", COL_CYAN);
    for (int i = 0; i < (int)sim.debugLines.size() && i < 5; i++) {
        Text(30, 530 + i * 15, sim.debugLines[i].c_str(), COL_TEXT, 0);
    }
}

void Renderer::DrawToast(Sim& sim) {
    if (sim.modal.open || sim.toastTimer <= 0.0f || sim.toast.empty()) return;
    const bool info = sim.toast.rfind(L"MUSIC //", 0) == 0 ||
                      sim.toast.rfind(L"LAYOUT //", 0) == 0 ||
                      sim.toast.rfind(L"CONFIG RELOADED", 0) == 0 ||
                      sim.toast.rfind(L"TARGET PROFILE //", 0) == 0;
    const COLORREF color = info ? COL_CYAN : COL_RED;
    int w = TW(sim.toast.c_str(), 3) + 40;
    Fill(640 - w / 2, 592, w, 26, info ? RGB(2, 10, 13) : RGB(10, 2, 3));
    Frame(640 - w / 2, 592, w, 26, color);
    TextC(640, 596, sim.toast.c_str(), color, 3);
}

void Renderer::DrawSecondary(Sim& sim) {
    Panel(420, 280, 440, 160, L"SECTOR 02 // ISOLATED", COL_GREEN);
    TextC(640, 340, L"NO OPERATOR SESSION", COL_TEXT, 4);
    TextC(640, 370, L"SENTINEL DEFENSE GRID // STANDBY", COL_DIM, 1);
    TextC(640, 398, L"PRIMARY DISPLAY HOLDS CONTAINMENT CONTROL", COL_DIM, 0);
    wchar_t pct[8];
    swprintf_s(pct, L"CORE %d%%", (int)lroundf(sim.AvgIntegrity()));
    TextC(640, 418, pct, COL_GREEN, 0);
}

void Renderer::DrawGhostQuotes(Sim& sim) {
    if (sim.quotes.empty()) return;
    const bool blackBox = sim.theme == Theme::BlackBox;
    for (const auto& g : sim.ghosts) {
        if (!g.active || g.index < 0 || g.index >= (int)sim.quotes.size()) continue;
        float t = g.dur > 0.0f ? g.life / g.dur : 1.0f;
        if (t <= 0.0f || t >= 1.0f) continue;
        float fade = sinf(3.1415926f * t);
        COLORREF c = Blend(COL_BG, RGB(164, 220, 184), 0.30f + 0.55f * fade);
        const std::wstring& q = sim.quotes[g.index];
        int start = (std::max)(0, g.start);
        int end = (std::min)((int)q.size(), (std::max)(start, g.reveal));
        if (end <= start) continue;
        std::wstring view = q.substr((size_t)start, (size_t)(end - start));
        int x = (int)g.x;
        int y = (int)g.y;
        if (blackBox) {
            // Last Signal has no empty center: give the fragment a dedicated evidence-rail socket.
            Fill(30, 486, 226, 136, RGB(0, 5, 4));
            Frame(30, 486, 226, 136, Blend(COL_GREEN, COL_BLACK, 0.58f));
            TextUI(x, y - 20, L"ECHO // SIGNAL FRAGMENT", COL_DIM, fontUI);
            Line(x - 10, y - 2, x - 10, y + 46, Blend(COL_GREEN, COL_BLACK, 0.42f), 1);
        }
        int line = 0;
        size_t i = 0;
        while (i < view.size() && line < 3) {
            size_t n = (std::min)((size_t)(blackBox ? 10 : 22), view.size() - i);
            std::wstring part = view.substr(i, n);
            if (blackBox) {
                TextUI(x + 1, y + 1, part.c_str(), COL_BLACK, fontCNHead);
                TextUI(x, y, part.c_str(), c, fontCNHead);
            } else {
                TextTerm(x + 1, y + 1, part.c_str(), COL_BLACK, true);
                TextTerm(x, y, part.c_str(), c, true);
            }
            i += n;
            y += blackBox ? 20 : 14;
            line++;
        }
    }
}

void Renderer::Render(Sim& sim) {
    if (!memDC || !bgDC) return;
    BitBlt(memDC, 0, 0, W, H, bgDC, 0, 0, SRCCOPY);
    DrawAmbient(sim);

    if (!primary) {
        DrawSecondary(sim);
    } else {
        frameNo_++;
        DrawStatusBar(sim);
        switch (sim.theme) {
        case Theme::SplitOps:   DrawSplitOps(sim); break;
        case Theme::Boundary:   DrawBoundary(sim); break;
        case Theme::WarRoom:    DrawWarRoom(sim); break;
        case Theme::Forensic:   DrawForensic(sim); break;
        case Theme::Containment:DrawContainment(sim); break;
        case Theme::BlackBox:   DrawBlackBox(sim); break;
        default:                DrawNodeSidebar(sim); DrawTerminal(sim); break;
        }
        bool showFixed = sim.theme == Theme::Terminal;
        for (int i = 0; i < (int)sim.popups.items.size(); i++) {
            const Popup& p = sim.popups.items[i];
            if (!p.active) continue;
            if (sim.theme == Theme::BlackBox) continue;
            if (p.kind < 2 && !showFixed) continue;
            DrawPane(sim, p, i);
        }
        DrawMotionOverlay(sim);
        DrawFooter(sim);
        DrawGhostQuotes(sim);
        DrawEffects(sim);
        DrawTakeover(sim);
        DrawBoot(sim);
        DrawDebug(sim);
        DrawModal(sim);
        DrawToast(sim);
    }

    DrawSignature(sim);
    shakeX = shakeY = 0;
}

void Renderer::Present(HDC target) {
    if (!memDC || !target) return;
    int tw = outW > 0 ? outW : W;
    int th = outH > 0 ? outH : H;
    SetStretchBltMode(target, HALFTONE);
    SetBrushOrgEx(target, 0, 0, nullptr);
    StretchBlt(target, 0, 0, tw, th, memDC, 0, 0, W, H, SRCCOPY);
}

void Renderer::Shutdown() {
    ShutdownBuffers();
    ShutdownGdi();
    hwnd = nullptr;
}

bool Renderer::SavePng(const std::wstring& path) {
    if (!memBmp) return false;
    using namespace Gdiplus;
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return false;
    bool ok = false;
    {
        Bitmap bmp(memBmp, nullptr);
        if (bmp.GetLastStatus() == Ok) {
            CLSID png{};
            UINT num = 0, size = 0;
            if (GetImageEncodersSize(&num, &size) == Ok && size > 0) {
                auto* info = (ImageCodecInfo*)malloc(size);
                if (info) {
                    if (GetImageEncoders(num, size, info) == Ok) {
                        for (UINT i = 0; i < num; i++) {
                            if (wcscmp(info[i].MimeType, L"image/png") == 0) {
                                png = info[i].Clsid;
                                break;
                            }
                        }
                    }
                    free(info);
                }
            }
            if (png.Data1 || png.Data2 || png.Data3) {
                ok = (bmp.Save(path.c_str(), &png, nullptr) == Ok);
            }
        }
    }
    GdiplusShutdown(token);
    return ok;
}

