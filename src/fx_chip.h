#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// FX CHIP MODULE
// Chip 2 as dedicated effects processor when FX mode enabled
// ============================================================================

// --- FX Mode Constants ---
#define FX_CHIP 2  // Chip used for effects when FX mode enabled

// Effect types
#define FX_NONE 0
#define FX_ECHO 1
#define FX_ARP 2
#define FX_BIT_CRUSH 3
#define FX_PSEUDO_REVERB 4
#define FX_CHORUS 5
#define FX_HARMONIZER 6
#define FX_GATE 7
#define FX_TYPE_COUNT 8

// Harmonizer chord types
#define HARM_CHORD_OCTAVE  0
#define HARM_CHORD_POWER   1
#define HARM_CHORD_MAJOR   2
#define HARM_CHORD_MINOR   3
#define HARM_CHORD_SUS2    4
#define HARM_CHORD_SUS4    5
#define HARM_CHORD_AUG     6
#define HARM_CHORD_DIM     7
#define HARM_CHORD_MAJ7    8
#define HARM_CHORD_MIN7    9
#define HARM_CHORD_DOM7    10
#define HARM_CHORD_5OCT    11
#define HARM_CHORD_COUNT   12

// Arp patterns
#define ARP_UP 0
#define ARP_DOWN 1
#define ARP_UPDOWN 2
#define ARP_RANDOM 3
#define ARP_PATTERN_COUNT 4

// Gate patterns
#define GATE_SQUARE 0    // Hard on/off
#define GATE_RAMP 1      // Volume ramps down during on-period
#define GATE_TRI 2       // Volume ramps up then down
#define GATE_RANDOM 3    // Random on/off per step
#define GATE_PATTERN_COUNT 4

// FX Routing options
#define FX_ROUTE_ALL 0      // Apply FX to all voices
#define FX_ROUTE_CHIP0 1    // Only Chip 0 voices
#define FX_ROUTE_CHIP1 2    // Only Chip 1 voices
#define FX_ROUTE_0A 3       // Chip 0, Voice A
#define FX_ROUTE_0B 4       // Chip 0, Voice B
#define FX_ROUTE_0C 5       // Chip 0, Voice C
#define FX_ROUTE_1A 6       // Chip 1, Voice A
#define FX_ROUTE_1B 7       // Chip 1, Voice B
#define FX_ROUTE_1C 8       // Chip 1, Voice C
#define FX_ROUTE_COUNT 9

// --- FX Mode State ---
extern bool fxModeEnabled;
extern uint8_t fxType;           // Current effect type
extern uint8_t fxRouting;        // Which voices/chips FX applies to

// --- Echo Parameters ---
extern uint16_t echoDelayMs;     // Delay time in ms (50-2000)
extern uint8_t echoRepeats;      // Number of echo repeats (1-10)
extern uint8_t echoDecay;        // Volume reduction per repeat (1-15)
extern uint8_t echoVolume;       // Base volume (1-15)

// --- Arp Parameters ---
extern uint8_t arpPattern;       // UP/DOWN/UPDOWN/RANDOM
extern uint16_t arpSpeedMs;      // Ms per step (50-500)
extern uint8_t arpVolume;        // Base volume (1-15)
extern int8_t arpOctave;         // Octave offset (-2 to +2)

// --- Bit Crush Parameters ---
extern uint8_t bitCrushBits;       // Bit depth (1-4, lower = crunchier)
extern uint8_t bitCrushRate;       // Update rate divisor (1-10)
extern uint8_t bitCrushVolume;     // Volume (1-15)
extern uint16_t bitCrushDuration;  // Duration in ms (50-500)

// --- Pseudo Reverb Parameters ---
extern uint8_t reverbTaps;         // Number of echo taps (1-12)
extern uint8_t reverbSpacing;      // Base spacing in ms (10-250)
extern uint8_t reverbDecay;        // Volume decay per tap (1-8)
extern int8_t reverbDetune;        // Pitch variation per tap (-5 to +5 cents)
extern uint8_t reverbVolume;       // Starting volume (1-15)

// --- Chorus Parameters ---
extern int8_t chorusDetune1;       // Voice 1 max detune (-50 to +50 cents)
extern int8_t chorusDetune2;       // Voice 2 max detune (-50 to +50 cents)
extern uint8_t chorusVolume;       // Volume per voice (1-15)
extern uint8_t chorusRate;         // LFO rate in tenths of Hz (5-80 = 0.5-8.0Hz, 0=static)

// --- Harmonizer Parameters ---
extern uint8_t harmChord;          // Chord type (0-11)
extern uint8_t harmVolume;         // Harmony volume (1-15)
extern int8_t harmOctave;          // Octave shift (-2 to +2)

// --- Gate Parameters ---
extern uint16_t gateRateMs;        // Gate cycle period in ms (30-500)
extern uint8_t gatePattern;        // Gate shape (SQUARE/RAMP/TRI/RANDOM)
extern uint8_t gateVolume;         // Max volume (1-15)
extern uint8_t gateDuty;           // On-time fraction: 1-7 (out of 8)
extern uint8_t gateSeed;           // Random pattern seed (0-15)

// --- MIDI Clock Sync ---
#define FX_CLOCK_DIV_COUNT 8
#define FX_CLOCK_TIMEOUT_MS 2000  // Fallback to free-running if no clock for 2s
extern volatile uint32_t midiClockTick;    // Running tick counter
extern volatile uint32_t lastClockTime;    // millis() of last clock tick
extern volatile uint32_t prevClockTime;    // millis() of previous tick (for interval)
extern volatile bool midiClockRunning;     // True between Start and Stop
extern bool fxClockSync;                   // Global toggle (saved in GlobalSettings)

// Subdivision tick counts (24 PPQ)
static const uint8_t clockDivTicks[FX_CLOCK_DIV_COUNT] = {
  96, 48, 24, 12, 6, 3, 18, 8
};
// Subdivision display names
const char* getClockDivName(uint8_t div);

// --- Note History for Echo/Reverb ---
#define NOTE_HISTORY_SIZE 16
struct NoteEvent {
  uint8_t note;
  uint8_t velocity;
  uint32_t timestamp;
  uint8_t echoCount;    // How many echoes/reverb taps have played
  bool active;
};
extern NoteEvent noteHistory[NOTE_HISTORY_SIZE];

// --- Bit Crush State ---
struct CrushedNote {
  uint8_t originalNote;
  uint8_t crushedNote;
  uint8_t voice;
  bool active;
};
extern CrushedNote crushedNotes[3];
extern uint8_t crushUpdateCounter;

// --- Held Notes for Arp ---
#define MAX_HELD_NOTES 12
extern uint8_t heldNotes[MAX_HELD_NOTES];
extern uint8_t heldNoteVelocity[MAX_HELD_NOTES];
extern uint8_t heldNoteCount;
extern uint8_t arpIndex;
extern int8_t arpDirection;      // 1 = up, -1 = down (for UPDOWN)
extern uint32_t lastArpStep;

// --- FX Voice Management ---
extern uint8_t fxVoiceIdx;       // Round-robin allocation for FX chip

// Voice release timing (auto-release FX voices after duration)
#define FX_VOICE_DURATION_MS 80  // How long each FX voice plays before release
extern uint32_t fxVoiceStartTime[3];  // When each FX voice was triggered
extern bool fxVoiceActive[3];         // Which FX voices are currently active
extern uint8_t fxVoiceNote[3];        // What note each FX voice is playing (for visualization)
extern uint16_t fxVoicePeriod[3];     // Actual period written to FX chip (for visualization)

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize FX chip module
void fxInit();

// Update FX processing (call from Core 0 main loop)
void fxUpdate();

// Called when a note is played (for echo history and arp tracking)
void fxNoteOn(uint8_t note, uint8_t velocity, uint8_t chip, uint8_t voice);

// Called when a note is released
void fxNoteOff(uint8_t note);

// Set FX mode enabled/disabled
void fxSetEnabled(bool enabled);

// Set a voice on the FX chip
void setFxVoice(uint8_t note, uint8_t vol);

// Stop a voice on the FX chip
void stopFxVoice(uint8_t voice);

// Stop all FX voices
void stopAllFxVoices();

// Panic: stop all FX voices and clear all held notes/state (for MIDI Stop)
void fxPanic();

// Check if a note should be processed for FX based on routing
bool fxShouldProcess(uint8_t chip, uint8_t voice);

// Get effect type name
const char* getFxTypeName(uint8_t type);

// Get arp pattern name
const char* getArpPatternName(uint8_t pattern);

// Get routing name
const char* getFxRoutingName(uint8_t routing);

// Get harmonizer chord name
const char* getHarmChordName(uint8_t chord);

// Get gate pattern name
const char* getGatePatternName(uint8_t pattern);

// MIDI clock tick handler (called from MIDI handler on 0xF8)
void fxClockTick();

// Check if clock sync is actively running (sync enabled + clock recent)
bool fxClockActive();
