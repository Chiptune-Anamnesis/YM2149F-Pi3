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

    // --- 3D Rotating Wireframe Cube Splash (HC = HobbyChop) ---
    // Cube vertices (unit cube centered at origin)
    static const float cubeVerts[8][3] = {
      {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
      {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };
    // 12 edges as vertex index pairs
    static const uint8_t cubeEdges[12][2] = {
      {0,1},{1,2},{2,3},{3,0},  // back face
      {4,5},{5,6},{6,7},{7,4},  // front face
      {0,4},{1,5},{2,6},{3,7}   // connecting edges
    };
    // "H" letter segments on right face (x=+1 plane) — visible first
    static const float letterH[3][6] = {
      {1.01f,-0.6f, 0.5f, 1.01f, 0.6f, 0.5f},   // left vertical
      {1.01f,-0.6f,-0.5f, 1.01f, 0.6f,-0.5f},   // right vertical
      {1.01f, 0.0f, 0.5f, 1.01f, 0.0f,-0.5f}    // crossbar
    };
    // "C" letter segments on front face (z=+1 plane) — visible second
    static const float letterC[3][6] = {
      {-0.5f,-0.6f, 1.01f,  0.5f,-0.6f, 1.01f},  // top horizontal
      {-0.5f, 0.6f, 1.01f,  0.5f, 0.6f, 1.01f},  // bottom horizontal
      { 0.5f,-0.6f, 1.01f,  0.5f, 0.6f, 1.01f}   // right vertical (flipped)
    };

    const float scale = 52.0f;
    const float focal = 90.0f;
    const float camDist = 6.0f;
    const int cx = 64, cy = 32;  // centered on full display

    // Thick line helper: draw line with 1px offset in perpendicular direction
    auto thickLine = [&](int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
      display.drawLine(x0, y0, x1, y1, SH110X_WHITE);
      // Offset perpendicular to line direction for thickness
      int16_t dx = x1 - x0, dy = y1 - y0;
      if (abs(dx) > abs(dy)) {
        // More horizontal - offset vertically
        display.drawLine(x0, y0+1, x1, y1+1, SH110X_WHITE);
      } else {
        // More vertical - offset horizontally
        display.drawLine(x0+1, y0, x1+1, y1, SH110X_WHITE);
      }
    };

    // Fixed X-axis tilt (45 degrees) so cube balances on a corner
    const float tiltX = 0.7854f;  // pi/4
    const float cosT = cosf(tiltX);
    const float sinT = sinf(tiltX);

    unsigned long splashStart = millis();
    float angle = 0.4f;

    while (millis() - splashStart < 2000) {
      float cosA = cosf(angle);
      float sinA = sinf(angle);

      // Transform helper: Y-rotate then X-tilt a 3D point
      // Y-rot: x' = x*cosA - z*sinA, y' = y, z' = x*sinA + z*cosA
      // X-tilt: y'' = y'*cosT - z'*sinT, z'' = y'*sinT + z'*cosT

      // Rotate and project all 8 vertices
      int16_t px[8], py[8];
      for (uint8_t i = 0; i < 8; i++) {
        float rx = cubeVerts[i][0]*cosA - cubeVerts[i][2]*sinA;
        float ry = cubeVerts[i][1];
        float rz = cubeVerts[i][0]*sinA + cubeVerts[i][2]*cosA;
        // Apply X-axis tilt
        float ty = ry*cosT - rz*sinT;
        float tz = ry*sinT + rz*cosT;
        float invZ = focal / (tz * scale / focal + camDist);
        px[i] = cx + (int16_t)(rx * scale * invZ / focal);
        py[i] = cy + (int16_t)(ty * scale * invZ / focal);
      }

      display.clearDisplay();

      // Draw 12 wireframe edges (thick)
      for (uint8_t i = 0; i < 12; i++) {
        thickLine(px[cubeEdges[i][0]], py[cubeEdges[i][0]],
                  px[cubeEdges[i][1]], py[cubeEdges[i][1]]);
      }

      // Back-face culling with tilt:
      // Front face normal (0,0,1): after Y-rot = (sinA, 0, cosA), after X-tilt nz = 0*sinT + cosA*cosT
      // Right face normal (1,0,0): after Y-rot = (cosA, 0, -sinA), after X-tilt nz = 0*sinT + (-sinA)*cosT
      float nzFront = cosA * cosT;
      float nzRight = -sinA * cosT;

      // Helper lambda to transform and project a 3D point
      auto project = [&](float ix, float iy, float iz, int16_t &sx, int16_t &sy) {
        float rx = ix*cosA - iz*sinA;
        float ry = iy;
        float rz = ix*sinA + iz*cosA;
        float ty = ry*cosT - rz*sinT;
        float tz = ry*sinT + rz*cosT;
        float invZ = focal / (tz * scale / focal + camDist);
        sx = cx + (int16_t)(rx * scale * invZ / focal);
        sy = cy + (int16_t)(ty * scale * invZ / focal);
      };

      // Draw "H" on right face if visible (nz < 0 = facing camera)
      if (nzRight < -0.1f) {
        for (uint8_t i = 0; i < 3; i++) {
          int16_t sx0, sy0, sx1, sy1;
          project(letterH[i][0], letterH[i][1], letterH[i][2], sx0, sy0);
          project(letterH[i][3], letterH[i][4], letterH[i][5], sx1, sy1);
          thickLine(sx0, sy0, sx1, sy1);
        }
      }

      // Draw "C" on front face if visible (nz < 0 = facing camera)
      if (nzFront < -0.1f) {
        for (uint8_t i = 0; i < 3; i++) {
          int16_t sx0, sy0, sx1, sy1;
          project(letterC[i][0], letterC[i][1], letterC[i][2], sx0, sy0);
          project(letterC[i][3], letterC[i][4], letterC[i][5], sx1, sy1);
          thickLine(sx0, sy0, sx1, sy1);
        }
      }

      display.display();
      angle += 0.08f;
    }
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
  // If Core 1 needs to write flash, enter RAM-safe spin loop
  if (flashWriteInProgress) {
    core0FlashSafeLoop();
    return;
  }

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
