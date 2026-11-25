#include "music.h"
#include "common.h"

#include <windows.h>
#include <mfplay.h>
#include <mfapi.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")

namespace {

std::vector<std::wstring> g_tracks;
size_t g_index = 0;
bool g_enabled = false;
bool g_playing = false;
bool g_comInit = false;
IMFPMediaPlayer* g_player = nullptr;
std::wstring g_current;

void CloseTrack() {
    if (g_player) {
        g_player->Stop();
        g_player->Shutdown();
        g_player->Release();
        g_player = nullptr;
    }
    g_playing = false;
}

bool PlayIndex(size_t idx) {
    if (g_tracks.empty()) return false;
    CloseTrack();
    g_index = idx % g_tracks.size();
    const std::wstring& path = g_tracks[g_index];
    HRESULT hr = MFPCreateMediaPlayer(path.c_str(), TRUE, MFP_OPTION_NONE,
                                      nullptr, nullptr, &g_player);
    if (FAILED(hr) || !g_player) {
        g_player = nullptr;
        g_playing = false;
        g_current.clear();
        return false;
    }
    g_player->SetVolume(0.32f);
    g_player->SetMute(FALSE);
    hr = g_player->Play();
    g_playing = false;
    g_current = path;
    if (FAILED(hr)) {
        CloseTrack();
        g_current.clear();
        return false;
    }
    return true;
}

void Scan(const std::wstring& root) {
    g_tracks.clear();
    if (root.empty()) return;
    const wchar_t* patterns[] = { L"\\*.mp3", L"\\*.wav", L"\\*.m4a" };
    for (const wchar_t* pat : patterns) {
        std::wstring spec = root + pat;
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(spec.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            g_tracks.push_back(root + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(g_tracks.begin(), g_tracks.end());
}

} // namespace

void MusicInit(bool enabled, const std::wstring& dir) {
    MusicShutdown();
    g_enabled = enabled;
    if (!g_enabled) return;

    if (!g_comInit) {
        g_comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        if (!g_comInit) { g_enabled = false; return; }
    }
    if (FAILED(MFStartup(MF_VERSION))) {
        CoUninitialize();
        g_comInit = false;
        g_enabled = false;
        return;
    }

    std::wstring root = dir.empty() ? L"music" : dir;
    if (root.size() > 1 && root[1] != L':' && root.rfind(L"\\\\", 0) != 0) {
        root = GetExeDir() + L"\\" + root;
    }
    Scan(root);
    if (!g_tracks.empty()) PlayIndex(0);
}

void MusicUpdate(float dt) {
    (void)dt;
    if (!g_enabled || !g_player) return;
    MFP_MEDIAPLAYER_STATE st = MFP_MEDIAPLAYER_STATE_EMPTY;
    if (SUCCEEDED(g_player->GetState(&st))) {
        if (st == MFP_MEDIAPLAYER_STATE_PLAYING) {
            g_playing = true;
        } else if (st == MFP_MEDIAPLAYER_STATE_STOPPED && g_playing) {
            PlayIndex(g_index + 1);
        }
    }
}

bool MusicNext() {
    if (!g_enabled || g_tracks.empty()) return false;
    return PlayIndex(g_index + 1);
}

void MusicShutdown() {
    CloseTrack();
    if (g_comInit) {
        MFShutdown();
        CoUninitialize();
        g_comInit = false;
    }
    g_tracks.clear();
    g_current.clear();
    g_index = 0;
    g_enabled = false;
}

const std::wstring& MusicCurrent() {
    return g_current;
}

int MusicTrackIndex() {
    return g_current.empty() || g_tracks.empty() ? 0 : (int)g_index + 1;
}

int MusicTrackCount() {
    return (int)g_tracks.size();
}
