#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// SETTINGS MODULE
// Per-chip and global configuration with pot mapping
// ============================================================================

// --- Target Selection ---
enum SettingsTarget {
  TARGET_ALL = 0,
  TARGET_CHIP0,
  TARGET_CHIP1,
  TARGET_CHIP2,
  TARGET_V1,
  TARGET_V2,
  TARGET_V3,
  TARGET_V4,
  TARGET_V5,
  TARGET_V6,
  TARGET_V7,
  TARGET_V8,
  TARGET_V9,
  TARGET_COUNT
};

// --- Pot Parameter Categories ---
// Organize 50+ parameters into logical groups for easy navigation
enum PotCategory : uint8_t {
  PCAT_OFF = 0,        // Disabled
  PCAT_VOICE,          // Detune, Octave, MaxVol, NoiseFreq, PortaSpeed
  PCAT_VIBRATO,        // VibRate, VibDepth, VibDelay
  PCAT_ENVELOPE,       // Attack, Decay, Sustain
  PCAT_TREMOLO,        // TremRate, TremDepth
  PCAT_PITCH_ENV,      // PitchAmt, PitchTime, PitchDir
  PCAT_SID,            // SidWave, SidDuty, SidDetune
  PCAT_FX_ECHO,        // EchoDelay, EchoRepeats, EchoDecay, EchoVol
  PCAT_FX_ARP,         // ArpSpeed, ArpPattern, ArpVol, ArpOctave
  PCAT_FX_CRUSH,       // Bits, Rate, Volume
  PCAT_FX_REVERB,      // Taps, Spacing, Decay, Detune, Vol
  PCAT_FX_CHORUS,      // Detune1, Detune2, Volume
  PCAT_SAMPLE,         // Select, Mode, Volume
  PCAT_GLOBAL,         // Expression, PolyMode
  PCAT_COUNT
};

// Parameter counts per category
#define PCAT_VOICE_PARAMS 5
#define PCAT_VIBRATO_PARAMS 3
#define PCAT_ENVELOPE_PARAMS 3
#define PCAT_TREMOLO_PARAMS 2
#define PCAT_PITCH_ENV_PARAMS 3
#define PCAT_SID_PARAMS 3
#define PCAT_FX_ECHO_PARAMS 4
#define PCAT_FX_ARP_PARAMS 4
#define PCAT_FX_CRUSH_PARAMS 3
#define PCAT_FX_REVERB_PARAMS 5
#define PCAT_FX_CHORUS_PARAMS 3
#define PCAT_SAMPLE_PARAMS 3
#define PCAT_GLOBAL_PARAMS 2

// Target value for "no targeting" (global/FX params)
#define TARGET_NONE 0xFF

// Full pot assignment (4 bytes per pot)
struct PotAssignment {
  PotCategory category;   // Which category
  uint8_t paramIndex;     // Which param within category (0-based)
  uint8_t target;         // TARGET_ALL..TARGET_V9, or TARGET_NONE for FX
  uint8_t reserved;       // Future use (curve type, etc)
};

// --- Per-Voice Settings ---
struct VoiceSettings {
  int8_t detuneCents;     // -50 to +50 cents
  int8_t octaveShift;     // -3 to +3 octaves
  uint8_t vibOn;          // 0=off, 1-127=intensity (bypasses mod wheel)
  uint8_t vibRateTenths;  // 10-150 (1.0-15.0 Hz in tenths)
  uint8_t vibDepthCents;  // 0-200 cents (2 semitones max)
  uint8_t vibDelay;       // 0-255 delay before vibrato starts (x10ms = 0-2550ms)
  uint8_t noiseFreq;      // 0-31 noise frequency (per-chip, voice 0 controls)
  uint8_t envAttack;      // 0-127 attack time
  uint8_t envDecay;       // 0-127 decay time
  uint8_t envSustain;     // 0-127 sustain level
  uint8_t sidOn;          // 0=off, 1=on (enables SID waveform for this voice)
  uint8_t sidWave;        // 0=square, 1=saw, 2=triangle, 3=pulse
  uint8_t sidDuty;        // 4-16 pattern length (affects timbre)
  uint8_t maxVolume;      // 0-15, caps voice output volume
  uint8_t portaOn;        // 0=off, 1=on (enables portamento/glide)
  uint8_t portaSpeed;     // 0-127 glide speed (0=slow, 127=fast)
  // Tremolo (volume LFO)
  uint8_t tremoloOn;      // 0=off, 1-127=intensity
  uint8_t tremoloRate;    // 10-150 (1.0-15.0 Hz in tenths)
  uint8_t tremoloDepth;   // 0-100 (percentage of volume swing)
  // Pitch envelope
  uint8_t pitchEnvAmt;    // 0-24 semitones
  uint8_t pitchEnvTime;   // 0-127 time (speed of sweep)
  uint8_t pitchEnvDir;    // 0=down (start high), 1=up (start low)
};

// --- Global Settings State ---
extern VoiceSettings voiceSettings[9];
extern SettingsTarget currentTarget;
extern PotAssignment potAssignments[3];  // 3 pots with flexible category/param/target

// --- Chip Link Settings ---
// When enabled, all 3 voices on a chip play the same note (with individual settings)
extern bool chipLink[3];

// Global link mode: 0=OFF, 1=CH1, 2=CH2, 3=ALL (CH1+CH2)
// When linked, chips inherit settings from Chip 0
extern uint8_t linkMode;
#define LINK_OFF  0
#define LINK_CH1  1
#define LINK_CH2  2
#define LINK_ALL  3

// Voice link mask: which Chip 0 voices participate in linking
// Bits 0,1,2 = voices A,B,C (default 0b111 = all linked)
extern uint8_t voiceLinkMask;
#define VOICE_LINK_A  0x01
#define VOICE_LINK_B  0x02
#define VOICE_LINK_C  0x04
#define VOICE_LINK_ALL 0x07

// --- Pot Reading State ---
extern int potValues[3];       // Current pot ADC values (smoothed)
extern int potLastValues[3];   // Last values for change detection

// --- MIDI Channel Settings ---
// 0-15 for channels 1-16, 0xFE = OFF (disabled), 0xFF = OMNI (all channels)
#define MIDI_CHANNEL_OFF  0xFE
#define MIDI_CHANNEL_OMNI 0xFF
extern uint8_t midiSynthChannel;   // Channel for synth notes (default: OMNI)
extern uint8_t midiDrumChannel;    // Channel for drum/sample trigger (default: 9 = ch 10)

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize settings to defaults
void settingsInit();

// Get voice range for current target
// Sets start and end (exclusive) voice indices, returns true if valid
bool getTargetVoices(uint8_t &start, uint8_t &end);

// Apply a setting change to target voice(s)
void applyDetune(int8_t cents);
void applyOctave(int8_t octaves);
void applyVibOn(uint8_t intensity);
void applyVibRate(uint8_t rate);
void applyVibDepth(uint8_t depth);
void applyVibDelay(uint8_t delay);
void applyNoiseFreq(uint8_t freq);
void applyVolume(uint8_t vol);
void applyEnvAttack(uint8_t attack);
void applyEnvDecay(uint8_t decay);
void applyEnvSustain(uint8_t sustain);
void applySidOn(uint8_t on);
void applySidWave(uint8_t wave);
void applySidDuty(uint8_t duty);
void applyMaxVolume(uint8_t vol);
void applyPortaOn(uint8_t on);
void applyPortaSpeed(uint8_t speed);
void applyTremoloOn(uint8_t intensity);
void applyTremoloRate(uint8_t rate);
void applyTremoloDepth(uint8_t depth);
void applyPitchEnvAmt(uint8_t amt);
void applyPitchEnvTime(uint8_t time);
void applyPitchEnvDir(uint8_t dir);
void applyChipLink(uint8_t chip, bool enabled);
void applyLinkMode(uint8_t mode);
void applyVoiceLinkMask(uint8_t mask);
void syncLinkedChipSettings();  // Copy Chip 0 settings to linked chips

// Read pots and apply mapped parameters
void updatePots();

// Apply a pot value based on assignment
void applyPotValue(const PotAssignment& assign, uint8_t value);

// Get category name for display (max 8 chars)
const char* getPotCategoryName(PotCategory cat);

// Get parameter name within category (max 6 chars)
const char* getPotParamName(PotCategory cat, uint8_t idx);

// Get number of parameters in a category
uint8_t getPotParamCount(PotCategory cat);

// Check if category requires voice targeting
bool categoryRequiresTarget(PotCategory cat);

// Get target name for display
const char* getTargetName(SettingsTarget target);
