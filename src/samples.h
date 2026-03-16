#pragma once
#include <Arduino.h>

// ============================================================================
// SAMPLE DATA
// Three sections: Drums (24), OneShots (24), DigiDrum (16)
// Sample Rate: 8000 Hz, Mono, Unsigned 8-bit
// ============================================================================

#define SAMPLE_RATE 8000

// Section counts
#define DRUM_SAMPLE_COUNT 24
#define ONESHOT_SAMPLE_COUNT 24
#define LEGACY_SAMPLE_COUNT 16

// Sample metadata structure
struct SampleInfo {
  const uint8_t* data;
  uint16_t length;
  const char name[5];  // 4-char display name + null
};

// Include generated sample data
#include "samples_drums.h"
#include "samples_oneshots.h"
#include "samples_legacy.h"

// ============================================================================
// DRUM SAMPLES TABLE
// ============================================================================

const SampleInfo drumSamples[DRUM_SAMPLE_COUNT] = {
  { drm00, drm00_len, "KCK1" },  //  0: Kick 1 (full)
  { drm01, drm01_len, "KCK2" },  //  1: Kick 2 (short)
  { drm02, drm02_len, "KCK3" },  //  2: Kick 3 (tight)
  { drm03, drm03_len, "KCK4" },  //  3: Kick 4 (click)
  { drm04, drm04_len, "SNR1" },  //  4: Snare 1
  { drm05, drm05_len, "SNR2" },  //  5: Snare 2
  { drm06, drm06_len, "SNR3" },  //  6: Snare 3
  { drm07, drm07_len, "SNR4" },  //  7: Snare 4 (tight)
  { drm08, drm08_len, "CHH1" },  //  8: Closed HH 1
  { drm09, drm09_len, "CHH2" },  //  9: Closed HH 2
  { drm10, drm10_len, "CHH3" },  // 10: Closed HH 3
  { drm11, drm11_len, "OHH1" },  // 11: Open HH 1
  { drm12, drm12_len, "OHH2" },  // 12: Open HH 2
  { drm13, drm13_len, "OHH3" },  // 13: Open HH 3
  { drm14, drm14_len, "CLP1" },  // 14: Clap 1
  { drm15, drm15_len, "CLP2" },  // 15: Clap 2
  { drm16, drm16_len, "TOM1" },  // 16: Low Tom
  { drm17, drm17_len, "TOM2" },  // 17: Mid Tom
  { drm18, drm18_len, "TOM3" },  // 18: High Tom
  { drm19, drm19_len, "PRC1" },  // 19: Hand Drum 1
  { drm20, drm20_len, "PRC2" },  // 20: Hand Drum 2
  { drm21, drm21_len, "CWBL" },  // 21: Cowbell
  { drm22, drm22_len, "TAMB" },  // 22: Tambourine
  { drm23, drm23_len, "SHKR" },  // 23: Shaker
};

// ============================================================================
// ONESHOT SAMPLES TABLE
// ============================================================================

const SampleInfo oneshotSamples[ONESHOT_SAMPLE_COUNT] = {
  { os00, os00_len, "BEP1" },  //  0: Beep 1
  { os01, os01_len, "BEP2" },  //  1: Beep 2
  { os02, os02_len, "BLP1" },  //  2: Blip 1
  { os03, os03_len, "BLP2" },  //  3: Blip 2
  { os04, os04_len, "BLOP" },  //  4: Blop
  { os05, os05_len, "BUZ1" },  //  5: Buzz 1
  { os06, os06_len, "BUZ2" },  //  6: Buzz 2
  { os07, os07_len, "DIST" },  //  7: Distortion
  { os08, os08_len, "GLT1" },  //  8: Glitch 1
  { os09, os09_len, "GLT2" },  //  9: Glitch 2
  { os10, os10_len, "MTL1" },  // 10: Metal 1
  { os11, os11_len, "MTL2" },  // 11: Metal 2
  { os12, os12_len, "NOI1" },  // 12: Noise 1
  { os13, os13_len, "NOI2" },  // 13: Noise 2
  { os14, os14_len, "SFX1" },  // 14: SFX 1
  { os15, os15_len, "SFX2" },  // 15: SFX 2
  { os16, os16_len, "STAB" },  // 16: Stab
  { os17, os17_len, "HIT1" },  // 17: Hit
  { os18, os18_len, "STK1" },  // 18: Strike 1
  { os19, os19_len, "STK2" },  // 19: Strike 2
  { os20, os20_len, "VOC1" },  // 20: Vocal 1
  { os21, os21_len, "VOC2" },  // 21: Vocal 2
  { os22, os22_len, "ZAP1" },  // 22: Zap 1
  { os23, os23_len, "ZAP2" },  // 23: Zap 2
};

// ============================================================================
// LEGACY DIGIDRUM SAMPLES TABLE (ST-Sound)
// ============================================================================

const SampleInfo legacySamples[LEGACY_SAMPLE_COUNT] = {
  { s00, s00_len, "DD00" },  //  0
  { s01, s01_len, "DD01" },  //  1
  { s02, s02_len, "DD02" },  //  2
  { s03, s03_len, "DD03" },  //  3
  { s04, s04_len, "DD04" },  //  4
  { s05, s05_len, "DD05" },  //  5
  { s06, s06_len, "DD06" },  //  6
  { s07, s07_len, "DD07" },  //  7
  { s08, s08_len, "DD08" },  //  8
  { s09, s09_len, "DD09" },  //  9
  { s10, s10_len, "DD10" },  // 10
  { s11, s11_len, "DD11" },  // 11
  { s12, s12_len, "DD12" },  // 12
  { s13, s13_len, "DD13" },  // 13
  { s14, s14_len, "DD14" },  // 14
  { s15, s15_len, "DD15" },  // 15
};

// ============================================================================
// SECTION HELPERS
// ============================================================================

// Get sample array and count for a section (0=DRUMS, 1=ONESHOTS, 2=DIGIDRUM)
inline const SampleInfo* getSectionSamples(uint8_t section) {
  if (section == 0) return drumSamples;
  if (section == 1) return oneshotSamples;
  return legacySamples;
}

inline uint8_t getSectionSampleCount(uint8_t section) {
  if (section == 0) return DRUM_SAMPLE_COUNT;
  if (section == 1) return ONESHOT_SAMPLE_COUNT;
  return LEGACY_SAMPLE_COUNT;
}

inline const char* getSectionName(uint8_t section) {
  if (section == 0) return "DRUM";
  if (section == 1) return "1SHT";
  return "DIGI";
}
