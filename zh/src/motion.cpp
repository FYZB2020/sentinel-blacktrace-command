#include "motion.h"
#include "common.h"

void MotionState::Reset() {
    elapsed = 0.0f;
    phase = 0.0f;
    phaseDuration = 1.15f;
    theme = 0.0f;
    themeDuration = 0.75f;
    pulse = 0.0f;
}

void MotionState::Update(float dt, bool reduced) {
    elapsed += dt;
    phase = (std::max)(0.0f, phase - dt);
    theme = (std::max)(0.0f, theme - dt);
    pulse = (std::max)(0.0f, pulse - dt * (reduced ? 3.5f : 1.65f));
}

void MotionState::TriggerPhase(bool reduced) {
    phaseDuration = reduced ? 0.28f : 1.15f;
    phase = phaseDuration;
    TriggerPulse(reduced ? 0.35f : 0.85f);
}

void MotionState::TriggerTheme(bool reduced) {
    themeDuration = reduced ? 0.20f : 0.75f;
    theme = themeDuration;
}

void MotionState::TriggerPulse(float amount) {
    pulse = (std::max)(pulse, clampf(amount, 0.0f, 1.0f));
}

float MotionState::PhaseProgress() const {
    if (phase <= 0.0f || phaseDuration <= 0.0f) return 1.0f;
    return clampf(1.0f - phase / phaseDuration, 0.0f, 1.0f);
}

float MotionState::ThemeProgress() const {
    if (theme <= 0.0f || themeDuration <= 0.0f) return 1.0f;
    return clampf(1.0f - theme / themeDuration, 0.0f, 1.0f);
}
