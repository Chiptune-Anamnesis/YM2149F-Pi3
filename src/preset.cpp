#include "preset.h"
#include "voice_manager.h"
#include "fx_chip.h"
#include "sid_mode.h"
#include "sample_player.h"
#include "dual_core.h"
#include "display.h"
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <Wire.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

uint8_t currentPresetIndex = PRESET_INDEX_NONE;
uint8_t currentSidPreset = 0xFF;  // 0xFF = custom (not from preset)

// Factory presets are defined in preset_factory.cpp
// See: factorySidPresets[], factoryPresets[], factoryPresetNames[]
// See: initFactorySidPresets(), initFactoryPresets()

// ============================================================================
// CRC16 CALCULATION
// ============================================================================

// Verify preset slots fit in allocated flash region
static_assert(PRESET_USER_SLOTS * sizeof(PresetData) <= YM_PRESET_FLASH_SIZE,
              "PRESET_USER_SLOTS exceeds YM preset flash region");

uint16_t presetCalcCRC(const PresetData& p) {
  // Simple CRC16-CCITT
  uint16_t crc = 0xFFFF;
  const uint8_t* data = (const uint8_t*)&p;
  size_t len = offsetof(PresetData, crc16);  // Cover everything before crc16

  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// ============================================================================
// FLASH OPERATIONS
// ============================================================================

// Read preset header from flash (legacy, for migration)
static void readHeader(PresetHeader& header) {
  const uint8_t* flash = (const uint8_t*)PRESET_FLASH_BASE;
  memcpy(&header, flash, sizeof(PresetHeader));
}

// Read global settings from end of flash
static void readSettings(GlobalSettings& s) {
  const uint8_t* flash = (const uint8_t*)SETTINGS_FLASH_BASE;
  memcpy(&s, flash, sizeof(GlobalSettings));
}

// Read user preset from flash
static bool readUserPreset(uint8_t userSlot, PresetData& p) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  // Calculate offset: presets start at offset 0 (settings in last sector)
  uint32_t offset = userSlot * sizeof(PresetData);
  const uint8_t* flash = (const uint8_t*)(PRESET_FLASH_BASE + offset);

  memcpy(&p, flash, sizeof(PresetData));

  // Validate magic
  if (p.magic != PRESET_MAGIC) return false;

  // Validate CRC (zero crc16 before calc, same as during save)
  uint16_t expectedCRC = p.crc16;
  p.crc16 = 0;
  if (presetCalcCRC(p) != expectedCRC) return false;
  p.crc16 = expectedCRC;  // Restore for caller

  return true;
}

// Static buffer to avoid stack overflow (4KB is too much for stack)
static uint8_t flashBuffer[PRESET_SECTOR_SIZE];

// Write data to flash (must be called from Core 0)
// Uses cooperative pause to ensure Core 1 is not in I2C transaction
// Handles non-sector-aligned writes by reading entire sector first
void presetWriteFlash(uint32_t offset, const uint8_t* data, size_t len) {
  // Calculate sector-aligned start address (flash erase requires 4KB alignment)
  uint32_t sectorStart = (offset / PRESET_SECTOR_SIZE) * PRESET_SECTOR_SIZE;
  uint32_t offsetInSector = offset - sectorStart;

  uint32_t flash_offset = PRESET_FLASH_BASE - XIP_BASE + sectorStart;

  // Stop SID timer if in SID mode (prevents timer corruption during flash operations)
  bool wasInSidMode = sidModeGlobal && sidTimerActive;
  if (wasInSidMode) {
    // Stop timer first (prevents new callbacks)
    sidTimerStop();

    // Wait for any in-progress callback to finish
    unsigned long busWaitStart = millis();
    while (ymBusBusy && (millis() - busWaitStart < 100)) {
      delayMicroseconds(10);
    }

    // Additional delay to ensure timer fully stopped
    delay(5);
  }

  // Read entire sector first (preserves other data in sector)
  const uint8_t* flashSector = (const uint8_t*)(PRESET_FLASH_BASE + sectorStart);
  memcpy(flashBuffer, flashSector, PRESET_SECTOR_SIZE);

  // Modify only our portion
  memcpy(flashBuffer + offsetInSector, data, len);

  // ===== COOPERATIVE PAUSE MECHANISM =====
  // Step 1: Request Core 1 to pause at a safe point (between display updates)
  flashPauseRequested = true;

  // Step 2: Wait for Core 1 to reach its safe pause point
  // Core 1 will set core1Paused=true when it's NOT in the middle of I2C
  unsigned long waitStart = millis();
  while (!core1Paused) {
    // Timeout after 500ms to avoid infinite hang
    if (millis() - waitStart > 500) {
      // Core 1 didn't respond - proceed anyway (last resort)
      break;
    }
    delayMicroseconds(100);
  }
  bool forcedIdle = !core1Paused;  // Track if we hit timeout

  // Step 3: Now Core 1 is safely paused in a busy-wait loop
  // It's safe to call idleOtherCore because Core 1 is at a known point
  noInterrupts();
  rp2040.idleOtherCore();

  // Erase sector (4KB) at aligned address
  flash_range_erase(flash_offset, PRESET_SECTOR_SIZE);

  // Write entire sector back
  flash_range_program(flash_offset, flashBuffer, PRESET_SECTOR_SIZE);

  // Resume other core
  rp2040.resumeOtherCore();
  interrupts();

  // Step 4: Signal Core 1 it can continue
  flashPauseRequested = false;

  // If we forcibly idled Core 1 mid-I2C, reinit the bus to recover
  if (forcedIdle) {
    Wire1.end();
    Wire1.setSDA(PIN_OLED_SDA);
    Wire1.setSCL(PIN_OLED_SCL);
    Wire1.begin();
    delay(10);
  }

  // Give Core 1 a moment to resume before we continue
  delay(10);

  // Restart SID timer if it was running before flash operations
  // Do this AFTER Core 1 has resumed to avoid any race conditions
  if (wasInSidMode) {
    sidTimerStart();
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void presetInit() {
  // Initialize factory presets with voice layering
  initFactoryPresets();

  // Initialize factory SID presets with full voice settings
  initFactorySidPresets();

  // Read global settings from new location (last sector)
  GlobalSettings settings;
  readSettings(settings);

  bool needsSave = false;

  if (settings.magic != SETTINGS_MAGIC) {
    // Settings not found - try migrating from legacy PresetHeader
    PresetHeader oldHeader;
    readHeader(oldHeader);

    if (oldHeader.magic == PRESET_HEADER_MAGIC) {
      // Migration from legacy header
      settings.magic = SETTINGS_MAGIC;
      settings.version = SETTINGS_VERSION;
      settings.midiSynthChannel = oldHeader.midiSynthChannel;
      settings.midiDrumChannel = oldHeader.midiDrumChannel;
      settings.usbMode = oldHeader.usbMode;
      settings.activePreset = PRESET_INDEX_NONE;
      settings.vizMode = VIZ_MODE_BARS;
      settings.sidModeGlobal = 0;  // YM mode by default
      memset(settings.midiChannelRemap, MIDI_REMAP_NONE, 16);  // No remapping
      settings.sidDutyChip[0] = 8;  // 50% duty for chip 1
      settings.sidDutyChip[1] = 8;  // 50% duty for chip 2
      settings.displayBrightness = DISPLAY_BRIGHTNESS_DEFAULT;
      settings.potDefaults[0] = { PCAT_ENVELOPE, 1, TARGET_ALL, 0 };
      settings.potDefaults[1] = { PCAT_ENVELOPE, 2, TARGET_ALL, 0 };
      settings.potDefaults[2] = { PCAT_ENVELOPE, 3, TARGET_ALL, 0 };
      memset(settings.reserved, 0, sizeof(settings.reserved));
      needsSave = true;
    } else {
      // Fresh install - initialize with defaults
      settings.magic = SETTINGS_MAGIC;
      settings.version = SETTINGS_VERSION;
      settings.midiSynthChannel = MIDI_CHANNEL_OMNI;
      settings.midiDrumChannel = 9;
      settings.usbMode = USB_MODE_MIDI;
      settings.activePreset = PRESET_INDEX_NONE;
      settings.vizMode = VIZ_MODE_BARS;
      settings.sidModeGlobal = 0;  // YM mode by default
      memset(settings.midiChannelRemap, MIDI_REMAP_NONE, 16);  // No remapping
      settings.sidDutyChip[0] = 8;  // 50% duty for chip 1
      settings.sidDutyChip[1] = 8;  // 50% duty for chip 2
      settings.displayBrightness = DISPLAY_BRIGHTNESS_DEFAULT;
      settings.potDefaults[0] = { PCAT_ENVELOPE, 1, TARGET_ALL, 0 };
      settings.potDefaults[1] = { PCAT_ENVELOPE, 2, TARGET_ALL, 0 };
      settings.potDefaults[2] = { PCAT_ENVELOPE, 3, TARGET_ALL, 0 };
      memset(settings.reserved, 0, sizeof(settings.reserved));
      needsSave = true;
    }
  } else if (settings.version < SETTINGS_VERSION) {
    // Migration from older version
    if (settings.version < 2) {
      // Version 1 -> 2: add midiChannelRemap (default to no remapping)
      memset(settings.midiChannelRemap, MIDI_REMAP_NONE, 16);
    }
    if (settings.version < 3) {
      // Version 2 -> 3: add sidModeGlobal, sidDutyChip (default off, 50% duty)
      settings.sidModeGlobal = 0;  // YM mode by default
      settings.sidDutyChip[0] = 8;  // 50% duty for chip 1
      settings.sidDutyChip[1] = 8;  // 50% duty for chip 2
    }
    if (settings.version < 4) {
      // Version 3 -> 4: add displayBrightness
      settings.displayBrightness = DISPLAY_BRIGHTNESS_DEFAULT;
    }
    if (settings.version < 5) {
      // Version 4 -> 5: add pot default assignments
      settings.potDefaults[0] = { PCAT_ENVELOPE, 1, TARGET_ALL, 0 };
      settings.potDefaults[1] = { PCAT_ENVELOPE, 2, TARGET_ALL, 0 };
      settings.potDefaults[2] = { PCAT_ENVELOPE, 3, TARGET_ALL, 0 };
    }
    if (settings.version < 6) {
      // Version 5 -> 6: add MIDI clock sync
      settings.clkSync = 0;
    }
    settings.version = SETTINGS_VERSION;
    needsSave = true;
  }

  if (needsSave) {
    presetWriteFlash(SETTINGS_FLASH_OFFSET, (const uint8_t*)&settings, sizeof(GlobalSettings));
  }

  // Load settings into global variables
  if (settings.midiSynthChannel <= 15 ||
      settings.midiSynthChannel == MIDI_CHANNEL_OFF ||
      settings.midiSynthChannel == MIDI_CHANNEL_OMNI) {
    midiSynthChannel = settings.midiSynthChannel;
  }
  if (settings.midiDrumChannel <= 15 || settings.midiDrumChannel == MIDI_CHANNEL_OFF) {
    midiDrumChannel = settings.midiDrumChannel;
  }
  if (settings.usbMode <= USB_MODE_SERIAL) {
    usbMode = settings.usbMode;
  }
  if (settings.vizMode < VIZ_MODE_COUNT) {
    vizMode = settings.vizMode;
  }
  // Load global SID mode settings
  sidModeGlobal = (settings.sidModeGlobal != 0);
  // Load per-chip duty cycles (clamp to 0-15)
  sidDutyChip[0] = (settings.sidDutyChip[0] <= 15) ? settings.sidDutyChip[0] : 8;
  sidDutyChip[1] = (settings.sidDutyChip[1] <= 15) ? settings.sidDutyChip[1] : 8;
  // Load MIDI channel routing
  for (uint8_t i = 0; i < 16; i++) {
    midiChannelRemap[i] = settings.midiChannelRemap[i];
  }
  // Load display brightness
  displayBrightness = settings.displayBrightness;
  // Load pot default assignments and apply as current pot assignments
  for (uint8_t i = 0; i < 3; i++) {
    potDefaultAssignments[i] = settings.potDefaults[i];
    potAssignments[i] = settings.potDefaults[i];
  }
  // Load MIDI clock sync setting
  fxClockSync = (settings.clkSync != 0);

  // Always start with no preset active - user must explicitly load one
  currentPresetIndex = PRESET_INDEX_NONE;
}

// ============================================================================
// CAPTURE CURRENT STATE
// ============================================================================

void presetCaptureCurrent(PresetData& p) {
  p.magic = PRESET_MAGIC;
  p.version = PRESET_VERSION;
  p.flags = PRESET_FLAG_USED;

  // Voice settings
  for (int i = 0; i < 9; i++) {
    p.voices[i] = voiceSettings[i];
  }

  // Pot assignments
  for (int i = 0; i < 3; i++) {
    p.pots[i] = potAssignments[i];
  }

  // FX state
  p.fxEnabled = fxModeEnabled;
  p.fxType = fxType;
  p.fxRouting = fxRouting;
  p.echoDelayMs = echoDelayMs;
  p.echoRepeats = echoRepeats;
  p.echoDecay = echoDecay;
  p.echoVolume = echoVolume;
  p.arpPattern = arpPattern;
  p.arpSpeedMs = arpSpeedMs;
  p.arpVolume = arpVolume;
  p.arpOctave = arpOctave;
  p.bitCrushBits = bitCrushBits;
  p.bitCrushRate = bitCrushRate;
  p.bitCrushVolume = bitCrushVolume;
  p.bitCrushDuration = bitCrushDuration;
  p.reverbTaps = reverbTaps;
  p.reverbSpacing = reverbSpacing;
  p.reverbDecay = reverbDecay;
  p.reverbDetune = reverbDetune;
  p.reverbVolume = reverbVolume;
  p.chorusDetune1 = chorusDetune1;
  p.chorusDetune2 = chorusDetune2;
  p.chorusVolume = chorusVolume;
  p.chorusRate = chorusRate;
  p.chorusReserved = 0;

  // Harmonizer
  p.harmChord = harmChord;
  p.harmVolume = harmVolume;
  p.harmOctave = harmOctave;

  // Gate
  p.gateRateMs = gateRateMs;
  p.gatePattern = gatePattern;
  p.gateVolume = gateVolume;
  p.gateDuty = gateDuty;
  p.gateSeed = gateSeed;

  // SID mode
  p.sidMode = sidMode;
  p.sidEnvFreqCoarse = sidEnvFreqCoarse;
  p.sidEnvFreqFine = sidEnvFreqFine;
  p.sidEnvShape = sidEnvShape;

  // Sample player
  p.sampleSelect = sampleSelect;
  p.sampleMode = sampleMode;
  p.sampleVolume = sampleVolume;
  p.sampleSeqIndex = sampleSeqIndex;

  // Global
  p.polyMode = polyMode;
  p.linkMode = linkMode;
  p.voiceLinkMask = voiceLinkMask;
  for (int i = 0; i < 3; i++) {
    p.chipLink[i] = chipLink[i];
  }

  // Calculate CRC (zero crc16/reserved first so they don't affect calculation)
  p.crc16 = 0;
  p.reserved[0] = 0;
  p.reserved[1] = 0;
  p.crc16 = presetCalcCRC(p);
}

// ============================================================================
// APPLY PRESET TO SYNTH STATE
// ============================================================================

void presetApplyCurrent(const PresetData& p) {
  // Voice settings
  for (int i = 0; i < 9; i++) {
    voiceSettings[i] = p.voices[i];
  }

  // Pot assignments
  for (int i = 0; i < 3; i++) {
    potAssignments[i] = p.pots[i];
  }

  // FX state
  fxModeEnabled = p.fxEnabled;
  fxType = p.fxType;
  fxRouting = p.fxRouting;
  echoDelayMs = p.echoDelayMs;
  echoRepeats = p.echoRepeats;
  echoDecay = p.echoDecay;
  echoVolume = p.echoVolume;
  arpPattern = p.arpPattern;
  arpSpeedMs = p.arpSpeedMs;
  arpVolume = p.arpVolume;
  arpOctave = p.arpOctave;
  bitCrushBits = p.bitCrushBits;
  bitCrushRate = p.bitCrushRate;
  bitCrushVolume = p.bitCrushVolume;
  bitCrushDuration = p.bitCrushDuration;
  reverbTaps = p.reverbTaps;
  reverbSpacing = p.reverbSpacing;
  reverbDecay = p.reverbDecay;
  reverbDetune = p.reverbDetune;
  reverbVolume = p.reverbVolume;
  chorusDetune1 = p.chorusDetune1;
  chorusDetune2 = p.chorusDetune2;
  chorusVolume = p.chorusVolume;
  chorusRate = p.chorusRate;

  // Harmonizer
  harmChord = p.harmChord;
  harmVolume = p.harmVolume;
  harmOctave = p.harmOctave;

  // Gate
  gateRateMs = p.gateRateMs;
  gatePattern = p.gatePattern;
  gateVolume = p.gateVolume;
  gateDuty = p.gateDuty;
  gateSeed = p.gateSeed;

  // SID mode
  sidMode = p.sidMode;
  sidEnvFreqCoarse = p.sidEnvFreqCoarse;
  sidEnvFreqFine = p.sidEnvFreqFine;
  sidEnvShape = p.sidEnvShape;

  // Sample player
  sampleSelect = p.sampleSelect;
  sampleMode = p.sampleMode;
  sampleVolume = p.sampleVolume;
  sampleSeqIndex = p.sampleSeqIndex;

  // Global
  polyMode = p.polyMode;
  linkMode = p.linkMode;
  voiceLinkMask = p.voiceLinkMask;
  for (int i = 0; i < 3; i++) {
    chipLink[i] = p.chipLink[i];
  }
}

// ============================================================================
// PRESET MANAGEMENT
// ============================================================================

// Static buffer to avoid stack overflow in presetSaveUser
static uint8_t sectorBuffer[PRESET_SECTOR_SIZE];

// Helper: write sizeof(PresetData) bytes at a flash offset, handling sector crossings.
// A preset that starts near the end of one 4KB sector can span into the next sector.
// This function splits the write into one or two sector read-modify-write cycles.
static void writePresetAtOffset(uint32_t offset, const uint8_t* data, size_t len) {
  uint32_t sectorStart = (offset / PRESET_SECTOR_SIZE) * PRESET_SECTOR_SIZE;
  uint32_t offsetInSector = offset - sectorStart;
  uint32_t bytesInFirstSector = PRESET_SECTOR_SIZE - offsetInSector;

  if (len <= bytesInFirstSector) {
    // Fits entirely in one sector
    const uint8_t* flash = (const uint8_t*)(PRESET_FLASH_BASE + sectorStart);
    memcpy(sectorBuffer, flash, PRESET_SECTOR_SIZE);
    memcpy(sectorBuffer + offsetInSector, data, len);
    presetWriteFlash(sectorStart, sectorBuffer, PRESET_SECTOR_SIZE);
  } else {
    // Crosses sector boundary — write first part, then second part
    const uint8_t* flash1 = (const uint8_t*)(PRESET_FLASH_BASE + sectorStart);
    memcpy(sectorBuffer, flash1, PRESET_SECTOR_SIZE);
    memcpy(sectorBuffer + offsetInSector, data, bytesInFirstSector);
    presetWriteFlash(sectorStart, sectorBuffer, PRESET_SECTOR_SIZE);

    uint32_t sector2Start = sectorStart + PRESET_SECTOR_SIZE;
    uint32_t remaining = len - bytesInFirstSector;
    const uint8_t* flash2 = (const uint8_t*)(PRESET_FLASH_BASE + sector2Start);
    memcpy(sectorBuffer, flash2, PRESET_SECTOR_SIZE);
    memcpy(sectorBuffer, data + bytesInFirstSector, remaining);
    presetWriteFlash(sector2Start, sectorBuffer, PRESET_SECTOR_SIZE);
  }
}

bool presetSaveUser(uint8_t userSlot, const char* name) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  PresetData p;
  presetCaptureCurrent(p);

  // Set name
  memset(p.name, 0, PRESET_NAME_LEN);
  if (name) {
    strncpy(p.name, name, PRESET_NAME_LEN);
  }

  // Recalculate CRC after name change
  p.crc16 = 0;
  p.crc16 = presetCalcCRC(p);

  // Write preset to flash (handles sector-crossing slots)
  uint32_t offset = userSlot * sizeof(PresetData);
  writePresetAtOffset(offset, (const uint8_t*)&p, sizeof(PresetData));

  currentPresetIndex = USER_SLOT_TO_PRESET(userSlot);
  return true;
}

bool presetLoad(uint8_t presetIndex) {
  PresetData p;

  if (PRESET_IS_FACTORY(presetIndex)) {
    // Load from factory presets
    p = factoryPresets[presetIndex];
  } else if (PRESET_IS_USER(presetIndex)) {
    // Load from user flash
    uint8_t userSlot = PRESET_TO_USER_SLOT(presetIndex);
    if (!readUserPreset(userSlot, p)) {
      return false;
    }
  } else {
    return false;
  }

  // Apply preset
  presetApplyCurrent(p);

  // Just update RAM tracking - don't write to flash on every load
  // (Active preset will be saved when user saves a new preset)
  currentPresetIndex = presetIndex;
  return true;
}

bool presetDeleteUser(uint8_t userSlot) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  // Create empty preset marker (0xFF = flash erased state)
  PresetData empty;
  memset(&empty, 0xFF, sizeof(PresetData));

  // Write to flash (handles sector-crossing slots)
  uint32_t offset = userSlot * sizeof(PresetData);
  writePresetAtOffset(offset, (const uint8_t*)&empty, sizeof(PresetData));

  if (currentPresetIndex == USER_SLOT_TO_PRESET(userSlot)) {
    currentPresetIndex = PRESET_INDEX_NONE;
  }

  return true;
}

bool presetUserIsUsed(uint8_t userSlot) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  // Presets start at offset 0
  uint32_t offset = userSlot * sizeof(PresetData);
  const PresetData* p = (const PresetData*)(PRESET_FLASH_BASE + offset);

  return (p->magic == PRESET_MAGIC && p->flags == PRESET_FLAG_USED);
}

void presetGetName(uint8_t presetIndex, char* buf) {
  if (PRESET_IS_FACTORY(presetIndex)) {
    strncpy(buf, factoryPresetNames[presetIndex], PRESET_NAME_LEN);
    buf[PRESET_NAME_LEN] = '\0';
  } else if (PRESET_IS_USER(presetIndex)) {
    uint8_t userSlot = PRESET_TO_USER_SLOT(presetIndex);
    // Presets start at offset 0
    uint32_t offset = userSlot * sizeof(PresetData);
    const PresetData* p = (const PresetData*)(PRESET_FLASH_BASE + offset);

    if (p->magic == PRESET_MAGIC && p->flags == PRESET_FLAG_USED) {
      memcpy(buf, p->name, PRESET_NAME_LEN);
      buf[PRESET_NAME_LEN] = '\0';
    } else {
      strcpy(buf, "--------");
    }
  } else {
    strcpy(buf, "--------");
  }
}

uint8_t presetGetTotalCount() {
  uint8_t count = PRESET_FACTORY_COUNT;

  for (uint8_t i = 0; i < PRESET_USER_SLOTS; i++) {
    if (presetUserIsUsed(i)) {
      count++;
    }
  }

  return count;
}

// ============================================================================
// GLOBAL SETTINGS (stored in last sector, separate from presets)
// ============================================================================

void saveGlobalSettings() {
  GlobalSettings s;
  s.magic = SETTINGS_MAGIC;
  s.version = SETTINGS_VERSION;
  s.midiSynthChannel = midiSynthChannel;
  s.midiDrumChannel = midiDrumChannel;
  s.usbMode = usbMode;
  s.activePreset = currentPresetIndex;
  s.vizMode = vizMode;
  s.sidModeGlobal = sidModeGlobal ? 1 : 0;
  // Save MIDI channel routing
  for (uint8_t i = 0; i < 16; i++) {
    s.midiChannelRemap[i] = midiChannelRemap[i];
  }
  // Save per-chip duty cycles
  s.sidDutyChip[0] = sidDutyChip[0];
  s.sidDutyChip[1] = sidDutyChip[1];
  s.displayBrightness = displayBrightness;
  // Save pot default assignments
  for (uint8_t i = 0; i < 3; i++) {
    s.potDefaults[i] = potDefaultAssignments[i];
  }
  s.clkSync = fxClockSync ? 1 : 0;
  memset(s.reserved, 0, sizeof(s.reserved));

  // Write to last sector (doesn't affect presets at all)
  presetWriteFlash(SETTINGS_FLASH_OFFSET, (const uint8_t*)&s, sizeof(GlobalSettings));
}

// Read USB mode directly from flash (for early init before presetInit)
uint8_t readUsbModeFromFlash() {
  // Try new GlobalSettings location first
  GlobalSettings s;
  readSettings(s);

  // Accept any valid version (migration will happen in presetInit)
  if (s.magic == SETTINGS_MAGIC && s.version >= 1 && s.version <= SETTINGS_VERSION) {
    if (s.usbMode <= USB_MODE_SERIAL) {
      return s.usbMode;
    }
    return USB_MODE_MIDI;
  }

  // Fall back to legacy PresetHeader location (for migration)
  PresetHeader header;
  readHeader(header);

  if (header.magic == PRESET_HEADER_MAGIC && header.usbMode <= USB_MODE_SERIAL) {
    return header.usbMode;
  }

  return USB_MODE_MIDI;  // Default to USB-MIDI
}

// ============================================================================
// SID PRESET FUNCTIONS
// ============================================================================

// User SID presets are stored in the dedicated SID preset section (12KB)
// This section has its own sectors, so writes don't affect other sections
#define SID_PRESETS_FLASH_OFFSET SID_PRESET_FLASH_OFFSET

// Read user SID presets from flash
static void readUserSidPresets(SidPreset* presets) {
  // PRESET_FLASH_BASE already includes XIP_BASE (0x101F7000), don't add XIP_BASE again!
  const uint8_t* flashData = (const uint8_t*)(PRESET_FLASH_BASE + SID_PRESETS_FLASH_OFFSET);
  memcpy(presets, flashData, sizeof(SidPreset) * SID_PRESET_USER_COUNT);
}

// Load SID preset by index (0-3 = factory, 4-11 = user)
bool sidPresetLoad(uint8_t presetIndex) {
  if (presetIndex >= SID_PRESET_TOTAL) return false;

  const SidPreset* preset;
  static SidPreset userPresets[SID_PRESET_USER_COUNT];

  if (presetIndex < SID_PRESET_FACTORY_COUNT) {
    // Factory preset
    preset = &factorySidPresets[presetIndex];
  } else {
    // User preset
    uint8_t userSlot = presetIndex - SID_PRESET_FACTORY_COUNT;
    readUserSidPresets(userPresets);
    if (!(userPresets[userSlot].flags & SID_PRESET_FLAG_USED)) {
      return false;  // Slot is empty
    }
    if (userPresets[userSlot].version != SID_PRESET_VERSION_FULL) {
      return false;  // Old format, incompatible struct layout
    }
    preset = &userPresets[userSlot];
  }

  currentSidPreset = presetIndex;

  // Apply poly mode
  polyMode = preset->polyMode;

  // Apply all voice settings (6 SID voices -> voiceSettings[3-8])
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t voiceIdx = i + 3;  // Map SID voice to voiceSettings index
    voiceSettings[voiceIdx] = preset->voices[i];

    // Update sidDutyChip from voice settings
    uint8_t chipIdx = (i < 3) ? 0 : 1;
    if (i == 0) sidDutyChip[0] = preset->voices[0].sidDuty;  // Use voice 0 for chip 1
    if (i == 3) sidDutyChip[1] = preset->voices[3].sidDuty;  // Use voice 3 for chip 2

    // Update SID voice state for active playback
    sidState[i].duty = preset->voices[i].sidDuty;
    sidState[i].waveform = preset->voices[i].sidWave;
    sidState[i].pwmDepth = preset->voices[i].sidPwmDepth;
    sidState[i].syncSource = preset->voices[i].sidSync;
    sidState[i].ringSource = preset->voices[i].sidRing;
    sidState[i].noiseOn = preset->voices[i].sidNoise;
    // Calculate PWM phase increment from rate
    if (preset->voices[i].sidPwmRate > 0) {
      float hz = 0.1f + (preset->voices[i].sidPwmRate / 127.0f) * 9.9f;
      sidState[i].pwmPhaseInc = (uint32_t)(hz * 65536.0f / 25000.0f * 65536.0f);
    } else {
      sidState[i].pwmPhaseInc = 0;
    }

    // Reset phase for active voices to prevent mid-cycle duty change click
    if (sidState[i].active) {
      sidState[i].phase = 0xFFFF;
      sidState[i].isHigh = false;
    }
  }

  // Memory barrier to ensure Core 0 (timer callback) sees the duty updates
  __dmb();

  return true;
}

// Save current SID settings to user slot (0-7)
bool sidPresetSaveUser(uint8_t userSlot, const char* name) {
  if (userSlot >= SID_PRESET_USER_COUNT) return false;

  // Read all user presets
  static SidPreset userPresets[SID_PRESET_USER_COUNT];
  readUserSidPresets(userPresets);

  // Prepare new preset with full voice settings
  SidPreset newPreset;
  memset(&newPreset, 0, sizeof(SidPreset));
  strncpy(newPreset.name, name, SID_PRESET_NAME_LEN);
  newPreset.version = SID_PRESET_VERSION_FULL;
  newPreset.flags = SID_PRESET_FLAG_USED;
  newPreset.polyMode = polyMode;

  // Copy all 6 SID voice settings from voiceSettings[3-8]
  for (uint8_t i = 0; i < 6; i++) {
    newPreset.voices[i] = voiceSettings[i + 3];
  }

  // Update the slot
  userPresets[userSlot] = newPreset;

  // Write back to flash
  presetWriteFlash(SID_PRESETS_FLASH_OFFSET, (const uint8_t*)userPresets, sizeof(userPresets));

  // Update current preset index
  currentSidPreset = SID_PRESET_FACTORY_COUNT + userSlot;

  return true;
}

// Delete user SID preset
bool sidPresetDeleteUser(uint8_t userSlot) {
  if (userSlot >= SID_PRESET_USER_COUNT) return false;

  // Read all user presets
  static SidPreset userPresets[SID_PRESET_USER_COUNT];
  readUserSidPresets(userPresets);

  // Clear the slot
  memset(&userPresets[userSlot], 0xFF, sizeof(SidPreset));  // 0xFF = empty

  // Write back to flash
  presetWriteFlash(SID_PRESETS_FLASH_OFFSET, (const uint8_t*)userPresets, sizeof(userPresets));

  // If we deleted the current preset, mark as custom
  if (currentSidPreset == SID_PRESET_FACTORY_COUNT + userSlot) {
    currentSidPreset = 0xFF;
  }

  return true;
}

// Check if user SID slot is used
// Uses direct pointer access (like YM preset functions) to minimize flash reads
bool sidPresetUserIsUsed(uint8_t userSlot) {
  if (userSlot >= SID_PRESET_USER_COUNT) return false;

  // Direct pointer access - only reads the specific slot's flags byte
  // PRESET_FLASH_BASE already includes XIP_BASE (0x101F7000)
  const SidPreset* p = (const SidPreset*)(PRESET_FLASH_BASE + SID_PRESETS_FLASH_OFFSET);
  return (p[userSlot].flags & SID_PRESET_FLAG_USED) != 0;
}

// Get SID preset name (copies to buffer, max 8 chars + null)
// Uses direct pointer access (like YM preset functions) to minimize flash reads
void sidPresetGetName(uint8_t presetIndex, char* buf) {
  if (presetIndex < SID_PRESET_FACTORY_COUNT) {
    // Factory preset - in RAM, no flash access needed
    strncpy(buf, factorySidPresets[presetIndex].name, SID_PRESET_NAME_LEN);
    buf[SID_PRESET_NAME_LEN] = '\0';
  } else if (presetIndex < SID_PRESET_TOTAL) {
    // User preset - direct pointer access to specific slot only
    uint8_t userSlot = presetIndex - SID_PRESET_FACTORY_COUNT;
    // PRESET_FLASH_BASE already includes XIP_BASE (0x101F7000)
    const SidPreset* p = (const SidPreset*)(PRESET_FLASH_BASE + SID_PRESETS_FLASH_OFFSET);
    if (p[userSlot].flags & SID_PRESET_FLAG_USED) {
      memcpy(buf, p[userSlot].name, SID_PRESET_NAME_LEN);
      buf[SID_PRESET_NAME_LEN] = '\0';
    } else {
      strcpy(buf, "---");  // Empty slot
    }
  } else {
    strcpy(buf, "???");
  }
}

// Get total SID preset count (factory + used user slots)
uint8_t sidPresetGetTotalCount() {
  uint8_t count = SID_PRESET_FACTORY_COUNT;

  static SidPreset userPresets[SID_PRESET_USER_COUNT];
  readUserSidPresets(userPresets);

  for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
    if (userPresets[i].flags & SID_PRESET_FLAG_USED) {
      count++;
    }
  }

  return count;
}
