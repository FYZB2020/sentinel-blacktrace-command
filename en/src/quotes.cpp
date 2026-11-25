#include "quotes.h"
#include "common.h"
#include <windows.h>
#include <vector>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace {

typedef struct ZStream {
    const unsigned char* next_in;
    unsigned int avail_in;
    unsigned long total_in;
    unsigned char* next_out;
    unsigned int avail_out;
    unsigned long total_out;
    const char* msg;
    void* state;
    void* zalloc;
    void* zfree;
    void* opaque;
    int data_type;
    unsigned long adler;
    unsigned long reserved;
} ZStream;

typedef int(__cdecl* InflateInit2Fn)(ZStream*, int, const char*, int);
typedef int(__cdecl* InflateFn)(ZStream*, int);
typedef int(__cdecl* InflateEndFn)(ZStream*);
typedef const char*(__cdecl* ZlibVersionFn)();

struct ZlibApi {
    HMODULE module = nullptr;
    InflateInit2Fn init2 = nullptr;
    InflateFn inflate = nullptr;
    InflateEndFn end = nullptr;
    ZlibVersionFn version = nullptr;
    bool ok() const { return module && init2 && inflate && end && version; }
};

ZlibApi& Zlib() {
    static ZlibApi api;
    static bool tried = false;
    if (tried) return api;
    tried = true;

    api.module = LoadLibraryW(L"zlib1.dll");
    if (!api.module) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (slash) {
            *(slash + 1) = 0;
            std::wstring p = std::wstring(exe) + L"zlib1.dll";
            api.module = LoadLibraryW(p.c_str());
        }
    }
    if (!api.module) return api;

    api.init2 = (InflateInit2Fn)GetProcAddress(api.module, "inflateInit2_");
    api.inflate = (InflateFn)GetProcAddress(api.module, "inflate");
    api.end = (InflateEndFn)GetProcAddress(api.module, "inflateEnd");
    api.version = (ZlibVersionFn)GetProcAddress(api.module, "zlibVersion");
    return api;
}

bool ReadAll(const std::wstring& path, std::vector<BYTE>& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    out.resize(got);
    return got == (size_t)n;
}

WORD U16(const BYTE* p) { return (WORD)(p[0] | (p[1] << 8)); }
DWORD U32(const BYTE* p) { return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24); }

struct ZipEntry {
    bool found = false;
    WORD method = 0;
    DWORD compressed = 0;
    DWORD uncompressed = 0;
    size_t dataOffset = 0;
};

ZipEntry FindZipEntry(const std::vector<BYTE>& zip, const char* wanted) {
    ZipEntry result;
    if (zip.size() < 30) return result;
    const size_t n = zip.size();
    for (size_t off = 0; off + 30 <= n; ++off) {
        if (U32(&zip[off]) != 0x04034b50) continue;
        WORD method = U16(&zip[off + 8]);
        DWORD comp = U32(&zip[off + 18]);
        DWORD uncomp = U32(&zip[off + 22]);
        WORD nameLen = U16(&zip[off + 26]);
        WORD extraLen = U16(&zip[off + 28]);
        if (off + 30 + nameLen > n) continue;
        const char* name = (const char*)&zip[off + 30];
        if (nameLen == strlen(wanted) && memcmp(name, wanted, nameLen) == 0) {
            result.found = true;
            result.method = method;
            result.compressed = comp;
            result.uncompressed = uncomp;
            result.dataOffset = off + 30 + nameLen + extraLen;
            return result;
        }
    }
    return result;
}

bool InflateRaw(const std::vector<BYTE>& zip, const ZipEntry& entry, std::vector<BYTE>& out) {
    if (!entry.found) return false;
    if (entry.dataOffset + entry.compressed > zip.size()) return false;
    if (entry.method == 0) {
        out.assign(zip.begin() + entry.dataOffset,
                   zip.begin() + entry.dataOffset + entry.compressed);
        return !out.empty();
    }
    if (entry.method != 8) return false;
    ZlibApi& z = Zlib();
    if (!z.ok()) return false;

    size_t expected = entry.uncompressed ? entry.uncompressed : (1u << 20);
    out.resize(expected);
    ZStream s{};
    int rc = z.init2(&s, -15, z.version(), (int)sizeof(ZStream));
    if (rc != 0) return false;
    s.next_in = &zip[entry.dataOffset];
    s.avail_in = entry.compressed;
    s.next_out = out.data();
    s.avail_out = (unsigned int)out.size();
    rc = z.inflate(&s, 4); // Z_FINISH
    bool ok = (rc == 1 || rc == 0) && s.total_out > 0;
    out.resize(ok ? (size_t)s.total_out : 0);
    z.end(&s);
    return ok;
}

bool HasCjk(const std::wstring& s) {
    for (wchar_t c : s) if (c >= 0x4E00 && c <= 0x9FFF) return true;
    return false;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::wstring DecodeXml(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'&') { out.push_back(s[i]); continue; }
        size_t semi = s.find(L';', i + 1);
        if (semi == std::wstring::npos || semi - i > 10) { out.push_back(L'&'); continue; }
        std::wstring ent = s.substr(i + 1, semi - i - 1);
        if (ent == L"amp") out.push_back(L'&');
        else if (ent == L"lt") out.push_back(L'<');
        else if (ent == L"gt") out.push_back(L'>');
        else if (ent == L"quot") out.push_back(L'"');
        else if (ent == L"apos") out.push_back(L'\'');
        else if (ent.size() > 1 && ent[0] == L'#') {
            wchar_t* end = nullptr;
            long v = (ent[1] == L'x' || ent[1] == L'X')
                ? wcstol(ent.c_str() + 2, &end, 16)
                : wcstol(ent.c_str() + 1, &end, 10);
            if (v > 0 && v < 0x110000) out.push_back((wchar_t)v);
        } else {
            out.push_back(L'&');
            out += ent;
            out.push_back(L';');
        }
        i = semi;
    }
    return out;
}

void AddSentence(const std::wstring& raw, std::vector<std::wstring>& out,
                 std::set<std::wstring>& seen) {
    std::wstring s = Trim(DecodeXml(raw));
    if (s.size() < 4 || s.size() > 180) return;
    if (!HasCjk(s)) return;
    if (!seen.insert(s).second) return;
    out.push_back(s);
}

bool LoadXlsx(const std::wstring& path, std::vector<std::wstring>& out) {
    std::vector<BYTE> zip;
    if (!ReadAll(path, zip)) return false;
    ZipEntry e = FindZipEntry(zip, "xl/sharedStrings.xml");
    if (!e.found) e = FindZipEntry(zip, "xl/worksheets/sheet1.xml");
    if (!e.found) return false;
    std::vector<BYTE> xmlBytes;
    if (!InflateRaw(zip, e, xmlBytes) || xmlBytes.empty()) return false;
    int len = (int)xmlBytes.size();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)xmlBytes.data(), len, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring xml((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, (const char*)xmlBytes.data(), len, &xml[0], wlen);

    std::set<std::wstring> seen;
    size_t pos = 0;
    while (true) {
        size_t lt = xml.find(L"<t", pos);
        if (lt == std::wstring::npos) break;
        wchar_t next = (lt + 2 < xml.size()) ? xml[lt + 2] : 0;
        if (next != L'>' && next != L' ' && next != L'\t' && next != L'\r' && next != L'\n') {
            pos = lt + 2;
            continue;
        }
        size_t gt = xml.find(L'>', lt);
        if (gt == std::wstring::npos) break;
        size_t end = xml.find(L"</t>", gt + 1);
        if (end == std::wstring::npos) break;
        AddSentence(xml.substr(gt + 1, end - gt - 1), out, seen);
        pos = end + 4;
        if (out.size() >= 600) break;
    }
    return !out.empty();
}

bool LoadText(const std::wstring& path, std::vector<std::wstring>& out) {
    std::vector<BYTE> bytes;
    if (!ReadAll(path, bytes)) return false;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        bytes.erase(bytes.begin(), bytes.begin() + 3);
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring text((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, (const char*)bytes.data(), (int)bytes.size(), &text[0], wlen);

    std::set<std::wstring> seen;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(L'\n', start);
        std::wstring line = text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
        AddSentence(line, out, seen);
        if (nl == std::wstring::npos) break;
        start = nl + 1;
        if (out.size() >= 600) break;
    }
    return !out.empty();
}
}

bool LoadQuoteFile(const std::wstring& path, std::vector<std::wstring>& out) {
    out.clear();
    if (path.empty()) return false;
    std::wstring ext;
    size_t dot = path.rfind(L'.');
    if (dot != std::wstring::npos) ext = path.substr(dot);
    for (wchar_t& c : ext) c = (wchar_t)towlower(c);
    bool ok = false;
    if (ext == L".xlsx" || ext == L".xlsm") ok = LoadXlsx(path, out);
    if (!ok && ext == L".txt") ok = LoadText(path, out);
    if (!ok) {
        // Fallback: allow a .txt beside an unreadable xlsx.
        size_t d = path.rfind(L'.');
        if (d != std::wstring::npos) {
            std::wstring txt = path.substr(0, d) + L".txt";
            ok = LoadText(txt, out);
        }
    }
    return ok;
}
