#pragma once
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "config.h"

// ============================================================================
// MIDI HANDLER MODULE
// USB MIDI and TRS MIDI parsing and message handling
// ============================================================================

// --- USB MIDI Instance ---
extern Adafruit_USBD_MIDI usb_midi;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize MIDI interfaces
void midiInit();

// Process USB MIDI messages
void processUsbMidi();

// Parse serial MIDI byte (TRS MIDI)
void parseSerialMidi(uint8_t b);

// Handle decoded MIDI message
void handleMidiMsg(uint8_t status, uint8_t d1, uint8_t d2);

// Pitch bend handler
void pitchBend(uint8_t ch, uint8_t lsb, uint8_t msb);
