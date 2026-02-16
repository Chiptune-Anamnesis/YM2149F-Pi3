#pragma once
#include <Arduino.h>
#include "samples.h"

// ============================================================================
// SAMPLE PLAYER MODULE
// Timer-based 1-bit sample playback at 4000 Hz
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

// --- Sample Player State ---
extern volatile bool samplePlaying;
extern volatile uint16_t samplePos;
extern volatile const uint8_t* currentSample;
extern volatile uint16_t currentSampleLen;

// --- Configuration ---
extern uint8_t sampleSelect;      // Selected sample (0-23)
extern uint8_t sampleMode;        // Selection mode
extern uint8_t sampleVolume;      // Volume scaling (1-15)
extern uint8_t sampleSeqIndex;    // Current index for SEQ mode

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize sample player (starts timer)
void samplePlayerInit();

// Trigger a sample (note used for MAPPED mode)
void sampleTrigger(uint8_t note, uint8_t velocity);

// Stop current sample playback
void sampleStop();

// Get sample mode name
const char* getSampleModeName(uint8_t mode);
