// ============================================================================
// FACTORY PRESETS MODULE
// Factory YM presets and SID presets (read-only, initialized at runtime)
// ============================================================================

#include "preset.h"
#include "sample_player.h"
#include "fx_chip.h"

// ============================================================================
// FACTORY SID PRESETS (initialized at runtime with full voice settings)
// ============================================================================

SidPreset factorySidPresets[SID_PRESET_FACTORY_COUNT];

// Helper to create a SID preset with consistent voice settings
static void createSidPreset(SidPreset& p, const char* name, uint8_t polyMode,
                            uint8_t duty1, uint8_t duty2,
                            uint8_t vibOn, uint8_t vibRate, uint8_t vibDepth, uint8_t vibDelay,
                            uint8_t tremoloOn, uint8_t tremoloRate, uint8_t tremoloDepth,
                            uint8_t portaOn, uint8_t portaSpeed,
                            uint8_t pitchEnvAmt, uint8_t pitchEnvTime, uint8_t pitchEnvDir,
                            uint8_t attack, uint8_t decay, uint8_t sustain) {
  memset(&p, 0, sizeof(SidPreset));
  strncpy(p.name, name, SID_PRESET_NAME_LEN);
  p.version = SID_PRESET_VERSION_FULL;
  p.flags = SID_PRESET_FLAG_USED;
  p.polyMode = polyMode;

  // Initialize all 6 SID voices
  for (uint8_t i = 0; i < 6; i++) {
    VoiceSettings& v = p.voices[i];
    uint8_t chipIdx = (i < 3) ? 0 : 1;  // Voices 0-2 = chip 1, 3-5 = chip 2

    v.detuneCents = 0;
    v.octaveShift = 0;
    v.vibOn = vibOn;
    v.vibRateTenths = vibRate;
    v.vibDepthCents = vibDepth;
    v.vibDelay = vibDelay;
    v.noiseFreq = 15;
    v.envAttack = attack;
    v.envDecay = decay;
    v.envSustain = sustain;
    v.sidPwmRate = 0;
    v.sidWave = 0;
    v.sidDuty = (chipIdx == 0) ? duty1 : duty2;
    v.maxVolume = 15;
    v.portaOn = portaOn;
    v.portaSpeed = portaSpeed;
    v.tremoloOn = tremoloOn;
    v.tremoloRate = tremoloRate;
    v.tremoloDepth = tremoloDepth;
    v.pitchEnvAmt = pitchEnvAmt;
    v.pitchEnvTime = pitchEnvTime;
    v.pitchEnvDir = pitchEnvDir;
    v.sidPwmDepth = 0;
    v.sidSync = 0;
    v.sidRing = 0;
    v.sidNoise = 0;
    v.envRelease = 0;
  }
}

// Initialize factory SID presets (called from presetInit)
void initFactorySidPresets() {
  // S01: SQUARE - Clean 50% duty square wave, poly mode
  // Good starting point for classic chiptune sounds
  createSidPreset(factorySidPresets[0], "SQUARE", 1,  // poly
    8, 8,      // duty1, duty2 (50%)
    0, 50, 30, 0,   // vibrato: off
    0, 50, 30,      // tremolo: off
    0, 64,          // porta: off
    0, 64, 0,       // pitch env: off
    0, 40, 100);    // envelope: instant attack, med decay, full sustain

  // S02: NARROW - Thin pulse for nasal/reedy tones
  createSidPreset(factorySidPresets[1], "NARROW", 1,  // poly
    4, 4,      // duty1, duty2 (thin pulse ~25%)
    0, 50, 30, 0,
    0, 50, 30,
    0, 64,
    0, 64, 0,
    0, 40, 100);

  // S03: WIDE - Thick pulse for warm bass
  createSidPreset(factorySidPresets[2], "WIDE", 1,  // poly
    12, 12,    // duty1, duty2 (wide pulse ~75%)
    0, 50, 30, 0,
    0, 50, 30,
    0, 64,
    0, 64, 0,
    0, 40, 100);

  // S04: PWMLEAD - PWM lead with vibrato and portamento
  // polyMode: 0=semi, 1=poly, 2=mono
  createSidPreset(factorySidPresets[3], "PWMLEAD", 2,  // mono (2)
    6, 10,     // duty1, duty2 (asymmetric for movement)
    80, 60, 40, 20,  // vibrato: on, moderate
    0, 50, 30,       // tremolo: off
    1, 80,           // porta: on, fast
    0, 64, 0,        // pitch env: off
    5, 50, 90);      // envelope: slight attack, med decay, high sustain
}

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
  int8_t chorusDetune1, int8_t chorusDetune2, uint8_t chorusVol, uint8_t chorusRate,
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
    p.voices[i].sidPwmRate = 0;
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
    p.voices[i].sidPwmDepth = 0;
    p.voices[i].sidSync = 0;
    p.voices[i].sidRing = 0;
    p.voices[i].sidNoise = 0;
    p.voices[i].envRelease = 0;
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
  p.chorusRate = chorusRate;
  p.chorusReserved = 0;

  // Harmonizer defaults
  p.harmChord = HARM_CHORD_MAJOR;
  p.harmVolume = 14;
  p.harmOctave = 0;

  // Gate defaults
  p.gateRateMs = 120;
  p.gatePattern = GATE_SQUARE;
  p.gateVolume = 14;
  p.gateDuty = 4;
  p.gateSeed = 0;

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
