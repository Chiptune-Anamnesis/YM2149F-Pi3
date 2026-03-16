#include "voice_manager.h"
#include "sid_mode.h"
#include "effects.h"
#include "settings.h"
#include "fx_chip.h"
#include "YM2149.h"
#include <math.h>
#include <hardware/sync.h>  // For save_and_disable_interrupts()

// Run timing-critical functions from RAM to avoid Flash bus contention
#define RAM_FUNC __attribute__((section(".time_critical")))

// External YM2149 instance
extern YM2149 ym;

// ============================================================================
// VOICE STATE
// ============================================================================

uint8_t polyMode = 1;  // Default: full poly
bool unisonMode = false;
float unisonDetuneCents = 12.0f;

volatile bool voiceActive[3][3] = {{false}};
uint8_t voiceNote[3][3] = {{0}};
uint8_t voiceChan[3][3] = {{0}};
volatile uint8_t voiceVol[3][3] = {{0}};
float voiceDetune[3][3] = {{0}};
uint8_t nextVoice[3] = {0};
volatile float curPeriod[3][3] = {{0}};

bool sustainOn[9] = {false};
bool pendingRelease[3][3] = {{false}};

// Last played period per channel (for portamento to glide FROM)
float lastPortaPeriod[9] = {0};

// SID voice tracking (for global SID mode: 6 voices on chips 1+2)
uint8_t sidVoiceNote[6] = {0};  // Externally visible for visualization
static uint8_t sidVoiceChan[6] = {0};
static uint8_t nextSidVoice = 0;

unsigned long ledOnTime[2] = {0};

// ============================================================================
// SID MODE NOTE HANDLING (6 voices on chips 1+2)
// ============================================================================

static void noteOnSid(uint8_t ch, uint8_t note, uint8_t vel) {
  int8_t targetVoice = -1;

  // MONO MODE: Direct MIDI channel → voice mapping
  if (polyMode == 2) {
    // Ch 0-5 → voices 0-5
    if (ch < 6) {
      targetVoice = ch;
    } else {
      return;  // Ignore channels beyond 6 in mono mode
    }
  }
  // POLYPHONY MODE: Smart allocation (not round-robin)
  else {
    // First, check for re-trigger of same note
    for (uint8_t i = 0; i < 6; i++) {
      if (sidState[i].active && sidVoiceNote[i] == note && sidVoiceChan[i] == ch) {
        targetVoice = i;
        break;
      }
    }

    // If not a re-trigger, find first free voice
    if (targetVoice < 0) {
      for (uint8_t i = 0; i < 6; i++) {
        if (!sidState[i].active) {
          targetVoice = i;
          break;
        }
      }
    }

    // If all voices busy, steal oldest
    if (targetVoice < 0) {
      targetVoice = nextSidVoice;
      // Clean stop before stealing to prevent click
      sidVoiceStopGlobal(targetVoice);
      nextSidVoice = (nextSidVoice + 1) % 6;
    }
  }

  // Map SID voice index to chip and voice (for voice state arrays)
  // sidIdx 0-2 → chip 1, voices 0-2
  // sidIdx 3-5 → chip 2, voices 0-2
  uint8_t chip = (targetVoice < 3) ? 1 : 2;
  uint8_t v = targetVoice % 3;
  uint8_t voiceIdx = chip * 3 + v;  // Global voice index (3-8 for chips 1-2)

  // Calculate period and volume
  float freq = 440.0f * powf(2.0f, ((int)note - 69) / 12.0f);
  uint16_t period = (uint16_t)(YM2149Class::YM_CLOCK_HZ / (16.0f * freq) + 0.5f);
  // Use same velocity curve as YM mode for consistent volume levels
  float velNorm = vel / 127.0f;
  float velCurve = powf(velNorm, VELOCITY_GAMMA);
  float range = VELOCITY_MAX - VELOCITY_MIN;
  uint8_t volume = uint8_t(VELOCITY_MIN + velCurve * range + 0.5f);

  // Set up voice state arrays (needed for effects processing)
  voiceActive[chip][v] = true;
  voiceNote[chip][v] = note;
  voiceChan[chip][v] = ch;
  voiceVol[chip][v] = volume;

  // Portamento-aware curPeriod initialization
  uint8_t settingsIdx = chip * 3 + v;
  float detuneSemi = voiceSettings[settingsIdx].detuneCents / 100.0f;
  float octaveSemi = voiceSettings[settingsIdx].octaveShift * 12.0f;
  float modifiedP = (float)period / fastPow2Semi(detuneSemi + octaveSemi);

  if (portamentoOn[ch] || voiceSettings[settingsIdx].portaOn) {
    if (lastPortaPeriod[ch] > 0) {
      curPeriod[chip][v] = lastPortaPeriod[ch];
    } else {
      curPeriod[chip][v] = modifiedP;
    }
  } else {
    curPeriod[chip][v] = (float)period;
  }

  // Initialize effect state for this voice
  vibPhase[voiceIdx] = 0;
  vibStartTime[voiceIdx] = millis();
  vibLastTime[voiceIdx] = millis();
  tremoloPhase[voiceIdx] = 0;
  tremoloLastTime[voiceIdx] = millis();
  voicePitchEnvPhase[voiceIdx] = 0;
  pitchEnvLastTime[voiceIdx] = millis();

  // Trigger envelope
  triggerADSEnvelope(voiceIdx);

  // Check if this is a retrigger (voice already active with PWM running)
  if (sidState[targetVoice].active) {
    // Retrigger: just update period, keep PWM phase continuous (no click)
    updateSidTimingGlobal(targetVoice, period);
  } else {
    // Fresh note: full SID voice initialization
    sidVoiceStartGlobal(targetVoice, period, volume);
  }

  // Apply envelope scaling immediately (prevents timer writing unscaled volume)
  updatePitchMod(ch);

  // Remember this note's period for next portamento glide
  lastPortaPeriod[ch] = modifiedP;

  // Track note for note-off matching
  sidVoiceNote[targetVoice] = note;
  sidVoiceChan[targetVoice] = ch;
}

static void noteOffSid(uint8_t ch, uint8_t note) {
  for (uint8_t i = 0; i < 6; i++) {
    if (sidState[i].active && sidVoiceNote[i] == note && sidVoiceChan[i] == ch) {
      // Map SID voice index to chip and voice
      uint8_t chip = (i < 3) ? 1 : 2;
      uint8_t v = i % 3;
      uint8_t voiceIdx = chip * 3 + v;

      // Check if sustain pedal is down
      if (sustainOn[ch]) {
        // Mark for release when sustain is lifted
        pendingRelease[chip][v] = true;
      } else {
        releaseADSEnvelope(voiceIdx);
        // If envelope has release time, keep voice active — updateAllEnvelopes will stop it
        if (envStage[voiceIdx] != ENV_RELEASE) {
          voiceActive[chip][v] = false;
          sidVoiceStopGlobal(i);
        }
      }
      break;
    }
  }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Precomputed MIDI note → YM period lookup table (populated at init)
static uint16_t notePeriodLUT[128];

void initNotePeriodLUT() {
  for (int n = 0; n < 128; n++) {
    float freq = 440.0f * powf(2.0f, (n - 69) / 12.0f);
    notePeriodLUT[n] = (uint16_t)(YM2149Class::YM_CLOCK_HZ / (16.0f * freq) + 0.5f);
  }
}

RAM_FUNC uint16_t noteToPeriod(uint8_t note) {
  if (note > 127) note = 127;
  return notePeriodLUT[note];
}

// Helper: get allowed voices mask for a chip
// Returns bitmask of voices that can be used for polyphony allocation
// When voice linking is active, disabled voices are excluded from allocation
static uint8_t getAllowedVoicesMask(uint8_t chip) {
  // If no voice linking active on Chip 0, all voices available everywhere
  if (voiceLinkMask == 0) {
    return 0x07;  // All voices
  }

  // Chip 0: only linked voices are allowed
  if (chip == 0) {
    return voiceLinkMask;
  }

  // Chip 1: if linked to Chip 0, use same mask
  if (chip == 1 && (linkMode == LINK_CH1 || linkMode == LINK_ALL)) {
    return voiceLinkMask;
  }

  // Chip 2: if linked to Chip 0, use same mask
  if (chip == 2 && (linkMode == LINK_CH2 || linkMode == LINK_ALL)) {
    return voiceLinkMask;
  }

  // Unlinked chips: all voices available
  return 0x07;
}

RAM_FUNC void ymSafeWrite(uint8_t chip, uint8_t reg, uint8_t val) {
  uint32_t irq = save_and_disable_interrupts();
  ymBusBusy = true;
  restore_interrupts(irq);
  ym.write(chip, reg, val);
  ymBusBusy = false;
}

RAM_FUNC void setVoice(uint8_t chip, uint8_t v, uint16_t per, uint8_t vol) {
  // In semi-poly mode, use channel-based settings so voice shaping
  // follows the MIDI channel predictably (like mono mode)
  uint8_t settingsIdx = (polyMode == 0)
      ? chip * 3 + (voiceChan[chip][v] % 3)
      : chip * 3 + v;

  // Apply per-voice max volume cap
  if (vol > voiceSettings[settingsIdx].maxVolume) {
    vol = voiceSettings[settingsIdx].maxVolume;
  }

  // Global SID mode: chips 1 and 2 are controlled by SID timer
  if (sidModeGlobal && (chip == 1 || chip == 2)) {
    uint8_t sidIdx = (chip - 1) * 3 + v;
    if (sidState[sidIdx].active) {
      sidState[sidIdx].volume = vol & 0x0F;
      updateSidTimingGlobal(sidIdx, per);
    }
    return;
  }

  // Normal mode: write period and volume directly
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.write(chip, v*2, per & 0xFF);
  ym.write(chip, v*2 + 1, (per >> 8) & 0x0F);
  ym.write(chip, 8 + v, vol & 0x0F);
  ymBusBusy = false;
}

RAM_FUNC void stopVoice(uint8_t chip, uint8_t v) {
  // Global SID mode: chips 1 and 2 use SID voices
  if (sidModeGlobal && (chip == 1 || chip == 2)) {
    uint8_t sidIdx = (chip - 1) * 3 + v;
    sidVoiceStopGlobal(sidIdx);
    return;
  }

  // Normal mode: stop voice by zeroing period and volume
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.write(chip, v*2, 0);
  ym.write(chip, v*2 + 1, 0);
  ym.write(chip, 8 + v, 0);
  ymBusBusy = false;

  // Clear legacy SID state (for compatibility)
  sidVoiceOn[chip][v] = false;
  sidPhase[chip][v] = 0;
  sidPhaseInc[chip][v] = 0;
  sidPeriod[chip][v] = 0;
  sidVoiceVol[chip][v] = 0;
}

void enableTones(uint8_t chip) {
  ymSafeWrite(chip, 7, 0b00111000);
}

// ============================================================================
// ALL NOTES OFF
// ============================================================================

void allNotesOffChannel(uint8_t ch) {
  if (ch >= 9) return;

  // In global SID mode, use direct SID voice search
  if (sidModeGlobal) {
    for (uint8_t i = 0; i < 6; i++) {
      if (sidState[i].active && sidVoiceChan[i] == ch) {
        uint8_t chip = (i < 3) ? 1 : 2;
        uint8_t v = i % 3;
        voiceActive[chip][v] = false;
        pendingRelease[chip][v] = false;
        laserTriggered[chip][v] = false;
        sidVoiceStopGlobal(i);
      }
    }
    return;
  }

  // Normal YM mode: chip-based search
  uint8_t startChip, endChip;
  if (polyMode == 1 || unisonMode) {
    startChip = 0;
    endChip = 3;
  } else {
    startChip = midiToChip[ch];
    endChip = startChip + 1;
  }

  for (uint8_t chip = startChip; chip < endChip; chip++) {
    for (uint8_t v = 0; v < 3; v++) {
      if (voiceActive[chip][v] && voiceChan[chip][v] == ch) {
        voiceActive[chip][v] = false;
        pendingRelease[chip][v] = false;
        laserTriggered[chip][v] = false;
        stopVoice(chip, v);
      }
    }
  }
}

void allNotesOffPanic() {
  // Stop all YM voices
  for (uint8_t c = 0; c < 3; c++) {
    for (uint8_t v = 0; v < 3; v++) {
      voiceActive[c][v] = false;
      pendingRelease[c][v] = false;
      laserTriggered[c][v] = false;
      stopVoice(c, v);
    }
  }

  // In global SID mode, also stop all SID voices
  if (sidModeGlobal) {
    for (uint8_t i = 0; i < 6; i++) {
      sidVoiceStopGlobal(i);
    }
  }
}

// ============================================================================
// NOTE ON
// ============================================================================

void noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
#if ENABLE_NOISE_CHANNEL
  if (ch == 9) {
    noiseOn(note, vel);
    return;
  }
#endif

  if (ch >= 9) return;

  // Global SID mode: route to 6-voice SID polyphony on chips 1+2
  if (sidModeGlobal) {
    noteOnSid(ch, note, vel);
    return;
  }

  if (polyMode == 1 && !unisonMode && ch != 0) return;

  note += OCTAVE_SHIFT;
  while (note < MIDI_NOTE_MIN) note += 12;
  while (note > MIDI_NOTE_MAX) note -= 12;
  while (note <= MIDI_NOTE_MAX - 12 && noteToPeriod(note) > 4095) {
    note += 12;
  }

  vibStartTime[ch] = millis();
  vibPhase[ch] = 0;

  // Calculate velocity
  float velNorm = vel / 127.0f;
  float velCurve = powf(velNorm, VELOCITY_GAMMA);
  float range = VELOCITY_MAX - VELOCITY_MIN;
  uint8_t voiceVolume = uint8_t(VELOCITY_MIN + velCurve * range + 0.5f);

  // --- UNISON MODE ---
  if (unisonMode) {
    uint8_t foundChips[3] = {0xFF, 0xFF, 0xFF};
    uint8_t foundVoices[3] = {0xFF, 0xFF, 0xFF};
    uint8_t foundCount = 0;

    // Check for re-trigger
    for (uint8_t c = 0; c < 3 && foundCount < 3; c++) {
      for (uint8_t voice = 0; voice < 3 && foundCount < 3; voice++) {
        if (voiceActive[c][voice] && voiceChan[c][voice] == ch && voiceNote[c][voice] == note) {
          foundChips[foundCount] = c;
          foundVoices[foundCount] = voice;
          foundCount++;
        }
      }
    }

    // Find free voices
    if (foundCount == 0) {
      for (uint8_t c = 0; c < 3 && foundCount < 3; c++) {
        for (uint8_t voice = 0; voice < 3 && foundCount < 3; voice++) {
          if (!voiceActive[c][voice]) {
            foundChips[foundCount] = c;
            foundVoices[foundCount] = voice;
            foundCount++;
          }
        }
      }

      // Steal if needed
      static uint8_t globalStealVoice = 0;
      while (foundCount < 3) {
        uint8_t c = globalStealVoice / 3;
        uint8_t voice = globalStealVoice % 3;
        globalStealVoice = (globalStealVoice + 1) % 9;

        bool alreadyFound = false;
        for (uint8_t i = 0; i < foundCount; i++) {
          if (foundChips[i] == c && foundVoices[i] == voice) {
            alreadyFound = true;
            break;
          }
        }
        if (!alreadyFound) {
          foundChips[foundCount] = c;
          foundVoices[foundCount] = voice;
          foundCount++;
        }
      }
    }

    float detuneOffsets[3] = {0.0f, unisonDetuneCents, -unisonDetuneCents};

    for (uint8_t i = 0; i < foundCount; i++) {
      uint8_t c = foundChips[i];
      uint8_t voice = foundVoices[i];

      voiceActive[c][voice] = true;
      voiceNote[c][voice] = note;
      voiceChan[c][voice] = ch;
      voiceVol[c][voice] = voiceVolume;
      voiceDetune[c][voice] = detuneOffsets[i];

      float targetP = noteToPeriod(note);
      uint8_t voiceIdx = c * 3 + voice;

      // Calculate modified period with ALL pitch modifiers (matches tpBase in effects.cpp)
      // Must include: unison spread (voiceDetune) + settings detune + octave shift
      float unisonDetuneSemi = detuneOffsets[i] / 100.0f;  // Per-voice unison spread
      float settingsDetuneSemi = voiceSettings[voiceIdx].detuneCents / 100.0f;
      float octaveSemi = voiceSettings[voiceIdx].octaveShift * 12.0f;
      float modifiedP = targetP / fastPow2Semi(unisonDetuneSemi + settingsDetuneSemi + octaveSemi);

      if (laserMode[ch] && laserAmt[ch] > 0.01f) {
        curPeriod[c][voice] = modifiedP * (1.0f + laserAmt[ch] * 10.0f);
        laserTriggered[c][voice] = true;
      } else if (portamentoOn[ch] || voiceSettings[voiceIdx].portaOn) {
        // Portamento: start from last played note's period (not this voice's period)
        if (lastPortaPeriod[ch] > 0) {
          curPeriod[c][voice] = lastPortaPeriod[ch];
        } else {
          // First note on this channel - initialize to modified target (no previous note to glide from)
          curPeriod[c][voice] = modifiedP;
        }
      } else {
        curPeriod[c][voice] = modifiedP;
      }

      if (c < 2) {
        ym.setLED(c, false);
        ledOnTime[c] = millis();
      }

      // Trigger ADS envelope for this voice (voiceIdx already calculated above)
      triggerADSEnvelope(voiceIdx);
    }

    // Remember this note's modified period for next portamento glide
    // Use first voice's settings as reference (all unison voices typically have same settings)
    if (foundCount > 0) {
      uint8_t refVoiceIdx = foundChips[0] * 3 + foundVoices[0];
      float refDetune = voiceSettings[refVoiceIdx].detuneCents / 100.0f;
      float refOctave = voiceSettings[refVoiceIdx].octaveShift * 12.0f;
      lastPortaPeriod[ch] = noteToPeriod(note) / fastPow2Semi(refDetune + refOctave);
    } else {
      lastPortaPeriod[ch] = noteToPeriod(note);
    }

    pitchEnvPhase[ch] = 0;
    triggerVolumeEnvelope(ch);
    updatePitchMod(ch);
    return;
  }

  // --- NORMAL VOICE ALLOCATION ---
  uint8_t chip;
  uint8_t v;

  if (polyMode == 2) {
    // MONO MODE
    chip = midiToChip[ch];
    v = ch % 3;
    // Check if this voice is allowed
    uint8_t allowedMask = getAllowedVoicesMask(chip);
    if (!(allowedMask & (1 << v))) return;  // Voice disabled, ignore note
  }
  else if (polyMode == 1) {
    // FULL POLY
    chip = 0xFF;
    v = 0xFF;

    // When FX mode is enabled, Chip 2 is reserved for effects
    uint8_t maxChips = fxModeEnabled ? 2 : 3;
    uint8_t maxVoices = fxModeEnabled ? 6 : 9;

    for (uint8_t c = 0; c < maxChips; c++) {
      for (uint8_t voice = 0; voice < 3; voice++) {
        if (voiceActive[c][voice] && voiceChan[c][voice] == ch && voiceNote[c][voice] == note) {
          chip = c;
          v = voice;
          goto voice_found;
        }
      }
    }

    for (uint8_t c = 0; c < maxChips; c++) {
      uint8_t allowedMask = getAllowedVoicesMask(c);
      for (uint8_t voice = 0; voice < 3; voice++) {
        if (!(allowedMask & (1 << voice))) continue;  // Skip disabled voices
        if (!voiceActive[c][voice]) {
          chip = c;
          v = voice;
          goto voice_found;
        }
      }
    }

    if (chip == 0xFF) {
      // Voice stealing: find an allowed voice to steal (only in available chips)
      static uint8_t globalNextVoice = 0;
      uint8_t startVoice = globalNextVoice;
      do {
        uint8_t c = globalNextVoice / 3;
        uint8_t voice = globalNextVoice % 3;
        globalNextVoice = (globalNextVoice + 1) % maxVoices;
        if (c < maxChips) {
          uint8_t allowedMask = getAllowedVoicesMask(c);
          if (allowedMask & (1 << voice)) {
            chip = c;
            v = voice;
            break;
          }
        }
      } while (globalNextVoice != startVoice);
    }
    if (chip == 0xFF) return;  // No voice available (all masked off)
  voice_found:;
  }
  else {
    // SEMI-POLY
    chip = midiToChip[ch];
    uint8_t allowedMask = getAllowedVoicesMask(chip);
    uint8_t startV;  // Declared here to avoid goto crossing initialization

    // Check for re-trigger of existing note
    for (v = 0; v < 3; v++) {
      if (!(allowedMask & (1 << v))) continue;  // Skip disabled voices
      if (voiceActive[chip][v] && voiceChan[chip][v] == ch && voiceNote[chip][v] == note) {
        goto voice_assigned;
      }
    }

    // Find free voice among allowed voices
    for (v = 0; v < 3; v++) {
      if (!(allowedMask & (1 << v))) continue;  // Skip disabled voices
      if (!voiceActive[chip][v]) goto voice_assigned;
    }

    // Voice stealing: round-robin among allowed voices
    startV = nextVoice[chip];
    v = startV;
    do {
      if (allowedMask & (1 << v)) {
        nextVoice[chip] = (v + 1) % 3;
        goto voice_assigned;
      }
      v = (v + 1) % 3;
    } while (v != startV);

    // Fallback: use first allowed voice
    for (v = 0; v < 3; v++) {
      if (allowedMask & (1 << v)) break;
    }
  voice_assigned:;
  }

  // Determine which voices to activate
  // voiceLinkMask controls intra-chip linking: if triggered voice is in mask, all masked voices play
  // chipLink only controls polyphony allocation (which chips are available), not voice activation
  uint8_t activateMask = (1 << v);  // Default: just the triggered voice

  if (voiceLinkMask & (1 << v)) {
    // Triggered voice is in link mask - activate all linked voices
    activateMask = voiceLinkMask;
  }

  float targetP = noteToPeriod(note);

  for (uint8_t voice = 0; voice < 3; voice++) {
    if (!(activateMask & (1 << voice))) continue;

    voiceActive[chip][voice] = true;
    voiceNote[chip][voice] = note;
    voiceChan[chip][voice] = ch;
    voiceVol[chip][voice] = voiceVolume;
    voiceDetune[chip][voice] = 0;

    uint8_t voiceIdx = chip * 3 + voice;
    // In semi-poly mode, use channel-based settings so voice shaping
    // follows the MIDI channel predictably (like mono mode)
    uint8_t settingsIdx = (polyMode == 0)
        ? chip * 3 + (ch % 3)
        : voiceIdx;

    // Calculate modified period with voice settings (matches tpBase in effects.cpp)
    float detuneSemi = voiceSettings[settingsIdx].detuneCents / 100.0f;
    float octaveSemi = voiceSettings[settingsIdx].octaveShift * 12.0f;
    float modifiedP = targetP / fastPow2Semi(detuneSemi + octaveSemi);

    if (laserMode[ch] && laserAmt[ch] > 0.01f) {
      curPeriod[chip][voice] = modifiedP * (1.0f + laserAmt[ch] * 10.0f);
      laserTriggered[chip][voice] = true;
    } else if (portamentoOn[ch] || voiceSettings[settingsIdx].portaOn) {
      // Portamento: start from last played note's period (not this voice's period)
      if (lastPortaPeriod[ch] > 0) {
        curPeriod[chip][voice] = lastPortaPeriod[ch];
      } else {
        // First note on this channel - initialize to modified target (no previous note to glide from)
        curPeriod[chip][voice] = modifiedP;
      }
    } else {
      curPeriod[chip][voice] = modifiedP;
    }

    // Trigger ADS envelope for this voice
    triggerADSEnvelope(voiceIdx);
  }

  // Remember this note's modified period for next portamento glide
  // Use the channel's settings as reference
  uint8_t primarySettingsIdx = (polyMode == 0) ? chip * 3 + (ch % 3) : chip * 3 + v;
  float primaryDetune = voiceSettings[primarySettingsIdx].detuneCents / 100.0f;
  float primaryOctave = voiceSettings[primarySettingsIdx].octaveShift * 12.0f;
  lastPortaPeriod[ch] = targetP / fastPow2Semi(primaryDetune + primaryOctave);

  pitchEnvPhase[ch] = 0;
  triggerVolumeEnvelope(ch);
  updatePitchMod(ch);

  if (chip < 2) {
    ym.setLED(chip, false);
    ledOnTime[chip] = millis();
  }

  // Notify FX module of note on (for echo/arp tracking)
  if (fxModeEnabled && chip < 2) {
    fxNoteOn(note, vel, chip, v);
  }
}

// ============================================================================
// NOTE OFF
// ============================================================================

void noteOff(uint8_t ch, uint8_t note) {
#if ENABLE_NOISE_CHANNEL
  if (ch == 9) {
    noiseOff();
    return;
  }
#endif

  if (ch >= 9) return;

  // Global SID mode: route to SID note off
  if (sidModeGlobal) {
    noteOffSid(ch, note);
    return;
  }

  if (polyMode == 1 && !unisonMode && ch != 0) return;

  note += OCTAVE_SHIFT;
  while (note < MIDI_NOTE_MIN) note += 12;
  while (note > MIDI_NOTE_MAX) note -= 12;
  while (note <= MIDI_NOTE_MAX - 12 && noteToPeriod(note) > 4095) {
    note += 12;
  }

  // Notify FX module of note off BEFORE sustain check
  // For arp, we want to track physically held keys, not sustained notes
  if (fxModeEnabled) {
    fxNoteOff(note);
  }

  // Sustain pedal logic
  if (sustainOn[ch]) {
    if (polyMode == 2 && !unisonMode) {
      uint8_t chip = midiToChip[ch];
      uint8_t v = ch % 3;

      // Determine which voices to mark for pending release
      uint8_t releaseMask = (1 << v);
      if (voiceLinkMask & (1 << v)) {
        releaseMask = voiceLinkMask;  // Intra-chip linking
      }

      for (uint8_t voice = 0; voice < 3; voice++) {
        if (!(releaseMask & (1 << voice))) continue;
        if (voiceActive[chip][voice] && voiceChan[chip][voice] == ch && voiceNote[chip][voice] == note) {
          pendingRelease[chip][voice] = true;
        }
      }
    }
    else if (polyMode == 1 || unisonMode) {
      for (uint8_t c = 0; c < 3; c++) {
        for (uint8_t v = 0; v < 3; v++) {
          if (voiceActive[c][v] && voiceChan[c][v] == ch && voiceNote[c][v] == note) {
            pendingRelease[c][v] = true;
            // Chip 0 intra-chip linking
            if (c == 0 && (voiceLinkMask & (1 << v))) {
              for (uint8_t lv = 0; lv < 3; lv++) {
                if (voiceLinkMask & (1 << lv)) pendingRelease[0][lv] = true;
              }
            }
          }
        }
      }
    } else {
      uint8_t chip = midiToChip[ch];
      for (uint8_t v = 0; v < 3; v++) {
        if (voiceActive[chip][v] && voiceChan[chip][v] == ch && voiceNote[chip][v] == note) {
          pendingRelease[chip][v] = true;
          // Chip 0 intra-chip linking
          if (chip == 0 && (voiceLinkMask & (1 << v))) {
            for (uint8_t lv = 0; lv < 3; lv++) {
              if (voiceLinkMask & (1 << lv)) pendingRelease[0][lv] = true;
            }
          }
        }
      }
    }
    return;
  }

  // Immediate note-off
  if (polyMode == 2 && !unisonMode) {
    uint8_t chip = midiToChip[ch];
    uint8_t v = ch % 3;

    // Determine which voices to release
    uint8_t releaseMask = (1 << v);  // Default: just the triggered voice

    if (voiceLinkMask & (1 << v)) {
      // Intra-chip linking - release all linked voices
      releaseMask = voiceLinkMask;
    }

    for (uint8_t voice = 0; voice < 3; voice++) {
      if (!(releaseMask & (1 << voice))) continue;
      if (voiceActive[chip][voice] && voiceChan[chip][voice] == ch && voiceNote[chip][voice] == note) {
        uint8_t vi = chip * 3 + voice;
        releaseADSEnvelope(vi);
        pendingRelease[chip][voice] = false;
        if (envStage[vi] != ENV_RELEASE) {
          voiceActive[chip][voice] = false;
          stopVoice(chip, voice);
        }
      }
    }
  }
  else if (polyMode == 1 || unisonMode) {
    for (uint8_t c = 0; c < 3; c++) {
      for (uint8_t v = 0; v < 3; v++) {
        if (voiceActive[c][v] && voiceChan[c][v] == ch && voiceNote[c][v] == note) {
          uint8_t vi = c * 3 + v;
          releaseADSEnvelope(vi);
          pendingRelease[c][v] = false;
          if (envStage[vi] != ENV_RELEASE) {
            voiceActive[c][v] = false;
            stopVoice(c, v);
          }
        }
      }
    }
  } else {
    uint8_t chip = midiToChip[ch];
    for (uint8_t v = 0; v < 3; v++) {
      if (voiceActive[chip][v] && voiceChan[chip][v] == ch && voiceNote[chip][v] == note) {
        uint8_t vi = chip * 3 + v;
        releaseADSEnvelope(vi);
        pendingRelease[chip][v] = false;
        if (envStage[vi] != ENV_RELEASE) {
          voiceActive[chip][v] = false;
          stopVoice(chip, v);
        }

        // --- CHIP 0 INTRA-CHIP LINKING RELEASE ---
        if (chip == 0 && (voiceLinkMask & (1 << v))) {
          for (uint8_t lv = 0; lv < 3; lv++) {
            if (lv == v) continue;
            if (voiceLinkMask & (1 << lv)) {
              releaseADSEnvelope(lv);
              pendingRelease[0][lv] = false;
              if (envStage[lv] != ENV_RELEASE) {
                voiceActive[0][lv] = false;
                stopVoice(0, lv);
              }
            }
          }
        }
      }
    }
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void voiceManagerInit() {
  for (uint8_t c = 0; c < 3; c++) {
    for (uint8_t v = 0; v < 3; v++) {
      voiceActive[c][v] = false;
      voiceNote[c][v] = 0;
      voiceChan[c][v] = 0;
      voiceVol[c][v] = 0;
      voiceDetune[c][v] = 0;
      curPeriod[c][v] = 0;
      pendingRelease[c][v] = false;
    }
    nextVoice[c] = 0;
  }
  for (uint8_t ch = 0; ch < 9; ch++) {
    sustainOn[ch] = false;
    lastPortaPeriod[ch] = 0;
  }
}
