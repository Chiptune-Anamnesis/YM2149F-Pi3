#pragma once
#include <Arduino.h>
#include "pico/time.h"
#include "config.h"

// ============================================================================
// SID EMULATION MODULE
// Software PWM and hardware envelope-based SID-like timbres
// ============================================================================

// --- SID Mode State ---
extern bool sidMode;
extern uint8_t sidEnvFreqCoarse;
extern uint8_t sidEnvFreqFine;
extern uint8_t sidEnvShape;

// --- Software PWM State ---
extern volatile bool sidPWM;
extern volatile uint8_t sidDuty;
extern volatile uint8_t sidWaveType;
extern volatile int8_t sidDetune;
extern volatile uint8_t sidPattern[SID_MAX_PATTERN];

// --- Per-voice state for phase-locked modulation ---
extern volatile uint16_t sidPhase[3][3];
extern volatile uint16_t sidPhaseInc[3][3];
extern volatile uint16_t sidPeriod[3][3];
extern volatile uint8_t sidVoiceVol[3][3];
extern volatile bool sidVoiceOn[3][3];
extern volatile bool ymBusBusy;

// --- Timer ---
extern struct repeating_timer sidTimer;
extern volatile bool sidTimerActive;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Calculate phase increment for a given period
uint16_t calcPhaseInc(uint16_t period);

// Generate waveform pattern based on type and duty length
void sidGeneratePattern();

// Timer callback for software PWM
bool sidTimerCallback(struct repeating_timer *t);

// Start SID timer
void sidTimerStart();

// Stop SID timer
void sidTimerStop();

// Set up SID envelope for a chip (HW or SW mode)
void setupSidEnvelope(uint8_t chip);

// Initialize SID mode
void sidInit();
