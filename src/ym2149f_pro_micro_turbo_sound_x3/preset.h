#pragma once
#include <Arduino.h>
#include "settings.h"

// ============================================================================
// PRESET STORAGE MODULE
// Factory presets (read-only, in firmware) + User presets (read-write, in flash)
// ============================================================================

// Flash storage location (modified in linker script)
#define PRESET_FLASH_BASE 0x101F7000
#define PRESET_FLASH_SIZE 32768
#define PRESET_SECTOR_SIZE 4096
#define PRESET_USER_SLOTS 120
#define PRESET_FACTORY_COUNT 8
#define PRESET_NAME_LEN 8

// Magic and version for validation
#define PRESET_MAGIC 0x594D33    // "YM3"
#define PRESET_HEADER_MAGIC 0x594D3348  // "YM3H"
#define PRESET_VERSION 1

// Preset flags
#define PRESET_FLAG_USED 0x01
#define PRESET_FLAG_EMPTY 0x00

// ============================================================================
// PRESET DATA STRUCTURE (270 bytes per preset)
// ============================================================================

struct PresetData {
  // Header (14 bytes)
  uint32_t magic;           // 0x594D33 = "YM3"
  uint8_t version;          // Structure version
  uint8_t flags;            // 0x01 = used, 0x00 = empty
  char name[PRESET_NAME_LEN]; // 8-char name (null-padded)

  // Voice settings (198 bytes = 22 bytes × 9 voices)
  VoiceSettings voices[9];

  // Pot assignments (12 bytes = 4 bytes × 3 pots)
  PotAssignment pots[3];

  // FX state (28 bytes)
  bool fxEnabled;
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
  uint8_t bitCrushBits;
  uint8_t bitCrushRate;
  uint8_t bitCrushVolume;
  uint16_t bitCrushDuration;
  uint8_t reverbTaps;
  uint8_t reverbSpacing;
  uint8_t reverbDecay;
  int8_t reverbDetune;
  uint8_t reverbVolume;
  int8_t chorusDetune1;
  int8_t chorusDetune2;
  uint8_t chorusVolume;
  uint16_t chorusDuration;

  // SID mode (4 bytes)
  bool sidMode;
  uint8_t sidEnvFreqCoarse;
  uint8_t sidEnvFreqFine;
  uint8_t sidEnvShape;

  // Sample player (4 bytes)
  uint8_t sampleSelect;
  uint8_t sampleMode;
  uint8_t sampleVolume;
  uint8_t sampleSeqIndex;

  // Global settings (5 bytes)
  uint8_t polyMode;
  uint8_t linkMode;
  uint8_t voiceLinkMask;
  bool chipLink[3];  // Note: bool[3] = 3 bytes

  // Checksum (2 bytes)
  uint16_t crc16;

  // Padding to ensure alignment (2 bytes)
  uint8_t reserved[2];
};

// ============================================================================
// STORAGE HEADER (16 bytes, stored at start of preset region)
// ============================================================================

struct PresetHeader {
  uint32_t magic;         // 0x594D3348 = "YM3H"
  uint8_t version;        // Storage format version
  uint8_t activePreset;   // Currently loaded preset (0xFF = none, 0-7 = factory, 8+ = user)
  uint8_t presetCount;    // Number of saved user presets
  uint8_t midiSynthChannel; // 0-15 or 0xFF for OMNI
  uint8_t midiDrumChannel;  // 0-15 (default 9 = channel 10)
  uint8_t reserved[7];    // Future use
};

// ============================================================================
// PRESET TYPE IDENTIFICATION
// ============================================================================

// Combined preset index: 0-7 = factory, 8-127 = user (0-119 mapped to U001-U120)
#define PRESET_INDEX_NONE 0xFF
#define PRESET_IS_FACTORY(idx) ((idx) < PRESET_FACTORY_COUNT)
#define PRESET_IS_USER(idx) ((idx) >= PRESET_FACTORY_COUNT && (idx) < (PRESET_FACTORY_COUNT + PRESET_USER_SLOTS))
#define PRESET_TO_USER_SLOT(idx) ((idx) - PRESET_FACTORY_COUNT)
#define USER_SLOT_TO_PRESET(slot) ((slot) + PRESET_FACTORY_COUNT)

// ============================================================================
// GLOBAL STATE
// ============================================================================

extern uint8_t currentPresetIndex;  // 0xFF = no preset loaded

// Factory presets (initialized at runtime with voice layering)
extern PresetData factoryPresets[PRESET_FACTORY_COUNT];
extern const char* factoryPresetNames[PRESET_FACTORY_COUNT];

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize preset system (reads header from flash)
void presetInit();

// Save current synth state to user slot (0-119)
bool presetSaveUser(uint8_t userSlot, const char* name);

// Load preset by combined index (0-7 factory, 8+ user)
bool presetLoad(uint8_t presetIndex);

// Delete user preset (mark as empty)
bool presetDeleteUser(uint8_t userSlot);

// Check if user slot is used
bool presetUserIsUsed(uint8_t userSlot);

// Get preset name (copies to buffer, max 8 chars + null)
void presetGetName(uint8_t presetIndex, char* buf);

// Get total preset count (factory + used user slots)
uint8_t presetGetTotalCount();

// Capture current synth state into PresetData struct
void presetCaptureCurrent(PresetData& p);

// Apply PresetData to synth state
void presetApplyCurrent(const PresetData& p);

// Calculate CRC16 for preset data
uint16_t presetCalcCRC(const PresetData& p);

// Low-level flash operations (called from Core 0 only)
void presetWriteFlash(uint32_t offset, const uint8_t* data, size_t len);
void presetEraseSlot(uint8_t userSlot);

// Save global settings (MIDI channels) to flash header
void saveGlobalSettings();
