#pragma once
#include "common.h"
#include "motion.h"

enum class Severity { Info, Warn, Crit };
enum class LogStream { Both, Attack, Defense, Evidence, Ledger };
enum class Beat { Boot, Calm, Probe, Breach, Cascade, Recovery, Aftermath };
enum class FWState { Secure, Degraded, Critical, Offline, Rebuilding };
enum class FileStatus { Ok, Modified, Deleted, Restored };

// v2.3 operation director: fourteen technical stages, including two boundary-intercept phases.
enum class OpPhase {
    Recon, InitialAccess, Escalation, CredentialAccess, BoundaryScan, BoundaryPurge,
    LateralMovement, Persistence, Exfiltration, Containment, Counterattack, Rebuild,
    Hardening, Aftermath, Count
};

enum class Theme { Terminal, SplitOps, Boundary, WarRoom, Forensic, Containment, BlackBox, Count };
enum class AttackGoal { Exfiltrate, Ransomware, CredentialTheft, CryptoMiner, Espionage, Sabotage, Count };
enum class DefenseStep { Detect, Isolate, Snapshot, Rollback, Rebuild, Verify, Count };
enum class Takeover { None, Warning, Failure, Disconnected };

// v2.3: three acts group fourteen technical stages, including boundary interception.
enum class Act { Intrusion, Escalation, Countermeasure, Count };

struct ActDef {
    const wchar_t* tag;
    const wchar_t* cn;
    Theme firstTheme;
    Theme secondTheme;
    COLORREF accent;
};

struct Sim;

struct LogEntry {
    std::wstring time;
    std::wstring module;
    std::wstring text;
    Severity sev = Severity::Info;
    LogStream stream = LogStream::Both;
    float age = 0.0f;
    bool corrupt = false;
    bool ghost = false;
    float corruptTimer = 0.0f;
};

struct EventLog {
    std::deque<LogEntry> entries;
    void Push(Severity sev, const std::wstring& module, const std::wstring& text,
              LogStream stream = LogStream::Both, bool ghost = false);
    void Update(float dt);
};

struct NodeInfo {
    std::wstring name;
    std::wstring ip;
    std::wstring role;
    float risk = 0.0f;
    bool online = true;
};

// v2.3 BOUNDARY: persistent high-speed orbital targets and two independent text wires.
enum class BoundaryTargetState { Tracked, Locked, Purging, Respawn };
struct BoundaryTarget {
    float angle = 0.0f;
    float stateTimer = 0.0f;
    int generation = 0;
    uint32_t signature = 0;
    BoundaryTargetState state = BoundaryTargetState::Tracked;
};
struct BoundaryState {
    std::array<BoundaryTarget, 14> targets;
    std::deque<std::wstring> stream;
    std::deque<std::wstring> events;
    float lockTimer = 0.35f;
    float streamTimer = 0.0f;
    float scroll = 0.0f;
    uint64_t denied = 0;
    int purges = 0;
    int serial = 0;
    int lockedTarget = -1;

    void Init(Sim& sim);
    void Update(float dt, Sim& sim);
    void PushEvent(const std::wstring& line);
};

// v1.8: occasional ghost quote, rendered as loose dim text over the wall.
struct GhostQuote {
    bool active = false;
    int index = 0;
    float x = 0.0f;
    float y = 0.0f;
    float life = 0.0f;
    float dur = 4.0f;
    int reveal = 0;
    int start = 0;
};

struct Firewall {
    int id = 0;
    std::wstring name;
    float integrity = 100.0f;
    float display = 100.0f;
    FWState state = FWState::Secure;
    float attackRate = 0.0f;
    float attackTimer = 0.0f;
    float repairRate = 0.0f;
    float stateTimer = 0.0f;
    float jitterPhase = 0.0f;
    float flash = 0.0f;
    float breachStart = 0.0f;
    float breachWidth = 0.0f;

    void Update(float dt, Sim& sim);
    void SetOffline();
    void SetRebuilding(float startIntegrity, float rate);
    void SetSecure(float value);
};

struct MonitoredFile {
    std::wstring path;
    std::wstring hash;
    std::wstring size;
    FileStatus status = FileStatus::Ok;
    float progress = 0.0f;
    float timer = 0.0f;
};

struct FileMonitor {
    std::array<MonitoredFile, 6> files;
    void Init();
    void Update(float dt, Sim& sim);
    int DeleteRandom(Sim& sim);
    int RestoreRandom(Sim& sim);
};

struct Injection {
    int phase = 0;              // 0 idle, 1 inject, 2 corrupt, 3 repair
    float timer = 0.0f;
    float payloadVisible = 0.0f;
    float repairVisible = 0.0f;

    void StartAttack();
    void StartRepair();
    void Update(float dt, Sim& sim);
};

struct RainColumn {
    float x = 0.0f;
    float y = 0.0f;
    float speed = 30.0f;
    float length = 16.0f;
    unsigned char glyph[32]{};
    float mutateTimer = 0.0f;
    bool red = false;
};

struct Rain {
    std::vector<RainColumn> cols;
    void Init(int w, int h, std::mt19937& rng, float density);
    void Update(float dt, Sim& sim);
};

struct Effects {
    float shake = 0.0f;
    float flash = 0.0f;
    float flashDuration = 0.0f;
    bool flashRed = true;
    float brownout = 0.0f;
    float glitch = 0.0f;
    float errorTextTimer = 0.0f;
    float tear = 0.0f;
    float tearTimer = 0.0f;
    float tearDuration = 0.0f;
    float staticNoise = 0.0f;
    float staticTimer = 0.0f;
    float staticDuration = 0.0f;
    float screenDamage = 0.0f;
    float actCurtainTimer = 0.0f;
    float actCurtainDuration = 0.0f;
    int   actCurtainAct = 0;
    Takeover takeover = Takeover::None;
    float takeoverTimer = 0.0f;
    float takeoverDuration = 0.0f;
    uint32_t glitchSeed = 0x7F3A1984u;
    float scanTimer = 3.0f;
    float scanY = -100.0f;
    std::wstring banner;
    float bannerT = 0.0f;
    float bannerDur = 3.0f;

    void TriggerShake(float a) { (void)a; /* v1.5: shake disabled */ }
    void TriggerFlash(bool red, float duration, float intensity);
    void TriggerFlash(float a) { TriggerFlash(true, 0.09f, a); }
    void TriggerErrorText(float duration) { errorTextTimer = (std::max)(errorTextTimer, duration); }
    void TriggerTakeover(Takeover t, float duration);
    void TriggerBrownout(float a) { brownout = (std::max)(brownout, a); }
    void TriggerGlitch(float a) { glitch = (std::max)(glitch, a); }
    void TriggerTear(float intensity, float duration);
    void TriggerStatic(float intensity, float duration);
    void TriggerScreenDamage(float amount);
    void TriggerActCurtain(int act, float duration);
    void ShowBanner(const std::wstring& s) { banner = s; bannerT = bannerDur; }
    void Update(float dt);
};

struct Campaign {
    AttackGoal goal = AttackGoal::Exfiltrate;
    float dataMB = 0.0f;
    float encryptPct = 0.0f;
    float minerPct = 0.0f;
    int credentials = 0;
    int documents = 0;
    int servicesDown = 0;
    DefenseStep defense = DefenseStep::Detect;
    float defenseProgress = 0.0f;
    float defenseStageTimer = 0.0f;
    int defenseRuns = 0;
    std::wstring c2 = L"203.0.113.77";
};

struct ConsoleBoxState {
    int page = 0;                 // 0 LOG, 1 FILES, 2 JOB
    std::wstring input;
    std::wstring path = L"/home";
    int treeSelection = 0;
};

struct Popup {
    bool active = false;
    int id = 0;
    Severity sev = Severity::Warn;
    std::wstring title;
    std::wstring body;            // legacy, unused by v1.3 renderer
    std::wstring tag;
    std::deque<std::wstring> lines;
    float x = 0.0f;
    float y = 0.0f;
    float w = 412.0f;
    float h = 310.0f;
    float life = 0.0f;
    float dur = 10.0f;
    float slide = 0.0f;
    float lineTimer = 0.0f;
    float progress = 0.0f;
    float abortAt = 1.0f;
    std::wstring job;
    int kind = 0;                 // 0 attack pane, 1 defense pane
    bool aborted = false;
    float damage = 0.0f;          // v1.6: window damage 0..1
};

struct PopupSystem {
    std::array<Popup, 5> items;   // 0,1 fixed panes; 2..4 floating consoles
    float timer = 0.5f;
    int nextId = 1;
    int floatingTarget = 1;
    int floatingCount = 0;

    void Clear();
    void SpawnJob(Sim& sim, int kind);
    void SpawnFloating(Sim& sim);
    void Update(float dt, Sim& sim);
};

struct OverrideModal {
    bool open = false;
    std::wstring input;
    int attempts = 0;
    float countdown = 300.0f;
    float lockout = 0.0f;
    int phase = 0;              // 0 input, 1 success, 2 fail
    float phaseTimer = 0.0f;
    float bodyReveal = 0.0f;

    void Open();
    void Close();
    void Add(wchar_t c);
    void Backspace();
    void Submit(Sim& sim);
    void Update(float dt, Sim& sim);
};

struct Sim {
    Config cfg;
    std::mt19937 rng;
    double time = 0.0;
    std::array<Firewall, 6> firewalls;
    EventLog log;
    FileMonitor files;
    Injection injection;
    Rain rain;
    Effects fx;
    PopupSystem popups;
    OverrideModal modal;

    Beat beat = Beat::Boot;
    float beatTimer = 0.0f;
    float beatDuration = 5.0f;
    int cycle = 0;

    OpPhase opPhase = OpPhase::Counterattack;
    int opIndex = (int)OpPhase::Counterattack;
    int opCycle = 0;
    float opTimer = 0.0f;
    float opDuration = 14.0f;
    float opActionTimer = 3.0f;
    float opCycleElapsed = 0.0f;
    Act act = Act::Intrusion;
    int actIndex = 0;
    float actElapsed = 0.0f;
    Theme theme = Theme::Terminal;
    bool autoTheme = true;
    Campaign campaign;
    std::wstring summary[5];
    float summaryTimer = 0.0f;
    std::array<ConsoleBoxState, 5> boxState;
    int focusBox = -1;
    std::deque<std::wstring> recentLines;
    std::deque<std::wstring> recentTemplates;
    std::array<std::deque<std::wstring>, 6> layerFeed;
    std::array<NodeInfo, 14> nodes;
    BoundaryState boundary;
    std::array<std::wstring, 3> phaseBrief;

    // v1.8: configurable ambient sound + external quote bank.
    int audioCue = -1;
    float audioTimer = 0.5f;
    float nodeTimer = 2.5f;
    std::vector<std::wstring> quotes;
    std::array<GhostQuote, 3> ghosts;
    float quoteTimer = 5.0f;
    MotionState motion;

    // Music metadata is supplied by App; screenshot and benchmark modes stay silent.
    std::wstring musicTrack;
    int musicIndex = 0;
    int musicCount = 0;

    uint64_t totalAutoLines = 0;
    uint64_t uniqueAutoLines = 0;
    int contentSerial = 0;
    int duplicateCount = 0;
    float layerFeedTimer = 1.0f;
    float evidenceTimer = 0.6f;
    bool exitRequested = false;
    bool debugOverlay = false;
    bool allowClose = false;
    std::wstring toast;
    float toastTimer = 0.0f;
    float logTimer = 0.0f;
    float statTimer = 0.0f;
    float chaosTimer = 0.6f;
    float burstTimer = 1.0f;
    bool autoPrompted = false;
    uint64_t packets = 1284553;
    uint64_t blocked = 0;
    uint64_t encrypted = 0;
    float sysCpuPct = 0.0f;
    float procCpuPct = 0.0f;
    float memPct = 0.0f;
    float procMemMB = 0.0f;
    std::vector<std::wstring> debugLines;

    float bootLineTimer = 0.0f;
    int bootLineIndex = 0;

    void Init(const Config& c, uint32_t seed);
    void Update(float dt);
    void SetBeat(Beat b, float duration);
    void EnterBeat(Beat b);
    void UpdateDirector(float dt);          // legacy, no longer called by Update
    void SetOpPhase(OpPhase p, float duration);
    void EnterOpPhase(OpPhase p);
    void UpdateOpDirector(float dt);
    const wchar_t* OpPhaseName() const;
    void SetAct(Act a, float duration);
    void UpdateAct(float dt);
    const ActDef& CurrentAct() const;
    int DamageLevel() const;
    void SetTheme(Theme t);
    void NextTheme();
    void SetGoal(AttackGoal g);
    void NextGoal();
    void UpdateCampaign(float dt);
    void BuildSummary();
    int FloatTarget() const;
    bool ClickBox(float vx, float vy);
    void AddBoxChar(wchar_t c);
    bool BackspaceBox();
    void SubmitBox();
    void ClearFocus();
    std::wstring UniqueLine(const std::wstring& s);
    void PushAutoLog(int category);
    std::wstring NextPaneLine(int kind, int phase);
    void BuildPhaseBrief();
    void UpdateLayerFeeds(float dt);
    void LoadQuotes(const std::wstring& path);
    void UpdateQuotes(float dt);
    LogStream StreamForCategory(int category) const;
    void UpdateStats(float dt);
    void TriggerCascade();
    void StartBreach();
    void StartRecovery();
    void PushLog(Severity sev, const wchar_t* module, const wchar_t* fmt, ...);
    void Attack(int idx, float rate, float duration);
    int ThreatLevel() const;
    int CountState(FWState st) const;
    float AvgIntegrity() const;
    void OpenModal();
    void RequestExit() { exitRequested = true; }
};
