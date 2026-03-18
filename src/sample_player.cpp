#include "sample_player.h"
#include "settings.h"
#include "YM2149.h"
#include "config.h"
#include "pico/time.h"
#include <hardware/sync.h>  // For save_and_disable_interrupts()
#include "sid_mode.h"       // For ymBusBusy

// External YM2149 instance
extern YM2149 ym;

// Which chip to use for sample playback (all 3 voices)
#define SAMPLE_CHIP 2

// ============================================================================
// PLAYBACK STATE — 3 polyphonic voices on chip 2
// ============================================================================

SampleVoice sampleVoices[SAMPLE_VOICE_COUNT] = {};
uint8_t nextSampleVoice = 0;  // Round-robin for voice stealing

// ============================================================================
// CONFIGURATION
// ============================================================================

uint8_t sampleSection = SAMPLE_SECTION_DRUMS;
uint8_t sampleSelect = 0;
uint8_t sampleMode = SAMPLE_MODE_MAPPED;  // Default to GM drum map
uint8_t sampleVolume = 15;
uint8_t sampleSeqIndex = 0;
volatile uint8_t lastSampleNote = 0;
volatile uint16_t sampleBendMultiplier = 256;  // 1.0x = no bend

// Semitone ratios in 8.8 fixed-point (256 = 1.0)
// Precomputed: round(256 * 2^(n/12)) for n=0..12
static const uint16_t semitoneTable[13] = {
  256, 271, 287, 304, 323, 342, 362, 384, 406, 431, 456, 483, 512
};

// Compute pitch increment from pitch (-12..+12) and octave (-3..+3)
static uint16_t computePitchIncrement(int8_t pitch, int8_t octave) {
  uint16_t inc = 256;
  if (pitch >= 0 && pitch <= 12) {
    inc = semitoneTable[pitch];
  } else if (pitch < 0 && pitch >= -12) {
    inc = (uint16_t)(65536UL / semitoneTable[-pitch]);
  }
  if (octave > 0) {
    inc <<= octave;
  } else if (octave < 0) {
    inc >>= (-octave);
    if (inc == 0) inc = 1;
  }
  return inc;
}

// ============================================================================
// GM DRUM MAPS
// Maps MIDI notes 35-58 to sample indices
// ============================================================================

// Bitkits drum map (section 0)
// 0-3=Kicks  4-7=Snares  8-10=Closed HH  11-13=Open HH
// 14-15=Claps  16-18=Toms  19-20=Hand Drums  21=Cowbell  22=Tamb  23=Shaker
const uint8_t gmDrumMap[24] = {
  0,   // Note 35: Acoustic Bass Drum → KCK1
  1,   // Note 36: Bass Drum 1       → KCK2
  20,  // Note 37: Side Stick        → PRC2 (short perc)
  4,   // Note 38: Acoustic Snare    → SNR1
  14,  // Note 39: Hand Clap         → CLP1
  5,   // Note 40: Electric Snare    → SNR2
  16,  // Note 41: Low Floor Tom     → TOM1 (low)
  8,   // Note 42: Closed Hi-Hat     → CHH1
  17,  // Note 43: High Floor Tom    → TOM2 (mid)
  9,   // Note 44: Pedal Hi-Hat      → CHH2
  16,  // Note 45: Low Tom           → TOM1 (low)
  11,  // Note 46: Open Hi-Hat       → OHH1
  17,  // Note 47: Low-Mid Tom       → TOM2 (mid)
  18,  // Note 48: Hi-Mid Tom        → TOM3 (high)
  12,  // Note 49: Crash Cymbal 1    → OHH2
  18,  // Note 50: High Tom          → TOM3 (high)
  13,  // Note 51: Ride Cymbal 1     → OHH3
  13,  // Note 52: Chinese Cymbal    → OHH3
  21,  // Note 53: Ride Bell         → CWBL
  22,  // Note 54: Tambourine        → TAMB
  10,  // Note 55: Splash Cymbal     → CHH3
  21,  // Note 56: Cowbell           → CWBL
  12,  // Note 57: Crash Cymbal 2    → OHH2
  23   // Note 58: Vibraslap         → SHKR
};

// Legacy DigiDrum map (section 2, 16 samples)
const uint8_t gmDrumMapLegacy[24] = {
  11,  // Note 35: Acoustic Bass Drum → Hard Kick
  4,   // Note 36: Bass Drum 1       → Kick
  12,  // Note 37: Side Stick        → Tap
  2,   // Note 38: Acoustic Snare    → Short Snare
  6,   // Note 39: Hand Clap         → Hard Clap
  15,  // Note 40: Electric Snare    → Hard Snare
  14,  // Note 41: Low Floor Tom     → Reg Kick
  3,   // Note 42: Closed Hi-Hat     → Muted Snare
  0,   // Note 43: High Floor Tom    → Short Kick
  12,  // Note 44: Pedal Hi-Hat      → Tap
  10,  // Note 45: Low Tom           → Short Kick
  3,   // Note 46: Open Hi-Hat       → Muted Snare
  5,   // Note 47: Low-Mid Tom       → S05
  0,   // Note 48: Hi-Mid Tom        → Short Kick
  8,   // Note 49: Crash Cymbal 1    → Laser
  1,   // Note 50: High Tom          → Light Kick
  7,   // Note 51: Ride Cymbal 1     → Short Laser
  8,   // Note 52: Chinese Cymbal    → Laser
  13,  // Note 53: Ride Bell         → S13
  3,   // Note 54: Tambourine        → Muted Snare
  7,   // Note 55: Splash Cymbal     → Short Laser
  9,   // Note 56: Cowbell           → Voice Sample
  8,   // Note 57: Crash Cymbal 2    → Laser
  9    // Note 58: Vibraslap         → Voice Sample
};

// ============================================================================
// TIMER
// ============================================================================

repeating_timer_t sampleTimer;
volatile bool sampleTimerActive = false;

// Timer callback - called at 8000 Hz continuously
// Processes all 3 sample voices on chip 2 with pitch control
// 3 writeFast calls × ~25µs = 75µs, fits within 125µs period
bool sampleTimerCallback(repeating_timer_t *rt) {
  // Quick exit if no voices active
  bool anyActive = sampleVoices[0].playing ||
                   sampleVoices[1].playing ||
                   sampleVoices[2].playing;
  if (!anyActive) return true;

  // Try to acquire bus — skip this tick if busy (preserves timing)
  uint32_t irq = save_and_disable_interrupts();
  if (ymBusBusy) { restore_interrupts(irq); return true; }
  ymBusBusy = true;
  restore_interrupts(irq);

  ym.selectYM(SAMPLE_CHIP);

  for (uint8_t v = 0; v < SAMPLE_VOICE_COUNT; v++) {
    if (!sampleVoices[v].playing) continue;

    // Fixed-point position: upper bits = actual sample index
    uint32_t fixedPos = sampleVoices[v].pos;
    uint16_t actualPos = fixedPos >> 8;
    const uint8_t* sample = (const uint8_t*)sampleVoices[v].data;

    if (sample == nullptr || actualPos >= sampleVoices[v].length) {
      // Sample finished — silence this voice
      sampleVoices[v].playing = false;
      ym.writeFast(8 + v, 0);
      continue;
    }

    // Read sample byte at current position
    uint8_t val = sample[actualPos];

    // Advance position by per-voice pitch increment (8.8 fixed-point)
    sampleVoices[v].pos = fixedPos + sampleVoices[v].pitchIncrement;

    // Per-sample bitcrush: reduce bit depth and sample rate for lo-fi effect
    uint8_t crush = sampleVoices[v].crushLevel;
    if (crush > 0) {
      // Bit-depth reduction: zero out lower bits
      static const uint8_t crushBits[] = {0, 1, 2, 3, 4, 5, 5, 6};
      uint8_t shift = crushBits[crush];
      val = (val >> shift) << shift;

      // Sample-rate reduction: hold value for N callbacks
      static const uint8_t crushRate[] = {1, 1, 2, 2, 4, 4, 8, 8};
      static uint8_t holdCounter[3] = {0, 0, 0};
      static uint8_t heldVal[3] = {128, 128, 128};
      uint8_t rate = crushRate[crush];
      holdCounter[v]++;
      if (holdCounter[v] >= rate) {
        holdCounter[v] = 0;
        heldVal[v] = val;
      }
      val = heldVal[v];
    }

    // Scale 8-bit (0-255) to 4-bit YM volume (0-15), weighted by sampleVolume
    val = ((uint16_t)val * sampleVolume) >> 8;
    ym.writeFast(8 + v, val);
  }

  ymBusBusy = false;
  return true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void samplePlayerInit() {
  // Clear all voice state
  for (uint8_t v = 0; v < SAMPLE_VOICE_COUNT; v++) {
    sampleVoices[v].playing = false;
    sampleVoices[v].pos = 0;
    sampleVoices[v].data = nullptr;
    sampleVoices[v].length = 0;
    sampleVoices[v].pitchIncrement = 256;  // Default 1.0x speed
    sampleVoices[v].basePitchIncrement = 256;
    sampleVoices[v].note = 0;
    sampleVoices[v].sampleIdx = 0;
    ym.write(SAMPLE_CHIP, 8 + v, 0);  // Silence
  }
  nextSampleVoice = 0;

  // Don't set mixer here - sampleModeEnter() handles it when sample mode is activated
  // Setting it here would overwrite enableTones() and kill chip 2 tone output on boot

  sampleSection = SAMPLE_SECTION_DRUMS;
  sampleSelect = 0;
  sampleMode = SAMPLE_MODE_MAPPED;
  sampleVolume = 15;
  sampleSeqIndex = 0;
  sampleBendMultiplier = 256;

  // Start the sample timer - runs continuously at 8000 Hz
  sampleTimerActive = true;
  add_repeating_timer_us(-125, sampleTimerCallback, NULL, &sampleTimer);
}

// ============================================================================
// PLAYBACK CONTROL
// ============================================================================

void sampleTrigger(uint8_t note, uint8_t velocity) {
  uint8_t sampleIdx = 0;
  const SampleInfo* sectionSamples = getSectionSamples(sampleSection);
  uint8_t sectionCount = getSectionSampleCount(sampleSection);

  // Select sample based on mode
  switch (sampleMode) {
    case SAMPLE_MODE_SINGLE:
      sampleIdx = sampleSelect;
      break;

    case SAMPLE_MODE_SEQ:
      sampleIdx = sampleSeqIndex;
      sampleSeqIndex = (sampleSeqIndex + 1) % sectionCount;
      break;

    case SAMPLE_MODE_RANDOM:
      sampleIdx = random(sectionCount);
      break;

    case SAMPLE_MODE_MAPPED:
      if (sampleSection == SAMPLE_SECTION_ONESHOTS) {
        // OneShots: chromatic mapping from C2 (note 36)
        if (note >= 36 && note < 36 + sectionCount) {
          sampleIdx = note - 36;
        } else {
          sampleIdx = note % sectionCount;
        }
      } else {
        // Drums or DigiDrum: GM drum map
        const uint8_t* map = (sampleSection == SAMPLE_SECTION_DRUMS) ? gmDrumMap : gmDrumMapLegacy;
        if (note >= GM_DRUM_NOTE_MIN && note <= GM_DRUM_NOTE_MAX) {
          sampleIdx = map[note - GM_DRUM_NOTE_MIN];
        } else if (note > GM_DRUM_NOTE_MAX) {
          sampleIdx = map[(note - GM_DRUM_NOTE_MIN) % (GM_DRUM_NOTE_MAX - GM_DRUM_NOTE_MIN + 1)];
        } else {
          sampleIdx = map[0];
        }
      }
      break;

    default:
      sampleIdx = 0;
      break;
  }

  // Bounds check
  if (sampleIdx >= sectionCount) sampleIdx = 0;

  // Track which note/sample triggered (for display and per-sample editing)
  lastSampleNote = note;
  sampleSelect = sampleIdx;

  // Voice allocation:
  // 1. Same sample already playing → retrigger that voice
  // 2. Free voice available → use it
  // 3. No free voice → steal oldest (round-robin)
  int8_t voiceIdx = -1;

  for (uint8_t v = 0; v < SAMPLE_VOICE_COUNT; v++) {
    if (sampleVoices[v].playing && sampleVoices[v].sampleIdx == sampleIdx) {
      voiceIdx = v;
      break;
    }
  }
  if (voiceIdx < 0) {
    for (uint8_t v = 0; v < SAMPLE_VOICE_COUNT; v++) {
      if (!sampleVoices[v].playing) {
        voiceIdx = v;
        break;
      }
    }
  }
  if (voiceIdx < 0) {
    voiceIdx = nextSampleVoice;
    nextSampleVoice = (nextSampleVoice + 1) % SAMPLE_VOICE_COUNT;
  }

  // Look up per-sample pitch/octave/length/crush
  uint8_t flatIdx = sampleFlatIndex(sampleSection, sampleIdx);
  int8_t pitch = samplePitchArr[flatIdx];
  int8_t octave = sampleOctaveArr[flatIdx];
  uint8_t len = sampleLengthArr[flatIdx];
  uint8_t crush = sampleCrushArr[flatIdx];

  // Compute effective length based on per-sample length (1-127, 127=full)
  uint16_t fullLen = sectionSamples[sampleIdx].length;
  uint16_t effectiveLen = ((uint32_t)fullLen * len) / 127;
  if (effectiveLen == 0) effectiveLen = 1;

  // Compute per-sample pitch increment and apply current pitch bend
  uint16_t pInc = computePitchIncrement(pitch, octave);
  uint16_t bendMult = sampleBendMultiplier;
  uint16_t effectiveInc = ((uint32_t)pInc * bendMult) >> 8;
  if (effectiveInc == 0) effectiveInc = 1;

  // Atomically update voice state
  uint32_t irq = save_and_disable_interrupts();
  sampleVoices[voiceIdx].data = sectionSamples[sampleIdx].data;
  sampleVoices[voiceIdx].length = effectiveLen;
  sampleVoices[voiceIdx].basePitchIncrement = pInc;
  sampleVoices[voiceIdx].pitchIncrement = effectiveInc;
  sampleVoices[voiceIdx].pos = 0;  // 8.8 fixed-point, starts at 0.0
  sampleVoices[voiceIdx].note = note;
  sampleVoices[voiceIdx].sampleIdx = sampleIdx;
  sampleVoices[voiceIdx].crushLevel = crush;
  sampleVoices[voiceIdx].playing = true;
  restore_interrupts(irq);
}

void sampleStop() {
  for (uint8_t v = 0; v < SAMPLE_VOICE_COUNT; v++) {
    sampleVoices[v].playing = false;
    ym.write(SAMPLE_CHIP, 8 + v, 0);
  }
}

void sampleModeEnter() {
  sampleStop();
  sampleBendMultiplier = 256;
  // Disable tone+noise on all chip 2 voices (pure volume-register PCM)
  ym.write(SAMPLE_CHIP, 0x07, 0b00111111);
}

void sampleModeExit() {
  sampleStop();
  sampleBendMultiplier = 256;
  // Restore chip 2 mixer to normal (tones enabled, noise off)
  ym.write(SAMPLE_CHIP, 0x07, 0b00111000);
}

// sampleUpdatePitchIncrement() — kept as no-op for API compatibility
// Pitch increments are now computed per-sample at trigger time
void sampleUpdatePitchIncrement() {}

void sampleApplyPitchBend(uint8_t lsb, uint8_t msb) {
  int val = (msb << 7) | lsb;    // 0..16383, center=8192
  int offset = val - 8192;        // -8192..+8191

  uint16_t mult;
  if (offset == 0) {
    mult = 256;
  } else if (offset > 0) {
    // Positive bend: pitch UP — interpolate in semitoneTable
    // Map offset (0..8191) to semitones (0..2.0) in 8.8 fixed-point
    uint32_t semiFixed = ((uint32_t)offset * 512) / 8192;  // 0..511
    uint8_t semiInt = semiFixed >> 8;     // 0..1
    uint8_t semiFrac = semiFixed & 0xFF;  // fractional 0..255
    if (semiInt > 11) semiInt = 11;
    uint16_t a = semitoneTable[semiInt];
    uint16_t b = semitoneTable[semiInt + 1];
    mult = a + (((uint32_t)(b - a) * semiFrac) >> 8);
  } else {
    // Negative bend: pitch DOWN — interpolate then invert
    uint32_t absOffset = (uint32_t)(-offset);
    uint32_t semiFixed = (absOffset * 512) / 8192;
    uint8_t semiInt = semiFixed >> 8;
    uint8_t semiFrac = semiFixed & 0xFF;
    if (semiInt > 11) semiInt = 11;
    uint16_t a = semitoneTable[semiInt];
    uint16_t b = semitoneTable[semiInt + 1];
    uint16_t upMult = a + (((uint32_t)(b - a) * semiFrac) >> 8);
    mult = (uint16_t)(65536UL / upMult);
  }

  sampleBendMultiplier = mult;

  // Update effective pitchIncrement for all active voices
  for (uint8_t v = 0; v < SAMPLE_VOICE_COUNT; v++) {
    if (sampleVoices[v].playing) {
      uint16_t effective = ((uint32_t)sampleVoices[v].basePitchIncrement * mult) >> 8;
      if (effective == 0) effective = 1;
      sampleVoices[v].pitchIncrement = effective;
    }
  }
}

// ============================================================================
// HELPERS
// ============================================================================

const char* getSampleModeName(uint8_t mode) {
  switch (mode) {
    case SAMPLE_MODE_SINGLE: return "SNGL";
    case SAMPLE_MODE_SEQ: return "SEQ";
    case SAMPLE_MODE_RANDOM: return "RND";
    case SAMPLE_MODE_MAPPED: return "GM";  // General MIDI drum map
    default: return "?";
  }
}

