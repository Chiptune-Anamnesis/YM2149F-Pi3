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
bool presetSaving = false;
unsigned long presetSaveStartTime = 0;
bool presetDeleting = false;
unsigned long presetDeleteStartTime = 0;
#define PRESET_SAVE_DISPLAY_MS 600  // Show "Saving..." for this long

// MIDI menu state
int midiMenuSelection = 0;
bool midiEditing = false;
int midiTempValue = 0;

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
    case SETTINGS_SCOPE:    return currentScope;
    case SETTINGS_LINK:     return maskToLinkOption(displaySnapshotCopy.voiceLinkMask);
    case SETTINGS_NOISE:    return vs.noiseFreq;
    default: return 0;
  }
}

// Get max value for a main settings item
int getSettingsMax(int item) {
  switch (item) {
    case SETTINGS_SCOPE:    return SCOPE_COUNT - 1;  // 0-3 (ALL, A, B, C)
    case SETTINGS_LINK:     return 3;    // 0=OFF, 1=+B, 2=+C, 3=ALL
    case SETTINGS_NOISE:    return 31;
    default: return 0;
  }
}

// Get display string for a main settings value
void getSettingsValueStr(int item, int value, char* buf) {
  switch (item) {
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
    case SETTINGS_SCOPE:     return "SCOPE";
    case SETTINGS_LINK:      return "LINK";
    case SETTINGS_PITCH:     return "PITCH";
    case SETTINGS_VIBRATO:   return "VIBRATO";
    case SETTINGS_TREMOLO:   return "TREMOLO";
    case SETTINGS_NOISE:     return "NOISE";
    case SETTINGS_ENVELOPE:  return "ENVELOPE";
    case SETTINGS_SID:       return "SID";
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
    default: return 0;
  }
}

int getEnvelopeMax(int item) {
  switch (item) {
    case ENVMENU_ATTACK:  return 127;
    case ENVMENU_DECAY:   return 127;
    case ENVMENU_SUSTAIN: return 127;
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
    case ENVMENU_BACK:    return "< BACK";
    default: return "?";
  }
}

// Get value for SID submenu item
int getSIDValue(int item) {
  int v = getFirstVoiceForScope();
  const VoiceSettings& vs = displaySnapshotCopy.voiceSettings[v];
  switch (item) {
    case SIDMENU_ON:    return vs.sidOn;
    case SIDMENU_WAVE:  return vs.sidWave;
    case SIDMENU_DUTY:  return vs.sidDuty;
    default: return 0;
  }
}

int getSIDMax(int item) {
  switch (item) {
    case SIDMENU_ON:    return 1;
    case SIDMENU_WAVE:  return 3;
    case SIDMENU_DUTY:  return 16;
    default: return 0;
  }
}

void getSIDValueStr(int item, int value, char* buf) {
  switch (item) {
    case SIDMENU_ON:
      strcpy(buf, value ? "ON" : "OFF");
      break;
    case SIDMENU_WAVE:
      switch (value) {
        case 0: strcpy(buf, "SQR"); break;
        case 1: strcpy(buf, "SAW"); break;
        case 2: strcpy(buf, "TRI"); break;
        case 3: strcpy(buf, "PLS"); break;
        default: strcpy(buf, "?"); break;
      }
      break;
    case SIDMENU_DUTY:
      sprintf(buf, "%d", value);
      break;
    default:
      strcpy(buf, "?");
  }
}

const char* getSIDLabel(int item) {
  switch (item) {
    case SIDMENU_ON:    return "ON";
    case SIDMENU_WAVE:  return "WAVE";
    case SIDMENU_DUTY:  return "DUTY";
    case SIDMENU_BACK:  return "< BACK";
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

// Check if a settings item should be visible (LINK only on Chip 0)
bool isSettingsItemVisible(int item) {
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
  }
}

// Apply a SID submenu value
void applySIDValue(int item, int value) {
  setTargetForCommands();
  switch (item) {
    case SIDMENU_ON:   sendCommand(CMD_SET_SID_ON, (uint8_t)value); break;
    case SIDMENU_WAVE: sendCommand(CMD_SET_SID_WAVE, (uint8_t)value); break;
    case SIDMENU_DUTY: sendCommand(CMD_SET_SID_DUTY, (uint8_t)value); break;
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
    display.clearDisplay();
    display.display();
    return true;
  }
  return false;
}

// Get target/scope prefix for pot (e.g., "FX:", "ALL:", "V1:")
const char* getPotTargetPrefix(const PotAssignment& pa) {
  // FX categories don't have voice targets
  if (pa.category >= PCAT_FX_ECHO && pa.category <= PCAT_FX_CHORUS) {
    return "FX:";
  }
  if (pa.category == PCAT_SAMPLE) return "SMP:";
  if (pa.category == PCAT_GLOBAL) return "GLB:";
  if (pa.category == PCAT_OFF) return "";

  // Voice-targeted parameters
  switch (pa.target) {
    case TARGET_ALL: return "ALL:";
    case TARGET_V1:  return "V1:";
    case TARGET_V2:  return "V2:";
    case TARGET_V3:  return "V3:";
    case TARGET_V4:  return "V4:";
    case TARGET_V5:  return "V5:";
    case TARGET_V6:  return "V6:";
    case TARGET_V7:  return "V7:";
    case TARGET_V8:  return "V8:";
    case TARGET_V9:  return "V9:";
    default:         return "?:";
  }
}

// Get category:param suffix for pot (e.g., "CHR:DET", "ENV:ATK")
const char* getPotCatParam(const PotAssignment& pa) {
  if (pa.category == PCAT_OFF) return "OFF";

  switch (pa.category) {
    case PCAT_VOICE:
      switch (pa.paramIndex) {
        case 0: return "VC:DET";
        case 1: return "VC:OCT";
        case 2: return "VC:VOL";
        case 3: return "VC:NSE";
        case 4: return "VC:SLD";
      }
      break;
    case PCAT_VIBRATO:
      switch (pa.paramIndex) {
        case 0: return "VIB:RT";
        case 1: return "VIB:DP";
        case 2: return "VIB:DL";
      }
      break;
    case PCAT_ENVELOPE:
      switch (pa.paramIndex) {
        case 0: return "ENV:ATK";
        case 1: return "ENV:DCY";
        case 2: return "ENV:SUS";
      }
      break;
    case PCAT_TREMOLO:
      switch (pa.paramIndex) {
        case 0: return "TRM:RT";
        case 1: return "TRM:DP";
      }
      break;
    case PCAT_PITCH_ENV:
      switch (pa.paramIndex) {
        case 0: return "PEN:AMT";
        case 1: return "PEN:TM";
        case 2: return "PEN:DIR";
      }
      break;
    case PCAT_SID:
      switch (pa.paramIndex) {
        case 0: return "SID:WAV";
        case 1: return "SID:DTY";
        case 2: return "SID:DET";
      }
      break;
    case PCAT_FX_ECHO:
      switch (pa.paramIndex) {
        case 0: return "ECO:DLY";
        case 1: return "ECO:RPT";
        case 2: return "ECO:DCY";
        case 3: return "ECO:VOL";
      }
      break;
    case PCAT_FX_ARP:
      switch (pa.paramIndex) {
        case 0: return "ARP:SPD";
        case 1: return "ARP:PTN";
        case 2: return "ARP:VOL";
        case 3: return "ARP:OCT";
      }
      break;
    case PCAT_FX_CRUSH:
      switch (pa.paramIndex) {
        case 0: return "CRU:BIT";
        case 1: return "CRU:RAT";
        case 2: return "CRU:VOL";
        case 3: return "CRU:DUR";
      }
      break;
    case PCAT_FX_REVERB:
      switch (pa.paramIndex) {
        case 0: return "RVB:TAP";
        case 1: return "RVB:SPC";
        case 2: return "RVB:DCY";
        case 3: return "RVB:DET";
        case 4: return "RVB:VOL";
      }
      break;
    case PCAT_FX_CHORUS:
      switch (pa.paramIndex) {
        case 0: return "CHR:DT1";
        case 1: return "CHR:DT2";
        case 2: return "CHR:VOL";
        case 3: return "CHR:DUR";
      }
      break;
    case PCAT_SAMPLE:
      switch (pa.paramIndex) {
        case 0: return "SEL";
        case 1: return "MOD";
        case 2: return "VOL";
      }
      break;
    case PCAT_GLOBAL:
      switch (pa.paramIndex) {
        case 0: return "MODE";
        case 1: return "LINK";
      }
      break;
    default: break;
  }
  return "???";
}

void updateDisplay() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  lastUpdate = now;

  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // === TOP SECTION: 3 Pot Circles with Labels (top + bottom text) ===
  const int potRadius = 7;
  const int potY = 15;  // Center Y of circles
  const int potSpacing = 42;
  const int potStartX = 21;

  for (uint8_t p = 0; p < 3; p++) {
    int cx = potStartX + p * potSpacing;
    int potVal = displaySnapshotCopy.potValues[p];
    const PotAssignment& pa = displaySnapshotCopy.potAssignments[p];

    // Top label: target prefix (e.g., "FX:", "ALL:", "V1:")
    const char* prefix = getPotTargetPrefix(pa);
    int prefixLen = strlen(prefix);
    display.setCursor(cx - (prefixLen * 3), potY - potRadius - 3);
    display.print(prefix);

    // Draw circle outline
    display.drawCircle(cx, potY, potRadius, SH110X_WHITE);

    // Fill based on pot value (0-1000 mapped to fill height)
    int fillHeight = map(constrain(potVal, 0, 1000), 0, 1000, 0, potRadius * 2);
    if (fillHeight > 0) {
      for (int y = 0; y < fillHeight; y++) {
        int dy = potRadius - y;
        if (dy >= -potRadius && dy <= potRadius) {
          int halfWidth = (int)sqrt((float)(potRadius * potRadius - dy * dy));
          if (halfWidth > 0) {
            display.drawFastHLine(cx - halfWidth, potY + dy, halfWidth * 2, SH110X_WHITE);
          }
        }
      }
    }

    // Bottom label: category:param (e.g., "CHR:DET", "ENV:ATK")
    const char* catParam = getPotCatParam(pa);
    int catLen = strlen(catParam);
    display.setCursor(cx - (catLen * 3), potY + potRadius + 9);
    display.print(catParam);
  }

  // === MIDDLE SECTION: 9 Voice Bars (compact) ===
  const int barWidth = 12;
  const int barSpacing = 1;
  const int barMaxHeight = 20;
  const int barBaseY = 53;
  const int barTopY = barBaseY - barMaxHeight;

  for (uint8_t ch = 0; ch < 9; ch++) {
    int x = 3 + ch * (barWidth + barSpacing);
    uint8_t chip = ch / 3;
    uint8_t voice = ch % 3;

    int barHeight = 0;
    if (displaySnapshotCopy.voiceActive[chip][voice] && displaySnapshotCopy.voiceVol[chip][voice] > 0) {
      barHeight = map(displaySnapshotCopy.voiceVol[chip][voice], 0, 15, 3, barMaxHeight);
    }

    if (barHeight > 0) {
      display.fillRect(x, barBaseY - barHeight, barWidth, barHeight, SH110X_WHITE);
    }

    display.drawRect(x, barTopY, barWidth, barMaxHeight, SH110X_WHITE);
  }

  // === BOTTOM SECTION: FX type (left) and HOBBYCHOP (right) ===
  const int bottomY = 63;

  // FX type on left
  display.setCursor(0, bottomY);
  display.print("FX:");
  if (displaySnapshotCopy.fxModeEnabled && displaySnapshotCopy.fxType != FX_NONE) {
    display.print(getFxTypeName(displaySnapshotCopy.fxType));
  } else {
    display.print("NONE");
  }

  // Preset name on right
  display.setCursor(73, bottomY);
  display.print("PRST:");
  if (currentPresetIndex == PRESET_INDEX_NONE) {
    display.print("---");
  } else {
    char nameBuf[9];
    presetGetName(currentPresetIndex, nameBuf);
    display.print(nameBuf);
  }

  display.display();
}

// ============================================================================
// OSCILLOSCOPE VISUALIZATION
// ============================================================================

void updateDisplayScope() {
  static unsigned long lastUpdate = 0;
  static float phaseAccum[9] = {0};  // Persistent phase accumulators per voice

  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  float dt = (now - lastUpdate) / 1000.0f;
  lastUpdate = now;

  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // === SCOPE AREA: Full width, y=4 to y=50 ===
  const int scopeY = 4;
  const int scopeH = 46;
  const int scopeMidY = scopeY + scopeH / 2;

  // Draw center line (zero crossing)
  display.drawFastHLine(0, scopeMidY, 128, SH110X_WHITE);

  // Update phase accumulators for each active voice
  for (uint8_t ch = 0; ch < 9; ch++) {
    uint8_t chip = ch / 3;
    uint8_t voice = ch % 3;

    if (displaySnapshotCopy.voiceActive[chip][voice] &&
        displaySnapshotCopy.voiceVol[chip][voice] > 0) {
      // Derive frequency from actual YM2149 period (captures all pitch mods)
      uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
      float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;

      // Scale frequency for visual appeal (show ~2-4 cycles on screen)
      float visualFreq = freq * 0.002f;
      phaseAccum[ch] += visualFreq * dt * 60.0f;  // 60 = rough frame scaling
      if (phaseAccum[ch] > 1000.0f) phaseAccum[ch] -= 1000.0f;
    } else {
      // Slowly decay phase when voice stops
      phaseAccum[ch] *= 0.95f;
    }
  }

  // Render composite waveform
  int prevY = scopeMidY;
  for (int x = 0; x < 128; x++) {
    float sum = 0;
    int activeCount = 0;

    for (uint8_t ch = 0; ch < 9; ch++) {
      uint8_t chip = ch / 3;
      uint8_t voice = ch % 3;

      if (displaySnapshotCopy.voiceActive[chip][voice] &&
          displaySnapshotCopy.voiceVol[chip][voice] > 0) {
        // Derive frequency from actual YM2149 period (captures all pitch mods)
        uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
        float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;

        // Phase for this x position (spread across screen width)
        float phase = phaseAccum[ch] + (x / 128.0f) * (freq * 0.01f);

        // Square wave (YM2149 native waveform)
        float wave = (fmodf(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f;

        // Scale by volume
        float vol = displaySnapshotCopy.voiceVol[chip][voice] / 15.0f;
        sum += wave * vol;
        activeCount++;
      }
    }

    int y = scopeMidY;
    if (activeCount > 0) {
      sum /= sqrtf((float)activeCount);  // Normalize with gentle rolloff
      y = scopeMidY - (int)(sum * (scopeH / 2 - 4));
      y = constrain(y, scopeY + 1, scopeY + scopeH - 2);
    }

    // Draw line from previous point for smooth waveform
    if (x > 0) {
      display.drawLine(x - 1, prevY, x, y, SH110X_WHITE);
    }
    prevY = y;
  }

  // === BOTTOM SECTION: Same as bar mode ===
  const int bottomY = 63;

  // FX type on left
  display.setCursor(0, bottomY);
  display.print("FX:");
  if (displaySnapshotCopy.fxModeEnabled && displaySnapshotCopy.fxType != FX_NONE) {
    display.print(getFxTypeName(displaySnapshotCopy.fxType));
  } else {
    display.print("NONE");
  }

  // Preset name on right
  display.setCursor(73, bottomY);
  display.print("PRST:");
  if (currentPresetIndex == PRESET_INDEX_NONE) {
    display.print("---");
  } else {
    char nameBuf[9];
    presetGetName(currentPresetIndex, nameBuf);
    display.print(nameBuf);
  }

  display.display();
}

void updateDisplayMatrix() {
  static unsigned long lastUpdate = 0;
  static float phaseAccum[9] = {0};  // Persistent phase accumulators per voice

  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  float dt = (now - lastUpdate) / 1000.0f;
  lastUpdate = now;

  display.clearDisplay();

  // === 3x3 GRID LAYOUT ===
  // Screen: 128x64, Grid: 3 cols x 3 rows
  // Cell size: 42x21 pixels (with 1px borders)
  const int cellW = 42;
  const int cellH = 21;
  const int innerW = cellW - 2;  // 40 pixels for waveform
  const int innerH = cellH - 2;  // 19 pixels for waveform

  // Update phase accumulators for each voice
  for (uint8_t ch = 0; ch < 9; ch++) {
    uint8_t chip = ch / 3;
    uint8_t voice = ch % 3;

    if (displaySnapshotCopy.voiceActive[chip][voice] &&
        displaySnapshotCopy.voiceVol[chip][voice] > 0) {
      // Derive frequency from actual YM2149 period (captures all pitch mods)
      uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
      float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;
      float visualFreq = freq * 0.002f;
      phaseAccum[ch] += visualFreq * dt * 60.0f;
      if (phaseAccum[ch] > 1000.0f) phaseAccum[ch] -= 1000.0f;
    } else {
      phaseAccum[ch] *= 0.95f;
    }
  }

  // Draw each cell
  for (uint8_t chip = 0; chip < 3; chip++) {
    for (uint8_t voice = 0; voice < 3; voice++) {
      uint8_t ch = chip * 3 + voice;

      // Cell position (voice = column, chip = row)
      int cellX = voice * cellW;
      int cellY = chip * cellH;
      int innerX = cellX + 1;
      int innerY = cellY + 1;
      int midY = innerY + innerH / 2;

      bool isActive = displaySnapshotCopy.voiceActive[chip][voice] &&
                      displaySnapshotCopy.voiceVol[chip][voice] > 0;

      // Draw cell border (dim for inactive, bright for active)
      if (isActive) {
        display.drawRect(cellX, cellY, cellW, cellH, SH110X_WHITE);
      } else {
        // Just corner dots for inactive cells
        display.drawPixel(cellX, cellY, SH110X_WHITE);
        display.drawPixel(cellX + cellW - 1, cellY, SH110X_WHITE);
        display.drawPixel(cellX, cellY + cellH - 1, SH110X_WHITE);
        display.drawPixel(cellX + cellW - 1, cellY + cellH - 1, SH110X_WHITE);
      }

      // Draw cell label (e.g., "0A", "1B", "2C")
      display.setFont(&TomThumb);
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(innerX + 1, innerY + 5);
      display.print(chip);
      display.print((char)('A' + voice));

      // Draw center line
      display.drawFastHLine(innerX, midY, innerW, SH110X_WHITE);

      if (isActive) {
        // Derive frequency from actual YM2149 period (captures all pitch mods)
        uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
        float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;
        float vol = displaySnapshotCopy.voiceVol[chip][voice] / 15.0f;

        int prevY = midY;
        for (int x = 0; x < innerW; x++) {
          float phase = phaseAccum[ch] + (x / (float)innerW) * (freq * 0.008f);
          float wave = (fmodf(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f;

          int amplitude = (int)(wave * vol * (innerH / 2 - 1));
          int y = midY - amplitude;
          y = constrain(y, innerY + 1, innerY + innerH - 2);

          if (x > 0) {
            display.drawLine(innerX + x - 1, prevY, innerX + x, y, SH110X_WHITE);
          }
          prevY = y;
        }
      }
    }
  }

  display.display();
}

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
  int linkX = 68;
  if (menuSelection == MENU_LINK && !editingValue) {
    display.fillRect(linkX - 2, y - 6, 44, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(linkX, y);
  display.print("LINK:");
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
  for (int c = 0; c < 3; c++) {
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
  int fxX = 82;
  if (menuSelection == MENU_FX) {
    display.fillRect(fxX - 2, y - 6, 36, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(fxX, y);
  display.print("FX:");
  display.print(displaySnapshotCopy.fxModeEnabled ? "ON" : "OFF");
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
  int prstX = 32;
  if (menuSelection == MENU_PRESETS) {
    display.fillRect(prstX - 2, y - 6, 22, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(prstX, y);
  display.print("PRST");
  display.setTextColor(SH110X_WHITE);

  // MIDI item
  int midiX = 62;
  if (menuSelection == MENU_MIDI) {
    display.fillRect(midiX - 2, y - 6, 22, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(midiX, y);
  display.print("MIDI");
  display.setTextColor(SH110X_WHITE);

  // EXIT item
  int exitX = 96;
  if (menuSelection == MENU_EXIT) {
    display.fillRect(exitX - 2, y - 6, 22, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(exitX, y);
  display.print("EXIT");
  display.setTextColor(SH110X_WHITE);

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (editingValue) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
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
    if (itemIdx == SETTINGS_PITCH || itemIdx == SETTINGS_VIBRATO || itemIdx == SETTINGS_TREMOLO ||
        itemIdx == SETTINGS_ENVELOPE || itemIdx == SETTINGS_SID || itemIdx == SETTINGS_GLIDE ||
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
  display.print(currentChip);
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
  display.print(currentChip);
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

void updateSIDSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");
  display.print(currentChip);
  display.print(" SID");

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

  // Get SID values from snapshot (thread-safe)
  int sidOn = (submenuSelection == SIDMENU_ON && submenuEditing)
              ? submenuTempValue : getSIDValue(SIDMENU_ON);
  int wave = (submenuSelection == SIDMENU_WAVE && submenuEditing)
             ? submenuTempValue : getSIDValue(SIDMENU_WAVE);
  int duty = (submenuSelection == SIDMENU_DUTY && submenuEditing)
             ? submenuTempValue : getSIDValue(SIDMENU_DUTY);

  // Simple SID waveform visualization (minimal code)
  {
    const int gx = 68, gy = 12, gw = 56, gh = 28;
    display.drawRect(gx, gy, gw, gh, SH110X_WHITE);
    int cy = gy + gh / 2;
    // Just show wave type with simple shapes
    if (wave == 0) {  // Square - two horizontal lines
      display.drawFastHLine(gx + 4, gy + 6, 24, SH110X_WHITE);
      display.drawFastHLine(gx + 28, gy + gh - 6, 24, SH110X_WHITE);
      display.drawFastVLine(gx + 28, gy + 6, gh - 12, SH110X_WHITE);
    } else if (wave == 1) {  // Saw - diagonal
      display.drawLine(gx + 4, gy + gh - 4, gx + gw - 4, gy + 4, SH110X_WHITE);
    } else if (wave == 2) {  // Triangle
      display.drawLine(gx + 4, cy, gx + gw/2, gy + 4, SH110X_WHITE);
      display.drawLine(gx + gw/2, gy + 4, gx + gw - 4, cy, SH110X_WHITE);
    } else {  // Pulse - narrow
      display.drawFastHLine(gx + 4, gy + 6, 8, SH110X_WHITE);
      display.drawFastHLine(gx + 12, gy + gh - 6, gw - 16, SH110X_WHITE);
      display.drawFastVLine(gx + 12, gy + 6, gh - 12, SH110X_WHITE);
    }
  }

  // Draw parameter list on left side (8px spacing for TomThumb)
  const int itemHeight = 8;
  int y = 17;
  for (int i = 0; i < SIDMENU_ITEM_COUNT; i++) {
    bool isSelected = (i == submenuSelection);
    bool isEditing = (isSelected && submenuEditing);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 6, 64, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(2, y);
    display.print(getSIDLabel(i));

    if (i != SIDMENU_BACK) {
      display.print(": ");
      char valBuf[8];
      int val = isEditing ? submenuTempValue : getSIDValue(i);
      getSIDValueStr(i, val, valBuf);

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

void updatePitchSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header with chip and scope indicator (TomThumb uses baseline)
  display.setCursor(0, 6);
  display.print("CHIP ");
  display.print(currentChip);
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
  display.print(currentChip);
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
  display.print(currentChip);
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
  display.print(currentChip);
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

void updatePotsMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setCursor(0, 6);
  display.print("POT ASSIGN");
  display.drawLine(0, 8, 127, 8, SH110X_WHITE);

  // Draw pot assignments (5 items: POT1-4 + BACK)
  int y = 17;
  const int rowH = 10;
  const int charW = 4;  // TomThumb character width

  for (int i = 0; i < POTS_ITEM_COUNT; i++) {
    bool isSelected = (i == potsSelection);
    bool isEditing = (isSelected && potsEditLevel != POT_EDIT_NONE);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 7, 127, rowH, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(1, y);

    if (i == POTS_BACK) {
      display.print("< BACK");
    } else {
      // Show pot number
      display.print(i + 1);
      display.print(":");

      // Get category, param, target - either from temp (editing) or snapshot
      uint8_t cat, param, target;
      if (isEditing) {
        cat = potsTempCategory;
        param = potsTempParam;
        target = potsTempTarget;
      } else {
        cat = displaySnapshotCopy.potAssignments[i].category;
        param = displaySnapshotCopy.potAssignments[i].paramIndex;
        target = displaySnapshotCopy.potAssignments[i].target;
      }

      // Category name (highlight if editing category)
      const char* catName = getPotCategoryName((PotCategory)cat);
      int catX = display.getCursorX();
      if (isEditing && potsEditLevel == POT_EDIT_CATEGORY) {
        display.fillRect(catX - 1, y - 7, strlen(catName) * charW + 2, rowH, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(catName);
      display.setTextColor(isSelected && !isEditing ? SH110X_BLACK : SH110X_WHITE);

      if (cat != PCAT_OFF) {
        display.print(" ");

        // Param name (highlight if editing param)
        const char* paramName = getPotParamName((PotCategory)cat, param);
        int paramX = display.getCursorX();
        if (isEditing && potsEditLevel == POT_EDIT_PARAM) {
          display.fillRect(paramX - 1, y - 7, strlen(paramName) * charW + 2, rowH, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        }
        display.print(paramName);
        display.setTextColor(isSelected && !isEditing ? SH110X_BLACK : SH110X_WHITE);

        // Target (only for categories that need it)
        if (categoryRequiresTarget((PotCategory)cat)) {
          display.print(" ");
          const char* targName = (target == TARGET_NONE) ? "---" : getTargetName((SettingsTarget)target);
          int targX = display.getCursorX();
          if (isEditing && potsEditLevel == POT_EDIT_TARGET) {
            display.fillRect(targX - 1, y - 7, strlen(targName) * charW + 2, rowH, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
          }
          display.print(targName);
        } else {
          // Show "---" for FX/SAMPLE/GLOBAL categories (no targeting)
          display.print(" ---");
        }
      }
    }

    display.setTextColor(SH110X_WHITE);
    y += rowH;
  }

  // Footer
  display.drawLine(0, 54, 127, 54, SH110X_WHITE);
  display.setCursor(1, 62);
  if (potsEditLevel == POT_EDIT_NONE) {
    display.print("Turn=sel Push=edit");
  } else if (potsEditLevel == POT_EDIT_CATEGORY) {
    display.print("Turn=cat  Push=next");
  } else if (potsEditLevel == POT_EDIT_PARAM) {
    display.print("Turn=param Push=next");
  } else if (potsEditLevel == POT_EDIT_TARGET) {
    display.print("Turn=targ Push=done");
  }

  display.display();
}

// ============================================================================
// FX SUBMENU
// ============================================================================

// Get number of visible items based on FX type
int getVisibleFXItemCount() {
  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      return 8;  // ENABLED, TYPE, ROUTING, DELAY, REPEATS, DECAY, VOLUME, BACK
    case FX_ARP:
      return 8;  // ENABLED, TYPE, ROUTING, PATTERN, SPEED, VOLUME, OCTAVE, BACK
    case FX_BIT_CRUSH:
      return 8;  // ENABLED, TYPE, ROUTING, BITS, RATE, VOLUME, DURATION, BACK
    case FX_PSEUDO_REVERB:
      return 9;  // ENABLED, TYPE, ROUTING, TAPS, SPACE, DECAY, DETUNE, VOLUME, BACK
    case FX_CHORUS:
      return 8;  // ENABLED, TYPE, ROUTING, DET1, DET2, VOLUME, DURATION, BACK
    default:
      return 4;  // ENABLED, TYPE, ROUTING, BACK
  }
}

// Get FX menu item label based on selection and current type
const char* getFXLabel(int sel) {
  if (sel == FXMENU_ENABLED) return "ENABLED";
  if (sel == FXMENU_TYPE) return "TYPE";
  if (sel == FXMENU_ROUTING) return "ROUTE";

  // Context-dependent items based on FX type
  uint8_t type = displaySnapshotCopy.fxType;
  int lastItem = getVisibleFXItemCount() - 1;

  if (sel == lastItem) return "< BACK";

  // Parameter items
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) return "DELAY";
      if (sel == FXMENU_PARAM2) return "REPS";
      if (sel == FXMENU_PARAM3) return "DECAY";
      if (sel == FXMENU_PARAM4) return "VOLUME";
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) return "PATTERN";
      if (sel == FXMENU_PARAM2) return "SPEED";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "OCTAVE";
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) return "BITS";
      if (sel == FXMENU_PARAM2) return "RATE";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "SUSTAIN";
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) return "TAPS";
      if (sel == FXMENU_PARAM2) return "SPACE";
      if (sel == FXMENU_PARAM3) return "DECAY";
      if (sel == FXMENU_PARAM4) return "DETUNE";
      if (sel == FXMENU_PARAM5) return "VOLUME";
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) return "DET1";
      if (sel == FXMENU_PARAM2) return "DET2";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "SUSTAIN";
      break;
  }

  return "?";
}

// Get current FX value for a selection
int getFXValue(int sel) {
  if (sel == FXMENU_ENABLED) return displaySnapshotCopy.fxModeEnabled ? 1 : 0;
  if (sel == FXMENU_TYPE) return displaySnapshotCopy.fxType;
  if (sel == FXMENU_ROUTING) return displaySnapshotCopy.fxRouting;

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.echoDelayMs / 20;  // 100-2000 as 5-100
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.echoRepeats;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.echoDecay;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.echoVolume;
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.arpPattern;
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.arpSpeedMs / 5;  // 50-500 as 10-100
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.arpVolume;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.arpOctave + 2;  // -2 to +2 as 0-4
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.bitCrushBits;
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.bitCrushRate;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.bitCrushVolume;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.bitCrushDuration / 10;  // 50-500 as 5-50
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.reverbTaps;
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.reverbSpacing;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.reverbDecay;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.reverbDetune + 5;  // -5 to +5 as 0-10
      if (sel == FXMENU_PARAM5) return displaySnapshotCopy.reverbVolume;
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.chorusDetune1 + 50;  // -50 to +50 as 0-100
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.chorusDetune2 + 50;  // -50 to +50 as 0-100
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.chorusVolume;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.chorusDuration / 20;  // 0-2000 as 0-100 (0=follow)
      break;
  }
  return 0;
}

// Get max value for FX selection
int getFXMax(int sel) {
  if (sel == FXMENU_ENABLED) return 1;
  if (sel == FXMENU_TYPE) return FX_TYPE_COUNT - 1;
  if (sel == FXMENU_ROUTING) return FX_ROUTE_COUNT - 1;

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) return 100;  // 5-100 (x20 = 100-2000ms)
      if (sel == FXMENU_PARAM2) return 10;   // 1-10 repeats
      if (sel == FXMENU_PARAM3) return 15;   // 0-15 decay
      if (sel == FXMENU_PARAM4) return 15;   // 1-15 volume
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) return ARP_PATTERN_COUNT - 1;
      if (sel == FXMENU_PARAM2) return 100;  // 10-100 (x5 = 50-500ms)
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return 4;    // -2 to +2 octave
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) return 4;    // 1-4 bits
      if (sel == FXMENU_PARAM2) return 10;   // 1-10 rate
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return 50;   // 5-50 (x10 = 50-500ms sustain)
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) return 6;    // 2-6 taps
      if (sel == FXMENU_PARAM2) return 100;  // 20-100ms spacing
      if (sel == FXMENU_PARAM3) return 8;    // 1-8 decay
      if (sel == FXMENU_PARAM4) return 10;   // -5 to +5 detune
      if (sel == FXMENU_PARAM5) return 15;   // 1-15 volume
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) return 100;  // -50 to +50
      if (sel == FXMENU_PARAM2) return 100;  // -50 to +50
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return 100;  // 0-100 (x20 = 0-2000ms, 0=follow)
      break;
  }
  return 0;
}

// Get FX value as string
void getFXValueStr(int sel, int val, char* buf) {
  if (sel == FXMENU_ENABLED) {
    strcpy(buf, val ? "ON" : "OFF");
    return;
  }
  if (sel == FXMENU_TYPE) {
    strcpy(buf, getFxTypeName(val));
    return;
  }
  if (sel == FXMENU_ROUTING) {
    strcpy(buf, getFxRoutingName(val));
    return;
  }

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) {
        sprintf(buf, "%dms", val * 20);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sprintf(buf, "%d", val);
        return;
      }
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) {
        strcpy(buf, getArpPatternName(val));
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%dms", val * 5);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        int octVal = val - 2;
        sprintf(buf, "%+d", octVal);
        return;
      }
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sprintf(buf, "%dms", val * 10);
        return;
      }
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%dms", val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        int detVal = val - 5;
        sprintf(buf, "%+d", detVal);
        return;
      }
      if (sel == FXMENU_PARAM5) {
        sprintf(buf, "%d", val);
        return;
      }
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) {
        int detVal = val - 50;
        sprintf(buf, "%+d", detVal);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        int detVal = val - 50;
        sprintf(buf, "%+d", detVal);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        if (val == 0) {
          strcpy(buf, "HOLD");
        } else {
          sprintf(buf, "%dms", val * 20);
        }
        return;
      }
      break;
  }
  strcpy(buf, "?");
}

// Apply FX value
void applyFXValue(int sel, int val) {
  if (sel == FXMENU_ENABLED) {
    sendCommand(CMD_SET_FX_ENABLED, val);
    return;
  }
  if (sel == FXMENU_TYPE) {
    sendCommand(CMD_SET_FX_TYPE, val);
    return;
  }
  if (sel == FXMENU_ROUTING) {
    sendCommand(CMD_SET_FX_ROUTING, val);
    return;
  }

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_ECHO_DELAY, val);  // 5-100, scaled x20 in handler
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_ECHO_REPEATS, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_ECHO_DECAY, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_ECHO_VOLUME, val);
        return;
      }
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_ARP_PATTERN, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_ARP_SPEED, val);  // 10-100, scaled x5 in handler
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_ARP_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_ARP_OCTAVE, 0, 0, 0, (int8_t)(val - 2));
        return;
      }
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_BIT_CRUSH_BITS, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_BIT_CRUSH_RATE, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_BIT_CRUSH_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_BIT_CRUSH_DURATION, val);  // val is 5-50, scaled x10 in handler
        return;
      }
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_REVERB_TAPS, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_REVERB_SPACING, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_REVERB_DECAY, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_REVERB_DETUNE, 0, 0, 0, (int8_t)(val - 5));
        return;
      }
      if (sel == FXMENU_PARAM5) {
        sendCommand(CMD_SET_REVERB_VOLUME, val);
        return;
      }
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_CHORUS_DETUNE1, 0, 0, 0, (int8_t)(val - 50));
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_CHORUS_DETUNE2, 0, 0, 0, (int8_t)(val - 50));
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_CHORUS_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_CHORUS_DURATION, val);  // val is 0-100, scaled x20 in handler
        return;
      }
      break;
  }
}

// Helper to draw an FX item (label + value), returns width drawn
static int drawFXItem(int x, int y, int sel, bool highlight, bool editing) {
  char valBuf[12];
  int val = editing ? fxTempValue : getFXValue(sel);
  getFXValueStr(sel, val, valBuf);
  const char* label = getFXLabel(sel);

  int labelWidth = strlen(label) * 4;
  int valWidth = strlen(valBuf) * 4;
  int totalWidth = labelWidth + 4 + valWidth + 4;  // "LABEL: VAL "

  if (highlight && !editing) {
    display.fillRect(x, y - 6, totalWidth, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }

  display.setCursor(x + 1, y);
  display.print(label);
  display.print(":");

  if (editing) {
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, valWidth + 2, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(valBuf);
  display.setTextColor(SH110X_WHITE);

  return totalWidth;
}

void updateFXSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setCursor(0, 6);
  display.print("FX CHIP [2]");
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  uint8_t type = displaySnapshotCopy.fxType;
  int y = 17;
  const int rowHeight = 9;

  // Row 1: ENABLED + TYPE (side by side)
  {
    bool sel0 = (fxSelection == FXMENU_ENABLED);
    bool edit0 = sel0 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_ENABLED, sel0, edit0);

    bool sel1 = (fxSelection == FXMENU_TYPE);
    bool edit1 = sel1 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_TYPE, sel1, edit1);
    y += rowHeight;
  }

  // Row 2: ROUTING (full width)
  {
    bool sel = (fxSelection == FXMENU_ROUTING);
    bool edit = sel && fxEditing;
    drawFXItem(0, y, FXMENU_ROUTING, sel, edit);
    y += rowHeight;
  }

  // Row 3: Parameters (type-dependent, paired when applicable)
  if (type == FX_ECHO) {
    // DELAY + REPS
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // DECAY + VOL
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_ARP) {
    // PATTERN + SPEED
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + OCTAVE
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_BIT_CRUSH) {
    // BITS + RATE
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + SUSTAIN
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_PSEUDO_REVERB) {
    // TAPS + SPACE
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // DECAY + DETUNE
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;

    // VOLUME
    bool sel7 = (fxSelection == FXMENU_PARAM5);
    bool edit7 = sel7 && fxEditing;
    drawFXItem(0, y, FXMENU_PARAM5, sel7, edit7);
    y += rowHeight;
  }
  else if (type == FX_CHORUS) {
    // DET1 + DET2
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + SUSTAIN
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }

  // BACK row
  int backItem = getVisibleFXItemCount() - 1;
  bool selBack = (fxSelection == backItem);
  if (selBack && !fxEditing) {
    display.fillRect(0, y - 6, 40, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(2, y);
  display.print("< BACK");
  display.setTextColor(SH110X_WHITE);

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (fxEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);
  display.display();
}

// ============================================================================
// PRESETS SUBMENU
// ============================================================================

// Character set for name entry: A-Z, 0-9, space
static const char nameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
static const int nameCharCount = 37;

// Get character index in nameChars
static int getNameCharIndex(char c) {
  for (int i = 0; i < nameCharCount; i++) {
    if (nameChars[i] == c) return i;
  }
  return 36;  // Default to space
}

void updatePresetsMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use tiny font for presets menu
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Show "Saving..." overlay while save is in progress
  if (presetSaving) {
    display.setCursor(40, 32);
    display.print("Saving...");
    // Clear flag after timeout (save should be complete)
    if (millis() - presetSaveStartTime >= PRESET_SAVE_DISPLAY_MS) {
      presetSaving = false;
    }
    display.setFont(NULL);
    display.display();
    return;
  }

  // Show "Deleting..." overlay while delete is in progress
  if (presetDeleting) {
    display.setCursor(36, 32);
    display.print("Deleting...");
    // Clear flag after timeout (delete should be complete)
    if (millis() - presetDeleteStartTime >= PRESET_SAVE_DISPLAY_MS) {
      presetDeleting = false;
    }
    display.setFont(NULL);
    display.display();
    return;
  }

  switch (presetMenuLevel) {
    case PRESET_LEVEL_MENU: {
      // Main preset menu: LOAD / SAVE / DELETE / BACK
      // TomThumb uses baseline positioning, add 5 for height
      display.setCursor(0, 6);
      display.print("PRESETS");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show current preset if any
      display.setCursor(0, 16);
      display.print("Active: ");
      if (currentPresetIndex == PRESET_INDEX_NONE) {
        display.print("---");
      } else if (PRESET_IS_FACTORY(currentPresetIndex)) {
        display.print("F0");
        display.print(currentPresetIndex + 1);
      } else {
        int userSlot = PRESET_TO_USER_SLOT(currentPresetIndex);
        display.print("U");
        if (userSlot < 9) display.print("00");
        else if (userSlot < 99) display.print("0");
        display.print(userSlot + 1);
      }

      // Menu items (7px spacing for TomThumb)
      const char* items[] = {"LOAD", "SAVE", "DELETE", "< BACK"};
      int y = 26;
      for (int i = 0; i < PRESETMENU_ITEM_COUNT; i++) {
        bool selected = (presetMenuSelection == i);
        if (selected) {
          display.fillRect(0, y - 6, 128, 8, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else {
          display.setTextColor(SH110X_WHITE);
        }
        display.setCursor(4, y);
        display.print(items[i]);
        display.setTextColor(SH110X_WHITE);
        y += 8;
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Turn=sel  Push=enter");
      break;
    }

    case PRESET_LEVEL_LOAD: {
      // Show list of presets to load (factory + user)
      display.setCursor(0, 6);
      display.print("LOAD PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int totalPresets = getLoadListCount();

      // Show presets (7 at a time with tiny font)
      int y = 16;
      for (int row = 0; row < 7; row++) {
        int listPos = presetScrollIndex + row;
        if (listPos >= totalPresets) break;

        uint8_t presetIdx = getLoadListPresetIndex(listPos);
        if (presetIdx == PRESET_INDEX_NONE) break;

        bool selected = (row == 0);  // First visible is selected
        if (selected) {
          display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else {
          display.setTextColor(SH110X_WHITE);
        }

        display.setCursor(2, y);
        if (PRESET_IS_FACTORY(presetIdx)) {
          // Factory preset: F01-F08
          display.print("F0");
          display.print(presetIdx + 1);
          display.print(": ");
          display.print(factoryPresetNames[presetIdx]);
        } else {
          // User preset: U001-U120
          int userSlot = PRESET_TO_USER_SLOT(presetIdx);
          display.print("U");
          if (userSlot < 9) display.print("00");
          else if (userSlot < 99) display.print("0");
          display.print(userSlot + 1);
          display.print(": ");
          char nameBuf[9];
          presetGetName(presetIdx, nameBuf);
          display.print(nameBuf);
        }
        display.setTextColor(SH110X_WHITE);
        y += 7;
      }

      // Scroll indicator
      if (totalPresets > 7) {
        int scrollH = 48;
        int scrollY = 10 + (scrollH * presetScrollIndex / totalPresets);
        display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
        display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Turn=scrl Push=load Hold=back");
      break;
    }

    case PRESET_LEVEL_SAVE: {
      // Show list of user slots to save to
      display.setCursor(0, 6);
      display.print("SAVE TO SLOT");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show slots starting from scroll index (7 at a time with tiny font)
      int y = 16;
      for (int row = 0; row < 7; row++) {
        int slot = presetScrollIndex + row;
        if (slot >= PRESET_USER_SLOTS) break;

        bool selected = (slot == presetScrollIndex);
        if (selected) {
          display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
          presetSelectedSlot = slot;
        } else {
          display.setTextColor(SH110X_WHITE);
        }

        display.setCursor(2, y);
        display.print("U");
        if (slot < 9) display.print("00");
        else if (slot < 99) display.print("0");
        display.print(slot + 1);
        display.print(": ");

        if (presetUserIsUsed(slot)) {
          char nameBuf[9];
          presetGetName(USER_SLOT_TO_PRESET(slot), nameBuf);
          display.print(nameBuf);
        } else {
          display.print("--------");
        }
        display.setTextColor(SH110X_WHITE);
        y += 7;
      }

      // Scroll indicator
      if (PRESET_USER_SLOTS > 7) {
        int scrollH = 48;
        int scrollY = 10 + (scrollH * presetScrollIndex / PRESET_USER_SLOTS);
        display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
        display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Push=sel Hold=back");
      break;
    }

    case PRESET_LEVEL_NAME: {
      // Name entry screen
      display.setCursor(0, 6);
      display.print("ENTER NAME");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show slot being saved to
      display.setCursor(0, 18);
      display.print("Slot: U");
      if (presetSelectedSlot < 9) display.print("00");
      else if (presetSelectedSlot < 99) display.print("0");
      display.print(presetSelectedSlot + 1);

      // Show name with cursor (larger chars for editing)
      display.setFont(NULL);  // Use default font for name characters
      display.setCursor(0, 28);
      display.print("Name:");

      // Draw each character
      int nameX = 36;
      for (int i = 0; i < 8; i++) {
        bool isCursor = (i == presetNameCursor);
        if (isCursor && presetNameEditing) {
          display.fillRect(nameX - 1, 26, 8, 11, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else if (isCursor) {
          display.drawRect(nameX - 1, 26, 8, 11, SH110X_WHITE);
        }
        display.setCursor(nameX, 28);
        display.print(presetNameBuffer[i]);
        display.setTextColor(SH110X_WHITE);
        nameX += 7;
      }

      // Draw SAVE option after name (position 8)
      nameX += 4;  // Small gap
      bool saveCursor = (presetNameCursor == 8);
      if (saveCursor) {
        display.fillRect(nameX - 1, 26, 26, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.setCursor(nameX, 28);
      display.print("SAVE");
      display.setTextColor(SH110X_WHITE);

      // Draw EXIT option (position 9)
      nameX += 30;
      bool exitCursor = (presetNameCursor == 9);
      if (exitCursor) {
        display.fillRect(nameX - 1, 26, 26, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.setCursor(nameX, 28);
      display.print("EXIT");
      display.setTextColor(SH110X_WHITE);

      // Switch back to tiny font for hints
      display.setFont(&TomThumb);

      // Show character selection hint and footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      if (presetNameEditing) {
        display.setCursor(0, 48);
        display.print("Turn=char  Push=next");
        display.setCursor(2, 62);
        display.print("Hold=stop editing");
      } else {
        display.setCursor(0, 48);
        if (presetNameCursor == 8) {
          display.print("Push = save preset");
        } else if (presetNameCursor == 9) {
          display.print("Push = exit (no save)");
        } else {
          display.print("Push=edit  Turn=move");
        }
        display.setCursor(2, 62);
        display.print("Slot U");
        if (presetSelectedSlot < 9) display.print("00");
        else if (presetSelectedSlot < 99) display.print("0");
        display.print(presetSelectedSlot + 1);
      }
      break;
    }

    case PRESET_LEVEL_DELETE: {
      display.setCursor(0, 6);
      display.print("DELETE PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int deleteCount = getDeleteListCount();

      if (deleteCount == 0) {
        // No user presets to delete
        display.setCursor(10, 30);
        display.print("No user presets");
        display.setCursor(10, 40);
        display.print("(save first)");

        // Footer
        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=back");
      } else {
        // Show user presets (7 at a time with tiny font)
        int y = 16;
        for (int row = 0; row < 7; row++) {
          int listPos = presetScrollIndex + row;
          if (listPos >= deleteCount) break;

          uint8_t userSlot = getDeleteListUserSlot(listPos);
          if (userSlot == 0xFF) break;

          bool selected = (row == 0);  // First visible is selected
          if (selected) {
            display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
            presetSelectedSlot = userSlot;  // Track for deletion
          } else {
            display.setTextColor(SH110X_WHITE);
          }

          display.setCursor(2, y);
          display.print("U");
          if (userSlot < 9) display.print("00");
          else if (userSlot < 99) display.print("0");
          display.print(userSlot + 1);
          display.print(": ");
          char nameBuf[9];
          presetGetName(USER_SLOT_TO_PRESET(userSlot), nameBuf);
          display.print(nameBuf);
          display.setTextColor(SH110X_WHITE);
          y += 7;
        }

        // Scroll indicator
        if (deleteCount > 7) {
          int scrollH = 48;
          int scrollY = 10 + (scrollH * presetScrollIndex / deleteCount);
          display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
          display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
        }

        // Footer
        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=del  Hold=back");
      }
      break;
    }
  }

  display.setFont(NULL);  // Reset to default font
  display.display();
}

// Helper to get MIDI channel display string
static const char* getMidiChannelName(uint8_t ch) {
  static char buf[5];
  if (ch == MIDI_CHANNEL_OFF) {
    return "OFF";
  } else if (ch == MIDI_CHANNEL_OMNI) {
    return "OMNI";
  } else {
    // Display as 1-16 (ch is 0-15 internally)
    snprintf(buf, sizeof(buf), "%d", ch + 1);
    return buf;
  }
}

void updateMidiMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setCursor(0, 6);
  display.print("MIDI SETTINGS");
  display.drawLine(0, 8, 127, 8, SH110X_WHITE);

  const int itemH = 10;
  int y = 20;

  // Get display values
  uint8_t synthVal = midiEditing && midiMenuSelection == MIDIMENU_SYNTH ? midiTempValue : midiSynthChannel;
  uint8_t drumVal = midiEditing && midiMenuSelection == MIDIMENU_DRUMS ? midiTempValue : midiDrumChannel;

  // Synth channel item
  bool synthSel = (midiMenuSelection == MIDIMENU_SYNTH);
  if (synthSel && !midiEditing) {
    display.fillRect(0, y - 6, 80, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("SYNTH CH: ");
  if (midiEditing && synthSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx - 1, y - 6, 24, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(getMidiChannelName(synthVal));
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // Drum channel item
  bool drumSel = (midiMenuSelection == MIDIMENU_DRUMS);
  if (drumSel && !midiEditing) {
    display.fillRect(0, y - 6, 80, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("DRUM CH:  ");
  if (midiEditing && drumSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx - 1, y - 6, 24, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(getMidiChannelName(drumVal));
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // VIZ mode item
  bool vizSel = (midiMenuSelection == MIDIMENU_VIZ);
  uint8_t vizVal = midiEditing && vizSel ? midiTempValue : vizMode;
  if (vizSel && !midiEditing) {
    display.fillRect(0, y - 6, 80, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("VIZ:      ");
  if (midiEditing && vizSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx - 1, y - 6, 30, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(vizVal == VIZ_MODE_BARS ? "BARS" :
                vizVal == VIZ_MODE_SCOPE ? "SCOPE" : "MATRIX");
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // BACK item
  if (midiMenuSelection == MIDIMENU_BACK) {
    display.fillRect(0, y - 6, 32, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("< BACK");
  display.setTextColor(SH110X_WHITE);

  // Footer with help text
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (midiEditing) {
    display.print("Turn=adj Push=save");
  } else {
    display.print("Push=edit  Turn=select");
  }

  display.setFont(NULL);
  display.display();
}

void handleEncoder() {
  int delta = getEncoderDelta();

  if (delta != 0) {
    if (displayMode == DISPLAY_MENU) {
      if (editingValue) {
        if (menuSelection == MENU_MODE) {
          tempModeValue += delta;
          if (tempModeValue < 0) tempModeValue = MODE_COUNT - 1;
          if (tempModeValue >= MODE_COUNT) tempModeValue = 0;
        }
        else if (menuSelection == MENU_LINK) {
          tempLinkValue += delta;
          if (tempLinkValue < 0) tempLinkValue = LINK_MODE_COUNT - 1;
          if (tempLinkValue >= LINK_MODE_COUNT) tempLinkValue = 0;
        }
      } else {
        menuSelection += delta;
        if (menuSelection < 0) menuSelection = MENU_ITEM_COUNT - 1;
        if (menuSelection >= MENU_ITEM_COUNT) menuSelection = 0;
      }
    }
    else if (displayMode == DISPLAY_SETTINGS) {
      if (settingsSubmenu == SUBMENU_NONE) {
        // Main settings menu
        if (settingsEditing) {
          settingsTempValue += delta;
          int maxVal = getSettingsMax(settingsSelection);
          if (settingsTempValue < 0) settingsTempValue = maxVal;
          if (settingsTempValue > maxVal) settingsTempValue = 0;
        } else {
          settingsSelection = nextVisibleSettingsItem(settingsSelection, delta);
        }
      }
      else if (settingsSubmenu == SUBMENU_VIBRATO) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getVibratoMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply (scope is applied immediately too)
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyVibratoValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = VIBMENU_ITEM_COUNT - 1;
          if (submenuSelection >= VIBMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
      else if (settingsSubmenu == SUBMENU_ENVELOPE) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getEnvelopeMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyEnvelopeValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = ENVMENU_ITEM_COUNT - 1;
          if (submenuSelection >= ENVMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
      else if (settingsSubmenu == SUBMENU_SID) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getSIDMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applySIDValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = SIDMENU_ITEM_COUNT - 1;
          if (submenuSelection >= SIDMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getPitchMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyPitchValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = PITCHMENU_ITEM_COUNT - 1;
          if (submenuSelection >= PITCHMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
      else if (settingsSubmenu == SUBMENU_GLIDE) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getGlideMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyGlideValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = GLIDEMENU_ITEM_COUNT - 1;
          if (submenuSelection >= GLIDEMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
      else if (settingsSubmenu == SUBMENU_TREMOLO) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getTremoloMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyTremoloValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = TREMMENU_ITEM_COUNT - 1;
          if (submenuSelection >= TREMMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH_ENV) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getPitchEnvMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Real-time apply
          if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyPitchEnvValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -1) submenuSelection = PENVMENU_ITEM_COUNT - 1;
          if (submenuSelection >= PENVMENU_ITEM_COUNT) submenuSelection = -1;
        }
      }
    }
    else if (displayMode == DISPLAY_POTS) {
      if (potsEditLevel == POT_EDIT_NONE) {
        // Navigate pot list
        potsSelection += delta;
        if (potsSelection < 0) potsSelection = POTS_ITEM_COUNT - 1;
        if (potsSelection >= POTS_ITEM_COUNT) potsSelection = 0;
      } else if (potsEditLevel == POT_EDIT_CATEGORY) {
        // Cycle through categories
        int cat = (int)potsTempCategory + delta;
        if (cat < 0) cat = PCAT_COUNT - 1;
        if (cat >= PCAT_COUNT) cat = 0;
        potsTempCategory = (uint8_t)cat;
        // Reset param when category changes
        potsTempParam = 0;
      } else if (potsEditLevel == POT_EDIT_PARAM) {
        // Cycle through params within category
        uint8_t paramCount = getPotParamCount((PotCategory)potsTempCategory);
        if (paramCount > 0) {
          int p = (int)potsTempParam + delta;
          if (p < 0) p = paramCount - 1;
          if (p >= paramCount) p = 0;
          potsTempParam = (uint8_t)p;
        }
      } else if (potsEditLevel == POT_EDIT_TARGET) {
        // Cycle through targets (ALL, C0-C2, V1-V9)
        int t = (int)potsTempTarget + delta;
        if (t < 0) t = TARGET_COUNT - 1;
        if (t >= TARGET_COUNT) t = 0;
        potsTempTarget = (uint8_t)t;
      }
    }
    else if (displayMode == DISPLAY_FX) {
      if (fxEditing) {
        fxTempValue += delta;
        int maxVal = getFXMax(fxSelection);
        if (fxTempValue < 0) fxTempValue = maxVal;
        if (fxTempValue > maxVal) fxTempValue = 0;
        // Real-time apply
        applyFXValue(fxSelection, fxTempValue);
      } else {
        // Navigate through visible items
        fxSelection += delta;
        int maxItem = getVisibleFXItemCount() - 1;
        if (fxSelection < 0) fxSelection = maxItem;
        if (fxSelection > maxItem) fxSelection = 0;
      }
    }
    else if (displayMode == DISPLAY_PRESETS) {
      if (presetMenuLevel == PRESET_LEVEL_MENU) {
        // Navigate main preset menu
        presetMenuSelection += delta;
        if (presetMenuSelection < 0) presetMenuSelection = PRESETMENU_ITEM_COUNT - 1;
        if (presetMenuSelection >= PRESETMENU_ITEM_COUNT) presetMenuSelection = 0;
      }
      else if (presetMenuLevel == PRESET_LEVEL_LOAD) {
        // Scroll through all available presets (factory + user)
        int totalPresets = getLoadListCount();
        presetScrollIndex += delta;
        if (presetScrollIndex < 0) presetScrollIndex = totalPresets - 1;
        if (presetScrollIndex >= totalPresets) presetScrollIndex = 0;
      }
      else if (presetMenuLevel == PRESET_LEVEL_SAVE) {
        // Scroll through user slots
        presetScrollIndex += delta;
        if (presetScrollIndex < 0) presetScrollIndex = PRESET_USER_SLOTS - 1;
        if (presetScrollIndex >= PRESET_USER_SLOTS) presetScrollIndex = 0;
      }
      else if (presetMenuLevel == PRESET_LEVEL_NAME) {
        if (presetNameEditing) {
          // Cycle through characters
          int charIdx = getNameCharIndex(presetNameBuffer[presetNameCursor]);
          charIdx += delta;
          if (charIdx < 0) charIdx = nameCharCount - 1;
          if (charIdx >= nameCharCount) charIdx = 0;
          presetNameBuffer[presetNameCursor] = nameChars[charIdx];
        } else {
          // Move cursor position (0-7 = name chars, 8 = SAVE, 9 = EXIT)
          presetNameCursor += delta;
          if (presetNameCursor > 9) presetNameCursor = 0;
          // Note: presetNameCursor is uint8_t so underflow wraps to 255
          if (presetNameCursor > 9) presetNameCursor = 9;
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_DELETE) {
        // Scroll through user presets
        int deleteCount = getDeleteListCount();
        if (deleteCount > 0) {
          presetScrollIndex += delta;
          if (presetScrollIndex < 0) presetScrollIndex = deleteCount - 1;
          if (presetScrollIndex >= deleteCount) presetScrollIndex = 0;
        }
      }
    }
    else if (displayMode == DISPLAY_MIDI) {
      if (midiEditing) {
        if (midiMenuSelection == MIDIMENU_SYNTH) {
          // Synth: OFF -> OMNI -> 1-16 -> OFF
          if (midiTempValue == MIDI_CHANNEL_OFF) {
            midiTempValue = (delta > 0) ? MIDI_CHANNEL_OMNI : 15;
          } else if (midiTempValue == MIDI_CHANNEL_OMNI) {
            midiTempValue = (delta > 0) ? 0 : MIDI_CHANNEL_OFF;
          } else {
            midiTempValue += delta;
            if (midiTempValue < 0) midiTempValue = MIDI_CHANNEL_OMNI;
            if (midiTempValue > 15) midiTempValue = MIDI_CHANNEL_OFF;
          }
        }
        else if (midiMenuSelection == MIDIMENU_DRUMS) {
          // Drums: OFF -> 1-16 -> OFF (no OMNI)
          if (midiTempValue == MIDI_CHANNEL_OFF) {
            midiTempValue = (delta > 0) ? 0 : 15;
          } else {
            midiTempValue += delta;
            if (midiTempValue < 0) midiTempValue = MIDI_CHANNEL_OFF;
            if (midiTempValue > 15) midiTempValue = MIDI_CHANNEL_OFF;
          }
        }
        else if (midiMenuSelection == MIDIMENU_VIZ) {
          // Cycle through visualization modes: BARS -> SCOPE -> MATRIX -> BARS
          midiTempValue += delta;
          if (midiTempValue < 0) midiTempValue = VIZ_MODE_COUNT - 1;
          if (midiTempValue >= VIZ_MODE_COUNT) midiTempValue = 0;
        }
      } else {
        // Navigate menu
        midiMenuSelection += delta;
        if (midiMenuSelection < 0) midiMenuSelection = MIDIMENU_ITEM_COUNT - 1;
        if (midiMenuSelection >= MIDIMENU_ITEM_COUNT) midiMenuSelection = 0;
      }
    }
  }

  // Long-press handling in preset screens
  if (encoderButtonLongPressed()) {
    if (displayMode == DISPLAY_PRESETS) {
      if (presetMenuLevel == PRESET_LEVEL_MENU) {
        // Long press on main preset menu - exit to viz
        displayMode = DISPLAY_VIZ;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_LOAD) {
        // Long press on load screen - go back to preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_LOAD;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_SAVE) {
        // Cancel - return to main preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_SAVE;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_DELETE) {
        // Long press on delete screen - go back to preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_DELETE;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_NAME) {
        // Long press in name entry = cancel editing current char
        if (presetNameEditing) {
          presetNameEditing = false;
        }
        // Long press on SAVE/EXIT does nothing (use short press)
        return;
      }
    }
  }

  if (encoderButtonPressed()) {
    if (displayMode == DISPLAY_VIZ) {
      displayMode = DISPLAY_MENU;
      menuSelection = 0;
      editingValue = false;
    }
    else if (displayMode == DISPLAY_MENU) {
      if (menuSelection == MENU_EXIT) {
        editingValue = false;
        displayMode = DISPLAY_VIZ;
      }
      else if (menuSelection >= MENU_CHIP0 && menuSelection <= MENU_CHIP2) {
        currentChip = menuSelection - MENU_CHIP0;
        currentScope = 0;
        displayMode = DISPLAY_SETTINGS;
        settingsSubmenu = SUBMENU_NONE;
        settingsSelection = 0;
        settingsScrollOffset = 0;
        settingsEditing = false;
      }
      else if (menuSelection == MENU_FX) {
        displayMode = DISPLAY_FX;
        fxSelection = 0;
        fxEditing = false;
      }
      else if (menuSelection == MENU_POTS) {
        displayMode = DISPLAY_POTS;
        potsSelection = 0;
        potsEditLevel = POT_EDIT_NONE;
      }
      else if (menuSelection == MENU_PRESETS) {
        displayMode = DISPLAY_PRESETS;
        presetMenuSelection = 0;
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetScrollIndex = 0;
      }
      else if (menuSelection == MENU_MIDI) {
        displayMode = DISPLAY_MIDI;
        midiMenuSelection = 0;
        midiEditing = false;
      }
      else if (menuSelection == MENU_MODE) {
        if (!editingValue) {
          editingValue = true;
          tempModeValue = getCurrentModeIndex();
        } else {
          applyModeFromIndex(tempModeValue);
          editingValue = false;
        }
      }
      else if (menuSelection == MENU_LINK) {
        if (!editingValue) {
          editingValue = true;
          tempLinkValue = displaySnapshotCopy.linkMode;
        } else {
          sendCommand(CMD_SET_LINK_MODE, (uint8_t)tempLinkValue);
          editingValue = false;
        }
      }
    }
    else if (displayMode == DISPLAY_SETTINGS) {
      if (settingsSubmenu == SUBMENU_NONE) {
        // Main settings menu
        if (settingsSelection == SETTINGS_BACK) {
          settingsEditing = false;
          displayMode = DISPLAY_MENU;
          menuSelection = MENU_CHIP0 + currentChip;
        }
        else if (settingsSelection == SETTINGS_VIBRATO) {
          // Enter vibrato submenu
          settingsSubmenu = SUBMENU_VIBRATO;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_ENVELOPE) {
          // Enter envelope submenu
          settingsSubmenu = SUBMENU_ENVELOPE;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_SID) {
          // Enter SID submenu
          settingsSubmenu = SUBMENU_SID;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_PITCH) {
          // Enter Pitch submenu
          settingsSubmenu = SUBMENU_PITCH;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_GLIDE) {
          // Enter Glide submenu
          settingsSubmenu = SUBMENU_GLIDE;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_TREMOLO) {
          // Enter Tremolo submenu
          settingsSubmenu = SUBMENU_TREMOLO;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_PITCH_ENV) {
          // Enter Pitch Envelope submenu
          settingsSubmenu = SUBMENU_PITCH_ENV;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (!settingsEditing) {
          settingsEditing = true;
          settingsTempValue = getSettingsValue(settingsSelection);
        } else {
          applySettingsValue(settingsSelection, settingsTempValue);
          settingsEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_VIBRATO) {
        if (submenuSelection == VIBMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getVibratoValue(submenuSelection);
        } else {
          applyVibratoValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_ENVELOPE) {
        if (submenuSelection == ENVMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getEnvelopeValue(submenuSelection);
        } else {
          applyEnvelopeValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_SID) {
        if (submenuSelection == SIDMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getSIDValue(submenuSelection);
        } else {
          applySIDValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH) {
        if (submenuSelection == PITCHMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getPitchValue(submenuSelection);
        } else {
          applyPitchValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_GLIDE) {
        if (submenuSelection == GLIDEMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getGlideValue(submenuSelection);
        } else {
          applyGlideValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_TREMOLO) {
        if (submenuSelection == TREMMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getTremoloValue(submenuSelection);
        } else {
          applyTremoloValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH_ENV) {
        if (submenuSelection == PENVMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getPitchEnvValue(submenuSelection);
        } else {
          applyPitchEnvValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
    }
    else if (displayMode == DISPLAY_POTS) {
      if (potsSelection == POTS_BACK) {
        // Back button - exit to menu
        potsEditLevel = POT_EDIT_NONE;
        displayMode = DISPLAY_MENU;
        menuSelection = MENU_POTS;
      }
      else if (potsEditLevel == POT_EDIT_NONE) {
        // Start editing - load current values
        potsEditLevel = POT_EDIT_CATEGORY;
        potsTempCategory = displaySnapshotCopy.potAssignments[potsSelection].category;
        potsTempParam = displaySnapshotCopy.potAssignments[potsSelection].paramIndex;
        potsTempTarget = displaySnapshotCopy.potAssignments[potsSelection].target;
      }
      else if (potsEditLevel == POT_EDIT_CATEGORY) {
        if (potsTempCategory == PCAT_OFF) {
          // OFF has no params/target - save immediately
          sendCommand(CMD_SET_POT_ASSIGN, potsSelection, potsTempCategory, 0, TARGET_ALL);
          potsEditLevel = POT_EDIT_NONE;
        } else {
          // Move to param selection
          potsEditLevel = POT_EDIT_PARAM;
        }
      }
      else if (potsEditLevel == POT_EDIT_PARAM) {
        if (categoryRequiresTarget((PotCategory)potsTempCategory)) {
          // Move to target selection
          potsEditLevel = POT_EDIT_TARGET;
        } else {
          // No targeting needed - save immediately
          sendCommand(CMD_SET_POT_ASSIGN, potsSelection, potsTempCategory, potsTempParam, TARGET_NONE);
          potsEditLevel = POT_EDIT_NONE;
        }
      }
      else if (potsEditLevel == POT_EDIT_TARGET) {
        // Save the assignment
        sendCommand(CMD_SET_POT_ASSIGN, potsSelection, potsTempCategory, potsTempParam, potsTempTarget);
        potsEditLevel = POT_EDIT_NONE;
      }
    }
    else if (displayMode == DISPLAY_FX) {
      if (fxSelection == getVisibleFXItemCount() - 1) {
        // BACK item - return to menu
        fxEditing = false;
        displayMode = DISPLAY_MENU;
        menuSelection = MENU_FX;
      }
      else if (!fxEditing) {
        fxEditing = true;
        fxTempValue = getFXValue(fxSelection);
      } else {
        applyFXValue(fxSelection, fxTempValue);
        fxEditing = false;
      }
    }
    else if (displayMode == DISPLAY_PRESETS) {
      if (presetMenuLevel == PRESET_LEVEL_MENU) {
        // Main preset menu actions
        if (presetMenuSelection == PRESETMENU_BACK) {
          displayMode = DISPLAY_MENU;
          menuSelection = MENU_PRESETS;
        }
        else if (presetMenuSelection == PRESETMENU_LOAD) {
          presetMenuLevel = PRESET_LEVEL_LOAD;
          presetScrollIndex = 0;
        }
        else if (presetMenuSelection == PRESETMENU_SAVE) {
          presetMenuLevel = PRESET_LEVEL_SAVE;
          presetScrollIndex = 0;
        }
        else if (presetMenuSelection == PRESETMENU_DELETE) {
          presetMenuLevel = PRESET_LEVEL_DELETE;
          presetScrollIndex = 0;
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_LOAD) {
        // Load selected preset (factory or user) via command queue
        uint8_t presetIdx = getLoadListPresetIndex(presetScrollIndex);
        if (presetIdx != PRESET_INDEX_NONE) {
          sendCommand(CMD_PRESET_LOAD, presetIdx);
        }
        // Return to main preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_LOAD;
      }
      else if (presetMenuLevel == PRESET_LEVEL_SAVE) {
        // Selected a slot - enter name entry mode
        presetSelectedSlot = presetScrollIndex;
        presetMenuLevel = PRESET_LEVEL_NAME;
        // Initialize name buffer - use existing name or blank
        if (presetUserIsUsed(presetSelectedSlot)) {
          presetGetName(USER_SLOT_TO_PRESET(presetSelectedSlot), presetNameBuffer);
        } else {
          strcpy(presetNameBuffer, "        ");
        }
        presetNameCursor = 0;
        presetNameEditing = false;
      }
      else if (presetMenuLevel == PRESET_LEVEL_NAME) {
        if (presetNameCursor == 8) {
          // SAVE selected - save the preset
          // Show "Saving..." IMMEDIATELY before flash write pauses Core 1
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(40, 28);
          display.print("Saving...");
          display.display();  // Force update to OLED NOW

          presetMenuLevel = PRESET_LEVEL_MENU;
          presetMenuSelection = PRESETMENU_SAVE;
          strncpy(presetNameCmd, presetNameBuffer, 8);
          presetNameCmd[8] = '\0';
          // Set pending flag - Core 0 will handle the flash operation
          presetSaveSlot = presetSelectedSlot;
          presetSavePending = true;
        }
        else if (presetNameCursor == 9) {
          // EXIT selected - return to preset menu without saving
          presetMenuLevel = PRESET_LEVEL_MENU;
          presetMenuSelection = PRESETMENU_SAVE;
        }
        else if (!presetNameEditing) {
          // Start editing current character
          presetNameEditing = true;
        } else {
          // Move to next character
          presetNameEditing = false;
          presetNameCursor++;
          // After position 7, cursor goes to SAVE (position 8)
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_DELETE) {
        int deleteCount = getDeleteListCount();
        if (deleteCount == 0) {
          // No presets - just return to menu
          presetMenuLevel = PRESET_LEVEL_MENU;
          presetMenuSelection = PRESETMENU_DELETE;
        } else {
          // Delete the selected preset
          // Show "Deleting..." IMMEDIATELY before flash write pauses Core 1
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(36, 28);
          display.print("Deleting...");
          display.display();  // Force update to OLED NOW

          // Adjust scroll if we deleted the last item
          int newCount = deleteCount - 1;
          if (newCount == 0) {
            presetMenuLevel = PRESET_LEVEL_MENU;
            presetMenuSelection = PRESETMENU_DELETE;
          } else if (presetScrollIndex >= newCount) {
            presetScrollIndex = newCount - 1;
          }
          // Set pending flag - Core 0 will handle the flash operation
          presetDeleteSlot = presetSelectedSlot;
          presetDeletePending = true;
        }
      }
    }
    else if (displayMode == DISPLAY_MIDI) {
      if (midiMenuSelection == MIDIMENU_BACK) {
        // Save MIDI settings to flash when exiting menu
        midiSavePending = true;  // Core 0 will handle flash write
        displayMode = DISPLAY_MENU;
        menuSelection = MENU_MIDI;
      }
      else if (midiMenuSelection == MIDIMENU_SYNTH) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = midiSynthChannel;
        } else {
          midiSynthChannel = midiTempValue;
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_DRUMS) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = midiDrumChannel;
        } else {
          midiDrumChannel = midiTempValue;
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_VIZ) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = vizMode;
        } else {
          vizMode = midiTempValue;
          midiEditing = false;
        }
      }
    }
  }
}
