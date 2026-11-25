#pragma once

// Small, allocation-free motion director. Values are normalized timers so the
// renderer can compose transitions without owning any simulation state.
struct MotionState {
    float elapsed = 0.0f;
    float phase = 0.0f;
    float phaseDuration = 1.15f;
    float theme = 0.0f;
    float themeDuration = 0.75f;
    float pulse = 0.0f;

    void Reset();
    void Update(float dt, bool reduced);
    void TriggerPhase(bool reduced);
    void TriggerTheme(bool reduced);
    void TriggerPulse(float amount = 1.0f);

    float PhaseProgress() const;
    float ThemeProgress() const;
};
