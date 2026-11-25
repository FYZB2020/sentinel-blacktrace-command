// Sentinel Defense Grid -- application, windowing, modes and self checks.
#include "app.h"
#include "audio.h"
#include "music.h"
#include "tracefx.h"

#include <shellapi.h>
#include <psapi.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <string>

namespace {

const wchar_t* kScreenClass = L"SentinelScreen";
const wchar_t* kPreviewClass = L"SentinelPreview";

std::wstring FormatW(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    buf[0] = 0;
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(buf, 1024, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;
    fwprintf(f, L"%ls", text.c_str());
    fclose(f);
    return true;
}

} // namespace

App::App() = default;

App::~App() {
    DestroyWindows();
}

int App::Run(HINSTANCE hInst, int) {
    hInst_ = hInst;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    WriteDefaultConfig();
    cfg_ = LoadConfig();
    ParseArgs();

    if (configMode_) {
        OpenConfigInNotepad();
        return 0;
    }
    if (screenshotMode_) return RunScreenshot();
    if (benchmarkMode_) return RunBenchmark();
    if (selftestMode_) return RunSelfTest();
    if (contentcheckMode_) return RunContentCheck();
    if (musiccheckMode_) return RunMusicCheck();
    if (previewMode_) return RunPreview();

    AudioInit(cfg_.sound);
    MusicInit(cfg_.music, cfg_.musicDir);
    sim_.Init(cfg_, seed_ ? seed_ : (uint32_t)GetTickCount());
    SyncMusicState(false);
    if (cfg_.quotes && !cfg_.quotesFile.empty()) sim_.LoadQuotes(cfg_.quotesFile);
    if (cfg_.music) {
        const std::wstring& track = MusicCurrent();
        if (!track.empty()) sim_.PushLog(Severity::Info, L"MUSIC", L"now playing: %ls", track.c_str());
        else sim_.PushLog(Severity::Warn, L"MUSIC", L"no tracks found in music folder");
    }
    if (!goalOverride_.empty()) {
        if (goalOverride_ == L"ransomware") sim_.SetGoal(AttackGoal::Ransomware);
        else if (goalOverride_ == L"credential") sim_.SetGoal(AttackGoal::CredentialTheft);
        else if (goalOverride_ == L"miner") sim_.SetGoal(AttackGoal::CryptoMiner);
        else if (goalOverride_ == L"espionage") sim_.SetGoal(AttackGoal::Espionage);
        else if (goalOverride_ == L"sabotage") sim_.SetGoal(AttackGoal::Sabotage);
        else sim_.SetGoal(AttackGoal::Exfiltrate);
    }
    if (!CreateWindows()) return 1;

    int rc = MessageLoop();
    DestroyWindows();
    MusicShutdown();
    AudioShutdown();
    return rc;
}

bool App::ParseArgs() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    args_.clear();
    for (int i = 1; i < argc; ++i) args_.push_back(argv[i]);
    LocalFree(argv);

    for (size_t i = 0; i < args_.size(); ++i) {
        const std::wstring& a = args_[i];
        auto next = [&](std::wstring& out) -> bool {
            if (i + 1 >= args_.size()) return false;
            out = args_[++i];
            return true;
        };

        if (a == L"--windowed") {
            windowed_ = true;
        } else if (a == L"--screenshot") {
            screenshotMode_ = true;
            std::wstring v;
            if (next(v)) screenshotPath_ = v;
        } else if (a == L"--at") {
            std::wstring v;
            if (next(v)) screenshotAt_ = (float)wcstod(v.c_str(), nullptr);
        } else if (a == L"--scenario") {
            std::wstring v;
            if (next(v)) screenshotScenario_ = v;
        } else if (a == L"--theme") {
            std::wstring v;
            if (next(v)) cfg_.theme = v;
        } else if (a == L"--goal") {
            std::wstring v;
            if (next(v)) goalOverride_ = v;
        } else if (a == L"--benchmark") {
            benchmarkMode_ = true;
        } else if (a == L"--benchmark-out") {
            std::wstring v;
            if (next(v)) benchmarkOut_ = v;
        } else if (a == L"--selftest") {
            selftestMode_ = true;
        } else if (a == L"--contentcheck") {
            contentcheckMode_ = true;
        } else if (a == L"--musiccheck") {
            musiccheckMode_ = true;
        } else if (a == L"--seed") {
            std::wstring v;
            if (next(v)) seed_ = (uint32_t)wcstoul(v.c_str(), nullptr, 10);
        } else if (a == L"--test-exit") {
            std::wstring v;
            if (next(v)) testExitSeconds_ = (float)wcstod(v.c_str(), nullptr);
        } else if (a == L"/c" || a == L"-c" || a == L"--config" ||
                   a.rfind(L"/c:", 0) == 0 || a.rfind(L"-c:", 0) == 0) {
            configMode_ = true;
        } else if (a == L"/p" || a == L"-p") {
            previewMode_ = true;
            std::wstring v;
            if (next(v)) previewParent_ = (HWND)(uintptr_t)wcstoull(v.c_str(), nullptr, 10);
        } else if (a.rfind(L"/p:", 0) == 0 || a.rfind(L"-p:", 0) == 0 ||
                   a.rfind(L"/p=", 0) == 0 || a.rfind(L"-p=", 0) == 0) {
            previewMode_ = true;
            std::wstring v = a.substr(3);
            previewParent_ = (HWND)(uintptr_t)wcstoull(v.c_str(), nullptr, 10);
        } else if (a == L"/s" || a == L"-s") {
            // Run mode, the default.
        }
    }

    if (screenshotMode_ && screenshotPath_.empty()) screenshotPath_ = L"sentinel.png";
    return true;
}

BOOL CALLBACK App::MonitorProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<MONITORINFOEXW>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) v->push_back(mi);
    return TRUE;
}

bool App::CreateWindows() {
    if (!screens_.empty()) DestroyWindows();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst_;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kScreenClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    if (windowed_) {
        RECT wr{100, 100, 100 + 1600, 100 + 900};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        HWND hwnd = CreateWindowExW(0, kScreenClass, L"Sentinel Defense Grid (windowed)",
                                    WS_OVERLAPPEDWINDOW, wr.left, wr.top,
                                    wr.right - wr.left, wr.bottom - wr.top,
                                    nullptr, nullptr, hInst_, this);
        if (!hwnd) return false;

        auto sw = std::make_unique<ScreenWindow>();
        sw->hwnd = hwnd;
        sw->primary = true;
        RECT cr{};
        GetClientRect(hwnd, &cr);
        sw->rect = cr;
        sw->renderer.Init(hwnd, cr.right, cr.bottom, true);
        screens_.push_back(std::move(sw));

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        allowClose_ = true;
        hotkeyRegistered_ = RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_SHIFT, 'Q') != FALSE;
        return true;
    }

    std::vector<MONITORINFOEXW> mons;
    EnumDisplayMonitors(nullptr, nullptr, MonitorProc, (LPARAM)&mons);
    if (mons.empty()) return false;

    bool havePrimary = false;
    for (auto& mi : mons) {
        if (mi.dwFlags & MONITORINFOF_PRIMARY) { havePrimary = true; break; }
    }

    for (size_t idx = 0; idx < mons.size(); ++idx) {
        const MONITORINFOEXW& mi = mons[idx];
        bool primary = ((mi.dwFlags & MONITORINFOF_PRIMARY) != 0) || (!havePrimary && idx == 0);
        if (cfg_.monitorMode == L"primary" && !primary) continue;

        RECT r = mi.rcMonitor;
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        if (w < 16 || h < 16) continue;

        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kScreenClass,
                                    L"Sentinel Defense Grid", WS_POPUP,
                                    r.left, r.top, w, h, nullptr, nullptr, hInst_, this);
        if (!hwnd) continue;

        auto sw = std::make_unique<ScreenWindow>();
        sw->hwnd = hwnd;
        sw->rect = r;
        sw->primary = primary;
        sw->renderer.Init(hwnd, w, h, primary);
        screens_.push_back(std::move(sw));

        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, w, h, SWP_SHOWWINDOW);
    }

    if (screens_.empty()) return false;

    HWND focus = nullptr;
    for (auto& s : screens_) {
        if (s->primary) { focus = s->hwnd; break; }
    }
    if (!focus) focus = screens_[0]->hwnd;

    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();
    if (fgThread && fgThread != myThread) AttachThreadInput(myThread, fgThread, TRUE);
    SetForegroundWindow(focus);
    SetFocus(focus);
    if (fgThread && fgThread != myThread) AttachThreadInput(myThread, fgThread, FALSE);

    while (ShowCursor(FALSE) >= 0) {}
    cursorHidden_ = true;

    if (cfg_.keepDisplayOn) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    }
    hotkeyRegistered_ = RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_SHIFT, 'Q') != FALSE;
    allowClose_ = false;
    return true;
}

void App::DestroyWindows() {
    if (hotkeyRegistered_) {
        UnregisterHotKey(nullptr, 1);
        hotkeyRegistered_ = false;
    }
    for (auto& s : screens_) {
        s->renderer.Shutdown();
        if (s->hwnd && IsWindow(s->hwnd)) DestroyWindow(s->hwnd);
    }
    screens_.clear();

    if (cursorHidden_) {
        while (ShowCursor(TRUE) < 0) {}
        cursorHidden_ = false;
    }
    SetThreadExecutionState(ES_CONTINUOUS);
    allowClose_ = false;
}

int App::MessageLoop() {
    startTime_ = Now();
    double last = startTime_;
    const double frame = 1.0 / (double)(cfg_.fps > 0 ? cfg_.fps : 30);
    ResetSystemStats();

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            if (msg.message == WM_HOTKEY && msg.wParam == 1) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
            (GetAsyncKeyState('Q') & 0x8000)) {
            return 0;
        }

        if (rebuildPending_) {
            rebuildPending_ = false;
            DestroyWindows();
            if (!CreateWindows()) return 1;
            continue;
        }

        double now = Now();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0.0f) dt = 0.0f;

        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&a);

        const std::wstring previousTrack = sim_.musicTrack;
        sim_.Update(dt);
        MusicUpdate(dt);
        SyncMusicState(false);
        if (previousTrack != sim_.musicTrack && !sim_.musicTrack.empty()) {
            SyncMusicState(true); // automatic end-of-track rollover gets the same clear feedback
        }
        if (sim_.audioCue >= 0) {
            AudioPlay(sim_.audioCue);
            sim_.audioCue = -1;
        }
        UpdateSystemStats(dt);
        if (!cursorHidden_) {
            cursorTimer_ -= dt;
            if (cursorTimer_ <= 0.0f) {
                while (ShowCursor(FALSE) >= 0) {}
                cursorHidden_ = true;
            }
        }
        if (sim_.exitRequested) return 0;
        if (sim_.debugOverlay) BuildDebugLines();

        for (auto& s : screens_) {
            s->renderer.Render(sim_);
            HDC dc = GetDC(s->hwnd);
            if (dc) {
                s->renderer.Present(dc);
                ReleaseDC(s->hwnd, dc);
            }
        }

        QueryPerformanceCounter(&b);
        lastFrameMs_ = (float)((double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)freq.QuadPart);

        if (testExitSeconds_ > 0.0f && (now - startTime_) >= (double)testExitSeconds_) return 0;

        double remain = (last + frame) - Now();
        int sleepMs = (int)(remain * 1000.0);
        if (sleepMs > 0) {
            Sleep(sleepMs);
        } else if (sleepMs < -1000) {
            last = Now();
        }
    }
}

LRESULT CALLBACK App::PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) return TRUE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        app = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (app) return app->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_CLOSE:
        if (allowClose_) return DefWindowProcW(hwnd, msg, wp, lp);
        return 0;

    case WM_DESTROY:
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        return 0;

    case WM_SETCURSOR:
        SetCursor(cursorHidden_ ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_MOUSEMOVE:
        if (cursorHidden_) {
            while (ShowCursor(TRUE) < 0) {}
            cursorHidden_ = false;
        }
        cursorTimer_ = 3.0f;
        return 0;

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);
        bool hit = false;
        float hitX = -1.0f, hitY = -1.0f;
        for (auto& s : screens_) {
            if (!s->primary) continue;
            float vx = 0.0f, vy = 0.0f;
            if (s->renderer.ScreenToVirtual(mx, my, vx, vy)) {
                hit = sim_.ClickBox(vx, vy);
                hitX = vx;
                hitY = vy;
            }
        }
        if (!sim_.modal.open && sim_.focusBox < 0 &&
            hitX >= 748.0f && hitX < 1020.0f && hitY >= 696.0f && hitY <= 720.0f) {
            NextTrack();
            return 0;
        }
        if (hit) AudioPlay(AUDIO_TICK);
        if (!hit) sim_.ClearFocus();
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
        return 0;

    case WM_SYSCOMMAND: {
        UINT cmd = (UINT)(wp & 0xFFF0);
        if (cmd == SC_CLOSE || cmd == SC_MINIMIZE || cmd == SC_MOVE || cmd == SC_SIZE) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_SYSKEYDOWN:
        if (wp == VK_F4 && (lp & 0x20000000)) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_KEYDOWN:
        switch (wp) {
        case VK_RETURN:
            if (sim_.modal.open) {
                sim_.modal.Submit(sim_);
            } else if (sim_.focusBox >= 0) {
                if (GetKeyState(VK_CONTROL) & 0x8000) sim_.OpenModal();
                else sim_.SubmitBox();
            } else {
                sim_.OpenModal();
            }
            return 0;
        case VK_ESCAPE:
            if (sim_.focusBox >= 0) {
                sim_.ClearFocus();
            } else if (sim_.modal.open) {
                if (sim_.modal.phase == 0) sim_.modal.Close();
            } else {
                sim_.toast = L"OVERRIDE REQUIRED \u2014 PRESS ENTER";
                sim_.toastTimer = 2.5f;
            }
            return 0;
        case VK_BACK:
            if (sim_.modal.open) sim_.modal.Backspace();
            else if (sim_.focusBox >= 0) sim_.BackspaceBox();
            return 0;
        case VK_F8:
            if (!sim_.modal.open && sim_.focusBox < 0) NextTrack();
            return 0;
        case 'M':
            if (!sim_.modal.open && sim_.focusBox < 0) {
                NextTrack();
                return 0;
            }
            break; // M remains ordinary text inside the override code or fake terminal
        case VK_F1:
            sim_.debugOverlay = !sim_.debugOverlay;
            return 0;
        case VK_F6: {
            static const wchar_t* names[] = {L"TERMINAL", L"SPLIT OPS", L"BOUNDARY", L"WAR ROOM", L"FORENSIC", L"CONTAINMENT", L"BLACK BOX"};
            sim_.NextTheme();
            sim_.toast = FormatW(L"LAYOUT // %ls", names[(int)sim_.theme]);
            sim_.toastTimer = 1.8f;
            sim_.PushLog(Severity::Info, L"SYS", L"theme preview -- %ls", names[(int)sim_.theme]);
            return 0;
        }
        case VK_F7:
            sim_.NextGoal();
            sim_.toast = FormatW(L"TARGET PROFILE // %02d", (int)sim_.campaign.goal + 1);
            sim_.toastTimer = 1.8f;
            sim_.PushLog(Severity::Info, L"SYS", L"campaign goal preview");
            return 0;
        case VK_F5:
            cfg_ = LoadConfig();
            sim_.cfg = cfg_;
            AudioInit(cfg_.sound);
            MusicInit(cfg_.music, cfg_.musicDir);
            SyncMusicState(false);
            if (cfg_.quotes) sim_.LoadQuotes(cfg_.quotesFile);
            else sim_.quotes.clear();
            sim_.toast = L"CONFIG RELOADED // MOTION " + cfg_.motion;
            sim_.toastTimer = 2.2f;
            sim_.PushLog(Severity::Info, L"SYS", L"CONFIG RELOADED");
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_CHAR:
        if (sim_.modal.open) {
            if (wp >= 0x21 && wp <= 0x7E) {
                sim_.modal.Add((wchar_t)wp);
                AudioPlay(AUDIO_TYPE);
                return 0;
            }
            if (wp == L'\r' || wp == L'\b' || wp == 27) return 0;
        } else if (sim_.focusBox >= 0) {
            if (wp >= 0x20 && wp <= 0x7E) {
                sim_.AddBoxChar((wchar_t)wp);
                AudioPlay(AUDIO_TYPE);
                return 0;
            }
            if (wp == L'\r' || wp == L'\b') return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_HOTKEY:
        if (wp == 1) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_DISPLAYCHANGE:
        rebuildPending_ = true;
        return 0;

    case WM_ACTIVATEAPP:
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int App::RunPreview() {
    if (!previewParent_ || !IsWindow(previewParent_)) return 1;

    RECT rc{};
    GetClientRect(previewParent_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 16 || h < 16) return 1;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = hInst_;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kPreviewClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE,
                                0, 0, w, h, previewParent_, nullptr, hInst_, nullptr);
    if (!hwnd) return 1;

    Sim sim;
    sim.Init(cfg_, seed_ ? seed_ : 1u);
    Renderer renderer;
    if (!renderer.Init(hwnd, w, h, true)) {
        DestroyWindow(hwnd);
        return 1;
    }

    ULONGLONG lastTick = GetTickCount64();
    while (IsWindow(previewParent_) && IsWindow(hwnd)) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                renderer.Shutdown();
                DestroyWindow(hwnd);
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        ULONGLONG nowTick = GetTickCount64();
        float dt = (float)((double)(nowTick - lastTick) / 1000.0);
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.25f) dt = 0.25f;
        lastTick = nowTick;

        sim.Update(dt);
        renderer.Render(sim);
        HDC dc = GetDC(hwnd);
        if (dc) {
            renderer.Present(dc);
            ReleaseDC(hwnd, dc);
        }
        Sleep(1000 / 12);
    }

    renderer.Shutdown();
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    return 0;
}

int App::RunScreenshot() {
    Sim sim;
    sim.Init(cfg_, seed_ ? seed_ : 12345u);
    if (!goalOverride_.empty()) {
        if (goalOverride_ == L"ransomware") sim.SetGoal(AttackGoal::Ransomware);
        else if (goalOverride_ == L"credential") sim.SetGoal(AttackGoal::CredentialTheft);
        else if (goalOverride_ == L"miner") sim.SetGoal(AttackGoal::CryptoMiner);
        else if (goalOverride_ == L"espionage") sim.SetGoal(AttackGoal::Espionage);
        else if (goalOverride_ == L"sabotage") sim.SetGoal(AttackGoal::Sabotage);
        else sim.SetGoal(AttackGoal::Exfiltrate);
    }
    if (cfg_.quotes && !cfg_.quotesFile.empty()) sim.LoadQuotes(cfg_.quotesFile);

    std::wstring sc = screenshotScenario_;
    if (sc == L"calm") {
        sim.SetBeat(Beat::Calm, 120.0f);
    } else if (sc == L"probe") {
        sim.SetBeat(Beat::Probe, 120.0f);
    } else if (sc == L"breach") {
        sim.SetBeat(Beat::Breach, 120.0f);
    } else if (sc == L"cascade") {
        sim.TriggerCascade();
        sim.SetBeat(Beat::Cascade, 120.0f);
    } else if (sc == L"recovery") {
        sim.TriggerCascade();
        sim.SetBeat(Beat::Recovery, 120.0f);
        sim.StartRecovery();
    } else if (sc == L"modal") {
        sim.SetBeat(Beat::Breach, 120.0f);
        sim.OpenModal();
    }

    int steps = (int)(screenshotAt_ * 30.0f);
    if (steps < 0) steps = 0;
    for (int i = 0; i < steps; ++i) sim.Update(1.0f / 30.0f);

    // Deterministic operator-dock coverage: permanent slots must work with 0, 1 or 2 popups.
    if (sc == L"dock-empty" || sc == L"dock-one" || sc == L"dock-many") {
        sim.SetTheme(Theme::Terminal);
        for (size_t i = 2; i < sim.popups.items.size(); ++i) sim.popups.items[i].active = false;
        int wanted = sc == L"dock-one" ? 1 : (sc == L"dock-many" ? 2 : 0);
        for (int i = 0; i < wanted; ++i) sim.popups.SpawnFloating(sim);
        sim.popups.floatingCount = wanted;
    }

    if (sc == L"damage") {
        sim.fx.screenDamage = 1.0f;
        sim.fx.TriggerTear(1.0f, 3.0f);
        sim.fx.TriggerStatic(1.0f, 3.0f);
    }
    if (sc == L"act") {
        sim.SetAct(Act::Escalation, 3.0f);
    }
    if (sc == L"ghost") {
        sim.SetTheme(Theme::BlackBox);
        if (sim.quotes.empty()) sim.quotes.push_back(L"The signal never disappeared; it only changed the way it is heard.");
        GhostQuote& g = sim.ghosts[0];
        g.active = true;
        g.index = 0;
        g.x = 48.0f;
        g.y = 526.0f;
        g.life = 2.0f;
        g.dur = 12.0f;
        g.start = 0;
        g.reveal = (int)sim.quotes[0].size();
        for (size_t i = 1; i < sim.ghosts.size(); ++i) sim.ghosts[i].active = false;
    }

    Renderer renderer;
    renderer.SetRenderScale(1.0f);
    if (!renderer.Init(nullptr, 2560, 1440, true)) return 1;
    renderer.Render(sim);
    DeleteFileW(screenshotPath_.c_str());
    bool ok = renderer.SavePng(screenshotPath_);
    renderer.Shutdown();
    return ok ? 0 : 1;
}

int App::RunBenchmark() {
    Sim sim;
    sim.Init(cfg_, seed_ ? seed_ : 1u);
    for (int i = 0; i < 240; ++i) sim.Update(1.0f / 30.0f);

    Renderer renderer;
    if (!renderer.Init(nullptr, 2560, 1440, true)) return 1;

    for (int i = 0; i < 60; ++i) {
        sim.Update(1.0f / 30.0f);
        renderer.Render(sim);
    }

    auto ft64 = [](const FILETIME& ft) -> uint64_t {
        return ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    };

    FILETIME c0{}, e0{}, k0{}, u0{};
    GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    const int kFrames = 600;
    std::vector<double> frameMs;
    frameMs.reserve(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        sim.Update(1.0f / 30.0f);
        LARGE_INTEGER a{}, b{};
        QueryPerformanceCounter(&a);
        renderer.Render(sim);
        QueryPerformanceCounter(&b);
        frameMs.push_back((double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)freq.QuadPart);
    }

    QueryPerformanceCounter(&t1);

    FILETIME c1{}, e1{}, k1{}, u1{};
    GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);

    double wall = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    double cpuSeconds = (double)((ft64(k1) - ft64(k0)) + (ft64(u1) - ft64(u0))) / 10000000.0;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    int cores = (int)si.dwNumberOfProcessors;
    if (cores < 1) cores = 1;

    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    double memMB = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);

    DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    double exeKB = 0.0;
    if (GetFileAttributesExW(exePath, GetFileExInfoStandard, &fad)) {
        exeKB = ((double)fad.nFileSizeHigh * 4294967296.0 + (double)fad.nFileSizeLow) / 1024.0;
    }

    std::sort(frameMs.begin(), frameMs.end());
    double sum = 0.0;
    for (double v : frameMs) sum += v;
    double avg = sum / (double)frameMs.size();
    double p95 = frameMs[(size_t)((frameMs.size() - 1) * 0.95)];
    double maxMs = frameMs.back();
    double estCpu = (wall > 0.0) ? (cpuSeconds / wall * 100.0 / (double)cores) : 0.0;

    bool pass = avg <= 20.0 && estCpu <= 3.0 && memMB <= 500.0 && gdi <= 1000;

    std::wstring report;
    report += L"Sentinel benchmark\n";
    report += FormatW(L"frames: %d @ 2560x1440 (offscreen)\n", kFrames);
    report += FormatW(L"avg frame: %.2f ms\n", avg);
    report += FormatW(L"p95 frame: %.2f ms\n", p95);
    report += FormatW(L"max frame: %.2f ms\n", maxMs);
    report += FormatW(L"estimated CPU: %.2f %% (process / %d logical cores)\n", estCpu, cores);
    report += FormatW(L"working set: %.1f MB\n", memMB);
    report += FormatW(L"GDI objects: %lu\n", (unsigned long)gdi);
    report += FormatW(L"exe size: %.1f KB\n", exeKB);
    report += FormatW(L"budgets: avg<=20ms, CPU<=3%%, RAM<=500MB, GDI<=1000\n");
    report += std::wstring(pass ? L"result: PASS\n" : L"result: FAIL\n");

    std::wstring out = benchmarkOut_;
    if (out.empty()) out = GetExeDir() + L"\\benchmark.txt";
    WriteTextFile(out, report);
    renderer.Shutdown();
    return pass ? 0 : 1;
}

int App::RunSelfTest() {
    Sim sim;
    sim.Init(cfg_, 7u);

    bool boundsOk = true;
    bool cascadeSeen = false;
    bool allOffline = false;
    bool boundaryQueueOk = true;
    bool boundaryStateOk = true;
    bool phaseWrapSeen = false;
    bool adjacentGoalOk = true;
    std::array<bool, 14> phasesSeen{};
    phasesSeen[(size_t)sim.opIndex] = true;
    int previousCycle = sim.opCycle;
    AttackGoal previousGoal = sim.campaign.goal;

    const int kUpdates = 54000;
    for (int i = 0; i < kUpdates; ++i) {
        sim.Update(1.0f / 30.0f);
        for (const auto& fw : sim.firewalls) {
            if (!std::isfinite(fw.integrity) || fw.integrity < -0.01f || fw.integrity > 100.01f) boundsOk = false;
        }
        int off = sim.CountState(FWState::Offline);
        int reb = sim.CountState(FWState::Rebuilding);
        if (off == 6) allOffline = true;
        if (off == 2 && reb == 3) cascadeSeen = true;
        if (sim.opIndex >= 0 && sim.opIndex < 14) phasesSeen[(size_t)sim.opIndex] = true;
        else boundaryStateOk = false;
        if (sim.opCycle != previousCycle) {
            if (sim.opIndex != 0) adjacentGoalOk = false;
            phaseWrapSeen = true;
            if (sim.campaign.goal == previousGoal) adjacentGoalOk = false;
            previousGoal = sim.campaign.goal;
            previousCycle = sim.opCycle;
        }
        if (sim.boundary.stream.size() > 28 || sim.boundary.events.size() > 22) boundaryQueueOk = false;
        for (const auto& target : sim.boundary.targets) {
            if (!std::isfinite(target.angle) || target.angle < -0.001f || target.angle > 6.284f ||
                target.stateTimer < -0.001f || (int)target.state < 0 || (int)target.state > 3)
                boundaryStateOk = false;
        }
    }
    bool phasesOk = true;
    for (bool seen : phasesSeen) if (!seen) phasesOk = false;
    bool logOk = !sim.log.entries.empty();

    // Force six real director wrap points to verify complete, non-repeating incident rotation.
    Sim rotationSim;
    rotationSim.Init(cfg_, 77u);
    std::array<bool, 6> goalsSeen{};
    goalsSeen[(size_t)rotationSim.campaign.goal] = true;
    bool rotationOk = true;
    for (int i = 0; i < 6; ++i) {
        AttackGoal before = rotationSim.campaign.goal;
        AttackGoal expected = (AttackGoal)(((int)before + 1) % (int)AttackGoal::Count);
        rotationSim.SetOpPhase(OpPhase::Aftermath, 0.01f);
        rotationSim.Update(0.02f);
        if (rotationSim.opIndex != 0 || rotationSim.campaign.goal != expected || before == expected) rotationOk = false;
        goalsSeen[(size_t)rotationSim.campaign.goal] = true;
    }
    for (bool seen : goalsSeen) if (!seen) rotationOk = false;

    // Exercise every target state, purge, regeneration, movement and bounded text wires.
    Config boundaryCfg = cfg_;
    boundaryCfg.theme = L"boundary";
    boundaryCfg.motion = L"full";
    Sim boundarySim;
    boundarySim.Init(boundaryCfg, 23u);
    std::array<float, 14> initialAngles{};
    for (size_t i = 0; i < initialAngles.size(); ++i) initialAngles[i] = boundarySim.boundary.targets[i].angle;
    bool statesSeen[4]{};
    bool movementSeen = false;
    bool regenerationSeen = false;
    for (int i = 0; i < 900; ++i) {
        boundarySim.Update(1.0f / 30.0f);
        for (size_t n = 0; n < boundarySim.boundary.targets.size(); ++n) {
            const auto& target = boundarySim.boundary.targets[n];
            statesSeen[(int)target.state] = true;
            if (fabsf(target.angle - initialAngles[n]) > 0.001f) movementSeen = true;
            if (target.generation > 0) regenerationSeen = true;
        }
    }
    bool boundaryLifecycleOk = statesSeen[0] && statesSeen[1] && statesSeen[2] && statesSeen[3] &&
                               movementSeen && regenerationSeen && boundarySim.boundary.purges >= 8 &&
                               boundarySim.boundary.stream.size() <= 28 && boundarySim.boundary.events.size() <= 22;

    Sim modalSim;
    modalSim.Init(cfg_, 9u);
    modalSim.OpenModal();
    for (int attempt = 0; attempt < 3; ++attempt) {
        for (int k = 0; k < 4; ++k) modalSim.modal.Add(L'0');
        modalSim.modal.Submit(modalSim);
        for (int i = 0; i < 45; ++i) modalSim.modal.Update(1.0f / 30.0f, modalSim);
    }
    bool wrongRejected = (modalSim.modal.attempts == 0);
    bool lockoutSeen = (modalSim.modal.lockout > 0.0f);
    for (int i = 0; i < 180; ++i) modalSim.modal.Update(1.0f / 30.0f, modalSim);
    const std::wstring code = L"1984";
    for (wchar_t ch : code) modalSim.modal.Add(ch);
    modalSim.modal.Submit(modalSim);
    for (int i = 0; i < 120; ++i) {
        modalSim.modal.Update(1.0f / 30.0f, modalSim);
        if (modalSim.exitRequested) break;
    }
    bool success = modalSim.exitRequested;

    Config reducedCfg = cfg_;
    reducedCfg.motion = L"reduced";
    reducedCfg.theme = L"terminal";
    Sim motionSim;
    motionSim.Init(reducedCfg, 21u);
    bool reducedOk = motionSim.motion.phase > 0.0f && motionSim.motion.phaseDuration <= 0.2801f;
    bool dockOk = true;
    for (int i = 0; i < 90; ++i) {
        motionSim.Update(1.0f / 30.0f);
        if (!std::isfinite(motionSim.motion.phase) || motionSim.motion.phase < 0.0f ||
            motionSim.motion.pulse < 0.0f || motionSim.motion.pulse > 1.001f) reducedOk = false;
    }
    for (const auto& p : motionSim.popups.items) {
        if (!p.active || p.kind < 2) continue;
        if (p.x < 218.0f || p.x + p.w > 838.0f || p.y < 484.0f || p.y + p.h > 688.0f) dockOk = false;
    }
    motionSim.NextTheme();
    bool themePulse = motionSim.motion.theme > 0.0f;

    // Reduced motion freezes orbital positions but leaves counters and streams live.
    reducedCfg.theme = L"boundary";
    Sim reducedBoundary;
    reducedBoundary.Init(reducedCfg, 29u);
    for (size_t i = 0; i < initialAngles.size(); ++i) initialAngles[i] = reducedBoundary.boundary.targets[i].angle;
    int reducedSerial = reducedBoundary.boundary.serial;
    bool reducedOrbitOk = true;
    for (int i = 0; i < 240; ++i) {
        reducedBoundary.Update(1.0f / 30.0f);
        for (size_t n = 0; n < initialAngles.size(); ++n)
            if (fabsf(reducedBoundary.boundary.targets[n].angle - initialAngles[n]) > 0.0001f) reducedOrbitOk = false;
    }
    if (reducedBoundary.boundary.serial <= reducedSerial || reducedBoundary.boundary.purges < 1) reducedOrbitOk = false;

    bool traceOk = fabsf(TraceFX::Cycle(0.0, 1.0f, 0.0f)) < 0.0001f &&
                   fabsf(TraceFX::Cycle(-0.25, 1.0f, 0.0f) - 0.75f) < 0.0001f &&
                   fabsf(TraceFX::Cycle(2.75, 1.0f, 0.0f) - 0.75f) < 0.0001f;
    for (uint32_t i = 0; i < 256; ++i) {
        float unit = TraceFX::Unit(i * 7919u);
        float cycle = TraceFX::Cycle(i * 0.371, 0.83f, 0.17f);
        if (unit < 0.0f || unit > 1.0f || cycle < 0.0f || cycle >= 1.0f ||
            TraceFX::Hash32(i) != TraceFX::Hash32(i) ||
            TraceFX::Byte(0x7F3Au, 3, (int)i) != TraceFX::Byte(0x7F3Au, 3, (int)i) ||
            TraceFX::Glyph(i) == 0) traceOk = false;
    }

    // Render all seven production layouts through the real GDI renderer.
    Renderer renderCheck;
    bool renderOk = renderCheck.Init(nullptr, 1280, 720, true);
    DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    if (renderOk) {
        Sim renderSim;
        renderSim.Init(cfg_, 31u);
        for (int i = 0; i < (int)Theme::Count; ++i) {
            renderSim.SetTheme((Theme)i);
            renderSim.SetGoal((AttackGoal)(i % (int)AttackGoal::Count));
            renderSim.time = 8.0 + i * 3.0;
            renderCheck.Render(renderSim);
        }
    }
    DWORD gdiAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    if (gdiAfter > 1000 || gdiAfter > gdiBefore + 256) renderOk = false;
    renderCheck.Shutdown();

    bool pass = boundsOk && cascadeSeen && !allOffline && logOk && phasesOk && phaseWrapSeen && adjacentGoalOk &&
                rotationOk && boundaryQueueOk && boundaryStateOk && boundaryLifecycleOk && reducedOrbitOk &&
                wrongRejected && lockoutSeen && success && reducedOk && dockOk && themePulse && traceOk && renderOk;

    std::wstring report;
    report += L"Sentinel selftest v2.3.1\n";
    report += FormatW(L"updates: %d (30 min at 30 fps)\n", kUpdates);
    report += FormatW(L"integrity bounds ok: %ls\n", boundsOk ? L"yes" : L"no");
    report += FormatW(L"cascade 2 offline + 3 rebuilding seen: %ls\n", cascadeSeen ? L"yes" : L"no");
    report += FormatW(L"never all six offline: %ls\n", allOffline ? L"NO" : L"yes");
    report += FormatW(L"event log nonempty: %ls\n", logOk ? L"yes" : L"no");
    report += FormatW(L"all 14 phases + wrap: %ls\n", (phasesOk && phaseWrapSeen) ? L"yes" : L"no");
    report += FormatW(L"adjacent incident goals never repeat: %ls\n", adjacentGoalOk ? L"yes" : L"no");
    report += FormatW(L"six-goal rotation covers every class: %ls\n", rotationOk ? L"yes" : L"no");
    report += FormatW(L"boundary stream/event queues bounded: %ls\n", boundaryQueueOk ? L"yes" : L"no");
    report += FormatW(L"boundary target state and angle bounds: %ls\n", boundaryStateOk ? L"yes" : L"no");
    report += FormatW(L"boundary lock/purge/erase/respawn lifecycle: %ls // purges %d\n",
                      boundaryLifecycleOk ? L"yes" : L"no", boundarySim.boundary.purges);
    report += FormatW(L"reduced orbit frozen while data stays live: %ls\n", reducedOrbitOk ? L"yes" : L"no");
    report += FormatW(L"wrong override rejected x3: %ls\n", wrongRejected ? L"yes" : L"no");
    report += FormatW(L"temporary lockout observed: %ls\n", lockoutSeen ? L"yes" : L"no");
    report += FormatW(L"code 1984 terminates: %ls\n", success ? L"yes" : L"no");
    report += FormatW(L"reduced motion bounded: %ls\n", reducedOk ? L"yes" : L"no");
    report += FormatW(L"terminal dock has no log overlap: %ls\n", dockOk ? L"yes" : L"no");
    report += FormatW(L"theme transition pulse: %ls\n", themePulse ? L"yes" : L"no");
    report += FormatW(L"BLACKTRACE deterministic primitives + cycle bounds: %ls\n", traceOk ? L"yes" : L"no");
    report += FormatW(L"all seven layouts render; GDI bounded: %ls // before %lu after %lu\n",
                      renderOk ? L"yes" : L"no", (unsigned long)gdiBefore, (unsigned long)gdiAfter);
    report += std::wstring(pass ? L"result: PASS\n" : L"result: FAIL\n");

    WriteTextFile(GetExeDir() + L"\\selftest.txt", report);
    return pass ? 0 : 1;
}

int App::RunContentCheck() {
    Sim sim;
    sim.Init(cfg_, seed_ ? seed_ : 11u);
    const int kSteps = 360 * 30;
    for (int i = 0; i < kSteps; ++i) sim.Update(1.0f / 30.0f);

    const double total = (double)sim.totalAutoLines;
    const double unique = (double)sim.uniqueAutoLines;
    const double dupRate = total > 0.0 ? (total - unique) / total : 1.0;
    const double uniqueRate = total > 0.0 ? unique / total : 0.0;
    const bool pass = total >= 2000.0 && dupRate <= 0.35;

    std::wstring report;
    report += L"Sentinel content check\n";
    report += FormatW(L"duration: 360 s at 30 fps (%d updates)\n", kSteps);
    report += FormatW(L"generated lines: %llu\n", (unsigned long long)sim.totalAutoLines);
    report += FormatW(L"unique lines: %llu\n", (unsigned long long)sim.uniqueAutoLines);
    report += FormatW(L"exact duplicate rate: %.2f %%\n", dupRate * 100.0);
    report += FormatW(L"unique rate: %.2f %%\n", uniqueRate * 100.0);
    report += FormatW(L"duplicate suffix count: %d\n", sim.duplicateCount);
    report += FormatW(L"log entries retained: %zu\n", sim.log.entries.size());
    report += FormatW(L"recent line window: %zu\n", sim.recentLines.size());
    report += FormatW(L"phase at end: %02d/14\n", sim.opIndex + 1);
    report += std::wstring(pass ? L"result: PASS\n" : L"result: FAIL\n");

    WriteTextFile(GetExeDir() + L"\\contentcheck.txt", report);
    return pass ? 0 : 1;
}

int App::RunMusicCheck() {
    cfg_.music = true;
    AudioInit(false); // Keep the check quiet except for short real playback probes.
    MusicInit(true, cfg_.musicDir);
    sim_.Init(cfg_, 1984u);
    SyncMusicState(false);
    Sleep(120);
    MusicUpdate(0.12f);

    const int count = sim_.musicCount;
    const int firstIndex = sim_.musicIndex;
    const std::wstring firstTrack = sim_.musicTrack;
    const int secondIndex = count > 0 ? firstIndex % count + 1 : 0;
    const int thirdIndex = count > 0 ? secondIndex % count + 1 : 0;
    const int fourthIndex = count > 0 ? thirdIndex % count + 1 : 0;
    bool initialOk = count >= 2 && firstIndex == 1 && !firstTrack.empty();

    HandleMessage(nullptr, WM_KEYDOWN, VK_F8, 0);
    Sleep(80);
    MusicUpdate(0.08f);
    const std::wstring secondTrack = sim_.musicTrack;
    bool f8Ok = sim_.musicIndex == secondIndex && !secondTrack.empty() &&
                secondTrack != firstTrack && sim_.toast.rfind(L"MUSIC //", 0) == 0;

    HandleMessage(nullptr, WM_KEYDOWN, (WPARAM)'M', 0);
    Sleep(80);
    MusicUpdate(0.08f);
    const std::wstring thirdTrack = sim_.musicTrack;
    bool mOk = sim_.musicIndex == thirdIndex && sim_.musicTrack == thirdTrack &&
               sim_.toast.rfind(L"MUSIC //", 0) == 0;

    // Exercise the production coordinate transform and mouse-message path at 1280x720.
    auto clickScreen = std::make_unique<ScreenWindow>();
    clickScreen->primary = true;
    bool clickSurfaceOk = clickScreen->renderer.Init(nullptr, 1280, 720, true);
    screens_.push_back(std::move(clickScreen));
    HandleMessage(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(910, 706));
    Sleep(80);
    MusicUpdate(0.08f);
    const std::wstring fourthTrack = sim_.musicTrack;
    bool clickOk = clickSurfaceOk && sim_.musicIndex == fourthIndex &&
                   sim_.musicTrack == fourthTrack && sim_.toast.rfind(L"MUSIC //", 0) == 0;

    sim_.OpenModal();
    const int modalIndex = sim_.musicIndex;
    const std::wstring modalTrack = sim_.musicTrack;
    HandleMessage(nullptr, WM_KEYDOWN, VK_F8, 0);
    HandleMessage(nullptr, WM_KEYDOWN, (WPARAM)'M', 0);
    HandleMessage(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(910, 706));
    bool modalGuardOk = sim_.musicIndex == modalIndex && sim_.musicTrack == modalTrack;
    sim_.modal.Close();

    sim_.focusBox = 0;
    const int terminalIndex = sim_.musicIndex;
    const std::wstring terminalTrack = sim_.musicTrack;
    HandleMessage(nullptr, WM_KEYDOWN, VK_F8, 0);
    HandleMessage(nullptr, WM_KEYDOWN, (WPARAM)'M', 0);
    HandleMessage(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(910, 706));
    bool terminalGuardOk = sim_.musicIndex == terminalIndex && sim_.musicTrack == terminalTrack;
    sim_.ClearFocus();
    screens_.clear();

    std::wstring report;
    report += L"Sentinel music runtime check v2.3.1\n";
    report += L"backend: MFPlay create/play through production MusicInit\n";
    report += L"input path: production App::HandleMessage WM_KEYDOWN + WM_LBUTTONDOWN\n";
    report += FormatW(L"tracks discovered: %d\n", count);
    report += FormatW(L"initial track: %02d/%02d // %ls\n", firstIndex, count, firstTrack.c_str());
    report += FormatW(L"F8 next track: %ls // %ls\n", f8Ok ? L"yes" : L"no", secondTrack.c_str());
    report += FormatW(L"M next track: %ls // %ls\n", mOk ? L"yes" : L"no", thirdTrack.c_str());
    report += FormatW(L"bottom click next track: %ls // %ls\n", clickOk ? L"yes" : L"no", fourthTrack.c_str());
    report += FormatW(L"F8/M/click ignored by override modal: %ls\n", modalGuardOk ? L"yes" : L"no");
    report += FormatW(L"F8/M/click ignored by focused terminal: %ls\n", terminalGuardOk ? L"yes" : L"no");
    report += FormatW(L"track metadata and toast synchronized: %ls\n",
                      (f8Ok && mOk && clickOk) ? L"yes" : L"no");
    bool pass = initialOk && f8Ok && mOk && clickOk && modalGuardOk && terminalGuardOk;
    report += std::wstring(pass ? L"result: PASS\n" : L"result: FAIL\n");

    MusicShutdown();
    AudioShutdown();
    bool written = WriteTextFile(GetExeDir() + L"\\musiccheck.txt", report);
    return pass && written ? 0 : 1;
}

void App::SyncMusicState(bool announce) {
    const std::wstring& path = MusicCurrent();
    std::wstring name = path;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    sim_.musicTrack = name;
    sim_.musicIndex = MusicTrackIndex();
    sim_.musicCount = MusicTrackCount();

    if (!announce) return;
    if (name.empty()) {
        sim_.toast = cfg_.music ? L"MUSIC // NO TRACKS FOUND" : L"MUSIC // DISABLED";
    } else {
        sim_.toast = FormatW(L"MUSIC // %02d/%02d // %ls", sim_.musicIndex, sim_.musicCount, name.c_str());
        sim_.PushLog(Severity::Info, L"MUSIC", L"track %02d/%02d -- %ls",
                     sim_.musicIndex, sim_.musicCount, name.c_str());
    }
    sim_.toastTimer = 2.4f;
}

void App::NextTrack() {
    // Central guard covers keyboard, footer click and any future invocation path.
    if (sim_.modal.open || sim_.focusBox >= 0) return;
    if (!cfg_.music) {
        sim_.toast = L"MUSIC // DISABLED IN CONFIG";
        sim_.toastTimer = 2.0f;
        AudioPlay(AUDIO_ERROR);
        return;
    }
    if (!MusicNext()) {
        SyncMusicState(false);
        sim_.toast = L"MUSIC // NO PLAYABLE TRACK";
        sim_.toastTimer = 2.0f;
        AudioPlay(AUDIO_ERROR);
        return;
    }
    SyncMusicState(true);
    AudioPlay(AUDIO_BEEP);
}

void App::ResetSystemStats() {
    prevIdle_ = prevKernel_ = prevUser_ = 0;
    prevProcKernel_ = prevProcUser_ = 0;
    statAccum_ = 0.0f;
    statsInit_ = false;
    sim_.sysCpuPct = 0.0f;
    sim_.procCpuPct = 0.0f;
    sim_.memPct = 0.0f;
    sim_.procMemMB = 0.0f;
}

void App::UpdateSystemStats(float dt) {
    statAccum_ += dt;
    if (statAccum_ < 1.0f) return;
    statAccum_ -= 1.0f;

    auto ft64 = [](const FILETIME& ft) -> uint64_t {
        return (uint64_t)ft.dwLowDateTime | ((uint64_t)ft.dwHighDateTime << 32);
    };

    FILETIME idle{}, kernel{}, user{};
    FILETIME procCreate{}, procExit{}, procKernel{}, procUser{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return;
    if (!GetProcessTimes(GetCurrentProcess(), &procCreate, &procExit, &procKernel, &procUser)) return;

    uint64_t ni = ft64(idle), nk = ft64(kernel), nu = ft64(user);
    uint64_t pk = ft64(procKernel), pu = ft64(procUser);

    if (!statsInit_) {
        prevIdle_ = ni;
        prevKernel_ = nk;
        prevUser_ = nu;
        prevProcKernel_ = pk;
        prevProcUser_ = pu;
        statsInit_ = true;
        return;
    }

    uint64_t dIdle = ni - prevIdle_;
    uint64_t dKernel = nk - prevKernel_;
    uint64_t dUser = nu - prevUser_;
    uint64_t dTotal = dKernel + dUser;
    if (dTotal > 0) {
        double busy = (double)dTotal - (double)dIdle;
        sim_.sysCpuPct = clampf((float)(busy * 100.0 / (double)dTotal), 0.0f, 100.0f);
        uint64_t dProc = (pk + pu) - (prevProcKernel_ + prevProcUser_);
        sim_.procCpuPct = clampf((float)((double)dProc * 100.0 / (double)dTotal), 0.0f, 100.0f);
    }

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        sim_.memPct = (float)ms.dwMemoryLoad;
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        sim_.procMemMB = (float)((double)pmc.WorkingSetSize / (1024.0 * 1024.0));
    }

    prevIdle_ = ni;
    prevKernel_ = nk;
    prevUser_ = nu;
    prevProcKernel_ = pk;
    prevProcUser_ = pu;
}

void App::BuildDebugLines() {
    sim_.debugLines.clear();
    wchar_t b[192];
    swprintf_s(b, L"BEAT %d  CYCLE %d", (int)sim_.beat, sim_.cycle);
    sim_.debugLines.push_back(b);
    swprintf_s(b, L"OFF %d  REB %d  AVG %.0f",
               sim_.CountState(FWState::Offline),
               sim_.CountState(FWState::Rebuilding),
               sim_.AvgIntegrity());
    sim_.debugLines.push_back(b);
    swprintf_s(b, L"RAIN %d  LOG %d", (int)sim_.rain.cols.size(), (int)sim_.log.entries.size());
    sim_.debugLines.push_back(b);
    swprintf_s(b, L"FRAME %.1f ms  FPS %d", lastFrameMs_, cfg_.fps);
    sim_.debugLines.push_back(b);
    swprintf_s(b, L"MODE %ls  SEED %u", windowed_ ? L"WINDOWED" : L"FULLSCREEN", seed_);
    sim_.debugLines.push_back(b);
}

double App::Now() const {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    if (f.QuadPart == 0) return 0.0;
    return (double)c.QuadPart / (double)f.QuadPart;
}
