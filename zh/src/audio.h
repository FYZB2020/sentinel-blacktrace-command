#pragma once
#include <windows.h>

enum AudioCue {
    AUDIO_TICK = 0,
    AUDIO_BEEP,
    AUDIO_ALARM,
    AUDIO_ERROR,
    AUDIO_SUCCESS,
    AUDIO_TYPE,
    AUDIO_OPEN,
    AUDIO_COUNT
};

// Generates small in-memory PCM WAVs once and plays them asynchronously.
bool AudioInit(bool enabled);
void AudioPlay(int cue);
void AudioShutdown();
