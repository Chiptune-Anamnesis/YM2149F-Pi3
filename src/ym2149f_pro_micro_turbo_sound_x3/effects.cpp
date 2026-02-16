#include "effects.h"
#include "voice_manager.h"
#include "sid_mode.h"
#include "settings.h"
#include "YM2149.h"
#include <math.h>

// External YM2149 instance
extern YM2149 ym;

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
  if (polyMode == 1 || unisonMode) {
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

      // Per-voice vibrato LFO
      float lfo = 0;
      // Use vibOn from settings if set, otherwise fall back to mod wheel
      float vibIntensity = voiceSettings[voiceIdx].vibOn > 0
                           ? (float)voiceSettings[voiceIdx].vibOn
                           : modWheel[ch];

      // Use per-voice delay from settings (in 10ms units)
      uint16_t vibDelay = voiceSettings[voiceIdx].vibDelay * 10;
      if (vibIntensity > 0 && now - vibStartTime[voiceIdx] >= vibDelay) {
        float rate = voiceSettings[voiceIdx].vibRateTenths / 10.0f;  // Hz
        float depthSemi = voiceSettings[voiceIdx].vibDepthCents / 100.0f;

        // Time-based phase increment for consistent vibrato rate
        unsigned long elapsed = now - vibLastTime[voiceIdx];
        if (elapsed > 0 && elapsed < 100) {  // Sanity check: 1-100ms
          vibPhase[voiceIdx] += rate * (elapsed / 1000.0f);
        }
        vibLastTime[voiceIdx] = now;

        if (vibPhase[voiceIdx] >= 1.0f) vibPhase[voiceIdx] -= 1.0f;
        lfo = sinf(vibPhase[voiceIdx] * 2 * PI)
            * (vibIntensity / 127.0f)
            * depthSemi;
      }

      // === PORTAMENTO TARGET (note-to-note glide, excludes pitch bend/vibrato/envelopes) ===
      // These are "static" pitch modifiers that portamento should glide between
      float base = noteToPeriod(voiceNote[chip][v]);
      float detuneSemi = voiceDetune[chip][v] / 100.0f;  // Per-voice unison detune
      float settingsDetuneSemi = voiceSettings[voiceIdx].detuneCents / 100.0f;  // Per-voice detune from settings
      float octaveSemi = voiceSettings[voiceIdx].octaveShift * 12.0f;  // Per-voice octave shift
      float tpBase = base / powf(2.0f, (detuneSemi + settingsDetuneSemi + octaveSemi) / 12.0f);

      // === PORTAMENTO GLIDE ===
      // One-shot laser
      if (laserTriggered[chip][v]) {
        laserTriggered[chip][v] = false;
      }
      // Portamento slide (per-voice via settings, or per-channel via MIDI CC)
      else if (voiceSettings[voiceIdx].portaOn || portamentoOn[ch]) {
        // Use per-voice speed if portaOn from settings, else use MIDI CC speed
        float speed;
        if (voiceSettings[voiceIdx].portaOn) {
          speed = PORTA_MIN + (PORTA_MAX - PORTA_MIN) * (voiceSettings[voiceIdx].portaSpeed / 127.0f);
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
      if (voiceSettings[voiceIdx].pitchEnvAmt > 0) {
        // Calculate time-based increment
        float envTime = 10.0f + voiceSettings[voiceIdx].pitchEnvTime * 15.0f;  // 10ms to ~2s
        unsigned long elapsed = now - pitchEnvLastTime[voiceIdx];
        if (elapsed > 0 && elapsed < 100) {
          voicePitchEnvPhase[voiceIdx] += elapsed / envTime;
        }
        pitchEnvLastTime[voiceIdx] = now;  // Update timestamp AFTER using it
        if (voicePitchEnvPhase[voiceIdx] > 1.0f) voicePitchEnvPhase[voiceIdx] = 1.0f;

        // Apply pitch offset based on direction
        float envVal = 1.0f - voicePitchEnvPhase[voiceIdx];  // Starts at 1, goes to 0
        float semitones = voiceSettings[voiceIdx].pitchEnvAmt * envVal;
        if (voiceSettings[voiceIdx].pitchEnvDir == 0) {
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
      float finalPeriod = curPeriod[chip][v] / powf(2.0f, realtimeSemi / 12.0f);

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
      if (voiceSettings[voiceIdx].tremoloOn > 0) {
        float rate = voiceSettings[voiceIdx].tremoloRate / 10.0f;  // Hz
        float depth = voiceSettings[voiceIdx].tremoloDepth / 100.0f;  // 0-1
        float intensity = voiceSettings[voiceIdx].tremoloOn / 127.0f;

        // Time-based phase increment
        unsigned long elapsed = now - tremoloLastTime[voiceIdx];
        if (elapsed > 0 && elapsed < 100) {
          tremoloPhase[voiceIdx] += rate * (elapsed / 1000.0f);
        }
        tremoloLastTime[voiceIdx] = now;
        if (tremoloPhase[voiceIdx] >= 1.0f) tremoloPhase[voiceIdx] -= 1.0f;

        // Calculate tremolo amount (0.5 + 0.5*sin = 0 to 1)
        float tremLfo = 0.5f + 0.5f * sinf(tremoloPhase[voiceIdx] * 2 * PI);
        // Scale by depth and intensity: output ranges from (1-depth*intensity) to 1
        float tremMult = 1.0f - (1.0f - tremLfo) * depth * intensity;
        vol = (uint8_t)(vol * tremMult + 0.5f);
      }

      // Apply per-voice max volume cap
      if (vol > voiceSettings[voiceIdx].maxVolume) {
        vol = voiceSettings[voiceIdx].maxVolume;
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
    uint8_t chip = midiToChip[ch];
    for (uint8_t v = 0; v < 3; v++) {
      laserTriggered[chip][v] = false;
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

  // Reset per-voice pitch envelope on note start
  voicePitchEnvPhase[voiceIdx] = 0;
  pitchEnvLastTime[voiceIdx] = millis();  // Initialize timestamp for pitch envelope

  // Only trigger if attack or decay is non-zero (otherwise instant full volume)
  if (voiceSettings[voiceIdx].envAttack > 0 || voiceSettings[voiceIdx].envDecay > 0) {
    envStage[voiceIdx] = ENV_ATTACK;
    envLevel[voiceIdx] = 0.0f;
    envLastTime[voiceIdx] = millis();
  } else {
    // No envelope - use sustain level directly
    envStage[voiceIdx] = ENV_SUSTAIN;
    envLevel[voiceIdx] = voiceSettings[voiceIdx].envSustain / 127.0f;
  }
}

void releaseADSEnvelope(uint8_t voiceIdx) {
  if (voiceIdx >= 9) return;
  // For now, just stop the envelope (no release stage)
  envStage[voiceIdx] = ENV_OFF;
  envLevel[voiceIdx] = 1.0f;
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
      uint8_t attack = voiceSettings[voiceIdx].envAttack;
      uint8_t decay = voiceSettings[voiceIdx].envDecay;
      uint8_t sustain = voiceSettings[voiceIdx].envSustain;

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
    }
  }
}
