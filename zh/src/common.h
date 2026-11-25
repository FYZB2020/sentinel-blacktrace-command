#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <random>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <algorithm>

constexpr int VW = 1280;
constexpr int VH = 720;

// 64 glyphs shared by the simulation and the renderer's glyph atlas.
inline const wchar_t* RainGlyphs() {
    return L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ<>/\\|=+-*#%&@$()[]{}:;!?~^_.";
}
constexpr int RAIN_GLYPH_COUNT = 64;

constexpr COLORREF COL_BG      = RGB(3, 7, 6);
constexpr COLORREF COL_PANEL   = RGB(5, 11, 9);
constexpr COLORREF COL_BORDER  = RGB(14, 59, 36);
constexpr COLORREF COL_TEXT    = RGB(184, 255, 214);
constexpr COLORREF COL_DIM     = RGB(62, 122, 90);
constexpr COLORREF COL_GREEN   = RGB(57, 255, 136);
constexpr COLORREF COL_AMBER   = RGB(255, 176, 32);
constexpr COLORREF COL_RED     = RGB(255, 45, 58);
constexpr COLORREF COL_CYAN    = RGB(53, 224, 255);
constexpr COLORREF COL_WHITE   = RGB(232, 255, 242);
constexpr COLORREF COL_DARKRED = RGB(90, 10, 18);
constexpr COLORREF COL_BLACK   = RGB(0, 0, 0);

struct Config {
    std::wstring code = L"1984";
    int fps = 30;
    std::wstring quality = L"auto";
    std::wstring monitorMode = L"all";
    int autoPromptSeconds = 0;
    int idleMinutes = 0;          // 0 = double-click only; >0 = Windows screen-saver idle timeout
    bool showSimulationTag = false; // retained for config compatibility; operational chrome is always shown
    bool keepDisplayOn = false;
    std::wstring language = L"bi";
    std::wstring flash = L"med";
    bool shake = false;          // v1.5: shake removed; kept for config compatibility
    bool glitch = true;
    float rainDensity = 0.6f;    // deprecated v1.5; ignored
    float speed = 1.6f;
    std::wstring theme = L"auto";
    std::wstring dust = L"med";
    std::wstring corruption = L"med";
    std::wstring motion = L"full"; // full | reduced; reduced keeps state feedback without large sweeps
    bool errorText = true;
    bool sound = true;
    bool quotes = true;
    std::wstring quotesFile = L"quotes.xlsx";
    bool music = true;
    std::wstring musicDir = L"music";
};

std::wstring GetExeDir();
std::wstring GetConfigPath();
Config LoadConfig();
void WriteDefaultConfig();
void OpenConfigInNotepad();

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float easeOutCubic(float t) { t = clampf(t, 0, 1); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
inline float smoothstepf(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }

inline COLORREF Blend(COLORREF a, COLORREF b, float t) {
    t = clampf(t, 0, 1);
    int r = (int)lerpf((float)GetRValue(a), (float)GetRValue(b), t);
    int g = (int)lerpf((float)GetGValue(a), (float)GetGValue(b), t);
    int bl = (int)lerpf((float)GetBValue(a), (float)GetBValue(b), t);
    return RGB(r, g, bl);
}
