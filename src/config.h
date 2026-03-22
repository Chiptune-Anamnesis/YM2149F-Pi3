#pragma once
#include <Arduino.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// OLED Display (128x64 SH1106 I2C on Wire1)
#define PIN_OLED_SDA 14
#define PIN_OLED_SCL 15
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Rotary Encoder (for menu navigation)
#define PIN_ENCODER_A 27
#define PIN_ENCODER_B 28
#define PIN_ENCODER_BTN 22  // Encoder button on GPIO 22 (shares with LED2, with internal pullup)

// CD74HC4067 Multiplexer for pots only
#define PIN_MUX_S0 17   // CD74HC4067 S0 select
#define PIN_MUX_S1 18   // CD74HC4067 S1 select
#define PIN_MUX_S2 19   // CD74HC4067 S2 select
#define PIN_MUX_SIG 26  // ADC0 - CD74HC4067 SIG

// ============================================================================
// FEATURE TOGGLES
// ============================================================================

// Toggle YM file streaming via USB CDC
#define USE_YMPLAYER_SERIAL 0

// Toggle noise channel support (MIDI channel 10)
#define ENABLE_NOISE_CHANNEL 0

// CC4 volume envelope
#define USE_CC4_ENVELOPE 1

// ============================================================================
// VELOCITY & EXPRESSION
// ============================================================================

#define VELOCITY_GAMMA  1.5f    // Gamma curve (>1 = more dynamic range, <1 = compressed)
#define VELOCITY_MIN    2       // Minimum YM volume (0-15)
#define VELOCITY_MAX    15      // Maximum YM volume (1-15)
#define EXPRESSION_AMOUNT 0.3f  // How much CC7/CC11 affects volume (0.0-1.0)

// ============================================================================
// PITCH & MODULATION
// ============================================================================

#define OCTAVE_SHIFT 0          // Global octave transpose in semitones
#define DEFAULT_VIB_RATE  7.0f  // Hz
#define DEFAULT_VIB_RANGE 0.5f  // semitone

// Portamento
#define PORTA_MIN 0.005f
#define PORTA_MAX 0.5f

// Note range
#define MIDI_NOTE_MIN 9    // A-1 = 13.75 Hz (extended bass range)
#define MIDI_NOTE_MAX 108  // C8

// ============================================================================
// SID MODE DEFAULTS
// ============================================================================

#define SID_MAX_PATTERN 32
#define SID_TIMER_INTERVAL_US 20  // Timer fires every 20µs (~50kHz) for NextSID volume flipping

// ============================================================================
// TIMING
// ============================================================================

#define LED_FLASH_MS 100
#define ENCODER_DEBOUNCE_MS 30
#define DISPLAY_UPDATE_MS 20
#define PITCH_MOD_UPDATE_MS 3

// ============================================================================
// MIDI TO CHIP MAPPING
// ============================================================================

// Maps MIDI channels 0-15 to YM chips 0-2
// Channels 1-3 (idx 0-2) → Chip 0, Channels 4-6 (idx 3-5) → Chip 1, Channels 7-9 (idx 6-8) → Chip 2
// Channels 10-16 (idx 9-15) wrap to Chip 0/1/2 to prevent out-of-bounds
const uint8_t midiToChip[16] = {0,0,0, 1,1,1, 2,2,2, 0,0,0, 1,1,1, 2};

// Note: YM_CLOCK_HZ, LED_ON, LED_OFF are defined in YM2149.h
// PIN_MIDI_TX and PIN_MIDI_RX are defined in YM2149.h
