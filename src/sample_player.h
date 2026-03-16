#pragma once
#include <Arduino.h>
#include "samples.h"

// ============================================================================
// SAMPLE PLAYER MODULE
// Timer-based 8-bit PCM sample playback at 8000 Hz
// ============================================================================

// Sample selection modes
#define SAMPLE_MODE_SINGLE 0    // Always play selected sample
#define SAMPLE_MODE_SEQ 1       // Sequential: s00, s01, s02...
#define SAMPLE_MODE_RANDOM 2    // Random sample each trigger
#define SAMPLE_MODE_MAPPED 3    // GM drum map (note 35-58 mapped to samples)
#define SAMPLE_MODE_COUNT 4

// GM Drum note range (standard MIDI drums)
#define GM_DRUM_NOTE_MIN 35   // Acoustic Bass Drum
#define GM_DRUM_NOTE_MAX 58   // Vibraslap (24 notes total)

// GM Drum map - maps MIDI note (35-58) to sample index (0-23)
// Edit this array to assign samples to drum sounds:
//   Index 0  = Note 35 (Acoustic Bass Drum)
//   Index 1  = Note 36 (Bass Drum 1 / Kick)
//   Index 2  = Note 37 (Side Stick)
//   Index 3  = Note 38 (Acoustic Snare)
//   Index 4  = Note 39 (Hand Clap)
//   Index 5  = Note 40 (Electric Snare)
//   Index 6  = Note 41 (Low Floor Tom)
//   Index 7  = Note 42 (Closed Hi-Hat)
//   Index 8  = Note 43 (High Floor Tom)
//   Index 9  = Note 44 (Pedal Hi-Hat)
//   Index 10 = Note 45 (Low Tom)
//   Index 11 = Note 46 (Open Hi-Hat)
//   Index 12 = Note 47 (Low-Mid Tom)
//   Index 13 = Note 48 (Hi-Mid Tom)
//   Index 14 = Note 49 (Crash Cymbal 1)
//   Index 15 = Note 50 (High Tom)
//   Index 16 = Note 51 (Ride Cymbal 1)
//   Index 17 = Note 52 (Chinese Cymbal)
//   Index 18 = Note 53 (Ride Bell)
//   Index 19 = Note 54 (Tambourine)
//   Index 20 = Note 55 (Splash Cymbal)
//   Index 21 = Note 56 (Cowbell)
//   Index 22 = Note 57 (Crash Cymbal 2)
//   Index 23 = Note 58 (Vibraslap)
extern const uint8_t gmDrumMap[24];

// --- Polyphonic Sample Voices (3 voices on chip 2) ---
#define SAMPLE_VOICE_COUNT 3

struct SampleVoice {
  volatile bool playing;
  volatile uint32_t pos;        // 8.8 fixed-point position for pitch shifting
  volatile const uint8_t* data;
  volatile uint16_t length;
  uint16_t pitchIncrement;      // Per-voice pitch increment (8.8 fixed-point, 256=1.0x)
  uint8_t note;                 // MIDI note that triggered this voice
  uint8_t sampleIdx;            // Which sample index is playing
};

extern SampleVoice sampleVoices[SAMPLE_VOICE_COUNT];

// --- Section Selection ---
#define SAMPLE_SECTION_DRUMS 0
#define SAMPLE_SECTION_ONESHOTS 1
#define SAMPLE_SECTION_COUNT 3

extern uint8_t sampleSection;     // 0=DRUMS, 1=ONESHOTS

// --- Configuration ---
extern uint8_t sampleSelect;      // Selected sample within current section
extern uint8_t sampleMode;        // Selection mode
extern uint8_t sampleVolume;      // Volume scaling (1-15)
extern uint8_t sampleSeqIndex;    // Current index for SEQ mode
extern volatile uint8_t lastSampleNote;  // MIDI note of last triggered sample

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize sample player (starts timer)
void samplePlayerInit();

// Trigger a sample (note used for MAPPED mode)
void sampleTrigger(uint8_t note, uint8_t velocity);

// Stop current sample playback
void sampleStop();

// Enter/exit sample mode (configures chip 2 mixer)
void sampleModeEnter();
void sampleModeExit();

// Recompute pitch increment from pitch/octave params
void sampleUpdatePitchIncrement();

// Get sample mode name
const char* getSampleModeName(uint8_t mode);
