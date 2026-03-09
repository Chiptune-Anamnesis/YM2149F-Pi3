#include "sample_player.h"
#include "YM2149.h"
#include "config.h"
#include "pico/time.h"
#include <hardware/sync.h>  // For save_and_disable_interrupts()
#include "sid_mode.h"       // For ymBusBusy

// External YM2149 instance
extern YM2149 ym;

// Which chip/voice to use for sample playback
#define SAMPLE_CHIP 2
#define SAMPLE_VOICE 2  // Use voice C on chip 2

// ============================================================================
// PLAYBACK STATE
// ============================================================================

volatile bool samplePlaying = false;
volatile uint16_t samplePos = 0;
volatile const uint8_t* currentSample = nullptr;
volatile uint16_t currentSampleLen = 0;

// ============================================================================
// CONFIGURATION
// ============================================================================

uint8_t sampleSelect = 0;
uint8_t sampleMode = SAMPLE_MODE_MAPPED;  // Default to GM drum map
uint8_t sampleVolume = 15;
uint8_t sampleSeqIndex = 0;

// ============================================================================
// GM DRUM MAP
// Maps MIDI notes 35-58 to sample indices 0-23
// Edit this array to assign your samples to the correct drum sounds
// ============================================================================
//
// Sample lengths for reference (shorter = hi-hats, longer = kicks/crashes):
//   s00=794, s01=741, s02=979, s03=794, s04=952, s05=1058, s06=1111, s07=1429
//   s08=952, s09=847, s10=1005, s11=952, s12=635, s13=635, s14=847, s15=1058
//   s16=635, s17=847, s18=211, s19=238, s20=555, s21=211, s22=900, s23=397
//
// Default mapping (adjust based on your actual samples):
const uint8_t gmDrumMap[24] = {
  // Note 35: Acoustic Bass Drum -> longest sample (likely kick)
  7,   // s07 (1429) - Acoustic Bass Drum
  // Note 36: Bass Drum 1 (Kick)
  6,   // s06 (1111) - Kick
  // Note 37: Side Stick
  23,  // s23 (397) - Side Stick (short click)
  // Note 38: Acoustic Snare
  2,   // s02 (979) - Snare
  // Note 39: Hand Clap
  12,  // s12 (635) - Clap
  // Note 40: Electric Snare
  4,   // s04 (952) - Electric Snare
  // Note 41: Low Floor Tom
  5,   // s05 (1058) - Low Tom
  // Note 42: Closed Hi-Hat
  18,  // s18 (211) - Closed HH (shortest)
  // Note 43: High Floor Tom
  15,  // s15 (1058) - High Floor Tom
  // Note 44: Pedal Hi-Hat
  21,  // s21 (211) - Pedal HH (short)
  // Note 45: Low Tom
  10,  // s10 (1005) - Low Tom
  // Note 46: Open Hi-Hat
  19,  // s19 (238) - Open HH (short with decay)
  // Note 47: Low-Mid Tom
  11,  // s11 (952) - Low-Mid Tom
  // Note 48: Hi-Mid Tom
  8,   // s08 (952) - Hi-Mid Tom
  // Note 49: Crash Cymbal 1
  0,   // s00 (794) - Crash
  // Note 50: High Tom
  9,   // s09 (847) - High Tom
  // Note 51: Ride Cymbal 1
  1,   // s01 (741) - Ride
  // Note 52: Chinese Cymbal
  3,   // s03 (794) - Chinese Cymbal
  // Note 53: Ride Bell
  16,  // s16 (635) - Ride Bell
  // Note 54: Tambourine
  20,  // s20 (555) - Tambourine
  // Note 55: Splash Cymbal
  13,  // s13 (635) - Splash
  // Note 56: Cowbell
  17,  // s17 (847) - Cowbell
  // Note 57: Crash Cymbal 2
  14,  // s14 (847) - Crash 2
  // Note 58: Vibraslap
  22   // s22 (900) - Vibraslap
};

// ============================================================================
// TIMER
// ============================================================================

repeating_timer_t sampleTimer;
volatile bool sampleTimerActive = false;

// Timer callback - called at 4000 Hz continuously
// Like the original Arduino code, this runs always and checks for active sample
bool sampleTimerCallback(repeating_timer_t *rt) {
  if (!samplePlaying) {
    return true;  // Keep timer running, but do nothing
  }

  // Atomic check-and-set of ymBusBusy to prevent bus collisions
  uint32_t irq = save_and_disable_interrupts();
  if (ymBusBusy) { restore_interrupts(irq); return true; }
  ymBusBusy = true;
  restore_interrupts(irq);

  uint16_t pos = samplePos;
  const uint8_t* sample = (const uint8_t*)currentSample;

  if (sample != nullptr && pos < currentSampleLen) {
    // Read sample value directly from flash (0 or 15)
    uint8_t val = sample[pos];
    samplePos = pos + 1;

    // Scale by volume if sample value is non-zero
    if (val > 0) {
      val = sampleVolume;
    }

    // Fast write: select chip 2 (minimal delay since SEL_B=LOW is reliable)
    // then use writeFast which skips the long chip settling delays
    ym.selectYM(SAMPLE_CHIP);
    ym.writeFast(8 + SAMPLE_VOICE, val);
  } else {
    // Sample finished
    samplePlaying = false;
    ym.selectYM(SAMPLE_CHIP);
    ym.writeFast(8 + SAMPLE_VOICE, 0);
  }

  ymBusBusy = false;
  return true;  // Always keep timer running
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void samplePlayerInit() {
  samplePlaying = false;
  samplePos = 0;
  currentSample = nullptr;
  currentSampleLen = 0;

  sampleSelect = 0;
  sampleMode = SAMPLE_MODE_MAPPED;  // Use GM drum map by default
  sampleVolume = 15;
  sampleSeqIndex = 0;

  // Set up sample voice - silence initially
  ym.write(SAMPLE_CHIP, 8 + SAMPLE_VOICE, 0);

  // Start the sample timer - runs continuously at 4000 Hz like the original code
  // This avoids timing glitches from starting/stopping the timer
  sampleTimerActive = true;
  add_repeating_timer_us(-250, sampleTimerCallback, NULL, &sampleTimer);
}

// ============================================================================
// PLAYBACK CONTROL
// ============================================================================

void sampleTrigger(uint8_t note, uint8_t velocity) {
  uint8_t sampleIdx = 0;

  // Select sample based on mode
  switch (sampleMode) {
    case SAMPLE_MODE_SINGLE:
      sampleIdx = sampleSelect;
      break;

    case SAMPLE_MODE_SEQ:
      sampleIdx = sampleSeqIndex;
      sampleSeqIndex = (sampleSeqIndex + 1) % SAMPLE_COUNT;
      break;

    case SAMPLE_MODE_RANDOM:
      sampleIdx = random(SAMPLE_COUNT);
      break;

    case SAMPLE_MODE_MAPPED:
      // GM drum map - MIDI notes 35-58 mapped via gmDrumMap[]
      if (note >= GM_DRUM_NOTE_MIN && note <= GM_DRUM_NOTE_MAX) {
        sampleIdx = gmDrumMap[note - GM_DRUM_NOTE_MIN];
      } else if (note > GM_DRUM_NOTE_MAX) {
        // Notes above 58: wrap around the map
        sampleIdx = gmDrumMap[(note - GM_DRUM_NOTE_MIN) % (GM_DRUM_NOTE_MAX - GM_DRUM_NOTE_MIN + 1)];
      } else {
        // Notes below 35: use first sample
        sampleIdx = gmDrumMap[0];
      }
      break;

    default:
      sampleIdx = 0;
      break;
  }

  // Bounds check
  if (sampleIdx >= SAMPLE_COUNT) sampleIdx = 0;

  // Disable ALL interrupts (including hardware timers) while updating sample state
  // noInterrupts() only disables GPIO interrupts on RP2040, not timer interrupts
  uint32_t irq = save_and_disable_interrupts();

  // Load sample from lookup table
  currentSample = samples[sampleIdx].data;
  currentSampleLen = samples[sampleIdx].length;
  samplePos = 0;

  // Start playback - timer is already running continuously
  samplePlaying = true;

  restore_interrupts(irq);
}

void sampleStop() {
  samplePlaying = false;
  // Don't stop timer - it runs continuously
  ym.write(SAMPLE_CHIP, 8 + SAMPLE_VOICE, 0);
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

// ============================================================================
// SAMPLE LOOKUP TABLE
// ============================================================================

const SampleInfo samples[SAMPLE_COUNT] = {
  { s00, s00_len },
  { s01, s01_len },
  { s02, s02_len },
  { s03, s03_len },
  { s04, s04_len },
  { s05, s05_len },
  { s06, s06_len },
  { s07, s07_len },
  { s08, s08_len },
  { s09, s09_len },
  { s10, s10_len },
  { s11, s11_len },
  { s12, s12_len },
  { s13, s13_len },
  { s14, s14_len },
  { s15, s15_len },
  { s16, s16_len },
  { s17, s17_len },
  { s18, s18_len },
  { s19, s19_len },
  { s20, s20_len },
  { s21, s21_len },
  { s22, s22_len },
  { s23, s23_len }
};
