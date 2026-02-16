#pragma once
#include <Arduino.h>
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "config.h"
#include "settings.h"
#include "effects.h"

// ============================================================================
// DUAL-CORE COMMUNICATION MODULE
// Core 0: Audio (MIDI, voice management, effects, YM2149 writes)
// Core 1: Display (OLED rendering, encoder, menu navigation)
// ============================================================================

// --- Snapshot ---
// Core 0 writes, Core 1 reads (via copy)
// This struct holds all state that Core 1 needs to render the display

struct DisplaySnapshot {
  // Voice state (for visualization)
  bool voiceActive[3][3];
  uint8_t voiceVol[3][3];
  uint8_t voiceNote[3][3];
  uint16_t voicePeriod[3][3];  // Actual YM2149 period (captures all pitch mods)

  // Envelope state (for envelope visualization)
  EnvStage envStage[9];
  float envLevel[9];

  // Mode and settings
  uint8_t polyMode;
  uint8_t linkMode;
  uint8_t voiceLinkMask;  // Which Chip 0 voices are linked
  VoiceSettings voiceSettings[9];

  // Pot state
  int potValues[3];
  PotAssignment potAssignments[3];

  // SID visualization state
  uint8_t sidWaveType;
  uint8_t sidDuty;

  // Vibrato visualization state
  float vibPhase[9];

  // FX chip state
  bool fxModeEnabled;
  uint8_t fxType;
  uint8_t fxRouting;
  uint16_t echoDelayMs;
  uint8_t echoRepeats;
  uint8_t echoDecay;
  uint8_t echoVolume;
  uint8_t arpPattern;
  uint16_t arpSpeedMs;
  uint8_t arpVolume;
  int8_t arpOctave;

  // Bit Crush state
  uint8_t bitCrushBits;
  uint8_t bitCrushRate;
  uint8_t bitCrushVolume;
  uint16_t bitCrushDuration;

  // Pseudo Reverb state
  uint8_t reverbTaps;
  uint8_t reverbSpacing;
  uint8_t reverbDecay;
  int8_t reverbDetune;
  uint8_t reverbVolume;

  // Chorus state
  int8_t chorusDetune1;
  int8_t chorusDetune2;
  uint8_t chorusVolume;
  uint16_t chorusDuration;

  // Sample state
  uint8_t sampleSelect;
  uint8_t sampleMode;
  uint8_t sampleVolume;
};

// --- Command Types ---
// Core 1 sends commands to Core 0 for state changes

enum CommandType : uint8_t {
  CMD_NONE = 0,

  // Mode changes
  CMD_SET_POLY_MODE,
  CMD_SET_LINK_MODE,
  CMD_SET_VOICE_LINK_MASK,  // Which Chip 0 voices participate in linking

  // Per-voice settings (uses chip/scope to determine target)
  CMD_SET_TARGET,       // Set currentTarget for subsequent commands
  CMD_SET_DETUNE,
  CMD_SET_OCTAVE,
  CMD_SET_VIB_ON,
  CMD_SET_VIB_RATE,
  CMD_SET_VIB_DEPTH,
  CMD_SET_VIB_DELAY,
  CMD_SET_NOISE_FREQ,
  CMD_SET_ENV_ATTACK,
  CMD_SET_ENV_DECAY,
  CMD_SET_ENV_SUSTAIN,
  CMD_SET_SID_ON,
  CMD_SET_SID_WAVE,
  CMD_SET_SID_DUTY,
  CMD_SET_MAX_VOLUME,
  CMD_SET_PORTA_ON,
  CMD_SET_PORTA_SPEED,
  CMD_SET_TREMOLO_ON,
  CMD_SET_TREMOLO_RATE,
  CMD_SET_TREMOLO_DEPTH,
  CMD_SET_PITCH_ENV_AMT,
  CMD_SET_PITCH_ENV_TIME,
  CMD_SET_PITCH_ENV_DIR,

  // Pot assignment
  CMD_SET_POT_ASSIGN,

  // Panic
  CMD_ALL_NOTES_OFF,

  // FX Chip commands
  CMD_SET_FX_ENABLED,
  CMD_SET_FX_TYPE,
  CMD_SET_FX_ROUTING,
  CMD_SET_ECHO_DELAY,
  CMD_SET_ECHO_REPEATS,
  CMD_SET_ECHO_DECAY,
  CMD_SET_ECHO_VOLUME,
  CMD_SET_ARP_PATTERN,
  CMD_SET_ARP_SPEED,
  CMD_SET_ARP_VOLUME,
  CMD_SET_ARP_OCTAVE,

  // Bit Crush commands
  CMD_SET_BIT_CRUSH_BITS,
  CMD_SET_BIT_CRUSH_RATE,
  CMD_SET_BIT_CRUSH_VOLUME,
  CMD_SET_BIT_CRUSH_DURATION,

  // Pseudo Reverb commands
  CMD_SET_REVERB_TAPS,
  CMD_SET_REVERB_SPACING,
  CMD_SET_REVERB_DECAY,
  CMD_SET_REVERB_DETUNE,
  CMD_SET_REVERB_VOLUME,

  // Chorus commands
  CMD_SET_CHORUS_DETUNE1,
  CMD_SET_CHORUS_DETUNE2,
  CMD_SET_CHORUS_VOLUME,
  CMD_SET_CHORUS_DURATION,

  // Sample commands
  CMD_SET_SAMPLE_SELECT,
  CMD_SET_SAMPLE_MODE,
  CMD_SET_SAMPLE_VOLUME,

  // Preset commands
  CMD_PRESET_LOAD,    // param1 = preset index
  CMD_PRESET_SAVE,    // param1 = user slot (0-119), name in presetNameCmd buffer
  CMD_PRESET_DELETE   // param1 = user slot (0-119)
};

// Command structure (8 bytes, queue-friendly)
struct Command {
  CommandType type;
  uint8_t param1;  // Primary parameter
  uint8_t param2;  // Secondary parameter
  uint8_t param3;  // Tertiary parameter (for pot assignments)
  int8_t  value;   // Signed value for things like detune
};

// --- Queue Configuration ---
#define CMD_QUEUE_SIZE 16

// --- Global State ---
// displaySnapshotCopy is updated by Core 1 by reading Core 0's state directly
extern DisplaySnapshot displaySnapshotCopy;
extern queue_t commandQueue;

// Shared buffer for preset name (Core 1 fills, Core 0 reads during save)
extern char presetNameCmd[9];

// Pending flash operation flags (Core 1 sets, Core 0 polls and clears)
// This avoids race conditions with command queue + idleOtherCore
extern volatile bool presetSavePending;
extern volatile uint8_t presetSaveSlot;
extern volatile bool presetDeletePending;
extern volatile uint8_t presetDeleteSlot;
extern volatile bool midiSavePending;

// Cooperative pause mechanism for flash operations
// Core 0 sets flashPauseRequested, Core 1 checks it and sets core1Paused when safe
// This ensures Core 1 isn't in the middle of I2C when idleOtherCore is called
extern volatile bool flashPauseRequested;
extern volatile bool core1Paused;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize dual-core communication (call from Core 0 setup)
void dualCoreInit();

// Update snapshot from current state (called from Core 1)
void updateSnapshot();

// Send a command from Core 1 to Core 0
bool sendCommand(CommandType type, uint8_t param1 = 0, uint8_t param2 = 0, uint8_t param3 = 0, int8_t value = 0);

// Process pending commands (call from Core 0 loop)
void processCommands();

// Core 1 entry point
void core1Entry();
