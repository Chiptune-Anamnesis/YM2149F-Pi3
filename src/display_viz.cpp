// ============================================================================
// DISPLAY VISUALIZATION MODULE
// Visualization rendering for bars, oscilloscope, matrix, and drum modes
// ============================================================================

#include "display.h"
#include "settings.h"
#include "dual_core.h"
#include "fx_chip.h"
#include "preset.h"
#include "sample_player.h"
#include "cat_anim.h"
#include <Fonts/TomThumb.h>

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Get target/scope prefix for pot (e.g., "FX:", "ALL:", "V1:")
const char* getPotTargetPrefix(const PotAssignment& pa) {
  // FX categories don't have voice targets
  if (pa.category >= PCAT_FX_ECHO && pa.category <= PCAT_FX_CHORUS) {
    return "FX:";
  }
  if (pa.category == PCAT_SAMPLE) return "SMP:";
  if (pa.category == PCAT_GLOBAL) return "GLB:";
  if (pa.category == PCAT_OFF) return "";

  // Voice-targeted parameters
  switch (pa.target) {
    case TARGET_ALL: return "ALL:";
    case TARGET_V1:  return "V1:";
    case TARGET_V2:  return "V2:";
    case TARGET_V3:  return "V3:";
    case TARGET_V4:  return "V4:";
    case TARGET_V5:  return "V5:";
    case TARGET_V6:  return "V6:";
    case TARGET_V7:  return "V7:";
    case TARGET_V8:  return "V8:";
    case TARGET_V9:  return "V9:";
    default:         return "?:";
  }
}

// Get category:param suffix for pot (e.g., "CHR:DET", "ENV:ATK")
const char* getPotCatParam(const PotAssignment& pa) {
  if (pa.category == PCAT_OFF) return "OFF";

  switch (pa.category) {
    case PCAT_VOICE:
      switch (pa.paramIndex) {
        case 0: return "VC:DET";
        case 1: return "VC:OCT";
        case 2: return "VC:VOL";
        case 3: return "VC:NSE";
        case 4: return "VC:SLD";
      }
      break;
    case PCAT_VIBRATO:
      switch (pa.paramIndex) {
        case 0: return "VIB:RT";
        case 1: return "VIB:DP";
        case 2: return "VIB:DL";
      }
      break;
    case PCAT_ENVELOPE:
      switch (pa.paramIndex) {
        case 0: return "ENV:ATK";
        case 1: return "ENV:DCY";
        case 2: return "ENV:SUS";
      }
      break;
    case PCAT_TREMOLO:
      switch (pa.paramIndex) {
        case 0: return "TRM:RT";
        case 1: return "TRM:DP";
      }
      break;
    case PCAT_PITCH_ENV:
      switch (pa.paramIndex) {
        case 0: return "PEN:AMT";
        case 1: return "PEN:TM";
        case 2: return "PEN:DIR";
      }
      break;
    case PCAT_SID:
      switch (pa.paramIndex) {
        case 0: return "SID:WAV";
        case 1: return "SID:DTY";
        case 2: return "SID:DET";
      }
      break;
    case PCAT_FX_ECHO:
      switch (pa.paramIndex) {
        case 0: return "ECO:DLY";
        case 1: return "ECO:RPT";
        case 2: return "ECO:DCY";
        case 3: return "ECO:VOL";
      }
      break;
    case PCAT_FX_ARP:
      switch (pa.paramIndex) {
        case 0: return "ARP:SPD";
        case 1: return "ARP:PTN";
        case 2: return "ARP:VOL";
        case 3: return "ARP:OCT";
      }
      break;
    case PCAT_FX_CRUSH:
      switch (pa.paramIndex) {
        case 0: return "CRU:BIT";
        case 1: return "CRU:RAT";
        case 2: return "CRU:VOL";
        case 3: return "CRU:DUR";
      }
      break;
    case PCAT_FX_REVERB:
      switch (pa.paramIndex) {
        case 0: return "RVB:TAP";
        case 1: return "RVB:SPC";
        case 2: return "RVB:DCY";
        case 3: return "RVB:DET";
        case 4: return "RVB:VOL";
      }
      break;
    case PCAT_FX_CHORUS:
      switch (pa.paramIndex) {
        case 0: return "CHR:DT1";
        case 1: return "CHR:DT2";
        case 2: return "CHR:VOL";
        case 3: return "CHR:DUR";
      }
      break;
    case PCAT_SAMPLE:
      switch (pa.paramIndex) {
        case 0: return "SEL";
        case 1: return "MOD";
        case 2: return "VOL";
      }
      break;
    case PCAT_GLOBAL:
      switch (pa.paramIndex) {
        case 0: return "MODE";
        case 1: return "LINK";
      }
      break;
    default: break;
  }
  return "???";
}

// ============================================================================
// BARS VISUALIZATION (default)
// ============================================================================

void updateDisplay() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  lastUpdate = now;

  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // === TOP SECTION: 3 Pot Circles with Labels (top + bottom text) ===
  const int potRadius = 7;
  const int potY = 15;  // Center Y of circles
  const int potSpacing = 42;
  const int potStartX = 21;

  for (uint8_t p = 0; p < 3; p++) {
    int cx = potStartX + p * potSpacing;
    int potVal = displaySnapshotCopy.potValues[p];
    const PotAssignment& pa = displaySnapshotCopy.potAssignments[p];

    // Top label: target prefix (e.g., "FX:", "ALL:", "V1:")
    const char* prefix = getPotTargetPrefix(pa);
    int prefixLen = strlen(prefix);
    display.setCursor(cx - (prefixLen * 3), potY - potRadius - 3);
    display.print(prefix);

    // Draw circle outline
    display.drawCircle(cx, potY, potRadius, SH110X_WHITE);

    // Fill based on pot value (0-1000 mapped to fill height)
    int fillHeight = map(constrain(potVal, 0, 1000), 0, 1000, 0, potRadius * 2);
    if (fillHeight > 0) {
      for (int y = 0; y < fillHeight; y++) {
        int dy = potRadius - y;
        if (dy >= -potRadius && dy <= potRadius) {
          int halfWidth = (int)sqrt((float)(potRadius * potRadius - dy * dy));
          if (halfWidth > 0) {
            display.drawFastHLine(cx - halfWidth, potY + dy, halfWidth * 2, SH110X_WHITE);
          }
        }
      }
    }

    // Bottom label: category:param (e.g., "CHR:DET", "ENV:ATK")
    const char* catParam = getPotCatParam(pa);
    int catLen = strlen(catParam);
    display.setCursor(cx - (catLen * 3), potY + potRadius + 9);
    display.print(catParam);
  }

  // === MIDDLE SECTION: 9 Voice Bars (compact) ===
  const int barWidth = 12;
  const int barSpacing = 1;
  const int barMaxHeight = 20;
  const int barBaseY = 53;
  const int barTopY = barBaseY - barMaxHeight;

  for (uint8_t ch = 0; ch < 9; ch++) {
    int x = 3 + ch * (barWidth + barSpacing);
    uint8_t chip = ch / 3;
    uint8_t voice = ch % 3;

    int barHeight = 0;
    if (displaySnapshotCopy.voiceActive[chip][voice] && displaySnapshotCopy.voiceVol[chip][voice] > 0) {
      barHeight = map(displaySnapshotCopy.voiceVol[chip][voice], 0, 15, 3, barMaxHeight);
    }

    if (barHeight > 0) {
      display.fillRect(x, barBaseY - barHeight, barWidth, barHeight, SH110X_WHITE);
    }

    display.drawRect(x, barTopY, barWidth, barMaxHeight, SH110X_WHITE);
  }

  // === BOTTOM SECTION: FX type (left) and HOBBYCHOP (right) ===
  const int bottomY = 63;

  // FX type on left
  display.setCursor(0, bottomY);
  display.print("FX:");
  if (displaySnapshotCopy.fxModeEnabled && displaySnapshotCopy.fxType != FX_NONE) {
    display.print(getFxTypeName(displaySnapshotCopy.fxType));
  } else {
    display.print("NONE");
  }

  // Preset name on right
  display.setCursor(73, bottomY);
  display.print("PRST:");
  if (currentPresetIndex == PRESET_INDEX_NONE) {
    display.print("---");
  } else {
    char nameBuf[9];
    presetGetName(currentPresetIndex, nameBuf);
    display.print(nameBuf);
  }

}

// ============================================================================
// OSCILLOSCOPE VISUALIZATION
// ============================================================================

void updateDisplayScope() {
  static unsigned long lastUpdate = 0;
  static float phaseAccum[9] = {0};  // Persistent phase accumulators per voice

  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  float dt = (now - lastUpdate) / 1000.0f;
  lastUpdate = now;

  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // === SCOPE AREA: Full width, y=4 to y=50 ===
  const int scopeY = 4;
  const int scopeH = 46;
  const int scopeMidY = scopeY + scopeH / 2;

  // Draw center line (zero crossing)
  display.drawFastHLine(0, scopeMidY, 128, SH110X_WHITE);

  // Update phase accumulators for each active voice
  for (uint8_t ch = 0; ch < 9; ch++) {
    uint8_t chip = ch / 3;
    uint8_t voice = ch % 3;

    if (displaySnapshotCopy.voiceActive[chip][voice] &&
        displaySnapshotCopy.voiceVol[chip][voice] > 0) {
      // Derive frequency from actual YM2149 period (captures all pitch mods)
      uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
      float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;

      // Scale frequency for visual appeal (show ~2-4 cycles on screen)
      float visualFreq = freq * 0.002f;
      phaseAccum[ch] += visualFreq * dt * 60.0f;  // 60 = rough frame scaling
      if (phaseAccum[ch] > 1000.0f) phaseAccum[ch] -= 1000.0f;
    } else {
      // Slowly decay phase when voice stops
      phaseAccum[ch] *= 0.95f;
    }
  }

  // Render composite waveform
  int prevY = scopeMidY;
  for (int x = 0; x < 128; x++) {
    float sum = 0;
    int activeCount = 0;

    for (uint8_t ch = 0; ch < 9; ch++) {
      uint8_t chip = ch / 3;
      uint8_t voice = ch % 3;

      if (displaySnapshotCopy.voiceActive[chip][voice] &&
          displaySnapshotCopy.voiceVol[chip][voice] > 0) {
        // Derive frequency from actual YM2149 period (captures all pitch mods)
        uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
        float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;

        // Phase for this x position (spread across screen width)
        float phase = phaseAccum[ch] + (x / 128.0f) * (freq * 0.01f);

        // Square wave (YM2149 native waveform)
        float wave = (fmodf(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f;

        // Scale by volume
        float vol = displaySnapshotCopy.voiceVol[chip][voice] / 15.0f;
        sum += wave * vol;
        activeCount++;
      }
    }

    int y = scopeMidY;
    if (activeCount > 0) {
      sum /= sqrtf((float)activeCount);  // Normalize with gentle rolloff
      y = scopeMidY - (int)(sum * (scopeH / 2 - 4));
      y = constrain(y, scopeY + 1, scopeY + scopeH - 2);
    }

    // Draw line from previous point for smooth waveform
    if (x > 0) {
      display.drawLine(x - 1, prevY, x, y, SH110X_WHITE);
    }
    prevY = y;
  }

  // === BOTTOM SECTION: Same as bar mode ===
  const int bottomY = 63;

  // FX type on left
  display.setCursor(0, bottomY);
  display.print("FX:");
  if (displaySnapshotCopy.fxModeEnabled && displaySnapshotCopy.fxType != FX_NONE) {
    display.print(getFxTypeName(displaySnapshotCopy.fxType));
  } else {
    display.print("NONE");
  }

  // Preset name on right
  display.setCursor(73, bottomY);
  display.print("PRST:");
  if (currentPresetIndex == PRESET_INDEX_NONE) {
    display.print("---");
  } else {
    char nameBuf[9];
    presetGetName(currentPresetIndex, nameBuf);
    display.print(nameBuf);
  }

}

// ============================================================================
// MATRIX VISUALIZATION (3x3 grid of individual voice oscilloscopes)
// ============================================================================

void updateDisplayMatrix() {
  static unsigned long lastUpdate = 0;
  static float phaseAccum[9] = {0};  // Persistent phase accumulators per voice

  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  float dt = (now - lastUpdate) / 1000.0f;
  lastUpdate = now;

  display.clearDisplay();

  // === 3x3 GRID LAYOUT ===
  // Screen: 128x64, Grid: 3 cols x 3 rows
  // Cell size: 42x21 pixels (with 1px borders)
  const int cellW = 42;
  const int cellH = 21;
  const int innerW = cellW - 2;  // 40 pixels for waveform
  const int innerH = cellH - 2;  // 19 pixels for waveform

  // Update phase accumulators for each voice
  for (uint8_t ch = 0; ch < 9; ch++) {
    uint8_t chip = ch / 3;
    uint8_t voice = ch % 3;

    if (displaySnapshotCopy.voiceActive[chip][voice] &&
        displaySnapshotCopy.voiceVol[chip][voice] > 0) {
      // Derive frequency from actual YM2149 period (captures all pitch mods)
      uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
      float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;
      float visualFreq = freq * 0.002f;
      phaseAccum[ch] += visualFreq * dt * 60.0f;
      if (phaseAccum[ch] > 1000.0f) phaseAccum[ch] -= 1000.0f;
    } else {
      phaseAccum[ch] *= 0.95f;
    }
  }

  // Draw each cell
  for (uint8_t chip = 0; chip < 3; chip++) {
    for (uint8_t voice = 0; voice < 3; voice++) {
      uint8_t ch = chip * 3 + voice;

      // Cell position (voice = column, chip = row)
      int cellX = voice * cellW;
      int cellY = chip * cellH;
      int innerX = cellX + 1;
      int innerY = cellY + 1;
      int midY = innerY + innerH / 2;

      bool isActive = displaySnapshotCopy.voiceActive[chip][voice] &&
                      displaySnapshotCopy.voiceVol[chip][voice] > 0;

      // Draw cell border (dim for inactive, bright for active)
      if (isActive) {
        display.drawRect(cellX, cellY, cellW, cellH, SH110X_WHITE);
      } else {
        // Just corner dots for inactive cells
        display.drawPixel(cellX, cellY, SH110X_WHITE);
        display.drawPixel(cellX + cellW - 1, cellY, SH110X_WHITE);
        display.drawPixel(cellX, cellY + cellH - 1, SH110X_WHITE);
        display.drawPixel(cellX + cellW - 1, cellY + cellH - 1, SH110X_WHITE);
      }

      // Draw cell label (e.g., "0A", "1B", "2C")
      display.setFont(&TomThumb);
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(innerX + 1, innerY + 5);
      display.print(chip);
      display.print((char)('A' + voice));

      // Draw center line
      display.drawFastHLine(innerX, midY, innerW, SH110X_WHITE);

      if (isActive) {
        // Derive frequency from actual YM2149 period (captures all pitch mods)
        uint16_t period = displaySnapshotCopy.voicePeriod[chip][voice];
        float freq = (period > 0) ? (1789772.5f / (16.0f * period)) : 440.0f;
        float vol = displaySnapshotCopy.voiceVol[chip][voice] / 15.0f;

        int prevY = midY;
        for (int x = 0; x < innerW; x++) {
          float phase = phaseAccum[ch] + (x / (float)innerW) * (freq * 0.008f);
          float wave = (fmodf(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f;

          int amplitude = (int)(wave * vol * (innerH / 2 - 1));
          int y = midY - amplitude;
          y = constrain(y, innerY + 1, innerY + innerH - 2);

          if (x > 0) {
            display.drawLine(innerX + x - 1, prevY, innerX + x, y, SH110X_WHITE);
          }
          prevY = y;
        }
      }
    }
  }

}

void updateDisplayChannelMatrix() {
  static unsigned long lastUpdate = 0;
  static float phaseAccum[9] = {0};  // Persistent phase accumulators per channel

  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  float dt = (now - lastUpdate) / 1000.0f;
  lastUpdate = now;

  display.clearDisplay();

  const int cellW = 42;
  const int cellH = 21;
  const int innerW = cellW - 2;
  const int innerH = cellH - 2;

  // For each MIDI channel 0-8, find the best active voice data
  uint8_t chanVol[9] = {0};
  uint16_t chanPeriod[9] = {0};
  bool chanActive[9] = {false};

  for (uint8_t ch = 0; ch < 9; ch++) {
    for (uint8_t c = 0; c < 3; c++) {
      for (uint8_t v = 0; v < 3; v++) {
        if (displaySnapshotCopy.voiceChan[c][v] == ch &&
            displaySnapshotCopy.voiceActive[c][v] &&
            displaySnapshotCopy.voiceVol[c][v] > 0) {
          chanActive[ch] = true;
          if (displaySnapshotCopy.voiceVol[c][v] > chanVol[ch]) {
            chanVol[ch] = displaySnapshotCopy.voiceVol[c][v];
            chanPeriod[ch] = displaySnapshotCopy.voicePeriod[c][v];
          }
        }
      }
    }
  }

  // Update phase accumulators
  for (uint8_t ch = 0; ch < 9; ch++) {
    if (chanActive[ch] && chanPeriod[ch] > 0) {
      float freq = 1789772.5f / (16.0f * chanPeriod[ch]);
      float visualFreq = freq * 0.002f;
      phaseAccum[ch] += visualFreq * dt * 60.0f;
      if (phaseAccum[ch] > 1000.0f) phaseAccum[ch] -= 1000.0f;
    } else {
      phaseAccum[ch] *= 0.95f;
    }
  }

  // Draw each cell (row = channel group 0-2, col = channel within group 0-2)
  for (uint8_t row = 0; row < 3; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      uint8_t ch = row * 3 + col;

      int cellX = col * cellW;
      int cellY = row * cellH;
      int innerX = cellX + 1;
      int innerY = cellY + 1;
      int midY = innerY + innerH / 2;

      bool isActive = chanActive[ch];

      if (isActive) {
        display.drawRect(cellX, cellY, cellW, cellH, SH110X_WHITE);
      } else {
        display.drawPixel(cellX, cellY, SH110X_WHITE);
        display.drawPixel(cellX + cellW - 1, cellY, SH110X_WHITE);
        display.drawPixel(cellX, cellY + cellH - 1, SH110X_WHITE);
        display.drawPixel(cellX + cellW - 1, cellY + cellH - 1, SH110X_WHITE);
      }

      // Label: "C1"-"C9"
      display.setFont(&TomThumb);
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(innerX + 1, innerY + 5);
      display.print('C');
      display.print(ch + 1);

      display.drawFastHLine(innerX, midY, innerW, SH110X_WHITE);

      if (isActive && chanPeriod[ch] > 0) {
        float freq = 1789772.5f / (16.0f * chanPeriod[ch]);
        float vol = chanVol[ch] / 15.0f;

        int prevY = midY;
        for (int x = 0; x < innerW; x++) {
          float phase = phaseAccum[ch] + (x / (float)innerW) * (freq * 0.008f);
          float wave = (fmodf(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f;

          int amplitude = (int)(wave * vol * (innerH / 2 - 1));
          int y = midY - amplitude;
          y = constrain(y, innerY + 1, innerY + innerH - 2);

          if (x > 0) {
            display.drawLine(innerX + x - 1, prevY, innerX + x, y, SH110X_WHITE);
          }
          prevY = y;
        }
      }
    }
  }

}

// ============================================================================
// DRUM SAMPLE WAVEFORM VISUALIZATION
// ============================================================================

void updateDisplayDrums() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate < DISPLAY_UPDATE_MS) return;
  lastUpdate = now;

  // Keep a static copy of last sample data so waveform persists after playback ends
  static const uint8_t* lastData = nullptr;
  static uint16_t lastLen = 0;
  static uint8_t lastNote = 0;
  static uint8_t lastSampleIdx = 0;

  // Idle tracking for cat animation
  static unsigned long lastSamplePlayTime = 0;
  static uint8_t catFrame = 0;
  static unsigned long lastCatFrameTime = 0;

  // Track when samples are playing
  if (displaySnapshotCopy.sampleIsPlaying) {
    lastSamplePlayTime = now;
  }

  // Update static copy when a new sample starts
  if (displaySnapshotCopy.sampleData != nullptr && displaySnapshotCopy.sampleLength > 0) {
    lastData = displaySnapshotCopy.sampleData;
    lastLen = displaySnapshotCopy.sampleLength;
    lastNote = displaySnapshotCopy.sampleTriggeredNote;
    lastSampleIdx = displaySnapshotCopy.sampleSelect;
  }

  display.clearDisplay();

  // Show cat animation when idle: never played, or 30s since last sample
  bool showCat = (lastData == nullptr || lastLen == 0) ||
                 (!displaySnapshotCopy.sampleIsPlaying && lastSamplePlayTime > 0 &&
                  (now - lastSamplePlayTime >= 30000));

  if (showCat) {
    // Animate cat frames (~150ms per frame)
    if (now - lastCatFrameTime >= 150) {
      catFrame = (catFrame + 1) % CAT_FRAME_COUNT;
      lastCatFrameTime = now;
    }

    // "404" large text centered
    display.setFont(NULL);
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(44, 2);
    display.print("404");

    // Running cat bitmap centered
    display.drawBitmap(48, 16, catFrames[catFrame], CAT_FRAME_W, CAT_FRAME_H, SH110X_WHITE);

    // "SAMPLE NOT FOUND" small text centered
    display.setTextSize(1);
    display.setCursor(10, 52);
    display.print("SAMPLE NOT FOUND");
    return;
  }

  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Waveform area
  const int waveTop = 8;
  const int waveBottom = 54;
  const int waveH = waveBottom - waveTop;

  // Top label: note name + sample index
  static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int noteName = lastNote % 12;
  int octave = (lastNote / 12) - 1;

  display.setCursor(0, 6);
  display.print(noteNames[noteName]);
  display.print(octave);

  display.setCursor(100, 6);
  display.print("S");
  if (lastSampleIdx < 10) display.print("0");
  display.print(lastSampleIdx);

  // Draw waveform - fit entire sample across 128 pixels
  int prevY = waveBottom;
  for (int x = 0; x < 128; x++) {
    uint32_t idx = ((uint32_t)x * lastLen) / 128;
    if (idx >= lastLen) idx = lastLen - 1;

    uint8_t val = lastData[idx];

    // Map 8-bit value (0-255) to display Y (waveBottom=low, waveTop=high)
    int y = waveBottom - ((val * waveH) >> 8);
    y = constrain(y, waveTop, waveBottom);

    if (x > 0) {
      display.drawLine(x - 1, prevY, x, y, SH110X_WHITE);
    }
    prevY = y;
  }

  // Draw playback cursor if playing
  if (displaySnapshotCopy.sampleIsPlaying && displaySnapshotCopy.sampleLength > 0) {
    int cursorX = ((uint32_t)displaySnapshotCopy.samplePosition * 127) / displaySnapshotCopy.sampleLength;
    cursorX = constrain(cursorX, 0, 127);
    display.drawFastVLine(cursorX, waveTop, waveH, SH110X_WHITE);
  }

  // Bottom section
  const int bottomY = 63;
  display.setCursor(0, bottomY);
  display.print("VOL:");
  display.print(displaySnapshotCopy.sampleVolume);

  display.setCursor(90, bottomY);
  display.print("MODE:");
  display.print(getSampleModeName(displaySnapshotCopy.sampleMode));

}
