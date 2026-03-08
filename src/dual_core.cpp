#include "dual_core.h"
#include "voice_manager.h"
#include "display.h"
#include "encoder.h"
#include "sid_mode.h"
#include "fx_chip.h"
#include "sample_player.h"
#include "preset.h"
#include "YM2149.h"

// External YM2149 instance
extern YM2149 ym;

// ============================================================================
// DUAL-CORE STATE
// ============================================================================

DisplaySnapshot displaySnapshotCopy;
queue_t commandQueue;
char presetNameCmd[9] = "        ";

// Pending flash operation flags (Core 1 sets, Core 0 polls and clears)
volatile bool presetSavePending = false;
volatile uint8_t presetSaveSlot = 0;
volatile bool presetDeletePending = false;
volatile uint8_t presetDeleteSlot = 0;
volatile bool midiSavePending = false;

// SID preset pending flags
volatile bool sidPresetSavePending = false;
volatile uint8_t sidPresetSaveSlot = 0;
volatile bool sidPresetDeletePending = false;
volatile uint8_t sidPresetDeleteSlot = 0;

// Cooperative pause mechanism for flash operations
volatile bool flashPauseRequested = false;
volatile bool core1Paused = false;

// Check if Core 0 requested a pause for flash operations and cooperatively wait
void checkFlashPause() {
  if (flashPauseRequested) {
    core1Paused = true;
    while (flashPauseRequested) {
      delayMicroseconds(10);
    }
    core1Paused = false;
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void dualCoreInit() {
  // Initialize command queue (thread-safe, blocking)
  queue_init(&commandQueue, sizeof(Command), CMD_QUEUE_SIZE);

  // Clear snapshot copy (Core 1 will populate it)
  memset(&displaySnapshotCopy, 0, sizeof(DisplaySnapshot));
}

// ============================================================================
// SNAPSHOT MANAGEMENT (Core 0)
// ============================================================================

void updateSnapshot() {
  // Copy current state into snapshot copy
  // Called from Core 1 - reads Core 0's state directly (safe for display purposes)

  // Voice state
  for (uint8_t c = 0; c < 3; c++) {
    for (uint8_t v = 0; v < 3; v++) {
      if (c == FX_CHIP && fxModeEnabled) {
        // FX chip uses its own voice tracking
        displaySnapshotCopy.voiceActive[c][v] = fxVoiceActive[v];
        // Use FX type's volume for display
        uint8_t fxVol = 0;
        switch (fxType) {
          case FX_ECHO: fxVol = echoVolume; break;
          case FX_ARP: fxVol = arpVolume; break;
          case FX_BIT_CRUSH: fxVol = bitCrushVolume; break;
          case FX_PSEUDO_REVERB: fxVol = reverbVolume; break;
          case FX_CHORUS: fxVol = chorusVolume; break;
          case FX_HARMONIZER: fxVol = harmVolume; break;
          case FX_GATE: fxVol = gateVolume; break;
          default: fxVol = 10; break;
        }
        displaySnapshotCopy.voiceVol[c][v] = fxVoiceActive[v] ? fxVol : 0;
        displaySnapshotCopy.voiceNote[c][v] = fxVoiceNote[v];  // Use actual FX note
        displaySnapshotCopy.voicePeriod[c][v] = fxVoicePeriod[v];  // Actual FX period
      }
      // SID mode: chips 1 and 2 use sidState instead of voiceActive
      else if (sidModeGlobal && (c == 1 || c == 2)) {
        uint8_t sidIdx = (c - 1) * 3 + v;  // Map to sidState[0-5]
        displaySnapshotCopy.voiceActive[c][v] = sidState[sidIdx].active;
        displaySnapshotCopy.voiceVol[c][v] = sidState[sidIdx].volume;
        displaySnapshotCopy.voiceNote[c][v] = sidVoiceNote[sidIdx];
        displaySnapshotCopy.voicePeriod[c][v] = sidState[sidIdx].period;
      }
      else {
        displaySnapshotCopy.voiceActive[c][v] = voiceActive[c][v];
        displaySnapshotCopy.voiceVol[c][v] = voiceVol[c][v];
        displaySnapshotCopy.voiceNote[c][v] = voiceNote[c][v];
        displaySnapshotCopy.voicePeriod[c][v] = (uint16_t)(curPeriod[c][v] + 0.5f);  // Actual period with all mods
      }
    }
  }

  // Envelope state
  for (uint8_t i = 0; i < 9; i++) {
    displaySnapshotCopy.envStage[i] = envStage[i];
    displaySnapshotCopy.envLevel[i] = envLevel[i];
    displaySnapshotCopy.vibPhase[i] = vibPhase[i];
  }

  // Mode and settings
  displaySnapshotCopy.polyMode = polyMode;
  displaySnapshotCopy.linkMode = linkMode;
  displaySnapshotCopy.voiceLinkMask = voiceLinkMask;

  // Copy all voice settings
  for (uint8_t i = 0; i < 9; i++) {
    displaySnapshotCopy.voiceSettings[i] = voiceSettings[i];
  }

  // Pot state
  for (uint8_t i = 0; i < 3; i++) {
    displaySnapshotCopy.potValues[i] = potValues[i];
    displaySnapshotCopy.potAssignments[i] = potAssignments[i];
  }

  // SID state for visualization (legacy per-voice SID, kept for compatibility)
  displaySnapshotCopy.sidWaveType = sidWaveType;
  displaySnapshotCopy.sidDuty = sidDuty;
  displaySnapshotCopy.sidChip = 0;  // Legacy field - global SID uses chips 1+2

  // FX chip state
  displaySnapshotCopy.fxModeEnabled = fxModeEnabled;
  displaySnapshotCopy.fxType = fxType;
  displaySnapshotCopy.fxRouting = fxRouting;
  displaySnapshotCopy.echoDelayMs = echoDelayMs;
  displaySnapshotCopy.echoRepeats = echoRepeats;
  displaySnapshotCopy.echoDecay = echoDecay;
  displaySnapshotCopy.echoVolume = echoVolume;
  displaySnapshotCopy.arpPattern = arpPattern;
  displaySnapshotCopy.arpSpeedMs = arpSpeedMs;
  displaySnapshotCopy.arpVolume = arpVolume;
  displaySnapshotCopy.arpOctave = arpOctave;

  // Bit Crush state
  displaySnapshotCopy.bitCrushBits = bitCrushBits;
  displaySnapshotCopy.bitCrushRate = bitCrushRate;
  displaySnapshotCopy.bitCrushVolume = bitCrushVolume;
  displaySnapshotCopy.bitCrushDuration = bitCrushDuration;

  // Pseudo Reverb state
  displaySnapshotCopy.reverbTaps = reverbTaps;
  displaySnapshotCopy.reverbSpacing = reverbSpacing;
  displaySnapshotCopy.reverbDecay = reverbDecay;
  displaySnapshotCopy.reverbDetune = reverbDetune;
  displaySnapshotCopy.reverbVolume = reverbVolume;

  // Chorus state
  displaySnapshotCopy.chorusDetune1 = chorusDetune1;
  displaySnapshotCopy.chorusDetune2 = chorusDetune2;
  displaySnapshotCopy.chorusVolume = chorusVolume;
  displaySnapshotCopy.chorusRate = chorusRate;

  // Harmonizer state
  displaySnapshotCopy.harmChord = harmChord;
  displaySnapshotCopy.harmVolume = harmVolume;
  displaySnapshotCopy.harmOctave = harmOctave;

  // Gate state
  displaySnapshotCopy.gateRateMs = gateRateMs;
  displaySnapshotCopy.gatePattern = gatePattern;
  displaySnapshotCopy.gateVolume = gateVolume;
  displaySnapshotCopy.gateDuty = gateDuty;
  displaySnapshotCopy.gateSeed = gateSeed;

  // Sample state
  displaySnapshotCopy.sampleSelect = sampleSelect;
  displaySnapshotCopy.sampleMode = sampleMode;
  displaySnapshotCopy.sampleVolume = sampleVolume;
}

// ============================================================================
// COMMAND QUEUE (Core 1 → Core 0)
// ============================================================================

bool sendCommand(CommandType type, uint8_t param1, uint8_t param2, uint8_t param3, int8_t value) {
  Command cmd;
  cmd.type = type;
  cmd.param1 = param1;
  cmd.param2 = param2;
  cmd.param3 = param3;
  cmd.value = value;

  // Non-blocking add to queue
  return queue_try_add(&commandQueue, &cmd);
}

// Helper: check if current target affects Chip 0
static bool targetAffectsChip0() {
  return currentTarget == TARGET_ALL ||
         currentTarget == TARGET_CHIP0 ||
         currentTarget == TARGET_V1 ||
         currentTarget == TARGET_V2 ||
         currentTarget == TARGET_V3;
}

void processCommands() {
  Command cmd;

  // Process all pending commands
  while (queue_try_remove(&commandQueue, &cmd)) {
    bool needsSync = false;  // Track if we need to sync linked chips

    switch (cmd.type) {
      case CMD_SET_POLY_MODE:
        if (cmd.param1 < MODE_COUNT) polyMode = cmd.param1;
        break;

      case CMD_SET_LINK_MODE:
        applyLinkMode(cmd.param1);
        break;

      case CMD_SET_VOICE_LINK_MASK:
        applyVoiceLinkMask(cmd.param1);
        break;

      case CMD_SET_TARGET:
        currentTarget = (SettingsTarget)cmd.param1;
        break;

      case CMD_SET_DETUNE:
        applyDetune(cmd.value);
        needsSync = true;
        break;

      case CMD_SET_OCTAVE:
        applyOctave(cmd.value);
        needsSync = true;
        break;

      case CMD_SET_VIB_ON:
        applyVibOn(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_VIB_RATE:
        applyVibRate(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_VIB_DEPTH:
        applyVibDepth(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_VIB_DELAY:
        applyVibDelay(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_NOISE_FREQ:
        applyNoiseFreq(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_ENV_ATTACK:
        applyEnvAttack(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_ENV_DECAY:
        applyEnvDecay(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_ENV_SUSTAIN:
        applyEnvSustain(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_WAVE:
        applySidWave(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_DUTY:
        applySidDuty(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_PWM_RATE:
        applySidPwmRate(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_PWM_DEPTH:
        applySidPwmDepth(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_SYNC:
        applySidSync(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_RING:
        applySidRing(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_SID_NOISE:
        applySidNoise(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_ENV_RELEASE:
        applyEnvRelease(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_MAX_VOLUME:
        applyMaxVolume(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_PORTA_ON:
        applyPortaOn(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_PORTA_SPEED:
        applyPortaSpeed(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_TREMOLO_ON:
        applyTremoloOn(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_TREMOLO_RATE:
        applyTremoloRate(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_TREMOLO_DEPTH:
        applyTremoloDepth(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_PITCH_ENV_AMT:
        applyPitchEnvAmt(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_PITCH_ENV_TIME:
        applyPitchEnvTime(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_PITCH_ENV_DIR:
        applyPitchEnvDir(cmd.param1);
        needsSync = true;
        break;

      case CMD_SET_POT_ASSIGN:
        // param1 = pot index, param2 = category, param3 = paramIndex, value = target
        if (cmd.param1 < 3) {
          potAssignments[cmd.param1].category = (PotCategory)cmd.param2;
          potAssignments[cmd.param1].paramIndex = cmd.param3;
          potAssignments[cmd.param1].target = (uint8_t)cmd.value;
        }
        break;

      case CMD_ALL_NOTES_OFF:
        allNotesOffPanic();
        break;

      case CMD_RESET_ALL:
        allNotesOffPanic();
        settingsInit();
        effectsInit();
        fxSetEnabled(false);
        break;

      // FX Chip commands
      case CMD_SET_FX_ENABLED:
        fxSetEnabled(cmd.param1 != 0);
        break;

      case CMD_SET_FX_TYPE:
        // Reset state when changing FX types
        if (fxType != cmd.param1) {
          // Stop all FX voices
          stopAllFxVoices();
          // Clear arp state
          heldNoteCount = 0;
          arpIndex = 0;
          arpDirection = 1;
          lastArpStep = 0;
          // Clear echo/reverb state
          for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
            noteHistory[i].active = false;
          }
          // Reset voice tracking
          for (int i = 0; i < 3; i++) {
            fxVoiceActive[i] = false;
          }
          // Clear bit crush state
          for (int i = 0; i < 3; i++) {
            crushedNotes[i].active = false;
          }
          crushUpdateCounter = 0;
        }
        fxType = cmd.param1;
        break;

      case CMD_SET_FX_ROUTING:
        fxRouting = cmd.param1;
        break;

      case CMD_SET_ECHO_DELAY:
        if (fxClockSync) {
          echoDelayMs = cmd.param1;  // Division index (0-7)
        } else {
          echoDelayMs = cmd.param1 * 20;  // param1 is 5-100, scale to 100-2000ms
        }
        break;

      case CMD_SET_ECHO_REPEATS:
        echoRepeats = cmd.param1;
        break;

      case CMD_SET_ECHO_DECAY:
        echoDecay = cmd.param1;
        break;

      case CMD_SET_ECHO_VOLUME:
        echoVolume = cmd.param1;
        break;

      case CMD_SET_ARP_PATTERN:
        arpPattern = cmd.param1;
        break;

      case CMD_SET_ARP_SPEED:
        if (fxClockSync) {
          arpSpeedMs = cmd.param1;  // Division index (0-7)
        } else {
          arpSpeedMs = cmd.param1 * 5;  // param1 is 10-100, scale to 50-500ms
        }
        break;

      case CMD_SET_ARP_VOLUME:
        arpVolume = cmd.param1;
        break;

      case CMD_SET_ARP_OCTAVE:
        arpOctave = cmd.value;  // -2 to +2
        break;

      // Bit Crush commands
      case CMD_SET_BIT_CRUSH_BITS:
        bitCrushBits = cmd.param1;
        break;

      case CMD_SET_BIT_CRUSH_RATE:
        bitCrushRate = cmd.param1;
        break;

      case CMD_SET_BIT_CRUSH_VOLUME:
        bitCrushVolume = cmd.param1;
        break;

      case CMD_SET_BIT_CRUSH_DURATION:
        bitCrushDuration = cmd.param1 * 10;  // param1 is 5-50, scale to 50-500ms
        if (bitCrushDuration < 50) bitCrushDuration = 50;
        break;

      // Pseudo Reverb commands
      case CMD_SET_REVERB_TAPS:
        reverbTaps = cmd.param1;
        if (reverbTaps < 1) reverbTaps = 1;  // Minimum 1 tap
        break;

      case CMD_SET_REVERB_SPACING:
        if (fxClockSync) {
          reverbSpacing = cmd.param1;  // Division index (0-7)
        } else {
          reverbSpacing = cmd.param1;
          if (reverbSpacing < 10) reverbSpacing = 10;  // Minimum 10ms to avoid division issues
        }
        break;

      case CMD_SET_REVERB_DECAY:
        reverbDecay = cmd.param1;
        if (reverbDecay < 1) reverbDecay = 1;  // Minimum 1 decay
        break;

      case CMD_SET_REVERB_DETUNE:
        reverbDetune = cmd.value;
        break;

      case CMD_SET_REVERB_VOLUME:
        reverbVolume = cmd.param1;
        break;

      // Chorus commands
      case CMD_SET_CHORUS_DETUNE1:
        chorusDetune1 = cmd.value;
        break;

      case CMD_SET_CHORUS_DETUNE2:
        chorusDetune2 = cmd.value;
        break;

      case CMD_SET_CHORUS_VOLUME:
        chorusVolume = cmd.param1;
        break;

      case CMD_SET_CHORUS_RATE:
        chorusRate = cmd.param1;  // 0=static, 5-80 in tenths of Hz (0.5-8.0Hz)
        break;

      // Harmonizer commands
      case CMD_SET_HARM_CHORD:
        harmChord = cmd.param1;
        if (harmChord >= HARM_CHORD_COUNT) harmChord = 0;
        break;

      case CMD_SET_HARM_VOLUME:
        harmVolume = cmd.param1;
        if (harmVolume > 15) harmVolume = 15;
        if (harmVolume < 1) harmVolume = 1;
        break;

      case CMD_SET_HARM_OCTAVE:
        harmOctave = cmd.value;  // -2 to +2
        break;

      // Gate commands
      case CMD_SET_GATE_RATE:
        if (fxClockSync) {
          gateRateMs = cmd.param1;  // Division index (0-7)
        } else {
          gateRateMs = cmd.param1 * 5;  // param1 is 6-100, scale to 30-500ms
          if (gateRateMs < 30) gateRateMs = 30;
        }
        break;

      case CMD_SET_GATE_PATTERN:
        gatePattern = cmd.param1;
        if (gatePattern >= GATE_PATTERN_COUNT) gatePattern = 0;
        break;

      case CMD_SET_GATE_VOLUME:
        gateVolume = cmd.param1;
        if (gateVolume > 15) gateVolume = 15;
        if (gateVolume < 1) gateVolume = 1;
        break;

      case CMD_SET_GATE_DUTY:
        gateDuty = cmd.param1;
        if (gateDuty < 1) gateDuty = 1;
        if (gateDuty > 7) gateDuty = 7;
        break;

      case CMD_SET_GATE_SEED:
        gateSeed = cmd.param1;
        if (gateSeed > 15) gateSeed = 15;
        break;

      // Sample commands
      case CMD_SET_SAMPLE_SELECT:
        sampleSelect = cmd.param1;
        if (sampleSelect >= SAMPLE_COUNT) sampleSelect = 0;
        break;

      case CMD_SET_SAMPLE_MODE:
        sampleMode = cmd.param1;
        if (sampleMode >= SAMPLE_MODE_COUNT) sampleMode = 0;
        break;

      case CMD_SET_SAMPLE_VOLUME:
        sampleVolume = cmd.param1;
        if (sampleVolume > 15) sampleVolume = 15;
        if (sampleVolume < 1) sampleVolume = 1;
        break;

      // Preset commands
      case CMD_PRESET_LOAD:
        presetLoad(cmd.param1);
        break;

      case CMD_PRESET_SAVE:
        presetSaveUser(cmd.param1, presetNameCmd);
        break;

      case CMD_PRESET_DELETE:
        presetDeleteUser(cmd.param1);
        break;

      default:
        break;
    }

    // Sync Chip 0 settings to linked chips when in linked mode
    if (needsSync && linkMode != LINK_OFF && targetAffectsChip0()) {
      syncLinkedChipSettings();
    }
  }

  // Handle pending flash operations (set by Core 1, processed here on Core 0)
  // This avoids race conditions - Core 0 controls when idleOtherCore happens
  // Read slot/flag into locals before clearing to avoid race with Core 1
  if (presetSavePending) {
    uint8_t slot = presetSaveSlot;
    presetSavePending = false;
    presetSaveUser(slot, presetNameCmd);
  }

  if (presetDeletePending) {
    uint8_t slot = presetDeleteSlot;
    presetDeletePending = false;
    presetDeleteUser(slot);
  }

  if (midiSavePending) {
    midiSavePending = false;
    saveGlobalSettings();
  }

  // SID preset operations (handled on Core 0 to avoid flash bus conflict)
  if (sidPresetSavePending) {
    uint8_t slot = sidPresetSaveSlot;
    sidPresetSavePending = false;
    sidPresetSaveUser(slot, presetNameCmd);
  }

  if (sidPresetDeletePending) {
    uint8_t slot = sidPresetDeleteSlot;
    sidPresetDeletePending = false;
    sidPresetDeleteUser(slot);
  }
}

// ============================================================================
// CORE 1 ENTRY POINT
// ============================================================================

void core1Entry() {
  // Core 1 main loop - handles display and encoder

  while (true) {
    // Check if Core 0 requested a pause for flash operations
    // This MUST be at the start of the loop, BEFORE any I2C/display operations
    checkFlashPause();

    // Update snapshot from Core 0's state
    updateSnapshot();

    // Handle encoder input (reads hardware, sends commands)
    handleEncoder();

    // Check for flash pause between encoder and display (reduces response latency)
    checkFlashPause();

    // Update OLED display based on current mode
    switch (displayMode) {
      case DISPLAY_VIZ:
        if (vizMode == VIZ_MODE_SCOPE) {
          updateDisplayScope();
        } else if (vizMode == VIZ_MODE_MATRIX) {
          updateDisplayMatrix();
        } else {
          updateDisplay();
        }
        break;
      case DISPLAY_MENU:
        updateMenu();
        break;
      case DISPLAY_SETTINGS:
        switch (settingsSubmenu) {
          case SUBMENU_VIBRATO:
            updateVibratoSubmenu();
            break;
          case SUBMENU_ENVELOPE:
            updateEnvelopeSubmenu();
            break;
          case SUBMENU_PITCH:
            updatePitchSubmenu();
            break;
          case SUBMENU_GLIDE:
            updateGlideSubmenu();
            break;
          case SUBMENU_TREMOLO:
            updateTremoloSubmenu();
            break;
          case SUBMENU_PITCH_ENV:
            updatePitchEnvSubmenu();
            break;
          case SUBMENU_SID:
            updateSidSubmenu();
            break;
          default:
            updateSettingsMenu();
            break;
        }
        break;
      case DISPLAY_POTS:
        updatePotsMenu();
        break;
      case DISPLAY_FX:
        updateFXSubmenu();
        break;
      case DISPLAY_PRESETS:
        updatePresetsMenu();
        break;
      case DISPLAY_MIDI:
        updateMidiMenu();
        break;
    }

    // Check for flash pause after display update (display.display() can block 50-100ms)
    checkFlashPause();

    // Small delay to prevent tight spinning
    delay(10);
  }
}
