#include "audio.h"
#include <mmsystem.h>
#include <vector>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

namespace {
std::vector<BYTE> g_wav[AUDIO_COUNT];
bool g_enabled = false;

void Put32(std::vector<BYTE>& v, DWORD x) {
    v.push_back((BYTE)(x & 0xFF));
    v.push_back((BYTE)((x >> 8) & 0xFF));
    v.push_back((BYTE)((x >> 16) & 0xFF));
    v.push_back((BYTE)((x >> 24) & 0xFF));
}
void Put16(std::vector<BYTE>& v, WORD x) {
    v.push_back((BYTE)(x & 0xFF));
    v.push_back((BYTE)((x >> 8) & 0xFF));
}

void MakeWav(int index, const std::vector<float>& freqs, const std::vector<float>& durs,
             float volume, float noise) {
    const int rate = 22050;
    std::vector<BYTE> pcm;
    for (size_t seg = 0; seg < freqs.size(); ++seg) {
        int samples = (int)(rate * durs[seg]);
        if (samples < 1) samples = 1;
        for (int i = 0; i < samples; ++i) {
            float t = (float)i / rate;
            float env = 1.0f;
            float attack = 0.008f;
            if (t < attack) env = t / attack;
            float remain = durs[seg] - t;
            if (remain < 0.03f) env *= (std::max)(0.0f, remain / 0.03f);
            float s = std::sin(6.2831853f * freqs[seg] * t);
            s += 0.35f * std::sin(6.2831853f * freqs[seg] * 2.0f * t);
            s *= 0.15f + 0.85f * env;
            if (noise > 0.0f) {
                float n = (float)((rand() % 2001) - 1000) / 1000.0f;
                s = s * (1.0f - noise) + n * noise;
            }
            int v = (int)(128.0f + s * volume * 120.0f);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            pcm.push_back((BYTE)v);
        }
    }

    std::vector<BYTE>& w = g_wav[index];
    w.clear();
    w.reserve(44 + pcm.size());
    w.insert(w.end(), {'R','I','F','F'});
    Put32(w, (DWORD)(36 + pcm.size()));
    w.insert(w.end(), {'W','A','V','E','f','m','t',' '});
    Put32(w, 16);
    Put16(w, 1);              // PCM
    Put16(w, 1);              // mono
    Put32(w, rate);
    Put32(w, rate);           // 8-bit mono
    Put16(w, 1);
    Put16(w, 8);
    w.insert(w.end(), {'d','a','t','a'});
    Put32(w, (DWORD)pcm.size());
    w.insert(w.end(), pcm.begin(), pcm.end());
}
}

bool AudioInit(bool enabled) {
    g_enabled = enabled;
    if (!enabled) return false;

    MakeWav(AUDIO_TICK, { 1320.0f }, { 0.012f }, 0.18f, 0.15f);
    MakeWav(AUDIO_BEEP, { 880.0f }, { 0.075f }, 0.22f, 0.0f);
    MakeWav(AUDIO_ALARM, { 620.0f, 880.0f, 620.0f }, { 0.10f, 0.09f, 0.10f }, 0.25f, 0.05f);
    MakeWav(AUDIO_ERROR, { 180.0f, 140.0f }, { 0.14f, 0.18f }, 0.25f, 0.10f);
    MakeWav(AUDIO_SUCCESS, { 660.0f, 880.0f, 1100.0f }, { 0.09f, 0.09f, 0.14f }, 0.22f, 0.0f);
    MakeWav(AUDIO_TYPE, { 1500.0f }, { 0.010f }, 0.12f, 0.30f);
    MakeWav(AUDIO_OPEN, { 440.0f, 660.0f }, { 0.10f, 0.14f }, 0.22f, 0.0f);
    return true;
}

void AudioPlay(int cue) {
    if (!g_enabled || cue < 0 || cue >= AUDIO_COUNT) return;
    if (g_wav[cue].empty()) return;
    PlaySoundA((LPCSTR)g_wav[cue].data(), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP);
}

void AudioShutdown() {
    if (g_enabled) PlaySoundA(nullptr, nullptr, 0);
    g_enabled = false;
    for (auto& w : g_wav) w.clear();
}
