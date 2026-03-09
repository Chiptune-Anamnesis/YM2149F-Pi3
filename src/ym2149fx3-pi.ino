// ============================================================================
// YM2149Fx3 - Triple YM2149 MIDI Synthesizer
// Main sketch file
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include "hardware/timer.h"

// Project modules
#include "config.h"
#include "YM2149.h"
#include "encoder.h"
#include "sid_mode.h"
#include "effects.h"
#include "voice_manager.h"
#include "display.h"
#include "midi_handler.h"
#include "settings.h"
#include "dual_core.h"
#include "fx_chip.h"
#include "sample_player.h"
#include "preset.h"

#if USE_YMPLAYER_SERIAL
  #include "YMPlayerSerial.h"
  YMPlayerSerial player;
#endif

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

YM2149 ym;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  // Initialize USB FIRST - must happen early for enumeration
  midiInit();

  delay(500);  // Wait for power rails and external ICs to stabilize

  // Initialize YM2149 hardware
  ym.begin();

  // Initialize OLED display
  if (displayInit()) {
    delay(50);

    // Splash screen
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(10, 20);
    display.print("HobbyChop");
    display.setTextSize(1);
    display.setCursor(30, 45);
    display.print("YM2149Fx3");
    display.display();
    delay(2000);
  }

  // Initialize multiplexer pins
  pinMode(PIN_MUX_S0, OUTPUT);
  pinMode(PIN_MUX_S1, OUTPUT);
  pinMode(PIN_MUX_S2, OUTPUT);
  pinMode(PIN_MUX_SIG, INPUT);

  // Initialize encoder
  encoderInit();

  delay(100);

  // Reset all YM2149 chips to known state
  for (uint8_t c = 0; c < 3; c++) {
    ym.write(c, 0x08, 0x00);  // Channel A volume = 0
    ym.write(c, 0x09, 0x00);  // Channel B volume = 0
    ym.write(c, 0x0A, 0x00);  // Channel C volume = 0
    ym.write(c, 0x07, 0b00111111);  // All disabled
    ym.write(c, 0x00, 0x00);
    ym.write(c, 0x01, 0x00);
    ym.write(c, 0x02, 0x00);
    ym.write(c, 0x03, 0x00);
    ym.write(c, 0x04, 0x00);
    ym.write(c, 0x05, 0x00);
  }

  delay(50);

  // LED startup animation (only LED0 and LED1)
  for (uint8_t i = 0; i < 2; i++) {
    ym.setLED(i, false);  // Turn ON
    delay(100);
    ym.setLED(i, true);   // Turn OFF
    delay(50);
  }

  // Initialize note-to-period lookup table
  initNotePeriodLUT();

  // Initialize all YM chips: enable tones, silence all voices
  for (uint8_t c = 0; c < 3; c++) {
    enableTones(c);
    for (uint8_t v = 0; v < 3; v++) stopVoice(c, v);
  }

  // Initialize settings FIRST (other modules depend on chipSettings)
  settingsInit();

  // Initialize preset system (reads header from flash)
  presetInit();

  // Initialize SID mode
  sidInit();

  // If device booted with SID mode enabled, activate it now
  if (sidModeGlobal) {
    sidModeInit();
  }

  // Initialize effects
  effectsInit();

  // Initialize voice manager
  voiceManagerInit();

  // Initialize FX chip module
  fxInit();

  // Initialize sample player (4000 Hz timer for digidrum playback)
  samplePlayerInit();

  // TRS MIDI on UART1 (GPIO 20/21)
  Serial2.setTX(PIN_MIDI_TX);
  Serial2.setRX(PIN_MIDI_RX);
  Serial2.begin(31250);

#if USE_YMPLAYER_SERIAL
  Serial.begin(115200);
  player.begin();
#endif

  // Initialize dual-core communication
  dualCoreInit();

  // Launch Core 1 for display/encoder handling
  core1Running = true;
  multicore_launch_core1(core1Entry);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // TRS MIDI (UART1 = Serial2)
  while (Serial2.available()) {
    parseSerialMidi(Serial2.read());
  }

  // LED flash timing (only LED0 and LED1)
  unsigned long now = millis();
  for (uint8_t c = 0; c < 2; c++) {
    if (ledOnTime[c] && now - ledOnTime[c] >= LED_FLASH_MS) {
      ym.setLED(c, true);  // Turn OFF
      ledOnTime[c] = 0;
    }
  }

  // USB MIDI
#if !USE_YMPLAYER_SERIAL
  processUsbMidi();
#else
  player.update();
#endif

  // Pitch modulation and envelope update (~3ms)
  static unsigned long lastPitchUpdate = millis();
  unsigned long m = millis();
  if (m - lastPitchUpdate >= PITCH_MOD_UPDATE_MS) {
    lastPitchUpdate = m;

    // Update all voice envelopes independently (per-voice)
    updateAllEnvelopes();

    // Update pitch modulation per channel
    for (uint8_t ch = 0; ch < 9; ++ch) {
      updatePitchMod(ch);
    }
  }

  // Update pot readings and apply mapped parameters
  updatePots();

  // Process commands from Core 1 (display/encoder)
  processCommands();

  // Update FX chip effects (echo, arp, etc.)
  fxUpdate();
}

// ============================================================================
// NOISE CHANNEL (optional)
// ============================================================================

#if ENABLE_NOISE_CHANNEL
void noiseOn(uint8_t note, uint8_t vel) {
  uint8_t chip = 2;
  uint8_t nf = constrain((int)note - 24, 2, 31);
  ymSafeWrite(chip, 6, nf);
  ymSafeWrite(chip, 7, 0b00011100);
  ymSafeWrite(chip, 8 + 2, vel >> 3);
}

void noiseOff() {
  uint8_t chip = 2;
  ymSafeWrite(chip, 8 + 2, 0);
  ymSafeWrite(chip, 7, 0b00111000);
}
#endif
