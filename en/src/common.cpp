#include "common.h"

#include <algorithm>
#include <cwchar>

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return L".";
    std::wstring s(buf, n);
    size_t p = s.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L".";
    return s.substr(0, p);
}

std::wstring GetConfigPath() {
    std::wstring local = GetExeDir() + L"\\config.ini";
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return local;

    wchar_t appdata[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(appdata, n) : GetExeDir();
    dir += L"\\Sentinel";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

static std::wstring ReadStr(const std::wstring& path, const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512]{};
    GetPrivateProfileStringW(sec, key, def, buf, 512, path.c_str());
    return std::wstring(buf);
}

// INI files are commonly saved as UTF-8 by modern editors. GetPrivateProfileStringW
// treats such a file as ANSI, which mangles non-ASCII paths (e.g. QuotesFile).
// Re-read just that value as UTF-8 when needed.
static std::wstring ReadUtf8IniValue(const std::wstring& path, const char* section, const char* key) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 1024 * 1024) {
        CloseHandle(h);
        return L"";
    }
    std::string data(size, '\0');
    DWORD got = 0;
    BOOL ok = ReadFile(h, &data[0], size, &got, nullptr);
    CloseHandle(h);
    if (!ok) return L"";
    data.resize(got);
    if (data.size() >= 2 && ((unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE)) return L"";
    if (data.size() >= 2 && ((unsigned char)data[0] == 0xFE && (unsigned char)data[1] == 0xFF)) return L"";
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        data.erase(0, 3);
    }
    std::string sec = std::string("[") + section + "]";
    size_t sp = data.find(sec);
    if (sp == std::string::npos) return L"";
    size_t line = data.find('\n', sp);
    if (line == std::string::npos) return L"";
    std::string keyline = std::string(key) + "=";
    while (line < data.size()) {
        size_t start = line + 1;
        size_t end = data.find_first_of("\r\n", start);
        if (end == std::string::npos) end = data.size();
        std::string row = data.substr(start, end - start);
        if (row.rfind(keyline, 0) == 0) {
            std::string val = row.substr(keyline.size());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
            if (val.empty()) return L"";
            int n = MultiByteToWideChar(CP_UTF8, 0, val.data(), (int)val.size(), nullptr, 0);
            if (n <= 0) return L"";
            std::wstring out((size_t)n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, val.data(), (int)val.size(), &out[0], n);
            return out;
        }
        if (row.rfind("[", 0) == 0) break;
        line = end;
    }
    return L"";
}

static int ReadInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int def) {
    std::wstring v = ReadStr(path, sec, key, L"");
    if (v.empty()) return def;
    return (int)wcstol(v.c_str(), nullptr, 10);
}

static bool ReadBool(const std::wstring& path, const wchar_t* sec, const wchar_t* key, bool def) {
    std::wstring v = ReadStr(path, sec, key, def ? L"1" : L"0");
    return v == L"1" || v == L"true" || v == L"yes" || v == L"on";
}

void WriteDefaultConfig() {
    std::wstring path = GetConfigPath();
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    WritePrivateProfileStringW(L"general", L"Code", L"1984", path.c_str());
    WritePrivateProfileStringW(L"general", L"Fps", L"30", path.c_str());
    WritePrivateProfileStringW(L"general", L"Quality", L"auto", path.c_str());
    WritePrivateProfileStringW(L"general", L"MonitorMode", L"all", path.c_str());
    WritePrivateProfileStringW(L"general", L"AutoPromptSeconds", L"0", path.c_str());
    WritePrivateProfileStringW(L"general", L"IdleMinutes", L"0", path.c_str());
    WritePrivateProfileStringW(L"general", L"ShowSimulationTag", L"0", path.c_str());
    WritePrivateProfileStringW(L"general", L"KeepDisplayOn", L"0", path.c_str());
    WritePrivateProfileStringW(L"general", L"Language", L"bi", path.c_str());
    WritePrivateProfileStringW(L"general", L"Speed", L"1.6", path.c_str());
    WritePrivateProfileStringW(L"general", L"Theme", L"auto", path.c_str());
    WritePrivateProfileStringW(L"effects", L"Flash", L"med", path.c_str());
    WritePrivateProfileStringW(L"effects", L"ErrorText", L"1", path.c_str());
    WritePrivateProfileStringW(L"effects", L"Shake", L"0", path.c_str());
    WritePrivateProfileStringW(L"effects", L"Dust", L"med", path.c_str());
    WritePrivateProfileStringW(L"effects", L"Corruption", L"med", path.c_str());
    WritePrivateProfileStringW(L"effects", L"Motion", L"full", path.c_str());
    WritePrivateProfileStringW(L"effects", L"Sound", L"on", path.c_str());
    WritePrivateProfileStringW(L"quotes", L"Quotes", L"on", path.c_str());
    WritePrivateProfileStringW(L"quotes", L"QuotesFile", L"quotes-en.txt", path.c_str());
    WritePrivateProfileStringW(L"music", L"Music", L"on", path.c_str());
    WritePrivateProfileStringW(L"music", L"MusicDir", L"music", path.c_str());
}

Config LoadConfig() {
    Config c;
    std::wstring path = GetConfigPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteDefaultConfig();
    }

    c.code = ReadStr(path, L"general", L"Code", L"1984");
    bool codeOk = c.code.size() >= 4 && c.code.size() <= 32;
    if (codeOk) {
        for (wchar_t ch : c.code) {
            if (ch < 0x21 || ch > 0x7E) { codeOk = false; break; }
        }
    }
    if (!codeOk) c.code = L"1984";
    c.theme = ReadStr(path, L"general", L"Theme", L"auto");
    if (c.theme != L"auto" && c.theme != L"terminal" && c.theme != L"split" &&
        c.theme != L"warroom" && c.theme != L"forensic" && c.theme != L"containment" &&
        c.theme != L"blackbox") {
        c.theme = L"auto";
    }
    c.dust = ReadStr(path, L"effects", L"Dust", L"med");
    if (c.dust != L"off" && c.dust != L"low" && c.dust != L"med" && c.dust != L"high") c.dust = L"med";
    c.corruption = ReadStr(path, L"effects", L"Corruption", L"med");
    if (c.corruption != L"low" && c.corruption != L"med" && c.corruption != L"high") c.corruption = L"med";
    c.motion = ReadStr(path, L"effects", L"Motion", L"full");
    if (c.motion != L"full" && c.motion != L"reduced") c.motion = L"full";
    c.errorText = ReadBool(path, L"effects", L"ErrorText", true);
    c.shake = false; // v1.6: always disabled
    c.fps = (std::max)(10, (std::min)(60, ReadInt(path, L"general", L"Fps", 30)));
    c.quality = ReadStr(path, L"general", L"Quality", L"auto");
    c.monitorMode = ReadStr(path, L"general", L"MonitorMode", L"all");
    c.autoPromptSeconds = (std::max)(0, ReadInt(path, L"general", L"AutoPromptSeconds", 0));
    c.idleMinutes = (std::max)(0, (std::min)(1440, ReadInt(path, L"general", L"IdleMinutes", 0)));
    c.showSimulationTag = ReadBool(path, L"general", L"ShowSimulationTag", false);
    c.keepDisplayOn = ReadBool(path, L"general", L"KeepDisplayOn", false);
    c.language = ReadStr(path, L"general", L"Language", L"bi");
    double speed = wcstod(ReadStr(path, L"general", L"Speed", L"1.6").c_str(), nullptr);
    c.speed = clampf((float)speed, 0.5f, 3.0f);
    c.flash = ReadStr(path, L"effects", L"Flash", L"med");
    if (c.flash != L"low" && c.flash != L"med" && c.flash != L"high") c.flash = L"med";
    c.shake = false; // v1.6: always disabled
    c.glitch = ReadBool(path, L"effects", L"Glitch", true);
    c.sound = ReadBool(path, L"effects", L"Sound", true);
    c.quotes = ReadBool(path, L"quotes", L"Quotes", true);
    c.quotesFile = ReadStr(path, L"quotes", L"QuotesFile", L"quotes-en.txt");
    if (GetFileAttributesW(c.quotesFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring utf8 = ReadUtf8IniValue(path, "quotes", "QuotesFile");
        if (!utf8.empty() && GetFileAttributesW(utf8.c_str()) != INVALID_FILE_ATTRIBUTES) {
            c.quotesFile = utf8;
        }
    }
    if (!c.quotesFile.empty() && c.quotesFile.size() > 1 && c.quotesFile[1] != L':' &&
        c.quotesFile.rfind(L"\\\\", 0) != 0) {
        c.quotesFile = GetExeDir() + L"\\" + c.quotesFile;
    }
    if (c.quotesFile.empty()) c.quotesFile = L"quotes-en.txt";
    c.music = ReadBool(path, L"music", L"Music", true);
    c.musicDir = ReadStr(path, L"music", L"MusicDir", L"music");
    if (c.musicDir.empty()) c.musicDir = L"music";
    double density = wcstod(ReadStr(path, L"effects", L"RainDensity", L"0.6").c_str(), nullptr);
    c.rainDensity = clampf((float)density, 0.1f, 1.0f);
    return c;
}

void OpenConfigInNotepad() {
    WriteDefaultConfig();
    std::wstring path = GetConfigPath();
    ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
}
