#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// ENCODER MODULE
// Interrupt-driven rotary encoder with button debouncing
// ============================================================================

// Initialize encoder pins and attach interrupts
void encoderInit();

// ISR for encoder rotation (called on pin change)
void encoderISR();

// Get accumulated encoder movement since last call
// Returns: -1, 0, or +1 based on detent movement
int getEncoderDelta();

// Check if button was pressed (with debounce)
// Returns true on rising edge (button release)
bool encoderButtonPressed();

// Check if button is being held for long press (1 second)
// Returns true once when threshold reached, resets on release
bool encoderButtonLongPressed();

// Read multiplexer channel (digital)
bool readMuxChannel(uint8_t channel);

// Read multiplexer channel (analog)
int readMuxAnalog(uint8_t channel);
