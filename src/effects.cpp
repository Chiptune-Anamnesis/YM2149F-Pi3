#include "effects.h"
#include "voice_manager.h"
#include "sid_mode.h"
#include "settings.h"
#include "YM2149.h"
#include <math.h>

// Run timing-critical functions from RAM to avoid Flash bus contention
#define RAM_FUNC __attribute__((section(".time_critical")))

// External YM2149 instance
extern YM2149 ym;

// ============================================================================
// FAST MATH
// ============================================================================

// Fast approximation of powf(2.0f, semitones / 12.0f)
// Uses precomputed table for integer semitones + linear interpolation
// Accurate to ~0.1% across full range — below YM2149's 12-bit period resolution
static const float semitoneMul[13] = {
  1.0f,        // 0
  1.059463f,   // 1
  1.122462f,   // 2
  1.189207f,   // 3
  1.259921f,   // 4
  1.334840f,   // 5
  1.414214f,   // 6
  1.498307f,   // 7
  1.587401f,   // 8
  1.681793f,   // 9
  1.781797f,   // 10
  1.887749f,   // 11
  2.0f         // 12 (octave)
};

RAM_FUNC float fastPow2Semi(float semitones) {
  // Floor-divide by 12 to get octave count, leaving remainder in [0, 12)
  int oct12 = (int)(semitones / 12.0f);
  if (oct12 * 12.0f > semitones) oct12--;  // Floor for negatives
  float rem = semitones - oct12 * 12.0f;   // Always in [0, 12)

  int idx = (int)rem;
  if (idx > 11) idx = 11;
  float frac = rem - idx;
  float result = semitoneMul[idx] + frac * (semitoneMul[idx + 1] - semitoneMul[idx]);

  // Apply octave scaling
  if (oct12 > 0) {
    for (int i = 0; i < oct12; i++) result *= 2.0f;
  } else if (oct12 < 0) {
    for (int i = 0; i < -oct12; i++) result *= 0.5f;
  }
  return result;
}

// Fast sine approximation for LFOs (vibrato, tremolo)
// Input: phase 0..1 (one full cycle), Output: -1..1
// Parabolic approximation, accurate to ~0.3% — inaudible for LFO use
RAM_FUNC float fastSin01(float phase) {
  // Normalize to 0..1 range
  phase -= (int)phase;
  if (phase < 0) phase += 1.0f;

  // Convert phase (0..1) to x (-1..1) matching sin() shape:
  // phase 0 → x=0 (zero), 0.25 → x=1 (peak), 0.5 → x=0, 0.75 → x=-1 (trough)
  float x;
  if (phase < 0.25f)
    x = phase * 4.0f;
  else if (phase < 0.75f)
    x = 2.0f - phase * 4.0f;
  else
    x = phase * 4.0f - 4.0f;

  // Parabolic approximation of sin(π*x/2)
  return x * (2.0f - (x < 0 ? -x : x)) * 0.225f + x * 0.775f;
}

// ============================================================================
// EFFECTS STATE
// ============================================================================

float modWheel[9] = {0};
float vibPhase[9] = {0};
float vibRate[16] = {
  DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE,
  DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE,
  DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE,
  DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE, DEFAULT_VIB_RATE
};
float vibRangeSemi[16] = {
  DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE,
  DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE,
  DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE,
  DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE, DEFAULT_VIB_RANGE
};
unsigned long vibStartTime[9] = {0};
unsigned long vibLastTime[9] = {0};  // For time-based vibrato rate
uint16_t vibDelayMs[9] = {0};

float pitchBendSemis[9] = {0};
float pitchEnvPhase[9] = {0};
float pitchEnvIncrement[9] = {0};
float pitchEnvAmt[9] = {0};
uint8_t pitchEnvShape[9] = {0};

uint8_t expressionVal[9] = {127,127,127,127,127,127,127,127,127};

bool portamentoOn[9] = {false};
float portamentoSpeed[9] = {0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f};

bool laserMode[9] = {false};
float laserAmt[9] = {1,1,1,1,1,1,1,1,1};
bool laserTriggered[3][3] = {{false}};

uint8_t cc4Shape[9] = {0};
bool volEnvOn[9] = {false};
bool volEnvDir[9] = {true};
float volEnvPhase[9] = {0.0f};
float volEnvIncrement[9] = {0.0f};

// Per-voice tremolo state
float tremoloPhase[9] = {0};
unsigned long tremoloLastTime[9] = {0};

// Per-voice pitch envelope state (for menu-controlled pitch env)
float voicePitchEnvPhase[9] = {0};

// Per-voice ADS envelope state (all 9 voices initialized explicitly)
EnvStage envStage[9] = {ENV_OFF, ENV_OFF, ENV_OFF, ENV_OFF, ENV_OFF, ENV_OFF, ENV_OFF, ENV_OFF, ENV_OFF};
float envLevel[9] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
unsigned long envLastTime[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
unsigned long pitchEnvLastTime[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

// ============================================================================
// FUNCTIONS
// ============================================================================

void updatePitchMod(uint8_t ch) {
  if (ch >= 9) return;
  unsigned long now = millis();

  // Determine which chips to search based on poly mode
  uint8_t startChip, endChip;
  if (sidModeGlobal) {
    // In SID mode, always search chips 1 and 2 (where SID voices are)
    startChip = 1;
    endChip = 3;
  } else if (polyMode == 1 || unisonMode) {
    startChip = 0;
    endChip = 3;
  } else {
    startChip = midiToChip[ch];
    endChip = startChip + 1;
  }

  for (uint8_t chip = startChip; chip < endChip; chip++) {
    for (int v = 0; v < 3; v++) {
      if (!voiceActive[chip][v] || voiceChan[chip][v] != ch) continue;

      uint8_t voiceIdx = chip * 3 + v;  // 0-8 voice index
      // In semi-poly mode, use channel-based settings so voice shaping
      // follows the MIDI channel predictably (like mono mode)
      uint8_t settingsIdx = (polyMode == 0)
          ? chip * 3 + (voiceChan[chip][v] % 3)
          : voiceIdx;

      // Per-voice vibrato LFO
      float lfo = 0;
      // Use vibOn from settings if set, otherwise fall back to mod wheel
      float vibIntensity = voiceSettings[settingsIdx].vibOn > 0
                           ? (float)voiceSettings[settingsIdx].vibOn
                           : modWheel[ch];

      // Use per-voice delay from settings (in 10ms units)
      uint16_t vibDelay = voiceSettings[settingsIdx].vibDelay * 10;
      if (vibIntensity > 0 && now - vibStartTime[voiceIdx] >= vibDelay) {
        float rate = voiceSettings[settingsIdx].vibRateTenths / 10.0f;  // Hz
        float depthSemi = voiceSettings[settingsIdx].vibDepthCents / 100.0f;

        // Time-based phase increment for consistent vibrato rate
        unsigned long elapsed = now - vibLastTime[voiceIdx];
        if (elapsed > 0 && elapsed < 100) {  // Sanity check: 1-100ms
          vibPhase[voiceIdx] += rate * (elapsed / 1000.0f);
        }
        vibLastTime[voiceIdx] = now;

        if (vibPhase[voiceIdx] >= 1.0f) vibPhase[voiceIdx] -= 1.0f;
        lfo = fastSin01(vibPhase[voiceIdx])
            * (vibIntensity / 127.0f)
            * depthSemi;
      }

      // === PORTAMENTO TARGET (note-to-note glide, excludes pitch bend/vibrato/envelopes) ===
      // These are "static" pitch modifiers that portamento should glide between
      float base = noteToPeriod(voiceNote[chip][v]);
      float detuneSemi = voiceDetune[chip][v] / 100.0f;  // Per-voice unison detune
      float settingsDetuneSemi = voiceSettings[settingsIdx].detuneCents / 100.0f;  // Per-voice detune from settings
      float octaveSemi = voiceSettings[settingsIdx].octaveShift * 12.0f;  // Per-voice octave shift
      float tpBase = base / fastPow2Semi(detuneSemi + settingsDetuneSemi + octaveSemi);

      // === PORTAMENTO GLIDE ===
      // One-shot laser
      if (laserTriggered[chip][v]) {
        laserTriggered[chip][v] = false;
      }
      // Portamento slide (per-voice via settings, or per-channel via MIDI CC)
      else if (voiceSettings[settingsIdx].portaOn || portamentoOn[ch]) {
        // Use per-voice speed if portaOn from settings, else use MIDI CC speed
        float speed;
        if (voiceSettings[settingsIdx].portaOn) {
          speed = PORTA_MIN + (PORTA_MAX - PORTA_MIN) * (voiceSettings[settingsIdx].portaSpeed / 127.0f);
        } else {
          speed = portamentoSpeed[ch];
        }
        // If curPeriod is uninitialized (0), snap to target immediately
        if (curPeriod[chip][v] < 1.0f) {
          curPeriod[chip][v] = tpBase;
        } else {
          curPeriod[chip][v] += (tpBase - curPeriod[chip][v]) * speed;
        }

        // When pitch bend is at center (no bend), snap to target to ensure clean return
        // This prevents drift from causing "stuck" pitch bend
        if (fabsf(pitchBendSemis[ch]) < 0.01f && fabsf(curPeriod[chip][v] - tpBase) < tpBase * 0.1f) {
          curPeriod[chip][v] = tpBase;
        }
      }
      // Immediate jump
      else {
        curPeriod[chip][v] = tpBase;
      }

      // Clamp period
      if (curPeriod[chip][v] < 1.0f || curPeriod[chip][v] > 4095.0f) {
        curPeriod[chip][v] = tpBase;
      }

      // === PITCH ENVELOPES (applied after portamento, like pitch bend) ===
      // These sweep over time but shouldn't affect the portamento target
      float envSemi = 0.0f;

      // Pitch envelope (MIDI CC-based, channel-wide)
      if (pitchEnvAmt[ch] > 0) {
        pitchEnvPhase[ch] += pitchEnvIncrement[ch];
        if (pitchEnvPhase[ch] > 1.0f) pitchEnvPhase[ch] = 1.0f;
        float ev = (pitchEnvShape[ch] < 64)
                     ? pitchEnvPhase[ch]
                     : 1.0f - pitchEnvPhase[ch];
        envSemi += pitchEnvAmt[ch] * ev;
      }

      // Per-voice pitch envelope (menu-controlled)
      // Uses separate pitchEnvLastTime to avoid timing conflicts with ADS envelope
      if (voiceSettings[settingsIdx].pitchEnvAmt > 0) {
        // Calculate time-based increment
        float envTime = 10.0f + voiceSettings[settingsIdx].pitchEnvTime * 15.0f;  // 10ms to ~2s
        unsigned long elapsed = now - pitchEnvLastTime[voiceIdx];
        if (elapsed > 0 && elapsed < 100) {
          voicePitchEnvPhase[voiceIdx] += elapsed / envTime;
        }
        pitchEnvLastTime[voiceIdx] = now;  // Update timestamp AFTER using it
        if (voicePitchEnvPhase[voiceIdx] > 1.0f) voicePitchEnvPhase[voiceIdx] = 1.0f;

        // Apply pitch offset based on direction
        float envVal = 1.0f - voicePitchEnvPhase[voiceIdx];  // Starts at 1, goes to 0
        float semitones = voiceSettings[settingsIdx].pitchEnvAmt * envVal;
        if (voiceSettings[settingsIdx].pitchEnvDir == 0) {
          // Down: start high, sweep down to note
          envSemi += semitones;
        } else {
          // Up: start low, sweep up to note
          envSemi -= semitones;
        }
      }

      // === REAL-TIME MODULATION (applied instantly, bypasses portamento) ===
      // Pitch bend, vibrato, and envelopes are expressive controls that should respond immediately
      float realtimeSemi = pitchBendSemis[ch] + lfo + envSemi;
      float finalPeriod = curPeriod[chip][v] / fastPow2Semi(realtimeSemi);

      // Compute volume with expression
      uint16_t outP = uint16_t(finalPeriod + 0.5f);
      uint8_t effectiveExpr = 127 - (uint8_t)((127 - expressionVal[ch]) * EXPRESSION_AMOUNT);
      uint8_t vol = (voiceVol[chip][v] * effectiveExpr + 63) / 127;

      // Apply per-voice envelope level to volume (envelope is updated in updateAllEnvelopes)
      if (envStage[voiceIdx] != ENV_OFF) {
        vol = (uint8_t)(vol * envLevel[voiceIdx] + 0.5f);
      }

      // CC4 volume envelope
#if USE_CC4_ENVELOPE
      if (volEnvOn[ch]) {
        if (volEnvDir[ch]) {
          volEnvPhase[ch] += volEnvIncrement[ch];
          if (volEnvPhase[ch] >= 1.0f) {
            volEnvPhase[ch] = 1.0f;
            volEnvOn[ch] = false;
          }
        } else {
          volEnvPhase[ch] -= volEnvIncrement[ch];
          if (volEnvPhase[ch] <= 0.0f) {
            volEnvPhase[ch] = 0.0f;
            volEnvOn[ch] = false;
          }
        }
        vol = uint8_t(vol * volEnvPhase[ch] + 0.5f);
      }
#endif

      // Per-voice tremolo (volume LFO)
      if (voiceSettings[settingsIdx].tremoloOn > 0) {
        float rate = voiceSettings[settingsIdx].tremoloRate / 10.0f;  // Hz
        float depth = voiceSettings[settingsIdx].tremoloDepth / 100.0f;  // 0-1
        float intensity = voiceSettings[settingsIdx].tremoloOn / 127.0f;

        // Time-based phase increment
        unsigned long elapsed = now - tremoloLastTime[voiceIdx];
        if (elapsed > 0 && elapsed < 100) {
          tremoloPhase[voiceIdx] += rate * (elapsed / 1000.0f);
        }
        tremoloLastTime[voiceIdx] = now;
        if (tremoloPhase[voiceIdx] >= 1.0f) tremoloPhase[voiceIdx] -= 1.0f;

        // Calculate tremolo amount (0.5 + 0.5*sin = 0 to 1)
        float tremLfo = 0.5f + 0.5f * fastSin01(tremoloPhase[voiceIdx]);
        // Scale by depth and intensity: output ranges from (1-depth*intensity) to 1
        float tremMult = 1.0f - (1.0f - tremLfo) * depth * intensity;
        vol = (uint8_t)(vol * tremMult + 0.5f);
      }

      // Apply per-voice max volume cap
      if (vol > voiceSettings[settingsIdx].maxVolume) {
        vol = voiceSettings[settingsIdx].maxVolume;
      }

      setVoice(chip, v, outP, vol);
    }
  }
}

void resetAllControllers(uint8_t ch) {
  if (ch >= 9) return;

  modWheel[ch] = 0;
  pitchBendSemis[ch] = 0;
  pitchEnvAmt[ch] = 0;
  pitchEnvPhase[ch] = 0;
  pitchEnvShape[ch] = 0;
  expressionVal[ch] = 127;
  portamentoOn[ch] = false;
  portamentoSpeed[ch] = 0.05f;
  laserMode[ch] = false;
  laserAmt[ch] = 1.0f;
  cc4Shape[ch] = 0;
  volEnvOn[ch] = false;
  volEnvPhase[ch] = 0;
  sustainOn[ch] = false;
  vibPhase[ch] = 0;
  vibStartTime[ch] = 0;
  vibLastTime[ch] = 0;

  if (ch < 9) {
    if (sidModeGlobal) {
      // In SID mode, clear laser triggers on both SID chips
      for (uint8_t chip = 1; chip < 3; chip++) {
        for (uint8_t v = 0; v < 3; v++) {
          laserTriggered[chip][v] = false;
        }
      }
    } else {
      uint8_t chip = midiToChip[ch];
      for (uint8_t v = 0; v < 3; v++) {
        laserTriggered[chip][v] = false;
      }
    }
  }
}

void triggerVolumeEnvelope(uint8_t ch) {
  if (cc4Shape[ch] == 0) {
    volEnvOn[ch] = false;
  } else if (cc4Shape[ch] < 64) {
    volEnvOn[ch] = true;
    volEnvDir[ch] = true;
    volEnvPhase[ch] = 0;
    uint16_t t = map(cc4Shape[ch], 1, 63, 20, 200);
    volEnvIncrement[ch] = 1.0f / t;
  } else {
    volEnvOn[ch] = true;
    volEnvDir[ch] = false;
    volEnvPhase[ch] = 1.0f;
    uint16_t t = map(cc4Shape[ch], 64, 127, 20, 200);
    volEnvIncrement[ch] = 1.0f / t;
  }
}

void triggerADSEnvelope(uint8_t voiceIdx) {
  if (voiceIdx >= 9) return;

  // In semi-poly mode, use channel-based settings
  uint8_t chip = voiceIdx / 3;
  uint8_t v = voiceIdx % 3;
  uint8_t settingsIdx = (polyMode == 0)
      ? chip * 3 + (voiceChan[chip][v] % 3)
      : voiceIdx;

  // Reset per-voice pitch envelope on note start
  voicePitchEnvPhase[voiceIdx] = 0;
  pitchEnvLastTime[voiceIdx] = millis();  // Initialize timestamp for pitch envelope

  // Only trigger if attack or decay is non-zero (otherwise instant full volume)
  if (voiceSettings[settingsIdx].envAttack > 0 || voiceSettings[settingsIdx].envDecay > 0) {
    // Only reset to 0 for fresh notes (voice was released/off)
    // For retriggers (voice still active), keep current level to avoid click
    if (envStage[voiceIdx] == ENV_OFF) {
      envLevel[voiceIdx] = 0.0f;
    }
    envStage[voiceIdx] = ENV_ATTACK;
    envLastTime[voiceIdx] = millis();
  } else {
    // No envelope - use sustain level directly
    envStage[voiceIdx] = ENV_SUSTAIN;
    envLevel[voiceIdx] = voiceSettings[settingsIdx].envSustain / 127.0f;
  }
}

void releaseADSEnvelope(uint8_t voiceIdx) {
  if (voiceIdx >= 9) return;

  // In semi-poly mode, use channel-based settings
  uint8_t chip = voiceIdx / 3;
  uint8_t v = voiceIdx % 3;
  uint8_t settingsIdx = (polyMode == 0)
      ? chip * 3 + (voiceChan[chip][v] % 3)
      : voiceIdx;

  // If release time is set and envelope is active, enter release stage
  if (voiceSettings[settingsIdx].envRelease > 0 && envStage[voiceIdx] != ENV_OFF) {
    envStage[voiceIdx] = ENV_RELEASE;
    envLastTime[voiceIdx] = millis();
    // envLevel stays at current value — release ramps from here to 0
  } else {
    envStage[voiceIdx] = ENV_OFF;
    envLevel[voiceIdx] = 1.0f;
  }
}

void effectsInit() {
  for (uint8_t ch = 0; ch < 9; ch++) {
    resetAllControllers(ch);
  }
  // Initialize per-voice envelope state
  for (uint8_t v = 0; v < 9; v++) {
    envStage[v] = ENV_OFF;
    envLevel[v] = 1.0f;
    envLastTime[v] = 0;
    pitchEnvLastTime[v] = 0;
    vibLastTime[v] = 0;
    tremoloPhase[v] = 0;
    tremoloLastTime[v] = 0;
    voicePitchEnvPhase[v] = 0;
  }
}

// Process all voice envelopes independently (called from main loop)
void updateAllEnvelopes() {
  unsigned long now = millis();

  for (uint8_t voiceIdx = 0; voiceIdx < 9; voiceIdx++) {
    uint8_t chip = voiceIdx / 3;
    uint8_t v = voiceIdx % 3;

    // Skip inactive voices
    if (!voiceActive[chip][v]) continue;

    // Process envelope for this voice
    if (envStage[voiceIdx] != ENV_OFF) {
      // In semi-poly mode, use channel-based settings
      uint8_t settingsIdx = (polyMode == 0)
          ? chip * 3 + (voiceChan[chip][v] % 3)
          : voiceIdx;
      uint8_t attack = voiceSettings[settingsIdx].envAttack;
      uint8_t decay = voiceSettings[settingsIdx].envDecay;
      uint8_t sustain = voiceSettings[settingsIdx].envSustain;

      unsigned long elapsed = now - envLastTime[voiceIdx];
      envLastTime[voiceIdx] = now;

      if (envStage[voiceIdx] == ENV_ATTACK) {
        float attackTime = 1.0f + (attack * 15.6f);
        float attackInc = elapsed / attackTime;
        envLevel[voiceIdx] += attackInc;
        if (envLevel[voiceIdx] >= 1.0f) {
          envLevel[voiceIdx] = 1.0f;
          envStage[voiceIdx] = (decay > 0) ? ENV_DECAY : ENV_SUSTAIN;
        }
      }
      else if (envStage[voiceIdx] == ENV_DECAY) {
        float sustainLevel = sustain / 127.0f;
        float decayTime = 1.0f + (decay * 15.6f);
        float decayInc = elapsed / decayTime;
        envLevel[voiceIdx] -= decayInc;
        if (envLevel[voiceIdx] <= sustainLevel) {
          envLevel[voiceIdx] = sustainLevel;
          envStage[voiceIdx] = ENV_SUSTAIN;
        }
      }
      // ENV_SUSTAIN: hold at current level
      else if (envStage[voiceIdx] == ENV_RELEASE) {
        uint8_t release = voiceSettings[settingsIdx].envRelease;
        float releaseTime = 1.0f + (release * 15.6f);
        float releaseInc = elapsed / releaseTime;
        envLevel[voiceIdx] -= releaseInc;
        if (envLevel[voiceIdx] <= 0.0f) {
          envLevel[voiceIdx] = 0.0f;
          envStage[voiceIdx] = ENV_OFF;
          // Stop the voice now that release is complete
          voiceActive[chip][v] = false;
          stopVoice(chip, v);
        }
      }
    }
  }
}
