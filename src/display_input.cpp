#include "display.h"
#include "encoder.h"
#include "voice_manager.h"
#include "sid_mode.h"
#include "settings.h"
#include "dual_core.h"
#include "fx_chip.h"
#include "sample_player.h"
#include "preset.h"

// ============================================================================
// ENCODER INPUT HANDLING
// ============================================================================

void handleEncoder() {
  int delta = getEncoderDelta();

  if (delta != 0) {
    if (displayMode == DISPLAY_MENU) {
      if (editingValue) {
        if (menuSelection == MENU_MODE) {
          tempModeValue += delta;
          if (tempModeValue < 0) tempModeValue = MODE_COUNT - 1;
          if (tempModeValue >= MODE_COUNT) tempModeValue = 0;
        }
        else if (menuSelection == MENU_LINK) {
          tempLinkValue += delta;
          if (tempLinkValue < 0) tempLinkValue = LINK_MODE_COUNT - 1;
          if (tempLinkValue >= LINK_MODE_COUNT) tempLinkValue = 0;
        }
      } else {
        menuSelection += delta;
        if (menuSelection < 0) menuSelection = MENU_ITEM_COUNT - 1;
        if (menuSelection >= MENU_ITEM_COUNT) menuSelection = 0;
        // Skip Chip 0 in SID mode
        if (sidModeGlobal && menuSelection == MENU_CHIP0) {
          menuSelection += delta > 0 ? 1 : -1;
          if (menuSelection < 0) menuSelection = MENU_ITEM_COUNT - 1;
          if (menuSelection >= MENU_ITEM_COUNT) menuSelection = 0;
        }
      }
    }
    else if (displayMode == DISPLAY_SETTINGS) {
      if (settingsSubmenu == SUBMENU_NONE) {
        // Main settings menu
        if (settingsEditing) {
          settingsTempValue += delta;
          int maxVal = getSettingsMax(settingsSelection);
          if (settingsTempValue < 0) settingsTempValue = maxVal;
          if (settingsTempValue > maxVal) settingsTempValue = 0;
          // Skip chip 0 in SID mode
          if (settingsSelection == SETTINGS_CHIP && sidModeGlobal && settingsTempValue == 0) {
            settingsTempValue = (delta > 0) ? 1 : maxVal;
          }
        } else {
          settingsSelection = nextVisibleSettingsItem(settingsSelection, delta);
        }
      }
      else if (settingsSubmenu == SUBMENU_VIBRATO) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getVibratoMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // Skip chip 0 in SID mode
          if (submenuSelection == -2 && sidModeGlobal && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyVibratoValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = VIBMENU_ITEM_COUNT - 1;
          if (submenuSelection >= VIBMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
      else if (settingsSubmenu == SUBMENU_ENVELOPE) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getEnvelopeMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          if (submenuSelection == -2 && sidModeGlobal && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyEnvelopeValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = ENVMENU_ITEM_COUNT - 1;
          if (submenuSelection >= ENVMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getPitchMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          if (submenuSelection == -2 && sidModeGlobal && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyPitchValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = PITCHMENU_ITEM_COUNT - 1;
          if (submenuSelection >= PITCHMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
      else if (settingsSubmenu == SUBMENU_GLIDE) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getGlideMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          if (submenuSelection == -2 && sidModeGlobal && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyGlideValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = GLIDEMENU_ITEM_COUNT - 1;
          if (submenuSelection >= GLIDEMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
      else if (settingsSubmenu == SUBMENU_TREMOLO) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getTremoloMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          if (submenuSelection == -2 && sidModeGlobal && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyTremoloValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = TREMMENU_ITEM_COUNT - 1;
          if (submenuSelection >= TREMMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH_ENV) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getPitchEnvMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          if (submenuSelection == -2 && sidModeGlobal && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applyPitchEnvValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = PENVMENU_ITEM_COUNT - 1;
          if (submenuSelection >= PENVMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
      else if (settingsSubmenu == SUBMENU_SID) {
        if (submenuEditing) {
          submenuTempValue += delta;
          int maxVal = (submenuSelection == -2) ? 2 : (submenuSelection == -1) ? (SCOPE_COUNT - 1) : getSidMax(submenuSelection);
          if (submenuTempValue < 0) submenuTempValue = maxVal;
          if (submenuTempValue > maxVal) submenuTempValue = 0;
          // SID submenu: always skip chip 0 (SID only on chips 1+2)
          if (submenuSelection == -2 && submenuTempValue == 0) {
            submenuTempValue = (delta > 0) ? 1 : maxVal;
          }
          // Real-time apply
          if (submenuSelection == -2) {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
          } else if (submenuSelection == -1) {
            currentScope = (uint8_t)submenuTempValue;
          } else {
            applySidValue(submenuSelection, submenuTempValue);
          }
        } else {
          submenuSelection += delta;
          if (submenuSelection < -2) submenuSelection = SIDMENU_ITEM_COUNT - 1;
          if (submenuSelection >= SIDMENU_ITEM_COUNT) submenuSelection = -2;
        }
      }
    }
    else if (displayMode == DISPLAY_POTS) {
      if (potsEditLevel == POT_EDIT_NONE) {
        // Navigate pot list
        potsSelection += delta;
        if (potsSelection < 0) potsSelection = POTS_ITEM_COUNT - 1;
        if (potsSelection >= POTS_ITEM_COUNT) potsSelection = 0;
      } else if (potsEditLevel == POT_EDIT_CATEGORY) {
        // Cycle through categories
        int cat = (int)potsTempCategory + delta;
        if (cat < 0) cat = PCAT_COUNT - 1;
        if (cat >= PCAT_COUNT) cat = 0;

        // Skip PCAT_SID in YM mode (not applicable)
        if (!sidModeGlobal && cat == PCAT_SID) {
          cat += delta;  // Continue in same direction
          if (cat < 0) cat = PCAT_COUNT - 1;
          if (cat >= PCAT_COUNT) cat = 0;
        }

        potsTempCategory = (uint8_t)cat;
        // Reset param when category changes
        potsTempParam = 0;
      } else if (potsEditLevel == POT_EDIT_PARAM) {
        // Cycle through params within category
        uint8_t paramCount = getPotParamCount((PotCategory)potsTempCategory);
        if (paramCount > 0) {
          int p = (int)potsTempParam + delta;
          if (p < 0) p = paramCount - 1;
          if (p >= paramCount) p = 0;
          potsTempParam = (uint8_t)p;
        }
      } else if (potsEditLevel == POT_EDIT_TARGET) {
        // Cycle through targets (ALL, C0-C2, V1-V9)
        int t = (int)potsTempTarget + delta;
        if (t < 0) t = TARGET_COUNT - 1;
        if (t >= TARGET_COUNT) t = 0;
        potsTempTarget = (uint8_t)t;
      }
    }
    else if (displayMode == DISPLAY_FX) {
      if (fxEditing) {
        fxTempValue += delta;
        int maxVal = getFXMax(fxSelection);
        if (fxTempValue < 0) fxTempValue = maxVal;
        if (fxTempValue > maxVal) fxTempValue = 0;
        // Real-time apply
        applyFXValue(fxSelection, fxTempValue);
      } else {
        // Navigate through visible items
        fxSelection += delta;
        int maxItem = getVisibleFXItemCount() - 1;
        if (fxSelection < 0) fxSelection = maxItem;
        if (fxSelection > maxItem) fxSelection = 0;
      }
    }
    else if (displayMode == DISPLAY_PRESETS) {
      if (presetMenuLevel == PRESET_LEVEL_MENU) {
        // Navigate main preset menu
        presetMenuSelection += delta;
        if (presetMenuSelection < 0) presetMenuSelection = PRESETMENU_ITEM_COUNT - 1;
        if (presetMenuSelection >= PRESETMENU_ITEM_COUNT) presetMenuSelection = 0;
      }
      else if (presetMenuLevel == PRESET_LEVEL_LOAD) {
        // Scroll through all available presets (factory + user)
        int totalPresets = sidModeGlobal ? SID_PRESET_TOTAL : getLoadListCount();
        presetScrollIndex += delta;
        if (presetScrollIndex < 0) presetScrollIndex = totalPresets - 1;
        if (presetScrollIndex >= totalPresets) presetScrollIndex = 0;
      }
      else if (presetMenuLevel == PRESET_LEVEL_SAVE) {
        // Scroll through user slots
        int maxSlots = sidModeGlobal ? SID_PRESET_USER_COUNT : PRESET_USER_SLOTS;
        presetScrollIndex += delta;
        if (presetScrollIndex < 0) presetScrollIndex = maxSlots - 1;
        if (presetScrollIndex >= maxSlots) presetScrollIndex = 0;
      }
      else if (presetMenuLevel == PRESET_LEVEL_NAME) {
        if (presetNameEditing) {
          // Cycle through characters
          int charIdx = getNameCharIndex(presetNameBuffer[presetNameCursor]);
          charIdx += delta;
          if (charIdx < 0) charIdx = nameCharCount - 1;
          if (charIdx >= nameCharCount) charIdx = 0;
          presetNameBuffer[presetNameCursor] = nameChars[charIdx];
        } else {
          // Move cursor position (0-7 = name chars, 8 = SAVE, 9 = EXIT)
          presetNameCursor += delta;
          if (presetNameCursor > 9) presetNameCursor = 0;
          // Note: presetNameCursor is uint8_t so underflow wraps to 255
          if (presetNameCursor > 9) presetNameCursor = 9;
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_DELETE) {
        // Scroll through user presets
        int deleteCount;
        if (sidModeGlobal) {
          deleteCount = 0;
          for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
            if (sidPresetCacheValid ? sidPresetUsed[i] : sidPresetUserIsUsed(i))
              deleteCount++;
          }
        } else {
          deleteCount = getDeleteListCount();
        }
        if (deleteCount > 0) {
          presetScrollIndex += delta;
          if (presetScrollIndex < 0) presetScrollIndex = deleteCount - 1;
          if (presetScrollIndex >= deleteCount) presetScrollIndex = 0;
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_CONFIRM_DEL) {
        // Toggle YES/NO
        presetConfirmYes = !presetConfirmYes;
      }
    }
    else if (displayMode == DISPLAY_MIDI) {
      // Handle route editing separately
      if (midiMenuSelection == MIDIMENU_ROUTE && routeEditLevel != ROUTE_EDIT_NONE) {
        if (routeEditLevel == ROUTE_EDIT_FROM) {
          // Cycle through FROM channels (0-15)
          int newFrom = (int)routeFromChannel + delta;
          if (newFrom < 0) newFrom = 15;
          if (newFrom > 15) newFrom = 0;
          routeFromChannel = (uint8_t)newFrom;
          // Load current routing for this channel
          routeTempTo = midiChannelRemap[routeFromChannel];
        }
        else if (routeEditLevel == ROUTE_EDIT_TO) {
          // Cycle through TO channels: "=" (no remap) -> 1-16 -> "="
          if (routeTempTo == MIDI_REMAP_NONE) {
            routeTempTo = (delta > 0) ? 0 : 15;
          } else {
            int newTo = (int)routeTempTo + delta;
            if (newTo < 0) routeTempTo = MIDI_REMAP_NONE;
            else if (newTo > 15) routeTempTo = MIDI_REMAP_NONE;
            else routeTempTo = (uint8_t)newTo;
          }
        }
      }
      else if (midiEditing) {
        if (midiMenuSelection == MIDIMENU_SYNTH) {
          // Synth: OFF -> OMNI -> 1-16 -> OFF
          if (midiTempValue == MIDI_CHANNEL_OFF) {
            midiTempValue = (delta > 0) ? MIDI_CHANNEL_OMNI : 15;
          } else if (midiTempValue == MIDI_CHANNEL_OMNI) {
            midiTempValue = (delta > 0) ? 0 : MIDI_CHANNEL_OFF;
          } else {
            midiTempValue += delta;
            if (midiTempValue < 0) midiTempValue = MIDI_CHANNEL_OMNI;
            if (midiTempValue > 15) midiTempValue = MIDI_CHANNEL_OFF;
          }
        }
        else if (midiMenuSelection == MIDIMENU_DRUMS) {
          // Drums: OFF -> 1-16 -> OFF (no OMNI)
          if (midiTempValue == MIDI_CHANNEL_OFF) {
            midiTempValue = (delta > 0) ? 0 : 15;
          } else {
            midiTempValue += delta;
            if (midiTempValue < 0) midiTempValue = MIDI_CHANNEL_OFF;
            if (midiTempValue > 15) midiTempValue = MIDI_CHANNEL_OFF;
          }
        }
        else if (midiMenuSelection == MIDIMENU_VIZ) {
          // Cycle through visualization modes: BARS -> SCOPE -> MATRIX -> BARS
          midiTempValue += delta;
          if (midiTempValue < 0) midiTempValue = VIZ_MODE_COUNT - 1;
          if (midiTempValue >= VIZ_MODE_COUNT) midiTempValue = 0;
        }
        else if (midiMenuSelection == MIDIMENU_SID) {
          // Toggle between YM and SID mode
          midiTempValue = (midiTempValue == 0) ? 1 : 0;
        }
        else if (midiMenuSelection == MIDIMENU_USB) {
          // Toggle between MIDI and Serial
          midiTempValue = (midiTempValue == USB_MODE_MIDI) ? USB_MODE_SERIAL : USB_MODE_MIDI;
        }
        else if (midiMenuSelection == MIDIMENU_BRT) {
          // Brightness 0-10
          midiTempValue += delta;
          if (midiTempValue < 0) midiTempValue = 10;
          if (midiTempValue > 10) midiTempValue = 0;
          // Real-time apply so user sees the effect
          uint8_t contrast = (midiTempValue >= 10) ? 255 : (uint8_t)(midiTempValue * 25);
          display.setContrast(contrast);
        }
        else if (midiMenuSelection == MIDIMENU_CLK) {
          // Toggle clock sync on/off
          midiTempValue = (midiTempValue == 0) ? 1 : 0;
        }
      } else {
        // Navigate menu
        midiMenuSelection += delta;
        if (midiMenuSelection < 0) midiMenuSelection = MIDIMENU_ITEM_COUNT - 1;
        if (midiMenuSelection >= MIDIMENU_ITEM_COUNT) midiMenuSelection = 0;
      }
    }
  }

  // Long-press handling in preset screens
  if (encoderButtonLongPressed()) {
    if (displayMode == DISPLAY_PRESETS) {
      if (presetMenuLevel == PRESET_LEVEL_MENU) {
        // Long press on main preset menu - exit to viz
        displayMode = DISPLAY_VIZ;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_LOAD) {
        // Long press on load screen - go back to preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_LOAD;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_SAVE) {
        // Cancel - return to main preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_SAVE;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_DELETE) {
        // Long press on delete screen - go back to preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_DELETE;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_CONFIRM_DEL) {
        // Long press on confirm screen - go back to delete list
        presetMenuLevel = PRESET_LEVEL_DELETE;
        return;
      }
      else if (presetMenuLevel == PRESET_LEVEL_NAME) {
        // Long press in name entry = cancel editing current char
        if (presetNameEditing) {
          presetNameEditing = false;
        }
        // Long press on SAVE/EXIT does nothing (use short press)
        return;
      }
    }
  }

  if (encoderButtonPressed()) {
    if (displayMode == DISPLAY_VIZ) {
      displayMode = DISPLAY_MENU;
      menuSelection = 0;
      editingValue = false;
    }
    else if (displayMode == DISPLAY_MENU) {
      if (menuSelection == MENU_EXIT) {
        editingValue = false;
        displayMode = DISPLAY_VIZ;
      }
      else if (menuSelection >= MENU_CHIP0 && menuSelection <= MENU_CHIP2) {
        // Skip Chip 0 in SID mode (shouldn't happen due to navigation, but safety check)
        if (sidModeGlobal && menuSelection == MENU_CHIP0) {
          return;
        }
        currentChip = menuSelection - MENU_CHIP0;
        currentScope = 0;
        displayMode = DISPLAY_SETTINGS;
        settingsSubmenu = SUBMENU_NONE;
        settingsSelection = 0;
        settingsScrollOffset = 0;
        settingsEditing = false;
      }
      else if (menuSelection == MENU_FX) {
        displayMode = DISPLAY_FX;
        fxSelection = 0;
        fxEditing = false;
      }
      else if (menuSelection == MENU_POTS) {
        editingPotDefaults = false;
        displayMode = DISPLAY_POTS;
        potsSelection = 0;
        potsEditLevel = POT_EDIT_NONE;
      }
      else if (menuSelection == MENU_PRESETS) {
        displayMode = DISPLAY_PRESETS;
        presetMenuSelection = 0;
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetScrollIndex = 0;
      }
      else if (menuSelection == MENU_MIDI) {
        displayMode = DISPLAY_MIDI;
        midiMenuSelection = 0;
        midiEditing = false;
      }
      else if (menuSelection == MENU_RESET) {
        sendCommand(CMD_RESET_ALL);
        currentPresetIndex = PRESET_INDEX_NONE;
      }
      else if (menuSelection == MENU_MODE) {
        if (!editingValue) {
          editingValue = true;
          tempModeValue = getCurrentModeIndex();
        } else {
          applyModeFromIndex(tempModeValue);
          editingValue = false;
        }
      }
      else if (menuSelection == MENU_LINK) {
        if (!editingValue) {
          editingValue = true;
          tempLinkValue = displaySnapshotCopy.linkMode;
        } else {
          sendCommand(CMD_SET_LINK_MODE, (uint8_t)tempLinkValue);
          editingValue = false;
        }
      }
    }
    else if (displayMode == DISPLAY_SETTINGS) {
      if (settingsSubmenu == SUBMENU_NONE) {
        // Main settings menu
        if (settingsSelection == SETTINGS_BACK) {
          settingsEditing = false;
          displayMode = DISPLAY_MENU;
          menuSelection = MENU_CHIP0 + currentChip;
        }
        else if (settingsSelection == SETTINGS_VIBRATO) {
          // Enter vibrato submenu
          settingsSubmenu = SUBMENU_VIBRATO;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_ENVELOPE) {
          // Enter envelope submenu
          settingsSubmenu = SUBMENU_ENVELOPE;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_PITCH) {
          // Enter Pitch submenu
          settingsSubmenu = SUBMENU_PITCH;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_GLIDE) {
          // Enter Glide submenu
          settingsSubmenu = SUBMENU_GLIDE;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_TREMOLO) {
          // Enter Tremolo submenu
          settingsSubmenu = SUBMENU_TREMOLO;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_PITCH_ENV) {
          // Enter Pitch Envelope submenu
          settingsSubmenu = SUBMENU_PITCH_ENV;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (settingsSelection == SETTINGS_SID) {
          // Enter SID submenu
          settingsSubmenu = SUBMENU_SID;
          submenuSelection = 0;
          submenuEditing = false;
        }
        else if (!settingsEditing) {
          settingsEditing = true;
          settingsTempValue = getSettingsValue(settingsSelection);
        } else {
          applySettingsValue(settingsSelection, settingsTempValue);
          settingsEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_VIBRATO) {
        if (submenuSelection == VIBMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getVibratoValue(submenuSelection);
        } else {
          applyVibratoValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_ENVELOPE) {
        if (submenuSelection == ENVMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getEnvelopeValue(submenuSelection);
        } else {
          applyEnvelopeValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH) {
        if (submenuSelection == PITCHMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getPitchValue(submenuSelection);
        } else {
          applyPitchValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_GLIDE) {
        if (submenuSelection == GLIDEMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getGlideValue(submenuSelection);
        } else {
          applyGlideValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_TREMOLO) {
        if (submenuSelection == TREMMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getTremoloValue(submenuSelection);
        } else {
          applyTremoloValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_PITCH_ENV) {
        if (submenuSelection == PENVMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getPitchEnvValue(submenuSelection);
        } else {
          applyPitchEnvValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
      else if (settingsSubmenu == SUBMENU_SID) {
        if (submenuSelection == SIDMENU_BACK) {
          submenuEditing = false;
          settingsSubmenu = SUBMENU_NONE;
        }
        else if (submenuSelection == -2) {
          // Header chip selection (SID only on chips 1+2)
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentChip;
          } else {
            currentChip = (uint8_t)submenuTempValue;
            currentScope = 0;
            submenuEditing = false;
          }
        }
        else if (submenuSelection == -1) {
          // Header scope selection
          if (!submenuEditing) {
            submenuEditing = true;
            submenuTempValue = currentScope;
          } else {
            currentScope = (uint8_t)submenuTempValue;
            submenuEditing = false;
          }
        }
        else if (!submenuEditing) {
          submenuEditing = true;
          submenuTempValue = getSidValue(submenuSelection);
        } else {
          applySidValue(submenuSelection, submenuTempValue);
          submenuEditing = false;
        }
      }
    }
    else if (displayMode == DISPLAY_POTS) {
      if (potsSelection == POTS_BACK) {
        // Back button - exit
        potsEditLevel = POT_EDIT_NONE;
        if (editingPotDefaults) {
          editingPotDefaults = false;
          displayMode = DISPLAY_MIDI;
          midiMenuSelection = MIDIMENU_POTS;
        } else {
          displayMode = DISPLAY_MENU;
          menuSelection = MENU_POTS;
        }
      }
      else if (potsEditLevel == POT_EDIT_NONE) {
        // Start editing - load current values from appropriate source
        potsEditLevel = POT_EDIT_CATEGORY;
        if (editingPotDefaults) {
          potsTempCategory = potDefaultAssignments[potsSelection].category;
          potsTempParam = potDefaultAssignments[potsSelection].paramIndex;
          potsTempTarget = potDefaultAssignments[potsSelection].target;
        } else {
          potsTempCategory = displaySnapshotCopy.potAssignments[potsSelection].category;
          potsTempParam = displaySnapshotCopy.potAssignments[potsSelection].paramIndex;
          potsTempTarget = displaySnapshotCopy.potAssignments[potsSelection].target;
        }

        // If pot has SID category but we're in YM mode, reset to OFF
        if (!sidModeGlobal && potsTempCategory == PCAT_SID) {
          potsTempCategory = PCAT_OFF;
          potsTempParam = 0;
        }
      }
      else if (potsEditLevel == POT_EDIT_CATEGORY) {
        if (potsTempCategory == PCAT_OFF) {
          // OFF has no params/target - save immediately
          if (editingPotDefaults) {
            potDefaultAssignments[potsSelection] = { PCAT_OFF, 0, TARGET_ALL, 0 };
          } else {
            sendCommand(CMD_SET_POT_ASSIGN, potsSelection, potsTempCategory, 0, TARGET_ALL);
          }
          potsEditLevel = POT_EDIT_NONE;
        } else {
          // Move to param selection
          potsEditLevel = POT_EDIT_PARAM;
        }
      }
      else if (potsEditLevel == POT_EDIT_PARAM) {
        if (categoryRequiresTarget((PotCategory)potsTempCategory)) {
          // Move to target selection
          potsEditLevel = POT_EDIT_TARGET;
        } else {
          // No targeting needed - save immediately
          if (editingPotDefaults) {
            potDefaultAssignments[potsSelection] = { (PotCategory)potsTempCategory, potsTempParam, TARGET_NONE, 0 };
          } else {
            sendCommand(CMD_SET_POT_ASSIGN, potsSelection, potsTempCategory, potsTempParam, TARGET_NONE);
          }
          potsEditLevel = POT_EDIT_NONE;
        }
      }
      else if (potsEditLevel == POT_EDIT_TARGET) {
        // Save the assignment
        if (editingPotDefaults) {
          potDefaultAssignments[potsSelection] = { (PotCategory)potsTempCategory, potsTempParam, potsTempTarget, 0 };
        } else {
          sendCommand(CMD_SET_POT_ASSIGN, potsSelection, potsTempCategory, potsTempParam, potsTempTarget);
        }
        potsEditLevel = POT_EDIT_NONE;
      }
    }
    else if (displayMode == DISPLAY_FX) {
      // If SID mode active, any press goes back (FX disabled)
      if (sidModeGlobal) {
        displayMode = DISPLAY_MENU;
        menuSelection = MENU_FX;
      }
      else if (fxSelection == getVisibleFXItemCount() - 1) {
        // BACK item - return to menu
        fxEditing = false;
        displayMode = DISPLAY_MENU;
        menuSelection = MENU_FX;
      }
      else if (!fxEditing) {
        fxEditing = true;
        fxTempValue = getFXValue(fxSelection);
      } else {
        applyFXValue(fxSelection, fxTempValue);
        fxEditing = false;
      }
    }
    else if (displayMode == DISPLAY_PRESETS) {
      if (presetMenuLevel == PRESET_LEVEL_MENU) {
        // Main preset menu actions
        if (presetMenuSelection == PRESETMENU_BACK) {
          displayMode = DISPLAY_MENU;
          menuSelection = MENU_PRESETS;
        }
        else if (presetMenuSelection == PRESETMENU_LOAD) {
          presetMenuLevel = PRESET_LEVEL_LOAD;
          presetScrollIndex = 0;
          if (sidModeGlobal) cacheSidPresets();
        }
        else if (presetMenuSelection == PRESETMENU_SAVE) {
          presetMenuLevel = PRESET_LEVEL_SAVE;
          presetScrollIndex = 0;
          if (sidModeGlobal) cacheSidPresets();
        }
        else if (presetMenuSelection == PRESETMENU_DELETE) {
          presetMenuLevel = PRESET_LEVEL_DELETE;
          presetScrollIndex = 0;
          if (sidModeGlobal) cacheSidPresets();
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_LOAD) {
        // Load selected preset (factory or user)
        if (sidModeGlobal) {
          // SID mode: load SID preset directly
          sidPresetLoad(presetScrollIndex);
        } else {
          // Regular mode: load via command queue
          uint8_t presetIdx = getLoadListPresetIndex(presetScrollIndex);
          if (presetIdx != PRESET_INDEX_NONE) {
            sendCommand(CMD_PRESET_LOAD, presetIdx);
          }
        }
        // Return to main preset menu
        presetMenuLevel = PRESET_LEVEL_MENU;
        presetMenuSelection = PRESETMENU_LOAD;
      }
      else if (presetMenuLevel == PRESET_LEVEL_SAVE) {
        // Selected a slot - enter name entry mode
        presetSelectedSlot = presetScrollIndex;
        presetMenuLevel = PRESET_LEVEL_NAME;

        // Initialize name buffer - use existing name or blank
        if (sidModeGlobal) {
          // Use cache to avoid flash read (prevents SID timer bus conflict)
          if (sidPresetCacheValid && sidPresetUsed[presetSelectedSlot]) {
            strcpy(presetNameBuffer, sidPresetNames[SID_PRESET_FACTORY_COUNT + presetSelectedSlot]);
          } else {
            strcpy(presetNameBuffer, "        ");
          }
        } else {
          if (presetUserIsUsed(presetSelectedSlot)) {
            presetGetName(USER_SLOT_TO_PRESET(presetSelectedSlot), presetNameBuffer);
          } else {
            strcpy(presetNameBuffer, "        ");
          }
        }
        presetNameCursor = 0;
        presetNameEditing = false;
      }
      else if (presetMenuLevel == PRESET_LEVEL_NAME) {
        if (presetNameCursor == 8) {
          // SAVE selected - save the preset
          // Show "Saving..." IMMEDIATELY before flash write pauses Core 1
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(40, 28);
          display.print("Saving...");
          display.display();  // Force update to OLED NOW

          presetMenuLevel = PRESET_LEVEL_MENU;
          presetMenuSelection = PRESETMENU_SAVE;

          if (sidModeGlobal) {
            // SID mode: use pending flag (Core 0 handles flash write)
            strncpy(presetNameCmd, presetNameBuffer, 8);
            presetNameCmd[8] = '\0';
            __dmb();  // Ensure name is fully written before flag is visible to Core 0
            sidPresetSaveSlot = presetSelectedSlot;
            sidPresetSavePending = true;
            sidPresetCacheValid = false;  // Invalidate cache
          } else {
            // Regular mode: use command queue for Core 0
            strncpy(presetNameCmd, presetNameBuffer, 8);
            presetNameCmd[8] = '\0';
            __dmb();  // Ensure name is fully written before flag is visible to Core 0
            // Set pending flag - Core 0 will handle the flash operation
            presetSaveSlot = presetSelectedSlot;
            presetSavePending = true;
          }
        }
        else if (presetNameCursor == 9) {
          // EXIT selected - return to preset menu without saving
          presetMenuLevel = PRESET_LEVEL_MENU;
          presetMenuSelection = PRESETMENU_SAVE;
        }
        else if (!presetNameEditing) {
          // Start editing current character
          presetNameEditing = true;
        } else {
          // Move to next character
          presetNameEditing = false;
          presetNameCursor++;
          // After position 7, cursor goes to SAVE (position 8)
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_DELETE) {
        int deleteCount;
        if (sidModeGlobal) {
          deleteCount = 0;
          for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
            if (sidPresetCacheValid ? sidPresetUsed[i] : sidPresetUserIsUsed(i))
              deleteCount++;
          }
        } else {
          deleteCount = getDeleteListCount();
        }
        if (deleteCount == 0) {
          // No presets - just return to menu
          presetMenuLevel = PRESET_LEVEL_MENU;
          presetMenuSelection = PRESETMENU_DELETE;
        } else {
          // Go to confirmation screen
          presetConfirmYes = false;  // Default to NO
          presetMenuLevel = PRESET_LEVEL_CONFIRM_DEL;
        }
      }
      else if (presetMenuLevel == PRESET_LEVEL_CONFIRM_DEL) {
        if (presetConfirmYes) {
          // Confirmed - delete the preset
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(36, 28);
          display.print("Deleting...");
          display.display();

          // Count for scroll adjustment
          int deleteCount;
          if (sidModeGlobal) {
            deleteCount = 0;
            for (uint8_t i = 0; i < SID_PRESET_USER_COUNT; i++) {
              if (sidPresetCacheValid ? sidPresetUsed[i] : sidPresetUserIsUsed(i))
                deleteCount++;
            }
          } else {
            deleteCount = getDeleteListCount();
          }

          int newCount = deleteCount - 1;
          if (newCount == 0) {
            presetMenuLevel = PRESET_LEVEL_MENU;
            presetMenuSelection = PRESETMENU_DELETE;
          } else {
            presetMenuLevel = PRESET_LEVEL_DELETE;
            if (presetScrollIndex >= newCount) {
              presetScrollIndex = newCount - 1;
            }
          }

          if (sidModeGlobal) {
            sidPresetDeleteSlot = presetSelectedSlot;
            sidPresetDeletePending = true;
            // Update cache in-place (Core 0 hasn't deleted yet, so don't re-read flash)
            if (sidPresetCacheValid && presetSelectedSlot < SID_PRESET_USER_COUNT) {
              sidPresetUsed[presetSelectedSlot] = false;
              strcpy(sidPresetNames[SID_PRESET_FACTORY_COUNT + presetSelectedSlot], "--------");
            }
          } else {
            presetDeleteSlot = presetSelectedSlot;
            presetDeletePending = true;
          }
        } else {
          // Cancelled - go back to delete list
          presetMenuLevel = PRESET_LEVEL_DELETE;
        }
      }
    }
    else if (displayMode == DISPLAY_MIDI) {
      if (midiMenuSelection == MIDIMENU_BACK) {
        // Save MIDI settings to flash when exiting menu
        midiSavePending = true;  // Core 0 will handle flash write
        displayMode = DISPLAY_MENU;
        menuSelection = MENU_MIDI;
      }
      else if (midiMenuSelection == MIDIMENU_SYNTH) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = midiSynthChannel;
        } else {
          midiSynthChannel = midiTempValue;
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_DRUMS) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = midiDrumChannel;
        } else {
          midiDrumChannel = midiTempValue;
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_ROUTE) {
        // Route editing: 3-step cycle: NONE -> FROM -> TO -> NONE (apply)
        if (routeEditLevel == ROUTE_EDIT_NONE) {
          // Start editing - select FROM channel
          routeEditLevel = ROUTE_EDIT_FROM;
          routeFromChannel = 0;
          routeTempTo = midiChannelRemap[0];
        }
        else if (routeEditLevel == ROUTE_EDIT_FROM) {
          // Move to TO channel selection
          routeEditLevel = ROUTE_EDIT_TO;
          // routeTempTo already loaded when FROM was selected
        }
        else if (routeEditLevel == ROUTE_EDIT_TO) {
          // Apply the routing change
          midiChannelRemap[routeFromChannel] = routeTempTo;
          routeEditLevel = ROUTE_EDIT_NONE;
          // Note: Settings will be saved when exiting MIDI menu via BACK
        }
      }
      else if (midiMenuSelection == MIDIMENU_VIZ) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = vizMode;
        } else {
          vizMode = midiTempValue;
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_SID) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = sidModeGlobal ? 1 : 0;
        } else {
          // Apply SID mode change
          bool newSidMode = (midiTempValue != 0);
          if (newSidMode != sidModeGlobal) {
            // Kill all notes first
            allNotesOffPanic();

            // Reset all voice settings to defaults
            settingsInit();

            // Reset all effects state (envelopes, vibrato, tremolo, etc.)
            effectsInit();

            // Reset FX to disabled
            fxSetEnabled(false);

            // Reset poly mode to full poly (1)
            polyMode = 1;

            if (newSidMode) {
              // Entering SID mode
              sidModeInit();
            } else {
              // Exiting SID mode
              sidModeStop();
            }
            sidModeGlobal = newSidMode;
          }
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_USB) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = usbMode;
        } else {
          usbMode = midiTempValue;
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_BRT) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = (displayBrightness + 12) / 25;  // Convert 0-255 to 0-10
          if (midiTempValue > 10) midiTempValue = 10;
        } else {
          displayBrightness = (midiTempValue >= 10) ? 255 : (uint8_t)(midiTempValue * 25);
          display.setContrast(displayBrightness);
          midiEditing = false;
        }
      }
      else if (midiMenuSelection == MIDIMENU_POTS) {
        // Enter pot defaults editing screen
        editingPotDefaults = true;
        displayMode = DISPLAY_POTS;
        potsSelection = 0;
        potsEditLevel = POT_EDIT_NONE;
      }
      else if (midiMenuSelection == MIDIMENU_CLK) {
        if (!midiEditing) {
          midiEditing = true;
          midiTempValue = fxClockSync ? 1 : 0;
        } else {
          fxClockSync = (midiTempValue != 0);
          midiEditing = false;
        }
      }
    }
  }
}
