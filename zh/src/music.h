#pragma once

#include <string>

void MusicInit(bool enabled, const std::wstring& dir);
void MusicUpdate(float dt);
bool MusicNext();
void MusicShutdown();
const std::wstring& MusicCurrent();
int MusicTrackIndex(); // one-based; zero when no track is loaded
int MusicTrackCount();
