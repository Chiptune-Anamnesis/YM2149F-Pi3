#include "display.h"
#include "encoder.h"
#include "voice_manager.h"
#include "sid_mode.h"
#include "settings.h"
#include "dual_core.h"
#include "fx_chip.h"
#include "sample_player.h"
#include "preset.h"
#include <Wire.h>
#include <Fonts/TomThumb.h>

// ============================================================================
// DISPLAY STATE
// ============================================================================

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);
DisplayMode displayMode = DISPLAY_VIZ;
uint8_t vizMode = VIZ_MODE_BARS;  // Visualization mode (bars or oscilloscope)

int menuSelection = 0;
bool editingValue = false;
int tempModeValue = 0;
int tempLinkValue = 0;

// Settings menu state
int settingsSelection = 0;
bool settingsEditing = false;
int settingsTempValue = 0;
int settingsScrollOffset = 0;

// Submenu state (for Vibrato, Envelope, SID submenus)
uint8_t settingsSubmenu = SUBMENU_NONE;  // Which submenu we're in
int submenuSelection = 0;
bool submenuEditing = false;
int submenuTempValue = 0;

// Pots submenu state (3-level editing: Category → Param → Target)
bool editingPotDefaults = false;  // True when editing defaults from SET menu
int potsSelection = 0;
uint8_t potsEditLevel = POT_EDIT_NONE;  // Current edit level
uint8_t potsTempCategory = 0;           // Temp category during editing
uint8_t potsTempParam = 0;              // Temp param index during editing
uint8_t potsTempTarget = 0;             // Temp target during editing

// FX menu state
int fxSelection = 0;
bool fxEditing = false;
int fxTempValue = 0;

// Preset menu state
int presetMenuSelection = 0;
uint8_t presetMenuLevel = PRESET_LEVEL_MENU;
int presetScrollIndex = 0;
int presetSelectedSlot = 0;
char presetNameBuffer[9] = "        ";
uint8_t presetNameCursor = 0;
bool presetNameEditing = false;
bool presetConfirmYes = false;
bool presetSaving = false;
unsigned long presetSaveStartTime = 0;
bool presetDeleting = false;
unsigned long presetDeleteStartTime = 0;
#define PRESET_SAVE_DISPLAY_MS 600  // Show "Saving..." for this long

// SID preset cache - prevents flash reads every frame during menu display
// Cached when entering LOAD/SAVE menus in SID mode
bool sidPresetCacheValid = false;
char sidPresetNames[SID_PRESET_TOTAL][9];  // Names for all presets
bool sidPresetUsed[SID_PRESET_USER_COUNT]; // Which user slots are used

void cacheSidPresets() {
  // Let Core 0 finish any pending flash write before we do our own flash reads
  checkFlashPause();

  // Pause SID timer during flash reads (timer keeps running but callback skips work)
  // This prevents any potential XIP bus conflicts while allowing audio to resume immediately
  sidTimerPause();

  // Cache factory preset names (from RAM - no flash access)
  for (uint8_t i = 0; i < SID_PRESET_FACTORY_COUNT; i++) {
    sidPresetGetName(i, sidPresetNames[i]);
  }
  // Cache user preset names and used flags (from flash)
  for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
    sidPresetUsed[i] = sidPresetUserIsUsed(i);
    if (sidPresetUsed[i]) {
      sidPresetGetName(SID_PRESET_FACTORY_COUNT + i, sidPresetNames[SID_PRESET_FACTORY_COUNT + i]);
    } else {
      strcpy(sidPresetNames[SID_PRESET_FACTORY_COUNT + i], "--------");
    }
  }
  sidPresetCacheValid = true;

  // Resume SID timer
  sidTimerResume();
}

// MIDI menu state
int midiMenuSelection = 0;
bool midiEditing = false;
int midiTempValue = 0;

// Route submenu state
uint8_t routeEditLevel = ROUTE_EDIT_NONE;
uint8_t routeFromChannel = 0;
uint8_t routeTempTo = MIDI_REMAP_NONE;

// Chip/scope selection
uint8_t currentChip = 0;   // Which chip's settings we're editing (0, 1, 2)
uint8_t currentScope = 0;  // 0=ALL, 1=A, 2=B, 3=C (within current chip)

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Forward declaration
void setTargetForCommands();

// Get preset index for a given position in the load list
// Factory presets are positions 0-7, then used user presets follow
uint8_t getLoadListPresetIndex(int position) {
  // First positions are factory presets
  if (position < PRESET_FACTORY_COUNT) {
    return position;
  }

  // After factory presets, find user presets that are used
  int userPosition = position - PRESET_FACTORY_COUNT;
  int found = 0;
  for (int slot = 0; slot < PRESET_USER_SLOTS; slot++) {
    if (presetUserIsUsed(slot)) {
      if (found == userPosition) {
        return USER_SLOT_TO_PRESET(slot);  // Convert slot to preset index
      }
      found++;
    }
  }

  return PRESET_INDEX_NONE;
}

// Get total count of loadable presets (factory + used user)
int getLoadListCount() {
  return presetGetTotalCount();
}

// Get user slot for a given position in the delete list (user presets only)
// Returns the user slot (0-119), or 0xFF if position is out of range
uint8_t getDeleteListUserSlot(int position) {
  int found = 0;
  for (int slot = 0; slot < PRESET_USER_SLOTS; slot++) {
    if (presetUserIsUsed(slot)) {
      if (found == position) {
        return slot;
      }
      found++;
    }
  }
  return 0xFF;  // Not found
}

// Get count of deletable presets (user presets only)
int getDeleteListCount() {
  int count = 0;
  for (int slot = 0; slot < PRESET_USER_SLOTS; slot++) {
    if (presetUserIsUsed(slot)) {
      count++;
    }
  }
  return count;
}

int getCurrentModeIndex() {
  // Use snapshot for Core 1 thread-safe read
  if (displaySnapshotCopy.polyMode == 2) return 0;  // Mono
  if (displaySnapshotCopy.polyMode == 0) return 1;  // Semi
  return 2;  // Poly
}

const char* getModeName(int idx) {
  switch (idx) {
    case 0: return "MONO";
    case 1: return "SEMI";
    case 2: return "POLY";
    default: return "???";
  }
}

void applyModeFromIndex(int modeIdx) {
  // Send command to Core 0 via queue
  uint8_t newPolyMode;
  switch (modeIdx) {
    case 0: newPolyMode = 2; break;  // Mono
    case 1: newPolyMode = 0; break;  // Semi
    case 2: newPolyMode = 1; break;  // Poly
    default: return;
  }
  sendCommand(CMD_SET_POLY_MODE, newPolyMode);
}

const char* getLinkModeName(int idx) {
  switch (idx) {
    case LINK_OFF:  return "OFF";
    case LINK_CH1:  return "CH1";
    case LINK_CH2:  return "CH2";
    case LINK_ALL:  return "ALL";
    default: return "???";
  }
}

// Get first voice index for current chip/scope
int getFirstVoiceForScope() {
  int baseVoice = currentChip * 3;  // 0, 3, or 6
  if (currentScope == 0) return baseVoice;  // ALL = first voice of chip
  return baseVoice + (currentScope - 1);    // A=0, B=1, C=2 within chip
}

// Get scope name
const char* getScopeName(int scope) {
  switch (scope) {
    case 0: return "ALL";
    case 1: return "A";
    case 2: return "B";
    case 3: return "C";
    default: return "?";
  }
}

// Convert voiceLinkMask to link option index (0=OFF, 1=+B, 2=+C, 3=ALL)
int maskToLinkOption(uint8_t mask) {
  switch (mask) {
    case 0x03: return 1;  // A+B
    case 0x05: return 2;  // A+C
    case 0x07: return 3;  // A+B+C (ALL)
    default:   return 0;  // OFF (0x00 or 0x01)
  }
}

// Convert link option index to voiceLinkMask
uint8_t linkOptionToMask(int option) {
  switch (option) {
    case 1: return 0x03;  // A+B
    case 2: return 0x05;  // A+C
    case 3: return 0x07;  // A+B+C (ALL)
    default: return 0x00; // OFF
  }
}

// Get current value for a main settings item (reads from snapshot for thread safety)
int getSettingsValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];

  switch (item) {
    case SETTINGS_CHIP:     return currentChip;
    case SETTINGS_SCOPE:    return currentScope;
    case SETTINGS_LINK:     return maskToLinkOption(displaySnapshotCopy.voiceLinkMask);
    case SETTINGS_NOISE:    return vs.noiseFreq;
    default: return 0;
  }
}

// Get max value for a main settings item
int getSettingsMax(int item) {
  switch (item) {
    case SETTINGS_CHIP:     return 2;    // 0-2 (chips 0, 1, 2)
    case SETTINGS_SCOPE:    return SCOPE_COUNT - 1;  // 0-3 (ALL, A, B, C)
    case SETTINGS_LINK:     return 3;    // 0=OFF, 1=+B, 2=+C, 3=ALL
    case SETTINGS_NOISE:    return 31;
    default: return 0;
  }
}

// Get display string for a main settings value
void getSettingsValueStr(int item, int value, char* buf) {
  switch (item) {
    case SETTINGS_CHIP:
      sprintf(buf, "%d", value);
      break;
    case SETTINGS_SCOPE:
      strcpy(buf, getScopeName(value));
      break;
    case SETTINGS_LINK:
      switch (value) {
        case 0: strcpy(buf, "OFF"); break;
        case 1: strcpy(buf, "+B"); break;
        case 2: strcpy(buf, "+C"); break;
        case 3: strcpy(buf, "ALL"); break;
        default: strcpy(buf, "?"); break;
      }
      break;
    case SETTINGS_NOISE:
      sprintf(buf, "%d", value);
      break;
    default:
      strcpy(buf, ">");  // Submenu indicator
  }
}

// Get label for a main settings item
const char* getSettingsLabel(int item) {
  switch (item) {
    case SETTINGS_CHIP:      return "CHIP";
    case SETTINGS_SID:       return "SID";
    case SETTINGS_SCOPE:     return "SCOPE";
    case SETTINGS_LINK:      return "LINK";
    case SETTINGS_PITCH:     return "PITCH";
    case SETTINGS_VIBRATO:   return "VIBRATO";
    case SETTINGS_TREMOLO:   return "TREMOLO";
    case SETTINGS_NOISE:     return "NOISE";
    case SETTINGS_ENVELOPE:  return "ENVELOPE";
    case SETTINGS_GLIDE:     return "GLIDE";
    case SETTINGS_PITCH_ENV: return "PITCH ENV";
    case SETTINGS_BACK:      return "< BACK";
    default: return "?";
  }
}

// ============================================================================
// SUBMENU HELPER FUNCTIONS
// ============================================================================

// Get value for vibrato submenu item
int getVibratoValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case VIBMENU_ON:    return vs.vibOn ? 1 : 0;  // Convert 127 to 1 for display
    case VIBMENU_RATE:  return vs.vibRateTenths;
    case VIBMENU_DEPTH: return vs.vibDepthCents;
    case VIBMENU_DELAY: return vs.vibDelay;
    default: return 0;
  }
}

int getVibratoMax(int item) {
  switch (item) {
    case VIBMENU_ON:    return 1;    // 0=OFF, 1=ON
    case VIBMENU_RATE:  return 150;
    case VIBMENU_DEPTH: return 200;
    case VIBMENU_DELAY: return 255;  // x10ms = 0-2550ms
    default: return 0;
  }
}

void getVibratoValueStr(int item, int value, char* buf) {
  switch (item) {
    case VIBMENU_ON:
      strcpy(buf, value ? "ON" : "OFF");
      break;
    case VIBMENU_RATE:
      sprintf(buf, "%d.%d", value / 10, value % 10);
      break;
    case VIBMENU_DEPTH:
      sprintf(buf, "%dc", value);
      break;
    case VIBMENU_DELAY:
      sprintf(buf, "%dms", value * 10);
      break;
    default:
      strcpy(buf, "?");
  }
}

const char* getVibratoLabel(int item) {
  switch (item) {
    case VIBMENU_ON:    return "ON";
    case VIBMENU_RATE:  return "RATE";
    case VIBMENU_DEPTH: return "DEPTH";
    case VIBMENU_DELAY: return "DELAY";
    case VIBMENU_BACK:  return "< BACK";
    default: return "?";
  }
}

// Get value for envelope submenu item
int getEnvelopeValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case ENVMENU_ATTACK:  return vs.envAttack;
    case ENVMENU_DECAY:   return vs.envDecay;
    case ENVMENU_SUSTAIN: return vs.envSustain;
    case ENVMENU_RELEASE: return vs.envRelease;
    default: return 0;
  }
}

int getEnvelopeMax(int item) {
  switch (item) {
    case ENVMENU_ATTACK:  return 127;
    case ENVMENU_DECAY:   return 127;
    case ENVMENU_SUSTAIN: return 127;
    case ENVMENU_RELEASE: return 127;
    default: return 0;
  }
}

void getEnvelopeValueStr(int item, int value, char* buf) {
  sprintf(buf, "%d", value);
}

const char* getEnvelopeLabel(int item) {
  switch (item) {
    case ENVMENU_ATTACK:  return "ATTACK";
    case ENVMENU_DECAY:   return "DECAY";
    case ENVMENU_SUSTAIN: return "SUSTAIN";
    case ENVMENU_RELEASE: return "RELEASE";
    case ENVMENU_BACK:    return "< BACK";
    default: return "?";
  }
}

// SID submenu helpers
int getSidValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case SIDMENU_WAVE:      return vs.sidWave;
    case SIDMENU_DUTY:      return vs.sidDuty;
    case SIDMENU_PWM_RATE:  return vs.sidPwmRate;
    case SIDMENU_PWM_DEPTH: return vs.sidPwmDepth;
    case SIDMENU_NOISE:     return vs.sidNoise;
    case SIDMENU_SYNC:      return vs.sidSync;
    case SIDMENU_RING:      return vs.sidRing;
    case SIDMENU_RELEASE:   return vs.envRelease;
    default: return 0;
  }
}

int getSidMax(int item) {
  switch (item) {
    case SIDMENU_WAVE:      return 3;
    case SIDMENU_DUTY:      return 15;
    case SIDMENU_PWM_RATE:  return 127;
    case SIDMENU_PWM_DEPTH: return 15;
    case SIDMENU_NOISE:     return 1;
    case SIDMENU_SYNC:      return 3;
    case SIDMENU_RING:      return 3;
    case SIDMENU_RELEASE:   return 127;
    default: return 0;
  }
}

void getSidValueStr(int item, int value, char* buf) {
  switch (item) {
    case SIDMENU_WAVE:
      switch (value) {
        case 0: strcpy(buf, "SQR"); break;
        case 1: strcpy(buf, "SAW"); break;
        case 2: strcpy(buf, "TRI"); break;
        case 3: strcpy(buf, "PLS"); break;
        default: strcpy(buf, "?"); break;
      }
      break;
    case SIDMENU_NOISE:
      strcpy(buf, value ? "ON" : "OFF");
      break;
    case SIDMENU_SYNC:
    case SIDMENU_RING:
      switch (value) {
        case 0: strcpy(buf, "OFF"); break;
        case 1: strcpy(buf, "A"); break;
        case 2: strcpy(buf, "B"); break;
        case 3: strcpy(buf, "C"); break;
        default: strcpy(buf, "?"); break;
      }
      break;
    default:
      sprintf(buf, "%d", value);
      break;
  }
}

const char* getSidLabel(int item) {
  switch (item) {
    case SIDMENU_WAVE:      return "WAVE";
    case SIDMENU_DUTY:      return "SQR DUTY";
    case SIDMENU_PWM_RATE:  return "SQR PWM S";
    case SIDMENU_PWM_DEPTH: return "SQR PWM D";
    case SIDMENU_NOISE:     return "NOISE";
    case SIDMENU_SYNC:      return "SYNC";
    case SIDMENU_RING:      return "RING";
    case SIDMENU_RELEASE:   return "RELEASE";
    case SIDMENU_BACK:      return "< BACK";
    default: return "?";
  }
}

// Get value for pitch submenu item
int getPitchValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case PITCHMENU_DETUNE: return vs.detuneCents + 50;  // Offset to 0-100 range
    case PITCHMENU_OCTAVE: return vs.octaveShift + 3;   // Offset to 0-6 range
    case PITCHMENU_VOLUME: return vs.maxVolume;         // 0-15
    default: return 0;
  }
}

int getPitchMax(int item) {
  switch (item) {
    case PITCHMENU_DETUNE: return 100;  // -50 to +50 mapped to 0-100
    case PITCHMENU_OCTAVE: return 6;    // -3 to +3 mapped to 0-6
    case PITCHMENU_VOLUME: return 15;   // 0-15 volume cap
    default: return 0;
  }
}

void getPitchValueStr(int item, int value, char* buf) {
  switch (item) {
    case PITCHMENU_DETUNE:
      sprintf(buf, "%+dc", value - 50);  // Show as -50 to +50 cents
      break;
    case PITCHMENU_OCTAVE:
      sprintf(buf, "%+d", value - 3);    // Show as -3 to +3
      break;
    case PITCHMENU_VOLUME:
      sprintf(buf, "%d", value);         // Show as 0-15
      break;
    default:
      strcpy(buf, "?");
  }
}

const char* getPitchLabel(int item) {
  switch (item) {
    case PITCHMENU_DETUNE: return "DETUNE";
    case PITCHMENU_OCTAVE: return "OCTAVE";
    case PITCHMENU_VOLUME: return "VOL MAX";
    case PITCHMENU_BACK:   return "< BACK";
    default: return "?";
  }
}

// Get value for glide submenu item
int getGlideValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case GLIDEMENU_ON:    return vs.portaOn;
    case GLIDEMENU_SPEED: return vs.portaSpeed;
    default: return 0;
  }
}

int getGlideMax(int item) {
  switch (item) {
    case GLIDEMENU_ON:    return 1;    // 0=OFF, 1=ON
    case GLIDEMENU_SPEED: return 127;  // 0-127 speed
    default: return 0;
  }
}

void getGlideValueStr(int item, int value, char* buf) {
  switch (item) {
    case GLIDEMENU_ON:
      strcpy(buf, value ? "ON" : "OFF");
      break;
    case GLIDEMENU_SPEED:
      sprintf(buf, "%d", value);
      break;
    default:
      strcpy(buf, "?");
  }
}

const char* getGlideLabel(int item) {
  switch (item) {
    case GLIDEMENU_ON:    return "ON";
    case GLIDEMENU_SPEED: return "SPEED";
    case GLIDEMENU_BACK:  return "< BACK";
    default: return "?";
  }
}

// Apply a glide submenu value
void applyGlideValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case GLIDEMENU_ON:    sendCommand(CMD_SET_PORTA_ON, (uint8_t)value); break;
    case GLIDEMENU_SPEED: sendCommand(CMD_SET_PORTA_SPEED, (uint8_t)value); break;
  }
}

// ============================================================================
// TREMOLO SUBMENU HELPERS
// ============================================================================

int getTremoloValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case TREMMENU_ON:    return vs.tremoloOn ? 1 : 0;  // Convert 127 to 1 for display
    case TREMMENU_RATE:  return vs.tremoloRate;
    case TREMMENU_DEPTH: return vs.tremoloDepth;
    default: return 0;
  }
}

int getTremoloMax(int item) {
  switch (item) {
    case TREMMENU_ON:    return 1;    // 0=OFF, 1=ON
    case TREMMENU_RATE:  return 150;
    case TREMMENU_DEPTH: return 100;
    default: return 0;
  }
}

void getTremoloValueStr(int item, int value, char* buf) {
  switch (item) {
    case TREMMENU_ON:
      strcpy(buf, value ? "ON" : "OFF");
      break;
    case TREMMENU_RATE:
      sprintf(buf, "%d.%d", value / 10, value % 10);
      break;
    case TREMMENU_DEPTH:
      sprintf(buf, "%d%%", value);
      break;
    default:
      strcpy(buf, "?");
  }
}

const char* getTremoloLabel(int item) {
  switch (item) {
    case TREMMENU_ON:    return "ON";
    case TREMMENU_RATE:  return "RATE";
    case TREMMENU_DEPTH: return "DEPTH";
    case TREMMENU_BACK:  return "< BACK";
    default: return "?";
  }
}

void applyTremoloValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case TREMMENU_ON:    sendCommand(CMD_SET_TREMOLO_ON, value ? 127 : 0); break;  // ON=127, OFF=0
    case TREMMENU_RATE:  sendCommand(CMD_SET_TREMOLO_RATE, (uint8_t)value); break;
    case TREMMENU_DEPTH: sendCommand(CMD_SET_TREMOLO_DEPTH, (uint8_t)value); break;
  }
}

// ============================================================================
// PITCH ENVELOPE SUBMENU HELPERS
// ============================================================================

int getPitchEnvValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case PENVMENU_AMT:   return vs.pitchEnvAmt;
    case PENVMENU_TIME:  return vs.pitchEnvTime;
    case PENVMENU_DIR:   return vs.pitchEnvDir;
    default: return 0;
  }
}

int getPitchEnvMax(int item) {
  switch (item) {
    case PENVMENU_AMT:   return 24;   // 0-24 semitones
    case PENVMENU_TIME:  return 127;  // 0-127 time
    case PENVMENU_DIR:   return 1;    // 0=down, 1=up
    default: return 0;
  }
}

void getPitchEnvValueStr(int item, int value, char* buf) {
  switch (item) {
    case PENVMENU_AMT:
      if (value == 0) strcpy(buf, "OFF");
      else sprintf(buf, "%dst", value);
      break;
    case PENVMENU_TIME:
      {
        // Convert to milliseconds: 10 + (value * 15)
        int ms = 10 + value * 15;
        sprintf(buf, "%dms", ms);
      }
      break;
    case PENVMENU_DIR:
      strcpy(buf, value ? "UP" : "DOWN");
      break;
    default:
      strcpy(buf, "?");
  }
}

const char* getPitchEnvLabel(int item) {
  switch (item) {
    case PENVMENU_AMT:   return "AMOUNT";
    case PENVMENU_TIME:  return "TIME";
    case PENVMENU_DIR:   return "DIR";
    case PENVMENU_BACK:  return "< BACK";
    default: return "?";
  }
}

void applyPitchEnvValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case PENVMENU_AMT:  sendCommand(CMD_SET_PITCH_ENV_AMT, (uint8_t)value); break;
    case PENVMENU_TIME: sendCommand(CMD_SET_PITCH_ENV_TIME, (uint8_t)value); break;
    case PENVMENU_DIR:  sendCommand(CMD_SET_PITCH_ENV_DIR, (uint8_t)value); break;
  }
}

// Check if a settings item should be visible
// SID only on chips 1+2 in SID mode, LINK only on Chip 0
bool isSettingsItemVisible(int item) {
  if (item == SETTINGS_SID) {
    // SID submenu only visible in SID mode for chips 1 and 2
    return sidModeGlobal && (currentChip == 1 || currentChip == 2);
  }
  if (item == SETTINGS_LINK) {
    return currentChip == 0;  // Only show LINK on Chip 0
  }
  return true;
}

// Navigate to next visible settings item
int nextVisibleSettingsItem(int current, int delta) {
  int next = current;
  int count = SETTINGS_ITEM_COUNT_CHIP0;

  // Loop until we find a visible item (or wrap around)
  for (int i = 0; i < count; i++) {
    next += delta;
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;

    if (isSettingsItemVisible(next)) {
      return next;
    }
  }
  return current;  // Fallback (shouldn't happen)
}

// Get voice range for current chip/scope
void getChipScopeVoices(uint8_t &start, uint8_t &end) {
  uint8_t base = currentChip * 3;  // 0, 3, or 6
  if (currentScope == 0) {
    // ALL voices on this chip
    start = base;
    end = base + 3;
  } else {
    // Single voice (A=1, B=2, C=3 maps to offset 0, 1, 2)
    start = base + (currentScope - 1);
    end = start + 1;
  }
}

// Set target for command queue based on current chip/scope
void setTargetForCommands() {
  SettingsTarget target;
  if (currentScope == 0) {
    target = (SettingsTarget)(TARGET_CHIP0 + currentChip);
  } else {
    target = (SettingsTarget)(TARGET_V1 + currentChip * 3 + (currentScope - 1));
  }
  sendCommand(CMD_SET_TARGET, (uint8_t)target);
}

// Apply a main settings value via command queue (Core 1 → Core 0)
void applySettingsValue(int item, int value) {
  // Chip is local to display - update and reset scope
  if (item == SETTINGS_CHIP) {
    // In SID mode, skip chip 0
    if (sidModeGlobal && value == 0) value = 1;
    currentChip = (uint8_t)value;
    currentScope = 0;
    settingsScrollOffset = 0;
    return;
  }

  // Scope is local to display - just update it directly
  if (item == SETTINGS_SCOPE) {
    currentScope = (uint8_t)value;
    return;
  }

  // LINK updates the voice link mask (global, not per-voice)
  if (item == SETTINGS_LINK) {
    uint8_t mask = linkOptionToMask(value);
    sendCommand(CMD_SET_VOICE_LINK_MASK, mask);
    return;
  }

  // Set target based on current chip/scope
  setTargetForCommands();

  // Then send the appropriate setting command
  switch (item) {
    case SETTINGS_NOISE:
      sendCommand(CMD_SET_NOISE_FREQ, (uint8_t)value);
      break;
  }
}

// Apply a vibrato submenu value
void applyVibratoValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case VIBMENU_ON:    sendCommand(CMD_SET_VIB_ON, value ? 127 : 0); break;  // ON=127, OFF=0
    case VIBMENU_RATE:  sendCommand(CMD_SET_VIB_RATE, (uint8_t)value); break;
    case VIBMENU_DEPTH: sendCommand(CMD_SET_VIB_DEPTH, (uint8_t)value); break;
    case VIBMENU_DELAY: sendCommand(CMD_SET_VIB_DELAY, (uint8_t)value); break;
  }
}

// Apply an envelope submenu value
void applyEnvelopeValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case ENVMENU_ATTACK:  sendCommand(CMD_SET_ENV_ATTACK, (uint8_t)value); break;
    case ENVMENU_DECAY:   sendCommand(CMD_SET_ENV_DECAY, (uint8_t)value); break;
    case ENVMENU_SUSTAIN: sendCommand(CMD_SET_ENV_SUSTAIN, (uint8_t)value); break;
    case ENVMENU_RELEASE: sendCommand(CMD_SET_ENV_RELEASE, (uint8_t)value); break;
  }
}

void applySidValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case SIDMENU_WAVE:      sendCommand(CMD_SET_SID_WAVE, (uint8_t)value); break;
    case SIDMENU_DUTY:      sendCommand(CMD_SET_SID_DUTY, (uint8_t)value); break;
    case SIDMENU_PWM_RATE:  sendCommand(CMD_SET_SID_PWM_RATE, (uint8_t)value); break;
    case SIDMENU_PWM_DEPTH: sendCommand(CMD_SET_SID_PWM_DEPTH, (uint8_t)value); break;
    case SIDMENU_NOISE:     sendCommand(CMD_SET_SID_NOISE, (uint8_t)value); break;
    case SIDMENU_SYNC:      sendCommand(CMD_SET_SID_SYNC, (uint8_t)value); break;
    case SIDMENU_RING:      sendCommand(CMD_SET_SID_RING, (uint8_t)value); break;
    case SIDMENU_RELEASE:   sendCommand(CMD_SET_ENV_RELEASE, (uint8_t)value); break;
  }
}

// Apply a pitch submenu value
void applyPitchValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case PITCHMENU_DETUNE: sendCommand(CMD_SET_DETUNE, 0, 0, 0, (int8_t)(value - 50)); break;
    case PITCHMENU_OCTAVE: sendCommand(CMD_SET_OCTAVE, 0, 0, 0, (int8_t)(value - 3)); break;
    case PITCHMENU_VOLUME: sendCommand(CMD_SET_MAX_VOLUME, (uint8_t)value); break;
  }
}

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================

bool displayInit() {
  Wire1.setSDA(PIN_OLED_SDA);
  Wire1.setSCL(PIN_OLED_SCL);
  Wire1.begin();
  delay(250);

  if (display.begin(0x3C, true)) {
    display.setContrast(displayBrightness);
    display.clearDisplay();
    display.display();
    return true;
  }
  return false;
}

// ============================================================================
// VISUALIZATION FUNCTIONS
// Moved to display_viz.cpp:
//   - updateDisplay()      (bars visualization)
//   - updateDisplayScope() (oscilloscope visualization)
//   - updateDisplayMatrix() (3x3 matrix visualization)
//   - getPotTargetPrefix()  (helper)
//   - getPotCatParam()      (helper)
// ============================================================================

// ============================================================================
// MENU RENDERING FUNCTIONS
// ============================================================================

void updateMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with line
  display.setCursor(0, 6);
  display.print("MENU");
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  const int itemH = 10;
  int y = 19;
  int displayModeIdx = editingValue && menuSelection == MENU_MODE ? tempModeValue : getCurrentModeIndex();
  int displayLinkIdx = editingValue && menuSelection == MENU_LINK ? tempLinkValue : displaySnapshotCopy.linkMode;

  // === Row 1: MODE and LINK on same line ===
  // MODE item (left side)
  if (menuSelection == MENU_MODE && !editingValue) {
    display.fillRect(0, y - 6, 44, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("MODE:");
  if (editingValue && menuSelection == MENU_MODE) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 20, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(getModeName(displayModeIdx));
  display.setTextColor(SH110X_WHITE);

  // LINK item (right side of same row)
  int linkX = 56;
  if (menuSelection == MENU_LINK && !editingValue) {
    display.fillRect(linkX - 2, y - 6, 56, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(linkX, y);
  display.print("CHP LINK:");
  if (editingValue && menuSelection == MENU_LINK) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 20, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(getLinkModeName(displayLinkIdx));
  display.setTextColor(SH110X_WHITE);

  // === Row 2: CHIP selection ===
  y += itemH;
  display.setCursor(2, y);
  display.print("CHIP:");

  int chipX = 28;
  // In SID mode, skip Chip 0 (only chips 1+2 are used)
  int startChip = sidModeGlobal ? 1 : 0;
  for (int c = startChip; c < 3; c++) {
    bool isSelected = (menuSelection == MENU_CHIP0 + c);
    if (isSelected) {
      display.fillRect(chipX - 1, y - 6, 14, itemH, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setCursor(chipX, y);
    display.print("[");
    display.print(c);
    display.print("]");
    display.setTextColor(SH110X_WHITE);
    chipX += 16;
  }

  // FX item (right side of CHIP row)
  int fxX = 78;
  if (menuSelection == MENU_FX) {
    display.fillRect(fxX - 2, y - 6, 26, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(fxX, y);
  display.print("FX");
  display.setTextColor(SH110X_WHITE);

  // === Row 3: POTS, PRST, MIDI, EXIT on same line ===
  y += itemH;

  // POTS item
  if (menuSelection == MENU_POTS) {
    display.fillRect(0, y - 6, 22, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("POTS");
  display.setTextColor(SH110X_WHITE);

  // PRST item
  int prstX = 28;
  if (menuSelection == MENU_PRESETS) {
    display.fillRect(prstX - 2, y - 6, 22, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(prstX, y);
  display.print("PRST");
  display.setTextColor(SH110X_WHITE);

  // SET item
  int midiX = 54;
  if (menuSelection == MENU_MIDI) {
    display.fillRect(midiX - 2, y - 6, 18, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(midiX, y);
  display.print("SET");
  display.setTextColor(SH110X_WHITE);

  // RST item
  int rstX = 80;
  if (menuSelection == MENU_RESET) {
    display.fillRect(rstX - 2, y - 6, 18, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(rstX, y);
  display.print("RST");
  display.setTextColor(SH110X_WHITE);

  // EXIT item
  int exitX = 104;
  if (menuSelection == MENU_EXIT) {
    display.fillRect(exitX - 2, y - 6, 22, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(exitX, y);
  display.print("EXIT");
  display.setTextColor(SH110X_WHITE);

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  if (editingValue) {
    display.setCursor(2, 62);
    display.print("Turn=adj  Push=save");
  }

  display.setFont(NULL);
  display.display();
}

void updateSettingsMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font for settings menu
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip indicator (TomThumb uses baseline, add 5 for height)
  display.setCursor(0, 6);
  display.print("CHIP ");
  display.print(currentChip);
  // Show scope on right side of header
  display.setCursor(90, 6);
  display.print(getScopeName(currentScope));
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Build list of visible item indices
  int visibleItems[SETTINGS_ITEM_COUNT_CHIP0];
  int visibleCount = 0;
  for (int i = 0; i < SETTINGS_ITEM_COUNT_CHIP0; i++) {
    if (isSettingsItemVisible(i)) {
      visibleItems[visibleCount++] = i;
    }
  }

  // Find selection position in visible items
  int selectionPos = 0;
  for (int i = 0; i < visibleCount; i++) {
    if (visibleItems[i] == settingsSelection) {
      selectionPos = i;
      break;
    }
  }

  // Show 7 items with 6px spacing (TomThumb is smaller)
  const int maxDisplay = 7;
  const int itemHeight = 6;

  // Adjust scroll to keep selection visible
  if (selectionPos < settingsScrollOffset) {
    settingsScrollOffset = selectionPos;
  } else if (selectionPos >= settingsScrollOffset + maxDisplay) {
    settingsScrollOffset = selectionPos - maxDisplay + 1;
  }

  // Draw visible items (TomThumb uses baseline, add 5 for height)
  int y = 16;
  for (int i = 0; i < maxDisplay && (settingsScrollOffset + i) < visibleCount; i++) {
    int itemIdx = visibleItems[settingsScrollOffset + i];
    bool isSelected = (itemIdx == settingsSelection);
    bool isEditing = (isSelected && settingsEditing);

    // Highlight selected item (adjust for TomThumb baseline)
    if (isSelected && !isEditing) {
      display.fillRect(0, y - 5, 118, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getSettingsLabel(itemIdx));

    // Show value or submenu indicator
    if (itemIdx == SETTINGS_PITCH || itemIdx == SETTINGS_VIBRATO ||
        itemIdx == SETTINGS_TREMOLO || itemIdx == SETTINGS_ENVELOPE || itemIdx == SETTINGS_GLIDE ||
        itemIdx == SETTINGS_PITCH_ENV) {
      // Submenu - show arrow
      display.setCursor(110, y);
      display.print(">");
    }
    else if (itemIdx != SETTINGS_BACK) {
      display.print(": ");

      char valBuf[12];
      int val = isEditing ? settingsTempValue : getSettingsValue(itemIdx);
      getSettingsValueStr(itemIdx, val, valBuf);

      if (isEditing) {
        // Highlight value when editing (TomThumb is ~4px wide per char)
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 5, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Scroll indicators on right edge
  if (settingsScrollOffset > 0) {
    display.fillTriangle(124, 14, 120, 19, 128, 19, SH110X_WHITE);
  }
  if (settingsScrollOffset + maxDisplay < visibleCount) {
    display.fillTriangle(124, 53, 120, 48, 128, 48, SH110X_WHITE);
  }

  // Footer with hints
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);  // TomThumb uses baseline positioning
  if (settingsEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=sel  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

// Draw envelope visualization
void drawEnvelopeVisual(int attack, int decay, int sustain) {
  // Envelope graph area: right side of screen (smaller to fit labels)
  const int graphX = 68;
  const int graphY = 12;
  const int graphW = 56;
  const int graphH = 28;

  // Draw border
  display.drawRect(graphX, graphY, graphW, graphH, SH110X_WHITE);

  // Calculate envelope points
  int attackW = map(attack, 0, 127, 2, 18);
  int decayW = map(decay, 0, 127, 2, 14);
  int sustainY = graphY + graphH - 2 - map(sustain, 0, 127, 0, graphH - 4);

  // Points for envelope shape
  int x0 = graphX + 2;
  int y0 = graphY + graphH - 2;
  int x1 = x0 + attackW;
  int y1 = graphY + 2;
  int x2 = x1 + decayW;
  int y2 = sustainY;
  int x3 = graphX + graphW - 2;

  // Draw envelope lines
  display.drawLine(x0, y0, x1, y1, SH110X_WHITE);  // Attack slope
  display.drawLine(x1, y1, x2, y2, SH110X_WHITE);  // Decay slope
  display.drawLine(x2, y2, x3, y2, SH110X_WHITE);  // Sustain line

  // Labels below graph
  display.setTextSize(1);
  display.setCursor(graphX + 4, graphY + graphH + 3);
  display.print("A  D  S");
}

// Draw vibrato visualization - amplitude shows depth directly
void drawVibratoVisual(int vibOn, int rate, int depth) {
  const int gx = 68, gy = 12, gw = 56, gh = 28;
  display.drawRect(gx, gy, gw, gh, SH110X_WHITE);
  int cy = gy + gh / 2;

  if (vibOn == 0) {
    // Flat line when vibrato is off
    display.drawFastHLine(gx + 2, cy, gw - 4, SH110X_WHITE);
  } else {
    // Amplitude directly from depth: 0-200 maps to 2-12 pixels
    int amp = 2 + (depth * 10) / 200;
    if (amp > 12) amp = 12;
    // 3-cycle zigzag
    display.drawLine(gx + 4, cy, gx + 12, cy - amp, SH110X_WHITE);
    display.drawLine(gx + 12, cy - amp, gx + 21, cy + amp, SH110X_WHITE);
    display.drawLine(gx + 21, cy + amp, gx + 30, cy - amp, SH110X_WHITE);
    display.drawLine(gx + 30, cy - amp, gx + 39, cy + amp, SH110X_WHITE);
    display.drawLine(gx + 39, cy + amp, gx + 48, cy - amp, SH110X_WHITE);
    display.drawLine(gx + 48, cy - amp, gx + gw - 4, cy, SH110X_WHITE);
  }
}

void updateVibratoSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" VIBRATO");

  // Draw scope indicator - highlight if selected/editing
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;  // TomThumb ~4px per char

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Get vibrato values from snapshot (thread-safe)
  int vibOn = (submenuSelection == VIBMENU_ON && submenuEditing)
              ? submenuTempValue : getVibratoValue(VIBMENU_ON);
  int rate = (submenuSelection == VIBMENU_RATE && submenuEditing)
             ? submenuTempValue : getVibratoValue(VIBMENU_RATE);
  int depth = (submenuSelection == VIBMENU_DEPTH && submenuEditing)
              ? submenuTempValue : getVibratoValue(VIBMENU_DEPTH);

  // Draw vibrato visualization
  drawVibratoVisual(vibOn, rate, depth);

  // Draw parameter list on left side (6px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < VIBMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getVibratoLabel(i));

    if (i != VIBMENU_BACK) {
      display.print(": ");
      char valBuf[12];
      int val = isEditing ? submenuTempValue : getVibratoValue(i);
      getVibratoValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

void updateEnvelopeSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" ENVELOPE");

  // Draw scope indicator - highlight if selected/editing
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;  // TomThumb ~4px per char

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Get envelope values from snapshot (thread-safe)
  int attack = (submenuSelection == ENVMENU_ATTACK && submenuEditing)
               ? submenuTempValue : getEnvelopeValue(ENVMENU_ATTACK);
  int decay = (submenuSelection == ENVMENU_DECAY && submenuEditing)
              ? submenuTempValue : getEnvelopeValue(ENVMENU_DECAY);
  int sustain = (submenuSelection == ENVMENU_SUSTAIN && submenuEditing)
                ? submenuTempValue : getEnvelopeValue(ENVMENU_SUSTAIN);

  // Draw envelope visualization
  drawEnvelopeVisual(attack, decay, sustain);

  // Draw parameter list on left side (8px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < ENVMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getEnvelopeLabel(i));

    if (i != ENVMENU_BACK) {
      display.print(": ");
      char valBuf[8];
      int val = isEditing ? submenuTempValue : getEnvelopeValue(i);
      getEnvelopeValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

void updateSidSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" SID");

  // Draw scope indicator
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Draw parameter list with scrolling (9 items, show 5 at a time)
  const int itemHeight = 8;
  const int visibleItems = 5;
  int scrollOffset = 0;
  if (submenuSelection > visibleItems - 2) {
    scrollOffset = submenuSelection - (visibleItems - 2);
    if (scrollOffset > SIDMENU_ITEM_COUNT - visibleItems)
      scrollOffset = SIDMENU_ITEM_COUNT - visibleItems;
  }
  if (scrollOffset < 0) scrollOffset = 0;

  int y = 17;
  for (int idx = 0; idx < visibleItems && (scrollOffset + idx) < SIDMENU_ITEM_COUNT; idx++) {
    int i = scrollOffset + idx;
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 80, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getSidLabel(i));

    if (i != SIDMENU_BACK) {
      display.print(": ");
      char valBuf[8];
      int val = isEditing ? submenuTempValue : getSidValue(i);
      getSidValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Scroll indicators
  if (scrollOffset > 0) {
    display.setCursor(120, 17);
    display.print("^");
  }
  if (scrollOffset + visibleItems < SIDMENU_ITEM_COUNT) {
    display.setCursor(120, 17 + (visibleItems - 1) * itemHeight);
    display.print("v");
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);
  display.display();
}

void updatePitchSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" PITCH");

  // Draw scope indicator - highlight if selected/editing
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;  // TomThumb ~4px per char

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Get pitch values from snapshot (thread-safe)
  int detune = (submenuSelection == PITCHMENU_DETUNE && submenuEditing)
               ? submenuTempValue : getPitchValue(PITCHMENU_DETUNE);
  int octave = (submenuSelection == PITCHMENU_OCTAVE && submenuEditing)
               ? submenuTempValue : getPitchValue(PITCHMENU_OCTAVE);

  // Draw pitch visualization - show pitch offset as a bar
  {
    const int gx = 68, gy = 12, gw = 56, gh = 28;
    display.drawRect(gx, gy, gw, gh, SH110X_WHITE);
    int cy = gy + gh / 2;

    // Draw center line
    display.drawFastHLine(gx + 2, cy, gw - 4, SH110X_WHITE);

    // Draw octave indicator (large offset)
    int octOffset = (octave - 3) * 8;  // -24 to +24 pixels
    if (octOffset != 0) {
      int barY = cy - octOffset;
      if (barY < gy + 2) barY = gy + 2;
      if (barY > gy + gh - 4) barY = gy + gh - 4;
      display.fillRect(gx + 6, min(cy, barY), 20, abs(cy - barY) + 2, SH110X_WHITE);
    }

    // Draw detune indicator (fine offset)
    int detOffset = ((detune - 50) * 10) / 50;  // -10 to +10 pixels
    int detY = cy - detOffset;
    display.fillCircle(gx + gw - 14, detY, 3, SH110X_WHITE);
  }

  // Draw parameter list on left side (8px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < PITCHMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getPitchLabel(i));

    if (i != PITCHMENU_BACK) {
      display.print(": ");
      char valBuf[12];
      int val = isEditing ? submenuTempValue : getPitchValue(i);
      getPitchValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

void updateGlideSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" GLIDE");

  // Draw scope indicator - highlight if selected/editing
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;  // TomThumb ~4px per char

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Draw parameter list (8px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < GLIDEMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getGlideLabel(i));

    if (i != GLIDEMENU_BACK) {
      display.print(": ");
      char valBuf[12];
      int val = isEditing ? submenuTempValue : getGlideValue(i);
      getGlideValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

void updateTremoloSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" TREMOLO");

  // Draw scope indicator - highlight if selected/editing
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Draw parameter list (8px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < TREMMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getTremoloLabel(i));

    if (i != TREMMENU_BACK) {
      display.print(": ");
      char valBuf[12];
      int val = isEditing ? submenuTempValue : getTremoloValue(i);
      getTremoloValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

void updatePitchEnvSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");

  // Draw chip indicator - highlight if selected/editing
  bool chipSelected = (submenuSelection == -2);
  bool chipEditing = chipSelected && submenuEditing;
  int chipVal = chipEditing ? submenuTempValue : currentChip;
  if (chipSelected) {
    int chipX = display.getCursorX();
    display.fillRect(chipX - 1, 0, 6, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(chipVal);
  display.setTextColor(SH110X_WHITE);
  display.print(" PITCH ENV");

  // Draw scope indicator - highlight if selected/editing
  bool scopeSelected = (submenuSelection == -1);
  bool scopeEditing = scopeSelected && submenuEditing;
  int scopeX = 100;
  const char* scopeStr = getScopeName(scopeEditing ? submenuTempValue : currentScope);
  int scopeW = strlen(scopeStr) * 4 + 4;

  if (scopeSelected) {
    display.fillRect(scopeX - 2, 0, scopeW, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(scopeX, 6);
  display.print(scopeStr);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Draw parameter list (8px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < PENVMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getPitchEnvLabel(i));

    if (i != PENVMENU_BACK) {
      display.print(": ");
      char valBuf[12];
      int val = isEditing ? submenuTempValue : getPitchEnvValue(i);
      getPitchEnvValueStr(i, val, valBuf);

      if (isEditing) {
        display.setTextColor(SH110X_WHITE);
        int vx = display.getCursorX();
        int vw = strlen(valBuf) * 4 + 4;
        display.fillRect(vx - 2, y - 6, vw, itemHeight, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(valBuf);
    }

    display.setTextColor(SH110X_WHITE);
    y += itemHeight;
  }

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (submenuEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}
