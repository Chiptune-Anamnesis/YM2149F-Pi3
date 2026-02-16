#include "preset.h"
#include "voice_manager.h"
#include "fx_chip.h"
#include "sid_mode.h"
#include "sample_player.h"
#include "dual_core.h"
#include <hardware/flash.h>
#include <hardware/sync.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

uint8_t currentPresetIndex = PRESET_INDEX_NONE;

// ============================================================================
// FACTORY PRESET NAMES
// ============================================================================

const char* factoryPresetNames[PRESET_FACTORY_COUNT] = {
  "INIT",     // F01 - Clean starting point
  "ACIDLP",   // F02 - Acid lead/bass with SID duty sweep
  "SPACER",   // F03 - Cosmic shimmer with extreme reverb
  "DRONEO",   // F04 - Evolving textural drone
  "BELLTR",   // F05 - Metallic bell tones with pitch env
  "SKAREE",   // F06 - Sci-fi horror atmosphere
  "RETRO8",   // F07 - 8-bit video game style
  "MORPHO"    // F08 - Morphing organic texture
};

// ============================================================================
// FACTORY PRESETS (stored in program flash, read-only)
// Each preset showcases different device features with meaningful pot mappings
// ============================================================================

// Helper to create a preset with full customization
static PresetData createPreset(
  const char* name,
  // Voice params (applied to all 9 voices)
  int8_t detune, int8_t octave, uint8_t attack, uint8_t decay, uint8_t sustain,
  uint8_t vibOn, uint8_t vibRate, uint8_t vibDepth, uint8_t vibDelay,
  uint8_t sidOn, uint8_t sidWave, uint8_t sidDuty,
  uint8_t tremoloOn, uint8_t tremoloRate, uint8_t tremoloDepth,
  uint8_t portaOn, uint8_t portaSpeed,
  uint8_t pitchEnvAmt, uint8_t pitchEnvTime, uint8_t pitchEnvDir,
  // Pot assignments
  PotCategory pot0Cat, uint8_t pot0Param, uint8_t pot0Target,
  PotCategory pot1Cat, uint8_t pot1Param, uint8_t pot1Target,
  PotCategory pot2Cat, uint8_t pot2Param, uint8_t pot2Target,
  // FX settings
  bool fxEnabled, uint8_t fxType, uint8_t fxRouting,
  uint16_t echoDelay, uint8_t echoRepeats, uint8_t echoDecay, uint8_t echoVol,
  uint8_t arpPattern, uint16_t arpSpeed, uint8_t arpVol, int8_t arpOct,
  uint8_t reverbTaps, uint8_t reverbSpacing, uint8_t reverbDecay, int8_t reverbDetune, uint8_t reverbVol,
  int8_t chorusDetune1, int8_t chorusDetune2, uint8_t chorusVol, uint16_t chorusDur,
  uint8_t crushBits, uint8_t crushRate, uint8_t crushVol,
  // Global
  uint8_t polyMode, uint8_t linkMode
) {
  PresetData p;
  p.magic = PRESET_MAGIC;
  p.version = PRESET_VERSION;
  p.flags = PRESET_FLAG_USED;
  memset(p.name, 0, PRESET_NAME_LEN);
  strncpy(p.name, name, PRESET_NAME_LEN);

  // Voice settings for all 9 voices
  for (int i = 0; i < 9; i++) {
    p.voices[i].detuneCents = detune;
    p.voices[i].octaveShift = octave;
    p.voices[i].vibOn = vibOn;
    p.voices[i].vibRateTenths = vibRate;
    p.voices[i].vibDepthCents = vibDepth;
    p.voices[i].vibDelay = vibDelay;
    p.voices[i].noiseFreq = 15;
    p.voices[i].envAttack = attack;
    p.voices[i].envDecay = decay;
    p.voices[i].envSustain = sustain;
    p.voices[i].sidOn = sidOn;
    p.voices[i].sidWave = sidWave;
    p.voices[i].sidDuty = sidDuty;
    p.voices[i].maxVolume = 15;
    p.voices[i].portaOn = portaOn;
    p.voices[i].portaSpeed = portaSpeed;
    p.voices[i].tremoloOn = tremoloOn;
    p.voices[i].tremoloRate = tremoloRate;
    p.voices[i].tremoloDepth = tremoloDepth;
    p.voices[i].pitchEnvAmt = pitchEnvAmt;
    p.voices[i].pitchEnvTime = pitchEnvTime;
    p.voices[i].pitchEnvDir = pitchEnvDir;
  }

  // Pot assignments
  p.pots[0] = { pot0Cat, pot0Param, pot0Target, 0 };
  p.pots[1] = { pot1Cat, pot1Param, pot1Target, 0 };
  p.pots[2] = { pot2Cat, pot2Param, pot2Target, 0 };

  // FX settings
  p.fxEnabled = fxEnabled;
  p.fxType = fxType;
  p.fxRouting = fxRouting;
  p.echoDelayMs = echoDelay;
  p.echoRepeats = echoRepeats;
  p.echoDecay = echoDecay;
  p.echoVolume = echoVol;
  p.arpPattern = arpPattern;
  p.arpSpeedMs = arpSpeed;
  p.arpVolume = arpVol;
  p.arpOctave = arpOct;
  p.bitCrushBits = crushBits;
  p.bitCrushRate = crushRate;
  p.bitCrushVolume = crushVol;
  p.bitCrushDuration = 100;
  p.reverbTaps = reverbTaps;
  p.reverbSpacing = reverbSpacing;
  p.reverbDecay = reverbDecay;
  p.reverbDetune = reverbDetune;
  p.reverbVolume = reverbVol;
  p.chorusDetune1 = chorusDetune1;
  p.chorusDetune2 = chorusDetune2;
  p.chorusVolume = chorusVol;
  p.chorusDuration = chorusDur;

  // SID mode
  p.sidMode = (sidOn != 0);
  p.sidEnvFreqCoarse = 8;
  p.sidEnvFreqFine = 0;
  p.sidEnvShape = 0;

  // Sample player
  p.sampleSelect = 0;
  p.sampleMode = SAMPLE_MODE_MAPPED;
  p.sampleVolume = 15;
  p.sampleSeqIndex = 0;

  // Global
  p.polyMode = polyMode;
  p.linkMode = linkMode;
  p.voiceLinkMask = 0;
  p.chipLink[0] = false;
  p.chipLink[1] = false;
  p.chipLink[2] = false;

  p.crc16 = 0;
  p.reserved[0] = 0;
  p.reserved[1] = 0;

  return p;
}

// Helper to configure voice linking properly
// linkMode: LINK_OFF, LINK_CH1, LINK_CH2, or LINK_ALL
// voiceMask: VOICE_LINK_A (0x01), VOICE_LINK_B (0x02), VOICE_LINK_C (0x04), or combinations
static void applyLinkConfig(PresetData& p, uint8_t lnkMode, uint8_t voiceMask) {
  p.linkMode = lnkMode;
  p.voiceLinkMask = voiceMask;
  // Set chipLink based on linkMode (same logic as setLinkMode in settings.cpp)
  p.chipLink[0] = (lnkMode != LINK_OFF);
  p.chipLink[1] = (lnkMode == LINK_CH1 || lnkMode == LINK_ALL);
  p.chipLink[2] = (lnkMode == LINK_CH2 || lnkMode == LINK_ALL);
}

// Helper to apply detune spread across Chip 0 and Chip 1 voices (not Chip 2 - that's for FX)
static void applyDetuneSpread(PresetData& p, int8_t spread) {
  // Chip 0: spread across 3 voices
  p.voices[0].detuneCents = (int8_t)(-spread);
  p.voices[1].detuneCents = 0;
  p.voices[2].detuneCents = spread;
  // Chip 1: tighter spread (linked to Chip 0)
  p.voices[3].detuneCents = (int8_t)(-spread / 2);
  p.voices[4].detuneCents = 0;
  p.voices[5].detuneCents = (int8_t)(spread / 2);
  // Chip 2 voices keep default (used for FX, not detuned unison)
  p.voices[6].detuneCents = 0;
  p.voices[7].detuneCents = 0;
  p.voices[8].detuneCents = 0;
}

// Helper to apply octave spread across Chip 0 and Chip 1 only
static void applyOctaveSpread(PresetData& p, int8_t baseOct) {
  // Chip 0: base, base, base+1
  p.voices[0].octaveShift = baseOct;
  p.voices[1].octaveShift = baseOct;
  p.voices[2].octaveShift = baseOct + 1;
  // Chip 1: base, base+1, base (varied)
  p.voices[3].octaveShift = baseOct;
  p.voices[4].octaveShift = baseOct + 1;
  p.voices[5].octaveShift = baseOct;
  // Chip 2: keep at base (FX chip)
  p.voices[6].octaveShift = baseOct;
  p.voices[7].octaveShift = baseOct;
  p.voices[8].octaveShift = baseOct;
}

// Helper to vary vibrato per voice for organic movement (Chip 0 and Chip 1)
static void applyVibratoVariation(PresetData& p) {
  // Chip 0: varied vibrato
  p.voices[0].vibRateTenths = 32; p.voices[0].vibDepthCents = 35;
  p.voices[1].vibRateTenths = 38; p.voices[1].vibDepthCents = 40;
  p.voices[2].vibRateTenths = 35; p.voices[2].vibDepthCents = 30;
  // Chip 1: slightly different variation
  p.voices[3].vibRateTenths = 40; p.voices[3].vibDepthCents = 45;
  p.voices[4].vibRateTenths = 30; p.voices[4].vibDepthCents = 35;
  p.voices[5].vibRateTenths = 36; p.voices[5].vibDepthCents = 38;
  // Chip 2: moderate vibrato (FX voices)
  p.voices[6].vibRateTenths = 35; p.voices[6].vibDepthCents = 30;
  p.voices[7].vibRateTenths = 35; p.voices[7].vibDepthCents = 30;
  p.voices[8].vibRateTenths = 35; p.voices[8].vibDepthCents = 30;
}

// Factory presets - each showcases different features with relevant pot mappings
PresetData factoryPresets[PRESET_FACTORY_COUNT];

// Initialize factory presets with voice layering
// Structure: Chip 0 (2-3 voices) linked to Chip 1, Chip 2 for FX
void initFactoryPresets() {
  // F01: INIT - Clean starting point, no linking
  // Pots: Detune (voice thickness), Attack (envelope), Sustain (envelope)
  factoryPresets[0] = createPreset("INIT",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, 0, 0, 40, 100, 0, 50, 30, 0,
    // SID: on, wave, duty
    0, 0, 8,
    // Tremolo: on, rate, depth
    0, 50, 30,
    // Porta: on, speed
    0, 64,
    // PitchEnv: amt, time, dir
    0, 64, 0,
    // Pots: cat, param, target (x3)
    PCAT_VOICE, 0, TARGET_ALL,      // Pot1: Detune
    PCAT_ENVELOPE, 0, TARGET_ALL,   // Pot2: Attack
    PCAT_ENVELOPE, 2, TARGET_ALL,   // Pot3: Sustain
    // FX: enabled, type, routing
    false, FX_NONE, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol
    200, 3, 4, 10,
    // Arp: pattern, speed, vol, oct
    ARP_UP, 100, 12, 0,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur
    -15, 15, 10, 0,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);

  // F02: ACIDLP - Acid lead/bass with aggressive modulation
  // Pots: Vibrato Rate (wobble), Vibrato Depth, Porta Speed (slide)
  factoryPresets[1] = createPreset("ACIDLP",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, -1, 0, 45, 80, 90, 80, 50, 0,
    // SID: off
    0, 0, 8,
    // Tremolo: on, rate, depth - rhythmic pumping
    60, 70, 40,
    // Porta: on, speed - essential for acid slides
    1, 95,
    // PitchEnv: amt, time, dir - pitch swoop down
    5, 85, 1,
    // Pots: cat, param, target (x3)
    PCAT_VIBRATO, 0, TARGET_ALL,    // Pot1: Vibrato Rate - wobble intensity
    PCAT_VIBRATO, 1, TARGET_ALL,    // Pot2: Vibrato Depth
    PCAT_VOICE, 4, TARGET_ALL,      // Pot3: Porta Speed (PCAT_VOICE index 4)
    // FX: enabled, type=ECHO, routing - slapback echo (ALL for linked voices)
    true, FX_ECHO, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol - tight slapback
    100, 2, 6, 9,
    // Arp: pattern, speed, vol, oct
    ARP_UP, 100, 12, 0,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur
    -15, 15, 10, 0,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // Link Chip 0 to Chip 1 for 6-voice unison thickness
  applyLinkConfig(factoryPresets[1], LINK_CH1, VOICE_LINK_ALL);
  applyDetuneSpread(factoryPresets[1], 8);

  // F03: SPACER - Cosmic shimmer with extreme reverb and wild detune
  // Pots: Reverb Detune (shimmer), Reverb Taps (density), Vibrato Depth (wobble)
  factoryPresets[2] = createPreset("SPACER",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, 1, 80, 10, 100, 100, 20, 60, 20,
    // SID: on, wave, duty
    0, 0, 8,
    // Tremolo: on, rate, depth - slow cosmic pulse
    50, 15, 35,
    // Porta: on, speed
    0, 64,
    // PitchEnv: amt, time, dir - gentle rise
    2, 60, 0,
    // Pots: cat, param, target (x3)
    PCAT_FX_REVERB, 3, TARGET_NONE, // Pot1: Reverb Detune (shimmer amount)
    PCAT_FX_REVERB, 0, TARGET_NONE, // Pot2: Reverb Taps (density/thickness)
    PCAT_VIBRATO, 1, TARGET_ALL,    // Pot3: Vibrato Depth (cosmic wobble)
    // FX: enabled, type=PSEUDO_REVERB, routing (ALL for linked voices)
    true, FX_PSEUDO_REVERB, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol
    200, 3, 4, 10,
    // Arp: pattern, speed, vol, oct
    ARP_UP, 100, 12, 0,
    // Reverb: taps=max, spacing=wide, decay=slow, detune=extreme, vol
    6, 60, 2, 5, 13,
    // Chorus: det1, det2, vol, dur
    -20, 20, 12, 0,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // Link Chip 0 to Chip 1 for massive 6-voice pad
  applyLinkConfig(factoryPresets[2], LINK_CH1, VOICE_LINK_ALL);
  // Wide detune for cosmic spread
  applyDetuneSpread(factoryPresets[2], 30);
  // Varied vibrato for organic shimmer
  applyVibratoVariation(factoryPresets[2]);

  // F04: DRONEO - Evolving textural drone with bit crush artifacts
  // Pots: Crush Bits (texture), Crush Rate (glitch amount), Tremolo Rate (pulse)
  factoryPresets[3] = createPreset("DRONEO",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, -1, 30, 10, 100, 60, 12, 30, 50,
    // SID: on, wave=triangle for smooth base, duty
    0, 0, 8,
    // Tremolo: on, rate, depth - slow pulsing
    80, 8, 50,
    // Porta: on, speed
    1, 50,
    // PitchEnv: amt, time, dir
    0, 64, 0,
    // Pots: cat, param, target (x3)
    PCAT_FX_CRUSH, 0, TARGET_NONE,  // Pot1: Crush Bits (texture density)
    PCAT_FX_CRUSH, 1, TARGET_NONE,  // Pot2: Crush Rate (glitch rate)
    PCAT_TREMOLO, 0, TARGET_ALL,    // Pot3: Tremolo Rate (pulse speed)
    // FX: enabled, type=BIT_CRUSH, routing (ALL for linked voices)
    true, FX_BIT_CRUSH, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol
    200, 3, 4, 10,
    // Arp: pattern, speed, vol, oct
    ARP_UP, 100, 12, 0,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur
    -15, 15, 10, 0,
    // Crush: bits=lo-fi, rate=slow, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // Link all chips for maximum drone thickness
  applyLinkConfig(factoryPresets[3], LINK_CH1, VOICE_LINK_ALL);
  // Wide detune for rich harmonics
  applyDetuneSpread(factoryPresets[3], 25);
  // Octave spread for depth
  applyOctaveSpread(factoryPresets[3], 0);

  // F05: BELLTR - Metallic bell/chime tones with pitch envelope
  // Pots: Pitch Env Amount (bell strike), Decay (ring time), Chorus Detune (shimmer)
  factoryPresets[4] = createPreset("BELLTR",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, 1, 0, 90, 20, 30, 60, 15, 30,
    // SID: on, wave, duty - pure tone for bell character
    0, 0, 8,
    // Tremolo: on, rate, depth - subtle shimmer
    40, 45, 20,
    // Porta: on, speed
    0, 64,
    // PitchEnv: amt, time, dir - downward strike, the "bell" character
    8, 95, 1,
    // Pots: cat, param, target (x3)
    PCAT_PITCH_ENV, 0, TARGET_ALL,  // Pot1: Pitch Env Amount (bell intensity)
    PCAT_ENVELOPE, 1, TARGET_ALL,   // Pot2: Decay (ring time)
    PCAT_FX_CHORUS, 0, TARGET_NONE, // Pot3: Chorus Detune (shimmer/spread)
    // FX: enabled, type=CHORUS, routing (ALL for linked voices)
    true, FX_CHORUS, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol
    200, 3, 4, 10,
    // Arp: pattern, speed, vol, oct
    ARP_UP, 100, 12, 0,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur - slight detuning for bell shimmer
    -12, 8, 11, 200,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // Link only 2 voices per chip for clarity
  applyLinkConfig(factoryPresets[4], LINK_CH1, VOICE_LINK_A | VOICE_LINK_B);
  // Octave spread for rich bell harmonics
  applyOctaveSpread(factoryPresets[4], 0);
  // Minimal detune to keep bell clarity
  applyDetuneSpread(factoryPresets[4], 5);

  // F06: SKAREE - Sci-fi/horror atmosphere with dissonant elements
  // Pots: Vibrato Rate (unease), Vibrato Depth, Echo Repeats (haunting)
  factoryPresets[5] = createPreset("SKAREE",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, 0, 40, 30, 80, 100, 25, 70, 0,
    // SID: off
    0, 0, 8,
    // Tremolo: on, rate, depth - unsettling pulse
    70, 18, 60,
    // Porta: on, speed - creepy slides
    1, 40,
    // PitchEnv: amt, time, dir - eerie pitch sag
    3, 70, 1,
    // Pots: cat, param, target (x3)
    PCAT_VIBRATO, 0, TARGET_ALL,    // Pot1: Vibrato Rate (unease)
    PCAT_VIBRATO, 1, TARGET_ALL,    // Pot2: Vibrato Depth (unease level)
    PCAT_FX_ECHO, 1, TARGET_NONE,   // Pot3: Echo Repeats (haunting tails)
    // FX: enabled, type=ECHO, routing (ALL for linked voices)
    true, FX_ECHO, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol - long haunting echoes
    400, 6, 3, 10,
    // Arp: pattern, speed, vol, oct
    ARP_RANDOM, 150, 12, 0,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur
    -15, 15, 10, 0,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // Link for thick eerie pad
  applyLinkConfig(factoryPresets[5], LINK_CH1, VOICE_LINK_ALL);
  // Dissonant detune spread - intentionally harsh
  applyDetuneSpread(factoryPresets[5], 35);
  // Varied vibrato for organic creepiness
  applyVibratoVariation(factoryPresets[5]);

  // F07: RETRO8 - 8-bit video game style with arp and plucky character
  // Pots: Arp Speed (game tempo), Arp Pattern (melody style), Pitch Env (blip)
  factoryPresets[6] = createPreset("RETRO8",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, 0, 0, 60, 30, 0, 50, 30, 0,
    // SID: off
    0, 0, 8,
    // Tremolo: on, rate, depth
    0, 50, 30,
    // Porta: on, speed
    0, 64,
    // PitchEnv: amt, time, dir - pitch blip up for plucky game SFX
    4, 100, 0,
    // Pots: cat, param, target (x3)
    PCAT_FX_ARP, 0, TARGET_NONE,    // Pot1: Arp Speed (game tempo)
    PCAT_FX_ARP, 1, TARGET_NONE,    // Pot2: Arp Pattern (melody type)
    PCAT_PITCH_ENV, 0, TARGET_ALL,  // Pot3: Pitch Env Amount (blip intensity)
    // FX: enabled, type=ARP, routing
    true, FX_ARP, FX_ROUTE_CHIP0,
    // Echo: delay, reps, decay, vol
    200, 3, 4, 10,
    // Arp: pattern=UP, speed=fast for 8-bit, vol, oct=+1
    ARP_UP, 60, 13, 1,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur
    -15, 15, 10, 0,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // No linking - clean voices for classic game sound

  // F08: MORPHO - Morphing organic texture with layered movement
  // Pots: Tremolo Rate (pulse), Vibrato Rate (wobble), Chorus Detune (width)
  factoryPresets[7] = createPreset("MORPHO",
    // Voice: detune, oct, atk, dec, sus, vibOn, vibRate, vibDepth, vibDelay
    0, 0, 60, 20, 100, 100, 40, 45, 10,
    // SID: off
    0, 0, 8,
    // Tremolo: on, rate, depth - rhythmic pumping
    100, 35, 55,
    // Porta: on, speed - smooth morphing transitions
    1, 60,
    // PitchEnv: amt, time, dir
    0, 64, 0,
    // Pots: cat, param, target (x3)
    PCAT_TREMOLO, 0, TARGET_ALL,    // Pot1: Tremolo Rate (pulse speed)
    PCAT_VIBRATO, 0, TARGET_ALL,    // Pot2: Vibrato Rate (wobble speed)
    PCAT_FX_CHORUS, 0, TARGET_NONE, // Pot3: Chorus Detune (stereo width)
    // FX: enabled, type=CHORUS, routing (ALL for linked voices)
    true, FX_CHORUS, FX_ROUTE_ALL,
    // Echo: delay, reps, decay, vol
    200, 3, 4, 10,
    // Arp: pattern, speed, vol, oct
    ARP_UP, 100, 12, 0,
    // Reverb: taps, spacing, decay, detune, vol
    4, 40, 3, 2, 10,
    // Chorus: det1, det2, vol, dur - wide asymmetric detune
    -30, 20, 12, 0,
    // Crush: bits, rate, vol
    2, 3, 12,
    // Global: polyMode=POLY(1), linkMode
    1, LINK_OFF);
  // Full 6-voice thickness
  applyLinkConfig(factoryPresets[7], LINK_CH1, VOICE_LINK_ALL);
  // Heavy varied vibrato for organic movement
  applyVibratoVariation(factoryPresets[7]);
  // Wide detune for lush texture
  applyDetuneSpread(factoryPresets[7], 20);
  // Octave layering for depth
  applyOctaveSpread(factoryPresets[7], 0);
}

// ============================================================================
// CRC16 CALCULATION
// ============================================================================

uint16_t presetCalcCRC(const PresetData& p) {
  // Simple CRC16-CCITT
  uint16_t crc = 0xFFFF;
  const uint8_t* data = (const uint8_t*)&p;
  size_t len = sizeof(PresetData) - 4;  // Exclude crc16 and reserved

  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// ============================================================================
// FLASH OPERATIONS
// ============================================================================

// Read preset header from flash
static void readHeader(PresetHeader& header) {
  const uint8_t* flash = (const uint8_t*)PRESET_FLASH_BASE;
  memcpy(&header, flash, sizeof(PresetHeader));
}

// Read user preset from flash
static bool readUserPreset(uint8_t userSlot, PresetData& p) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  // Calculate offset: header (16 bytes) + slot * sizeof(PresetData)
  uint32_t offset = sizeof(PresetHeader) + (userSlot * sizeof(PresetData));
  const uint8_t* flash = (const uint8_t*)(PRESET_FLASH_BASE + offset);

  memcpy(&p, flash, sizeof(PresetData));

  // Validate magic
  if (p.magic != PRESET_MAGIC) return false;

  // Validate CRC
  uint16_t expectedCRC = p.crc16;
  if (presetCalcCRC(p) != expectedCRC) return false;

  return true;
}

// Static buffer to avoid stack overflow (4KB is too much for stack)
static uint8_t flashBuffer[PRESET_SECTOR_SIZE];

// Write data to flash (must be called from Core 0)
// Uses cooperative pause to ensure Core 1 is not in I2C transaction
void presetWriteFlash(uint32_t offset, const uint8_t* data, size_t len) {
  uint32_t flash_offset = PRESET_FLASH_BASE - XIP_BASE + offset;

  // Ensure 256-byte page alignment for writing
  size_t aligned_len = ((len + 255) / 256) * 256;

  // Use static buffer
  memset(flashBuffer, 0xFF, PRESET_SECTOR_SIZE);
  memcpy(flashBuffer, data, len);

  // ===== COOPERATIVE PAUSE MECHANISM =====
  // Step 1: Request Core 1 to pause at a safe point (between display updates)
  flashPauseRequested = true;

  // Step 2: Wait for Core 1 to reach its safe pause point
  // Core 1 will set core1Paused=true when it's NOT in the middle of I2C
  unsigned long waitStart = millis();
  while (!core1Paused) {
    // Timeout after 500ms to avoid infinite hang
    if (millis() - waitStart > 500) {
      // Core 1 didn't respond - proceed anyway (last resort)
      break;
    }
    delayMicroseconds(100);
  }

  // Step 3: Now Core 1 is safely paused in a busy-wait loop
  // It's safe to call idleOtherCore because Core 1 is at a known point
  noInterrupts();
  rp2040.idleOtherCore();

  // Erase sector (4KB)
  flash_range_erase(flash_offset, PRESET_SECTOR_SIZE);

  // Write data
  flash_range_program(flash_offset, flashBuffer, aligned_len);

  // Resume other core
  rp2040.resumeOtherCore();
  interrupts();

  // Step 4: Signal Core 1 it can continue
  flashPauseRequested = false;

  // Give Core 1 a moment to resume before we continue
  delay(10);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void presetInit() {
  // Initialize factory presets with voice layering
  initFactoryPresets();

  PresetHeader header;
  readHeader(header);

  // Check if flash has valid header
  if (header.magic != PRESET_HEADER_MAGIC || header.version != PRESET_VERSION) {
    // Initialize fresh header
    header.magic = PRESET_HEADER_MAGIC;
    header.version = PRESET_VERSION;
    header.activePreset = PRESET_INDEX_NONE;
    header.presetCount = 0;
    header.midiSynthChannel = MIDI_CHANNEL_OMNI;  // Default to OMNI
    header.midiDrumChannel = 9;                    // Default to channel 10
    memset(header.reserved, 0, sizeof(header.reserved));

    // Write header to flash
    presetWriteFlash(0, (const uint8_t*)&header, sizeof(PresetHeader));
  } else {
    // Load global MIDI settings from header
    // Check for valid data (not erased flash 0xFF meaning uninitialized)
    if (header.midiSynthChannel <= 15 ||
        header.midiSynthChannel == MIDI_CHANNEL_OFF ||
        header.midiSynthChannel == MIDI_CHANNEL_OMNI) {
      midiSynthChannel = header.midiSynthChannel;
    }
    if (header.midiDrumChannel <= 15 || header.midiDrumChannel == MIDI_CHANNEL_OFF) {
      midiDrumChannel = header.midiDrumChannel;
    }
  }

  // Always start with no preset active - user must explicitly load one
  currentPresetIndex = PRESET_INDEX_NONE;
}

// ============================================================================
// CAPTURE CURRENT STATE
// ============================================================================

void presetCaptureCurrent(PresetData& p) {
  p.magic = PRESET_MAGIC;
  p.version = PRESET_VERSION;
  p.flags = PRESET_FLAG_USED;

  // Voice settings
  for (int i = 0; i < 9; i++) {
    p.voices[i] = voiceSettings[i];
  }

  // Pot assignments
  for (int i = 0; i < 3; i++) {
    p.pots[i] = potAssignments[i];
  }

  // FX state
  p.fxEnabled = fxModeEnabled;
  p.fxType = fxType;
  p.fxRouting = fxRouting;
  p.echoDelayMs = echoDelayMs;
  p.echoRepeats = echoRepeats;
  p.echoDecay = echoDecay;
  p.echoVolume = echoVolume;
  p.arpPattern = arpPattern;
  p.arpSpeedMs = arpSpeedMs;
  p.arpVolume = arpVolume;
  p.arpOctave = arpOctave;
  p.bitCrushBits = bitCrushBits;
  p.bitCrushRate = bitCrushRate;
  p.bitCrushVolume = bitCrushVolume;
  p.bitCrushDuration = bitCrushDuration;
  p.reverbTaps = reverbTaps;
  p.reverbSpacing = reverbSpacing;
  p.reverbDecay = reverbDecay;
  p.reverbDetune = reverbDetune;
  p.reverbVolume = reverbVolume;
  p.chorusDetune1 = chorusDetune1;
  p.chorusDetune2 = chorusDetune2;
  p.chorusVolume = chorusVolume;
  p.chorusDuration = chorusDuration;

  // SID mode
  p.sidMode = sidMode;
  p.sidEnvFreqCoarse = sidEnvFreqCoarse;
  p.sidEnvFreqFine = sidEnvFreqFine;
  p.sidEnvShape = sidEnvShape;

  // Sample player
  p.sampleSelect = sampleSelect;
  p.sampleMode = sampleMode;
  p.sampleVolume = sampleVolume;
  p.sampleSeqIndex = sampleSeqIndex;

  // Global
  p.polyMode = polyMode;
  p.linkMode = linkMode;
  p.voiceLinkMask = voiceLinkMask;
  for (int i = 0; i < 3; i++) {
    p.chipLink[i] = chipLink[i];
  }

  // Calculate CRC
  p.crc16 = presetCalcCRC(p);
  p.reserved[0] = 0;
  p.reserved[1] = 0;
}

// ============================================================================
// APPLY PRESET TO SYNTH STATE
// ============================================================================

void presetApplyCurrent(const PresetData& p) {
  // Voice settings
  for (int i = 0; i < 9; i++) {
    voiceSettings[i] = p.voices[i];
  }

  // Pot assignments
  for (int i = 0; i < 3; i++) {
    potAssignments[i] = p.pots[i];
  }

  // FX state
  fxModeEnabled = p.fxEnabled;
  fxType = p.fxType;
  fxRouting = p.fxRouting;
  echoDelayMs = p.echoDelayMs;
  echoRepeats = p.echoRepeats;
  echoDecay = p.echoDecay;
  echoVolume = p.echoVolume;
  arpPattern = p.arpPattern;
  arpSpeedMs = p.arpSpeedMs;
  arpVolume = p.arpVolume;
  arpOctave = p.arpOctave;
  bitCrushBits = p.bitCrushBits;
  bitCrushRate = p.bitCrushRate;
  bitCrushVolume = p.bitCrushVolume;
  bitCrushDuration = p.bitCrushDuration;
  reverbTaps = p.reverbTaps;
  reverbSpacing = p.reverbSpacing;
  reverbDecay = p.reverbDecay;
  reverbDetune = p.reverbDetune;
  reverbVolume = p.reverbVolume;
  chorusDetune1 = p.chorusDetune1;
  chorusDetune2 = p.chorusDetune2;
  chorusVolume = p.chorusVolume;
  chorusDuration = p.chorusDuration;

  // SID mode
  sidMode = p.sidMode;
  sidEnvFreqCoarse = p.sidEnvFreqCoarse;
  sidEnvFreqFine = p.sidEnvFreqFine;
  sidEnvShape = p.sidEnvShape;

  // Sample player
  sampleSelect = p.sampleSelect;
  sampleMode = p.sampleMode;
  sampleVolume = p.sampleVolume;
  sampleSeqIndex = p.sampleSeqIndex;

  // Global
  polyMode = p.polyMode;
  linkMode = p.linkMode;
  voiceLinkMask = p.voiceLinkMask;
  for (int i = 0; i < 3; i++) {
    chipLink[i] = p.chipLink[i];
  }
}

// ============================================================================
// PRESET MANAGEMENT
// ============================================================================

// Static buffer to avoid stack overflow in presetSaveUser
static uint8_t sectorBuffer[PRESET_SECTOR_SIZE];

bool presetSaveUser(uint8_t userSlot, const char* name) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  PresetData p;
  presetCaptureCurrent(p);

  // Set name
  memset(p.name, 0, PRESET_NAME_LEN);
  if (name) {
    strncpy(p.name, name, PRESET_NAME_LEN);
  }

  // Recalculate CRC after name change
  p.crc16 = presetCalcCRC(p);

  // Calculate flash offset
  uint32_t offset = sizeof(PresetHeader) + (userSlot * sizeof(PresetData));

  // We need to read the entire sector, modify our slot, and write back
  // because flash can only be erased in 4KB sectors
  uint32_t sectorStart = (offset / PRESET_SECTOR_SIZE) * PRESET_SECTOR_SIZE;
  uint32_t offsetInSector = offset - sectorStart;

  // Read entire sector into static buffer
  const uint8_t* flashSector = (const uint8_t*)(PRESET_FLASH_BASE + sectorStart);
  memcpy(sectorBuffer, flashSector, PRESET_SECTOR_SIZE);

  // Modify our preset in the buffer
  memcpy(sectorBuffer + offsetInSector, &p, sizeof(PresetData));

  // If header is in the same sector, update it too (avoids second flash write)
  if (sectorStart == 0) {
    PresetHeader* headerPtr = (PresetHeader*)sectorBuffer;
    headerPtr->activePreset = USER_SLOT_TO_PRESET(userSlot);
    // Don't increment count - not reliable anyway
  }

  // Write sector back (single flash write)
  presetWriteFlash(sectorStart, sectorBuffer, PRESET_SECTOR_SIZE);

  // If header was in different sector, update it separately
  if (sectorStart != 0) {
    PresetHeader header;
    readHeader(header);
    header.activePreset = USER_SLOT_TO_PRESET(userSlot);
    presetWriteFlash(0, (const uint8_t*)&header, sizeof(PresetHeader));
  }

  currentPresetIndex = USER_SLOT_TO_PRESET(userSlot);
  return true;
}

bool presetLoad(uint8_t presetIndex) {
  PresetData p;

  if (PRESET_IS_FACTORY(presetIndex)) {
    // Load from factory presets
    p = factoryPresets[presetIndex];
  } else if (PRESET_IS_USER(presetIndex)) {
    // Load from user flash
    uint8_t userSlot = PRESET_TO_USER_SLOT(presetIndex);
    if (!readUserPreset(userSlot, p)) {
      return false;
    }
  } else {
    return false;
  }

  // Apply preset
  presetApplyCurrent(p);

  // Just update RAM tracking - don't write to flash on every load
  // (Active preset will be saved when user saves a new preset)
  currentPresetIndex = presetIndex;
  return true;
}

bool presetDeleteUser(uint8_t userSlot) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  // Calculate flash offset
  uint32_t offset = sizeof(PresetHeader) + (userSlot * sizeof(PresetData));
  uint32_t sectorStart = (offset / PRESET_SECTOR_SIZE) * PRESET_SECTOR_SIZE;
  uint32_t offsetInSector = offset - sectorStart;

  // Read entire sector into static buffer (avoids stack overflow)
  const uint8_t* flashSector = (const uint8_t*)(PRESET_FLASH_BASE + sectorStart);
  memcpy(sectorBuffer, flashSector, PRESET_SECTOR_SIZE);

  // Create empty preset marker
  PresetData empty;
  memset(&empty, 0xFF, sizeof(PresetData));  // Flash erased state

  // Modify our preset in the buffer
  memcpy(sectorBuffer + offsetInSector, &empty, sizeof(PresetData));

  // If header is in the same sector and this was the active preset, update it too
  bool wasActive = (currentPresetIndex == USER_SLOT_TO_PRESET(userSlot));
  if (sectorStart == 0 && wasActive) {
    PresetHeader* headerPtr = (PresetHeader*)sectorBuffer;
    headerPtr->activePreset = PRESET_INDEX_NONE;
  }

  // Write sector back (single flash write)
  presetWriteFlash(sectorStart, sectorBuffer, PRESET_SECTOR_SIZE);

  // If header was in different sector and this was the active preset, update it separately
  if (sectorStart != 0 && wasActive) {
    PresetHeader header;
    readHeader(header);
    header.activePreset = PRESET_INDEX_NONE;
    presetWriteFlash(0, (const uint8_t*)&header, sizeof(PresetHeader));
  }

  if (wasActive) {
    currentPresetIndex = PRESET_INDEX_NONE;
  }

  return true;
}

bool presetUserIsUsed(uint8_t userSlot) {
  if (userSlot >= PRESET_USER_SLOTS) return false;

  uint32_t offset = sizeof(PresetHeader) + (userSlot * sizeof(PresetData));
  const PresetData* p = (const PresetData*)(PRESET_FLASH_BASE + offset);

  return (p->magic == PRESET_MAGIC && p->flags == PRESET_FLAG_USED);
}

void presetGetName(uint8_t presetIndex, char* buf) {
  if (PRESET_IS_FACTORY(presetIndex)) {
    strncpy(buf, factoryPresetNames[presetIndex], PRESET_NAME_LEN);
    buf[PRESET_NAME_LEN] = '\0';
  } else if (PRESET_IS_USER(presetIndex)) {
    uint8_t userSlot = PRESET_TO_USER_SLOT(presetIndex);
    uint32_t offset = sizeof(PresetHeader) + (userSlot * sizeof(PresetData));
    const PresetData* p = (const PresetData*)(PRESET_FLASH_BASE + offset);

    if (p->magic == PRESET_MAGIC && p->flags == PRESET_FLAG_USED) {
      memcpy(buf, p->name, PRESET_NAME_LEN);
      buf[PRESET_NAME_LEN] = '\0';
    } else {
      strcpy(buf, "--------");
    }
  } else {
    strcpy(buf, "--------");
  }
}

uint8_t presetGetTotalCount() {
  uint8_t count = PRESET_FACTORY_COUNT;

  for (uint8_t i = 0; i < PRESET_USER_SLOTS; i++) {
    if (presetUserIsUsed(i)) {
      count++;
    }
  }

  return count;
}

// ============================================================================
// GLOBAL SETTINGS (MIDI CHANNELS)
// ============================================================================

void saveGlobalSettings() {
  // Read entire first sector to preserve user presets
  const uint8_t* flashSector = (const uint8_t*)PRESET_FLASH_BASE;
  memcpy(sectorBuffer, flashSector, PRESET_SECTOR_SIZE);

  // Update header in buffer
  PresetHeader* headerPtr = (PresetHeader*)sectorBuffer;
  headerPtr->midiSynthChannel = midiSynthChannel;
  headerPtr->midiDrumChannel = midiDrumChannel;

  // Write entire sector back to flash (preserves presets)
  presetWriteFlash(0, sectorBuffer, PRESET_SECTOR_SIZE);
}
