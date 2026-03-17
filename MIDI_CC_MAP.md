# YM2149Fx3 MIDI CC Map

Complete MIDI Control Change mapping for the YM2149Fx3 synthesizer.
All CCs respond on the configured MIDI synth channel (or all channels in OMNI mode).

## Performance Controls

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 1 | Mod Wheel | 0-127 | Vibrato intensity |
| 4 | Foot Pedal | 0-127 | Volume envelope shape |
| 5 | Portamento Time | 0-127 | Glide speed (exponential curve) |
| 7 | Volume | 0-127 | Channel volume / expression |
| 11 | Expression | 0-127 | Channel volume (same as CC 7) |
| 64 | Sustain | 0-63=off, 64-127=on | Hold notes until release |
| 65 | Portamento | 0-63=off, 64-127=on | Enable pitch glide between notes |

## Pitch Bend

| Message | Range | Description |
|---------|-------|-------------|
| Pitch Bend | 14-bit (center=8192) | +/- 2 semitones. Works in both synth and sample modes |

## Vibrato

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 1 | Mod Wheel | 0-127 | Vibrato intensity (real-time) |
| 76 | Vibrato Rate | 0-127 | LFO speed (0-10 Hz) |
| 77 | Vibrato Depth | 0-127 | LFO range (0-2 semitones) |
| 85 | Vibrato Delay | 0-127 | Delay before vibrato starts (0-2000ms) |

## Voice Settings (CC 14-17)

Applied to all 9 voices simultaneously.

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 14 | Detune | 0-127 | Fine pitch (-50 to +50 cents, 64=center) |
| 15 | Octave | 0-127 | Octave shift (-3 to +3, 64=center) |
| 16 | Max Volume | 0-127 | Voice volume cap (maps to 0-15) |
| 17 | Noise Freq | 0-127 | Noise generator frequency (maps to 0-31) |

## Envelope ADSR (CC 18-21)

Applied to all 9 voices simultaneously.

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 18 | Attack | 0-127 | Attack time (0=instant, 127=slow) |
| 19 | Decay | 0-127 | Decay time |
| 20 | Sustain | 0-127 | Sustain level |
| 21 | Release | 0-127 | Release time (0=instant, 127=long) |

## Tremolo (CC 22-24)

Applied to all 9 voices simultaneously.

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 22 | Tremolo On | 0=off, 1-127 | Tremolo intensity |
| 23 | Tremolo Rate | 0-127 | LFO speed (1.0-15.0 Hz) |
| 24 | Tremolo Depth | 0-127 | Volume swing (0-100%) |

## Pitch Envelope (CC 25-27)

Applied to all 9 voices simultaneously.

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 9 | Pitch Env Amt (RT) | 0-127 | Real-time pitch sweep (0-2 semitones) |
| 10 | Pitch Env Shape (RT) | 0-127 | Real-time envelope shape |
| 25 | Pitch Env Amount | 0-127 | Per-voice sweep amount (0-24 semitones) |
| 26 | Pitch Env Time | 0-127 | Per-voice sweep speed |
| 27 | Pitch Env Dir | 0-63=down, 64-127=up | Sweep direction |

## FX Master (CC 28-30)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 28 | FX Enable | 0-63=off, 64-127=on | Toggle effects on/off |
| 29 | FX Type | 0-127 | Select effect (see table below) |
| 30 | FX Routing | 0-127 | Route FX to specific voices/chips (maps to 0-8) |

### FX Type Values (CC 29)

| Value Range | Type | Description |
|-------------|------|-------------|
| 0-17 | NONE | Effects off |
| 18-35 | ECHO | Delay/echo effect |
| 36-53 | ARP | Arpeggiator |
| 54-71 | CRUSH | Bit crusher |
| 72-89 | REVERB | Pseudo reverb |
| 90-107 | CHORUS | Chorus/detune |
| 108-117 | HARM | Harmonizer |
| 118-127 | GATE | Rhythmic gate |

## Echo (CC 33-36)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 33 | Echo Delay | 0-127 | Delay time (50-2000ms) |
| 34 | Echo Repeats | 0-127 | Number of echoes (1-10) |
| 35 | Echo Decay | 0-127 | Volume reduction per repeat (1-15) |
| 36 | Echo Volume | 0-127 | Echo output level (1-15) |

## Arpeggiator (CC 37-40)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 37 | Arp Speed | 0-127 | Step timing (50-500ms) |
| 38 | Arp Pattern | 0-127 | Pattern: 0-31=UP, 32-63=DOWN, 64-95=UP/DOWN, 96-127=RANDOM |
| 39 | Arp Volume | 0-127 | Arp output level (1-15) |
| 40 | Arp Octave | 0-127 | Octave range (-2 to +2, 64=center) |

## Bit Crush (CC 41-43)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 41 | Crush Bits | 0-127 | Bit depth (1-4, lower=crunchier) |
| 42 | Crush Rate | 0-127 | Sample-and-hold divisor (1-10) |
| 43 | Crush Volume | 0-127 | Crush output level (1-15) |

## Reverb (CC 44-48)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 44 | Reverb Taps | 0-127 | Number of echo taps (2-6) |
| 45 | Reverb Spacing | 0-127 | Tap spacing (20-100ms) |
| 46 | Reverb Decay | 0-127 | Volume decay per tap (1-8) |
| 47 | Reverb Detune | 0-127 | Pitch variation per tap (-5 to +5 cents, 64=center) |
| 48 | Reverb Volume | 0-127 | Reverb output level (1-15) |

## Chorus (CC 49-52)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 49 | Chorus Detune 1 | 0-127 | Voice 1 detune (-50 to +50 cents, 64=center) |
| 50 | Chorus Detune 2 | 0-127 | Voice 2 detune (-50 to +50 cents, 64=center) |
| 51 | Chorus Volume | 0-127 | Chorus output level (1-15) |
| 52 | Chorus Rate | 0-127 | LFO rate (0=static, up to 8.0 Hz) |

## Harmonizer (CC 53-55)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 53 | Harm Chord | 0-127 | Chord type (maps to 0-11, see device menu for chord names) |
| 54 | Harm Volume | 0-127 | Harmony output level (1-15) |
| 55 | Harm Octave | 0-127 | Octave offset (-2 to +2, 64=center) |

## Gate (CC 56-60)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 56 | Gate Rate | 0-127 | Gate cycle period (30-500ms) |
| 57 | Gate Pattern | 0-127 | Shape: 0-31=SQUARE, 32-63=RAMP, 64-95=TRI, 96-127=RANDOM |
| 58 | Gate Volume | 0-127 | Gate max level (1-15) |
| 59 | Gate Duty | 0-127 | On-time fraction (1/8 to 7/8) |
| 60 | Gate Seed | 0-127 | Random pattern seed (0-15) |

## Mode Switches (CC 68-78)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 68 | Laser Mode | 0-63=off, 64-127=on | Toggle laser glitch effect |
| 69 | Laser Amount | 0-127 | Laser intensity |
| 70 | Poly Mode | 0-42=semi, 43-84=poly, 85-127=mono | Voice allocation mode |
| 71 | SID Mode | 0-63=YM, 64-127=SID | Toggle SID emulation (full mode reset) |
| 72 | Unison | 0-63=off, 64-127=on | Toggle unison mode |
| 73 | Unison Detune | 0-127 | Unison spread (0-50 cents) |
| 74 | SID Duty / Env Freq | 0-127 | PWM mode: duty cycle. Env mode: envelope frequency |
| 75 | SID Wave / Env Shape | 0-127 | PWM mode: square/pulse. Env mode: envelope shape |
| 78 | SID Detune | 0-127 | Per-voice PWM detune (64=center) |

## Sample Controls (CC 102-111)

These CCs are only active when SMPL mode is enabled.

### Global Sample Settings (CC 102-105)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 102 | Sample Volume | 0-127 | Playback volume (1-15) |
| 103 | Sample Mode | 0-127 | 0-31=SNGL, 32-63=SEQ, 64-95=RND, 96-127=GM |
| 104 | Sample Section | 0-127 | 0-63=DRUMS, 64-95=1SHOTS, 96-127=DIGI |
| 105 | Sample Select | 0-127 | Select sample within section (scaled to section size) |

### Per-Sample Settings (CC 108-111)

These affect the currently selected sample (last played or selected via CC 105).

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 108 | Sample Pitch | 0-127 | Pitch offset (-12 to +12 semitones, 64=center) |
| 109 | Sample Octave | 0-127 | Octave shift (-3 to +3, 64=center) |
| 110 | Sample Length | 1-127 | Playback length (1=short, 127=full sample) |
| 111 | Sample Crush | 0-127 | Bitcrush amount (0=clean, maps to 0-7) |

## Link / Voice Settings (CC 106-107)

| CC | Name | Value | Description |
|----|------|-------|-------------|
| 106 | Link Mode | 0-127 | 0-31=OFF, 32-63=CH1, 64-95=CH2, 96-127=ALL |
| 107 | Voice Link Mask | 0-127 | Bitmask of linked voices (maps to 0-7) |

## System Messages

| CC | Name | Description |
|----|------|-------------|
| 120 | All Sound Off | Silence all voices + clear FX state |
| 121 | Reset Controllers | Reset all CC values to defaults + clear FX |
| 123 | All Notes Off | Release all notes (respects sustain pedal) |

## Quick Reference by CC Number

| CC | Function | CC | Function | CC | Function |
|----|----------|----|----------|----|----------|
| 1 | Mod Wheel | 33 | Echo Delay | 64 | Sustain |
| 4 | Foot Pedal | 34 | Echo Repeats | 65 | Portamento |
| 5 | Porta Time | 35 | Echo Decay | 68 | Laser On |
| 7 | Volume | 36 | Echo Volume | 69 | Laser Amt |
| 9 | PitchEnv Amt | 37 | Arp Speed | 70 | Poly Mode |
| 10 | PitchEnv Shape | 38 | Arp Pattern | 71 | SID Mode |
| 11 | Expression | 39 | Arp Volume | 72 | Unison |
| 14 | Detune | 40 | Arp Octave | 73 | Uni Detune |
| 15 | Octave | 41 | Crush Bits | 74 | SID Duty |
| 16 | Max Volume | 42 | Crush Rate | 75 | SID Wave |
| 17 | Noise Freq | 43 | Crush Volume | 76 | Vib Rate |
| 18 | Env Attack | 44 | Reverb Taps | 77 | Vib Depth |
| 19 | Env Decay | 45 | Reverb Space | 78 | SID Detune |
| 20 | Env Sustain | 46 | Reverb Decay | 85 | Vib Delay |
| 21 | Env Release | 47 | Reverb Det | 102 | Smpl Vol |
| 22 | Tremolo On | 48 | Reverb Vol | 103 | Smpl Mode |
| 23 | Tremolo Rate | 49 | Chorus Det1 | 104 | Smpl Sect |
| 24 | Tremolo Depth | 50 | Chorus Det2 | 105 | Smpl Select |
| 25 | PEnv Amount | 51 | Chorus Vol | 106 | Link Mode |
| 26 | PEnv Time | 52 | Chorus Rate | 107 | Link Mask |
| 27 | PEnv Dir | 53 | Harm Chord | 108 | Smpl Pitch |
| 28 | FX Enable | 54 | Harm Volume | 109 | Smpl Octave |
| 29 | FX Type | 55 | Harm Octave | 110 | Smpl Length |
| 30 | FX Routing | 56 | Gate Rate | 111 | Smpl Crush |
| | | 57 | Gate Pattern | 120 | All Snd Off |
| | | 58 | Gate Volume | 121 | Reset Ctrl |
| | | 59 | Gate Duty | 123 | All Notes Off |
| | | 60 | Gate Seed | | |
