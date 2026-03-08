# YM2149Fx3 User Guide

A 9-voice synthesizer built around three YM2149F sound chips, with SID emulation, effects processing, sample playback, and full MIDI control.

---

## Getting Started

The synth has three inputs: a **rotary encoder** (turn + press), **three pots**, and **MIDI** (USB or TRS). The OLED display shows the current state and menus.

- **Turn** the encoder to navigate menus or change values
- **Press** the encoder to select items or toggle edit mode
- **Pots** control whichever parameters you assign to them

On power-up you'll see the visualization screen. Press the encoder to open the main menu.

---

## Architecture: Three Chips, Nine Voices

The synth has three YM2149F chips, each with 3 square-wave voices (A, B, C):

| Chip | Voices | Notes |
|------|--------|-------|
| Chip 0 | Voices 0-2 (A/B/C) | Main synth chip |
| Chip 1 | Voices 3-5 (A/B/C) | Second chip / SID chip 1 |
| Chip 2 | Voices 6-8 (A/B/C) | Third chip / SID chip 2 / FX chip |

Two LEDs indicate note activity on Chip 0 and Chip 1.

---

## Polyphony Modes

Access via **Menu > MODE**. Three modes control how MIDI notes are distributed across voices:

### Mono
One voice per MIDI channel. Channels 1-6 map directly to voices 0-5. Best for monophonic leads and basses where you want precise control over which voice plays what.

### Semi-Poly
Three voices per chip, allocated by MIDI channel group:
- Channels 1-3 share Chip 0's three voices
- Channels 4-6 share Chip 1's three voices
- Channels 7-9 share Chip 2's three voices

Each chip can play up to 3 notes simultaneously within its channel group. Voice settings follow the MIDI channel number, so shaping is predictable regardless of which physical voice gets allocated.

### Poly (Default)
All 9 voices in a single pool. Notes from any channel are assigned round-robin across all available voices. Best for rich chords and dense polyphonic playing.

### Unison
Available in Poly and Semi modes. Multiple voices play the same note with adjustable detuning for a thick, chorus-like sound. Toggle with CC 72, control detune with CC 73.

---

## Chip Linking

Access via **Menu > LINK**. Makes multiple chips mirror Chip 0's voice settings for layered sounds:

- **OFF** - All chips independent
- **+CH1** - Chip 1 mirrors Chip 0
- **+CH2** - Chip 2 mirrors Chip 0
- **ALL** - Both Chip 1 and 2 mirror Chip 0

Use linking with detuned octave shifts across chips for massive ensemble textures.

---

## Voice Settings

Access via **Menu > CHIP 0/1/2**. Each chip opens a settings screen where you can shape individual voices or all voices on that chip at once.

### Scope Selector
Choose what you're editing:
- **ALL** - Edit all 3 voices on this chip simultaneously
- **A / B / C** - Edit one specific voice

### Pitch
- **Detune**: +/-50 cents fine pitch offset
- **Octave**: +/-3 octave shift
- **Volume**: Maximum volume cap (0-15)

### Vibrato
- **On**: 0 = controlled by mod wheel, 1-127 = always-on at set intensity
- **Rate**: 1.0-15.0 Hz
- **Depth**: 0-200 cents (up to 2 semitones)
- **Delay**: 0-2550 ms before vibrato kicks in after note-on

### Tremolo
Volume LFO that pulses the amplitude:
- **On**: 0 = off, 1-127 = intensity
- **Rate**: 1.0-15.0 Hz
- **Depth**: 0-100% volume swing

### Noise
- **Frequency**: 0-31, controls the YM2149's noise generator pitch
- Shared per chip (voice A's setting controls all three voices)
- Lower values = higher pitched noise, higher = rumblier

### Envelope (ADSR)
Shapes volume over time:
- **Attack**: 0-127, time to reach full volume
- **Decay**: 0-127, time to fall to sustain level
- **Sustain**: 0-127, held volume level
- **Release**: 0-127, fade time after note-off

### Glide (Portamento)
Smooth pitch slide between notes:
- **On**: Enable/disable (also toggled by CC 65)
- **Speed**: 0-127 (0 = slow glide, 127 = fast snap)

### Pitch Envelope
Automatic pitch sweep on note-on:
- **Amount**: 0-24 semitones sweep range
- **Time**: 0-127 sweep speed
- **Direction**: Down (start high, sweep to note) or Up (start low, sweep up)

Great for bass "bwoob" sounds, laser effects, and percussive attacks.

---

## SID Mode

Access via **Menu > MIDI > SID**. Transforms Chips 1 and 2 into a 6-voice SID-style synthesizer using volume PWM synthesis. Chip 0 is disabled in this mode.

### How It Works
A high-frequency timer (50 kHz) rapidly switches each voice's volume between 0 and its target level, creating waveforms through pulse-width modulation. This produces richer timbres than the YM2149's native square waves.

### SID Voice Parameters
Access via **CHIP 1/2 > SID submenu**:

- **Wave**: Square, Saw, Triangle, or Pulse
- **Duty**: 0-15 pulse width (0 = thin/nasal, 8 = 50% square, 15 = thick/warm)
- **PWM Rate**: 0-127, LFO speed for automatic duty cycle sweep
- **PWM Depth**: 0-15, how far the LFO sweeps the duty cycle
- **Noise**: Mix YM noise into the voice
- **Sync**: Hard sync to another voice on the same chip
- **Ring**: Ring modulation with another voice on the same chip (Work in progress)
- **Release**: 0-127, voice release time

### SID Presets
SID mode has its own preset bank: 4 factory + 8 user slots.

**Factory SID Presets:**
- **SQUARE** - Clean 50% duty square wave
- **NARROW** - Thin pulse for nasal/reedy tones
- **WIDE** - Thick pulse for warm bass
- **PWMLEAD** - PWM lead with vibrato and portamento

All standard voice settings (vibrato, envelope, pitch envelope, etc.) work on top of SID synthesis.

---

## Effects (FX)

Access via **Menu > FX**. Effects use Chip 2's voices to process audio. Choose an effect type and configure its parameters.

### Echo
Time-delayed repeats of played notes:
- **Delay**: 50-2000 ms (or MIDI clock divisions when synced)
- **Repeats**: 1-10 echo copies
- **Decay**: 0-15 volume reduction per repeat
- **Volume**: 1-15 output level

### Arpeggiator
Automatic note pattern cycling:
- **Pattern**: Up, Down, Up/Down, Random
- **Speed**: 50-500 ms per step (or clock divisions)
- **Volume**: 1-15
- **Octave**: +/-2 octave shift

### Bit Crush
Lo-fi bit reduction:
- **Bits**: 1-4 (lower = crunchier)
- **Rate**: 1-10 sample rate divisor
- **Volume**: 1-15
- **Duration**: 50-500 ms

### Pseudo Reverb
Multi-tap delay simulating reverb:
- **Taps**: 1-12 echo taps
- **Spacing**: 10-250 ms between taps
- **Decay**: 1-8 volume reduction per tap
- **Detune**: +/-5 cents pitch variation per tap
- **Volume**: 1-15

### Chorus
Layered detuning for width:
- **Detune 1**: +/-50 cents
- **Detune 2**: +/-50 cents
- **Volume**: 1-15
- **Rate**: 0.5-8.0 Hz LFO (0 = static detune)

### Harmonizer
Automatic chord generation:
- **Chord**: Octave, Power, Major, Minor, Sus2, Sus4, Aug, Dim, Maj7, Min7, Dom7, 5th+Octave
- **Volume**: 1-15
- **Octave**: +/-2

### Gate
Rhythmic volume gating:
- **Rate**: 30-500 ms cycle (or clock divisions)
- **Pattern**: Square (hard), Ramp (fade), Triangle (envelope), Random
- **Volume**: 1-15 peak
- **Duty**: 1-7 out of 8 on-time
- **Seed**: 0-15 random pattern seed

### FX Routing
Choose which voices the effect applies to:
- ALL, CHIP0, CHIP1, or individual voices (0A, 0B, 0C, 1A, 1B, 1C)

---

## Sample Player (Work in progress)

A built-in drum sampler using 24 dithered samples played back through Chip 2, Voice C.

### Trigger (Work in progress)
Send notes on the **drum channel** (default: MIDI channel 10).

### Modes (Work in progress)
- **Single**: Always plays the selected sample
- **Sequential**: Cycles through samples in order on each trigger
- **Random**: Random sample per trigger
- **Mapped** (default): GM drum mapping, MIDI notes 35-58 trigger specific sounds (kick, snare, hi-hat, toms, cymbals, etc.)

### GM Drum Map (Notes 35-58) (Work in progress)
Standard General MIDI percussion mapping. Key sounds:
- 35-36: Bass drums
- 38, 40: Snares
- 42, 44, 46: Hi-hats (closed, pedal, open)
- 41, 43, 45, 47, 48, 50: Toms
- 49, 51, 52, 55, 57: Cymbals
- 56: Cowbell

---

## Pot Assignments

Access via **Menu > POTS**. Each of the 3 physical pots can be mapped to any synth parameter.

### Assignment Categories
| Category | Parameters |
|----------|-----------|
| OFF | Pot disabled |
| VOICE | Detune, Octave, MaxVol, Noise, Slide |
| VIBRATO | Rate, Depth, Delay |
| ENVELOPE | Attack, Decay, Sustain, Release |
| TREMOLO | Rate, Depth |
| PITCH ENV | Amount, Time, Direction |
| SID | Wave, Duty, Detune |
| FX ECHO | Delay, Repeats, Decay, Volume |
| FX ARP | Speed, Pattern, Volume, Octave |
| FX CRUSH | Bits, Rate, Volume |
| FX REVERB | Taps, Spacing, Decay, Detune, Volume |
| FX CHORUS | Detune1, Detune2, Volume, Rate |
| SAMPLE | Select, Mode, Volume |
| GLOBAL | Expression, PolyMode |

### Editing Flow
1. Select which pot (1, 2, or 3)
2. Choose the category
3. Choose the parameter within that category
4. Choose the target (ALL voices, specific chip, or individual voice)

Pot defaults can be saved persistently via **Menu > MIDI > Pot Defaults**.

---

## Presets

Access via **Menu > PRESETS**.

### Factory Presets (8, read-only)
| Name | Description |
|------|-------------|
| INIT | Clean starting point |
| ACIDLP | Acid lead/bass with SID duty sweep |
| SPACER | Cosmic shimmer with extreme reverb |
| DRONEO | Evolving textural drone |
| BELLTR | Metallic bell tones with pitch envelope |
| SKAREE | Sci-fi horror atmosphere |
| RETRO8 | 8-bit video game style |
| MORPHO | Morphing organic texture |

### User Presets (36 slots)
- **Load**: Browse and load any factory or saved user preset
- **Save**: Name your preset (8 characters), pick a slot
- **Delete**: Remove a user preset

Presets store everything: all voice settings, pot assignments, effects, poly mode, linking, and sample player state. Load a factory preset, tweak it, and save as your own.

---

## MIDI Reference

### Channels
- **Synth channel**: Configurable (default: OMNI / all channels)
- **Drum channel**: Configurable (default: channel 10)
- Set both via **Menu > MIDI**

### Note Range
MIDI notes 9 (A-1) through 108 (C8).

### Key CC Messages

| CC | Function | Range |
|----|----------|-------|
| 1 | Mod wheel (vibrato) | 0-127 |
| 5 | Portamento time | 0-127 |
| 7 | Channel volume | 0-127 |
| 9 | Pitch env amount | 0-127 |
| 10 | Pitch env direction | 0-127 |
| 11 | Expression | 0-127 |
| 64 | Sustain pedal | 0-63 off, 64-127 on |
| 65 | Portamento on/off | 0-63 off, 64-127 on |
| 68 | Laser mode | 0-63 off, 64-127 on |
| 69 | Laser amount | 0-127 |
| 70 | Poly mode | 0-42 mono, 43-84 semi, 85-127 poly |
| 71 | SID mode toggle | 0-63 off, 64-127 on |
| 72 | Unison mode | 0-63 off, 64-127 on |
| 73 | Unison detune | 0-127 (+/-50 cents) |
| 120 | All sound off | - |
| 121 | Reset controllers | - |

### Pitch Bend
+/-2 semitones, 14-bit resolution.

### MIDI Clock Sync
Enable via **Menu > MIDI > Clock Sync**. Syncs echo, arpeggiator, reverb, and gate timing to incoming MIDI clock from your DAW or hardware sequencer.

---

## Visualization Modes

Three display modes when not in a menu (cycle via **Menu > MIDI > Viz**):

- **Bars**: Real-time volume meters for all 9 voices in a 3x3 grid
- **Scope**: Single-voice oscilloscope waveform view
- **Matrix**: Mini oscilloscope for all 9 voices simultaneously

---

## Laser Mode

A special effect toggled by CC 68 with intensity controlled by CC 69. Creates dramatic pitch-diving "laser gun" sounds by multiplying the voice period. Great for sci-fi sound effects and glitchy textures.

---

## YMPlayer (Serial Mode)

Access via **Menu > MIDI > USB Mode > SERIAL**. Switches USB from MIDI to high-speed serial (2 Mbps) for streaming YM-format chiptune files directly to the hardware. Requires a companion application on the host computer. Requires reboot to take effect.

---

## Tips for Musicians

**Building patches from scratch:**
Load the INIT preset, set your poly mode, then shape one chip at a time.

**Layering for thickness:**
Set Link to ALL, then give each chip a different octave shift or detune. Three detuned copies of the same note across all 9 voices creates massive unison sounds.

**Expressive performance:**
Assign pots to the parameters you want to tweak live (filter-like sweeps with noise frequency, volume shaping with envelope controls). Use the mod wheel for vibrato and expression for dynamics.

**SID mode for richer timbres:**
Switch to SID mode when you need waveforms beyond square waves, metallic and aggressive bass textures.

**Effects as instruments:**
The arpeggiator and harmonizer can turn single notes into full patterns. Route effects to specific voices to keep some voices clean while others are processed.

**MIDI clock sync:**
When playing with a DAW, enable clock sync so echo repeats and arp steps lock to your tempo. The gate effect becomes a rhythmic sidechain when synced. (work in progress)

**Semi-poly for multi-timbral:**
Use semi-poly mode to give each chip its own sound. Send bass on channels 1-3, lead on 4-6, and pads on 7-9 for a complete arrangement from one device.

**Save often:**
You have 36 user preset slots. Save variations as you go - it's easy to lose a good sound by tweaking one parameter too far.
