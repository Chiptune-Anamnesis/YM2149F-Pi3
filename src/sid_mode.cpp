#include "sid_mode.h"
#include "settings.h"  // For sidModeGlobal, sidDutyChip
#include "YM2149.h"
#include "hardware/timer.h"  // For time_us_32()
#include <hardware/sync.h>   // For save_and_disable_interrupts()

// Which chip to update this callback (alternates 1 and 2 in global mode)
static uint8_t currentSidChip = 1;

// Phase compensation: track last callback time to handle missed intervals
static uint32_t lastCallbackTime = 0;

// Run timing-critical functions from RAM to avoid Flash bus contention
#define RAM_FUNC __attribute__((section(".time_critical")))

// External YM2149 instance (defined in main)
extern YM2149 ym;

// External voice state (defined in voice_manager)
extern bool voiceActive[3][3];
extern uint8_t voiceVol[3][3];

// ============================================================================
// SID CHIP SELECTION
// ============================================================================

// NOTE: In global SID mode, we use chips 1 and 2 (not chip 0)
// because chip 0 requires 500µs settling time which is too long for
// the SID timer (fires every 20µs). Chips 1/2 only need 100µs.
// The old per-voice sidChip variable is kept for legacy compatibility
uint8_t sidChip = 2;  // Legacy: default to Chip 2

// ============================================================================
// SID MODE STATE
// ============================================================================

bool sidMode = false;
uint8_t sidEnvFreqCoarse = 0;
uint8_t sidEnvFreqFine = 50;
uint8_t sidEnvShape = 0b1110;  // Triangle

// ============================================================================
// NEXTSID-STYLE STATE
// ============================================================================

// Per-voice state for synchronized volume flipping
// Global SID mode: 6 voices ([0-2]=chip1, [3-5]=chip2)
// Legacy mode: only first 3 used ([0-2]=sidChip)
SidVoiceState sidState[6] = {{0}};

// Duty cycle: 0-15 (0=thin pulse ~6%, 8=50% square, 15=thick pulse ~94%)
volatile uint8_t sidDuty = 11;

// ============================================================================
// LEGACY STATE (kept for compatibility during transition)
// ============================================================================

volatile bool sidPWM = true;
volatile uint8_t sidWaveType = 0;
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
volatile bool sidTimerPauseRequested = false;  // Used by Core 1 to pause timer during flash reads

// ============================================================================
// NEXTSID-STYLE FUNCTIONS
// ============================================================================

// Update SID timing when period changes
// period: YM period value (2MHz / 16 clock cycles per count)
// Uses phase accumulator approach for timing-drift-free operation
void updateSidTiming(uint8_t voice, uint16_t period) {
  if (voice >= 3) return;

  SidVoiceState& s = sidState[voice];
  s.period = period;

  if (period == 0) {
    s.phaseInc = 0;
    return;
  }

  // Calculate phase increment for timer callback
  // YM clock = 2MHz, tone frequency = 2,000,000 / (16 * period) Hz
  // Period in µs = period * 8
  // Timer fires every SID_TIMER_INTERVAL_US (20µs)
  //
  // Phase accumulator wraps at 65536 (16-bit overflow in upper 16 bits)
  // We want one full cycle (65536 phase units) per YM period
  // phaseInc = 65536 * timer_interval / period_us
  //          = 65536 * 20 / (period * 8)
  //          = 163840 / period
  //
  // For high frequencies (small period), phaseInc can be large
  // For low frequencies (large period), phaseInc will be small
  s.phaseInc = 163840 / period;

  // Clamp to prevent issues with very high frequencies
  // At phaseInc = 65536, we complete one cycle per callback (max frequency)
  if (s.phaseInc > 65536) s.phaseInc = 65536;

  // Minimum increment to ensure some audible output
  if (s.phaseInc == 0) s.phaseInc = 1;
}

// Update mixer register to disable tones for active SID voices
// This is crucial - we need pure volume PWM, not interference with tone generator
static void updateSidMixer() {
  // Mixer register 7:
  // Bits 0-2: Tone enable A,B,C (0=on, 1=off)
  // Bits 3-5: Noise enable A,B,C (0=on, 1=off)
  // For SID voices: disable tone (set bit to 1), keep noise off (bit = 1)

  uint8_t mixer = 0b00111000;  // Start with all tones on, noise off

  // Disable tone for each active SID voice
  for (uint8_t v = 0; v < 3; v++) {
    if (sidState[v].active) {
      mixer |= (1 << v);  // Set bit to disable tone for this voice
    }
  }

  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(sidChip);
  ym.writeFast(7, mixer);
  ymBusBusy = false;
}

// Start a SID voice with given period and volume
void sidVoiceStart(uint8_t voice, uint16_t period, uint8_t volume) {
  if (voice >= 3) return;

  // Block timer callback while modifying voice state
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }

  SidVoiceState& s = sidState[voice];
  s.volume = volume & 0x0F;
  s.phase = 0;      // Start at beginning of cycle
  s.isHigh = true;  // Initial state is HIGH
  s.active = true;

  updateSidTiming(voice, period);

  ymBusBusy = false;

  // Disable tone generator for this voice (use only volume PWM)
  updateSidMixer();

  // Write initial high volume
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(sidChip);
  ym.writeFast(8 + voice, s.volume);
  ymBusBusy = false;

  // Ensure timer is running
  sidTimerStart();
}

// Stop a SID voice
void sidVoiceStop(uint8_t voice) {
  if (voice >= 3) return;

  // Block timer callback while modifying voice state
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }

  SidVoiceState& s = sidState[voice];
  s.active = false;
  s.phase = 0;
  s.phaseInc = 0;

  // Write zero volume
  ym.selectYM(sidChip);
  ym.writeFast(8 + voice, 0);
  ymBusBusy = false;

  // Update mixer to re-enable tone for this voice (if no longer in SID mode)
  updateSidMixer();

  // Check if any SID voices still active
  bool anyActive = false;
  for (uint8_t v = 0; v < 3; v++) {
    if (sidState[v].active) {
      anyActive = true;
      break;
    }
  }

  // Stop timer if no voices active
  if (!anyActive) {
    sidTimerStop();
  }
}

// ============================================================================
// TIMER CALLBACK - NextSID-style synchronized volume flipping
// ============================================================================

RAM_FUNC bool sidTimerCallback(struct repeating_timer *t) {
  // Check if pause is requested (for flash reads from Core 1)
  if (sidTimerPauseRequested) return true;

  // Atomic check-and-set of ymBusBusy to prevent TOCTOU race
  uint32_t irq = save_and_disable_interrupts();
  if (ymBusBusy) { restore_interrupts(irq); return true; }
  ymBusBusy = true;
  restore_interrupts(irq);

  // Phase compensation: calculate how many intervals elapsed since last callback
  // This corrects for skipped callbacks (when ymBusBusy blocked us)
  uint32_t now = time_us_32();
  uint32_t elapsed = now - lastCallbackTime;
  lastCallbackTime = now;
  uint8_t steps = elapsed / SID_TIMER_INTERVAL_US;
  if (steps < 1) steps = 1;
  if (steps > 10) steps = 10;  // Cap to avoid huge jumps on first call or long pauses

  if (sidModeGlobal) {
    // Global SID mode: round-robin between chips 1 and 2
    // Each callback updates one chip (3 voices), alternating each call
    // This gives each voice a 40µs update rate (25kHz)
    ym.selectYM(currentSidChip);

    uint8_t chipOffset = (currentSidChip - 1) * 3;  // 0 for chip 1, 3 for chip 2
    // Advance all phases
    for (uint8_t v = 0; v < 3; v++) {
      SidVoiceState& s = sidState[chipOffset + v];
      if (s.active && s.phaseInc > 0) {
        s.phase += s.phaseInc * steps;  // Compensate for missed callbacks
      }
    }

    // Calculate volumes and write to hardware
    for (uint8_t v = 0; v < 3; v++) {
      SidVoiceState& s = sidState[chipOffset + v];
      if (!s.active || s.phaseInc == 0) continue;

      uint16_t phasePos = (uint16_t)s.phase;
      uint8_t vol;

      // Calculate effective duty (with PWM sweep)
      uint8_t effectiveDuty = s.duty;
      if (s.pwmPhaseInc > 0) {
        s.pwmPhase += s.pwmPhaseInc * steps;  // Compensate for missed callbacks
        uint16_t pwmPos = (uint16_t)s.pwmPhase;
        // Triangle LFO: sweeps ±pwmDepth around base duty
        int8_t mod;
        if (pwmPos < 32768)
          mod = (int8_t)((uint32_t)s.pwmDepth * pwmPos / 32768);
        else
          mod = (int8_t)((uint32_t)s.pwmDepth * (65535 - pwmPos) / 32768);
        int eDuty = (int)s.duty + mod;
        if (eDuty < 0) eDuty = 0;
        if (eDuty > 15) eDuty = 15;
        effectiveDuty = (uint8_t)eDuty;
      }

      // Calculate volume based on waveform type
      switch (s.waveform) {
        case 1: // Sawtooth (ramp down)
          vol = (uint8_t)((uint32_t)s.volume * (65535 - phasePos) >> 16);
          break;
        case 2: // Triangle
          if (phasePos < 32768)
            vol = (uint8_t)((uint32_t)s.volume * phasePos >> 15);
          else
            vol = (uint8_t)((uint32_t)s.volume * (65535 - phasePos) >> 15);
          break;
        case 3: // Double pulse (two narrow pulses, duty controls spacing)
        {
          const uint32_t pulseWidth = 4096;  // Each pulse is 1/16 of cycle
          // Second pulse position: duty maps to 1/16..15/16 of cycle
          uint32_t secondPos = ((uint32_t)(effectiveDuty + 1) * 65536) / 16;
          bool inFirst = (phasePos < pulseWidth);
          bool inSecond = (phasePos >= secondPos && phasePos < secondPos + pulseWidth);
          vol = (inFirst || inSecond) ? s.volume : 0;
          break;
        }
        default: // Square with duty
        {
          uint32_t dutyThreshold = ((uint32_t)effectiveDuty * 65536) / 16;
          vol = (phasePos < dutyThreshold) ? s.volume : 0;
          break;
        }
      }

      // Only write if value changed (efficiency)
      if (vol != s.lastWrittenVol) {
        ym.writeFast(8 + v, vol);
        s.lastWrittenVol = vol;
      }
    }

    // Alternate between chips 1 and 2
    currentSidChip = (currentSidChip == 1) ? 2 : 1;
  } else {
    // Legacy mode: single dedicated chip (sidChip variable)
    ym.selectYM(sidChip);

    // Process all 3 voices on the dedicated chip using phase accumulator
    for (uint8_t v = 0; v < 3; v++) {
      SidVoiceState& s = sidState[v];

      if (!s.active || s.phaseInc == 0) continue;

      s.phase += s.phaseInc * steps;  // Compensate for missed callbacks

      // Duty cycle threshold (legacy uses global sidDuty)
      uint32_t dutyThreshold = ((uint32_t)sidDuty * 65536) / 16;
      uint16_t phasePos = (uint16_t)s.phase;

      bool shouldBeHigh = (phasePos < dutyThreshold);

      if (shouldBeHigh != s.isHigh) {
        s.isHigh = shouldBeHigh;
        ym.writeFast(8 + v, s.isHigh ? s.volume : 0);
      }
    }
  }

  ymBusBusy = false;  // Allow main loop writes again
  return true;
}

// ============================================================================
// TIMER CONTROL
// ============================================================================

void sidTimerStart() {
  if (!sidTimerActive) {
    lastCallbackTime = time_us_32();  // Initialize so first callback has valid elapsed time
    add_repeating_timer_us(-SID_TIMER_INTERVAL_US, sidTimerCallback, NULL, &sidTimer);
    sidTimerActive = true;
  }
}

void sidTimerStop() {
  if (sidTimerActive) {
    sidTimerActive = false;  // Set flag first to prevent restarts
    cancel_repeating_timer(&sidTimer);  // This blocks until timer actually stops
    __dmb();  // Memory barrier to ensure all operations complete
  }
}

// Pause SID timer (for flash reads from Core 1)
// Uses flag-based pause - timer keeps running but callback returns immediately
void sidTimerPause() {
  if (!sidTimerActive) return;

  // Set pause flag - callback will return immediately when it sees this
  sidTimerPauseRequested = true;
  __dmb();  // Memory barrier to ensure flag is visible to Core 0

  // Wait for any in-progress callback to complete
  // The callback sets ymBusBusy=true at start, clears it at end
  unsigned long waitStart = millis();
  while (ymBusBusy && (millis() - waitStart < 50)) {
    delayMicroseconds(5);
  }

  // Give extra time for callback to fully exit and see the flag
  // Timer fires every 20µs, so 100µs is 5 timer periods
  delayMicroseconds(100);
}

// Resume SID timer after flash read
void sidTimerResume() {
  if (sidTimerPauseRequested) {
    lastCallbackTime = time_us_32();  // Reset so first callback after resume doesn't over-compensate
    sidTimerPauseRequested = false;
    __dmb();  // Memory barrier
  }
}

// ============================================================================
// SID ENVELOPE SETUP (hardware envelope mode)
// ============================================================================

void setupSidEnvelope(uint8_t chip) {
  // This is for hardware envelope mode, not NextSID PWM mode
  // Configure YM envelope registers for the chip
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(chip);
  ym.writeFast(0x0B, sidEnvFreqFine);
  ym.writeFast(0x0C, sidEnvFreqCoarse);
  ym.writeFast(0x0D, sidEnvShape);
  ymBusBusy = false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void sidInit() {
  // Initialize SID state with phase accumulator fields (all 6 voices)
  for (uint8_t v = 0; v < 6; v++) {
    sidState[v].period = 0;
    sidState[v].phase = 0;
    sidState[v].phaseInc = 0;
    sidState[v].isHigh = false;
    sidState[v].volume = 0;
    sidState[v].duty = 8;  // Default 50% duty (voiceSettings may not be loaded yet)
    sidState[v].active = false;
    sidState[v].waveform = 0;
    sidState[v].lastWrittenVol = 0;
    sidState[v].pwmPhase = 0;
    sidState[v].pwmPhaseInc = 0;
    sidState[v].pwmDepth = 0;
    sidState[v].syncSource = 0;
    sidState[v].ringSource = 0;
    sidState[v].noiseOn = false;
  }

  // Timer will be started when a SID voice is activated
}

// ============================================================================
// GLOBAL SID MODE FUNCTIONS (6-voice, chips 1+2)
// ============================================================================

void sidModeInit() {
  // Stop any existing timer
  sidTimerStop();

  // Clear all voice states and initialize from voiceSettings + sidDutyChip[]
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t voiceIdx = 3 + i;  // voices 3-8
    sidState[i].period = 0;
    sidState[i].phase = 0;
    sidState[i].phaseInc = 0;
    sidState[i].isHigh = false;
    sidState[i].volume = 0;
    uint8_t chipIdx = (i < 3) ? 0 : 1;
    sidState[i].duty = sidDutyChip[chipIdx];
    sidState[i].active = false;
    sidState[i].waveform = voiceSettings[voiceIdx].sidWave;
    sidState[i].lastWrittenVol = 0;
    sidState[i].pwmPhase = 0;
    sidState[i].pwmPhaseInc = 0;
    sidState[i].pwmDepth = voiceSettings[voiceIdx].sidPwmDepth;
    sidState[i].syncSource = voiceSettings[voiceIdx].sidSync;
    sidState[i].ringSource = voiceSettings[voiceIdx].sidRing;
    sidState[i].noiseOn = (voiceSettings[voiceIdx].sidNoise != 0);
  }

  // Disable tone generators on chips 1 and 2 (we use volume PWM only)
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(1);
  ym.writeFast(7, 0b00111111);  // All tones off, all noise off
  ym.selectYM(2);
  ym.writeFast(7, 0b00111111);
  // Zero all volumes
  for (uint8_t v = 0; v < 3; v++) {
    ym.selectYM(1);
    ym.writeFast(8 + v, 0);
    ym.selectYM(2);
    ym.writeFast(8 + v, 0);
  }
  ymBusBusy = false;

  // Reset round-robin to start with chip 1
  currentSidChip = 1;

  // Start the timer
  sidTimerStart();
}

void sidModeStop() {
  // Stop the timer first
  sidTimerStop();

  // Stop all SID voices
  for (uint8_t i = 0; i < 6; i++) {
    sidState[i].active = false;
    sidState[i].phase = 0;
    sidState[i].phaseInc = 0;
  }

  // Re-enable tone generators on chips 1 and 2
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(1);
  ym.writeFast(7, 0b00111000);  // Tones on, noise off
  ym.selectYM(2);
  ym.writeFast(7, 0b00111000);
  // Zero all volumes
  for (uint8_t v = 0; v < 3; v++) {
    ym.selectYM(1);
    ym.writeFast(8 + v, 0);
    ym.selectYM(2);
    ym.writeFast(8 + v, 0);
  }
  ymBusBusy = false;
}

void sidVoiceStartGlobal(uint8_t voiceIdx, uint16_t period, uint8_t volume) {
  if (voiceIdx >= 6) return;

  SidVoiceState& s = sidState[voiceIdx];
  uint8_t chip = (voiceIdx < 3) ? 1 : 2;
  uint8_t voice = voiceIdx % 3;

  // Start silent - updatePitchMod will set envelope-scaled volume
  // This closes the race window where the timer could read full velocity
  s.volume = 0;

  // Start phase at end of cycle so first callback sees LOW->HIGH transition
  // This prevents the click - voice starts silent, timer writes real volume
  s.phase = 0xFFFF;  // Near end of cycle (will wrap on first increment)
  s.isHigh = false;  // Start LOW to match phase position
  s.active = true;

  // Get per-chip duty cycle from global settings (same as timer callback uses)
  uint8_t chipIdx = chip - 1;  // chip 1->0, chip 2->1
  s.duty = sidDutyChip[chipIdx];

  // Update timing (set phaseInc)
  updateSidTimingGlobal(voiceIdx, period);

  // Write ZERO volume initially - timer callback will write real volume
  // on first LOW->HIGH transition, preventing click
  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(chip);
  ym.writeFast(8 + voice, 0);  // Start silent
  ymBusBusy = false;

  // Ensure timer is running (safeguard in case it got stopped)
  sidTimerStart();
}

void sidVoiceStopGlobal(uint8_t voiceIdx) {
  if (voiceIdx >= 6) return;

  SidVoiceState& s = sidState[voiceIdx];

  // Set to LOW state before cutting volume (prevents HIGH→0 click)
  // This ensures we're going from 0→0 instead of potentially 15→0
  s.isHigh = false;
  s.volume = 0;
  s.active = false;
  s.phase = 0;
  s.phaseInc = 0;

  // Write zero volume to the appropriate chip
  uint8_t chip = (voiceIdx < 3) ? 1 : 2;
  uint8_t voice = voiceIdx % 3;

  {
    uint32_t irq = save_and_disable_interrupts();
    ymBusBusy = true;
    restore_interrupts(irq);
  }
  ym.selectYM(chip);
  ym.writeFast(8 + voice, 0);
  ymBusBusy = false;
}

void updateSidTimingGlobal(uint8_t voiceIdx, uint16_t period) {
  if (voiceIdx >= 6) return;

  SidVoiceState& s = sidState[voiceIdx];
  s.period = period;

  if (period == 0) {
    s.phaseInc = 0;
    return;
  }

  // Calculate phase increment
  // In global mode, timer alternates between chips 1 and 2, so each chip
  // only gets updated every 40µs (not 20µs). Double the constant to compensate:
  // 327680 = 65536 * 40 / 8
  s.phaseInc = 327680 / period;

  // Clamp to prevent issues with very high frequencies
  if (s.phaseInc > 65536) s.phaseInc = 65536;
  if (s.phaseInc == 0) s.phaseInc = 1;
}

// ============================================================================
// LEGACY FUNCTIONS (kept for compatibility)
// ============================================================================

// Fixed pattern length for smooth waveforms
#define SID_PATTERN_LEN 16

RAM_FUNC uint16_t calcPhaseInc(uint16_t period) {
  if (period == 0) return 0;
  int32_t adjustedPeriod = (int32_t)period + sidDetune;
  if (adjustedPeriod < 1) adjustedPeriod = 1;
  return (uint16_t)((23040UL * SID_PATTERN_LEN) / adjustedPeriod);
}

void sidGeneratePattern() {
  const uint8_t len = SID_PATTERN_LEN;

  switch (sidWaveType) {
    case 0: // Square
      {
        uint8_t highSamples = sidDuty;
        if (highSamples < 1) highSamples = 1;
        if (highSamples > len - 1) highSamples = len - 1;
        for (uint8_t i = 0; i < len; i++)
          sidPattern[i] = (i < highSamples) ? 15 : 0;
      }
      break;

    case 1: // Sawtooth
      for (uint8_t i = 0; i < len; i++)
        sidPattern[i] = 15 - (i * 15) / (len - 1);
      break;

    case 2: // Triangle
      for (uint8_t i = 0; i < len; i++) {
        if (i < len / 2)
          sidPattern[i] = (i * 15) / (len / 2);
        else
          sidPattern[i] = 15 - ((i - len / 2) * 15) / (len / 2);
      }
      break;

    case 3: // Narrow pulse
      for (uint8_t i = 0; i < len; i++)
        sidPattern[i] = (i == 0) ? 15 : 0;
      break;
  }
}
