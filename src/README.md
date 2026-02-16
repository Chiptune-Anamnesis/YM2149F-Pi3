# YM2149Fx3 MIDI Synthesizer

## Recent Stability Fixes (v1.49)

### Critical Bugs Fixed

#### 1. Array Bounds Violation (MIDI Channels 11-16)
**Problem:** MIDI files with active channels 11-16 caused wild behavior, stuck notes, and memory corruption.
- When channels 11-16 sent note messages, the code accessed `midiToChip[10-15]`
- The `midiToChip` array only has 9 elements (indices 0-8)
- Out-of-bounds array access read garbage memory
- Random chip values caused invalid YM writes and state corruption
- Behavior degraded over time as corruption accumulated

**Fix:** Added channel validation to prevent out-of-bounds access:
- `noteOn()`: Returns early for channels >= 9
- `noteOff()`: Returns early for channels >= 9
- `pitchBend()`: Returns early for channels >= 9

#### 2. Division by Zero in Laser Mode
**Problem:** Laser mode caused stuck glitchy notes that wouldn't stop, even with MIDI Stop.
- `curPeriod = targetP / laserAmt` when `laserAmt = 0` produced NaN/Infinity
- Invalid period values propagated through the voice state
- Caused unstoppable glitchy "portamento/laser" effect

**Fix:** Changed laser calculation to prevent division by zero:
- Added `laserAmt > 0.01f` safety check
- Changed from division to multiplication: `curPeriod = targetP * (1.0f + laserAmt * 10.0f)`
- Added period clamping (1.0-4095.0) to catch NaN/Infinity/overflow
- Fixed immediate attack to use `targetP` instead of 0

#### 3. Portamento Speed Scaling
**Problem:** Portamento speed was inverted (CC5: 0=fast, 127=slow instead of 0=slow, 127=fast).

**Fix:** Applied quadratic curve to CC5 mapping:
```cpp
float norm  = d2 / 127.0f;
float curve = norm * norm;
portamentoSpeed[ch] = PORTA_MIN + (PORTA_MAX - PORTA_MIN) * curve;
```

#### 4. Note Range Validation
**Problem:** Out-of-bounds MIDI notes caused invalid period calculations and glitches.

**Fix:** Added note clamping to valid range:
- `MIDI_NOTE_MIN = 21` (A0)
- `MIDI_NOTE_MAX = 108` (C8)
- Applied in both `noteOn()` and `noteOff()`

#### 5. LED Polarity Inversion
**Problem:** LEDs were always on and turned off when notes were hit (inverted behavior).

**Fix:** Inverted boolean values passed to `ym.setLED()`:
- The YM2149 class has inverted logic (false=on, true=off)
- Changed `setLED(chip, true)` to `setLED(chip, false)` for LED on
- Changed `setLED(chip, false)` to `setLED(chip, true)` for LED off
- LEDs now correctly flash on when notes hit

#### 6. Noise Channel Cleanup
**Problem:** Noise channel didn't fully stop when MIDI Stop command was sent, especially if sent mid-note. Noise would continue playing until the next noise note started.

**Fix:** Simplified `noiseOff()` function to just mute volume:
- **Solution**: Just set volume (reg 10) to 0 - that's it
- Between drum hits, `noiseOn()` sets volume which makes sound audible
- So `noiseOff()` only needs to mute volume, nothing else
- Previous versions over-complicated this with mixer and register manipulation
- Simple approach: if volume controls sound on, it controls sound off

**Additional Fix:** Added LED flash for noise channel:
- LED2 (chip 2) now flashes when channel 10 notes are received
- Consistent with tone channel LED behavior

### Enhancements

#### MIDI Real-Time Message Handling
Added proper support for MIDI Stop/Start/Continue (0xFC/0xFA/0xFB):
- **MIDI Stop (0xFC)**: Calls `allNotesOffPanic()` and `resetAllControllers()` for all channels
- **MIDI Start (0xFA)**: Resets all controllers for clean playback start
- **MIDI Continue (0xFB)**: Resets all controllers

#### Controller Reset Functions
- `resetAllControllers(ch)`: Resets all CC values to defaults for a channel
- `allNotesOffPanic()`: Emergency stop for all voices on all chips
- `allNotesOffChannel(ch)`: Stop all notes on a specific channel

#### CC121 Support (Reset All Controllers)
Implements standard MIDI CC121 behavior to reset stuck controller states.

#### Improved Note-Off Handling
Changed `noteOff()` to release **ALL** matching notes instead of just the first one:
- Prevents stuck notes when duplicate note-ons occur
- Removed `break` statements in voice-scanning loops

#### YM2149 Class Integration
Replaced slow `digitalWrite()` calls with optimized class methods:
- Direct port manipulation (PORTB/PORTF writes)
- ATOMIC_BLOCK for interrupt safety
- Chip caching to avoid redundant selection
- Precise timing with `_NOP()` instead of `delayMicroseconds()`
- ~10x performance improvement

### Testing Notes
All fixes tested with `circus-in-rindo-battle-.mid`, which previously caused:
- Wild portamento/laser glitches starting at 14 seconds
- Stuck notes that persisted after MIDI Stop
- Progressive degradation as state corruption accumulated

After v1.44 fixes, the file plays cleanly without glitches.

**v1.45 additions:**
- LEDs now correctly flash on when notes are played (previously inverted)
- Noise channel properly stops when MIDI Stop is sent

**v1.46 additions:**
- Fixed noise channel stop order - now stops instantly even mid-note
- LED2 flashes when channel 10 (noise) notes are received

**v1.47 additions:**
- Corrected noise stop order: mute volume FIRST (mirrors noiseOn() order)
- Ensures noise stops immediately when MIDI Stop is sent

**v1.48 additions:**
- Fixed mixer value in noiseOff(): disables BOTH tone C and noise C (0x3C)
- Prevents stuck noise that wouldn't stop until next noise note
- Channels 7-9 can still play tones while noise is properly disabled

**v1.49 additions:**
- SIMPLIFIED noiseOff() to just mute volume - removed all complex register manipulation
- Matches how noiseOn() works: volume controls whether sound is audible
- More reliable and performant than previous attempts

---

## Configuration Parameters

### Velocity Curve Settings

The YM2149F has only 16 volume levels (0-15), which can make soft MIDI notes inaudible. These parameters allow you to customize how MIDI velocity (0-127) maps to YM volume.

#### `VELOCITY_GAMMA` (default: `0.4f`)
- **Range:** 0.1 - 1.0
- **Purpose:** Gamma curve exponent for velocity response
- **Lower values** (0.2-0.4): Boost soft notes significantly, compress loud notes
- **Higher values** (0.6-1.0): More linear response, preserves velocity dynamics
- **Recommended:** 0.4 for general use, 0.2-0.3 for very sensitive response

#### `VELOCITY_MIN` (default: `3`)
- **Range:** 0-15
- **Purpose:** Minimum YM volume for softest MIDI velocity (0)
- **0:** Softest notes are silent (maximum dynamic range)
- **3-4:** Softest notes are audible but quiet (recommended)
- **8-10:** All notes are fairly loud (compressed dynamics)
- **15:** All notes at maximum volume (no velocity response)

#### `VELOCITY_MAX` (default: `15`)
- **Range:** 1-15
- **Purpose:** Maximum YM volume for loudest MIDI velocity (127)
- **15:** Full volume range (recommended)
- **10-14:** Limit maximum loudness
- **Note:** Must be ≥ VELOCITY_MIN

**Example configurations:**

```cpp
// Balanced response (default)
#define VELOCITY_GAMMA  0.4f
#define VELOCITY_MIN    3
#define VELOCITY_MAX    15

// Very sensitive for quiet playing
#define VELOCITY_GAMMA  0.2f
#define VELOCITY_MIN    5
#define VELOCITY_MAX    15

// Compressed (all notes fairly loud)
#define VELOCITY_GAMMA  0.5f
#define VELOCITY_MIN    10
#define VELOCITY_MAX    15

// Maximum volume (ignore velocity)
#define VELOCITY_GAMMA  0.4f
#define VELOCITY_MIN    15
#define VELOCITY_MAX    15
```

### Volume Control Settings

#### `EXPRESSION_AMOUNT` (default: `1.0f`)
- **Range:** 0.0-1.0
- **Purpose:** Controls how much CC7 (Channel Volume) and CC11 (Expression) affect volume
- **0.0:** Bypass expression entirely (velocity only controls volume)
- **0.3-0.5:** Reduced expression effect (dynamic range compression)
- **1.0:** Full expression control (standard MIDI behavior)
- **Use case:** Reduce to 0.0-0.3 if MIDI files have very low CC7/CC11 values causing inaudible notes

#### `USE_CC4_ENVELOPE` (default: `1`)
- **Range:** 0 or 1
- **Purpose:** Enable/disable CC4 volume envelope shaping
- **1:** Enable CC4 volume envelopes (standard)
- **0:** Disable CC4 processing (bypass envelope)
- **Use case:** Disable if CC4 envelopes cause unwanted volume changes

**Example configurations:**

```cpp
// Standard MIDI behavior (default)
#define EXPRESSION_AMOUNT 1.0f
#define USE_CC4_ENVELOPE  1

// Bypass all MIDI volume control (velocity only)
#define EXPRESSION_AMOUNT 0.0f
#define USE_CC4_ENVELOPE  0

// Compressed dynamics (limit CC7/CC11 effect)
#define EXPRESSION_AMOUNT 0.3f
#define USE_CC4_ENVELOPE  1
```

### Noise Channel

#### `ENABLE_NOISE_CHANNEL` (default: `0`)
- **Range:** 0 or 1
- **Purpose:** Enable MIDI channel 10 (percussion) via noise generator
- **0:** Disabled (noise code excluded from compilation)
- **1:** Enabled (channel 10 uses chip 2 noise generator)
- **Note:** When enabled, chip 2 voice C is dedicated to noise and unavailable for tone

---

## Hardware

### YM2149F Sound Chips
- **3x YM2149F chips** providing 9 independent tone voices (3 per chip)
- Clock frequency: 500 kHz
- 4-bit volume control (0-15) per voice
- 12-bit period register per voice (valid range: 1-4095)

### Controller
- **Arduino Pro Micro** (ATmega32U4)
- 5V operation
- USB-MIDI via MIDIUSB library
- TRS-MIDI via Serial1 at 31250 baud

### Chip Selection
- 74HC138 3-to-8 line decoder
- Inverted logic: `chip = 2 - chip`
- Select pins: A3 (SEL_A), A1 (SEL_B), A0 (SEL_C)
- Enable pin: A2

### Data Bus Mapping
8-bit data bus mapped across multiple AVR ports:
- D0→PD1, D1→PD0, D2→PD4, D3→PC6
- D4→PD7, D5→PE6, D6→PB4, D7→PB5

### Control Pins
- BC1: D10 (PB6)
- BDIR: D20 (PF5)

### LEDs (Active-LOW)
- Chip 0: D15 (PB1)
- Chip 1: D14 (PB3)
- Chip 2: D16 (PB2)

### MIDI Routing
- **Channels 1-9** → Tone voices (3 chips × 3 voices)
  - Channels 1-3 → Chip 0 (voices A, B, C)
  - Channels 4-6 → Chip 1 (voices A, B, C)
  - Channels 7-9 → Chip 2 (voices A, B, C)
- **Channel 10** → Noise channel (Chip 2, voice C)
- **Channels 11-16** → Ignored (filtered out to prevent array bounds violations)
