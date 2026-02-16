#include "sid_mode.h"
#include "settings.h"
#include "YM2149.h"

// Run timing-critical functions from RAM to avoid Flash bus contention
#define RAM_FUNC __attribute__((section(".time_critical")))

// Fixed pattern length for smooth waveforms (sidDuty only affects square wave pulse width)
#define SID_PATTERN_LEN 16

// External YM2149 instance (defined in main)
extern YM2149 ym;

// External voice state (defined in voice_manager)
extern bool voiceActive[3][3];
extern uint8_t voiceVol[3][3];

// ============================================================================
// SID MODE STATE
// ============================================================================

bool sidMode = false;
uint8_t sidEnvFreqCoarse = 0;
uint8_t sidEnvFreqFine = 50;
uint8_t sidEnvShape = 0b1110;  // Triangle

// ============================================================================
// SOFTWARE PWM STATE
// ============================================================================

volatile bool sidPWM = true;
volatile uint8_t sidDuty = 8;
volatile uint8_t sidWaveType = 0;  // 0=square, 1=saw, 2=triangle, 3=pulse
volatile int8_t sidDetune = 0;
volatile uint8_t sidPattern[SID_MAX_PATTERN];

volatile uint16_t sidPhase[3][3] = {{0}};
volatile uint16_t sidPhaseInc[3][3] = {{0}};
volatile uint16_t sidPeriod[3][3] = {{0}};
volatile uint8_t sidVoiceVol[3][3] = {{0}};
volatile bool sidVoiceOn[3][3] = {{false}};
volatile bool ymBusBusy = false;

struct repeating_timer sidTimer;
volatile bool sidTimerActive = false;

// ============================================================================
// FUNCTIONS
// ============================================================================

RAM_FUNC uint16_t calcPhaseInc(uint16_t period) {
  if (period == 0) return 0;
  // Add detune: slightly offset the frequency for phasing effect
  int32_t adjustedPeriod = (int32_t)period + sidDetune;
  if (adjustedPeriod < 1) adjustedPeriod = 1;
  // Fixed pattern length for consistent pitch across all waveforms
  return (uint16_t)((23040UL * SID_PATTERN_LEN) / adjustedPeriod);
}

void sidGeneratePattern() {
  const uint8_t len = SID_PATTERN_LEN;

  switch (sidWaveType) {
    case 0: // Square: duty controls pulse width within fixed pattern
      {
        uint8_t highSamples = sidDuty;  // sidDuty 4-16 = high samples
        if (highSamples < 1) highSamples = 1;
        if (highSamples > len - 1) highSamples = len - 1;
        for (uint8_t i = 0; i < len; i++)
          sidPattern[i] = (i < highSamples) ? 15 : 0;
      }
      break;

    case 1: // Sawtooth: smooth ramp down from 15 to 0
      for (uint8_t i = 0; i < len; i++)
        sidPattern[i] = 15 - (i * 15) / (len - 1);
      break;

    case 2: // Triangle: smooth ramp up then down
      for (uint8_t i = 0; i < len; i++) {
        if (i < len / 2)
          sidPattern[i] = (i * 15) / (len / 2);
        else
          sidPattern[i] = 15 - ((i - len / 2) * 15) / (len / 2);
      }
      break;

    case 3: // Narrow pulse: 1 sample high, rest low
      for (uint8_t i = 0; i < len; i++)
        sidPattern[i] = (i == 0) ? 15 : 0;
      break;
  }
}

RAM_FUNC bool sidTimerCallback(struct repeating_timer *t) {
  if (ymBusBusy) return true;  // Skip if main loop is using the bus

  // Round-robin through all 9 voices (chip0/v0, chip0/v1, ... chip2/v2)
  static uint8_t isrSlot = 0;
  static uint8_t lastChip = 0xFF;

  uint8_t chip = isrSlot / 3;
  uint8_t v = isrSlot % 3;

  if (sidVoiceOn[chip][v]) {
    // Only re-select chip if changed
    if (chip != lastChip) {
      ym.selectYM(chip);
      if (chip == 0) delayMicroseconds(8);  // SEL_B threshold settle
      lastChip = chip;
    }

    // Advance phase accumulator (8.8 fixed point)
    sidPhase[chip][v] += sidPhaseInc[chip][v];

    // Extract pattern position from high byte, wrap at fixed pattern length
    uint8_t patIdx = (sidPhase[chip][v] >> 8) % SID_PATTERN_LEN;

    // Scale pattern sample by voice target volume
    uint8_t sample = sidPattern[patIdx];
    uint8_t vol = (sample * sidVoiceVol[chip][v] + 7) / 15;
    ym.writeFast(8 + v, vol & 0x0F);
  }

  isrSlot = (isrSlot + 1) % 9;
  return true;
}

void sidTimerStart() {
  if (!sidTimerActive) {
    add_repeating_timer_us(-SID_TIMER_INTERVAL_US, sidTimerCallback, NULL, &sidTimer);
    sidTimerActive = true;
  }
}

void sidTimerStop() {
  if (sidTimerActive) {
    cancel_repeating_timer(&sidTimer);
    sidTimerActive = false;
  }
}

void setupSidEnvelope(uint8_t chip) {
  // Enable SID for voices on this chip that have it enabled in settings
  for (uint8_t v = 0; v < 3; v++) {
    uint8_t voiceIdx = chip * 3 + v;
    if (voiceSettings[voiceIdx].sidOn && voiceActive[chip][v]) {
      sidVoiceOn[chip][v] = true;
      sidPhase[chip][v] = 0;  // Reset phase on note-on
    }
  }
}

void sidInit() {
  sidGeneratePattern();
  // Timer will be started when a voice with SID enabled is activated
}
