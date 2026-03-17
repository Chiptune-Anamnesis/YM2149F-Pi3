# YM2149Fx3 MIDI Synthesizer

Triple YM2149F sound chip MIDI synthesizer with SID emulation, sample playback, and real-time sound design — powered by RP2040 (Raspberry Pi Pico).

## Features

### 9-Voice Polyphonic YM2149F Synthesis
- 3x YM2149F sound chips providing 9 independent tone voices
- Mono, Semi-poly, and Poly voice allocation modes
- Per-voice settings: detune (cents), octave shift, max volume, noise frequency
- Chip linking: mirror Chip 0 settings across chips for massive unison sounds
- MIDI channels 1-9 mapped to voices (3 per chip)

### SID Emulation Mode (NextSID)
- Software-emulated SID-style waveforms on Chips 1 and 2 (6 voices)
- Waveforms: Square and Pulse with adjustable duty cycle (default duty: 11)
- PWM sweep with configurable rate and depth
- Per-voice sync and ring modulation between voices within a chip
- Noise mixing per voice
- Portamento/glide fully supported in SID mode
- ~50kHz timer-driven volume flipping for waveform generation

### Sample Playback (SMPL Mode)
- Timer-based 8-bit PCM playback at 8000 Hz on Chip 2 (3 polyphonic voices)
- **Three sample sections:**
  - **DRUM** — 24 Bitkits drum samples: kicks, snares, hi-hats, toms, claps, cowbell, tambourine, shaker
  - **1SHT** — 24 Bitkits one-shot samples: beeps, blips, buzzes, glitches, metals, noises, stabs, vocals, zaps
  - **DIGI** — 16 legacy ST-Sound DigiDrum samples (classic Atari ST sounds)
- **Four playback modes:**
  - **SNGL** — Always play the selected sample
  - **SEQ** — Play samples sequentially (s00, s01, s02...)
  - **RND** — Random sample each trigger
  - **GM** — General MIDI drum map (notes 35-58 mapped to samples). OneShots use chromatic mapping from C2
- Per-section GM drum maps (Bitkits and DigiDrum have independent mappings)
- Pitch control: -12 to +12 semitones fine tuning plus -3 to +3 octave shift (8.8 fixed-point)
- Adjustable sample length (1-127, 127 = full sample)
- Volume scaling (1-15)
- Voice stealing with same-sample retrigger and round-robin allocation

### Effects (Chip 2)
- **Echo** — Delay, repeats, decay, volume
- **Arpeggio** — Speed, pattern, volume, octave range
- **Bit Crush** — Bit depth, sample rate, volume
- **Reverb** — Taps, spacing, decay, detune, volume
- **Chorus** — Detune spread with configurable volume
- Flexible routing: per-chip or global

### Per-Voice Modulation
- **Vibrato** — Rate (1.0-15.0 Hz), depth (0-200 cents), delay (0-2550ms)
- **Tremolo** — Rate (1.0-15.0 Hz), depth (0-100%)
- **Pitch Envelope** — Amount (0-24 semitones), time, direction (up/down)
- **ADSR Envelope** — Attack, decay, sustain, release
- **Portamento/Glide** — On/off per voice, adjustable speed

### Preset System
- Save and load complete synth states to flash (8 user slots)
- 4 factory SID presets
- Stores all voice settings, FX, SID config, sample section, mode, and more
- 8-character preset naming

### MIDI Implementation
- USB-MIDI and TRS-MIDI (Serial1 at 31250 baud) simultaneously
- MIDI channel filtering (per-channel, OMNI, or OFF)
- Channel remapping (route any incoming channel to any internal channel)
- 74 MIDI CC mappings covering voice, envelope, tremolo, FX, sample, and link controls — see [MIDI CC Map](MIDI_CC_MAP.md)
- Pitch bend (synth and sample modes)
- MIDI Real-Time: Start (0xFA), Continue (0xFB), Stop (0xFC) with proper note/controller reset
- MIDI clock sync
- Note range: A-1 to C8 (MIDI notes 9-108)
- Velocity curve with configurable gamma, min, and max

### Display & Controls
- 128x64 SH1106 OLED display (I2C on Wire1)
- Rotary encoder for menu navigation
- 3 analog pots with flexible parameter mapping (50+ assignable parameters)
- 5 visualization modes: Bars, Scope, Matrix (9-voice), Channel Matrix, Sample Waveform
- Hierarchical menu system: Main → Settings → Submenus (Pitch, Vibrato, Tremolo, Envelope, Glide, Pitch Env, SID, FX, Sample)

### Dual-Core Architecture (RP2040)
- **Core 0:** Audio engine — MIDI processing, YM2149 register writes, SID timer, sample timer, pitch modulation, effects
- **Core 1:** UI — OLED display, encoder input, pot reading, menu system
- Thread-safe communication via command queue and display snapshot
- Interrupt-safe bus arbitration for shared YM2149 data bus

## Hardware

### Sound Chips
- 3x YM2149F at 500 kHz clock
- 4-bit volume (0-15) per voice, 12-bit period registers (1-4095)

### Controller
- Raspberry Pi Pico (RP2040)
- Dual ARM Cortex-M0+ cores at 133 MHz
- 2MB flash for firmware + presets + sample data

### I/O
- 74HC138 3-to-8 decoder for chip selection
- CD74HC4067 multiplexer for 3 analog pots
- SH1106 128x64 OLED (I2C)
- Rotary encoder with push button
- TRS-MIDI input (Serial1)
- USB-MIDI
- 3x LEDs (active-low, one per chip)

### MIDI Routing
- **Channels 1-9** → Tone voices (3 chips x 3 voices)
  - Ch 1-3 → Chip 0, Ch 4-6 → Chip 1, Ch 7-9 → Chip 2
- **Channel 10** → Noise channel (Chip 2 voice C, when enabled)
- **Channels 11-16** → Wrapped to prevent out-of-bounds access

## Configuration

Compile-time settings in `config.h`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `VELOCITY_GAMMA` | 0.3 | Velocity curve exponent (lower = more boost for soft notes) |
| `VELOCITY_MIN` | 4 | Minimum YM volume for softest velocity |
| `VELOCITY_MAX` | 15 | Maximum YM volume for loudest velocity |
| `EXPRESSION_AMOUNT` | 0.3 | CC7/CC11 volume influence (0.0 = bypass, 1.0 = full) |
| `ENABLE_NOISE_CHANNEL` | 0 | Enable MIDI channel 10 noise percussion |
| `USE_CC4_ENVELOPE` | 1 | Enable CC4 volume envelope shaping |

## Sample Conversion

The `tools/convert_samples.py` script converts WAV files to 8-bit unsigned PCM C arrays:

```bash
python tools/convert_samples.py
```

- Input: WAV files in `src/.../Samples/` (any sample rate, bit depth, stereo/mono)
- Output: `samples_drums.h` and `samples_oneshots.h` (8kHz, 8-bit, mono, normalized)
- Total sample data: ~92 KB (drums 31KB + oneshots 46KB + legacy DigiDrum 15KB)

## Build

PlatformIO project targeting `rpipico` with the `earlephilhower/arduino-pico` framework.

```bash
pio run -e pico
pio run -e pico -t upload
```

## Tips for Musicians

**Building patches from scratch:**
Load the INIT preset, set your poly mode, then shape one chip at a time.

**Layering for thickness:**
Set Link to ALL, then give each chip a different octave shift or detune. Three detuned copies of the same note across all 9 voices creates massive unison sounds.

**Expressive performance:**
Assign pots to the parameters you want to tweak live (filter-like sweeps with noise frequency, volume shaping with envelope controls). Use the mod wheel for vibrato and expression for dynamics.

**SID mode for richer timbres:**
Switch to SID mode when you need, metallic and aggressive bass textures.

**Effects as instruments:**
The arpeggiator and harmonizer can turn single notes into full patterns. Route effects to specific voices to keep some voices clean while others are processed.

**MIDI clock sync:**
When playing with a DAW, enable clock sync so echo repeats and arp steps lock to your tempo. The gate effect becomes a rhythmic sidechain when synced. (work in progress)

**Semi-poly for multi-timbral:**
Use semi-poly mode to give each chip its own sound. Send bass on channels 1-3, lead on 4-6, and pads on 7-9 for a complete arrangement from one device.

**Save often:**
You have 36 user preset slots. Save variations as you go - it's easy to lose a good sound by tweaking one parameter too far.

Buy it here: https://hobbychop.com
