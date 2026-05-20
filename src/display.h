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
#define VIZ_MODE_BARS     0
#define VIZ_MODE_SCOPE    1
#define VIZ_MODE_MATRIX   2
#define VIZ_MODE_CHMATRIX 3
#define VIZ_MODE_SAMPLE   4
#define VIZ_MODE_STATIC   5
#define VIZ_MODE_COUNT    6
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
#define MENU_ITEM_COUNT 11
#define MENU_MODE 0
#define MENU_LINK 1       // Global link mode: OFF / CH1 / CH2 / ALL
#define MENU_CHIP0 2
#define MENU_CHIP1 3
#define MENU_CHIP2 4
#define MENU_FX 5         // FX Chip submenu (uses Chip 2 for effects)
#define MENU_POTS 6
#define MENU_PRESETS 7    // Preset save/load submenu
#define MENU_MIDI 8       // MIDI channel settings
#define MENU_RESET 9      // Reset all voices and FX to defaults
#define MENU_EXIT 10
#define MODE_COUNT 4  // MONO, SEMI, POLY, MULTI
#define LINK_MODE_COUNT 4  // OFF, CH1, CH2, ALL

// Currently selected chip for settings (0, 1, or 2)
extern uint8_t currentChip;

// Settings menu - main level (category-based)
// Note: SID only shown when sidModeGlobal && (currentChip == 1 || currentChip == 2) - after SCOPE
// Note: LINK only shown when currentChip == 0
#define SETTINGS_ITEM_COUNT_CHIP0 12  // Full count for Chip 0 (no SID)
#define SETTINGS_ITEM_COUNT 12        // Total items
#define SETTINGS_CHIP 0       // Chip selector (0 / 1 / 2)
#define SETTINGS_SCOPE 1      // ALL / A / B / C (within current chip)
#define SETTINGS_SID 2        // SID submenu - only chips 1+2 in SID mode
#define SETTINGS_LINK 3       // Chip 0 only: voice linking (OFF, +B, +C, ALL)
#define SETTINGS_PITCH 4      // → Pitch submenu (detune, octave)
#define SETTINGS_VIBRATO 5    // → Vibrato submenu
#define SETTINGS_TREMOLO 6    // → Tremolo submenu (volume LFO)
#define SETTINGS_NOISE 7
#define SETTINGS_ENVELOPE 8   // → Envelope submenu
#define SETTINGS_GLIDE 9      // → Glide/Portamento submenu
#define SETTINGS_PITCH_ENV 10 // → Pitch envelope submenu
#define SETTINGS_BACK 11

// Settings submenu levels
#define SUBMENU_NONE 0
#define SUBMENU_VIBRATO 1
#define SUBMENU_ENVELOPE 2
#define SUBMENU_PITCH 3
#define SUBMENU_GLIDE 4
#define SUBMENU_TREMOLO 5
#define SUBMENU_PITCH_ENV 6
#define SUBMENU_FX 7
#define SUBMENU_SID 8
#define SUBMENU_SMPL_PRESET 9

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
#define ENVMENU_ITEM_COUNT 5
#define ENVMENU_ATTACK 0
#define ENVMENU_DECAY 1
#define ENVMENU_SUSTAIN 2
#define ENVMENU_RELEASE 3
#define ENVMENU_BACK 4

// SID submenu items
#define SIDMENU_ITEM_COUNT 7
#define SIDMENU_WAVE 0
#define SIDMENU_DUTY 1
#define SIDMENU_PWM_RATE 2
#define SIDMENU_PWM_DEPTH 3
#define SIDMENU_NOISE 4
#define SIDMENU_RELEASE 5
#define SIDMENU_BACK 6

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


// SMPL settings menu items (used when sampleModeGlobal is active)
#define SMPL_ITEM_COUNT 10
#define SMPL_SECTION 0
#define SMPL_SAMPLE 1
#define SMPL_MODE 2
#define SMPL_VOL 3
#define SMPL_PITCH 4
#define SMPL_OCT 5
#define SMPL_LEN 6
#define SMPL_CRUSH 7
#define SMPL_PRESET 8
#define SMPL_BACK 9

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
extern int settingsScrollOffset;

// Pots menu state
extern bool editingPotDefaults;  // True when editing defaults from SET menu
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
#define PRESET_LEVEL_CONFIRM_DEL 5 // Confirm delete (YES/NO)

// Preset menu state
extern int presetMenuSelection;
extern uint8_t presetMenuLevel;      // PRESET_LEVEL_*
extern int presetScrollIndex;        // Current scroll position in list
extern int presetSelectedSlot;       // Selected slot for save/load/delete
extern char presetNameBuffer[9];     // Name being edited (8 chars + null)
extern uint8_t presetNameCursor;     // Current character position (0-7)
extern bool presetNameEditing;       // Currently editing character
extern bool presetConfirmYes;        // Confirm delete selection (false=NO, true=YES)
extern bool presetSaving;
extern unsigned long presetSaveStartTime;
extern bool presetDeleting;
extern unsigned long presetDeleteStartTime;
#define PRESET_SAVE_DISPLAY_MS 600

// SID preset cache (shared across display files)
extern bool sidPresetCacheValid;
extern char sidPresetNames[20][9];   // SID_PRESET_TOTAL = 20 (4 factory + 16 user)
extern bool sidPresetUsed[16];       // SID_PRESET_USER_COUNT = 16
void cacheSidPresets();

// SMPL preset cache (shared across display files)
extern bool smplPresetCacheValid;
extern char smplPresetNames[16][9];  // SMPL_PRESET_USER_COUNT = 16
extern bool smplPresetUsed[16];      // SMPL_PRESET_USER_COUNT = 16
void cacheSmplPresets();

// SMPL preset menu state
extern uint8_t smplPresetMenuLevel;  // Reuses PRESET_LEVEL_* constants
extern int smplPresetSelection;
extern int smplPresetScrollIndex;
extern int smplPresetSelectedSlot;
extern bool smplPresetSaving;
extern unsigned long smplPresetSaveStartTime;
extern bool smplPresetDeleting;
extern unsigned long smplPresetDeleteStartTime;
extern bool smplPresetFromMainMenu;  // true if entered from MENU_PRESETS

// MIDI menu item indices
#define MIDIMENU_ITEM_COUNT 10
#define MIDIMENU_MCH 0       // MIDI channel (all modes)
#define MIDIMENU_MODE 1      // Device mode: YM/SID/SMPL
#define MIDIMENU_ROUTE 2     // MIDI channel routing
#define MIDIMENU_VIZ 3       // Visualization mode
#define MIDIMENU_USB 4       // USB mode (MIDI/Serial) - requires reboot
#define MIDIMENU_BRT 5       // Display brightness (0-10)
#define MIDIMENU_VEL 6       // Velocity curve (0-10)
#define MIDIMENU_POTS 7      // Pot defaults submenu
#define MIDIMENU_CLK 8       // MIDI clock sync toggle
#define MIDIMENU_BACK 9

// MIDI menu state
extern int midiMenuSelection;
extern bool midiEditing;
extern int midiTempValue;

// Route submenu state (2-step: select FROM channel, then select TO channel)
#define ROUTE_EDIT_NONE 0    // Not editing, just showing summary
#define ROUTE_EDIT_FROM 1    // Selecting which incoming channel to configure
#define ROUTE_EDIT_TO 2      // Selecting target channel for the selected FROM channel
extern uint8_t routeEditLevel;
extern uint8_t routeFromChannel;   // Currently selected FROM channel (0-15)
extern uint8_t routeTempTo;        // Temporary TO value while editing

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialize display
bool displayInit();

// Update visualization display
void updateDisplay();
void updateDisplayScope();   // Oscilloscope visualization
void updateDisplayMatrix();         // 3x3 grid oscilloscope (all 9 voices)
void updateDisplayChannelMatrix();  // 3x3 grid oscilloscope (MIDI channels 1-9)
void updateDisplayDrums();          // Drum sample waveform visualization
void updateDisplayStatic();         // TV static / poltergeist visualization

// Update main menu display
void updateMenu();

// Update settings menu display (main level)
void updateSettingsMenu();

// Update SMPL settings screen (when sampleModeGlobal active)
void updateSampleSettingsMenu();

// Update effect submenus
void updateVibratoSubmenu();
void updateEnvelopeSubmenu();
void updatePitchSubmenu();
void updateGlideSubmenu();
void updateTremoloSubmenu();
void updatePitchEnvSubmenu();
void updateSidSubmenu();
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

// --- Helper functions (shared across display files) ---

// Preset list helpers
uint8_t getLoadListPresetIndex(int position);
int getLoadListCount();
uint8_t getDeleteListUserSlot(int position);
int getDeleteListCount();

// Settings helpers
int getFirstVoiceForScope();
const char* getScopeName(int scope);
int maskToLinkOption(uint8_t mask);
uint8_t linkOptionToMask(int option);
int getSettingsValue(int item);
int getSettingsMax(int item);
void getSettingsValueStr(int item, int value, char* buf);
const char* getSettingsLabel(int item);
bool isSettingsItemVisible(int item);
int nextVisibleSettingsItem(int current, int delta);
void getChipScopeVoices(uint8_t &start, uint8_t &end);
void setTargetForCommands();
void applySettingsValue(int item, int value);

// Submenu helpers (get/max/valueStr/label/apply for each submenu)
int getVibratoValue(int item);
int getVibratoMax(int item);
void getVibratoValueStr(int item, int value, char* buf);
const char* getVibratoLabel(int item);
void applyVibratoValue(int item, int value);

int getEnvelopeValue(int item);
int getEnvelopeMax(int item);
void getEnvelopeValueStr(int item, int value, char* buf);
const char* getEnvelopeLabel(int item);
void applyEnvelopeValue(int item, int value);

int getSidValue(int item);
int getSidMax(int item);
void getSidValueStr(int item, int value, char* buf);
const char* getSidLabel(int item);
void applySidValue(int item, int value);

int getPitchValue(int item);
int getPitchMax(int item);
void getPitchValueStr(int item, int value, char* buf);
const char* getPitchLabel(int item);
void applyPitchValue(int item, int value);

int getGlideValue(int item);
int getGlideMax(int item);
void getGlideValueStr(int item, int value, char* buf);
const char* getGlideLabel(int item);
void applyGlideValue(int item, int value);

int getTremoloValue(int item);
int getTremoloMax(int item);
void getTremoloValueStr(int item, int value, char* buf);
const char* getTremoloLabel(int item);
void applyTremoloValue(int item, int value);

int getPitchEnvValue(int item);
int getPitchEnvMax(int item);
void getPitchEnvValueStr(int item, int value, char* buf);
const char* getPitchEnvLabel(int item);
void applyPitchEnvValue(int item, int value);

// FX submenu helpers
int getVisibleFXItemCount();
const char* getFXLabel(int sel);
int getFXValue(int sel);
int getFXMax(int sel);
void getFXValueStr(int sel, int val, char* buf);
void applyFXValue(int sel, int val);

// SMPL settings helpers
int getSampleSettingsValue(int item);

// Name entry helpers (shared between presets menu and input)
extern const char nameChars[];
extern const int nameCharCount;
int getNameCharIndex(char c);
