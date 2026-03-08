#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// EFFECTS MODULE
// Vibrato, portamento, pitch envelope, volume envelope, laser mode
// ============================================================================

// Fast math — replaces powf/sinf in audio hot paths (~50x faster on M0+)
float fastPow2Semi(float semitones);  // powf(2, semi/12) replacement
float fastSin01(float phase);         // sinf(phase * 2*PI) replacement, phase 0..1

// --- Per-Channel Modulation State ---
extern float modWheel[9];
extern float vibPhase[9];
extern float vibRate[16];
extern float vibRangeSemi[16];
extern unsigned long vibStartTime[9];
extern unsigned long vibLastTime[9];
extern uint16_t vibDelayMs[9];

// --- Pitch Bend & Envelope ---
extern float pitchBendSemis[9];
extern float pitchEnvPhase[9];
extern float pitchEnvIncrement[9];
extern float pitchEnvAmt[9];
extern uint8_t pitchEnvShape[9];

// --- Expression ---
extern uint8_t expressionVal[9];

// --- Portamento ---
extern bool portamentoOn[9];
extern float portamentoSpeed[9];

// --- Laser Mode ---
extern bool laserMode[9];
extern float laserAmt[9];
extern bool laserTriggered[3][3];

// --- CC4 Volume Envelope ---
extern uint8_t cc4Shape[9];
extern bool volEnvOn[9];
extern bool volEnvDir[9];
extern float volEnvPhase[9];
extern float volEnvIncrement[9];

// --- Per-Voice ADS Envelope ---
enum EnvStage {
  ENV_OFF = 0,
  ENV_ATTACK,
  ENV_DECAY,
  ENV_SUSTAIN,
  ENV_RELEASE
};
extern EnvStage envStage[9];      // Current envelope stage per voice
extern float envLevel[9];         // Current envelope level (0.0 - 1.0)
extern unsigned long envLastTime[9];  // Last update time for ADS envelope
extern unsigned long pitchEnvLastTime[9];  // Last update time for pitch envelope (separate to avoid timing conflicts)

// --- Per-Voice Tremolo (Volume LFO) ---
extern float tremoloPhase[9];
extern unsigned long tremoloLastTime[9];

// --- Per-Voice Pitch Envelope ---
extern float voicePitchEnvPhase[9];

// ============================================================================
// FUNCTIONS
// ============================================================================

// Update pitch modulation for a channel (vibrato, bend, portamento, etc.)
void updatePitchMod(uint8_t ch);

// Reset all controllers for a channel
void resetAllControllers(uint8_t ch);

// Trigger volume envelope based on CC4 shape
void triggerVolumeEnvelope(uint8_t ch);

// Trigger ADS envelope for a voice (called on note on)
void triggerADSEnvelope(uint8_t voiceIdx);

// Release ADS envelope for a voice (called on note off)
void releaseADSEnvelope(uint8_t voiceIdx);

// Initialize effects state
void effectsInit();

// Process all voice envelopes independently (call from main loop)
void updateAllEnvelopes();
