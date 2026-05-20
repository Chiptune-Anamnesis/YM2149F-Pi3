#pragma once
#include <Arduino.h>
#include "settings.h"

// ============================================================================
// PRESET STORAGE MODULE
// Factory presets (read-only, in firmware) + User presets (read-write, in flash)
// ============================================================================

// ============================================================================
// FLASH LAYOUT (28KB total = 7 sectors)
// Each section has dedicated sectors to avoid cross-section read-modify-write
// ============================================================================
//
// Section 0: YM Presets  (12KB = 3 sectors) @ 0x101F7000
// Section 1: SID Presets (12KB = 3 sectors) @ 0x101FA000
// Section 2: Settings    (4KB = 1 sector)   @ 0x101FD000
//
#define PRESET_FLASH_BASE 0x101F7000
#define PRESET_FLASH_SIZE 28672       // 28KB total
#define PRESET_SECTOR_SIZE 4096

// YM Presets section (12KB)
#define YM_PRESET_FLASH_OFFSET 0
#define YM_PRESET_FLASH_SIZE 12288    // 12KB = 3 sectors
#define PRESET_USER_SLOTS 21          // 12KB / sizeof(PresetData), verified by static_assert
#define PRESET_FACTORY_COUNT 8
#define PRESET_NAME_LEN 8

// SID Presets section (12KB) - starts after YM presets
#define SID_PRESET_FLASH_OFFSET 12288   // 12KB offset
#define SID_PRESET_FLASH_SIZE 12288     // 12KB = 3 sectors

// Settings section (4KB) - starts after SID presets
#define SETTINGS_FLASH_OFFSET 24576     // 24KB offset (12KB + 12KB)
#define SETTINGS_FLASH_SIZE 4096        // 4KB = 1 sector
#define SETTINGS_FLASH_BASE (PRESET_FLASH_BASE + SETTINGS_FLASH_OFFSET)
#define SETTINGS_MAGIC 0x594D5347  // "YMSG"
#define SETTINGS_VERSION 8  // Version 8: added velocity curve setting

// Magic and version for validation
#define PRESET_MAGIC 0x594D33    // "YM3"
#define PRESET_HEADER_MAGIC 0x594D3348  // "YM3H"
#define PRESET_VERSION 2

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

  // Voice settings (9 voices)
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
  uint8_t chorusRate;   // LFO rate in tenths of Hz (0-80)
  uint8_t chorusReserved; // Padding (was uint16_t, now uint8_t to match variable)

  // Harmonizer (3 bytes)
  uint8_t harmChord;
  uint8_t harmVolume;
  int8_t harmOctave;

  // Gate (6 bytes)
  uint16_t gateRateMs;
  uint8_t gatePattern;
  uint8_t gateVolume;
  uint8_t gateDuty;
  uint8_t gateSeed;

  // SID mode (4 bytes)
  bool sidMode;
  uint8_t sidEnvFreqCoarse;
  uint8_t sidEnvFreqFine;
  uint8_t sidEnvShape;

  // Sample player (4 bytes)
  uint8_t sampleSelect;
  uint8_t sampleMode;
  uint8_t sampleSection;   // 0=DRUMS, 1=ONESHOTS
  uint8_t sampleVolume;
  uint8_t sampleSeqIndex;

  // Per-sample pitch + length + crush (256 bytes = 64 samples × 4 bytes each)
  int8_t  samplePitchArr[TOTAL_SAMPLE_COUNT];
  int8_t  sampleOctaveArr[TOTAL_SAMPLE_COUNT];
  uint8_t sampleLengthArr[TOTAL_SAMPLE_COUNT];
  uint8_t sampleCrushArr[TOTAL_SAMPLE_COUNT];

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
  uint32_t magic;         // 0x594D3348 = "YM3H" (legacy, for migration)
  uint8_t version;        // Storage format version
  uint8_t activePreset;   // Currently loaded preset (0xFF = none, 0-7 = factory, 8+ = user)
  uint8_t presetCount;    // Number of saved user presets
  uint8_t midiSynthChannel; // 0-15 or 0xFF for OMNI
  uint8_t midiDrumChannel;  // 0-15 (default 9 = channel 10)
  uint8_t usbMode;        // 0 = USB-MIDI (default), 1 = USB-Serial
  uint8_t reserved[6];    // Future use
};

// Global settings structure - stored at end of flash, separate from presets
struct GlobalSettings {
  uint32_t magic;           // 0x594D5347 = "YMSG"
  uint8_t version;          // Settings format version
  uint8_t midiSynthChannel; // 0-15, 0xFE=OFF, 0xFF=OMNI
  uint8_t sampleModeGlobal; // 0=off, 1=SMPL mode active (replaces midiDrumChannel)
  uint8_t usbMode;          // 0=USB-MIDI, 1=USB-Serial
  uint8_t activePreset;     // Currently loaded preset
  uint8_t vizMode;          // 0=BARS, 1=SCOPE, 2=MATRIX
  uint8_t sidModeGlobal;    // 0=YM mode, 1=SID mode (dedicated SID emulator)
  uint8_t midiChannelRemap[16]; // MIDI channel routing (0xFF=no remap, 0-15=remap to that channel)
  uint8_t sidDutyChip[2];   // Per-chip duty cycle: [0]=chip1, [1]=chip2 (0-15)
  uint8_t displayBrightness; // OLED brightness (0-255, default 200)
  PotAssignment potDefaults[3]; // Default pot assignments on boot (12 bytes)
  uint8_t clkSync;            // MIDI clock sync for FX (0=off, 1=on)
  uint8_t velocityCurve;     // Velocity curve index (0-10, maps to gamma 0.5-3.0)
};

// ============================================================================
// SID PRESETS (stored in dedicated 12KB section)
// ============================================================================

// SID presets now store full voice settings for all 6 SID voices
// 4 factory + 16 user = 20 total
#define SID_PRESET_FACTORY_COUNT 4
#define SID_PRESET_USER_COUNT 16
#define SID_PRESET_TOTAL (SID_PRESET_FACTORY_COUNT + SID_PRESET_USER_COUNT)
#define SID_PRESET_NAME_LEN 8

// Version marker to distinguish new format from old (old had duty1 at offset 8)
// Using 0xF2 since duty1 in old format was 0-15, so this is clearly new format
#define SID_PRESET_VERSION_FULL 0xF3  // Bumped for VoiceSettings expansion (5 new SID fields)

// SID preset structure (~150 bytes) - full voice settings for 6 SID voices
// Maps to voiceSettings[3-8] (chips 1 and 2)
struct SidPreset {
  // Header (12 bytes)
  char name[SID_PRESET_NAME_LEN];  // 8 chars (null-terminated if <8)
  uint8_t version;                 // SID_PRESET_VERSION_FULL (0xF2) for new format
  uint8_t flags;                   // 0x01 = used
  uint8_t polyMode;                // 0=mono, 1=poly, 2=semi-poly
  uint8_t reserved1;               // Alignment/future use

  // Per-voice settings for 6 SID voices
  // Voice 0-2 = Chip 1 (voiceSettings[3-5])
  // Voice 3-5 = Chip 2 (voiceSettings[6-8])
  VoiceSettings voices[6];

  // Reserved for future expansion (4 bytes)
  uint8_t reserved2[4];
};

#define SID_PRESET_FLAG_USED 0x01

// Current SID preset index (0xFF = custom/not from preset)
extern uint8_t currentSidPreset;

// Factory SID presets (initialized at runtime)
extern SidPreset factorySidPresets[SID_PRESET_FACTORY_COUNT];

// Initialize factory SID presets (called from presetInit)
void initFactorySidPresets();

// Initialize factory YM presets (called from presetInit)
void initFactoryPresets();

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

// Low-level flash operations (called from Core 1 before I2C, idles Core 0)
void presetWriteFlash(uint32_t offset, const uint8_t* data, size_t len);
void presetEraseSlot(uint8_t userSlot);

// Save global settings (MIDI channels, USB mode) to flash header
void saveGlobalSettings();

// Read USB mode from flash (for early init before presetInit)
uint8_t readUsbModeFromFlash();

// --- SID Preset Functions ---

// Load SID preset by index (0-3 = factory, 4-11 = user)
bool sidPresetLoad(uint8_t presetIndex);

// Save current SID settings to user slot (0-7)
bool sidPresetSaveUser(uint8_t userSlot, const char* name);

// Delete user SID preset
bool sidPresetDeleteUser(uint8_t userSlot);

// Check if user SID slot is used
bool sidPresetUserIsUsed(uint8_t userSlot);

// Get SID preset name (copies to buffer, max 8 chars + null)
void sidPresetGetName(uint8_t presetIndex, char* buf);

// Get total SID preset count (factory + used user slots)
uint8_t sidPresetGetTotalCount();

// ============================================================================
// SMPL PRESETS (stored in sector 1 of SID region)
// Section-agnostic: stores per-sample pitch/octave/length for all 64 slots
// ============================================================================

#define SMPL_PRESET_USER_COUNT 16
#define SMPL_PRESET_NAME_LEN 8
#define SMPL_PRESET_VERSION 1
#define SMPL_PRESET_FLAG_USED 0x01
#define SMPL_PRESET_FLASH_OFFSET (SID_PRESET_FLASH_OFFSET + 4096)  // 2nd sector of SID region

struct SmplPreset {
  char name[SMPL_PRESET_NAME_LEN];        // 8 bytes
  uint8_t version;                         // 1 byte
  uint8_t flags;                           // 1 byte
  uint8_t sampleMode;                      // 1 byte (SNGL/SEQ/RND/GM)
  uint8_t sampleVolume;                    // 1 byte
  int8_t  pitchArr[TOTAL_SAMPLE_COUNT];    // 64 bytes
  int8_t  octaveArr[TOTAL_SAMPLE_COUNT];   // 64 bytes
  uint8_t lengthArr[TOTAL_SAMPLE_COUNT];   // 64 bytes
  uint8_t crushPacked[TOTAL_SAMPLE_COUNT / 2]; // 32 bytes: nibble-packed crush (0-7), 2 samples per byte
};
// ~236 bytes × 16 = 3,776 bytes (fits in 4KB sector)

// Current SMPL preset index (0xFF = custom/not from preset)
extern uint8_t currentSmplPreset;

// --- SMPL Preset Functions ---

bool smplPresetLoad(uint8_t presetIndex);
bool smplPresetSaveUser(uint8_t userSlot, const char* name);
bool smplPresetDeleteUser(uint8_t userSlot);
bool smplPresetUserIsUsed(uint8_t userSlot);
void smplPresetGetName(uint8_t presetIndex, char* buf);
uint8_t smplPresetGetTotalCount();
