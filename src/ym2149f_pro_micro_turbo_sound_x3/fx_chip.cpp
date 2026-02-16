#include "fx_chip.h"
#include "voice_manager.h"
#include "settings.h"
#include "effects.h"
#include "YM2149.h"
#include "sample_player.h"

// External YM2149 instance
extern YM2149 ym;

// ============================================================================
// FX STATE
// ============================================================================

bool fxModeEnabled = false;
uint8_t fxType = FX_ECHO;
uint8_t fxRouting = FX_ROUTE_ALL;

// Echo parameters
uint16_t echoDelayMs = 200;
uint8_t echoRepeats = 3;
uint8_t echoDecay = 4;
uint8_t echoVolume = 12;  // Base volume for echo (1-15)

// Arp parameters
uint8_t arpPattern = ARP_UP;
uint16_t arpSpeedMs = 100;
uint8_t arpVolume = 12;  // Base volume for arp (1-15)
int8_t arpOctave = 0;    // Octave offset (-2 to +2)

// Bit Crush parameters
uint8_t bitCrushBits = 2;         // Bit depth (1-4, lower = crunchier)
uint8_t bitCrushRate = 3;         // Update rate divisor (1-10)
uint8_t bitCrushVolume = 12;      // Volume (1-15)
uint16_t bitCrushDuration = 200;  // Duration in ms (50-500)

// Pseudo Reverb parameters
uint8_t reverbTaps = 4;           // Number of echo taps (2-6)
uint8_t reverbSpacing = 40;       // Base spacing in ms (20-100)
uint8_t reverbDecay = 4;          // Volume decay per tap (1-8)
int8_t reverbDetune = 2;          // Pitch variation per tap (-5 to +5 cents)
uint8_t reverbVolume = 12;        // Starting volume (1-15)

// Chorus parameters
int8_t chorusDetune1 = -10;       // Voice 1 detune (-50 to +50 cents)
int8_t chorusDetune2 = 10;        // Voice 2 detune (-50 to +50 cents)
uint8_t chorusVolume = 12;        // Volume per voice (1-15)
uint16_t chorusDuration = 0;      // Duration in ms (0=follow note, 50-2000)

// Note history for echo
NoteEvent noteHistory[NOTE_HISTORY_SIZE];

// Held notes for arp
uint8_t heldNotes[MAX_HELD_NOTES];
uint8_t heldNoteVelocity[MAX_HELD_NOTES];
uint8_t heldNoteCount = 0;
uint8_t arpIndex = 0;
int8_t arpDirection = 1;
uint32_t lastArpStep = 0;

// FX voice management
uint8_t fxVoiceIdx = 0;

// Voice release timing
uint32_t fxVoiceStartTime[3] = {0, 0, 0};
bool fxVoiceActive[3] = {false, false, false};
uint8_t fxVoiceNote[3] = {60, 60, 60};  // Track note per voice for visualization
uint16_t fxVoicePeriod[3] = {0, 0, 0};  // Track actual period per voice for visualization

// Bit Crush state
CrushedNote crushedNotes[3] = {{0, 0, 0, false}, {0, 0, 0, false}, {0, 0, 0, false}};
uint8_t crushUpdateCounter = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void fxInit() {
  fxModeEnabled = false;
  fxType = FX_ECHO;
  fxRouting = FX_ROUTE_ALL;

  echoDelayMs = 200;
  echoRepeats = 3;
  echoDecay = 4;
  echoVolume = 12;

  arpPattern = ARP_UP;
  arpSpeedMs = 100;
  arpVolume = 12;
  arpOctave = 0;

  // Clear voice tracking
  for (int i = 0; i < 3; i++) {
    fxVoiceStartTime[i] = 0;
    fxVoiceActive[i] = false;
  }

  // Bit Crush defaults
  bitCrushBits = 2;
  bitCrushRate = 3;
  bitCrushVolume = 12;
  bitCrushDuration = 200;

  // Pseudo Reverb defaults
  reverbTaps = 4;
  reverbSpacing = 40;
  reverbDecay = 4;
  reverbDetune = 2;
  reverbVolume = 12;

  // Chorus defaults
  chorusDetune1 = -10;
  chorusDetune2 = 10;
  chorusVolume = 12;
  chorusDuration = 0;  // 0 = follow note

  // Clear bit crush state
  for (int i = 0; i < 3; i++) {
    crushedNotes[i].active = false;
  }
  crushUpdateCounter = 0;

  // Clear note history
  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    noteHistory[i].active = false;
    noteHistory[i].note = 0;
    noteHistory[i].velocity = 0;
    noteHistory[i].timestamp = 0;
    noteHistory[i].echoCount = 0;
  }

  // Clear held notes
  heldNoteCount = 0;
  arpIndex = 0;
  arpDirection = 1;
  lastArpStep = 0;
  fxVoiceIdx = 0;
}

// ============================================================================
// FX VOICE MANAGEMENT
// ============================================================================

void setFxVoice(uint8_t note, uint8_t vol) {
  uint8_t v = fxVoiceIdx;
  fxVoiceIdx = (fxVoiceIdx + 1) % 3;

  uint16_t period = noteToPeriod(note);
  setVoice(FX_CHIP, v, period, vol);

  // Track when this voice was triggered for auto-release
  fxVoiceStartTime[v] = millis();
  fxVoiceActive[v] = true;
  fxVoiceNote[v] = note;  // Track note for visualization
  fxVoicePeriod[v] = period;  // Track period for visualization
}

void stopFxVoice(uint8_t voice) {
  stopVoice(FX_CHIP, voice);
}

void stopAllFxVoices() {
  for (uint8_t v = 0; v < 3; v++) {
    stopVoice(FX_CHIP, v);
  }
}

void fxPanic() {
  // Stop all FX voices
  stopAllFxVoices();

  // Clear held notes for arp
  heldNoteCount = 0;
  arpIndex = 0;
  arpDirection = 1;
  lastArpStep = 0;

  // Clear echo history
  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    noteHistory[i].active = false;
  }

  // Clear bit crush state
  for (int i = 0; i < 3; i++) {
    crushedNotes[i].active = false;
  }

  // Clear voice tracking
  for (int i = 0; i < 3; i++) {
    fxVoiceActive[i] = false;
  }
}

void fxSetEnabled(bool enabled) {
  fxModeEnabled = enabled;

  // Always reset state when toggling FX mode
  stopAllFxVoices();

  // Clear held notes for arp
  heldNoteCount = 0;
  arpIndex = 0;
  arpDirection = 1;
  lastArpStep = 0;

  // Clear echo history
  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    noteHistory[i].active = false;
  }

  // Clear voice tracking
  for (int i = 0; i < 3; i++) {
    fxVoiceActive[i] = false;
  }

  // Ensure FX chip mixer is set correctly when enabling
  if (enabled) {
    enableTones(FX_CHIP);
  }
}

// ============================================================================
// ROUTING CHECK
// ============================================================================

bool fxShouldProcess(uint8_t chip, uint8_t voice) {
  switch (fxRouting) {
    case FX_ROUTE_ALL:
      return true;
    case FX_ROUTE_CHIP0:
      return chip == 0;
    case FX_ROUTE_CHIP1:
      return chip == 1;
    case FX_ROUTE_0A:
      return chip == 0 && voice == 0;
    case FX_ROUTE_0B:
      return chip == 0 && voice == 1;
    case FX_ROUTE_0C:
      return chip == 0 && voice == 2;
    case FX_ROUTE_1A:
      return chip == 1 && voice == 0;
    case FX_ROUTE_1B:
      return chip == 1 && voice == 1;
    case FX_ROUTE_1C:
      return chip == 1 && voice == 2;
    default:
      return true;
  }
}

// ============================================================================
// NOTE TRACKING
// ============================================================================

void fxNoteOn(uint8_t note, uint8_t velocity, uint8_t chip, uint8_t voice) {
  if (!fxModeEnabled) return;
  if (!fxShouldProcess(chip, voice)) return;

  // --- Echo: Add to note history ---
  if (fxType == FX_ECHO) {
    // Find empty slot in note history
    int slot = -1;
    for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
      if (!noteHistory[i].active) {
        slot = i;
        break;
      }
    }
    // If no empty slot, use oldest entry
    if (slot < 0) {
      uint32_t oldest = 0xFFFFFFFF;
      for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
        if (noteHistory[i].timestamp < oldest) {
          oldest = noteHistory[i].timestamp;
          slot = i;
        }
      }
    }

    if (slot >= 0) {
      noteHistory[slot].note = note;
      noteHistory[slot].velocity = velocity;
      noteHistory[slot].timestamp = millis();
      noteHistory[slot].echoCount = 0;
      noteHistory[slot].active = true;
    }
  }

  // --- Arp: Add to held notes ---
  if (fxType == FX_ARP) {
    // Check if already held
    for (int i = 0; i < heldNoteCount; i++) {
      if (heldNotes[i] == note) return;
    }
    // Add to held notes (sorted for arp patterns)
    if (heldNoteCount < MAX_HELD_NOTES) {
      // Insert sorted
      int insertIdx = heldNoteCount;
      for (int i = 0; i < heldNoteCount; i++) {
        if (note < heldNotes[i]) {
          insertIdx = i;
          break;
        }
      }
      // Shift notes up
      for (int i = heldNoteCount; i > insertIdx; i--) {
        heldNotes[i] = heldNotes[i - 1];
        heldNoteVelocity[i] = heldNoteVelocity[i - 1];
      }
      heldNotes[insertIdx] = note;
      heldNoteVelocity[insertIdx] = velocity;
      heldNoteCount++;

      // Play immediate note when added (like unison/octave do)
      setFxVoice(note, arpVolume);
      // Reset arp timing so arp starts fresh
      lastArpStep = millis();
    }
  }

  // --- Bit Crush: Play quantized/crushed note ---
  if (fxType == FX_BIT_CRUSH) {
    uint8_t v = fxVoiceIdx;
    fxVoiceIdx = (fxVoiceIdx + 1) % 3;

    uint16_t period = noteToPeriod(note);
    setVoice(FX_CHIP, v, period, bitCrushVolume);

    crushedNotes[v].originalNote = note;
    crushedNotes[v].crushedNote = note;
    crushedNotes[v].voice = v;
    crushedNotes[v].active = true;

    fxVoiceStartTime[v] = millis();
    fxVoiceActive[v] = true;
    fxVoiceNote[v] = note;  // Track note for visualization
    fxVoicePeriod[v] = period;  // Track period for visualization
  }

  // --- Pseudo Reverb: Add to note history for reverb taps ---
  if (fxType == FX_PSEUDO_REVERB) {
    // Find empty slot in note history (same as echo)
    int slot = -1;
    for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
      if (!noteHistory[i].active) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      uint32_t oldest = 0xFFFFFFFF;
      for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
        if (noteHistory[i].timestamp < oldest) {
          oldest = noteHistory[i].timestamp;
          slot = i;
        }
      }
    }
    if (slot >= 0) {
      noteHistory[slot].note = note;
      noteHistory[slot].velocity = velocity;
      noteHistory[slot].timestamp = millis();
      noteHistory[slot].echoCount = 0;
      noteHistory[slot].active = true;
    }
  }

  // --- Chorus: Play all 3 FX voices with detuning ---
  if (fxType == FX_CHORUS) {
    uint8_t vol = (velocity * chorusVolume) / 127;
    if (vol < 1) vol = 1;

    // Voice 0: original pitch
    uint16_t period0 = noteToPeriod(note);
    setVoice(FX_CHIP, 0, period0, vol);
    fxVoiceStartTime[0] = millis();
    fxVoiceActive[0] = true;
    fxVoiceNote[0] = note;  // Track note for visualization
    fxVoicePeriod[0] = period0;  // Track period for visualization

    // Voice 1: detuned by chorusDetune1
    float freq1 = 440.0f * powf(2.0f, ((int)note - 69 + (chorusDetune1 / 100.0f)) / 12.0f);
    uint16_t period1 = (uint16_t)(1789772.5f / (16.0f * freq1) + 0.5f);
    setVoice(FX_CHIP, 1, period1, vol);
    fxVoiceStartTime[1] = millis();
    fxVoiceActive[1] = true;
    fxVoiceNote[1] = note;  // Track note for visualization
    fxVoicePeriod[1] = period1;  // Track period for visualization

    // Voice 2: detuned by chorusDetune2
    float freq2 = 440.0f * powf(2.0f, ((int)note - 69 + (chorusDetune2 / 100.0f)) / 12.0f);
    uint16_t period2 = (uint16_t)(1789772.5f / (16.0f * freq2) + 0.5f);
    setVoice(FX_CHIP, 2, period2, vol);
    fxVoiceStartTime[2] = millis();
    fxVoiceActive[2] = true;
    fxVoiceNote[2] = note;  // Track note for visualization
    fxVoicePeriod[2] = period2;  // Track period for visualization
  }
}

void fxNoteOff(uint8_t note) {
  if (!fxModeEnabled) return;

  // --- Arp: Remove from held notes ---
  if (fxType == FX_ARP) {
    for (int i = 0; i < heldNoteCount; i++) {
      if (heldNotes[i] == note) {
        // Shift notes down
        for (int j = i; j < heldNoteCount - 1; j++) {
          heldNotes[j] = heldNotes[j + 1];
          heldNoteVelocity[j] = heldNoteVelocity[j + 1];
        }
        heldNoteCount--;
        if (arpIndex >= heldNoteCount && heldNoteCount > 0) {
          arpIndex = 0;
        }
        break;
      }
    }

    // If no more held notes, stop FX voices
    if (heldNoteCount == 0) {
      stopAllFxVoices();
    }
  }

  // --- Chorus: Stop all FX voices when note released ---
  // Since chorus plays all 3 voices for each note, we stop them all
  if (fxType == FX_CHORUS) {
    stopAllFxVoices();
    for (int v = 0; v < 3; v++) {
      fxVoiceActive[v] = false;
    }
  }

  // --- Unison/Octave: notes release naturally with ADS envelope ---
}

// ============================================================================
// FX PROCESSING
// ============================================================================

// Check and release voices that have played long enough
static void fxCheckVoiceRelease() {
  // For Chorus: skip auto-release if duration is 0 (follow note)
  if (fxType == FX_CHORUS && chorusDuration == 0) return;

  // Determine duration based on effect type
  uint16_t duration;
  if (fxType == FX_BIT_CRUSH) {
    duration = bitCrushDuration;
  } else if (fxType == FX_CHORUS) {
    duration = chorusDuration;
  } else {
    duration = FX_VOICE_DURATION_MS;
  }

  uint32_t now = millis();
  for (int v = 0; v < 3; v++) {
    if (fxVoiceActive[v]) {
      if (now - fxVoiceStartTime[v] >= duration) {
        stopVoice(FX_CHIP, v);
        fxVoiceActive[v] = false;

        // Clear bit crush state for this voice
        crushedNotes[v].active = false;
      }
    }
  }
}

static void fxProcessEcho() {
  uint32_t now = millis();

  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    if (!noteHistory[i].active) continue;

    uint32_t elapsed = now - noteHistory[i].timestamp;
    uint8_t expectedEchoes = elapsed / echoDelayMs;

    // Check if we need to trigger a new echo
    if (expectedEchoes > noteHistory[i].echoCount && noteHistory[i].echoCount < echoRepeats) {
      noteHistory[i].echoCount = expectedEchoes;

      // Calculate decayed volume starting from echoVolume
      // Each repeat reduces volume by echoDecay
      int vol = echoVolume;
      for (uint8_t r = 0; r < expectedEchoes; r++) {
        vol = (vol * (16 - echoDecay)) / 16;
      }
      if (vol < 1) vol = 1;
      if (vol > 15) vol = 15;

      setFxVoice(noteHistory[i].note, (uint8_t)vol);
    }

    // Remove old entries after all echoes have played
    if (noteHistory[i].echoCount >= echoRepeats) {
      // Keep for a bit longer to avoid re-triggering
      if (elapsed > echoDelayMs * (echoRepeats + 1)) {
        noteHistory[i].active = false;
      }
    }
  }
}

static void fxProcessArp() {
  // If no notes held, nothing to arpeggiate
  if (heldNoteCount == 0) return;

  uint32_t now = millis();
  if (now - lastArpStep < arpSpeedMs) return;
  lastArpStep = now;

  // Ensure arpIndex is in bounds (may have changed due to note-off)
  if (arpIndex >= heldNoteCount) arpIndex = 0;

  // Get note to play based on pattern
  uint8_t noteIdx;
  if (arpPattern == ARP_RANDOM) {
    // Random pattern: pick a random note each time
    noteIdx = random(heldNoteCount);
  } else {
    // Other patterns: use current index
    noteIdx = arpIndex;
  }

  // Play the current note using arpVolume, with octave offset
  int arpNote = heldNotes[noteIdx] + (arpOctave * 12);
  if (arpNote >= MIDI_NOTE_MIN && arpNote <= MIDI_NOTE_MAX) {
    setFxVoice(arpNote, arpVolume);
  }

  // Advance to next note for the next arp step (non-random patterns)
  switch (arpPattern) {
    case ARP_UP:
      arpIndex++;
      if (arpIndex >= heldNoteCount) arpIndex = 0;
      break;
    case ARP_DOWN:
      if (arpIndex == 0) arpIndex = heldNoteCount - 1;
      else arpIndex--;
      break;
    case ARP_UPDOWN:
      if (heldNoteCount > 1) {
        arpIndex += arpDirection;
        if (arpIndex >= heldNoteCount - 1) {
          arpIndex = heldNoteCount - 1;
          arpDirection = -1;
        } else if (arpIndex == 0) {
          arpDirection = 1;
        }
      }
      break;
    default:
      break;
  }
}

static void fxProcessBitCrush() {
  crushUpdateCounter++;
  if (crushUpdateCounter < bitCrushRate) return;
  crushUpdateCounter = 0;

  // Update active crushed voices - add jitter/wobble
  for (int i = 0; i < 3; i++) {
    if (crushedNotes[i].active) {
      // Add random pitch wobble based on bits setting
      int8_t wobble = (random(1 << (5 - bitCrushBits)) - (1 << (4 - bitCrushBits)));
      uint8_t note = crushedNotes[i].originalNote + wobble;
      if (note < MIDI_NOTE_MIN) note = MIDI_NOTE_MIN;
      if (note > MIDI_NOTE_MAX) note = MIDI_NOTE_MAX;
      uint16_t period = noteToPeriod(note);
      setVoice(FX_CHIP, crushedNotes[i].voice, period, bitCrushVolume);
      fxVoicePeriod[crushedNotes[i].voice] = period;  // Track period for visualization
    }
  }
}

static void fxProcessPseudoReverb() {
  uint32_t now = millis();

  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    if (!noteHistory[i].active) continue;

    uint32_t elapsed = now - noteHistory[i].timestamp;
    uint8_t expectedTaps = elapsed / reverbSpacing;

    // Check if we need to trigger a new reverb tap
    if (expectedTaps > noteHistory[i].echoCount && noteHistory[i].echoCount < reverbTaps) {
      noteHistory[i].echoCount = expectedTaps;

      // Calculate decayed volume
      int vol = reverbVolume;
      for (uint8_t t = 0; t < expectedTaps; t++) {
        vol = (vol * (16 - reverbDecay)) / 16;
      }
      if (vol < 1) vol = 1;
      if (vol > 15) vol = 15;

      // Calculate detuned frequency
      int detuneTotal = reverbDetune * expectedTaps;
      float freq = 440.0f * powf(2.0f, ((int)noteHistory[i].note - 69 + (detuneTotal / 100.0f)) / 12.0f);
      uint16_t period = (uint16_t)(1789772.5f / (16.0f * freq) + 0.5f);

      // Play tap
      uint8_t v = fxVoiceIdx;
      fxVoiceIdx = (fxVoiceIdx + 1) % 3;
      setVoice(FX_CHIP, v, period, (uint8_t)vol);
      fxVoiceStartTime[v] = now;
      fxVoiceActive[v] = true;
      fxVoiceNote[v] = noteHistory[i].note;  // Track note for visualization
      fxVoicePeriod[v] = period;  // Track period for visualization
    }

    // Remove old entries after all taps have played
    if (noteHistory[i].echoCount >= reverbTaps) {
      if (elapsed > reverbSpacing * (reverbTaps + 1)) {
        noteHistory[i].active = false;
      }
    }
  }
}

// ============================================================================
// MAIN UPDATE
// ============================================================================

void fxUpdate() {
  if (!fxModeEnabled) return;

  // Always check for voice release (echo/unison/octave voices need to end)
  fxCheckVoiceRelease();

  switch (fxType) {
    case FX_ECHO:
      fxProcessEcho();
      break;
    case FX_ARP:
      fxProcessArp();
      break;
    case FX_BIT_CRUSH:
      fxProcessBitCrush();
      break;
    case FX_PSEUDO_REVERB:
      fxProcessPseudoReverb();
      break;
    // Unison, Octave, and Chorus are handled in fxNoteOn
    default:
      break;
  }
}

// ============================================================================
// NAME HELPERS
// ============================================================================

const char* getFxTypeName(uint8_t type) {
  switch (type) {
    case FX_NONE: return "OFF";
    case FX_ECHO: return "ECHO";
    case FX_ARP: return "ARP";
    case FX_BIT_CRUSH: return "CRSH";
    case FX_PSEUDO_REVERB: return "VERB";
    case FX_CHORUS: return "CHOR";
    default: return "?";
  }
}

const char* getArpPatternName(uint8_t pattern) {
  switch (pattern) {
    case ARP_UP: return "UP";
    case ARP_DOWN: return "DOWN";
    case ARP_UPDOWN: return "U/D";
    case ARP_RANDOM: return "RND";
    default: return "?";
  }
}

const char* getFxRoutingName(uint8_t routing) {
  switch (routing) {
    case FX_ROUTE_ALL: return "ALL";
    case FX_ROUTE_CHIP0: return "CH0";
    case FX_ROUTE_CHIP1: return "CH1";
    case FX_ROUTE_0A: return "0-A";
    case FX_ROUTE_0B: return "0-B";
    case FX_ROUTE_0C: return "0-C";
    case FX_ROUTE_1A: return "1-A";
    case FX_ROUTE_1B: return "1-B";
    case FX_ROUTE_1C: return "1-C";
    default: return "?";
  }
}
