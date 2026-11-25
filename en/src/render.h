#pragma once
#include "common.h"
#include "sim.h"
#include <map>
#include <utility>

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Init(HWND hwnd, int w, int h, bool primary);
    void Resize(int w, int h);
    void SetRenderScale(float s) { renderScale_ = clampf(s, 0.5f, 1.0f); }
    void Render(Sim& sim);
    void Present(HDC target);
    void Shutdown();
    bool SavePng(const std::wstring& path);

    int Width() const { return W; }
    int Height() const { return H; }
    bool ScreenToVirtual(int sx, int sy, float& vx, float& vy) const;

private:
    struct CacheSurface {
        HDC dc = nullptr;
        HBITMAP bmp = nullptr;
        void* bits = nullptr;
        int w = 0;
        int h = 0;
        int id = -1;
    };

    bool MakeDib(int w, int h, HDC& dc, HBITMAP& bmp, void** bits);
    void ShutdownBuffers();
    void DestroyCache(CacheSurface& c);
    void DestroyCaches();
    bool EnsureCache(CacheSurface& c, int w, int h);
    void DrawCachedWindow(CacheSurface& c, int x, int y, int w, int h,
                          bool refresh, void (Renderer::*draw)(Sim&), Sim& sim);
    void ShutdownGdi();
    void BuildFonts();
    void BuildBackground();
    void BuildRainAtlas();
    void DestroyRainAtlas();

    int PX(float x) const;
    int PY(float y) const;
    int PS(float v) const;

    HBRUSH Br(COLORREF c);
    HPEN Pen(COLORREF c, int w);
    void Fill(int x, int y, int w, int h, COLORREF c);
    void Frame(int x, int y, int w, int h, COLORREF c);
    void Line(int x1, int y1, int x2, int y2, COLORREF c, int width = 1);
    void Text(int x, int y, const wchar_t* s, COLORREF c, int fid);
    void TextR(int xRight, int y, const wchar_t* s, COLORREF c, int fid);
    void TextC(int cx, int y, const wchar_t* s, COLORREF c, int fid);
    void TextTyped(int x, int y, const wchar_t* s, COLORREF c, int fid, float chars);
    void TextTypedFont(int x, int y, const wchar_t* s, COLORREF c, HFONT f, float chars);
    void TextUI(int x, int y, const wchar_t* s, COLORREF c, HFONT f);
    void TextUIR(int xRight, int y, const wchar_t* s, COLORREF c, HFONT f);
    int TWUI(const wchar_t* s, HFONT f);
    int TW(const wchar_t* s, int fid);
    void Panel(int x, int y, int w, int h, const wchar_t* title, COLORREF accent);
    void Bar(int x, int y, int w, int h, float pct, COLORREF c);

    void DrawTop(Sim& sim);
    void DrawLeft(Sim& sim);
    void DrawCore(Sim& sim);
    void DrawInjection(Sim& sim);
    void DrawLog(Sim& sim);
    void DrawFiles(Sim& sim);
    void DrawBottom(Sim& sim);
    void DrawPopups(Sim& sim);
    void DrawPopupOne(const Popup& p, int px, int py);
    void DrawDesktop(Sim& sim);
    void DrawTaskbar(Sim& sim);
    void DrawMainConsole(Sim& sim);
    void DrawEventWindow(Sim& sim);
    void DrawServicesWindow(Sim& sim);
    void WindowFrame(int x, int y, int w, int h, const wchar_t* title,
                     COLORREF accent, bool active);
    void DrawRain(Sim& sim);
    void DrawAmbient(Sim& sim);
    void DrawBanner(Sim& sim);
    void DrawBoot(Sim& sim);
    void DrawModal(Sim& sim);
    void DrawEffects(Sim& sim);
    void DrawTakeover(Sim& sim);
    void DrawGlitchText(int x, int y, const wchar_t* s, int maxChars, int fid, COLORREF c, float intensity, uint32_t seed);
    void DrawCorruption(Sim& sim);
    void DrawGhostQuotes(Sim& sim);
    void DrawActCurtain(Sim& sim);
    void DrawActBadge(Sim& sim);
    void DrawSituationStrip(Sim& sim, int x, int y, int w, int lines);
    void DrawFileTree(Sim& sim, int x, int y, int w, int h);
    void DrawPaneDamage(Sim& sim, const Popup& p, int x, int y, int w, int h);
    void DrawDebug(Sim& sim);
    void DrawToast(Sim& sim);
    void DrawSecondary(Sim& sim);

    // v1.3 terminal wall
    void DrawStatusBar(Sim& sim);
    void DrawNodeSidebar(Sim& sim);
    void DrawTerminal(Sim& sim);
    void DrawLogStream(Sim& sim, int x, int y, int w, int h, const wchar_t* title,
                       int maxLines, LogStream filter = LogStream::Both);
    void DrawNodeTable(Sim& sim, int x, int y, int w, int h, int rows);
    void DrawPhaseBrief(Sim& sim, int x, int y, int w, int lines);
    void DrawSplitOps(Sim& sim);
    void DrawBoundary(Sim& sim);
    void DrawWarRoom(Sim& sim);
    void DrawForensic(Sim& sim);
    void DrawContainment(Sim& sim);
    void DrawBlackBox(Sim& sim);
    void DrawPane(Sim& sim, const Popup& p, int index);
    void DrawFooter(Sim& sim);
    void DrawSignature(Sim& sim);
    void DrawMotionOverlay(Sim& sim);
    void TextTerm(int x, int y, const wchar_t* s, COLORREF c, bool cn = false);

    // v1.9 BLACKTRACE: original, allocation-free hacking-tool choreography.
    void TextDecode(Sim& sim, int x, int y, const wchar_t* s, COLORREF c, int fid, int channel = 0);
    void TextDecodeC(Sim& sim, int cx, int y, const wchar_t* s, COLORREF c, int fid, int channel = 0);
    void PacketLine(Sim& sim, int x1, int y1, int x2, int y2, float t, COLORREF c);
    void DrawTraceCommand(Sim& sim, int x, int y, int w, int channel);
    void DrawHexTape(Sim& sim, int x, int y, int w, int rows, int channel);
    void DrawTargetBrackets(Sim& sim, int x, int y, int w, int h, COLORREF c);

    HWND hwnd = nullptr;
    int W = 0;
    int H = 0;
    int outW = 0;
    int outH = 0;
    float renderScale_ = 0.65f;
    bool primary = true;

    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    void* memBits = nullptr;

    HDC scratchDC = nullptr;
    HBITMAP scratchBmp = nullptr;
    void* scratchBits = nullptr;

    HDC bgDC = nullptr;
    HBITMAP bgBmp = nullptr;
    void* bgBits = nullptr;

    HDC redDC = nullptr;
    HBITMAP redBmp = nullptr;
    HDC blackDC = nullptr;
    HBITMAP blackBmp = nullptr;
    HDC whiteDC = nullptr;
    HBITMAP whiteBmp = nullptr;

    CacheSurface mainCache_;
    CacheSurface eventCache_;
    CacheSurface servicesCache_;
    std::array<CacheSurface, 14> popupCache_;
    uint64_t frameNo_ = 0;

    HFONT fonts[10]{};
    HFONT fontCN = nullptr;
    HFONT fontCNHead = nullptr;
    HFONT fontCNTerm = nullptr;
    HFONT rainFont = nullptr;
    HFONT fontUI = nullptr;
    HFONT fontUIHead = nullptr;

    std::map<COLORREF, HBRUSH> brushCache;
    std::map<std::pair<COLORREF, int>, HPEN> penCache;

    float scale = 1.0f;
    int ox = 0;
    int oy = 0;
    int shakeX = 0;
    int shakeY = 0;

    HDC rainMaskDC = nullptr;
    HBITMAP rainMaskBmp = nullptr;
    void* rainMaskBits = nullptr;
    HDC rainColorDC[4]{};
    HBITMAP rainColorBmp[4]{};
    void* rainColorBits[4]{};
    int rainCellW = 0;
    int rainCellH = 0;
    bool rainAtlasReady = false;
};
