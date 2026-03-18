#pragma once
#include <Arduino.h>
#include "pico/time.h"
#include "config.h"

// ============================================================================
// SID EMULATION MODULE
// NextSID-style synchronized volume flipping on a dedicated chip
// ============================================================================

// --- Global SID Mode ---
// When sidModeGlobal is true, device operates as dedicated SID emulator
// using chips 1 and 2 (6 voices total). Chip 0 is skipped for speed.
// Per-chip duty cycle allows different timbres on each chip.
extern volatile bool sidModeGlobal;
extern uint8_t sidDutyChip[2];  // Duty cycle for chips 1 and 2 (0-15)

// --- SID Mode State ---
extern bool sidMode;
extern uint8_t sidEnvFreqCoarse;
extern uint8_t sidEnvFreqFine;
extern uint8_t sidEnvShape;

// --- Per-Voice SID State (NextSID-style) ---
// Phase accumulator approach for timing-drift-free volume flipping
// Even when timer callbacks are delayed, frequency remains accurate
struct SidVoiceState {
  uint16_t period;                // YM period (determines note frequency)
  volatile uint32_t phase;        // Phase accumulator (16.16 fixed point) - timer reads/writes
  volatile uint32_t phaseInc;     // Phase increment per timer callback - main loop writes, timer reads
  volatile bool isHigh;           // Current volume state (high or low) - timer reads/writes
  volatile uint8_t volume;        // Target volume when high (0-15) - main loop writes, timer reads
  volatile uint8_t duty;          // Per-voice duty cycle (0-15) - volatile for cross-core visibility
  volatile uint16_t dutyThreshold; // Pre-computed: duty << 12 (avoids division in ISR)
  volatile bool active;           // Voice is actively playing - main loop writes, timer reads
  // Extended SID features
  volatile uint8_t waveform;      // 0=square, 1=saw, 2=tri, 3=double-pulse
  volatile uint8_t lastWrittenVol; // Track last HW value to avoid redundant writes
  // PWM sweep (duty cycle LFO)
  volatile uint32_t pwmPhase;     // LFO phase accumulator - timer reads/writes
  volatile uint32_t pwmPhaseInc;  // LFO rate - main loop writes, timer reads
  volatile uint8_t pwmDepth;      // 0-15 duty modulation depth - main loop writes, timer reads
  // Hard sync and ring modulation
  volatile uint8_t syncSource;    // 0=off, 1-2=voice index within chip trio (0-based)
  volatile uint8_t ringSource;    // 0=off, 1-2=voice index within chip trio (0-based)
  volatile bool noiseOn;          // Mix YM noise into this voice
};

extern SidVoiceState sidState[6];  // 6 voices: [0-2]=chip1, [3-5]=chip2

// --- Legacy Duty Cycle Setting (kept for compatibility) ---
// Use sidDutyChip[2] for per-chip duty in global SID mode
extern volatile uint8_t sidDuty;

// --- Legacy variables (kept for compatibility during transition) ---
extern volatile bool sidPWM;
extern volatile uint8_t sidWaveType;
extern volatile int8_t sidDetune;
extern volatile uint8_t sidPattern[SID_MAX_PATTERN];
extern volatile uint16_t sidPhase[3][3];
extern volatile uint16_t sidPhaseInc[3][3];
extern volatile uint16_t sidPeriod[3][3];
extern volatile uint8_t sidVoiceVol[3][3];
extern volatile bool sidVoiceOn[3][3];
extern volatile bool ymBusBusy;

// --- Timer ---
extern volatile bool sidTimerActive;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Update SID timing for a voice when period or duty changes
// Called when note starts or pitch modulation occurs
void updateSidTiming(uint8_t voice, uint16_t period);

// Start a SID voice (sets up timing and activates)
void sidVoiceStart(uint8_t voice, uint16_t period, uint8_t volume);

// Stop a SID voice
void sidVoiceStop(uint8_t voice);

// Timer ISR for synchronized volume flipping (RAM-resident, bypasses flash alarm pool)
void sidTimerISR();

// Start SID timer
void sidTimerStart();

// Stop SID timer
void sidTimerStop();

// Pause SID timer (for flash reads from Core 1 - doesn't actually stop timer)
void sidTimerPause();

// Resume SID timer after flash read
void sidTimerResume();

// Set up SID envelope for a chip (HW envelope mode)
void setupSidEnvelope(uint8_t chip);

// Initialize SID mode
void sidInit();

// --- Global SID Mode Functions (6-voice, chips 1+2) ---
// Initialize global SID mode (disable tone generators, start timer)
void sidModeInit();

// Stop global SID mode (re-enable tone generators, stop timer)
void sidModeStop();

// Start/stop a voice in global SID mode (voiceIdx: 0-5)
void sidVoiceStartGlobal(uint8_t voiceIdx, uint16_t period, uint8_t volume);
void sidVoiceStopGlobal(uint8_t voiceIdx);

// Update timing for a voice in global SID mode
void updateSidTimingGlobal(uint8_t voiceIdx, uint16_t period);

// --- Legacy functions (kept for compatibility) ---
uint16_t calcPhaseInc(uint16_t period);
void sidGeneratePattern();
