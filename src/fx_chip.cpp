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
uint16_t echoDelayMs = 140;
uint8_t echoRepeats = 6;
uint8_t echoDecay = 2;
uint8_t echoVolume = 14;  // Base volume for echo (1-15)

// Arp parameters
uint8_t arpPattern = ARP_UP;
uint16_t arpSpeedMs = 100;
uint8_t arpVolume = 12;  // Base volume for arp (1-15)
int8_t arpOctave = 0;    // Octave offset (-2 to +2)

// Bit Crush parameters
uint8_t bitCrushBits = 2;         // Period quantization (1-4, higher = crunchier)
uint8_t bitCrushRate = 4;         // Sample-and-hold update speed (1-10, lower = more stepped)
uint8_t bitCrushVolume = 14;      // Volume (1-15)
uint16_t bitCrushDuration = 200;  // Duration in ms (50-500)

// Pseudo Reverb parameters
uint8_t reverbTaps = 4;           // Number of echo taps (1-12)
uint8_t reverbSpacing = 100;      // Base spacing in ms (10-250)
uint8_t reverbDecay = 1;          // Volume decay per tap (1-8)
int8_t reverbDetune = 1;          // Pitch variation per tap (-5 to +5 cents)
uint8_t reverbVolume = 14;        // Starting volume (1-15)

// Chorus parameters
int8_t chorusDetune1 = -25;       // Voice 1 max detune (-50 to +50 cents)
int8_t chorusDetune2 = 25;        // Voice 2 max detune (-50 to +50 cents)
uint8_t chorusVolume = 14;        // Volume per voice (1-15)
uint8_t chorusRate = 15;          // LFO rate in tenths of Hz (0=static, 5-80 = 0.5-8.0Hz)

// Harmonizer parameters
uint8_t harmChord = HARM_CHORD_MAJOR;
uint8_t harmVolume = 14;
int8_t harmOctave = 0;

// Gate parameters
uint16_t gateRateMs = 120;        // Gate cycle period in ms (30-500)
uint8_t gatePattern = GATE_SQUARE;
uint8_t gateVolume = 14;
uint8_t gateDuty = 4;             // On-time: 4/8 = 50%
uint8_t gateSeed = 0;             // Random pattern seed (0-15)

// Gate state
#define GATE_MAX_HELD 12
static uint8_t gateHeldNotes[GATE_MAX_HELD];
static uint8_t gateHeldVelocity[GATE_MAX_HELD];
static uint8_t gateHeldCount = 0;
static uint8_t gateActiveNote = 0;
static uint8_t gateActiveVel = 0;

// Harmonizer held notes
#define HARM_MAX_HELD 12
static uint8_t harmHeldNotes[HARM_MAX_HELD];
static uint8_t harmHeldVelocity[HARM_MAX_HELD];
static uint8_t harmHeldCount = 0;

// Chord interval table: up to 3 intervals per chord, 0 in slots 1/2 = unused voice
static const int8_t chordIntervals[HARM_CHORD_COUNT][3] = {
  { 12,  0,  0 },  // OCTAVE: +12
  {  7,  0,  0 },  // POWER: +7
  {  4,  7,  0 },  // MAJOR: +4, +7
  {  3,  7,  0 },  // MINOR: +3, +7
  {  2,  7,  0 },  // SUS2: +2, +7
  {  5,  7,  0 },  // SUS4: +5, +7
  {  4,  8,  0 },  // AUG: +4, +8
  {  3,  6,  0 },  // DIM: +3, +6
  {  4,  7, 11 },  // MAJ7: +4, +7, +11
  {  3,  7, 10 },  // MIN7: +3, +7, +10
  {  4,  7, 10 },  // DOM7: +4, +7, +10
  {  7, 12,  0 },  // 5+OCT: +7, +12
};

// Chorus LFO state
static float chorusPhase = 0.0f;        // LFO phase (0-2*PI)
static uint32_t chorusLastUpdate = 0;   // Last LFO update time
static uint8_t chorusActiveNote = 0;    // Currently sounding note
static uint8_t chorusActiveVel = 0;     // Velocity of active note
#define CHORUS_MAX_HELD 12
static uint8_t chorusHeldNotes[CHORUS_MAX_HELD];
static uint8_t chorusHeldVelocity[CHORUS_MAX_HELD];
static uint8_t chorusHeldCount = 0;

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

// MIDI Clock Sync state
volatile uint32_t midiClockTick = 0;
volatile uint32_t lastClockTime = 0;
volatile uint32_t prevClockTime = 0;
volatile bool midiClockRunning = false;
bool fxClockSync = false;

// ============================================================================
// MIDI CLOCK FUNCTIONS
// ============================================================================

void fxClockTick() {
  prevClockTime = lastClockTime;
  lastClockTime = millis();
  midiClockTick++;
}

bool fxClockActive() {
  return fxClockSync && midiClockRunning &&
         (millis() - lastClockTime < FX_CLOCK_TIMEOUT_MS);
}

const char* getClockDivName(uint8_t div) {
  switch (div) {
    case 0: return "1/1";
    case 1: return "1/2";
    case 2: return "1/4";
    case 3: return "1/8";
    case 4: return "1/16";
    case 5: return "1/32";
    case 6: return "1/8d";
    case 7: return "1/8t";
    default: return "?";
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void fxInit() {
  fxModeEnabled = false;
  fxType = FX_ECHO;
  fxRouting = FX_ROUTE_ALL;

  echoDelayMs = 140;
  echoRepeats = 6;
  echoDecay = 2;
  echoVolume = 14;

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
  bitCrushRate = 4;
  bitCrushVolume = 14;
  bitCrushDuration = 200;

  // Pseudo Reverb defaults
  reverbTaps = 4;
  reverbSpacing = 100;
  reverbDecay = 1;
  reverbDetune = 1;
  reverbVolume = 14;

  // Chorus defaults
  chorusDetune1 = -25;
  chorusDetune2 = 25;
  chorusVolume = 14;
  chorusRate = 15;     // 1.5 Hz LFO - slow sweep
  chorusPhase = 0.0f;
  chorusLastUpdate = 0;
  chorusActiveNote = 0;
  chorusActiveVel = 0;
  chorusHeldCount = 0;

  // Harmonizer defaults
  harmChord = HARM_CHORD_MAJOR;
  harmVolume = 14;
  harmOctave = 0;
  harmHeldCount = 0;

  // Gate defaults
  gateRateMs = 120;
  gatePattern = GATE_SQUARE;
  gateVolume = 14;
  gateDuty = 4;
  gateSeed = 0;
  gateHeldCount = 0;
  gateActiveNote = 0;
  gateActiveVel = 0;

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

  // Reset clock state
  midiClockTick = 0;
  lastClockTime = 0;
  prevClockTime = 0;
  midiClockRunning = false;
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

  // Clear chorus held notes
  chorusHeldCount = 0;

  // Clear harmonizer held notes
  harmHeldCount = 0;

  // Clear gate held notes
  gateHeldCount = 0;

  // Clear voice tracking
  for (int i = 0; i < 3; i++) {
    fxVoiceActive[i] = false;
  }
}

void fxSetEnabled(bool enabled) {
  // FX disabled when in SID mode (FX uses chip 2, SID uses chips 1+2)
  if (enabled && sidModeGlobal) {
    return;
  }

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

// Set FX chip voices to chord intervals for harmonizer
static void harmSetVoices(uint8_t note, uint8_t velocity) {
  uint8_t vol = (velocity * harmVolume) / 127;
  if (vol < 1) vol = 1;

  uint32_t now = millis();
  for (int v = 0; v < 3; v++) {
    int8_t interval = chordIntervals[harmChord][v];
    if (interval == 0 && v > 0) {
      // Unused voice slot — silence it
      stopVoice(FX_CHIP, v);
      fxVoiceActive[v] = false;
      continue;
    }
    int harmNote = (int)note + interval + (harmOctave * 12);
    if (harmNote < MIDI_NOTE_MIN || harmNote > MIDI_NOTE_MAX) {
      stopVoice(FX_CHIP, v);
      fxVoiceActive[v] = false;
      continue;
    }
    uint16_t period = noteToPeriod((uint8_t)harmNote);
    setVoice(FX_CHIP, v, period, vol);
    fxVoiceStartTime[v] = now;
    fxVoiceActive[v] = true;
    fxVoiceNote[v] = (uint8_t)harmNote;
    fxVoicePeriod[v] = period;
  }
}

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

  // --- Chorus: Track held notes and play all 3 FX voices with detuning ---
  if (fxType == FX_CHORUS) {
    // Add to held notes list
    if (chorusHeldCount < CHORUS_MAX_HELD) {
      chorusHeldNotes[chorusHeldCount] = note;
      chorusHeldVelocity[chorusHeldCount] = velocity;
      chorusHeldCount++;
    }

    // Set as active note and play
    chorusActiveNote = note;
    chorusActiveVel = velocity;
    chorusPhase = 0.0f;
    chorusLastUpdate = millis();

    uint8_t vol = (velocity * chorusVolume) / 127;
    if (vol < 1) vol = 1;

    // All 3 voices start at base pitch — LFO will sweep voices 1+2
    uint16_t period0 = noteToPeriod(note);
    uint32_t now = millis();
    for (int v = 0; v < 3; v++) {
      setVoice(FX_CHIP, v, period0, vol);
      fxVoiceStartTime[v] = now;
      fxVoiceActive[v] = true;
      fxVoiceNote[v] = note;
      fxVoicePeriod[v] = period0;
    }
  }

  // --- Harmonizer: Track held notes and play chord intervals ---
  if (fxType == FX_HARMONIZER) {
    if (harmHeldCount < HARM_MAX_HELD) {
      harmHeldNotes[harmHeldCount] = note;
      harmHeldVelocity[harmHeldCount] = velocity;
      harmHeldCount++;
    }
    harmSetVoices(note, velocity);
  }

  // --- Gate: Track held notes and play on all 3 FX voices ---
  if (fxType == FX_GATE) {
    if (gateHeldCount < GATE_MAX_HELD) {
      gateHeldNotes[gateHeldCount] = note;
      gateHeldVelocity[gateHeldCount] = velocity;
      gateHeldCount++;
    }
    gateActiveNote = note;
    gateActiveVel = velocity;

    // Set all 3 FX voices to the note (gate LFO will modulate volume)
    uint8_t vol = (velocity * gateVolume) / 127;
    if (vol < 1) vol = 1;
    uint16_t period = noteToPeriod(note);
    uint32_t now = millis();
    for (int v = 0; v < 3; v++) {
      setVoice(FX_CHIP, v, period, vol);
      fxVoiceStartTime[v] = now;
      fxVoiceActive[v] = true;
      fxVoiceNote[v] = note;
      fxVoicePeriod[v] = period;
    }
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

  // --- Chorus: Remove from held notes, switch to previous or stop ---
  if (fxType == FX_CHORUS) {
    // Remove note from held list
    for (int i = 0; i < chorusHeldCount; i++) {
      if (chorusHeldNotes[i] == note) {
        for (int j = i; j < chorusHeldCount - 1; j++) {
          chorusHeldNotes[j] = chorusHeldNotes[j + 1];
          chorusHeldVelocity[j] = chorusHeldVelocity[j + 1];
        }
        chorusHeldCount--;
        break;
      }
    }

    if (chorusHeldCount > 0) {
      // Switch to the most recent held note
      uint8_t newNote = chorusHeldNotes[chorusHeldCount - 1];
      uint8_t newVel = chorusHeldVelocity[chorusHeldCount - 1];
      chorusActiveNote = newNote;
      chorusActiveVel = newVel;

      uint8_t vol = (newVel * chorusVolume) / 127;
      if (vol < 1) vol = 1;

      // Set all 3 voices to base pitch — LFO will handle detune
      uint16_t period0 = noteToPeriod(newNote);
      for (int v = 0; v < 3; v++) {
        setVoice(FX_CHIP, v, period0, vol);
        fxVoiceNote[v] = newNote;
        fxVoicePeriod[v] = period0;
      }
    } else {
      // No more held notes - stop all voices
      stopAllFxVoices();
      for (int v = 0; v < 3; v++) {
        fxVoiceActive[v] = false;
      }
    }
  }

  // --- Harmonizer: Remove from held notes, switch or stop ---
  if (fxType == FX_HARMONIZER) {
    for (int i = 0; i < harmHeldCount; i++) {
      if (harmHeldNotes[i] == note) {
        for (int j = i; j < harmHeldCount - 1; j++) {
          harmHeldNotes[j] = harmHeldNotes[j + 1];
          harmHeldVelocity[j] = harmHeldVelocity[j + 1];
        }
        harmHeldCount--;
        break;
      }
    }

    if (harmHeldCount > 0) {
      uint8_t newNote = harmHeldNotes[harmHeldCount - 1];
      uint8_t newVel = harmHeldVelocity[harmHeldCount - 1];
      harmSetVoices(newNote, newVel);
    } else {
      stopAllFxVoices();
      for (int v = 0; v < 3; v++) {
        fxVoiceActive[v] = false;
      }
    }
  }

  // --- Gate: Remove from held notes, switch or stop ---
  if (fxType == FX_GATE) {
    for (int i = 0; i < gateHeldCount; i++) {
      if (gateHeldNotes[i] == note) {
        for (int j = i; j < gateHeldCount - 1; j++) {
          gateHeldNotes[j] = gateHeldNotes[j + 1];
          gateHeldVelocity[j] = gateHeldVelocity[j + 1];
        }
        gateHeldCount--;
        break;
      }
    }

    if (gateHeldCount > 0) {
      uint8_t newNote = gateHeldNotes[gateHeldCount - 1];
      uint8_t newVel = gateHeldVelocity[gateHeldCount - 1];
      gateActiveNote = newNote;
      gateActiveVel = newVel;

      uint16_t period = noteToPeriod(newNote);
      for (int v = 0; v < 3; v++) {
        uint16_t reg = v * 2;
        ymSafeWrite(FX_CHIP, reg, period & 0xFF);
        ymSafeWrite(FX_CHIP, reg + 1, (period >> 8) & 0x0F);
        fxVoiceNote[v] = newNote;
        fxVoicePeriod[v] = period;
      }
    } else {
      stopAllFxVoices();
      for (int v = 0; v < 3; v++) {
        fxVoiceActive[v] = false;
      }
    }
  }

  // --- Unison/Octave: notes release naturally with ADS envelope ---
}

// ============================================================================
// FX PROCESSING
// ============================================================================

// Check and release voices that have played long enough
static void fxCheckVoiceRelease() {
  // For Chorus: always follow note (no auto-release)
  if (fxType == FX_CHORUS || fxType == FX_HARMONIZER || fxType == FX_GATE) return;

  // Determine duration based on effect type
  uint16_t duration;
  if (fxType == FX_BIT_CRUSH) {
    duration = bitCrushDuration;
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

  // Determine effective delay
  uint16_t effectiveDelay = echoDelayMs;
  if (fxClockSync && !fxClockActive()) return;  // Sync enabled but no clock — wait
  if (fxClockActive() && prevClockTime > 0) {
    // Clock-synced: echoDelayMs holds division index when synced
    uint8_t divIdx = echoDelayMs;
    if (divIdx >= FX_CLOCK_DIV_COUNT) divIdx = 3;  // Default 1/8
    uint32_t msPerTick = lastClockTime - prevClockTime;
    if (msPerTick > 0 && msPerTick < 500) {
      effectiveDelay = msPerTick * clockDivTicks[divIdx];
      if (effectiveDelay < 20) effectiveDelay = 20;
    }
  }

  if (effectiveDelay < 1) effectiveDelay = 20;  // Prevent division by zero

  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    if (!noteHistory[i].active) continue;

    uint32_t elapsed = now - noteHistory[i].timestamp;
    uint8_t expectedEchoes = elapsed / effectiveDelay;

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
      if (elapsed > effectiveDelay * (echoRepeats + 1)) {
        noteHistory[i].active = false;
      }
    }
  }
}

// Track last clock tick that triggered an arp step (for sync mode)
static uint32_t lastArpClockTick = 0;

static void fxProcessArp() {
  // If no notes held, nothing to arpeggiate
  if (heldNoteCount == 0) return;
  if (fxClockSync && !fxClockActive()) return;  // Sync enabled but no clock — wait

  if (fxClockActive()) {
    // Clock-synced mode: step when tick count crosses subdivision boundary
    uint8_t divIdx = arpSpeedMs;  // When synced, arpSpeedMs holds division index (0-7)
    if (divIdx >= FX_CLOCK_DIV_COUNT) divIdx = 3;  // Default to 1/8
    uint8_t ticks = clockDivTicks[divIdx];
    uint32_t currentStep = midiClockTick / ticks;
    uint32_t lastStep = lastArpClockTick / ticks;
    if (currentStep == lastStep) return;  // No new step boundary crossed
    lastArpClockTick = midiClockTick;
  } else {
    // Free-running ms-based mode
    uint32_t now = millis();
    if (now - lastArpStep < arpSpeedMs) return;
    lastArpStep = now;
  }

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

  // Play the current note using arpVolume, with octave offset (clamped to valid range)
  int arpNote = heldNotes[noteIdx] + (arpOctave * 12);
  if (arpNote < MIDI_NOTE_MIN) arpNote = MIDI_NOTE_MIN;
  if (arpNote > MIDI_NOTE_MAX) arpNote = MIDI_NOTE_MAX;
  setFxVoice(arpNote, arpVolume);

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

  // Update active crushed voices with period quantization
  for (int i = 0; i < 3; i++) {
    if (crushedNotes[i].active) {
      uint16_t basePeriod = noteToPeriod(crushedNotes[i].originalNote);

      // Quantize period by stripping lower bits (like reducing bit depth)
      // bits=1: strip 2 bits (step 4), bits=2: strip 3 (step 8),
      // bits=3: strip 4 (step 16), bits=4: strip 5 (step 32)
      uint8_t shift = bitCrushBits + 1;
      uint16_t step = 1 << shift;
      uint16_t quantized = (basePeriod / step) * step;

      // Add small jitter (±1 step) for lo-fi character
      int8_t jitter = (int8_t)(random(3) - 1);  // -1, 0, or +1
      int32_t finalPeriod = (int32_t)quantized + jitter * (int32_t)step;
      if (finalPeriod < 1) finalPeriod = 1;
      if (finalPeriod > 4095) finalPeriod = 4095;

      setVoice(FX_CHIP, crushedNotes[i].voice, (uint16_t)finalPeriod, bitCrushVolume);
      fxVoicePeriod[crushedNotes[i].voice] = (uint16_t)finalPeriod;
    }
  }
}

static void fxProcessPseudoReverb() {
  uint32_t now = millis();

  // Determine effective spacing
  uint16_t effectiveSpacing = reverbSpacing;
  if (fxClockSync && !fxClockActive()) return;  // Sync enabled but no clock — wait
  if (fxClockActive() && prevClockTime > 0) {
    uint8_t divIdx = reverbSpacing;
    if (divIdx >= FX_CLOCK_DIV_COUNT) divIdx = 4;  // Default 1/16
    uint32_t msPerTick = lastClockTime - prevClockTime;
    if (msPerTick > 0 && msPerTick < 500) {
      effectiveSpacing = msPerTick * clockDivTicks[divIdx];
      if (effectiveSpacing < 10) effectiveSpacing = 10;
    }
  }

  if (effectiveSpacing < 1) effectiveSpacing = 10;  // Prevent division by zero

  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    if (!noteHistory[i].active) continue;

    uint32_t elapsed = now - noteHistory[i].timestamp;
    uint8_t expectedTaps = elapsed / effectiveSpacing;

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

      // Calculate detuned period using LUT + fast semitone shift
      int detuneTotal = reverbDetune * expectedTaps;
      float basePeriod = (float)noteToPeriod(noteHistory[i].note);
      uint16_t period = (uint16_t)(basePeriod / fastPow2Semi(detuneTotal / 100.0f) + 0.5f);

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
      if (elapsed > effectiveSpacing * (reverbTaps + 1)) {
        noteHistory[i].active = false;
      }
    }
  }
}

static void fxProcessChorus() {
  if (chorusHeldCount == 0) return;
  if (fxClockSync && !fxClockActive()) return;  // Sync enabled but no clock — wait

  uint32_t now = millis();
  uint32_t elapsed = now - chorusLastUpdate;
  if (elapsed < 10) return;  // Update at ~100Hz max
  chorusLastUpdate = now;

  // Determine LFO rate
  float rateHz;
  if (fxClockActive() && prevClockTime > 0) {
    // Clock-synced: chorusRate holds division index (0-7)
    uint8_t divIdx = chorusRate;
    if (divIdx >= FX_CLOCK_DIV_COUNT) divIdx = 2;  // Default 1/4
    uint32_t msPerTick = lastClockTime - prevClockTime;
    if (msPerTick > 0 && msPerTick < 500) {
      uint32_t cyclePeriodMs = msPerTick * clockDivTicks[divIdx];
      rateHz = 1000.0f / (float)cyclePeriodMs;
    } else {
      rateHz = 1.5f;  // Fallback
    }
  } else {
    if (chorusRate == 0) return;  // Static mode (no LFO) only in free-running
    rateHz = chorusRate / 10.0f;  // Convert tenths to Hz
  }

  // Advance LFO phase
  chorusPhase += 2.0f * 3.14159265f * rateHz * (elapsed / 1000.0f);
  if (chorusPhase > 2.0f * 3.14159265f) chorusPhase -= 2.0f * 3.14159265f;

  // Triangle LFO (smoother on chip than sine, and avoids sinf() cost)
  // Phase 0..PI = ramp up, PI..2PI = ramp down => range -1.0 to +1.0
  float phase01 = chorusPhase / (2.0f * 3.14159265f);  // 0.0 to 1.0
  float lfo = (phase01 < 0.5f) ? (4.0f * phase01 - 1.0f) : (3.0f - 4.0f * phase01);

  // Get base period for active note
  uint16_t basePeriod = noteToPeriod(chorusActiveNote);

  // Scale detune to period offset: detune value maps to period steps
  // At detune=50, offset = ~2.9% of period (50 cents ≈ semitone/2)
  // We use a direct period offset scaled by detune for guaranteed audible effect
  // offset = basePeriod * (2^(cents/1200) - 1), approximated as basePeriod * cents / 1731
  int16_t offset1 = (int16_t)((long)basePeriod * chorusDetune1 * lfo / 1731);
  int16_t offset2 = (int16_t)((long)basePeriod * chorusDetune2 * (-lfo) / 1731);

  // Ensure at least 1 step of movement when detune is nonzero
  if (chorusDetune1 != 0 && offset1 == 0) {
    offset1 = (lfo > 0) ? 1 : (lfo < 0) ? -1 : 0;
  }
  if (chorusDetune2 != 0 && offset2 == 0) {
    offset2 = (-lfo > 0) ? 1 : (-lfo < 0) ? -1 : 0;
  }

  // Apply to voice 1
  int32_t p1 = (int32_t)basePeriod + offset1;
  if (p1 < 1) p1 = 1;
  if (p1 > 4095) p1 = 4095;
  uint16_t period1 = (uint16_t)p1;
  ymSafeWrite(FX_CHIP, 1*2, period1 & 0xFF);
  ymSafeWrite(FX_CHIP, 1*2 + 1, (period1 >> 8) & 0x0F);
  fxVoicePeriod[1] = period1;

  // Apply to voice 2
  int32_t p2 = (int32_t)basePeriod + offset2;
  if (p2 < 1) p2 = 1;
  if (p2 > 4095) p2 = 4095;
  uint16_t period2 = (uint16_t)p2;
  ymSafeWrite(FX_CHIP, 2*2, period2 & 0xFF);
  ymSafeWrite(FX_CHIP, 2*2 + 1, (period2 >> 8) & 0x0F);
  fxVoicePeriod[2] = period2;
}

static void fxProcessHarmonizer() {
  // Harmonizer voices sustain as long as notes are held.
  // No continuous processing needed (unlike chorus LFO).
}

static void fxProcessGate() {
  if (gateHeldCount == 0) return;
  if (fxClockSync && !fxClockActive()) return;  // Sync enabled but no clock — wait

  uint32_t pos;
  uint32_t cycleLen;

  if (fxClockActive()) {
    // Clock-synced: use tick position within subdivision
    uint8_t divIdx = gateRateMs;  // When synced, gateRateMs holds division index (0-7)
    if (divIdx >= FX_CLOCK_DIV_COUNT) divIdx = 4;  // Default to 1/16
    uint8_t ticks = clockDivTicks[divIdx];
    cycleLen = ticks;
    pos = midiClockTick % ticks;
  } else {
    // Free-running ms-based
    uint32_t now = millis();
    cycleLen = gateRateMs;
    pos = now % gateRateMs;
  }

  // Duty: gateDuty out of 8 subdivisions
  uint32_t onTime = (cycleLen * gateDuty) / 8;
  bool inOnPhase = (pos < onTime);

  uint8_t vol = 0;
  if (gatePattern == GATE_RANDOM) {
    // Random: re-evaluate each cycle boundary
    // Use a simple hash of cycle number to get consistent on/off per cycle
    uint32_t cycleNum;
    if (fxClockActive()) {
      uint8_t divIdx = gateRateMs;
      if (divIdx >= FX_CLOCK_DIV_COUNT) divIdx = 4;
      cycleNum = midiClockTick / clockDivTicks[divIdx];
    } else {
      cycleNum = millis() / gateRateMs;
    }
    // Seed shifts the pattern by offsetting the cycle number
    uint32_t seeded = (cycleNum + gateSeed * 37) * 2654435761UL;
    bool gateOn = (seeded & 0xFF) < (gateDuty * 32);
    vol = gateOn ? gateVolume : 0;
  } else if (!inOnPhase) {
    vol = 0;  // Off phase for all non-random patterns
  } else {
    // In the on-phase: shape volume based on pattern
    float phase = (float)pos / (float)onTime;  // 0.0 to 1.0 within on-period
    switch (gatePattern) {
      case GATE_SQUARE:
        vol = gateVolume;
        break;
      case GATE_RAMP:
        // Volume ramps down during on-period
        vol = (uint8_t)(gateVolume * (1.0f - phase));
        if (vol < 1 && phase < 0.9f) vol = 1;
        break;
      case GATE_TRI:
        // Triangle: ramp up first half, ramp down second half
        if (phase < 0.5f) {
          vol = (uint8_t)(gateVolume * (phase * 2.0f));
        } else {
          vol = (uint8_t)(gateVolume * ((1.0f - phase) * 2.0f));
        }
        if (vol < 1 && phase > 0.1f && phase < 0.9f) vol = 1;
        break;
      default:
        vol = gateVolume;
        break;
    }
  }

  // Scale by velocity
  uint8_t scaledVol = (gateActiveVel * vol) / 127;
  if (scaledVol > 15) scaledVol = 15;

  // Write volume to all 3 FX voices (registers 8, 9, 10)
  for (int v = 0; v < 3; v++) {
    ymSafeWrite(FX_CHIP, 8 + v, scaledVol);
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
    case FX_CHORUS:
      fxProcessChorus();
      break;
    case FX_HARMONIZER:
      fxProcessHarmonizer();
      break;
    case FX_GATE:
      fxProcessGate();
      break;
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
    case FX_HARMONIZER: return "HARM";
    case FX_GATE: return "GATE";
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

const char* getHarmChordName(uint8_t chord) {
  switch (chord) {
    case HARM_CHORD_OCTAVE: return "OCT";
    case HARM_CHORD_POWER:  return "PWR";
    case HARM_CHORD_MAJOR:  return "MAJ";
    case HARM_CHORD_MINOR:  return "MIN";
    case HARM_CHORD_SUS2:   return "SU2";
    case HARM_CHORD_SUS4:   return "SU4";
    case HARM_CHORD_AUG:    return "AUG";
    case HARM_CHORD_DIM:    return "DIM";
    case HARM_CHORD_MAJ7:   return "M7";
    case HARM_CHORD_MIN7:   return "m7";
    case HARM_CHORD_DOM7:   return "D7";
    case HARM_CHORD_5OCT:   return "5+O";
    default: return "?";
  }
}

const char* getGatePatternName(uint8_t pattern) {
  switch (pattern) {
    case GATE_SQUARE: return "SQR";
    case GATE_RAMP:   return "RMP";
    case GATE_TRI:    return "TRI";
    case GATE_RANDOM: return "RND";
    default: return "?";
  }
}
