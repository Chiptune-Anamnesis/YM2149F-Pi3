#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// VOICE MANAGER MODULE
// Voice allocation, polyphony modes, and per-voice state
// ============================================================================

// --- Polyphony Mode ---
// 0 = Semi-poly (chip-based), 1 = Full poly (global), 2 = Mono (1:1)
extern uint8_t polyMode;

// --- Unison Mode ---
extern bool unisonMode;
extern float unisonDetuneCents;

// --- Per-Voice State ---
extern bool voiceActive[3][3];
extern uint8_t voiceNote[3][3];
extern uint8_t voiceChan[3][3];
extern uint8_t voiceVol[3][3];
extern float voiceDetune[3][3];
extern uint8_t nextVoice[3];
extern float curPeriod[3][3];

// --- Sustain/Release ---
extern bool sustainOn[9];
extern bool pendingRelease[3][3];

// --- Portamento tracking ---
// Last played period per channel (for gliding FROM the previous note)
extern float lastPortaPeriod[9];

// --- SID Mode Voice Tracking ---
// Note tracking for 6 SID voices (for visualization and note-off matching)
extern uint8_t sidVoiceNote[6];

// --- LED Flash Timers ---
extern unsigned long ledOnTime[2];

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize note-to-period lookup table (call once at startup)
void initNotePeriodLUT();

// Convert MIDI note to YM2149 period (uses precomputed LUT)
uint16_t noteToPeriod(uint8_t note);

// Set voice parameters (period, volume) with SID mode support
void setVoice(uint8_t chip, uint8_t v, uint16_t per, uint8_t vol);

// Stop a voice
void stopVoice(uint8_t chip, uint8_t v);

// Enable tones on a chip
void enableTones(uint8_t chip);

// Bus-safe YM write (blocks ISR)
void ymSafeWrite(uint8_t chip, uint8_t reg, uint8_t val);

// Note on handler
void noteOn(uint8_t ch, uint8_t note, uint8_t vel);

// Note off handler
void noteOff(uint8_t ch, uint8_t note);

// All notes off for a channel
void allNotesOffChannel(uint8_t ch);

// Global panic: kill all voices
void allNotesOffPanic();

// Initialize voice manager
void voiceManagerInit();
