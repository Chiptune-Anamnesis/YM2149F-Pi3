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
#define PCAT_ENVELOPE_PARAMS 4
#define PCAT_TREMOLO_PARAMS 2
#define PCAT_PITCH_ENV_PARAMS 3
#define PCAT_SID_PARAMS 6
#define PCAT_FX_ECHO_PARAMS 4
#define PCAT_FX_ARP_PARAMS 4
#define PCAT_FX_CRUSH_PARAMS 3
#define PCAT_FX_REVERB_PARAMS 5
#define PCAT_FX_CHORUS_PARAMS 4
#define PCAT_SAMPLE_PARAMS 6
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
  uint8_t sidPwmRate;     // 0-127 PWM sweep LFO rate (was sidOn)
  uint8_t sidWave;        // 0=square, 3=pulse (saw/triangle disabled)
  uint8_t sidDuty;        // 0-15 duty cycle (0=6%, 8=50%, 15=94%)
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
  // SID extended params
  uint8_t sidPwmDepth;    // 0-15 how much duty varies during PWM sweep
  uint8_t sidSync;        // 0=off, 1-3=sync to voice A/B/C within chip
  uint8_t sidRing;        // 0=off, 1-3=ring mod with voice A/B/C within chip
  uint8_t sidNoise;       // 0=off, 1=on (mix YM noise into this voice)
  uint8_t envRelease;     // 0-127 release time
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
// Note: volatile required because these are modified by Core 1 (display) and read by Core 0 (MIDI)
extern volatile uint8_t midiSynthChannel;   // MIDI channel for active mode (default: OMNI)

// --- MIDI Channel Routing ---
// Remaps incoming MIDI channels to internal channels before routing decisions.
// midiChannelRemap[X] = Y means incoming channel X is treated as internal channel Y.
// Default: identity mapping (0→0, 1→1, ..., 15→15)
// Use 0xFF to indicate "no remap" (pass through as-is)
#define MIDI_REMAP_NONE 0xFF
extern volatile uint8_t midiChannelRemap[16];

// --- USB Mode Setting ---
// 0 = USB-MIDI (default), 1 = USB-Serial (debug/YMPlayer)
// Takes effect on reboot
#define USB_MODE_MIDI   0
#define USB_MODE_SERIAL 1
extern uint8_t usbMode;

// --- Device Mode (YM / SID / SMPL) ---
// Only one mode active at a time
extern volatile bool sidModeGlobal;    // SID emulator mode (chips 1+2)
extern volatile bool sampleModeGlobal; // Sample playback mode (chip 2, 3 voices)
extern uint8_t sidDutyChip[2];  // Per-chip duty cycle: [0]=chip1, [1]=chip2 (0-15)

// --- Per-Sample Pitch + Length ---
// Each of the 64 samples (24 drums + 24 oneshots + 16 digi) has independent settings
#define TOTAL_SAMPLE_COUNT 64

extern int8_t  samplePitchArr[TOTAL_SAMPLE_COUNT];   // -12 to +12 semitones per sample
extern int8_t  sampleOctaveArr[TOTAL_SAMPLE_COUNT];   // -3 to +3 octave shift per sample
extern uint8_t sampleLengthArr[TOTAL_SAMPLE_COUNT];   // 1-127 playback length per sample
extern uint8_t sampleCrushArr[TOTAL_SAMPLE_COUNT];    // 0-7 bitcrush level per sample

// Convert (section, sampleIdx) to flat array index
inline uint8_t sampleFlatIndex(uint8_t section, uint8_t idx) {
  if (section == 0) return idx;          // 0-23  (drums)
  if (section == 1) return 24 + idx;     // 24-47 (oneshots)
  return 48 + idx;                        // 48-63 (digi)
}

// --- Display Brightness ---
#define DISPLAY_BRIGHTNESS_DEFAULT 200
extern uint8_t displayBrightness;  // 0-255 OLED contrast level

// --- Pot Default Assignments ---
// Boot defaults for pots (persisted to flash via GlobalSettings)
extern PotAssignment potDefaultAssignments[3];

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
void applySidWave(uint8_t wave);
void applySidDuty(uint8_t duty);
void applySidPwmRate(uint8_t rate);
void applySidPwmDepth(uint8_t depth);
void applySidNoise(uint8_t on);
void applyEnvRelease(uint8_t release);
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
