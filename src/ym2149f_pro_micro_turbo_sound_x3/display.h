#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"

// ============================================================================
// DISPLAY MODULE
// OLED visualization and menu system
// ============================================================================

// --- Display Modes ---
enum DisplayMode {
  DISPLAY_VIZ,
  DISPLAY_MENU,
  DISPLAY_SETTINGS,
  DISPLAY_POTS,
  DISPLAY_FX,
  DISPLAY_PRESETS,
  DISPLAY_MIDI
};

// --- Visualization Modes ---
#define VIZ_MODE_BARS   0
#define VIZ_MODE_SCOPE  1
#define VIZ_MODE_MATRIX 2
#define VIZ_MODE_COUNT  3
extern uint8_t vizMode;

// --- Display State ---
extern Adafruit_SH1106G display;
extern DisplayMode displayMode;

// --- Menu State ---
extern int menuSelection;
extern bool editingValue;
extern int tempModeValue;
extern int tempLinkValue;

// Main menu item indices
#define MENU_ITEM_COUNT 10
#define MENU_MODE 0
#define MENU_LINK 1       // Global link mode: OFF / CH1 / CH2 / ALL
#define MENU_CHIP0 2
#define MENU_CHIP1 3
#define MENU_CHIP2 4
#define MENU_FX 5         // FX Chip submenu (uses Chip 2 for effects)
#define MENU_POTS 6
#define MENU_PRESETS 7    // Preset save/load submenu
#define MENU_MIDI 8       // MIDI channel settings
#define MENU_EXIT 9
#define MODE_COUNT 3
#define LINK_MODE_COUNT 4  // OFF, CH1, CH2, ALL

// Currently selected chip for settings (0, 1, or 2)
extern uint8_t currentChip;

// Settings menu - main level (category-based)
// Note: LINK only shown when currentChip == 0
#define SETTINGS_ITEM_COUNT_CHIP0 11  // Full count for Chip 0
#define SETTINGS_ITEM_COUNT 10        // Count for Chips 1 and 2 (no LINK)
#define SETTINGS_SCOPE 0      // ALL / A / B / C (within current chip)
#define SETTINGS_LINK 1       // Chip 0 only: voice linking (OFF, +B, +C, ALL)
#define SETTINGS_PITCH 2      // → Pitch submenu (detune, octave)
#define SETTINGS_VIBRATO 3    // → Vibrato submenu
#define SETTINGS_TREMOLO 4    // → Tremolo submenu (volume LFO)
#define SETTINGS_NOISE 5
#define SETTINGS_ENVELOPE 6   // → Envelope submenu
#define SETTINGS_SID 7        // → SID submenu
#define SETTINGS_GLIDE 8      // → Glide/Portamento submenu
#define SETTINGS_PITCH_ENV 9  // → Pitch envelope submenu
#define SETTINGS_BACK 10

// Settings submenu levels
#define SUBMENU_NONE 0
#define SUBMENU_VIBRATO 1
#define SUBMENU_ENVELOPE 2
#define SUBMENU_SID 3
#define SUBMENU_PITCH 4
#define SUBMENU_GLIDE 5
#define SUBMENU_TREMOLO 6
#define SUBMENU_PITCH_ENV 7
#define SUBMENU_FX 8

// FX submenu items (accessible from main menu)
// Parameters shown depend on fxType
#define FXMENU_ITEM_COUNT 9
#define FXMENU_ENABLED 0
#define FXMENU_TYPE 1
#define FXMENU_ROUTING 2
#define FXMENU_PARAM1 3   // DELAY/PATTERN/DETUNE/OFFSET
#define FXMENU_PARAM2 4   // REPEATS/SPEED (or hidden)
#define FXMENU_PARAM3 5   // DECAY/OCTAVE (or hidden)
#define FXMENU_PARAM4 6   // VOLUME (echo) / DETUNE (reverb)
#define FXMENU_PARAM5 7   // VOLUME (reverb only)
#define FXMENU_BACK 8

// Vibrato submenu items
#define VIBMENU_ITEM_COUNT 5
#define VIBMENU_ON 0
#define VIBMENU_RATE 1
#define VIBMENU_DEPTH 2
#define VIBMENU_DELAY 3
#define VIBMENU_BACK 4

// Envelope submenu items
#define ENVMENU_ITEM_COUNT 4
#define ENVMENU_ATTACK 0
#define ENVMENU_DECAY 1
#define ENVMENU_SUSTAIN 2
#define ENVMENU_BACK 3

// SID submenu items
#define SIDMENU_ITEM_COUNT 4
#define SIDMENU_ON 0
#define SIDMENU_WAVE 1
#define SIDMENU_DUTY 2
#define SIDMENU_BACK 3

// Pitch submenu items
#define PITCHMENU_ITEM_COUNT 4
#define PITCHMENU_DETUNE 0
#define PITCHMENU_OCTAVE 1
#define PITCHMENU_VOLUME 2
#define PITCHMENU_BACK 3

// Glide submenu items
#define GLIDEMENU_ITEM_COUNT 3
#define GLIDEMENU_ON 0
#define GLIDEMENU_SPEED 1
#define GLIDEMENU_BACK 2

// Tremolo submenu items
#define TREMMENU_ITEM_COUNT 4
#define TREMMENU_ON 0
#define TREMMENU_RATE 1
#define TREMMENU_DEPTH 2
#define TREMMENU_BACK 3

// Pitch envelope submenu items
#define PENVMENU_ITEM_COUNT 4
#define PENVMENU_AMT 0
#define PENVMENU_TIME 1
#define PENVMENU_DIR 2
#define PENVMENU_BACK 3

// Current submenu level (0 = main settings, 1-3 = effect submenus)
extern uint8_t settingsSubmenu;
extern int submenuSelection;
extern bool submenuEditing;
extern int submenuTempValue;

// Scope within a chip (0=ALL, 1=A, 2=B, 3=C)
#define SCOPE_COUNT 4
extern uint8_t currentScope;

// Pots menu item indices
#define POTS_ITEM_COUNT 4
#define POTS_POT1 0
#define POTS_POT2 1
#define POTS_POT3 2
#define POTS_BACK 3

// Pot editing levels (3-step cycle: Category → Param → Target)
#define POT_EDIT_NONE 0      // Not editing, just selecting pot
#define POT_EDIT_CATEGORY 1  // Cycling through categories
#define POT_EDIT_PARAM 2     // Cycling through params within category
#define POT_EDIT_TARGET 3    // Cycling through targets (if applicable)

// Settings menu state
extern int settingsSelection;
extern bool settingsEditing;
extern int settingsTempValue;

// Pots menu state
extern int potsSelection;
extern uint8_t potsEditLevel;    // POT_EDIT_NONE, CATEGORY, PARAM, TARGET
extern uint8_t potsTempCategory; // Temp category during editing
extern uint8_t potsTempParam;    // Temp param index during editing
extern uint8_t potsTempTarget;   // Temp target during editing

// FX menu state
extern int fxSelection;
extern bool fxEditing;
extern int fxTempValue;

// Presets menu item indices
#define PRESETMENU_ITEM_COUNT 4
#define PRESETMENU_LOAD 0
#define PRESETMENU_SAVE 1
#define PRESETMENU_DELETE 2
#define PRESETMENU_BACK 3

// Preset submenu levels
#define PRESET_LEVEL_MENU 0      // Main preset menu (LOAD/SAVE/DELETE/BACK)
#define PRESET_LEVEL_LOAD 1      // Scrolling through presets to load
#define PRESET_LEVEL_SAVE 2      // Selecting slot to save
#define PRESET_LEVEL_NAME 3      // Entering preset name
#define PRESET_LEVEL_DELETE 4    // Selecting preset to delete

// Preset menu state
extern int presetMenuSelection;
extern uint8_t presetMenuLevel;      // PRESET_LEVEL_*
extern int presetScrollIndex;        // Current scroll position in list
extern int presetSelectedSlot;       // Selected slot for save/load/delete
extern char presetNameBuffer[9];     // Name being edited (8 chars + null)
extern uint8_t presetNameCursor;     // Current character position (0-7)
extern bool presetNameEditing;       // Currently editing character

// MIDI menu item indices
#define MIDIMENU_ITEM_COUNT 4
#define MIDIMENU_SYNTH 0     // Synth MIDI channel
#define MIDIMENU_DRUMS 1     // Drum MIDI channel
#define MIDIMENU_VIZ 2       // Visualization mode (BARS/SCOPE)
#define MIDIMENU_BACK 3

// MIDI menu state
extern int midiMenuSelection;
extern bool midiEditing;
extern int midiTempValue;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize display
bool displayInit();

// Update visualization display
void updateDisplay();
void updateDisplayScope();   // Oscilloscope visualization
void updateDisplayMatrix();  // 3x3 grid oscilloscope (all 9 voices)

// Update main menu display
void updateMenu();

// Update settings menu display (main level)
void updateSettingsMenu();

// Update effect submenus
void updateVibratoSubmenu();
void updateEnvelopeSubmenu();
void updateSIDSubmenu();
void updatePitchSubmenu();
void updateGlideSubmenu();
void updateTremoloSubmenu();
void updatePitchEnvSubmenu();
void updateFXSubmenu();

// Update pots submenu display
void updatePotsMenu();

// Update presets submenu display
void updatePresetsMenu();

// Update MIDI settings menu
void updateMidiMenu();

// Handle encoder input for menu navigation
void handleEncoder();

// Get current mode index (0=Mono, 1=Semi, 2=Poly)
int getCurrentModeIndex();

// Get mode name from index
const char* getModeName(int idx);

// Apply mode from index
void applyModeFromIndex(int modeIdx);

// Get link mode name from index
const char* getLinkModeName(int idx);
