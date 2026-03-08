#include "encoder.h"

// ============================================================================
// ENCODER STATE
// ============================================================================

static volatile int8_t encoderPosition = 0;
static volatile uint8_t encoderLastState = 0;

static bool lastButtonState = HIGH;
static unsigned long lastButtonStableTime = 0;
static bool rawButtonState = HIGH;

// Long press detection
#define LONG_PRESS_MS 1000
static unsigned long buttonPressStart = 0;
static bool longPressTriggered = false;
static bool suppressNextRelease = false;  // Suppress button release after long press

// ============================================================================
// ENCODER ISR
// ============================================================================

void encoderISR() {
  uint8_t a = digitalRead(PIN_ENCODER_A);
  uint8_t b = digitalRead(PIN_ENCODER_B);
  uint8_t currentState = (a << 1) | b;

  // Simple and reliable: check specific transitions
  // CW:  00->10->11->01->00  (A leads B)
  // CCW: 00->01->11->10->00  (B leads A)

  uint8_t transition = (encoderLastState << 2) | currentState;

  switch (transition) {
    // Clockwise transitions
    case 0b0010:  // 00 -> 10
    case 0b1011:  // 10 -> 11
    case 0b1101:  // 11 -> 01
    case 0b0100:  // 01 -> 00
      encoderPosition++;
      break;

    // Counter-clockwise transitions
    case 0b0001:  // 00 -> 01
    case 0b0111:  // 01 -> 11
    case 0b1110:  // 11 -> 10
    case 0b1000:  // 10 -> 00
      encoderPosition--;
      break;
  }

  encoderLastState = currentState;
}

// ============================================================================
// ENCODER FUNCTIONS
// ============================================================================

void encoderInit() {
  // Initialize encoder pins with internal pull-ups
  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_BTN, INPUT_PULLUP);

  // Initialize encoder state
  uint8_t a = digitalRead(PIN_ENCODER_A);
  uint8_t b = digitalRead(PIN_ENCODER_B);
  encoderLastState = (a << 1) | b;

  // Attach interrupts for reliable encoder reading
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), encoderISR, CHANGE);
}

int getEncoderDelta() {
  // Atomic read and reset
  noInterrupts();
  int8_t pos = encoderPosition;
  encoderPosition = 0;
  interrupts();

  // Convert accumulated position to menu movement
  // Threshold of 4 = one detent on most encoders
  // Note: direction reversed (negate result)
  if (pos >= 4) return -1;
  if (pos <= -4) return 1;

  // If less than threshold, put it back for accumulation
  if (pos != 0) {
    noInterrupts();
    encoderPosition += pos;
    interrupts();
  }

  return 0;
}

bool encoderButtonPressed() {
  bool currentRead = digitalRead(PIN_ENCODER_BTN);

  // If reading changed, reset stability timer
  if (currentRead != rawButtonState) {
    rawButtonState = currentRead;
    lastButtonStableTime = millis();
  }

  // Only update debounced state if signal has been stable for debounce period
  if ((millis() - lastButtonStableTime) > ENCODER_DEBOUNCE_MS) {
    if (rawButtonState != lastButtonState) {
      bool wasReleased = (lastButtonState == LOW && rawButtonState == HIGH);
      lastButtonState = rawButtonState;

      // If this release follows a long press, suppress it
      if (wasReleased && suppressNextRelease) {
        suppressNextRelease = false;
        return false;
      }
      return wasReleased;
    }
  }

  return false;
}

bool encoderButtonLongPressed() {
  bool currentRead = digitalRead(PIN_ENCODER_BTN);

  if (currentRead == LOW) {  // Button held down
    if (buttonPressStart == 0) {
      buttonPressStart = millis();
    } else if (!longPressTriggered && (millis() - buttonPressStart >= LONG_PRESS_MS)) {
      longPressTriggered = true;
      suppressNextRelease = true;  // Don't trigger short press on release
      return true;  // Long press detected
    }
  } else {  // Button released
    buttonPressStart = 0;
    longPressTriggered = false;
  }
  return false;
}

// ============================================================================
// MULTIPLEXER FUNCTIONS
// ============================================================================

bool readMuxChannel(uint8_t channel) {
  // Set channel select pins (S0-S2 for channels 0-7, S3 tied to GND)
  digitalWrite(PIN_MUX_S0, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(PIN_MUX_S1, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(PIN_MUX_S2, (channel & 0x04) ? HIGH : LOW);

  // Small delay for multiplexer switching
  delayMicroseconds(5);

  // Read from signal pin as digital
  return digitalRead(PIN_MUX_SIG);
}

int readMuxAnalog(uint8_t channel) {
  // Set channel select pins (S0-S2 for channels 0-7, S3 tied to GND)
  digitalWrite(PIN_MUX_S0, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(PIN_MUX_S1, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(PIN_MUX_S2, (channel & 0x04) ? HIGH : LOW);

  // Small delay for multiplexer switching
  delayMicroseconds(10);

  // Read from signal pin as analog (0-4095 on Pico ADC)
  return analogRead(PIN_MUX_SIG);
}
