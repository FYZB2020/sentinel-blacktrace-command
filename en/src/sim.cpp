#include "sim.h"
#include "quotes.h"
#include <cstdlib>

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
static float frand01(std::mt19937& r) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(r);
}
static float frand(std::mt19937& r, float a, float b) {
    return std::uniform_real_distribution<float>(a, b)(r);
}
static int irand(std::mt19937& r, int a, int b) {
    if (b <= a) return a;
    return std::uniform_int_distribution<int>(a, b)(r);
}
static std::wstring NowTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[16];
    swprintf_s(b, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return b;
}

static const wchar_t* kFWNames[6] = {
    L"PERIMETER", L"IDS / IPS", L"KERNEL", L"EGRESS", L"DATA VAULT", L"RECOVERY"
};

static const ActDef kActDefs[3] = {
    { L"ACT I",   L"RECON & INITIAL ACCESS", Theme::Terminal,    Theme::SplitOps,   RGB(53, 224, 255) },
    { L"ACT II",  L"BOUNDARY ENGAGEMENT & EXFIL", Theme::Boundary,    Theme::WarRoom,    RGB(255, 176, 32) },
    { L"ACT III", L"COUNTERMEASURE & RECOVERY",     Theme::Containment, Theme::BlackBox,   RGB(255, 45, 58) }
};

static Act ActForPhase(int index) {
    if (index < 4) return Act::Intrusion;
    if (index < 10) return Act::Escalation;
    return Act::Countermeasure;
}

static Theme ThemeForPhase(int index) {
    static const Theme themes[14] = {
        Theme::Terminal, Theme::Terminal, Theme::SplitOps, Theme::SplitOps,
        Theme::Boundary, Theme::Boundary, Theme::WarRoom, Theme::WarRoom,
        Theme::Forensic, Theme::Forensic, Theme::Containment, Theme::Containment,
        Theme::BlackBox, Theme::BlackBox
    };
    return themes[(std::max)(0, (std::min)(13, index))];
}

// ---------------------------------------------------------------------------
// event log
// ---------------------------------------------------------------------------
void EventLog::Push(Severity sev, const std::wstring& module, const std::wstring& text,
                    LogStream stream, bool ghost) {
    LogEntry e;
    e.time = NowTime();
    e.module = module;
    e.text = text;
    e.sev = sev;
    e.stream = stream;
    e.ghost = ghost;
    e.age = 0.0f;
    static int corruptCounter = 0;
    e.corrupt = !ghost && ((++corruptCounter % 29) == 0);
    e.corruptTimer = e.corrupt ? 0.35f : 0.0f;
    entries.push_back(std::move(e));
    while (entries.size() > 512) entries.pop_front();
}

void EventLog::Update(float dt) {
    for (auto& e : entries) {
        e.age += dt;
        if (e.corruptTimer > 0.0f) {
            e.corruptTimer -= dt;
            if (e.corruptTimer <= 0.0f) e.corrupt = false;
        }
    }
    while (!entries.empty() && entries.front().ghost && entries.front().age > 7.0f) {
        entries.pop_front();
    }
}

// ---------------------------------------------------------------------------
// firewall
// ---------------------------------------------------------------------------
void Firewall::SetOffline() {
    integrity = 0.0f;
    display = 0.0f;
    state = FWState::Offline;
    stateTimer = 0.0f;
    attackRate = 0.0f;
    attackTimer = 0.0f;
    repairRate = 0.0f;
    breachStart = fmodf((float)id * 1.7f + 0.4f, 6.2831853f);
    breachWidth = 1.2f;
}

void Firewall::SetRebuilding(float startIntegrity, float rate) {
    integrity = clampf(startIntegrity, 0.0f, 100.0f);
    display = integrity;
    state = FWState::Rebuilding;
    stateTimer = 0.0f;
    attackRate = 0.0f;
    attackTimer = 0.0f;
    repairRate = rate;
    breachStart = fmodf((float)id * 1.7f + 0.4f, 6.2831853f);
    breachWidth = (1.0f - integrity / 100.0f) * 1.5f;
}

void Firewall::SetSecure(float value) {
    integrity = clampf(value, 0.0f, 100.0f);
    display = integrity;
    state = FWState::Secure;
    stateTimer = 0.0f;
    attackRate = 0.0f;
    attackTimer = 0.0f;
    repairRate = 0.0f;
    breachWidth = 0.0f;
}

void Firewall::Update(float dt, Sim& sim) {
    stateTimer += dt;
    jitterPhase += dt * 2.1f;
    flash = (std::max)(0.0f, flash - dt * 2.0f);
    display += (integrity - display) * (std::min)(1.0f, dt * 8.0f);

    if (attackTimer > 0.0f) {
        attackTimer -= dt;
        if (attackTimer <= 0.0f) {
            attackTimer = 0.0f;
            attackRate = 0.0f;
        }
    }

    if (state == FWState::Offline) {
        breachWidth = 1.2f;
        if (stateTimer > 8.0f) {
            state = FWState::Rebuilding;
            stateTimer = 0.0f;
            repairRate = (std::max)(repairRate, 1.2f);
            sim.PushLog(Severity::Warn, name.c_str(), L"layer offline -- rebuild scheduled");
        }
        return;
    }

    if (state == FWState::Rebuilding) {
        integrity += repairRate * dt;
        breachWidth = (1.0f - integrity / 100.0f) * 1.5f;
        if (integrity >= 100.0f) {
            integrity = 100.0f;
            state = FWState::Secure;
            stateTimer = 0.0f;
            repairRate = 0.0f;
            breachWidth = 0.0f;
            sim.PushLog(Severity::Info, name.c_str(), L"layer fully restored -- integrity 100%%");
        }
        return;
    }

    if (attackRate > 0.0f) {
        integrity -= attackRate * dt;
        flash = (std::min)(1.0f, flash + dt * 5.0f);
        if (integrity <= 0.0f) {
            SetOffline();
            sim.PushLog(Severity::Crit, name.c_str(), L"INTEGRITY 0%% -- LAYER OFFLINE");
            sim.audioCue = 3;
            sim.fx.TriggerShake(5.0f);
            sim.fx.TriggerFlash(0.45f);
            return;
        }
    } else {
        // passive slow repair while not under attack
        if (integrity < 100.0f) {
            integrity = (std::min)(100.0f, integrity + 0.8f * dt);
        }
    }

    FWState ns = integrity > 80.0f ? FWState::Secure
               : integrity >= 40.0f ? FWState::Degraded
               : FWState::Critical;
    if (ns != state) {
        if (ns == FWState::Critical) {
            sim.PushLog(Severity::Warn, name.c_str(), L"integrity %d%% -- CRITICAL", (int)integrity);
        } else if (ns == FWState::Degraded) {
            sim.PushLog(Severity::Warn, name.c_str(), L"integrity %d%% -- DEGRADED", (int)integrity);
        } else {
            sim.PushLog(Severity::Info, name.c_str(), L"integrity %d%% -- SECURE", (int)integrity);
        }
        state = ns;
    }

    breachWidth = (integrity < 60.0f) ? (1.0f - integrity / 100.0f) * 0.8f : 0.0f;
}

// ---------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------
void FileMonitor::Init() {
    const wchar_t* paths[6] = {
        L"C:\\Windows\\System32\\config\\SAM",
        L"C:\\Windows\\System32\\drivers\\disk.sys",
        L"D:\\Backup\\2026\\family_photos.7z",
        L"\\\\FS01\\legal\\contracts\\2026\\NDA.pdf",
        L"C:\\Users\\Public\\Documents\\Q4_finance.xlsx",
        L"C:\\ProgramData\\Sentinel\\policy.db"
    };
    const wchar_t* sizes[6] = {
        L"48 KB", L"1.2 MB", L"7.8 GB", L"2.4 MB", L"812 KB", L"196 KB"
    };
    for (int i = 0; i < 6; i++) {
        files[i].path = paths[i];
        files[i].size = sizes[i];
        wchar_t h[17];
        for (int k = 0; k < 16; k++) {
            h[k] = L"0123456789ABCDEF"[(i * 7 + k * 13 + 3) % 16];
        }
        h[16] = 0;
        files[i].hash = h;
        files[i].status = FileStatus::Ok;
        files[i].progress = 0.0f;
        files[i].timer = 0.0f;
    }
}

void FileMonitor::Update(float dt, Sim&) {
    for (auto& f : files) {
        if (f.status == FileStatus::Deleted) {
            f.progress = (std::min)(100.0f, f.progress + dt * 25.0f);
        } else if (f.status == FileStatus::Restored) {
            f.progress = (std::min)(100.0f, f.progress + dt * 32.0f);
            if (f.progress >= 100.0f) {
                f.status = FileStatus::Ok;
                f.progress = 0.0f;
            }
        } else if (f.status == FileStatus::Modified) {
            f.timer += dt;
            if (f.timer > 6.0f) {
                f.status = FileStatus::Ok;
                f.timer = 0.0f;
            }
        }
    }
}

int FileMonitor::DeleteRandom(Sim& sim) {
    std::vector<int> candidates;
    for (int i = 0; i < 6; i++) {
        if (files[i].status == FileStatus::Ok ||
            files[i].status == FileStatus::Modified ||
            files[i].status == FileStatus::Restored) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) return -1;
    int idx = candidates[irand(sim.rng, 0, (int)candidates.size() - 1)];
    files[idx].status = FileStatus::Deleted;
    files[idx].progress = 0.0f;
    files[idx].timer = 0.0f;
    sim.PushLog(Severity::Crit, L"FS", L"file deleted: %ls", files[idx].path.c_str());
    return idx;
}

int FileMonitor::RestoreRandom(Sim& sim) {
    std::vector<int> candidates;
    for (int i = 0; i < 6; i++) {
        if (files[i].status == FileStatus::Deleted) candidates.push_back(i);
    }
    if (candidates.empty()) return -1;
    int idx = candidates[irand(sim.rng, 0, (int)candidates.size() - 1)];
    files[idx].status = FileStatus::Restored;
    files[idx].progress = 0.0f;
    sim.PushLog(Severity::Info, L"FS", L"restoring from snapshot: %ls", files[idx].path.c_str());
    return idx;
}

// ---------------------------------------------------------------------------
// injection
// ---------------------------------------------------------------------------
void Injection::StartAttack() {
    if (phase == 0 || phase == 3) {
        phase = 1;
        timer = 0.0f;
        payloadVisible = 0.0f;
        repairVisible = 0.0f;
    }
}

void Injection::StartRepair() {
    if (phase != 3) {
        phase = 3;
        timer = 0.0f;
        repairVisible = 0.0f;
    }
}

void Injection::Update(float dt, Sim& sim) {
    if (phase == 1) {
        timer += dt;
        payloadVisible = (std::min)(44.0f, timer * 24.0f);
        if (timer > 2.0f) {
            phase = 2;
            timer = 0.0f;
            sim.PushLog(Severity::Crit, L"KERNEL", L"shellcode staged in kernel pool -- 0x7F3A");
        }
    } else if (phase == 2) {
        timer += dt;
        if (timer > 1.8f) {
            phase = 3;
            timer = 0.0f;
            repairVisible = 0.0f;
            sim.PushLog(Severity::Warn, L"KERNEL", L"quarantine sweep started");
        }
    } else if (phase == 3) {
        timer += dt;
        repairVisible = (std::min)(60.0f, timer * 28.0f);
        if (timer > 2.6f) {
            phase = 0;
            sim.PushLog(Severity::Info, L"KERNEL", L"payload quarantined -- patch applied");
        }
    }
}

// ---------------------------------------------------------------------------
// rain
// ---------------------------------------------------------------------------
void Rain::Init(int w, int h, std::mt19937& rng, float density) {
    cols.clear();
    int count = (int)(w / 9.0f);
    if (count < 1) count = 1;
    cols.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        if (frand01(rng) > density) continue;
        RainColumn c;
        c.x = i * 9.0f + frand(rng, -2.0f, 2.0f);
        c.y = frand(rng, -720.0f, 720.0f);
        c.speed = frand(rng, 20.0f, 90.0f);
        c.length = (float)irand(rng, 8, 24);
        for (int k = 0; k < 32; k++) {
            c.glyph[k] = (unsigned char)irand(rng, 0, RAIN_GLYPH_COUNT - 1);
        }
        c.mutateTimer = frand(rng, 0.05f, 0.3f);
        cols.push_back(c);
    }
    (void)h;
}

void Rain::Update(float dt, Sim& sim) {
    int threat = sim.ThreatLevel();
    for (auto& c : cols) {
        c.y += c.speed * dt;
        if (c.y - c.length * 14.0f > VH + 40.0f) {
            c.y = -frand(sim.rng, 40.0f, 320.0f);
            c.speed = frand(sim.rng, 20.0f, 90.0f);
            c.length = (float)irand(sim.rng, 8, 24);
        }
        c.mutateTimer -= dt;
        if (c.mutateTimer <= 0.0f) {
            c.glyph[irand(sim.rng, 0, 31)] = (unsigned char)irand(sim.rng, 0, RAIN_GLYPH_COUNT - 1);
            c.mutateTimer = frand(sim.rng, 0.08f, 0.35f);
        }
        if (threat >= 3 && frand01(sim.rng) < dt * 0.35f) {
            c.red = !c.red;
        } else if (threat < 2) {
            c.red = false;
        }
    }
}

// ---------------------------------------------------------------------------
// effects
// ---------------------------------------------------------------------------
void Effects::TriggerFlash(bool red, float duration, float intensity) {
    flash = (std::max)(flash, clampf(intensity, 0.0f, 1.0f));
    flashDuration = (std::max)(flashDuration, duration);
    flashRed = red;
}

void Effects::TriggerTakeover(Takeover t, float duration) {
    takeover = t;
    takeoverTimer = duration;
    takeoverDuration = duration;
}

void Effects::TriggerTear(float intensity, float duration) {
    tear = (std::max)(tear, clampf(intensity, 0.0f, 1.0f));
    tearDuration = (std::max)(tearDuration, duration);
    tearTimer = (std::max)(tearTimer, duration);
}

void Effects::TriggerStatic(float intensity, float duration) {
    staticNoise = (std::max)(staticNoise, clampf(intensity, 0.0f, 1.0f));
    staticDuration = (std::max)(staticDuration, duration);
    staticTimer = (std::max)(staticTimer, duration);
}

void Effects::TriggerScreenDamage(float amount) {
    screenDamage = clampf((std::max)(screenDamage, amount), 0.0f, 1.0f);
}

void Effects::TriggerActCurtain(int act, float duration) {
    actCurtainAct = act;
    actCurtainTimer = duration;
    actCurtainDuration = duration;
}

void Effects::Update(float dt) {
    shake = 0.0f; // v1.5: screen shake is permanently disabled
    flash = (std::max)(0.0f, flash - dt * (flashDuration > 0.0f ? 1.0f / flashDuration : 6.0f));
    if (flash <= 0.0f) flashDuration = 0.0f;
    brownout = (std::max)(0.0f, brownout - dt * 5.0f);
    glitch = (std::max)(0.0f, glitch - dt * 3.0f);
    errorTextTimer = (std::max)(0.0f, errorTextTimer - dt);
    if (tearTimer > 0.0f) {
        tearTimer = (std::max)(0.0f, tearTimer - dt);
        tear = (std::max)(0.0f, tear - dt * (tearDuration > 0.0f ? 1.0f / tearDuration : 2.0f));
        if (tearTimer <= 0.0f) tear = 0.0f;
    }
    if (staticTimer > 0.0f) {
        staticTimer = (std::max)(0.0f, staticTimer - dt);
        staticNoise = (std::max)(0.0f, staticNoise - dt * (staticDuration > 0.0f ? 1.0f / staticDuration : 2.0f));
        if (staticTimer <= 0.0f) staticNoise = 0.0f;
    }
    screenDamage = (std::max)(0.0f, screenDamage - dt * 0.055f);
    if (actCurtainTimer > 0.0f) {
        actCurtainTimer = (std::max)(0.0f, actCurtainTimer - dt);
    }
    if (takeoverTimer > 0.0f) {
        takeoverTimer = (std::max)(0.0f, takeoverTimer - dt);
        if (takeoverTimer <= 0.0f) takeover = Takeover::None;
    }
    glitchSeed = glitchSeed * 1664525u + 1013904223u;
    if (bannerT > 0.0f) bannerT = (std::max)(0.0f, bannerT - dt);

    scanTimer -= dt;
    if (scanTimer <= 0.0f) {
        scanY = -30.0f;
        scanTimer = 6.0f + (float)(std::rand() % 60) / 10.0f;
    }
    if (scanY > -20.0f) scanY += dt * 620.0f;
}

// ---------------------------------------------------------------------------
// v1.3 console panes: two tall job windows, each spraying lines upward.
// ---------------------------------------------------------------------------
static const wchar_t* kPaneArt[2][5] = {
    { L"+---[ 10.14.0.0/16 ]----------------+",
      L"|  .1 --- .4 --- .7 --- .9 --- .12  |",
      L"|   |      |      |      |      |   |",
      L"|  L01    L02    L03    L04    L05  |",
      L"+------------------------------------+" },
    { L"+---[ PORT MAP ]---------------------+",
      L"| 443/tcp  OPEN    heap 0x7FFE0000  |",
      L"| 22/tcp   OPEN    MAC 00:1A:2B:3C  |",
      L"| 445/tcp  FILTER  job queue 0x7F3A |",
      L"+------------------------------------+" }
};

void PopupSystem::Clear() {
    for (auto& p : items) p = Popup{};
    timer = 0.2f;
    floatingTarget = 1;
    floatingCount = 0;
}

void PopupSystem::SpawnJob(Sim& sim, int kind) {
    if (kind < 0 || kind > 1) return;
    int slot = kind;
    if (items[slot].active) return;

    Popup& p = items[slot];
    p = Popup{};
    p.active = true;
    p.id = nextId;
    p.kind = kind;
    p.sev = kind == 0 ? Severity::Crit : Severity::Info;
    p.x = 850.0f;
    p.y = kind == 0 ? 52.0f : 370.0f;
    p.w = 412.0f;
    p.h = 310.0f;
    p.dur = frand(sim.rng, 10.0f, 18.0f);
    p.slide = 0.0f;
    p.life = 0.0f;
    p.abortAt = frand(sim.rng, 0.55f, 0.95f);
    p.progress = 0.0f;
    p.lineTimer = 0.04f;
    p.aborted = false;
    p.damage = clampf((float)sim.DamageLevel() / 100.0f * (kind == 0 ? 1.0f : 0.45f), 0.0f, 1.0f);

    wchar_t job[32];
    swprintf_s(job, L"0x%04X", nextId);
    p.job = job;
    wchar_t title[160];
    if (kind == 0) {
        swprintf_s(title, L"SESSION %ls // root@198.51.100.%d", job, irand(sim.rng, 2, 250));
        p.tag = L"REMOTE";
    } else {
        swprintf_s(title, L"REBUILD %ls // L%02d", job, irand(sim.rng, 1, 6));
        p.tag = L"DEFENSE";
    }
    p.title = title;

    for (int i = 0; i < 5; i++) p.lines.push_back(kPaneArt[kind][i]);
    p.lines.push_back(kind == 0 ? L"root@sentinel:~# ./porthack --target 198.51.100.23"
                                : L"root@sentinel:/ops# rebuild --layer L04 --verify");
    p.lines.push_back(sim.NextPaneLine(kind, sim.opIndex));
    p.progress = frand(sim.rng, 0.02f, 0.10f);
    nextId++;
}

void PopupSystem::SpawnFloating(Sim& sim) {
    int slot = -1;
    for (int i = 2; i < (int)items.size(); i++) {
        if (!items[i].active) { slot = i; break; }
    }
    if (slot < 0) return;
    Popup& p = items[slot];
    p = Popup{};
    p.active = true;
    p.id = nextId;
    p.kind = 2;
    p.sev = Severity::Warn;
    switch (slot) {
        case 2: p.x = 226.0f; p.y = 508.0f; break;
        case 3: p.x = 533.0f; p.y = 508.0f; break;
        default: p.x = 226.0f; p.y = 508.0f; break;
    }
    if (sim.theme == Theme::Terminal) {
        p.w = 296.0f;
        p.h = 172.0f;
    } else {
        // One compact operator window floats above the phase brief, never over it.
        p.x = 270.0f;
        p.y = 474.0f;
        p.w = 300.0f;
        p.h = 154.0f;
    }
    p.dur = frand(sim.rng, 8.0f, 14.0f);
    p.slide = 0.0f;
    p.life = 0.0f;
    p.abortAt = frand(sim.rng, 0.55f, 0.95f);
    p.progress = frand(sim.rng, 0.02f, 0.10f);
    p.lineTimer = 0.04f;
    p.aborted = false;
    p.damage = clampf(0.20f + (float)sim.DamageLevel() / 140.0f, 0.0f, 1.0f);

    wchar_t job[32];
    swprintf_s(job, L"%04X", nextId);
    p.job = job;
    wchar_t title[160];
    swprintf_s(title, L"root@sentinel:/ops# job %ls", job);
    p.title = title;
    p.tag = L"ALERT";
    p.lines.push_back(L"root@sentinel:/ops# trace --follow --json");
    p.lines.push_back(L"session accepted // remote handshake complete");
    p.lines.push_back(sim.NextPaneLine(2, sim.opIndex));
    nextId++;
}

void PopupSystem::Update(float dt, Sim& sim) {
    if (!items[0].active) SpawnJob(sim, 0);
    if (!items[1].active) SpawnJob(sim, 1);

    floatingCount = 0;
    for (int i = 2; i < (int)items.size(); i++) if (items[i].active) floatingCount++;

    timer -= dt;
    if (timer <= 0.0f) {
        if (floatingCount < floatingTarget) {
            SpawnFloating(sim);
            timer = floatingCount + 1 >= floatingTarget ? 1.2f : 0.8f;
        } else if (floatingCount > floatingTarget) {
            int oldest = -1;
            float oldestLife = -1.0f;
            for (int i = 2; i < (int)items.size(); i++) {
                if (items[i].active && items[i].life > oldestLife) {
                    oldestLife = items[i].life;
                    oldest = i;
                }
            }
            if (oldest >= 0 && items[oldest].life > 2.5f) items[oldest].active = false;
            timer = 1.2f;
        } else {
            timer = 0.8f;
        }
    }

    for (auto& p : items) {
        if (!p.active) continue;
        p.life += dt;
        p.slide = (std::min)(1.0f, p.slide + dt * 5.0f);
        float repair = (sim.act == Act::Countermeasure) ? 0.20f : 0.10f;
        p.damage = (std::max)(0.0f, p.damage - dt * repair);

        if (!p.aborted) {
            p.progress = (std::min)(1.0f, p.progress + dt * frand(sim.rng, 0.012f, 0.040f));
            if (p.progress >= p.abortAt && p.abortAt < 1.0f) {
                p.aborted = true;
                p.progress = p.abortAt;
                p.lines.push_back(L"q -- job terminated by operator [SIGKILL]");
                p.lines.push_back(L"[xx] partial write rolled back -- checksum mismatch");
            } else if (p.progress >= 1.0f) {
                p.progress = 1.0f;
                p.lines.push_back(L"[done] patch verified -- 0x7F3A sealed");
                p.life = (std::max)(p.life, p.dur - 1.0f);
            }
        }

        p.lineTimer -= dt;
        if (p.lineTimer <= 0.0f) {
            p.lineTimer = p.kind < 2 ? frand(sim.rng, 0.055f, 0.18f)
                                     : frand(sim.rng, 0.08f, 0.22f);
            p.lines.push_back(sim.NextPaneLine(p.kind, sim.opIndex));
            while ((int)p.lines.size() > (p.kind < 2 ? 30 : 12)) p.lines.pop_front();
        }

        if (p.life >= p.dur) {
            if (p.kind < 2) {
                p.active = false;
                SpawnJob(sim, p.kind);
            } else {
                p.active = false;
            }
        }
    }
    for (int i = 0; i < (int)items.size(); i++) {
        if (!items[i].active) {
            sim.boxState[i] = ConsoleBoxState{};
            if (sim.focusBox == i) sim.focusBox = -1;
        }
    }
}

// ---------------------------------------------------------------------------
// override modal
// ---------------------------------------------------------------------------
void OverrideModal::Open() {
    open = true;
    input.clear();
    attempts = 0;
    countdown = 300.0f;
    lockout = 0.0f;
    phase = 0;
    phaseTimer = 0.0f;
    bodyReveal = 0.0f;
}

void OverrideModal::Close() {
    open = false;
    input.clear();
    phase = 0;
}

void OverrideModal::Add(wchar_t c) {
    if (!open || phase != 0 || lockout > 0.0f) return;
    if (c < 0x21 || c > 0x7E) return;       // printable ASCII, no spaces
    if (input.size() >= 32) return;
    input.push_back(c);
}

void OverrideModal::Backspace() {
    if (!open || phase != 0) return;
    if (!input.empty()) input.pop_back();
}

void OverrideModal::Submit(Sim& sim) {
    if (!open || phase != 0 || lockout > 0.0f) return;
    if (input == sim.cfg.code) {
        phase = 1;
        phaseTimer = 1.2f;
        bodyReveal = 100.0f;
        sim.fx.TriggerFlash(false, 0.12f, 0.18f);
        sim.audioCue = 4;
        sim.PushLog(Severity::Info, L"SENTINEL", L"override accepted -- containment aborted");
    } else {
        phase = 2;
        phaseTimer = 1.2f;
        attempts++;
        countdown = (std::max)(0.0f, countdown - 30.0f);
        input.clear();
        sim.fx.TriggerFlash(true, 0.10f, 0.35f);
        sim.fx.TriggerErrorText(0.6f);
        sim.fx.TriggerGlitch(0.7f);
        sim.audioCue = 3;
        sim.PushLog(Severity::Crit, L"SENTINEL", L"override rejected -- attempt %d logged", attempts);
        if (attempts >= 3) {
            lockout = 5.0f;
            attempts = 0;
        }
    }
}

void OverrideModal::Update(float dt, Sim& sim) {
    if (!open) return;
    bodyReveal += dt * 55.0f;
    if (lockout > 0.0f) lockout = (std::max)(0.0f, lockout - dt);
    if (phase == 0) {
        countdown = (std::max)(0.0f, countdown - dt);
    } else if (phase == 1) {
        phaseTimer -= dt;
        if (phaseTimer <= 0.0f) sim.exitRequested = true;
    } else if (phase == 2) {
        phaseTimer -= dt;
        if (phaseTimer <= 0.0f) {
            phase = 0;
            input.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// v1.3 text pools. Every generated line carries a unique token or address so
// the visible log never repeats a sentence verbatim.
// ---------------------------------------------------------------------------
static const wchar_t* kHosts[] = {
    L"198.51.100.23", L"203.0.113.77", L"192.0.2.41", L"10.14.9.42",
    L"172.16.33.8", L"10.0.0.17", L"198.18.7.21", L"203.0.113.9",
    L"10.14.2.90", L"172.16.33.19", L"192.0.2.188", L"10.0.0.53",
    L"198.51.100.44", L"203.0.113.201", L"192.0.2.77", L"10.14.8.31",
    L"172.16.42.5", L"10.0.9.88", L"198.18.12.6", L"203.0.113.140",
    L"192.0.2.203", L"10.14.5.17", L"172.16.9.240", L"10.0.3.64"
};
static const wchar_t* kMacs[] = {
    L"00:1A:2B:3C:4D:5E", L"3C:7A:8F:01:22:9B", L"B4:2E:99:AA:10:7C",
    L"00:0C:29:7E:41:2D", L"AC:1F:6B:90:3D:44", L"F0:2F:74:8C:55:10",
    L"18:60:24:11:9E:03", L"E4:54:E8:77:2A:6F",
    L"00:1B:44:11:3A:B7", L"2C:F0:5D:9A:12:33", L"9C:5C:8E:14:7A:20",
    L"38:2C:4A:9D:0B:11", L"5C:3A:45:2A:88:CC", L"70:85:C2:61:AE:09",
    L"A4:BB:6D:11:77:2E", L"00:25:90:4C:8A:10", L"34:17:EB:0F:51:9D",
    L"84:16:F9:2B:64:70", L"B8:27:EB:19:7C:42", L"00:15:5D:03:44:2A",
    L"F4:8E:38:9A:10:0B", L"DC:A6:32:55:91:7F", L"E0:D5:5E:6A:33:12"
};
static const wchar_t* kUsers[] = {
    L"svc_backup", L"admin", L"user01", L"finance", L"helpdesk",
    L"SYSTEM", L"sqlsvc", L"operator",
    L"svc_web", L"svc_print", L"backup", L"qa", L"devops", L"analyst",
    L"contractor", L"root", L"svc_sql", L"audit", L"guest", L"mssql",
    L"nessus", L"wazuh", L"splunk", L"jenkins", L"deploy", L"helpdesk2"
};
static const wchar_t* kPorts[] = {
    L"22,80,443,445,3389", L"139,445,5985,3389", L"21,22,3306,8080",
    L"53,88,389,636,3268", L"80,443,8000,8443", L"1433,1521,5432,6379",
    L"22,80,443,3306", L"135,139,445,5985", L"25,110,143,587",
    L"161,162,514,1514", L"2375,2376,6443,10250", L"5900,5985,8000,8080",
    L"88,135,389,445,3389", L"5000,5001,8080,8443", L"1883,8883,9092,9093",
    L"2049,111,3270,1521"
};
static const wchar_t* kSerials[] = {
    L"SN-7F3A-9C41", L"WD-WCC4E1234567", L"ST2000DM008-2FR102",
    L"SAMSUNG MZVLB512HAJQ", L"KINGSTON SA400S37240G", L"NVMe 0x7F3A9C41",
    L"SN-4C21-7A9E", L"WD-WCC7K1238844", L"ST4000VN008-2DR166",
    L"CRUCIAL CT500MX500SSD1", L"INTEL SSDPEKNW010T8", L"NVMe 0x9A44C1E2",
    L"SN-91B7-2D04", L"TOSHIBA DT01ACA200", L"SK hynix SC311 SATA"
};
static const wchar_t* kPaths[] = {
    L"C:\\Windows\\System32\\config\\SAM",
    L"C:\\Windows\\System32\\drivers\\disk.sys",
    L"D:\\Backup\\2026\\family_photos.7z",
    L"\\\\FS01\\legal\\contracts\\2026\\NDA.pdf",
    L"C:\\Users\\Public\\Documents\\Q4_finance.xlsx",
    L"C:\\ProgramData\\Sentinel\\policy.db",
    L"C:\\Windows\\System32\\winlogon.exe",
    L"C:\\Windows\\System32\\lsass.exe",
    L"C:\\Windows\\System32\\Tasks\\SentinelRecovery",
    L"C:\\ProgramData\\Microsoft\\Crypto\\RSA\\MachineKeys",
    L"\\\\FS01\\finance\\2026\\wire_transfers.xlsx",
    L"\\\\FS01\\legal\\custody\\evidence_chain.csv",
    L"D:\\Backup\\2026\\mailbox.pst",
    L"D:\\Backup\\2026\\ERP_snapshot.vhdx",
    L"C:\\Users\\Public\\Documents\\incident_0421.docx",
    L"C:\\ProgramData\\Sentinel\\quarantine\\.index",
    L"C:\\Windows\\System32\\drivers\\tcpip.sys",
    L"C:\\Windows\\System32\\config\\SYSTEM"
};
static const wchar_t* kCn[] = {
    L"Windows Resource Protection found corrupt files but was unable to repair them.",
    L"Error 0x800F081F: source files could not be found.",
    L"Access is denied.",
    L"The system cannot find the file specified.",
    L"Volume Shadow Copy Service provider error.",
    L"Unable to terminate the process.",
    L"The service cannot be stopped.",
    L"The operation completed successfully.",
    L"Deleting volume shadow copies...",
    L"The system will restart in 0 seconds.",
    L"Access denied: SeBackupPrivilege is required.",
    L"Volume shadow copy creation failed 0x8004230F.",
    L"Service Control Manager: service did not respond to the start request.",
    L"Kernel integrity check failed: code integrity policy not satisfied.",
    L"Object Manager: handle reference count anomaly.",
    L"Security log: event 4625 failed-logon count exceeds threshold.",
    L"Network layer: TCP retransmission rate exceeds baseline by 3.2x.",
    L"Credential Guard: read-only LSASS handle denied.",
    L"Backup agent: snapshot mount point is busy.",
    L"File system: USN journal gap detected.",
    L"Certificate store: root chain validation failed.",
    L"Driver verification: unsigned driver load blocked.",
    L"Scheduled task: trigger modified externally.",
    L"Registry: unauthorized Run key added.",
    L"Domain controller: Kerberos ticket anomaly.",
    L"DLP: outbound channel matched policy DLP-07.",
    L"Endpoint detection: behavior chain matched T1059.001.",
    L"Quarantine: object is being imaged for forensics.",
    L"Recovery point: checksum mismatch, rolled back."
};

static const wchar_t* kTechniques[] = {
    L"T1595 Active Scanning", L"T1566 Phishing Attachment", L"T1059.001 PowerShell",
    L"T1003.001 LSASS Dump", L"T1021.002 SMB Lateral Movement", L"T1053.005 Scheduled Task",
    L"T1041 C2 Exfiltration", L"T1486 Data Encrypted for Impact", L"T1496 Resource Hijacking",
    L"T1071 Application Layer Protocol", L"T1547 Boot/Logon Autostart", L"T1562.001 Impair Defenses"
};

static const wchar_t* kArtifacts[] = {
    L"memory.raw", L"ntfs_usn.journal", L"prefetch.pf", L"amcache.hve",
    L"shimcache.bin", L"evtx/Security.evtx", L"evtx/Sysmon.evtx",
    L"pcap/egress.pcap", L"pcap/dns.pcap", L"registry/SYSTEM",
    L"registry/SAM", L"lnk/recent.lnk", L"wmi/consumers.txt",
    L"tasks/sentinel.job", L"driver/unsigned.sys", L"quarantine/index.db"
};

static const wchar_t* kNodeRoles[] = {
    L"EDGE", L"RELAY", L"VAULT", L"CORE", L"DNS", L"GW",
    L"PROXY", L"DB", L"LOG", L"AUTH", L"BACKUP", L"MAIL",
    L"CI", L"JUMP"
};

static const wchar_t* kNodeNames[14] = {
    L"edge-01", L"relay-02", L"vault-03", L"core-04", L"dns-05",
    L"gw-06", L"proxy-07", L"db-08", L"log-09", L"auth-10",
    L"backup-11", L"mail-12", L"ci-13", L"jump-14"
};

static const wchar_t* kPhaseBrief[14][3] = {
    { L"RECON // ATTACK SURFACE NARROWING", L"Perimeter fingerprint retained; scanner source is rotating", L"ACTION // Review ACLs, tighten perimeter access" },
    { L"INTRUSION // PAYLOAD DELIVERY IDENTIFIED", L"Phishing attachment and credential spray in parallel", L"ACTION // Block C2, reset suspicious sessions" },
    { L"ESCALATION // KERNEL PROBING RISING", L"Unsigned driver and anomalous handles flagged", L"ACTION // Verify driver chain, enable kernel isolation" },
    { L"CREDENTIALS // DUMP ATTEMPT BLOCKED", L"LSASS handle denied; anomalous tickets under review", L"ACTION // Rotate credentials, clear privileged sessions" },
    { L"BOUNDARY // HIGH-SPEED TARGET CAPTURE", L"Orbital targets rotate signatures; intercept bus saturated", L"ACTION // Lock external vectors, cut anomalous sessions" },
    { L"INTERCEPT // THREAT SIGNATURE COLLAPSING", L"Purge, regeneration and evidence capture run in parallel", L"ACTION // Hold boundary fire, review purge chain" },
    { L"LATERAL // ANOMALOUS AUTH SPREADING", L"SMB / WinRM paths flagged", L"ACTION // Cut lateral paths, segment and isolate" },
    { L"PERSISTENCE // STARTUP ITEMS TAMPERED", L"Scheduled tasks and Run key deviations located", L"ACTION // Remove persistence, compare baseline" },
    { L"EXFIL // DLP-07 HIT ON COVERT CHANNEL", L"Outbound volume rising; C2 heartbeat stable", L"ACTION // Seal egress, freeze forensic image" },
    { L"CONTAINMENT // SEGMENT ISOLATION ACTIVE", L"Attack surface contracting; residual hosts under review", L"ACTION // Review isolation boundary and policy coverage" },
    { L"COUNTERATTACK // KILL CHAIN SEGMENTED", L"Residual sessions clearing; risk beginning to fall", L"ACTION // Hold blocks, monitor recurrence" },
    { L"RECOVERY // VERIFIED SNAPSHOT ROLLBACK", L"Core services restored first; patch queue in parallel", L"ACTION // Staged rollback, verify service state" },
    { L"HARDENING // SECURITY BASELINE CONVERGING", L"Policies pushed in batches; exceptions trending to zero", L"ACTION // Review coverage and policy exceptions" },
    { L"AFTERMATH // EVIDENCE CHAIN SEALED", L"Residual risk under review; response chain traceable", L"ACTION // Preserve evidence, publish findings" }
};

static const int kPhaseCategories[14][3] = {
    {0, 10, 9}, {1, 0, 2}, {2, 1, 3}, {2, 2, 9},
    {3, 6, 1}, {6, 7, 3}, {3, 3, 1}, {4, 4, 9},
    {5, 5, 9}, {6, 7, 6}, {1, 3, 6}, {7, 7, 8},
    {8, 8, 10}, {6, 8, 10}
};

template <size_t N>
static const wchar_t* pick(std::mt19937& r, const wchar_t* (&a)[N]) {
    return a[irand(r, 0, (int)N - 1)];
}

static std::wstring GenerateAutoLine(Sim& sim, int category, int phase) {
    wchar_t buf[512];
    const wchar_t* host = pick(sim.rng, kHosts);
    const wchar_t* mac = pick(sim.rng, kMacs);
    const wchar_t* user = pick(sim.rng, kUsers);
    const wchar_t* ports = pick(sim.rng, kPorts);
    const wchar_t* serial = pick(sim.rng, kSerials);
    const wchar_t* path = pick(sim.rng, kPaths);
    const wchar_t* tech = pick(sim.rng, kTechniques);
    const wchar_t* artifact = pick(sim.rng, kArtifacts);
    int c = category % 11;
    int variant = (phase + category + (int)(sim.time * 10.0)) & 1;

    switch (c) {
    case 0:
        if (variant)
            swprintf_s(buf, L"[recon] %ls ports %ls -- ttl=%d -- %ls", host, ports,
                       irand(sim.rng, 48, 126), tech);
        else
            swprintf_s(buf, L"[scan] %ls:%d open -- banner 0x%08X -- %ls", host,
                       irand(sim.rng, 21, 65535), irand(sim.rng, 0x10000000, 0x7FFFFFFF), tech);
        break;
    case 1:
        if (variant)
            swprintf_s(buf, L"[access] session 0x%04X -- %ls -> %ls -- %ls",
                       irand(sim.rng, 0x1000, 0xFFFF), user, host, tech);
        else
            swprintf_s(buf, L"[+] %ls authenticated as %ls -- token 0x%08X", host, user,
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF));
        break;
    case 2:
        if (variant)
            swprintf_s(buf, L"[cred] Kerberos TGT 0x%08X user=%ls rid=%d -- %ls",
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF), user,
                       irand(sim.rng, 500, 1200), artifact);
        else
            swprintf_s(buf, L"[cred] NTLM %08X user=%ls rid=%d -- hive %ls",
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF), user,
                       irand(sim.rng, 500, 1200), path);
        break;
    case 3:
        if (variant)
            swprintf_s(buf, L"[lateral] wmiexec %ls -u %ls -- service 0x%04X -- %ls",
                       host, user, irand(sim.rng, 0x1000, 0xFFFF), tech);
        else
            swprintf_s(buf, L"[lateral] psexec %ls -u %ls -h 0x%08X -- pipe %04X",
                       host, user, irand(sim.rng, 0x10000000, 0x7FFFFFFF),
                       irand(sim.rng, 0x1000, 0xFFFF));
        break;
    case 4:
        if (variant)
            swprintf_s(buf, L"[persist] Run key HKCU\\Software\\Sentinel%04X -> %ls",
                       irand(sim.rng, 0x1000, 0xFFFF), artifact);
        else
            swprintf_s(buf, L"[persist] schtasks /create /tn Sent%04X /tr %ls /sc onlogon",
                       irand(sim.rng, 0x1000, 0xFFFF), host);
        break;
    case 5:
        if (variant)
            swprintf_s(buf, L"[ledger] %ls -> %ls %d KB sha256=%08X artifact=%ls",
                       path, host, irand(sim.rng, 64, 8192),
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF), artifact);
        else
            swprintf_s(buf, L"[exfil] %ls -> %ls %d KB sha256=%08X", path, host,
                       irand(sim.rng, 64, 8192), irand(sim.rng, 0x10000000, 0x7FFFFFFF));
        break;
    case 6:
        if (variant)
            swprintf_s(buf, L"[contain] quarantine %ls -- object 0x%04X -- %ls",
                       artifact, irand(sim.rng, 0x1000, 0xFFFF), tech);
        else
            swprintf_s(buf, L"[contain] isolate segment %d -- MAC %ls -- rule %04X",
                       irand(sim.rng, 1, 48), mac, irand(sim.rng, 0x1000, 0xFFFF));
        break;
    case 7:
        if (variant)
            swprintf_s(buf, L"[rebuild] snapshot %ls -> layer L%02d queue=%d hash=%08X",
                       artifact, irand(sim.rng, 1, 6), irand(sim.rng, 1, 24),
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF));
        else
            swprintf_s(buf, L"[rebuild] L%02d %3d%% hash=%08X queue=%d",
                       irand(sim.rng, 1, 6), irand(sim.rng, 5, 99),
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF), irand(sim.rng, 1, 24));
        break;
    case 8:
        if (variant)
            swprintf_s(buf, L"[harden] baseline %ls -- exceptions=%d -- MAC %ls",
                       tech, irand(sim.rng, 0, 12), mac);
        else
            swprintf_s(buf, L"[harden] policy %04X applied -- %d rules -- MAC %ls",
                       irand(sim.rng, 0x1000, 0xFFFF), irand(sim.rng, 12, 240), mac);
        break;
    case 9:
        if (variant)
            swprintf_s(buf, L"%ls [0x%08X] artifact=%ls", pick(sim.rng, kCn),
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF), artifact);
        else
            swprintf_s(buf, L"%ls [0x%08X] object=%ls", pick(sim.rng, kCn),
                       irand(sim.rng, 0x10000000, 0x7FFFFFFF), path);
        break;
    default:
        swprintf_s(buf, L"[hw] MAC %ls UUID %08X-%04X disk %ls -- %ls", mac,
                   irand(sim.rng, 0x10000000, 0x7FFFFFFF),
                   irand(sim.rng, 0x1000, 0xFFFF), serial, tech);
        break;
    }
    return sim.UniqueLine(buf);
}

std::wstring Sim::UniqueLine(const std::wstring& s) {
    totalAutoLines++;
    std::wstring out = s;
    if (std::find(recentLines.begin(), recentLines.end(), out) != recentLines.end()) {
        duplicateCount++;
        wchar_t suffix[32];
        swprintf_s(suffix, L" [%04X]", irand(rng, 0x1000, 0xFFFF));
        out += suffix;
    } else {
        uniqueAutoLines++;
    }
    recentLines.push_back(out);
    if (recentLines.size() > 1200) recentLines.pop_front();
    return out;
}

LogStream Sim::StreamForCategory(int category) const {
    switch (category % 11) {
    case 5:  return LogStream::Ledger;
    case 6:
    case 7:
    case 8:  return LogStream::Defense;
    case 9:
    case 10: return LogStream::Evidence;
    default: return LogStream::Attack;
    }
}

void Sim::PushAutoLog(int category) {
    std::wstring line = GenerateAutoLine(*this, category, opIndex);
    Severity sev = Severity::Info;
    const wchar_t* module = L"SENTINEL";
    int c = category % 11;
    switch (c) {
    case 0: sev = Severity::Warn; module = L"NET"; break;
    case 1: sev = Severity::Crit; module = L"L04"; break;
    case 2: sev = Severity::Crit; module = L"KERNEL"; break;
    case 3: sev = Severity::Warn; module = L"NET"; break;
    case 4: sev = Severity::Warn; module = L"L03"; break;
    case 5: sev = Severity::Crit; module = L"FS"; break;
    case 6: sev = Severity::Info; module = L"L06"; break;
    case 7: sev = Severity::Info; module = L"L06"; break;
    case 8: sev = Severity::Info; module = L"POLICY"; break;
    case 9: sev = Severity::Warn; module = L"CMD"; break;
    default: sev = Severity::Info; module = L"L01"; break;
    }
    log.Push(sev, module, line, StreamForCategory(c));
}

std::wstring Sim::NextPaneLine(int kind, int phase) {
    wchar_t buf[512];
    const wchar_t* host = pick(rng, kHosts);
    const wchar_t* mac = pick(rng, kMacs);
    const wchar_t* user = pick(rng, kUsers);
    const wchar_t* ports = pick(rng, kPorts);
    const wchar_t* serial = pick(rng, kSerials);
    const wchar_t* path = pick(rng, kPaths);
    const wchar_t* tech = pick(rng, kTechniques);
    const wchar_t* artifact = pick(rng, kArtifacts);
    int n = (phase * 3 + irand(rng, 0, 7)) & 7;
    if (kind == 0) {
        switch (n) {
        case 0:
            swprintf_s(buf, L"root@%ls:~# nmap -sS -Pn -p %ls %ls", host, ports, host);
            break;
        case 1:
            swprintf_s(buf, L"root@%ls:~# smbclient \\\\%ls\\IPC$ -U %ls -- 0x%08X",
                       host, host, user, irand(rng, 0x10000000, 0x7FFFFFFF));
            break;
        case 2:
            swprintf_s(buf, L"[+] %ls:%d open -- 0x%08X -- %ls", host,
                       irand(rng, 21, 65535), irand(rng, 0x10000000, 0x7FFFFFFF), tech);
            break;
        case 3:
            swprintf_s(buf, L"root@%ls:~# hashcat -m 1000 %08X.ntlm %ls", host,
                       irand(rng, 0x10000000, 0x7FFFFFFF), serial);
            break;
        case 4:
            swprintf_s(buf, L"root@%ls:~# psexec \\\\%ls -u %ls -p 0x%04X", host, host,
                       user, irand(rng, 0x1000, 0xFFFF));
            break;
        case 5:
            swprintf_s(buf, L"root@%ls:~# curl -sk https://%ls/beacon --data @%ls",
                       host, host, artifact);
            break;
        case 6:
            swprintf_s(buf, L"[+] session 0x%04X established -- MAC %ls -- %ls",
                       irand(rng, 0x1000, 0xFFFF), mac, tech);
            break;
        default:
            swprintf_s(buf, L"[recon] %ls ports %ls -- ttl=%d", host, ports,
                       irand(rng, 48, 126));
            break;
        }
    } else if (kind == 1) {
        switch (n) {
        case 0:
            swprintf_s(buf, L"[rebuild] L%02d %3d%% queue=%d", irand(rng, 1, 6),
                       irand(rng, 5, 99), irand(rng, 1, 24));
            break;
        case 1:
            swprintf_s(buf, L"[*] patch 0x%08X verified -- MAC %ls -- %ls",
                       irand(rng, 0x10000000, 0x7FFFFFFF), mac, tech);
            break;
        case 2:
            swprintf_s(buf, L"root@sentinel:/ops# fsck --layer L%02d --check %ls",
                       irand(rng, 1, 6), artifact);
            break;
        case 3:
            swprintf_s(buf, L"[!!] heap 0x%08X -> 0x%08X", irand(rng, 0x10000000, 0x7FFFFFFF),
                       irand(rng, 0x10000000, 0x7FFFFFFF));
            break;
        case 4:
            swprintf_s(buf, L"[ok] serial %ls -- 0x%08X", serial,
                       irand(rng, 0x10000000, 0x7FFFFFFF));
            break;
        case 5:
            swprintf_s(buf, L"[snapshot] %ls verified -- rollback point %02d",
                       artifact, irand(rng, 1, 9));
            break;
        case 6:
            swprintf_s(buf, L"[policy] %ls -- exceptions=%d -- MAC %ls", tech,
                       irand(rng, 0, 8), mac);
            break;
        default:
            swprintf_s(buf, L"[contain] quarantine %ls -- object 0x%04X",
                       artifact, irand(rng, 0x1000, 0xFFFF));
            break;
        }
    } else {
        switch (n) {
        case 0:
            swprintf_s(buf, L"root@%ls:~# tail -f /var/log/%ls", host, artifact);
            break;
        case 1:
            swprintf_s(buf, L"root@sentinel:/ops# grep -R \"%ls\" %ls", tech, path);
            break;
        case 2:
            swprintf_s(buf, L"[*] hash 0x%08X -- object=%ls", irand(rng, 0x10000000, 0x7FFFFFFF), artifact);
            break;
        case 3:
            swprintf_s(buf, L"[job] queue=%d target=%ls user=%ls", irand(rng, 1, 24), host, user);
            break;
        case 4:
            swprintf_s(buf, L"[net] flow %ls:%d -> %ls:443", host, irand(rng, 1024, 65535), host);
            break;
        case 5:
            swprintf_s(buf, L"[*] sha256=%08X%08X artifact=%ls",
                       irand(rng, 0x10000000, 0x7FFFFFFF), irand(rng, 0x10000000, 0x7FFFFFFF), artifact);
            break;
        case 6:
            swprintf_s(buf, L"[evidence] custody chain %d entries -- %ls", irand(rng, 3, 18), artifact);
            break;
        default:
            swprintf_s(buf, L"[note] %ls -- %ls", tech, artifact);
            break;
        }
    }
    return UniqueLine(buf);
}

void Sim::BuildPhaseBrief() {
    int p = opIndex;
    if (p < 0 || p >= 14) p = 0;
    for (int i = 0; i < 3; i++) phaseBrief[i] = kPhaseBrief[p][i];
}

void Sim::LoadQuotes(const std::wstring& path) {
    quotes.clear();
    if (!LoadQuoteFile(path, quotes)) {
        // Resolve the portable corpus beside the executable without machine-specific paths.
        std::wstring dir = GetExeDir();
        if (!LoadQuoteFile(dir + L"\\quotes-en.txt", quotes)) {
            LoadQuoteFile(dir + L"\\quotes.txt", quotes);
        }
    }
    if (quotes.empty()) {
        quotes = {
            L"All that is seen is permitted to be seen.",
            L"The system never forgets; it only hides the record deeper.",
            L"We believe we use the tools, until the tools begin to choose us.",
            L"True containment is making you believe the door was never closed."
        };
    }
    for (auto& g : ghosts) g = GhostQuote{};
    quoteTimer = 2.0f;
    PushLog(Severity::Info, L"QUOTE", L"ghost corpus indexed -- %d entries", (int)quotes.size());
}

void Sim::UpdateQuotes(float dt) {
    for (auto& g : ghosts) {
        if (!g.active) continue;
        g.life += dt;
        if (g.index < 0 || g.index >= (int)quotes.size()) {
            g.active = false;
            continue;
        }
        int len = (int)quotes[g.index].size();
        if (g.reveal < len) {
            g.reveal = (std::min)(len, g.reveal + (int)(dt * 34.0f) + 1);
        }
        // Once the typing head passes the midpoint, the tail starts sliding
        // off the left edge too: a moving window, not a static line.
        if (g.reveal >= len / 2 && g.start < len) {
            g.start = (std::min)(len, g.start + (int)(dt * 18.0f) + 1);
        }
        if (g.start >= len || g.life >= g.dur) g.active = false;
    }
    quoteTimer -= dt;
    if (quoteTimer > 0.0f || quotes.empty()) return;
    int active = 0;
    for (const auto& g : ghosts) if (g.active) active++;
    if (active < 1) {
        for (auto& g : ghosts) {
            if (g.active) continue;
            g.active = true;
            g.index = irand(rng, 0, (int)quotes.size() - 1);
            // Each layout gets a deliberate quiet zone instead of a random collision.
            switch (theme) {
            case Theme::Terminal:
                g.x = frand(rng, 250.0f, 420.0f); g.y = frand(rng, 390.0f, 430.0f); break;
            case Theme::SplitOps:
                g.x = frand(rng, 410.0f, 510.0f); g.y = 602.0f; break;
            case Theme::WarRoom:
                g.x = frand(rng, 390.0f, 480.0f); g.y = 584.0f; break;
            case Theme::Forensic:
                g.x = frand(rng, 390.0f, 500.0f); g.y = 602.0f; break;
            case Theme::Containment:
                g.x = frand(rng, 390.0f, 500.0f); g.y = 584.0f; break;
            case Theme::BlackBox:
                g.x = 48.0f; g.y = 526.0f; break;
            default:
                g.x = 400.0f; g.y = 560.0f; break;
            }
            g.life = 0.0f;
            g.dur = frand(rng, 9.0f, 15.0f);
            g.reveal = 0;
            g.start = 0;
            // The same sentence also cuts into the live data stream, as a ghost line.
            std::wstring line = quotes[g.index];
            if (line.size() > 110) line = line.substr(0, 110) + L"...";
            log.Push(Severity::Info, L"QUOTE", line, LogStream::Both, true);
            break;
        }
    }
    quoteTimer = frand(rng, 10.0f, 20.0f);
}

void Sim::UpdateLayerFeeds(float dt) {
    layerFeedTimer -= dt;
    if (layerFeedTimer > 0.0f) return;
    layerFeedTimer = 1.1f;
    for (int i = 0; i < 6; i++) {
        int cat = 6 + (i % 3);
        std::wstring line = GenerateAutoLine(*this, cat, opIndex);
        wchar_t prefix[32];
        swprintf_s(prefix, L"L%02d // ", i + 1);
        layerFeed[i].push_back(std::wstring(prefix) + line);
        while (layerFeed[i].size() > 14) layerFeed[i].pop_front();
    }
}

// ---------------------------------------------------------------------------
// v2.3 high-speed boundary interception
// ---------------------------------------------------------------------------
void BoundaryState::PushEvent(const std::wstring& line) {
    events.push_back(line);
    while (events.size() > 22) events.pop_front();
}

void BoundaryState::Init(Sim& sim) {
    *this = BoundaryState{};
    for (int i = 0; i < 14; ++i) {
        BoundaryTarget& t = targets[i];
        t.angle = (float)i * 6.28318530718f / 14.0f;
        t.signature = 0x7F3A0000u ^ (uint32_t)(i * 0x1F31 + 0x1984);
    }
    for (int i = 0; i < 20; ++i) {
        const int node = i % 14;
        wchar_t line[160];
        swprintf_s(line, L"%06d  scan --node %-8.8ls --sig %04X  risk %03d",
                   serial++, sim.nodes[node].name.c_str(), targets[node].signature & 0xFFFFu,
                   20 + (i * 37 + node * 11) % 80);
        stream.push_back(line);
        denied += 3 + (uint64_t)(i % 9);
    }
    PushEvent(L"BOUNDARY ONLINE // 14 ORBITAL TARGETS");
    PushEvent(L"INTERCEPT BUS // HIGH-SPEED MODE");
}

void BoundaryState::Update(float dt, Sim& sim) {
    const bool reduced = sim.cfg.motion == L"reduced";
    scroll += dt * 92.0f;
    for (int i = 0; i < 14; ++i) {
        BoundaryTarget& t = targets[i];
        if (!reduced) {
            const float speed = (i % 2 == 0) ? 0.46f : -0.59f;
            t.angle = fmodf(t.angle + speed * dt + 6.28318530718f, 6.28318530718f);
        }
        if (t.state == BoundaryTargetState::Tracked) continue;
        t.stateTimer -= dt;
        if (t.stateTimer > 0.0f) continue;
        if (t.state == BoundaryTargetState::Locked) {
            t.state = BoundaryTargetState::Purging;
            t.stateTimer = 0.34f;
            ++purges;
            denied += 37;
            sim.blocked += 19;
            wchar_t line[160];
            swprintf_s(line, L"PURGE  %-8.8ls  sig %04X  boundary cut +%02d",
                       sim.nodes[i].name.c_str(), t.signature & 0xFFFFu, 19);
            PushEvent(line);
            lockedTarget = -1;
        } else if (t.state == BoundaryTargetState::Purging) {
            t.state = BoundaryTargetState::Respawn;
            t.stateTimer = 0.62f;
            wchar_t line[160];
            swprintf_s(line, L"ERASED %-8.8ls  generation %02d  route collapsed",
                       sim.nodes[i].name.c_str(), t.generation);
            PushEvent(line);
        } else {
            t.state = BoundaryTargetState::Tracked;
            t.stateTimer = 0.0f;
            ++t.generation;
            t.signature = 0x7F3A0000u ^ (uint32_t)(i * 0x1F31 + t.generation * 0x45D9 + serial);
            if (!reduced) t.angle = fmodf(t.angle + 0.73f, 6.28318530718f);
            wchar_t line[160];
            swprintf_s(line, L"VECTOR %-8.8ls  new sig %04X  outer orbit",
                       sim.nodes[i].name.c_str(), t.signature & 0xFFFFu);
            PushEvent(line);
        }
    }

    lockTimer -= dt;
    if (lockTimer <= 0.0f) {
        int selected = -1;
        int best = -1;
        for (int k = 0; k < 14; ++k) {
            const int i = (k + serial + purges) % 14;
            if (targets[i].state != BoundaryTargetState::Tracked) continue;
            int score = (int)(sim.nodes[i].risk * 100.0f) + ((i * 17 + serial) % 29);
            if (score > best) { best = score; selected = i; }
        }
        if (selected >= 0) {
            BoundaryTarget& t = targets[selected];
            t.state = BoundaryTargetState::Locked;
            t.stateTimer = 0.42f;
            lockedTarget = selected;
            wchar_t line[160];
            swprintf_s(line, L"LOCK   %-8.8ls  sig %04X  risk %03d  fire",
                       sim.nodes[selected].name.c_str(), t.signature & 0xFFFFu, best % 101);
            PushEvent(line);
        }
        lockTimer = 0.58f;
    }

    streamTimer -= dt;
    while (streamTimer <= 0.0f) {
        streamTimer += 0.085f;
        const int node = serial % 14;
        const uint32_t sig = targets[node].signature ^ (uint32_t)(serial * 0x9E37);
        static const wchar_t* commands[8] = {
            L"deny --channel", L"trace --vector", L"cut --session", L"verify --edge",
            L"purge --signal", L"seal --route", L"scan --burst", L"fence --node"
        };
        wchar_t line[192];
        swprintf_s(line, L"%06d  %-15.15ls %-8.8ls  %04X  +%02d",
                   serial, commands[serial % 8], sim.nodes[node].name.c_str(),
                   sig & 0xFFFFu, 3 + serial % 17);
        ++serial;
        denied += 3 + (uint64_t)(serial % 17);
        stream.push_back(line);
        while (stream.size() > 28) stream.pop_front();
        if ((serial % 5) == 0) sim.blocked += 3;
    }
}

// ---------------------------------------------------------------------------
// sim
// ---------------------------------------------------------------------------
void Sim::Init(const Config& c, uint32_t seed) {
    cfg = c;
    rng.seed(seed);

    for (int i = 0; i < 6; i++) {
        firewalls[i].id = i;
        firewalls[i].name = kFWNames[i];
        firewalls[i].SetSecure(100.0f);
        firewalls[i].jitterPhase = frand(rng, 0.0f, 6.28f);
    }
    files.Init();
    log.entries.clear();
    modal = OverrideModal{};
    injection = Injection{};
    fx = Effects{};
    motion.Reset();
    musicTrack.clear();
    musicIndex = 0;
    musicCount = 0;
    popups.Clear();
    popups.timer = 0.2f;
    popups.floatingTarget = 1;
    recentLines.clear();
    recentTemplates.clear();
    duplicateCount = 0;
    layerFeedTimer = 1.0f;
    evidenceTimer = 0.6f;
    audioCue = -1;
    audioTimer = 0.5f;
    nodeTimer = 2.5f;
    quotes.clear();
    for (auto& g : ghosts) g = GhostQuote{};
    quoteTimer = 5.0f;
    for (auto& f : layerFeed) f.clear();
    for (int i = 0; i < 14; i++) {
        nodes[i].name = kNodeNames[i];
        nodes[i].ip = pick(rng, kHosts);
        nodes[i].role = kNodeRoles[i];
        nodes[i].risk = frand01(rng);
        nodes[i].online = true;
    }
    boundary.Init(*this);
    campaign = Campaign{};
    campaign.goal = (AttackGoal)irand(rng, 0, (int)AttackGoal::Count - 1);
    campaign.c2 = L"203.0.113.77";
    summaryTimer = 0.0f;
    opCycleElapsed = 0.0f;
    autoTheme = cfg.theme == L"auto";
    theme = Theme::Terminal;
    if (cfg.theme == L"split") theme = Theme::SplitOps;
    else if (cfg.theme == L"boundary") theme = Theme::Boundary;
    else if (cfg.theme == L"warroom") theme = Theme::WarRoom;
    else if (cfg.theme == L"forensic") theme = Theme::Forensic;
    else if (cfg.theme == L"containment") theme = Theme::Containment;
    else if (cfg.theme == L"blackbox") theme = Theme::BlackBox;
    for (auto& b : boxState) b = ConsoleBoxState{};
    focusBox = -1;
    BuildSummary();

    time = 0.0;
    cycle = 0;
    opPhase = OpPhase::Counterattack;
    opIndex = (int)OpPhase::Counterattack;
    opCycle = 0;
    opTimer = 0.0f;
    opDuration = 14.0f;
    opActionTimer = 3.0f;
    exitRequested = false;
    allowClose = false;
    debugOverlay = false;
    toast.clear();
    toastTimer = 0.0f;
    logTimer = 0.8f;
    statTimer = 0.0f;
    chaosTimer = 0.35f;
    burstTimer = 1.0f;
    autoPrompted = false;
    packets = 1284553;
    blocked = 0;
    encrypted = 0;
    sysCpuPct = 0.0f;
    procCpuPct = 0.0f;
    memPct = 0.0f;
    procMemMB = 0.0f;
    bootLineTimer = 0.0f;
    bootLineIndex = 0;
    debugLines.clear();

    // Open mid-crisis at phase 11: the countermeasure grid is already engaged.
    act = Act::Countermeasure;
    actIndex = 2;
    actElapsed = 0.0f;
    if (autoTheme) theme = kActDefs[2].firstTheme;
    BuildPhaseBrief();
    PushLog(Severity::Warn, L"SENTINEL", L"containment grid under sustained assault");
    PushLog(Severity::Crit, L"NET", L"multi-vector intrusion -- active");
    for (int i = 0; i < 6; i++) PushAutoLog(i);
    EnterOpPhase(OpPhase::Counterattack);
    popups.SpawnJob(*this, 0);
    popups.SpawnJob(*this, 1);
    popups.SpawnFloating(*this);
}

void Sim::PushLog(Severity sev, const wchar_t* module, const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(buf, fmt, ap);
    va_end(ap);
    log.Push(sev, module, buf);
}

void Sim::Update(float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;
    float adt = dt * clampf(cfg.speed, 0.5f, 3.0f);
    time += adt;
    motion.Update(adt, cfg.motion == L"reduced");

    UpdateOpDirector(adt);
    UpdateAct(adt);
    UpdateCampaign(adt);
    boundary.Update(adt, *this);
    UpdateQuotes(adt);
    popups.floatingTarget = FloatTarget();

    nodeTimer -= adt;
    if (nodeTimer <= 0.0f) {
        nodeTimer = frand(rng, 3.0f, 5.0f);
        for (auto& n : nodes) {
            if (irand(rng, 0, 2) == 0) n.risk = clampf(n.risk + frand(rng, -0.18f, 0.18f), 0.02f, 0.99f);
            if (irand(rng, 0, 15) == 0) n.online = !n.online;
        }
    }

    audioTimer -= adt;
    if (audioTimer <= 0.0f) {
        audioTimer = ThreatLevel() >= 3 ? 0.42f : 0.95f;
        if (audioCue < 0) audioCue = 0; // tick
    }

    for (auto& fw : firewalls) fw.Update(adt, *this);
    UpdateLayerFeeds(adt);
    files.Update(adt, *this);
    injection.Update(adt, *this);
    log.Update(adt);
    fx.Update(adt);
    popups.Update(adt, *this);
    if (modal.open) modal.Update(dt, *this);
    if (toastTimer > 0.0f) toastTimer -= adt;

    UpdateStats(adt);

    // v1.2 chaos layer: less flash, more text and console noise.
    chaosTimer -= adt;
    if (chaosTimer <= 0.0f) {
        chaosTimer = frand(rng, 2.6f, 6.0f);
        if (cfg.shake) fx.TriggerShake(frand(rng, 1.0f, 3.2f));
        if (cfg.glitch) fx.TriggerGlitch(frand(rng, 0.12f, 0.35f));
        if (cfg.flash != L"low" && frand01(rng) < 0.10f) {
            float scale = cfg.flash == L"high" ? 1.2f : 1.0f;
            fx.TriggerFlash(frand(rng, 0.05f, 0.11f) * scale);
        }
        if (frand01(rng) < 0.08f) fx.TriggerBrownout(frand(rng, 0.10f, 0.22f));
    }

    int th = ThreatLevel();
    logTimer -= adt;
    if (logTimer <= 0.0f) {
        logTimer = th >= 3 ? frand(rng, 0.055f, 0.13f)
                           : (th >= 2 ? frand(rng, 0.12f, 0.28f) : frand(rng, 0.30f, 0.60f));
        int pack = (opIndex >= 0 && opIndex < 14) ? opIndex : 0;
        PushAutoLog(kPhaseCategories[pack][irand(rng, 0, 2)]);
        if (th >= 3 && frand01(rng) < 0.55f)
            PushAutoLog(kPhaseCategories[pack][irand(rng, 0, 2)]);
    }
    evidenceTimer -= adt;
    if (evidenceTimer <= 0.0f) {
        evidenceTimer = 0.55f;
        PushAutoLog(5);
        PushAutoLog(9);
    }
    burstTimer -= adt;
    if (burstTimer <= 0.0f) {
        burstTimer = frand(rng, 1.2f, 2.8f);
        int n = irand(rng, 3, 7);
        int pack = (opIndex >= 0 && opIndex < 14) ? opIndex : 0;
        for (int i = 0; i < n; i++)
            PushAutoLog(kPhaseCategories[pack][irand(rng, 0, 2)]);
    }

    if (cfg.autoPromptSeconds > 0 && !modal.open && !autoPrompted &&
        time > (double)cfg.autoPromptSeconds) {
        modal.Open();
        autoPrompted = true;
    }
}

void Sim::SetBeat(Beat b, float duration) {
    beat = b;
    beatTimer = 0.0f;
    beatDuration = duration;
    EnterBeat(b);
}

void Sim::EnterBeat(Beat b) {
    switch (b) {
    case Beat::Boot:
        PushLog(Severity::Info, L"BIOS", L"SENTINEL BIOS v4.9.0");
        PushLog(Severity::Info, L"BIOS", L"memory check 32768 MB -- OK");
        PushLog(Severity::Warn, L"NET", L"WARNING: anomalous egress traffic");
        break;

    case Beat::Calm:
        for (auto& fw : firewalls) fw.repairRate = 0.0f;
        PushLog(Severity::Info, L"SENTINEL", L"threat level guarded -- all layers nominal");
        break;

    case Beat::Probe: {
        int a = irand(rng, 0, 5);
        Attack(a, frand(rng, 4.0f, 9.0f), frand(rng, 4.0f, 8.0f));
        fx.TriggerGlitch(0.25f);
        PushLog(Severity::Warn, L"IDS", L"%d probes blocked -- source 198.51.100.%d",
                1, irand(rng, 2, 250));
        break;
    }

    case Beat::Breach:
        StartBreach();
        break;

    case Beat::Cascade:
        TriggerCascade();
        break;

    case Beat::Recovery:
        StartRecovery();
        break;

    case Beat::Aftermath:
        for (auto& fw : firewalls) {
            if (fw.state != FWState::Secure) {
                fw.SetRebuilding(fw.integrity, frand(rng, 1.5f, 2.8f));
            }
        }
        PushLog(Severity::Info, L"SENTINEL", L"PATCH APPLIED -- containment restored");
        fx.TriggerFlash(0.10f);
        fx.ShowBanner(L"CONTAINMENT RESTORED");
        break;
    }
}

void Sim::UpdateDirector(float dt) {
    beatTimer += dt;
    if (beatTimer < beatDuration) return;

    switch (beat) {
    case Beat::Boot:      SetBeat(Beat::Cascade, 9.0f); break;
    case Beat::Calm:      SetBeat(Beat::Breach, frand(rng, 7.0f, 11.0f)); break;
    case Beat::Probe:     SetBeat(Beat::Breach, frand(rng, 7.0f, 11.0f)); break;
    case Beat::Breach:    SetBeat(Beat::Cascade, frand(rng, 8.0f, 12.0f)); break;
    case Beat::Cascade:   SetBeat(Beat::Recovery, frand(rng, 8.0f, 12.0f)); break;
    case Beat::Recovery:  SetBeat(Beat::Breach, frand(rng, 7.0f, 11.0f)); break;
    case Beat::Aftermath: SetBeat(Beat::Cascade, frand(rng, 8.0f, 12.0f)); break;
    }
}

// ---------------------------------------------------------------------------
// v2.3 operation director: fourteen technical stages with a high-speed boundary engagement.
// It only schedules attacks, interceptions, repairs, scans and job panes.
// ---------------------------------------------------------------------------
static const wchar_t* kOpNames[14] = {
    L"RECON", L"INITIAL ACCESS", L"ESCALATION", L"CREDENTIAL ACCESS",
    L"BOUNDARY SCAN", L"INTERCEPT STORM", L"LATERAL MOVEMENT", L"PERSISTENCE",
    L"EXFILTRATION", L"CONTAINMENT", L"COUNTERATTACK", L"REBUILD", L"HARDENING", L"AFTERMATH"
};
static const float kOpMin[14] = { 24, 24, 25, 26, 22, 20, 25, 24, 26, 24, 24, 26, 22, 30 };
static const float kOpMax[14] = { 38, 38, 40, 42, 32, 30, 40, 38, 42, 38, 38, 40, 36, 44 };

const wchar_t* Sim::OpPhaseName() const {
    return kOpNames[(int)opPhase % 14];
}

const ActDef& Sim::CurrentAct() const {
    int i = (int)act;
    if (i < 0 || i >= 3) i = 0;
    return kActDefs[i];
}

void Sim::SetAct(Act a, float duration) {
    (void)duration;
    if (a == Act::Count) a = Act::Intrusion;
    act = a;
    actIndex = (int)act;
    actElapsed = 0.0f;
    if (autoTheme) theme = ThemeForPhase(opIndex);
    audioCue = 1;
    fx.TriggerTear(0.72f, 0.8f);
    fx.TriggerStatic(0.38f, 0.55f);
    fx.TriggerScreenDamage(0.42f);
}

void Sim::UpdateAct(float dt) {
    actElapsed += dt;
}

int Sim::DamageLevel() const {
    int base = 25;
    switch (opPhase) {
    case OpPhase::InitialAccess: base = 48; break;
    case OpPhase::Escalation: base = 62; break;
    case OpPhase::CredentialAccess: base = 68; break;
    case OpPhase::BoundaryScan: base = 78; break;
    case OpPhase::BoundaryPurge: base = 88; break;
    case OpPhase::LateralMovement: base = 72; break;
    case OpPhase::Persistence: base = 66; break;
    case OpPhase::Exfiltration: base = 82; break;
    case OpPhase::Counterattack: base = 90; break;
    case OpPhase::Containment: base = 38; break;
    case OpPhase::Rebuild: base = 28; break;
    case OpPhase::Hardening: base = 18; break;
    case OpPhase::Aftermath: base = 10; break;
    default: base = 24; break;
    }
    base += CountState(FWState::Offline) * 6;
    base += (int)(fx.screenDamage * 30.0f);
    if (base > 100) base = 100;
    return base;
}

void Sim::SetTheme(Theme t) {
    Theme next = (Theme)((int)t % (int)Theme::Count);
    if (next == Theme::Count) next = Theme::Terminal;
    if (theme != next) motion.TriggerTheme(cfg.motion == L"reduced");
    theme = next;
}

void Sim::NextTheme() {
    SetTheme((Theme)(((int)theme + 1) % (int)Theme::Count));
}

void Sim::SetGoal(AttackGoal g) {
    campaign.goal = g;
    campaign.dataMB = 0;
    campaign.encryptPct = 0;
    campaign.minerPct = 0;
    campaign.credentials = 0;
    campaign.documents = 0;
    campaign.servicesDown = 0;
    campaign.defense = DefenseStep::Detect;
    campaign.defenseProgress = 0;
    campaign.defenseStageTimer = 0;
    BuildSummary();
}

void Sim::NextGoal() {
    SetGoal((AttackGoal)(((int)campaign.goal + 1) % (int)AttackGoal::Count));
}

int Sim::FloatTarget() const {
    if (theme == Theme::BlackBox) return 0;
    int n = 1;
    switch (opPhase) {
    case OpPhase::CredentialAccess:
    case OpPhase::LateralMovement:
    case OpPhase::Exfiltration:
    case OpPhase::Counterattack:
        n = 3;
        break;
    case OpPhase::InitialAccess:
    case OpPhase::Escalation:
    case OpPhase::Persistence:
        n = 2;
        break;
    default:
        n = 1;
        break;
    }
    // The terminal has two dedicated dock cells; alternate layouts get one compact console.
    if (theme == Theme::Terminal && n > 2) n = 2;
    if (theme != Theme::Terminal && n > 1) n = 1;
    return n;
}

void Sim::BuildSummary() {
    const wchar_t* goal = L"Data Exfiltration · Targeted Intrusion";
    switch (campaign.goal) {
    case AttackGoal::Exfiltrate:        goal = L"Data Exfiltration · Targeted Intrusion"; break;
    case AttackGoal::Ransomware:        goal = L"Ransomware · Volume Encryption Activity"; break;
    case AttackGoal::CredentialTheft:   goal = L"Credential Dump · Privilege Escalation Prep"; break;
    case AttackGoal::CryptoMiner:       goal = L"Hashrate Hijack · Mining Payload"; break;
    case AttackGoal::Espionage:         goal = L"Long-Term Persistence · Covert Channel"; break;
    case AttackGoal::Sabotage:          goal = L"Availability Sabotage · Service Disruption"; break;
    default: break;
    }
    const wchar_t* step = L"DETECT";
    const wchar_t* stepCn = L"CHECK";
    switch (campaign.defense) {
    case DefenseStep::Detect:   step = L"DETECT";   stepCn = L"CHECK"; break;
    case DefenseStep::Isolate:  step = L"ISOLATE";  stepCn = L"ISOLATE"; break;
    case DefenseStep::Snapshot: step = L"SNAPSHOT"; stepCn = L"SNAPSHOT"; break;
    case DefenseStep::Rollback: step = L"ROLLBACK"; stepCn = L"ROLLBACK"; break;
    case DefenseStep::Rebuild:  step = L"REBUILD";  stepCn = L"REBUILD"; break;
    case DefenseStep::Verify:   step = L"VERIFY";   stepCn = L"VERIFY"; break;
    default: break;
    }
    wchar_t b0[160], b1[160], b2[160], b3[160], b4[160];
    swprintf_s(b0, L"THREAT // %ls", goal);
    if (campaign.goal == AttackGoal::Exfiltrate || campaign.goal == AttackGoal::Espionage)
        swprintf_s(b1, L"STATUS // EXFIL %.1f GB · C2 %ls", campaign.dataMB / 1024.0f, campaign.c2.c_str());
    else if (campaign.goal == AttackGoal::Ransomware)
        swprintf_s(b1, L"STATUS // VOLUME ENCRYPT %d%% · ROLLBACK POINTS %d", (int)campaign.encryptPct,
                   CountState(FWState::Rebuilding));
    else if (campaign.goal == AttackGoal::CredentialTheft)
        swprintf_s(b1, L"STATUS // CREDENTIALS %d SETS · PRIVILEGED SESSIONS 2", campaign.credentials);
    else if (campaign.goal == AttackGoal::CryptoMiner)
        swprintf_s(b1, L"STATUS // GPU %.0f%% · ANOMALOUS COMPUTE ACTIVE", campaign.minerPct);
    else if (campaign.goal == AttackGoal::Sabotage)
        swprintf_s(b1, L"STATUS // SERVICE INTERRUPTIONS %d · CORE AVAILABILITY DROP", campaign.servicesDown);
    else
        swprintf_s(b1, L"STATUS // PERSISTENCE %d ITEMS · COVERT CHANNELS 2", campaign.documents + 4);
    swprintf_s(b2, L"RESPONSE // %ls %d/6 · %ls %.0f%%", step,
               (campaign.defenseRuns % 6) + 1, stepCn, campaign.defenseProgress * 100.0f);
    int off = CountState(FWState::Offline);
    int reb = CountState(FWState::Rebuilding);
    swprintf_s(b3, L"DEFENSE // OFFLINE %d · REBUILD %d · INTEGRITY %d%%", off, reb, (int)AvgIntegrity());
    const wchar_t* advice = L"Expand isolation perimeter";
    if (modal.open) advice = L"Await human takeover confirmation";
    else if (act == Act::Countermeasure) advice = L"Verify snapshots and sessions after rollback";
    else if (opPhase == OpPhase::Exfiltration) advice = L"Seal egress and rotate credentials";
    else if (opPhase == OpPhase::CredentialAccess) advice = L"Reset all privileged sessions";
    else if (opPhase == OpPhase::LateralMovement) advice = L"Cut lateral authentication paths";
    swprintf_s(b4, L"ACTION // %ls", advice);
    summary[0] = b0; summary[1] = b1; summary[2] = b2; summary[3] = b3; summary[4] = b4;
}

void Sim::UpdateCampaign(float dt) {
    campaign.defenseStageTimer += dt;
    if (campaign.defenseStageTimer >= 1.8f) {
        campaign.defenseStageTimer = 0.0f;
        campaign.defense = (DefenseStep)(((int)campaign.defense + 1) % (int)DefenseStep::Count);
        campaign.defenseRuns++;
    }
    campaign.defenseProgress = clampf(campaign.defenseProgress + dt * 0.08f, 0.0f, 1.0f);
    switch (campaign.goal) {
    case AttackGoal::Exfiltrate:
        campaign.dataMB += dt * 12.0f;
        if (campaign.dataMB > 4096.0f) campaign.dataMB = 4096.0f;
        break;
    case AttackGoal::Ransomware:
        campaign.encryptPct = clampf(campaign.encryptPct + dt * 0.35f, 0.0f, 100.0f);
        break;
    case AttackGoal::CredentialTheft:
        if (frand01(rng) < dt * 0.6f) campaign.credentials = (std::min)(9999, campaign.credentials + 1);
        break;
    case AttackGoal::CryptoMiner:
        campaign.minerPct = clampf(campaign.minerPct + dt * 0.8f, 0.0f, 100.0f);
        break;
    case AttackGoal::Espionage:
        campaign.documents += (int)(dt * 1.5f);
        break;
    case AttackGoal::Sabotage:
        if (frand01(rng) < dt * 0.3f) campaign.servicesDown = (std::min)(12, campaign.servicesDown + 1);
        break;
    default:
        break;
    }
    summaryTimer -= dt;
    if (summaryTimer <= 0.0f) {
        summaryTimer = 0.35f;
        BuildSummary();
    }
}

bool Sim::ClickBox(float vx, float vy) {
    for (int i = (int)popups.items.size() - 1; i >= 0; i--) {
        Popup& p = popups.items[i];
        if (!p.active) continue;
        if (vx < p.x || vy < p.y || vx >= p.x + p.w || vy >= p.y + p.h) continue;
        focusBox = i;
        if (boxState[i].input.empty() && boxState[i].page > 2) boxState[i].page = 0;
        if (vy < p.y + 38.0f) {
            int tab = (int)((vx - (p.x + 8.0f)) / 72.0f);
            if (tab < 0) tab = 0;
            if (tab > 2) tab = 2;
            boxState[i].page = tab;
        }
        return true;
    }
    return false;
}

void Sim::AddBoxChar(wchar_t c) {
    if (focusBox < 0 || focusBox >= (int)boxState.size()) return;
    if (!popups.items[focusBox].active) { focusBox = -1; return; }
    if (c < 0x20 || c > 0x7E) return;
    if (boxState[focusBox].input.size() >= 80) return;
    boxState[focusBox].input.push_back(c);
}

bool Sim::BackspaceBox() {
    if (focusBox < 0 || focusBox >= (int)boxState.size()) return false;
    if (boxState[focusBox].input.empty()) return false;
    boxState[focusBox].input.pop_back();
    return true;
}

void Sim::ClearFocus() {
    focusBox = -1;
}

void Sim::SubmitBox() {
    if (focusBox < 0 || focusBox >= (int)boxState.size()) return;
    Popup& p = popups.items[focusBox];
    if (!p.active) { focusBox = -1; return; }
    auto& b = boxState[focusBox];
    if (!b.input.empty()) {
        wchar_t echo[200];
        swprintf_s(echo, L"root@sentinel:%ls# %ls", b.path.c_str(), b.input.c_str());
        p.lines.push_back(echo);
        wchar_t note[160];
        const wchar_t* msg;
        if (b.input == L"ls" || b.input == L"dir") msg = L"evidence/  kernel/  quarantine/  snapshots/";
        else if (b.input == L"whoami") msg = L"root";
        else if (b.input == L"help") msg = L"commands: ls whoami ps netstat repair";
        else msg = L"queued for analysis -- elevated queue full";
        swprintf_s(note, L"[!] %ls", msg);
        p.lines.push_back(note);
        b.input.clear();
        b.page = 0;
        while (p.lines.size() > 30) p.lines.pop_front();
        PushLog(Severity::Info, L"CMD", L"operator input queued -- %ls", msg);
    }
}

void Sim::SetOpPhase(OpPhase p, float duration) {
    opPhase = p;
    opIndex = (int)p;
    opTimer = 0.0f;
    opDuration = duration > 0.0f ? duration : frand(rng, kOpMin[opIndex], kOpMax[opIndex]);
    opActionTimer = frand(rng, 1.5f, 3.0f);
    EnterOpPhase(p);
}

void Sim::EnterOpPhase(OpPhase p) {
    opPhase = p;
    opIndex = (int)p;
    BuildPhaseBrief();
    audioCue = 1;
    motion.TriggerPhase(cfg.motion == L"reduced");

    auto setB = [&](Beat b, float d) {
        beat = b;
        beatTimer = 0.0f;
        beatDuration = d;
    };

    switch (p) {
    case OpPhase::Recon:
        setB(Beat::Calm, opDuration);
        PushAutoLog(0); PushAutoLog(0); PushAutoLog(10);
        break;
    case OpPhase::InitialAccess:
        setB(Beat::Probe, opDuration);
        Attack(irand(rng, 0, 5), frand(rng, 5.0f, 10.0f), frand(rng, 6.0f, 10.0f));
        PushAutoLog(1); PushAutoLog(0); PushAutoLog(10);
        fx.TriggerGlitch(0.25f);
        break;
    case OpPhase::Escalation:
        setB(Beat::Breach, opDuration);
        StartBreach();
        PushAutoLog(2); PushAutoLog(1); PushAutoLog(3);
        break;
    case OpPhase::CredentialAccess:
        setB(Beat::Breach, opDuration);
        Attack(0, frand(rng, 10.0f, 18.0f), frand(rng, 7.0f, 12.0f));
        Attack(4, frand(rng, 6.0f, 12.0f), frand(rng, 6.0f, 10.0f));
        files.DeleteRandom(*this);
        PushAutoLog(2); PushAutoLog(10); PushAutoLog(9);
        fx.TriggerShake(5.0f);
        break;
    case OpPhase::BoundaryScan:
        setB(Beat::Breach, opDuration);
        Attack(0, frand(rng, 8.0f, 14.0f), frand(rng, 5.0f, 8.0f));
        boundary.lockTimer = 0.0f;
        boundary.PushEvent(L"PHASE 05 // ORBITAL ACQUISITION ACCELERATED");
        PushAutoLog(3); PushAutoLog(6); PushAutoLog(1);
        break;
    case OpPhase::BoundaryPurge:
        setB(Beat::Cascade, opDuration);
        Attack(1, frand(rng, 7.0f, 12.0f), frand(rng, 4.0f, 7.0f));
        boundary.lockTimer = 0.0f;
        boundary.PushEvent(L"PHASE 06 // INTERCEPT STORM AUTHORIZED");
        PushAutoLog(6); PushAutoLog(7); PushAutoLog(3);
        break;
    case OpPhase::LateralMovement:
        setB(Beat::Breach, opDuration);
        for (int i = 0; i < 3; i++) {
            Attack(irand(rng, 0, 5), frand(rng, 3.0f, 7.0f), frand(rng, 5.0f, 9.0f));
        }
        files.DeleteRandom(*this);
        PushAutoLog(3); PushAutoLog(3); PushAutoLog(10);
        break;
    case OpPhase::Persistence:
        setB(Beat::Breach, opDuration);
        Attack(2, frand(rng, 4.0f, 9.0f), frand(rng, 6.0f, 10.0f));
        Attack(5, frand(rng, 4.0f, 8.0f), frand(rng, 5.0f, 9.0f));
        files.DeleteRandom(*this);
        PushAutoLog(4); PushAutoLog(4); PushAutoLog(9);
        break;
    case OpPhase::Exfiltration:
        setB(Beat::Cascade, opDuration);
        Attack(irand(rng, 0, 5), frand(rng, 8.0f, 15.0f), frand(rng, 7.0f, 12.0f));
        files.DeleteRandom(*this);
        files.DeleteRandom(*this);
        PushAutoLog(5); PushAutoLog(5); PushAutoLog(9);
        fx.TriggerShake(7.0f);
        break;
    case OpPhase::Containment:
        setB(Beat::Recovery, opDuration);
        StartRecovery();
        PushAutoLog(6); PushAutoLog(7); PushAutoLog(6);
        break;
    case OpPhase::Counterattack:
        setB(Beat::Cascade, opDuration);
        TriggerCascade();
        PushAutoLog(1); PushAutoLog(3); PushAutoLog(6);
        break;
    case OpPhase::Rebuild:
        setB(Beat::Recovery, opDuration);
        StartRecovery();
        PushAutoLog(7); PushAutoLog(7); PushAutoLog(8);
        break;
    case OpPhase::Hardening:
        setB(Beat::Aftermath, opDuration);
        for (auto& fw : firewalls) {
            if (fw.state != FWState::Secure) fw.SetRebuilding(fw.integrity, frand(rng, 1.8f, 3.0f));
        }
        PushAutoLog(8); PushAutoLog(7); PushAutoLog(10);
        break;
    case OpPhase::Aftermath:
        setB(Beat::Aftermath, opDuration);
        for (auto& fw : firewalls) fw.SetSecure(frand(rng, 86.0f, 100.0f));
        PushAutoLog(6); PushAutoLog(8); PushAutoLog(10);
        break;
    }

    Act wanted = ActForPhase(opIndex);
    if (wanted != act) SetAct(wanted, 1.4f);
    else if (autoTheme) SetTheme(ThemeForPhase(opIndex));
    int dmg = DamageLevel();
    if (dmg >= 40) {
        fx.TriggerTear((float)dmg / 125.0f, 0.65f);
        fx.TriggerStatic((float)dmg / 150.0f, 0.45f);
        fx.TriggerScreenDamage((float)dmg / 180.0f);
    }

    // every stage gets two fresh job panes; they spray their own text upward
    popups.SpawnJob(*this, 0);
    popups.SpawnJob(*this, 1);
    popups.floatingTarget = FloatTarget();
    for (int i = 0; i < popups.floatingTarget; i++) popups.SpawnFloating(*this);
}

void Sim::UpdateOpDirector(float dt) {
    opCycleElapsed += dt;
    opTimer += dt;
    if (opTimer >= opDuration) {
        int next = (opIndex + 1) % 14;
        if (next == 0) {
            opCycle++;
            opCycleElapsed = 0.0f;
            cycle++;
            SetGoal((AttackGoal)(((int)campaign.goal + 1) % (int)AttackGoal::Count));
            boundary.PushEvent(L"NEW INCIDENT CYCLE // THREAT CLASS ROTATED");
        }
        float d = frand(rng, kOpMin[next], kOpMax[next]);
        SetOpPhase((OpPhase)next, d);
        return;
    }

    opActionTimer -= dt;
    if (opActionTimer > 0.0f) return;
    opActionTimer = frand(rng, 2.2f, 5.0f);

    switch (opPhase) {
    case OpPhase::Recon:
        PushAutoLog(0); PushAutoLog(0); PushAutoLog(10);
        break;
    case OpPhase::InitialAccess:
        Attack(irand(rng, 0, 5), frand(rng, 4.0f, 8.0f), frand(rng, 4.0f, 8.0f));
        PushAutoLog(1); PushAutoLog(0);
        break;
    case OpPhase::Escalation:
        Attack(irand(rng, 0, 5), frand(rng, 5.0f, 10.0f), frand(rng, 4.0f, 8.0f));
        PushAutoLog(2); PushAutoLog(3);
        break;
    case OpPhase::CredentialAccess:
        Attack(0, frand(rng, 8.0f, 14.0f), frand(rng, 4.0f, 7.0f));
        files.DeleteRandom(*this);
        PushAutoLog(2); PushAutoLog(9);
        break;
    case OpPhase::BoundaryScan:
        boundary.lockTimer = (std::min)(boundary.lockTimer, 0.18f);
        PushAutoLog(3); PushAutoLog(6);
        break;
    case OpPhase::BoundaryPurge:
        boundary.lockTimer = 0.0f;
        PushAutoLog(6); PushAutoLog(7);
        break;
    case OpPhase::LateralMovement:
        Attack(irand(rng, 0, 5), frand(rng, 3.0f, 6.0f), frand(rng, 4.0f, 7.0f));
        PushAutoLog(3); PushAutoLog(3);
        break;
    case OpPhase::Persistence:
        files.DeleteRandom(*this);
        PushAutoLog(4); PushAutoLog(9);
        break;
    case OpPhase::Exfiltration:
        files.DeleteRandom(*this);
        Attack(irand(rng, 0, 5), frand(rng, 6.0f, 12.0f), frand(rng, 4.0f, 8.0f));
        PushAutoLog(5); PushAutoLog(5);
        break;
    case OpPhase::Containment:
        for (auto& fw : firewalls) {
            if (fw.state == FWState::Rebuilding) fw.repairRate = frand(rng, 2.0f, 3.5f);
        }
        PushAutoLog(6); PushAutoLog(7);
        break;
    case OpPhase::Counterattack:
        Attack(irand(rng, 0, 5), frand(rng, 5.0f, 12.0f), frand(rng, 4.0f, 8.0f));
        PushAutoLog(1); PushAutoLog(3);
        break;
    case OpPhase::Rebuild:
        for (auto& fw : firewalls) {
            if (fw.state == FWState::Rebuilding) fw.repairRate = frand(rng, 1.8f, 3.2f);
        }
        PushAutoLog(7); PushAutoLog(7);
        break;
    case OpPhase::Hardening:
        for (auto& fw : firewalls) {
            if (fw.state != FWState::Secure) fw.SetRebuilding(fw.integrity, frand(rng, 2.0f, 3.2f));
        }
        PushAutoLog(8); PushAutoLog(7);
        break;
    case OpPhase::Aftermath:
        PushAutoLog(6); PushAutoLog(8);
        break;
    }
}

void Sim::Attack(int idx, float rate, float duration) {
    if (idx < 0 || idx >= 6) return;
    Firewall& fw = firewalls[idx];
    if (fw.state == FWState::Offline) return;
    fw.attackRate = rate;
    fw.attackTimer = duration;
    fw.flash = (std::max)(fw.flash, 0.5f);
    fx.TriggerScreenDamage(0.10f + rate / 120.0f);
    fx.TriggerTear(0.18f + rate / 90.0f, 0.35f);
    for (auto& p : popups.items) {
        if (p.active && p.kind == 0) p.damage = (std::min)(1.0f, p.damage + 0.05f);
    }
    if (fw.state == FWState::Rebuilding) {
        // attacking a rebuilding layer knocks it back but does not instantly delete progress
        fw.integrity = (std::max)(0.0f, fw.integrity - rate * 0.35f);
    }
}

void Sim::StartBreach() {
    int a = irand(rng, 0, 5);
    int b = (a + irand(rng, 1, 5)) % 6;
    Attack(a, frand(rng, 8.0f, 16.0f), frand(rng, 8.0f, 14.0f));
    Attack(b, frand(rng, 6.0f, 12.0f), frand(rng, 6.0f, 12.0f));

    files.DeleteRandom(*this);
    injection.StartAttack();

    fx.TriggerShake(9.0f);
    fx.TriggerFlash(true, 0.10f, 0.40f);
    fx.TriggerErrorText(0.8f);
    fx.TriggerTakeover(Takeover::Warning, 1.2f);
    fx.TriggerGlitch(0.8f);
    fx.TriggerBrownout(0.25f);
    fx.TriggerTear(0.85f, 0.9f);
    fx.TriggerStatic(0.55f, 0.7f);
    fx.TriggerScreenDamage(0.55f);
    audioCue = 2;
    PushLog(Severity::Crit, L"L04", L"egress filter bypass -- payload executing");
}

void Sim::TriggerCascade() {
    cycle++;

    std::array<int, 6> idx = { 0, 1, 2, 3, 4, 5 };
    std::shuffle(idx.begin(), idx.end(), rng);

    int off1 = idx[0], off2 = idx[1];
    int reb1 = idx[2], reb2 = idx[3], reb3 = idx[4];
    int sec = idx[5];

    firewalls[off1].SetOffline();
    firewalls[off2].SetOffline();
    firewalls[off1].breachStart = frand(rng, 0.0f, 6.283f);
    firewalls[off2].breachStart = frand(rng, 0.0f, 6.283f);

    firewalls[reb1].SetRebuilding(frand(rng, 12.0f, 55.0f), frand(rng, 1.6f, 3.4f));
    firewalls[reb2].SetRebuilding(frand(rng, 12.0f, 55.0f), frand(rng, 1.6f, 3.4f));
    firewalls[reb3].SetRebuilding(frand(rng, 12.0f, 55.0f), frand(rng, 1.6f, 3.4f));

    firewalls[sec].SetSecure(frand(rng, 88.0f, 100.0f));

    PushLog(Severity::Crit, L"L01", L"CASCADE EVENT -- layer L%02d OFFLINE", off1 + 1);
    PushLog(Severity::Crit, L"L02", L"CASCADE EVENT -- layer L%02d OFFLINE", off2 + 1);
    PushLog(Severity::Warn, L"L03", L"rebuild started -- L%02d L%02d L%02d",
            reb1 + 1, reb2 + 1, reb3 + 1);
    PushLog(Severity::Info, L"L06", L"holding secure -- L%02d", sec + 1);

    // keep hammering the grid even while it rebuilds
    for (int k = 0; k < 3; k++) {
        Attack(irand(rng, 0, 5), frand(rng, 2.0f, 8.0f), frand(rng, 5.0f, 10.0f));
    }

    fx.TriggerShake(14.0f);
    fx.TriggerFlash(true, 0.12f, 0.55f);
    fx.TriggerErrorText(1.0f);
    fx.TriggerTakeover(Takeover::Failure, 1.6f);
    fx.TriggerBrownout(0.5f);
    fx.TriggerGlitch(1.0f);
    fx.TriggerTear(1.0f, 1.2f);
    fx.TriggerStatic(0.8f, 0.9f);
    fx.TriggerScreenDamage(0.85f);
    audioCue = 2;

    injection.StartAttack();
    files.DeleteRandom(*this);
}

void Sim::StartRecovery() {
    for (auto& fw : firewalls) {
        if (fw.state == FWState::Offline) {
            fw.SetRebuilding(0.0f, frand(rng, 1.6f, 3.0f));
        } else if (fw.state != FWState::Secure) {
            fw.SetRebuilding(fw.integrity, frand(rng, 1.6f, 3.0f));
        } else {
            fw.repairRate = 0.0f;
        }
    }
    files.RestoreRandom(*this);
    injection.StartRepair();

    // recovery is not peaceful -- the attacker keeps probing the rebuild
    for (int k = 0; k < 2; k++) {
        Attack(irand(rng, 0, 5), frand(rng, 1.5f, 5.0f), frand(rng, 4.0f, 8.0f));
    }

    fx.TriggerShake(4.5f);
    fx.TriggerFlash(0.18f);
    fx.TriggerGlitch(0.55f);
    fx.TriggerTear(0.58f, 0.55f);
    fx.TriggerStatic(0.30f, 0.45f);
    fx.screenDamage = (std::max)(0.0f, fx.screenDamage - 0.22f);
    audioCue = 4;
    PushLog(Severity::Info, L"SENTINEL", L"recovery protocol engaged -- %d layers rebuilding",
            CountState(FWState::Rebuilding));
    PushLog(Severity::Warn, L"NET", L"counterattack during rebuild -- patch queue rolled back");
}

void Sim::UpdateStats(float dt) {
    statTimer += dt;
    if (statTimer >= 1.0f) {
        statTimer -= 1.0f;
        packets += (uint64_t)irand(rng, 25000, 180000);
        blocked += (uint64_t)irand(rng, 20, 1200);
        if (ThreatLevel() >= 2) encrypted += (uint64_t)irand(rng, 0, 3);
    }
}

int Sim::ThreatLevel() const {
    int off = CountState(FWState::Offline);
    int reb = CountState(FWState::Rebuilding);
    int crit = CountState(FWState::Critical);
    float avg = AvgIntegrity();
    if (off >= 2 || avg < 35.0f) return 3;
    if (off >= 1 || crit >= 2 || avg < 60.0f) return 2;
    if (avg < 85.0f || beat == Beat::Breach || beat == Beat::Cascade) return 1;
    (void)reb;
    return 0;
}

int Sim::CountState(FWState st) const {
    int n = 0;
    for (const auto& fw : firewalls) if (fw.state == st) n++;
    return n;
}

float Sim::AvgIntegrity() const {
    float sum = 0.0f;
    for (const auto& fw : firewalls) sum += fw.integrity;
    return sum / 6.0f;
}

void Sim::OpenModal() {
    modal.Open();
    audioCue = 6;
}
