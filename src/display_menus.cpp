#include "display.h"
#include "encoder.h"
#include "voice_manager.h"
#include "sid_mode.h"
#include "settings.h"
#include "dual_core.h"
#include "fx_chip.h"
#include "sample_player.h"
#include "preset.h"
#include <Fonts/TomThumb.h>

// ============================================================================
// FX SUBMENU
// ============================================================================

// Get number of visible items based on FX type
int getVisibleFXItemCount() {
  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      return 8;  // ENABLED, TYPE, ROUTING, DELAY, REPEATS, DECAY, VOLUME, BACK
    case FX_ARP:
      return 8;  // ENABLED, TYPE, ROUTING, PATTERN, SPEED, VOLUME, OCTAVE, BACK
    case FX_BIT_CRUSH:
      return 8;  // ENABLED, TYPE, ROUTING, BITS, RATE, VOLUME, DURATION, BACK
    case FX_PSEUDO_REVERB:
      return 9;  // ENABLED, TYPE, ROUTING, TAPS, SPACE, DECAY, DETUNE, VOLUME, BACK
    case FX_CHORUS:
      return 8;  // ENABLED, TYPE, ROUTING, DET1, DET2, VOLUME, DURATION, BACK
    case FX_HARMONIZER:
      return 7;  // ENABLED, TYPE, ROUTING, CHORD, VOLUME, OCTAVE, BACK
    case FX_GATE:
      return 9;  // ENABLED, TYPE, ROUTING, RATE, PATTERN, VOLUME, DUTY, SEED, BACK
    default:
      return 4;  // ENABLED, TYPE, ROUTING, BACK
  }
}

// Get FX menu item label based on selection and current type
const char* getFXLabel(int sel) {
  if (sel == FXMENU_ENABLED) return "ENABLED";
  if (sel == FXMENU_TYPE) return "TYPE";
  if (sel == FXMENU_ROUTING) return "ROUTE";

  // Context-dependent items based on FX type
  uint8_t type = displaySnapshotCopy.fxType;
  int lastItem = getVisibleFXItemCount() - 1;

  if (sel == lastItem) return "< BACK";

  // Parameter items
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) return "DELAY";
      if (sel == FXMENU_PARAM2) return "REPS";
      if (sel == FXMENU_PARAM3) return "DECAY";
      if (sel == FXMENU_PARAM4) return "VOLUME";
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) return "PATTERN";
      if (sel == FXMENU_PARAM2) return "SPEED";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "OCTAVE";
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) return "BITS";
      if (sel == FXMENU_PARAM2) return "RATE";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "SUSTAIN";
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) return "TAPS";
      if (sel == FXMENU_PARAM2) return "SPACE";
      if (sel == FXMENU_PARAM3) return "DECAY";
      if (sel == FXMENU_PARAM4) return "DETUNE";
      if (sel == FXMENU_PARAM5) return "VOLUME";
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) return "DET1";
      if (sel == FXMENU_PARAM2) return "DET2";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "RATE";
      break;
    case FX_HARMONIZER:
      if (sel == FXMENU_PARAM1) return "CHORD";
      if (sel == FXMENU_PARAM2) return "VOLUME";
      if (sel == FXMENU_PARAM3) return "OCTAVE";
      break;
    case FX_GATE:
      if (sel == FXMENU_PARAM1) return "RATE";
      if (sel == FXMENU_PARAM2) return "PATTERN";
      if (sel == FXMENU_PARAM3) return "VOLUME";
      if (sel == FXMENU_PARAM4) return "DUTY";
      if (sel == FXMENU_PARAM5) return "SEED";
      break;
  }

  return "?";
}

// Get current FX value for a selection
int getFXValue(int sel) {
  if (sel == FXMENU_ENABLED) return displaySnapshotCopy.fxModeEnabled ? 1 : 0;
  if (sel == FXMENU_TYPE) return displaySnapshotCopy.fxType;
  if (sel == FXMENU_ROUTING) return displaySnapshotCopy.fxRouting;

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) {
        if (fxClockSync) return displaySnapshotCopy.echoDelayMs;  // Division index directly
        return displaySnapshotCopy.echoDelayMs / 20;  // 100-2000 as 5-100
      }
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.echoRepeats;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.echoDecay;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.echoVolume;
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.arpPattern;
      if (sel == FXMENU_PARAM2) {
        if (fxClockSync) return displaySnapshotCopy.arpSpeedMs;  // Division index directly
        return displaySnapshotCopy.arpSpeedMs / 5;  // 50-500 as 10-100
      }
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.arpVolume;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.arpOctave + 2;  // -2 to +2 as 0-4
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.bitCrushBits;
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.bitCrushRate;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.bitCrushVolume;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.bitCrushDuration / 10;  // 50-500 as 5-50
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.reverbTaps;
      if (sel == FXMENU_PARAM2) {
        if (fxClockSync) return displaySnapshotCopy.reverbSpacing;  // Division index directly
        return displaySnapshotCopy.reverbSpacing;
      }
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.reverbDecay;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.reverbDetune + 5;  // -5 to +5 as 0-10
      if (sel == FXMENU_PARAM5) return displaySnapshotCopy.reverbVolume;
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.chorusDetune1 + 50;  // -50 to +50 as 0-100
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.chorusDetune2 + 50;  // -50 to +50 as 0-100
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.chorusVolume;
      if (sel == FXMENU_PARAM4) {
        if (fxClockSync) return displaySnapshotCopy.chorusRate;  // Division index directly
        return displaySnapshotCopy.chorusRate;  // 0=static, 5-80
      }
      break;
    case FX_HARMONIZER:
      if (sel == FXMENU_PARAM1) return displaySnapshotCopy.harmChord;
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.harmVolume;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.harmOctave + 2;  // -2 to +2 as 0-4
      break;
    case FX_GATE:
      if (sel == FXMENU_PARAM1) {
        if (fxClockSync) return displaySnapshotCopy.gateRateMs;  // Division index directly
        return displaySnapshotCopy.gateRateMs / 5;  // 30-500 as 6-100
      }
      if (sel == FXMENU_PARAM2) return displaySnapshotCopy.gatePattern;
      if (sel == FXMENU_PARAM3) return displaySnapshotCopy.gateVolume;
      if (sel == FXMENU_PARAM4) return displaySnapshotCopy.gateDuty;
      if (sel == FXMENU_PARAM5) return displaySnapshotCopy.gateSeed;
      break;
  }
  return 0;
}

// Get max value for FX selection
int getFXMax(int sel) {
  if (sel == FXMENU_ENABLED) return 1;
  if (sel == FXMENU_TYPE) return FX_TYPE_COUNT - 1;
  if (sel == FXMENU_ROUTING) return FX_ROUTE_COUNT - 1;

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) return fxClockSync ? FX_CLOCK_DIV_COUNT - 1 : 100;
      if (sel == FXMENU_PARAM2) return 10;   // 1-10 repeats
      if (sel == FXMENU_PARAM3) return 15;   // 0-15 decay
      if (sel == FXMENU_PARAM4) return 15;   // 1-15 volume
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) return ARP_PATTERN_COUNT - 1;
      if (sel == FXMENU_PARAM2) return fxClockSync ? FX_CLOCK_DIV_COUNT - 1 : 100;
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return 4;    // -2 to +2 octave
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) return 4;    // 1-4 bits
      if (sel == FXMENU_PARAM2) return 10;   // 1-10 rate
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return 50;   // 5-50 (x10 = 50-500ms sustain)
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) return 12;   // 1-12 taps
      if (sel == FXMENU_PARAM2) return fxClockSync ? FX_CLOCK_DIV_COUNT - 1 : 250;
      if (sel == FXMENU_PARAM3) return 8;    // 1-8 decay
      if (sel == FXMENU_PARAM4) return 10;   // -5 to +5 detune
      if (sel == FXMENU_PARAM5) return 15;   // 1-15 volume
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) return 100;  // -50 to +50
      if (sel == FXMENU_PARAM2) return 100;  // -50 to +50
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return fxClockSync ? FX_CLOCK_DIV_COUNT - 1 : 80;
      break;
    case FX_HARMONIZER:
      if (sel == FXMENU_PARAM1) return HARM_CHORD_COUNT - 1;  // 0-11 chord types
      if (sel == FXMENU_PARAM2) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM3) return 4;    // -2 to +2 octave
      break;
    case FX_GATE:
      if (sel == FXMENU_PARAM1) return fxClockSync ? FX_CLOCK_DIV_COUNT - 1 : 100;
      if (sel == FXMENU_PARAM2) return GATE_PATTERN_COUNT - 1;
      if (sel == FXMENU_PARAM3) return 15;   // 1-15 volume
      if (sel == FXMENU_PARAM4) return 7;    // 1-7 duty
      if (sel == FXMENU_PARAM5) return 15;   // 0-15 seed
      break;
  }
  return 0;
}

// Get FX value as string
void getFXValueStr(int sel, int val, char* buf) {
  if (sel == FXMENU_ENABLED) {
    strcpy(buf, val ? "ON" : "OFF");
    return;
  }
  if (sel == FXMENU_TYPE) {
    strcpy(buf, getFxTypeName(val));
    return;
  }
  if (sel == FXMENU_ROUTING) {
    strcpy(buf, getFxRoutingName(val));
    return;
  }

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) {
        if (fxClockSync) {
          strcpy(buf, getClockDivName(val));
        } else {
          sprintf(buf, "%dms", val * 20);
        }
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sprintf(buf, "%d", val);
        return;
      }
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) {
        strcpy(buf, getArpPatternName(val));
        return;
      }
      if (sel == FXMENU_PARAM2) {
        if (fxClockSync) {
          strcpy(buf, getClockDivName(val));
        } else {
          sprintf(buf, "%dms", val * 5);
        }
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        int octVal = val - 2;
        sprintf(buf, "%+d", octVal);
        return;
      }
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sprintf(buf, "%dms", val * 10);
        return;
      }
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        if (fxClockSync) {
          strcpy(buf, getClockDivName(val));
        } else {
          sprintf(buf, "%dms", val);
        }
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        int detVal = val - 5;
        sprintf(buf, "%+d", detVal);
        return;
      }
      if (sel == FXMENU_PARAM5) {
        sprintf(buf, "%d", val);
        return;
      }
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) {
        int detVal = val - 50;
        sprintf(buf, "%+d", detVal);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        int detVal = val - 50;
        sprintf(buf, "%+d", detVal);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        if (fxClockSync) {
          strcpy(buf, getClockDivName(val));
        } else if (val == 0) {
          strcpy(buf, "OFF");
        } else {
          sprintf(buf, "%d.%d", val / 10, val % 10);
        }
        return;
      }
      break;
    case FX_HARMONIZER:
      if (sel == FXMENU_PARAM1) {
        strcpy(buf, getHarmChordName(val));
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%+d", val - 2);
        return;
      }
      break;
    case FX_GATE:
      if (sel == FXMENU_PARAM1) {
        if (fxClockSync) {
          strcpy(buf, getClockDivName(val));
        } else {
          sprintf(buf, "%d", val * 5);  // Show actual ms
        }
        return;
      }
      if (sel == FXMENU_PARAM2) {
        strcpy(buf, getGatePatternName(val));
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sprintf(buf, "%d", val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sprintf(buf, "%d/8", val);
        return;
      }
      if (sel == FXMENU_PARAM5) {
        sprintf(buf, "%d", val);
        return;
      }
      break;
  }
  strcpy(buf, "?");
}

// Apply FX value
void applyFXValue(int sel, int val) {
  if (sel == FXMENU_ENABLED) {
    sendCommand(CMD_SET_FX_ENABLED, val);
    return;
  }
  if (sel == FXMENU_TYPE) {
    sendCommand(CMD_SET_FX_TYPE, val);
    return;
  }
  if (sel == FXMENU_ROUTING) {
    sendCommand(CMD_SET_FX_ROUTING, val);
    return;
  }

  uint8_t type = displaySnapshotCopy.fxType;
  switch (type) {
    case FX_ECHO:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_ECHO_DELAY, val);  // 5-100, scaled x20 in handler
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_ECHO_REPEATS, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_ECHO_DECAY, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_ECHO_VOLUME, val);
        return;
      }
      break;
    case FX_ARP:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_ARP_PATTERN, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_ARP_SPEED, val);  // 10-100, scaled x5 in handler
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_ARP_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_ARP_OCTAVE, 0, 0, 0, (int8_t)(val - 2));
        return;
      }
      break;
    case FX_BIT_CRUSH:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_BIT_CRUSH_BITS, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_BIT_CRUSH_RATE, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_BIT_CRUSH_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_BIT_CRUSH_DURATION, val);  // val is 5-50, scaled x10 in handler
        return;
      }
      break;
    case FX_PSEUDO_REVERB:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_REVERB_TAPS, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_REVERB_SPACING, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_REVERB_DECAY, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_REVERB_DETUNE, 0, 0, 0, (int8_t)(val - 5));
        return;
      }
      if (sel == FXMENU_PARAM5) {
        sendCommand(CMD_SET_REVERB_VOLUME, val);
        return;
      }
      break;
    case FX_CHORUS:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_CHORUS_DETUNE1, 0, 0, 0, (int8_t)(val - 50));
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_CHORUS_DETUNE2, 0, 0, 0, (int8_t)(val - 50));
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_CHORUS_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_CHORUS_RATE, val);  // 0=static, 5-80 tenths of Hz
        return;
      }
      break;
    case FX_HARMONIZER:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_HARM_CHORD, val);
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_HARM_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_HARM_OCTAVE, 0, 0, 0, (int8_t)(val - 2));
        return;
      }
      break;
    case FX_GATE:
      if (sel == FXMENU_PARAM1) {
        sendCommand(CMD_SET_GATE_RATE, val);  // 6-100, scaled x5 in handler
        return;
      }
      if (sel == FXMENU_PARAM2) {
        sendCommand(CMD_SET_GATE_PATTERN, val);
        return;
      }
      if (sel == FXMENU_PARAM3) {
        sendCommand(CMD_SET_GATE_VOLUME, val);
        return;
      }
      if (sel == FXMENU_PARAM4) {
        sendCommand(CMD_SET_GATE_DUTY, val);
        return;
      }
      if (sel == FXMENU_PARAM5) {
        sendCommand(CMD_SET_GATE_SEED, val);
        return;
      }
      break;
  }
}

// Helper to draw an FX item (label + value), returns width drawn
static int drawFXItem(int x, int y, int sel, bool highlight, bool editing) {
  char valBuf[12];
  int val = editing ? fxTempValue : getFXValue(sel);
  getFXValueStr(sel, val, valBuf);
  const char* label = getFXLabel(sel);

  int labelWidth = strlen(label) * 4;
  int valWidth = strlen(valBuf) * 4;
  int totalWidth = labelWidth + 4 + valWidth + 4;  // "LABEL: VAL "

  if (highlight && !editing) {
    display.fillRect(x, y - 6, totalWidth, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }

  display.setCursor(x + 1, y);
  display.print(label);
  display.print(":");

  if (editing) {
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, valWidth + 2, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(valBuf);
  display.setTextColor(SH110X_WHITE);

  return totalWidth;
}

// ============================================================================
// POTS SUBMENU
// ============================================================================

void updatePotsMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use smaller font
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setCursor(0, 6);
  display.print(editingPotDefaults ? "POT DEFAULTS" : "POT ASSIGN");
  display.drawLine(0, 8, 127, 8, SH110X_WHITE);

  // Draw pot assignments (5 items: POT1-4 + BACK)
  int y = 17;
  const int rowH = 10;
  const int charW = 4;  // TomThumb character width

  for (int i = 0; i < POTS_ITEM_COUNT; i++) {
    bool isSelected = (i == potsSelection);
    bool isEditing = (isSelected && potsEditLevel != POT_EDIT_NONE);

    if (isSelected && !isEditing) {
      display.fillRect(0, y - 7, 127, rowH, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(1, y);

    if (i == POTS_BACK) {
      display.print("< BACK");
    } else {
      // Show pot number
      display.print(i + 1);
      display.print(":");

      // Get category, param, target - either from temp (editing) or source
      uint8_t cat, param, target;
      if (isEditing) {
        cat = potsTempCategory;
        param = potsTempParam;
        target = potsTempTarget;
      } else if (editingPotDefaults) {
        cat = potDefaultAssignments[i].category;
        param = potDefaultAssignments[i].paramIndex;
        target = potDefaultAssignments[i].target;
      } else {
        cat = displaySnapshotCopy.potAssignments[i].category;
        param = displaySnapshotCopy.potAssignments[i].paramIndex;
        target = displaySnapshotCopy.potAssignments[i].target;
      }

      // Category name (highlight if editing category)
      const char* catName = getPotCategoryName((PotCategory)cat);
      int catX = display.getCursorX();
      if (isEditing && potsEditLevel == POT_EDIT_CATEGORY) {
        display.fillRect(catX - 1, y - 7, strlen(catName) * charW + 2, rowH, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.print(catName);
      display.setTextColor(isSelected && !isEditing ? SH110X_BLACK : SH110X_WHITE);

      if (cat != PCAT_OFF) {
        display.print(" ");

        // Param name (highlight if editing param)
        const char* paramName = getPotParamName((PotCategory)cat, param);
        int paramX = display.getCursorX();
        if (isEditing && potsEditLevel == POT_EDIT_PARAM) {
          display.fillRect(paramX - 1, y - 7, strlen(paramName) * charW + 2, rowH, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        }
        display.print(paramName);
        display.setTextColor(isSelected && !isEditing ? SH110X_BLACK : SH110X_WHITE);

        // Target (only for categories that need it)
        if (categoryRequiresTarget((PotCategory)cat)) {
          display.print(" ");
          const char* targName = (target == TARGET_NONE) ? "---" : getTargetName((SettingsTarget)target);
          int targX = display.getCursorX();
          if (isEditing && potsEditLevel == POT_EDIT_TARGET) {
            display.fillRect(targX - 1, y - 7, strlen(targName) * charW + 2, rowH, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
          }
          display.print(targName);
        } else {
          // Show "---" for FX/SAMPLE/GLOBAL categories (no targeting)
          display.print(" ---");
        }
      }
    }

    display.setTextColor(SH110X_WHITE);
    y += rowH;
  }

  // Footer
  display.drawLine(0, 54, 127, 54, SH110X_WHITE);
  display.setCursor(1, 62);
  if (potsEditLevel == POT_EDIT_NONE) {
    display.print("Turn=sel Push=edit");
  } else if (potsEditLevel == POT_EDIT_CATEGORY) {
    display.print("Turn=cat  Push=next");
  } else if (potsEditLevel == POT_EDIT_PARAM) {
    display.print("Turn=param Push=next");
  } else if (potsEditLevel == POT_EDIT_TARGET) {
    display.print("Turn=targ Push=done");
  }
}

// ============================================================================
// FX SUBMENU RENDERING
// ============================================================================

void updateFXSubmenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setCursor(0, 6);
  display.print("FX CHIP [2]");
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);

  // Show warning if SID mode is active (FX uses chip 2, conflicts with SID)
  if (sidModeGlobal) {
    display.setCursor(0, 30);
    display.print("FX DISABLED");
    display.setCursor(0, 40);
    display.print("(SID mode active)");
    display.setCursor(0, 56);
    display.print("Push=BACK");
    display.setFont(NULL);
    return;
  }

  uint8_t type = displaySnapshotCopy.fxType;
  int y = 16;
  const int rowHeight = 7;

  // Row 1: ENABLED + TYPE (side by side)
  {
    bool sel0 = (fxSelection == FXMENU_ENABLED);
    bool edit0 = sel0 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_ENABLED, sel0, edit0);

    bool sel1 = (fxSelection == FXMENU_TYPE);
    bool edit1 = sel1 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_TYPE, sel1, edit1);
    y += rowHeight;
  }

  // Row 2: ROUTING (full width)
  {
    bool sel = (fxSelection == FXMENU_ROUTING);
    bool edit = sel && fxEditing;
    drawFXItem(0, y, FXMENU_ROUTING, sel, edit);
    y += rowHeight;
  }

  // Row 3: Parameters (type-dependent, paired when applicable)
  if (type == FX_ECHO) {
    // DELAY + REPS
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // DECAY + VOL
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_ARP) {
    // PATTERN + SPEED
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + OCTAVE
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_BIT_CRUSH) {
    // BITS + RATE
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + SUSTAIN
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_PSEUDO_REVERB) {
    // TAPS + SPACE
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // DECAY + DETUNE
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;

    // VOLUME
    bool sel7 = (fxSelection == FXMENU_PARAM5);
    bool edit7 = sel7 && fxEditing;
    drawFXItem(0, y, FXMENU_PARAM5, sel7, edit7);
    y += rowHeight;
  }
  else if (type == FX_CHORUS) {
    // DET1 + DET2
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + RATE
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;
  }
  else if (type == FX_HARMONIZER) {
    // CHORD + VOLUME
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // OCTAVE
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);
    y += rowHeight;
  }
  else if (type == FX_GATE) {
    // RATE + PATTERN
    bool sel3 = (fxSelection == FXMENU_PARAM1);
    bool edit3 = sel3 && fxEditing;
    int w = drawFXItem(0, y, FXMENU_PARAM1, sel3, edit3);

    bool sel4 = (fxSelection == FXMENU_PARAM2);
    bool edit4 = sel4 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM2, sel4, edit4);
    y += rowHeight;

    // VOLUME + DUTY
    bool sel5 = (fxSelection == FXMENU_PARAM3);
    bool edit5 = sel5 && fxEditing;
    w = drawFXItem(0, y, FXMENU_PARAM3, sel5, edit5);

    bool sel6 = (fxSelection == FXMENU_PARAM4);
    bool edit6 = sel6 && fxEditing;
    drawFXItem(w + 4, y, FXMENU_PARAM4, sel6, edit6);
    y += rowHeight;

    // SEED
    bool sel7 = (fxSelection == FXMENU_PARAM5);
    bool edit7 = sel7 && fxEditing;
    drawFXItem(0, y, FXMENU_PARAM5, sel7, edit7);
    y += rowHeight;
  }

  // BACK row
  int backItem = getVisibleFXItemCount() - 1;
  bool selBack = (fxSelection == backItem);
  if (selBack && !fxEditing) {
    display.fillRect(0, y - 6, 40, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(2, y);
  display.print("< BACK");
  display.setTextColor(SH110X_WHITE);

  // Footer
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (fxEditing) {
    display.print("Turn=adj  Push=save");
  } else {
    display.print("Push=edit  BACK=exit");
  }

  display.setFont(NULL);
}

// ============================================================================
// PRESETS SUBMENU
// ============================================================================

// Character set for name entry: A-Z, 0-9, space
const char nameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
const int nameCharCount = 37;

// Get character index in nameChars
int getNameCharIndex(char c) {
  for (int i = 0; i < nameCharCount; i++) {
    if (nameChars[i] == c) return i;
  }
  return 36;  // Default to space
}

void updatePresetsMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);  // Use tiny font for presets menu
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);


  // Show "Deleting..." overlay while delete is in progress
  if (presetDeleting) {
    display.setCursor(36, 32);
    display.print("Deleting...");
    // Clear flag after timeout (delete should be complete)
    if (millis() - presetDeleteStartTime >= PRESET_SAVE_DISPLAY_MS) {
      presetDeleting = false;
    }
    display.setFont(NULL);
    return;
  }

  switch (presetMenuLevel) {
    case PRESET_LEVEL_MENU: {
      // Main preset menu: LOAD / SAVE / DELETE / BACK
      // TomThumb uses baseline positioning, add 5 for height
      display.setCursor(0, 6);
      display.print(sidModeGlobal ? "SID TIMBRES" : "PRESETS");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show current preset if any
      display.setCursor(0, 16);
      display.print("Active: ");
      if (sidModeGlobal) {
        // SID preset display
        if (currentSidPreset == 0xFF) {
          display.print("CUSTOM");
        } else if (currentSidPreset < SID_PRESET_FACTORY_COUNT) {
          display.print("F");
          display.print(currentSidPreset + 1);
        } else {
          display.print("U");
          display.print(currentSidPreset - SID_PRESET_FACTORY_COUNT + 1);
        }
      } else {
        // Regular preset display — show name
        if (currentPresetIndex == PRESET_INDEX_NONE) {
          display.print("---");
        } else {
          char nameBuf[PRESET_NAME_LEN + 1];
          presetGetName(currentPresetIndex, nameBuf);
          display.print(nameBuf);
        }
      }

      // Menu items (7px spacing for TomThumb)
      const char* items[] = {"LOAD", "SAVE", "DELETE", "< BACK"};
      int y = 26;
      for (int i = 0; i < PRESETMENU_ITEM_COUNT; i++) {
        bool selected = (presetMenuSelection == i);
        if (selected) {
          display.fillRect(0, y - 6, 128, 8, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else {
          display.setTextColor(SH110X_WHITE);
        }
        display.setCursor(4, y);
        display.print(items[i]);
        display.setTextColor(SH110X_WHITE);
        y += 8;
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Turn=sel  Push=enter");
      break;
    }

    case PRESET_LEVEL_LOAD: {
      // Show list of presets to load (factory + user)
      display.setCursor(0, 6);
      display.print(sidModeGlobal ? "LOAD TIMBRE" : "LOAD PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int totalPresets = sidModeGlobal ? SID_PRESET_TOTAL : getLoadListCount();

      // Show presets (7 at a time with tiny font)
      int y = 16;
      for (int row = 0; row < 7; row++) {
        int listPos = presetScrollIndex + row;
        if (listPos >= totalPresets) break;

        bool selected = (row == 0);  // First visible is selected
        if (selected) {
          display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else {
          display.setTextColor(SH110X_WHITE);
        }

        display.setCursor(2, y);
        if (sidModeGlobal) {
          // SID preset display - use cached names
          if (listPos < SID_PRESET_FACTORY_COUNT) {
            display.print("F");
            display.print(listPos + 1);
          } else {
            display.print("U");
            display.print(listPos - SID_PRESET_FACTORY_COUNT + 1);
          }
          display.print(": ");
          if (sidPresetCacheValid) {
            display.print(sidPresetNames[listPos]);
          }
        } else {
          // Regular preset display
          uint8_t presetIdx = getLoadListPresetIndex(listPos);
          if (presetIdx == PRESET_INDEX_NONE) break;

          if (PRESET_IS_FACTORY(presetIdx)) {
            // Factory preset: F01-F08
            display.print("F0");
            display.print(presetIdx + 1);
            display.print(": ");
            display.print(factoryPresetNames[presetIdx]);
          } else {
            // User preset: U001-U120
            int userSlot = PRESET_TO_USER_SLOT(presetIdx);
            display.print("U");
            if (userSlot < 9) display.print("00");
            else if (userSlot < 99) display.print("0");
            display.print(userSlot + 1);
            display.print(": ");
            char nameBuf[9];
            presetGetName(presetIdx, nameBuf);
            display.print(nameBuf);
          }
        }
        display.setTextColor(SH110X_WHITE);
        y += 7;
      }

      // Scroll indicator
      if (totalPresets > 7) {
        int scrollH = 48;
        int scrollY = 10 + (scrollH * presetScrollIndex / totalPresets);
        display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
        display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Turn=scrl Push=load Hold=back");
      break;
    }

    case PRESET_LEVEL_SAVE: {
      // Show list of user slots to save to
      display.setCursor(0, 6);
      display.print(sidModeGlobal ? "SAVE TIMBRE" : "SAVE TO SLOT");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int maxSlots = sidModeGlobal ? SID_PRESET_USER_COUNT : PRESET_USER_SLOTS;

      // Show slots starting from scroll index (7 at a time with tiny font)
      int y = 16;
      for (int row = 0; row < 7; row++) {
        int slot = presetScrollIndex + row;
        if (slot >= maxSlots) break;

        bool selected = (slot == presetScrollIndex);
        if (selected) {
          display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
          presetSelectedSlot = slot;
        } else {
          display.setTextColor(SH110X_WHITE);
        }

        display.setCursor(2, y);
        display.print("U");
        display.print(slot + 1);
        display.print(": ");

        if (sidModeGlobal) {
          // Use cached data instead of reading flash every frame
          if (sidPresetCacheValid && sidPresetUsed[slot]) {
            display.print(sidPresetNames[SID_PRESET_FACTORY_COUNT + slot]);
          } else {
            display.print("--------");
          }
        } else {
          if (slot < 9) { display.setCursor(2, y); display.print("U00"); display.print(slot + 1); display.print(": "); }
          else if (slot < 99) { display.setCursor(2, y); display.print("U0"); display.print(slot + 1); display.print(": "); }
          if (presetUserIsUsed(slot)) {
            char nameBuf[9];
            presetGetName(USER_SLOT_TO_PRESET(slot), nameBuf);
            display.print(nameBuf);
          } else {
            display.print("--------");
          }
        }
        display.setTextColor(SH110X_WHITE);
        y += 7;
      }

      // Scroll indicator
      if (maxSlots > 7) {
        int scrollH = 48;
        int scrollY = 10 + (scrollH * presetScrollIndex / maxSlots);
        display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
        display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Push=sel Hold=back");
      break;
    }

    case PRESET_LEVEL_NAME: {
      // Name entry screen
      display.setCursor(0, 6);
      display.print("ENTER NAME");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show slot being saved to
      display.setCursor(0, 18);
      display.print("Slot: U");
      if (presetSelectedSlot < 9) display.print("00");
      else if (presetSelectedSlot < 99) display.print("0");
      display.print(presetSelectedSlot + 1);

      // Show name with cursor (larger chars for editing)
      display.setFont(NULL);  // Use default font for name characters
      display.setCursor(0, 28);
      display.print("Name:");

      // Draw each character
      int nameX = 36;
      for (int i = 0; i < 8; i++) {
        bool isCursor = (i == presetNameCursor);
        if (isCursor && presetNameEditing) {
          display.fillRect(nameX - 1, 26, 8, 11, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else if (isCursor) {
          display.drawRect(nameX - 1, 26, 8, 11, SH110X_WHITE);
        }
        display.setCursor(nameX, 28);
        display.print(presetNameBuffer[i]);
        display.setTextColor(SH110X_WHITE);
        nameX += 7;
      }

      // SAVE and EXIT centered on row below name
      int btnY = 40;
      int saveX = 34;
      int exitX = 68;
      bool saveCursor = (presetNameCursor == 8);
      if (saveCursor) {
        display.fillRect(saveX - 1, btnY - 2, 26, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.setCursor(saveX, btnY);
      display.print("SAVE");
      display.setTextColor(SH110X_WHITE);

      bool exitCursor = (presetNameCursor == 9);
      if (exitCursor) {
        display.fillRect(exitX - 1, btnY - 2, 26, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.setCursor(exitX, btnY);
      display.print("EXIT");
      display.setTextColor(SH110X_WHITE);

      if (presetSaving) {
        display.setCursor(40, 56);
        display.print("Saved!");
      }

      display.setFont(&TomThumb);
      if (!presetSaving) {
        if (presetNameEditing) {
          display.setCursor(0, 56);
          display.print("Turn=char  Push=next");
          display.setCursor(2, 63);
          display.print("Hold=stop editing");
        } else {
          display.setCursor(0, 56);
          if (presetNameCursor == 8) {
            display.print("Push = save preset");
          } else if (presetNameCursor == 9) {
            display.print("Push = exit (no save)");
          } else {
            display.print("Push=edit  Turn=move");
          }
          display.setCursor(2, 63);
          display.print("Slot U");
          if (presetSelectedSlot < 9) display.print("00");
          else if (presetSelectedSlot < 99) display.print("0");
          display.print(presetSelectedSlot + 1);
        }
      }
      break;
    }

    case PRESET_LEVEL_DELETE: {
      display.setCursor(0, 6);
      display.print(sidModeGlobal ? "DELETE TIMBRE" : "DELETE PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int deleteCount;
      if (sidModeGlobal) {
        // Count used SID user presets
        deleteCount = 0;
        for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
          if (sidPresetCacheValid ? sidPresetUsed[i] : sidPresetUserIsUsed(i))
            deleteCount++;
        }
      } else {
        deleteCount = getDeleteListCount();
      }

      if (deleteCount == 0) {
        // No user presets to delete
        display.setCursor(10, 30);
        display.print(sidModeGlobal ? "No user timbres" : "No user presets");
        display.setCursor(10, 40);
        display.print("(save first)");

        // Footer
        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=back");
      } else {
        // Show user presets (7 at a time with tiny font)
        int y = 16;
        for (int row = 0; row < 7; row++) {
          int listPos = presetScrollIndex + row;
          if (listPos >= deleteCount) break;

          if (sidModeGlobal) {
            // Find the Nth used SID user slot
            int found = 0;
            uint8_t userSlot = 0xFF;
            for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
              bool used = sidPresetCacheValid ? sidPresetUsed[i] : sidPresetUserIsUsed(i);
              if (used) {
                if (found == listPos) { userSlot = i; break; }
                found++;
              }
            }
            if (userSlot == 0xFF) break;

            bool selected = (row == 0);
            if (selected) {
              display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
              display.setTextColor(SH110X_BLACK);
              presetSelectedSlot = userSlot;
            } else {
              display.setTextColor(SH110X_WHITE);
            }

            display.setCursor(2, y);
            display.print("U");
            display.print(userSlot + 1);
            display.print(": ");
            if (sidPresetCacheValid) {
              display.print(sidPresetNames[SID_PRESET_FACTORY_COUNT + userSlot]);
            } else {
              char nameBuf[9];
              sidPresetGetName(SID_PRESET_FACTORY_COUNT + userSlot, nameBuf);
              display.print(nameBuf);
            }
          } else {
            uint8_t userSlot = getDeleteListUserSlot(listPos);
            if (userSlot == 0xFF) break;

            bool selected = (row == 0);
            if (selected) {
              display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
              display.setTextColor(SH110X_BLACK);
              presetSelectedSlot = userSlot;
            } else {
              display.setTextColor(SH110X_WHITE);
            }

            display.setCursor(2, y);
            display.print("U");
            if (userSlot < 9) display.print("00");
            else if (userSlot < 99) display.print("0");
            display.print(userSlot + 1);
            display.print(": ");
            char nameBuf[9];
            presetGetName(USER_SLOT_TO_PRESET(userSlot), nameBuf);
            display.print(nameBuf);
          }
          display.setTextColor(SH110X_WHITE);
          y += 7;
        }

        // Scroll indicator
        if (deleteCount > 7) {
          int scrollH = 48;
          int scrollY = 10 + (scrollH * presetScrollIndex / deleteCount);
          display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
          display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
        }

        // Footer
        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=sel  Hold=back");
      }
      break;
    }

    case PRESET_LEVEL_CONFIRM_DEL: {
      display.setCursor(0, 6);
      display.print("CONFIRM DELETE");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show preset name being deleted
      display.setCursor(10, 22);
      display.print("Delete ");
      if (sidModeGlobal) {
        if (sidPresetCacheValid) {
          display.print(sidPresetNames[SID_PRESET_FACTORY_COUNT + presetSelectedSlot]);
        }
      } else {
        char nameBuf[9];
        presetGetName(USER_SLOT_TO_PRESET(presetSelectedSlot), nameBuf);
        display.print(nameBuf);
      }
      display.print("?");

      // YES / NO buttons
      int y = 38;
      if (!presetConfirmYes) {
        // NO selected
        display.fillRect(20, y - 6, 20, 8, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(22, y);
        display.print("NO");
        display.setTextColor(SH110X_WHITE);
        display.setCursor(62, y);
        display.print("YES");
      } else {
        // YES selected
        display.setCursor(22, y);
        display.print("NO");
        display.fillRect(58, y - 6, 24, 8, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(62, y);
        display.print("YES");
        display.setTextColor(SH110X_WHITE);
      }

      // Footer
      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Push=confirm");
      break;
    }
  }

  display.setFont(NULL);  // Reset to default font
}

// ============================================================================
// SMPL PRESET MENU (submenu within SMPL settings)
// ============================================================================

void updateSmplPresetsMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  switch (smplPresetMenuLevel) {
    case PRESET_LEVEL_MENU: {
      display.setCursor(0, 6);
      display.print("SMPL PRESETS");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Show current preset
      display.setCursor(0, 16);
      display.print("Active: ");
      if (currentSmplPreset == 0xFF) {
        display.print("CUSTOM");
      } else {
        display.print("U");
        if (currentSmplPreset < 9) display.print("0");
        display.print(currentSmplPreset + 1);
      }

      const char* items[] = {"LOAD", "SAVE", "DELETE", "< BACK"};
      int y = 26;
      for (int i = 0; i < PRESETMENU_ITEM_COUNT; i++) {
        bool selected = (smplPresetSelection == i);
        if (selected) {
          display.fillRect(0, y - 6, 128, 8, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else {
          display.setTextColor(SH110X_WHITE);
        }
        display.setCursor(4, y);
        display.print(items[i]);
        display.setTextColor(SH110X_WHITE);
        y += 8;
      }

      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Turn=sel  Push=enter");
      break;
    }

    case PRESET_LEVEL_LOAD: {
      display.setCursor(0, 6);
      display.print("LOAD SMPL PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      // Count used presets
      int totalPresets = 0;
      for (uint8_t i = 0; i < SMPL_PRESET_USER_COUNT; i++) {
        if (smplPresetCacheValid ? smplPresetUsed[i] : smplPresetUserIsUsed(i))
          totalPresets++;
      }

      if (totalPresets == 0) {
        display.setCursor(10, 30);
        display.print("No saved presets");
        display.setCursor(10, 40);
        display.print("(save first)");
        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=back");
      } else {
        int y = 16;
        for (int row = 0; row < 7; row++) {
          int listPos = smplPresetScrollIndex + row;
          if (listPos >= totalPresets) break;

          // Find the Nth used slot
          int found = 0;
          uint8_t userSlot = 0xFF;
          for (uint8_t i = 0; i < SMPL_PRESET_USER_COUNT; i++) {
            bool used = smplPresetCacheValid ? smplPresetUsed[i] : smplPresetUserIsUsed(i);
            if (used) {
              if (found == listPos) { userSlot = i; break; }
              found++;
            }
          }
          if (userSlot == 0xFF) break;

          bool selected = (row == 0);
          if (selected) {
            display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
            smplPresetSelectedSlot = userSlot;
          } else {
            display.setTextColor(SH110X_WHITE);
          }

          display.setCursor(2, y);
          display.print("U");
          if (userSlot < 9) display.print("0");
          display.print(userSlot + 1);
          display.print(": ");
          if (smplPresetCacheValid) {
            display.print(smplPresetNames[userSlot]);
          }
          display.setTextColor(SH110X_WHITE);
          y += 7;
        }

        if (totalPresets > 7) {
          int scrollH = 48;
          int scrollY = 10 + (scrollH * smplPresetScrollIndex / totalPresets);
          display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
          display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
        }

        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Turn=scrl Push=load Hold=back");
      }
      break;
    }

    case PRESET_LEVEL_SAVE: {
      display.setCursor(0, 6);
      display.print("SAVE SMPL PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int y = 16;
      for (int row = 0; row < 7; row++) {
        int slot = smplPresetScrollIndex + row;
        if (slot >= SMPL_PRESET_USER_COUNT) break;

        bool selected = (slot == smplPresetScrollIndex);
        if (selected) {
          display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
          smplPresetSelectedSlot = slot;
        } else {
          display.setTextColor(SH110X_WHITE);
        }

        display.setCursor(2, y);
        display.print("U");
        if (slot < 9) display.print("0");
        display.print(slot + 1);
        display.print(": ");

        if (smplPresetCacheValid && smplPresetUsed[slot]) {
          display.print(smplPresetNames[slot]);
        } else {
          display.print("--------");
        }
        display.setTextColor(SH110X_WHITE);
        y += 7;
      }

      if (SMPL_PRESET_USER_COUNT > 7) {
        int scrollH = 48;
        int scrollY = 10 + (scrollH * smplPresetScrollIndex / SMPL_PRESET_USER_COUNT);
        display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
        display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
      }

      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Push=sel Hold=back");
      break;
    }

    case PRESET_LEVEL_NAME: {
      display.setCursor(0, 6);
      display.print("ENTER NAME");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      display.setCursor(0, 18);
      display.print("Slot: U");
      if (smplPresetSelectedSlot < 9) display.print("0");
      display.print(smplPresetSelectedSlot + 1);

      display.setFont(NULL);
      display.setCursor(0, 28);
      display.print("Name:");

      int nameX = 36;
      for (int i = 0; i < 8; i++) {
        bool isCursor = (i == presetNameCursor);
        if (isCursor && presetNameEditing) {
          display.fillRect(nameX - 1, 26, 8, 11, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else if (isCursor) {
          display.drawRect(nameX - 1, 26, 8, 11, SH110X_WHITE);
        }
        display.setCursor(nameX, 28);
        display.print(presetNameBuffer[i]);
        display.setTextColor(SH110X_WHITE);
        nameX += 7;
      }

      // SAVE and EXIT centered on row below name
      int btnY = 40;
      int saveX = 34;
      int exitX = 68;
      bool saveCursor = (presetNameCursor == 8);
      if (saveCursor) {
        display.fillRect(saveX - 1, btnY - 2, 26, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.setCursor(saveX, btnY);
      display.print("SAVE");
      display.setTextColor(SH110X_WHITE);

      bool exitCursor = (presetNameCursor == 9);
      if (exitCursor) {
        display.fillRect(exitX - 1, btnY - 2, 26, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      display.setCursor(exitX, btnY);
      display.print("EXIT");
      display.setTextColor(SH110X_WHITE);

      if (smplPresetSaving) {
        display.setCursor(40, 56);
        display.print("Saved!");
      }

      display.setFont(&TomThumb);
      if (!smplPresetSaving) {
        if (presetNameEditing) {
          display.setCursor(0, 56);
          display.print("Turn=char  Push=next");
          display.setCursor(2, 63);
          display.print("Hold=stop editing");
        } else {
          display.setCursor(0, 56);
          if (presetNameCursor == 8) {
            display.print("Push = save preset");
          } else if (presetNameCursor == 9) {
            display.print("Push = exit (no save)");
          } else {
            display.print("Push=edit  Turn=move");
          }
          display.setCursor(2, 63);
          display.print("Slot U");
          if (smplPresetSelectedSlot < 9) display.print("0");
          display.print(smplPresetSelectedSlot + 1);
        }
      }
      break;
    }

    case PRESET_LEVEL_DELETE: {
      display.setCursor(0, 6);
      display.print("DELETE SMPL PRESET");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      int deleteCount = 0;
      for (uint8_t i = 0; i < SMPL_PRESET_USER_COUNT; i++) {
        if (smplPresetCacheValid ? smplPresetUsed[i] : smplPresetUserIsUsed(i))
          deleteCount++;
      }

      if (deleteCount == 0) {
        display.setCursor(10, 30);
        display.print("No saved presets");
        display.setCursor(10, 40);
        display.print("(save first)");
        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=back");
      } else {
        int y = 16;
        for (int row = 0; row < 7; row++) {
          int listPos = smplPresetScrollIndex + row;
          if (listPos >= deleteCount) break;

          int found = 0;
          uint8_t userSlot = 0xFF;
          for (uint8_t i = 0; i < SMPL_PRESET_USER_COUNT; i++) {
            bool used = smplPresetCacheValid ? smplPresetUsed[i] : smplPresetUserIsUsed(i);
            if (used) {
              if (found == listPos) { userSlot = i; break; }
              found++;
            }
          }
          if (userSlot == 0xFF) break;

          bool selected = (row == 0);
          if (selected) {
            display.fillRect(0, y - 6, 122, 7, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
            smplPresetSelectedSlot = userSlot;
          } else {
            display.setTextColor(SH110X_WHITE);
          }

          display.setCursor(2, y);
          display.print("U");
          if (userSlot < 9) display.print("0");
          display.print(userSlot + 1);
          display.print(": ");
          if (smplPresetCacheValid) {
            display.print(smplPresetNames[userSlot]);
          }
          display.setTextColor(SH110X_WHITE);
          y += 7;
        }

        if (deleteCount > 7) {
          int scrollH = 48;
          int scrollY = 10 + (scrollH * smplPresetScrollIndex / deleteCount);
          display.drawFastVLine(126, 10, scrollH, SH110X_WHITE);
          display.fillRect(125, scrollY, 3, 4, SH110X_WHITE);
        }

        display.drawLine(0, 55, 127, 55, SH110X_WHITE);
        display.setCursor(2, 62);
        display.print("Push=sel  Hold=back");
      }
      break;
    }

    case PRESET_LEVEL_CONFIRM_DEL: {
      display.setCursor(0, 6);
      display.print("CONFIRM DELETE");
      display.drawLine(0, 8, 127, 8, SH110X_WHITE);

      display.setCursor(10, 22);
      display.print("Delete ");
      if (smplPresetCacheValid) {
        display.print(smplPresetNames[smplPresetSelectedSlot]);
      }
      display.print("?");

      int y = 38;
      if (!presetConfirmYes) {
        display.fillRect(20, y - 6, 20, 8, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(22, y);
        display.print("NO");
        display.setTextColor(SH110X_WHITE);
        display.setCursor(62, y);
        display.print("YES");
      } else {
        display.setCursor(22, y);
        display.print("NO");
        display.fillRect(58, y - 6, 24, 8, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(62, y);
        display.print("YES");
        display.setTextColor(SH110X_WHITE);
      }

      display.drawLine(0, 55, 127, 55, SH110X_WHITE);
      display.setCursor(2, 62);
      display.print("Push=confirm");
      break;
    }
  }

  display.setFont(NULL);
}

// ============================================================================
// MIDI SETTINGS MENU
// ============================================================================

// Helper to get MIDI channel display string
static const char* getMidiChannelName(uint8_t ch) {
  static char buf[5];
  if (ch == MIDI_CHANNEL_OFF) {
    return "OFF";
  } else if (ch == MIDI_CHANNEL_OMNI) {
    return "OMNI";
  } else {
    // Display as 1-16 (ch is 0-15 internally)
    snprintf(buf, sizeof(buf), "%d", ch + 1);
    return buf;
  }
}

// Helper to get route display string (shows first active route or "---")
static void getRouteSummary(char* buf) {
  // Find first active route
  for (uint8_t i = 0; i < 16; i++) {
    if (midiChannelRemap[i] != MIDI_REMAP_NONE) {
      // Show first active route: "1>4" means ch1 routes to ch4
      snprintf(buf, 8, "%d>%d", i + 1, midiChannelRemap[i] + 1);
      return;
    }
  }
  strcpy(buf, "---");
}

void updateMidiMenu() {
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setCursor(0, 6);
  display.print("SETTINGS");
  display.drawLine(0, 8, 127, 8, SH110X_WHITE);

  const int itemH = 8;  // Compact to fit 5 rows + footer
  int y = 17;

  // Get display values
  uint8_t mchVal = midiEditing && midiMenuSelection == MIDIMENU_MCH ? midiTempValue : midiSynthChannel;
  uint8_t vizVal = midiEditing && midiMenuSelection == MIDIMENU_VIZ ? midiTempValue : vizMode;
  uint8_t usbVal = midiEditing && midiMenuSelection == MIDIMENU_USB ? midiTempValue : usbMode;
  uint8_t modeVal = midiEditing && midiMenuSelection == MIDIMENU_MODE ? midiTempValue
                  : (sampleModeGlobal ? 2 : (sidModeGlobal ? 1 : 0));
  const char* modeName = modeVal == 2 ? "SMPL" : modeVal == 1 ? "SID" : "YM";

  // === Row 1: M.CH and MODE on same line ===
  bool mchSel = (midiMenuSelection == MIDIMENU_MCH);
  bool modeSel = (midiMenuSelection == MIDIMENU_MODE);

  // M.CH (left side)
  if (mchSel && !midiEditing) {
    display.fillRect(0, y - 6, 62, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("M.CH:");
  if (midiEditing && mchSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 24, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(getMidiChannelName(mchVal));
  display.setTextColor(SH110X_WHITE);

  // MODE (right side)
  int modeX = 66;
  if (modeSel && !midiEditing) {
    display.fillRect(modeX - 2, y - 6, 62, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(modeX, y);
  display.print("MODE:");
  if (midiEditing && modeSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 24, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(modeName);
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // === Row 2: ROUTE ===
  bool routeSel = (midiMenuSelection == MIDIMENU_ROUTE);
  bool routeEditing = routeSel && (routeEditLevel != ROUTE_EDIT_NONE);

  if (routeSel && !routeEditing) {
    display.fillRect(0, y - 6, 127, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("ROUTE:");

  if (routeEditing) {
    // Show FROM:x TO:y format when editing
    display.setTextColor(SH110X_WHITE);

    // FROM value
    int fromX = display.getCursorX();
    if (routeEditLevel == ROUTE_EDIT_FROM) {
      display.fillRect(fromX, y - 6, 16, itemH, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    display.print(routeFromChannel + 1);
    display.setTextColor(SH110X_WHITE);

    display.print(">");

    // TO value
    int toX = display.getCursorX();
    if (routeEditLevel == ROUTE_EDIT_TO) {
      display.fillRect(toX, y - 6, 16, itemH, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    if (routeTempTo == MIDI_REMAP_NONE) {
      display.print("=");  // "=" means no remap (pass through)
    } else {
      display.print(routeTempTo + 1);
    }
  } else {
    // Show summary when not editing
    char routeBuf[8];
    getRouteSummary(routeBuf);
    display.print(routeBuf);
  }
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // === Row 3: VIZ (full width) ===
  bool vizSel = (midiMenuSelection == MIDIMENU_VIZ);

  if (vizSel && !midiEditing) {
    display.fillRect(0, y - 6, 127, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("VIZ:");
  if (midiEditing && vizSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 30, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(vizVal == VIZ_MODE_BARS ? "BARS" :
                vizVal == VIZ_MODE_SCOPE ? "SCOPE" :
                vizVal == VIZ_MODE_MATRIX ? "MATRIX" :
                vizVal == VIZ_MODE_CHMATRIX ? "CHMATX" :
                vizVal == VIZ_MODE_SAMPLE ? "SMPL" : "STATC");
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // === Row 4: USB, BRT, VEL on same line ===
  bool usbSel = (midiMenuSelection == MIDIMENU_USB);
  bool brtSel = (midiMenuSelection == MIDIMENU_BRT);
  bool velSel = (midiMenuSelection == MIDIMENU_VEL);
  uint8_t brtScale = midiEditing && brtSel ? midiTempValue : (displayBrightness + 12) / 25;  // 0-255 to 0-10
  if (brtScale > 10) brtScale = 10;
  // Find current velocity curve index from gamma
  uint8_t velIdx = 5;
  if (midiEditing && velSel) {
    velIdx = midiTempValue;
  } else {
    float minD = 99.0f;
    for (uint8_t i = 0; i <= 10; i++) {
      float d = velocityGamma - velocityCurveTable[i];
      if (d < 0) d = -d;
      if (d < minD) { minD = d; velIdx = i; }
    }
  }

  // USB (left)
  if (usbSel && !midiEditing) {
    display.fillRect(0, y - 6, 36, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("USB:");
  if (midiEditing && usbSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 20, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(usbVal == USB_MODE_MIDI ? "MIDI" : "SER");
  display.setTextColor(SH110X_WHITE);

  // BRT
  int brtX = 40;
  if (brtSel && !midiEditing) {
    display.fillRect(brtX - 2, y - 6, 28, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(brtX, y);
  display.print("BRT:");
  if (midiEditing && brtSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 14, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(brtScale);
  display.setTextColor(SH110X_WHITE);

  // VEL
  int velX = 74;
  if (velSel && !midiEditing) {
    display.fillRect(velX - 2, y - 6, 28, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(velX, y);
  display.print("VEL:");
  if (midiEditing && velSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 14, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(velIdx);
  display.setTextColor(SH110X_WHITE);

  y += itemH;

  // === Row 5: POTS, CLK, BACK on same line ===
  bool potsSel = (midiMenuSelection == MIDIMENU_POTS);
  bool clkSel = (midiMenuSelection == MIDIMENU_CLK);
  bool clkVal = midiEditing && clkSel ? (midiTempValue != 0) : fxClockSync;

  // POTS
  if (potsSel) {
    display.fillRect(0, y - 6, 24, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(2, y);
  display.print("POTS");
  display.setTextColor(SH110X_WHITE);

  // CLK
  int clkX = 30;
  if (clkSel && !midiEditing) {
    display.fillRect(clkX - 2, y - 6, 34, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(clkX, y);
  display.print("CLK:");
  if (midiEditing && clkSel) {
    display.setTextColor(SH110X_WHITE);
    int vx = display.getCursorX();
    display.fillRect(vx, y - 6, 18, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.print(clkVal ? "ON" : "OFF");
  display.setTextColor(SH110X_WHITE);

  // BACK (right)
  int backX = 100;
  if (midiMenuSelection == MIDIMENU_BACK) {
    display.fillRect(backX - 2, y - 6, 28, itemH, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  display.setCursor(backX, y);
  display.print("BACK");
  display.setTextColor(SH110X_WHITE);

  // Footer with help text
  display.drawLine(0, 55, 127, 55, SH110X_WHITE);
  display.setCursor(2, 62);
  if (routeEditing) {
    if (routeEditLevel == ROUTE_EDIT_FROM) {
      display.print("Turn=ch Push=next");
    } else {
      display.print("Turn=to Push=save");
    }
  } else if (midiEditing) {
    display.print("Turn=adj Push=save");
  } else {
    display.print("Push=edit Turn=select");
  }

  display.setFont(NULL);
}
