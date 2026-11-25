#pragma once
#include "common.h"
#include "sim.h"
#include "render.h"

#include <memory>
#include <vector>
#include <string>

struct ScreenWindow {
    HWND hwnd = nullptr;
    HMONITOR monitor = nullptr;
    RECT rect{};
    bool primary = false;
    Renderer renderer;
};

class App {
public:
    App();
    ~App();
    int Run(HINSTANCE hInst, int nCmdShow);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static BOOL CALLBACK MonitorProc(HMONITOR mon, HDC dc, LPRECT rc, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool ParseArgs();
    bool CreateWindows();
    void DestroyWindows();
    int MessageLoop();
    int RunScreenshot();
    int RunBenchmark();
    int RunSelfTest();
    int RunContentCheck();
    int RunMusicCheck();
    int RunPreview();
    void BuildDebugLines();
    void NextTrack();
    void SyncMusicState(bool announce);
    void ResetSystemStats();
    void UpdateSystemStats(float dt);
    double Now() const;

    HINSTANCE hInst_ = nullptr;
    Config cfg_;
    Sim sim_;
    std::vector<std::unique_ptr<ScreenWindow>> screens_;
    std::vector<std::wstring> args_;

    bool windowed_ = false;
    bool previewMode_ = false;
    bool screenshotMode_ = false;
    bool benchmarkMode_ = false;
    bool selftestMode_ = false;
    bool contentcheckMode_ = false;
    bool musiccheckMode_ = false;
    bool configMode_ = false;
    bool rebuildPending_ = false;
    bool allowClose_ = false;
    bool hotkeyRegistered_ = false;
    bool cursorHidden_ = false;
    float cursorTimer_ = 0.0f;

    HWND previewParent_ = nullptr;
    std::wstring screenshotPath_;
    std::wstring screenshotScenario_;
    std::wstring benchmarkOut_;
    std::wstring goalOverride_;
    float screenshotAt_ = 9.0f;
    uint32_t seed_ = 0;
    float testExitSeconds_ = 0.0f;
    float lastFrameMs_ = 0.0f;
    double startTime_ = 0.0;
    uint64_t prevIdle_ = 0;
    uint64_t prevKernel_ = 0;
    uint64_t prevUser_ = 0;
    uint64_t prevProcKernel_ = 0;
    uint64_t prevProcUser_ = 0;
    float statAccum_ = 0.0f;
    bool statsInit_ = false;
};
