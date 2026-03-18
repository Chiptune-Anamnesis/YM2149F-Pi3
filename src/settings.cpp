#include "settings.h"
#include "effects.h"
#include "voice_manager.h"
#include "encoder.h"
#include "YM2149.h"
#include "sid_mode.h"
#include "fx_chip.h"
#include "sample_player.h"
#include "preset.h"  // For saveGlobalSettings()
#include <hardware/sync.h>  // For save_and_disable_interrupts()

extern YM2149 ym;

// ============================================================================
// SETTINGS STATE
// ============================================================================

VoiceSettings voiceSettings[9];
SettingsTarget currentTarget = TARGET_ALL;

// Default pot assignments: Decay, Sustain, Release (basic envelope control)
PotAssignment potAssignments[3] = {
  { PCAT_ENVELOPE, 1, TARGET_ALL, 0 },  // POT1: Envelope Decay → ALL
  { PCAT_ENVELOPE, 2, TARGET_ALL, 0 },  // POT2: Envelope Sustain → ALL
  { PCAT_ENVELOPE, 3, TARGET_ALL, 0 }   // POT3: Envelope Release → ALL
};

// Chip link: when enabled, all 3 voices on a chip play the same note
bool chipLink[3] = {false, false, false};

// Global link mode: 0=OFF, 1=CH1, 2=CH2, 3=ALL
uint8_t linkMode = LINK_OFF;

// Voice link mask: which Chip 0 voices participate in linking (default OFF)
uint8_t voiceLinkMask = 0;  // OFF by default - voices play independently

int potValues[3] = {0};
int potLastValues[3] = {0};

// MIDI channel settings (volatile for cross-core access)
volatile uint8_t midiSynthChannel = MIDI_CHANNEL_OMNI;  // OMNI by default (respond to all channels)

// MIDI channel routing (volatile for cross-core access)
// Default: identity mapping (no remapping)
volatile uint8_t midiChannelRemap[16] = {
  MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE,
  MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE,
  MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE,
  MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE, MIDI_REMAP_NONE
};

// USB mode setting (takes effect on reboot)
uint8_t usbMode = USB_MODE_MIDI;  // USB-MIDI by default

// Device mode settings (YM / SID / SMPL — only one active at a time)
volatile bool sidModeGlobal = false;
volatile bool sampleModeGlobal = false;
uint8_t sidDutyChip[2] = {11, 11};         // Default duty for both chips

// Per-sample pitch + length arrays (indexed by sampleFlatIndex)
int8_t  samplePitchArr[TOTAL_SAMPLE_COUNT]  = {0};  // All default to 0
int8_t  sampleOctaveArr[TOTAL_SAMPLE_COUNT] = {0};  // All default to 0
uint8_t sampleLengthArr[TOTAL_SAMPLE_COUNT];         // Initialized to 127 in settingsInit()
uint8_t sampleCrushArr[TOTAL_SAMPLE_COUNT] = {0};   // All default to 0 (clean)

// Display brightness
uint8_t displayBrightness = DISPLAY_BRIGHTNESS_DEFAULT;

// Pot default assignments (persisted to flash)
PotAssignment potDefaultAssignments[3] = {
  { PCAT_ENVELOPE, 1, TARGET_ALL, 0 },  // POT1: Envelope Decay → ALL
  { PCAT_ENVELOPE, 2, TARGET_ALL, 0 },  // POT2: Envelope Sustain → ALL
  { PCAT_ENVELOPE, 3, TARGET_ALL, 0 }   // POT3: Envelope Release → ALL
};

// Smoothing for pot readings
static const int POT_THRESHOLD = 20;  // ADC units before registering change
static const int POT_ADC_MAX = 1000;  // Actual pot range (10k pot reads ~0-1000)

// ============================================================================
// INITIALIZATION
// ============================================================================

void settingsInit() {
  for (uint8_t v = 0; v < 9; v++) {
    voiceSettings[v].detuneCents = 0;
    voiceSettings[v].octaveShift = 0;     // No octave shift
    voiceSettings[v].vibOn = 0;           // Off by default (use mod wheel)
    voiceSettings[v].vibRateTenths = 70;  // Default 7.0 Hz (in tenths)
    voiceSettings[v].vibDepthCents = 50;  // Default 0.5 semitone
    voiceSettings[v].vibDelay = 0;        // No delay before vibrato
    voiceSettings[v].noiseFreq = 15;
    voiceSettings[v].envAttack = 0;       // Instant attack
    voiceSettings[v].envDecay = 0;        // No decay
    voiceSettings[v].envSustain = 127;    // Full sustain
    voiceSettings[v].sidPwmRate = 0;      // No PWM sweep by default
    voiceSettings[v].sidWave = 0;         // Square wave
    voiceSettings[v].sidDuty = 11;        // Default duty cycle
    voiceSettings[v].maxVolume = 15;      // Full volume (no cap)
    voiceSettings[v].portaOn = 0;         // Portamento off by default
    voiceSettings[v].portaSpeed = 64;     // Medium glide speed
    voiceSettings[v].tremoloOn = 0;       // Tremolo off by default
    voiceSettings[v].tremoloRate = 70;    // Default 7.0 Hz
    voiceSettings[v].tremoloDepth = 50;   // Default 50% depth
    voiceSettings[v].pitchEnvAmt = 0;     // No pitch envelope
    voiceSettings[v].pitchEnvTime = 64;   // Medium speed
    voiceSettings[v].pitchEnvDir = 0;     // Down (start high, sweep to note)
    voiceSettings[v].sidPwmDepth = 0;     // No PWM depth
    voiceSettings[v].sidSync = 0;         // No hard sync
    voiceSettings[v].sidRing = 0;         // No ring mod
    voiceSettings[v].sidNoise = 0;        // No noise mix
    voiceSettings[v].envRelease = 0;      // No release (instant stop)

    // Initialize per-voice vibrato state
    vibPhase[v] = 0;
    vibStartTime[v] = 0;
    vibDelayMs[v] = 0;
  }

  for (uint8_t i = 0; i < 3; i++) {
    potValues[i] = 0;
    potLastValues[i] = 0;
  }

  // Initialize per-sample length to full (pitch/octave already zero-init)
  for (uint8_t i = 0; i < TOTAL_SAMPLE_COUNT; i++) {
    sampleLengthArr[i] = 127;
  }
}

// ============================================================================
// TARGET HELPERS
// ============================================================================

bool getTargetVoices(uint8_t &start, uint8_t &end) {
  switch (currentTarget) {
    case TARGET_ALL:   start = 0; end = 9; return true;
    case TARGET_CHIP0: start = 0; end = 3; return true;
    case TARGET_CHIP1: start = 3; end = 6; return true;
    case TARGET_CHIP2: start = 6; end = 9; return true;
    case TARGET_V1:    start = 0; end = 1; return true;
    case TARGET_V2:    start = 1; end = 2; return true;
    case TARGET_V3:    start = 2; end = 3; return true;
    case TARGET_V4:    start = 3; end = 4; return true;
    case TARGET_V5:    start = 4; end = 5; return true;
    case TARGET_V6:    start = 5; end = 6; return true;
    case TARGET_V7:    start = 6; end = 7; return true;
    case TARGET_V8:    start = 7; end = 8; return true;
    case TARGET_V9:    start = 8; end = 9; return true;
    default: return false;
  }
}

const char* getTargetName(SettingsTarget target) {
  switch (target) {
    case TARGET_ALL:   return "ALL";
    case TARGET_CHIP0: return "CH0";
    case TARGET_CHIP1: return "CH1";
    case TARGET_CHIP2: return "CH2";
    case TARGET_V1:    return "V1";
    case TARGET_V2:    return "V2";
    case TARGET_V3:    return "V3";
    case TARGET_V4:    return "V4";
    case TARGET_V5:    return "V5";
    case TARGET_V6:    return "V6";
    case TARGET_V7:    return "V7";
    case TARGET_V8:    return "V8";
    case TARGET_V9:    return "V9";
    default: return "?";
  }
}

// ============================================================================
// POT CATEGORY AND PARAMETER NAME LOOKUPS
// ============================================================================

const char* getPotCategoryName(PotCategory cat) {
  switch (cat) {
    case PCAT_OFF:       return "OFF";
    case PCAT_VOICE:     return "VOICE";
    case PCAT_VIBRATO:   return "VIBRATO";
    case PCAT_ENVELOPE:  return "ENVELOPE";
    case PCAT_TREMOLO:   return "TREMOLO";
    case PCAT_PITCH_ENV: return "PITCHENV";
    case PCAT_SID:       return "SID";
    case PCAT_FX_ECHO:   return "FX-ECHO";
    case PCAT_FX_ARP:    return "FX-ARP";
    case PCAT_FX_CRUSH:  return "FX-CRUSH";
    case PCAT_FX_REVERB: return "FX-RVRB";
    case PCAT_FX_CHORUS: return "FX-CHRS";
    case PCAT_SAMPLE:    return "SAMPLE";
    case PCAT_GLOBAL:    return "GLOBAL";
    default:             return "?";
  }
}

const char* getPotParamName(PotCategory cat, uint8_t idx) {
  switch (cat) {
    case PCAT_VOICE:
      switch (idx) {
        case 0: return "Detune";
        case 1: return "Octave";
        case 2: return "MaxVol";
        case 3: return "Noise";
        case 4: return "Slide";
        default: return "?";
      }
    case PCAT_VIBRATO:
      switch (idx) {
        case 0: return "Rate";
        case 1: return "Depth";
        case 2: return "Delay";
        default: return "?";
      }
    case PCAT_ENVELOPE:
      switch (idx) {
        case 0: return "Attack";
        case 1: return "Decay";
        case 2: return "Sustn";
        case 3: return "Rel";
        default: return "?";
      }
    case PCAT_TREMOLO:
      switch (idx) {
        case 0: return "Rate";
        case 1: return "Depth";
        default: return "?";
      }
    case PCAT_PITCH_ENV:
      switch (idx) {
        case 0: return "Amount";
        case 1: return "Time";
        case 2: return "Dir";
        default: return "?";
      }
    case PCAT_SID:
      switch (idx) {
        case 0: return "Wave";
        case 1: return "Duty";
        case 2: return "PWMRt";
        case 3: return "PWMDp";
        case 4: return "Detune";
        case 5: return "Rel";
        default: return "?";
      }
    case PCAT_FX_ECHO:
      switch (idx) {
        case 0: return "Delay";
        case 1: return "Repeat";
        case 2: return "Decay";
        case 3: return "Vol";
        default: return "?";
      }
    case PCAT_FX_ARP:
      switch (idx) {
        case 0: return "Speed";
        case 1: return "Patrn";
        case 2: return "Vol";
        case 3: return "Octave";
        default: return "?";
      }
    case PCAT_FX_CRUSH:
      switch (idx) {
        case 0: return "Bits";
        case 1: return "Rate";
        case 2: return "Vol";
        default: return "?";
      }
    case PCAT_FX_REVERB:
      switch (idx) {
        case 0: return "Taps";
        case 1: return "Space";
        case 2: return "Decay";
        case 3: return "Detune";
        case 4: return "Vol";
        default: return "?";
      }
    case PCAT_FX_CHORUS:
      switch (idx) {
        case 0: return "Dtune1";
        case 1: return "Dtune2";
        case 2: return "Vol";
        case 3: return "Rate";
        default: return "?";
      }
    case PCAT_SAMPLE:
      switch (idx) {
        case 0: return "Select";
        case 1: return "Mode";
        case 2: return "Vol";
        case 3: return "Pitch";
        case 4: return "Oct";
        case 5: return "Len";
        default: return "?";
      }
    case PCAT_GLOBAL:
      switch (idx) {
        case 0: return "Expr";
        case 1: return "Poly";
        default: return "?";
      }
    default:
      return "-";
  }
}

uint8_t getPotParamCount(PotCategory cat) {
  switch (cat) {
    case PCAT_OFF:       return 0;
    case PCAT_VOICE:     return PCAT_VOICE_PARAMS;
    case PCAT_VIBRATO:   return PCAT_VIBRATO_PARAMS;
    case PCAT_ENVELOPE:  return PCAT_ENVELOPE_PARAMS;
    case PCAT_TREMOLO:   return PCAT_TREMOLO_PARAMS;
    case PCAT_PITCH_ENV: return PCAT_PITCH_ENV_PARAMS;
    case PCAT_SID:       return PCAT_SID_PARAMS;
    case PCAT_FX_ECHO:   return PCAT_FX_ECHO_PARAMS;
    case PCAT_FX_ARP:    return PCAT_FX_ARP_PARAMS;
    case PCAT_FX_CRUSH:  return PCAT_FX_CRUSH_PARAMS;
    case PCAT_FX_REVERB: return PCAT_FX_REVERB_PARAMS;
    case PCAT_FX_CHORUS: return PCAT_FX_CHORUS_PARAMS;
    case PCAT_SAMPLE:    return PCAT_SAMPLE_PARAMS;
    case PCAT_GLOBAL:    return PCAT_GLOBAL_PARAMS;
    default:             return 0;
  }
}

bool categoryRequiresTarget(PotCategory cat) {
  // FX categories and SAMPLE/GLOBAL don't need voice targeting
  switch (cat) {
    case PCAT_OFF:
    case PCAT_FX_ECHO:
    case PCAT_FX_ARP:
    case PCAT_FX_CRUSH:
    case PCAT_FX_REVERB:
    case PCAT_FX_CHORUS:
    case PCAT_SAMPLE:
    case PCAT_GLOBAL:
      return false;
    default:
      return true;  // VOICE, VIBRATO, ENVELOPE, TREMOLO, PITCH_ENV, SID
  }
}

// ============================================================================
// APPLY SETTINGS TO VOICES
// ============================================================================

void applyDetune(int8_t cents) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].detuneCents = cents;
  }
}

void applyOctave(int8_t octaves) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].octaveShift = octaves;
  }
}

void applyVibOn(uint8_t intensity) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].vibOn = intensity;
  }
}

void applyVibRate(uint8_t rate) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].vibRateTenths = rate;
  }
}

void applyVibDepth(uint8_t depth) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].vibDepthCents = depth;
  }
}

void applyEnvAttack(uint8_t attack) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].envAttack = attack;
  }
}

void applyEnvDecay(uint8_t decay) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].envDecay = decay;
  }
}

void applyEnvSustain(uint8_t sustain) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].envSustain = sustain;
  }
}

void applySidWave(uint8_t wave) {
  // Only square (0) and pulse (3) are supported; clamp invalid values
  if (wave != 0 && wave != 3) wave = 0;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].sidWave = wave;

    // Update active SID voice state (block timer while modifying)
    if (sidModeGlobal && v >= 3 && v <= 8) {
      uint8_t sidIdx = v - 3;
      {
        uint32_t irq = save_and_disable_interrupts();
        ymBusBusy = true;
        restore_interrupts(irq);
      }
      sidState[sidIdx].waveform = wave;
      sidState[sidIdx].lastWrittenVol = 0xFF;  // Force fresh HW write
      ymBusBusy = false;
    }
  }
}

void applySidDuty(uint8_t duty) {
  if (duty > 15) duty = 15;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].sidDuty = duty;

    // If in global SID mode and this voice is on chips 1+2, update the active SID voice
    // AND update sidDutyChip[] so SID preset save captures the value
    if (sidModeGlobal && v >= 3 && v <= 8) {
      uint8_t sidIdx = v - 3;  // Map voice 3-8 to sidState[0-5]

      // Update sidDutyChip for this voice's chip (used by SID preset save)
      // Voices 3-5 (sidIdx 0-2) = chip 1 = sidDutyChip[0]
      // Voices 6-8 (sidIdx 3-5) = chip 2 = sidDutyChip[1]
      uint8_t chipIdx = (sidIdx < 3) ? 0 : 1;
      sidDutyChip[chipIdx] = duty;

      // Update the active SID voice state (block timer while modifying)
      {
        uint32_t irq = save_and_disable_interrupts();
        ymBusBusy = true;
        restore_interrupts(irq);
      }
      sidState[sidIdx].duty = duty;
      ymBusBusy = false;
    }
  }
}

void applySidPwmRate(uint8_t rate) {
  if (rate > 127) rate = 127;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].sidPwmRate = rate;

    if (sidModeGlobal && v >= 3 && v <= 8) {
      uint8_t sidIdx = v - 3;
      // Convert rate 0-127 to phaseInc for LFO
      // Rate 0 = off, 1 = ~0.1Hz, 127 = ~10Hz
      // Timer fires every 40µs per voice (alternating chips)
      // Full LFO cycle = 65536 phase units
      // phaseInc = rate * 65536 / (25000 * 10) ≈ rate * 0.26
      // Simplified: phaseInc = rate * 17 / 64 (close approximation)
      {
        uint32_t irq = save_and_disable_interrupts();
        ymBusBusy = true;
        restore_interrupts(irq);
      }
      sidState[sidIdx].pwmPhaseInc = (rate == 0) ? 0 : ((uint32_t)rate * 17) / 64 + 1;
      ymBusBusy = false;
    }
  }
}

void applySidPwmDepth(uint8_t depth) {
  if (depth > 15) depth = 15;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].sidPwmDepth = depth;

    if (sidModeGlobal && v >= 3 && v <= 8) {
      {
        uint32_t irq = save_and_disable_interrupts();
        ymBusBusy = true;
        restore_interrupts(irq);
      }
      sidState[v - 3].pwmDepth = depth;
      ymBusBusy = false;
    }
  }
}

void applySidNoise(uint8_t on) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  extern YM2149 ym;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].sidNoise = on ? 1 : 0;

    if (sidModeGlobal && v >= 3 && v <= 8) {
      uint8_t sidIdx = v - 3;

      // Update YM mixer register to enable/disable noise for this voice
      uint8_t chip = (sidIdx < 3) ? 1 : 2;

      // Block timer while modifying state and writing hardware
      {
        uint32_t irq = save_and_disable_interrupts();
        ymBusBusy = true;
        restore_interrupts(irq);
      }
      sidState[sidIdx].noiseOn = (on != 0);

      // Read current mixer state and update noise bit
      // Mixer reg 7: bits 0-2 = tone disable (always 1 in SID mode)
      //              bits 3-5 = noise disable (0=on, 1=off)
      // In SID mode tones are always disabled (bits 0-2 = 0b111)
      uint8_t mixer = 0b00111111;  // All disabled by default
      for (uint8_t i = 0; i < 3; i++) {
        uint8_t si = (chip == 1) ? i : 3 + i;
        if (sidState[si].noiseOn) {
          mixer &= ~(1 << (3 + i));  // Enable noise (clear bit)
        }
      }
      ym.selectYM(chip);
      ym.writeFast(7, mixer);
      ymBusBusy = false;
    }
  }
}

void applyEnvRelease(uint8_t release) {
  if (release > 127) release = 127;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].envRelease = release;
  }
}

void applyMaxVolume(uint8_t vol) {
  if (vol > 15) vol = 15;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].maxVolume = vol;
  }
}

void applyPortaOn(uint8_t on) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].portaOn = on ? 1 : 0;
  }
}

void applyPortaSpeed(uint8_t speed) {
  if (speed > 127) speed = 127;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].portaSpeed = speed;
  }
}

void applyVibDelay(uint8_t delay) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].vibDelay = delay;
  }
}

void applyTremoloOn(uint8_t intensity) {
  if (intensity > 127) intensity = 127;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].tremoloOn = intensity;
  }
}

void applyTremoloRate(uint8_t rate) {
  if (rate < 10) rate = 10;
  if (rate > 150) rate = 150;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].tremoloRate = rate;
  }
}

void applyTremoloDepth(uint8_t depth) {
  if (depth > 100) depth = 100;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].tremoloDepth = depth;
  }
}

void applyPitchEnvAmt(uint8_t amt) {
  if (amt > 24) amt = 24;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].pitchEnvAmt = amt;
  }
}

void applyPitchEnvTime(uint8_t time) {
  if (time > 127) time = 127;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].pitchEnvTime = time;
  }
}

void applyPitchEnvDir(uint8_t dir) {
  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].pitchEnvDir = dir ? 1 : 0;
  }
}

void applyChipLink(uint8_t chip, bool enabled) {
  if (chip >= 3) return;
  chipLink[chip] = enabled;
}

void syncLinkedChipSettings() {
  // Copy Chip 0 voice settings to linked chips (only for voices in link mask)
  for (uint8_t v = 0; v < 3; v++) {
    // Skip this voice if not in link mask
    if (!(voiceLinkMask & (1 << v))) continue;

    VoiceSettings &src = voiceSettings[v];  // Chip 0 voices 0,1,2

    // Copy to Chip 1 if linked
    if (linkMode == LINK_CH1 || linkMode == LINK_ALL) {
      voiceSettings[3 + v] = src;  // Chip 1 voices 3,4,5
    }

    // Copy to Chip 2 if linked
    if (linkMode == LINK_CH2 || linkMode == LINK_ALL) {
      voiceSettings[6 + v] = src;  // Chip 2 voices 6,7,8
    }
  }
}

void applyVoiceLinkMask(uint8_t mask) {
  voiceLinkMask = mask & VOICE_LINK_ALL;  // Ensure only bits 0-2 are set
  // Re-sync if linking is active
  if (linkMode != LINK_OFF) {
    syncLinkedChipSettings();
  }
}

void applyLinkMode(uint8_t mode) {
  if (mode > LINK_ALL) mode = LINK_ALL;
  linkMode = mode;

  // Update chipLink flags based on mode
  // Chip 0 is always linked when any linking is active (it's the master)
  chipLink[0] = (mode != LINK_OFF);
  chipLink[1] = (mode == LINK_CH1 || mode == LINK_ALL);
  chipLink[2] = (mode == LINK_CH2 || mode == LINK_ALL);

  // Sync settings from Chip 0 to linked chips
  if (mode != LINK_OFF) {
    syncLinkedChipSettings();
  }
}

void applyNoiseFreq(uint8_t freq) {
  if (freq > 31) freq = 31;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  // Noise is per-chip, so apply to each chip that has a voice in range
  for (uint8_t v = start; v < end; v++) {
    voiceSettings[v].noiseFreq = freq;
    uint8_t chip = v / 3;
    ym.write(chip, 0x06, freq);
  }
}

void applyVolume(uint8_t vol) {
  // Volume is handled via expression for now
  if (vol > 15) vol = 15;

  uint8_t start, end;
  if (!getTargetVoices(start, end)) return;

  // Map 0-15 to 0-127 expression range
  uint8_t expr = (vol * 127) / 15;
  for (uint8_t v = start; v < end; v++) {
    expressionVal[v] = expr;
  }
}

// ============================================================================
// POT VALUE APPLICATION
// ============================================================================

// Helper to apply value to targeted voices
static void applyToTarget(uint8_t target, void (*applyFunc)(uint8_t), uint8_t value) {
  SettingsTarget savedTarget = currentTarget;

  if (target == TARGET_NONE || target == TARGET_ALL) {
    currentTarget = TARGET_ALL;
    applyFunc(value);
  } else if (target >= TARGET_CHIP0 && target <= TARGET_V9) {
    currentTarget = (SettingsTarget)target;
    applyFunc(value);
  }

  currentTarget = savedTarget;
}

// Signed version for detune/octave
static void applyToTargetSigned(uint8_t target, void (*applyFunc)(int8_t), int8_t value) {
  SettingsTarget savedTarget = currentTarget;

  if (target == TARGET_NONE || target == TARGET_ALL) {
    currentTarget = TARGET_ALL;
    applyFunc(value);
  } else if (target >= TARGET_CHIP0 && target <= TARGET_V9) {
    currentTarget = (SettingsTarget)target;
    applyFunc(value);
  }

  currentTarget = savedTarget;
}

void applyPotValue(const PotAssignment& assign, uint8_t value) {
  // value is 0-255 (mapped from pot ADC)

  switch (assign.category) {
    case PCAT_OFF:
      // Do nothing
      break;

    case PCAT_VOICE:
      switch (assign.paramIndex) {
        case 0: // Detune (-50 to +50)
          applyToTargetSigned(assign.target, applyDetune, (int8_t)map(value, 0, 255, -50, 50));
          break;
        case 1: // Octave (-3 to +3)
          applyToTargetSigned(assign.target, applyOctave, (int8_t)map(value, 0, 255, -3, 3));
          break;
        case 2: // MaxVolume (0-15)
          applyToTarget(assign.target, applyMaxVolume, map(value, 0, 255, 0, 15));
          break;
        case 3: // NoiseFreq (0-31)
          applyToTarget(assign.target, applyNoiseFreq, map(value, 0, 255, 0, 31));
          break;
        case 4: // PortaSpeed (0-127)
          applyToTarget(assign.target, applyPortaSpeed, map(value, 0, 255, 0, 127));
          break;
      }
      break;

    case PCAT_VIBRATO:
      switch (assign.paramIndex) {
        case 0: // Rate (10-150)
          applyToTarget(assign.target, applyVibRate, map(value, 0, 255, 10, 150));
          break;
        case 1: // Depth (0-200)
          applyToTarget(assign.target, applyVibDepth, map(value, 0, 255, 0, 200));
          break;
        case 2: // Delay (0-255)
          applyToTarget(assign.target, applyVibDelay, value);
          break;
      }
      break;

    case PCAT_ENVELOPE:
      switch (assign.paramIndex) {
        case 0: // Attack (0-127)
          applyToTarget(assign.target, applyEnvAttack, value < 5 ? 0 : map(value, 5, 255, 0, 127));
          break;
        case 1: // Decay (0-127)
          applyToTarget(assign.target, applyEnvDecay, value < 5 ? 0 : map(value, 5, 255, 0, 127));
          break;
        case 2: // Sustain (0-127)
          applyToTarget(assign.target, applyEnvSustain, value < 5 ? 0 : map(value, 5, 255, 0, 127));
          break;
        case 3: // Release (0-127)
          applyToTarget(assign.target, applyEnvRelease, value < 5 ? 0 : map(value, 5, 255, 0, 127));
          break;
      }
      break;

    case PCAT_TREMOLO:
      switch (assign.paramIndex) {
        case 0: // Rate (10-150)
          applyToTarget(assign.target, applyTremoloRate, map(value, 0, 255, 10, 150));
          break;
        case 1: // Depth (0-100)
          applyToTarget(assign.target, applyTremoloDepth, map(value, 0, 255, 0, 100));
          break;
      }
      break;

    case PCAT_PITCH_ENV:
      switch (assign.paramIndex) {
        case 0: // Amount (0-24)
          applyToTarget(assign.target, applyPitchEnvAmt, map(value, 0, 255, 0, 24));
          break;
        case 1: // Time (0-127)
          applyToTarget(assign.target, applyPitchEnvTime, value >> 1);
          break;
        case 2: // Direction (0 or 1)
          applyToTarget(assign.target, applyPitchEnvDir, value > 127 ? 1 : 0);
          break;
      }
      break;

    case PCAT_SID:
      switch (assign.paramIndex) {
        case 0: // Wave (SQR=0, PLS=3)
          applyToTarget(assign.target, applySidWave, (value >= 128) ? 3 : 0);
          break;
        case 1: // Duty (0-15)
          applyToTarget(assign.target, applySidDuty, map(value, 0, 255, 0, 15));
          break;
        case 2: // PWM Rate (0-127)
          applyToTarget(assign.target, applySidPwmRate, value >> 1);
          break;
        case 3: // PWM Depth (0-15)
          applyToTarget(assign.target, applySidPwmDepth, map(value, 0, 255, 0, 15));
          break;
        case 4: // Detune - reuse voice detune
          applyToTargetSigned(assign.target, applyDetune, (int8_t)map(value, 0, 255, -50, 50));
          break;
        case 5: // Release (0-127)
          applyToTarget(assign.target, applyEnvRelease, value < 5 ? 0 : map(value, 5, 255, 0, 127));
          break;
      }
      break;

    // FX categories - global only (no targeting)
    case PCAT_FX_ECHO:
      switch (assign.paramIndex) {
        case 0: // Delay (50-2000ms)
          echoDelayMs = map(value, 0, 255, 50, 2000);
          break;
        case 1: // Repeats (1-10)
          echoRepeats = map(value, 0, 255, 1, 10);
          break;
        case 2: // Decay (1-15)
          echoDecay = map(value, 0, 255, 1, 15);
          break;
        case 3: // Volume (1-15)
          echoVolume = map(value, 0, 255, 1, 15);
          break;
      }
      break;

    case PCAT_FX_ARP:
      switch (assign.paramIndex) {
        case 0: // Speed (50-500ms)
          arpSpeedMs = map(value, 0, 255, 50, 500);
          break;
        case 1: // Pattern (0-3)
          arpPattern = value >> 6;
          break;
        case 2: // Volume (1-15)
          arpVolume = map(value, 0, 255, 1, 15);
          break;
        case 3: // Octave (-2 to +2)
          arpOctave = map(value, 0, 255, -2, 2);
          break;
      }
      break;

    case PCAT_FX_CRUSH:
      switch (assign.paramIndex) {
        case 0: // Bits (1-4)
          bitCrushBits = map(value, 0, 255, 1, 4);
          break;
        case 1: // Rate (1-10)
          bitCrushRate = map(value, 0, 255, 1, 10);
          break;
        case 2: // Volume (1-15)
          bitCrushVolume = map(value, 0, 255, 1, 15);
          break;
      }
      break;

    case PCAT_FX_REVERB:
      switch (assign.paramIndex) {
        case 0: // Taps (2-6)
          reverbTaps = map(value, 0, 255, 2, 6);
          break;
        case 1: // Spacing (20-100)
          reverbSpacing = map(value, 0, 255, 20, 100);
          break;
        case 2: // Decay (1-8)
          reverbDecay = map(value, 0, 255, 1, 8);
          break;
        case 3: // Detune (-5 to +5)
          reverbDetune = map(value, 0, 255, -5, 5);
          break;
        case 4: // Volume (1-15)
          reverbVolume = map(value, 0, 255, 1, 15);
          break;
      }
      break;

    case PCAT_FX_CHORUS:
      switch (assign.paramIndex) {
        case 0: // Detune1 (-50 to +50)
          chorusDetune1 = map(value, 0, 255, -50, 50);
          break;
        case 1: // Detune2 (-50 to +50)
          chorusDetune2 = map(value, 0, 255, -50, 50);
          break;
        case 2: // Volume (1-15)
          chorusVolume = map(value, 0, 255, 1, 15);
          break;
        case 3: // Rate (0-80 tenths of Hz)
          chorusRate = map(value, 0, 255, 0, 80);
          break;
      }
      break;

    case PCAT_SAMPLE:
      switch (assign.paramIndex) {
        case 0: // Select (0-15)
          sampleSelect = map(value, 0, 255, 0, getSectionSampleCount(sampleSection) - 1);
          break;
        case 1: // Mode (0-3)
          sampleMode = value >> 6;
          break;
        case 2: // Volume (1-15)
          sampleVolume = map(value, 0, 255, 1, 15);
          break;
        case 3: { // Pitch (-12 to +12) per sample
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          samplePitchArr[flat] = map(value, 0, 255, -12, 12);
          break;
        }
        case 4: { // Octave (-3 to +3) per sample
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          sampleOctaveArr[flat] = map(value, 0, 255, -3, 3);
          break;
        }
        case 5: { // Length (1-127) per sample
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          sampleLengthArr[flat] = map(value, 0, 255, 1, 127);
          break;
        }
      }
      break;

    case PCAT_GLOBAL:
      switch (assign.paramIndex) {
        case 0: // Expression (0-127)
          {
            uint8_t expr = value >> 1;
            for (uint8_t v = 0; v < 9; v++) {
              expressionVal[v] = expr;
            }
          }
          break;
        case 1: // PolyMode (0-2) - would need to call applyModeFromIndex
          // Skip for now - mode change is complex
          break;
      }
      break;

    default:
      break;
  }
}

// ============================================================================
// POT READING AND PARAMETER MAPPING
// ============================================================================

void updatePots() {
  static unsigned long lastPotRead = 0;
  unsigned long now = millis();

  // Only read pots every 20ms to avoid noise
  if (now - lastPotRead < 20) return;
  lastPotRead = now;

  // Hardware mapping: Pot1=C3, Pot2=C4, Pot3=C5
  static const uint8_t potMuxChannels[3] = {3, 4, 5};

  for (uint8_t i = 0; i < 3; i++) {
    // Read from multiplexer using correct channel mapping
    int raw = readMuxAnalog(potMuxChannels[i]);

    // Simple smoothing
    potValues[i] = (potValues[i] * 3 + raw) / 4;

    // Check if value changed significantly
    if (abs(potValues[i] - potLastValues[i]) > POT_THRESHOLD) {
      potLastValues[i] = potValues[i];

      // Skip if pot is disabled
      if (potAssignments[i].category == PCAT_OFF) continue;

      // Map pot value to 0-255 range for applyPotValue
      int value = constrain(potValues[i], 0, POT_ADC_MAX);
      uint8_t mappedValue = (uint8_t)map(value, 0, POT_ADC_MAX, 0, 255);

      // Apply the value using the assignment's category/param/target
      applyPotValue(potAssignments[i], mappedValue);
    }
  }
}
