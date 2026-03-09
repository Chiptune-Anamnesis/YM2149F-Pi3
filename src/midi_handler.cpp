#include "midi_handler.h"
#include "voice_manager.h"
#include "effects.h"
#include "sid_mode.h"
#include "settings.h"
#include "sample_player.h"
#include "fx_chip.h"
#include "YM2149.h"
#include "preset.h"

// External YM2149 instance
extern YM2149 ym;

// ============================================================================
// MIDI STATE
// ============================================================================

Adafruit_USBD_MIDI usb_midi;

// Serial MIDI parser state
enum SerState { WAIT_STATUS, WAIT_D1, WAIT_D2 };
static SerState serState = WAIT_STATUS;
static uint8_t serStatus, serD1;

// ============================================================================
// MIDI INITIALIZATION
// ============================================================================

void midiInit() {
  // Read USB mode from flash (before presetInit runs)
  uint8_t mode = readUsbModeFromFlash();
  usbMode = mode;  // Store in global so display shows correct value

  if (mode == USB_MODE_SERIAL) {
    // USB Serial mode - for debug or YMPlayer
    // Don't initialize TinyUSB MIDI, just serial will be available
    // Note: Serial.begin() is called elsewhere if USE_YMPLAYER_SERIAL
    return;
  }

  // USB MIDI mode (default)
  // Initialize TinyUSB device with descriptors
  TinyUSBDevice.setID(0x239A, 0x8014);  // Adafruit VID/PID
  TinyUSBDevice.setManufacturerDescriptor("HobbyChop");
  TinyUSBDevice.setProductDescriptor("YM2149Fx3 MIDI");
  TinyUSBDevice.setSerialDescriptor("123456");

  if (!TinyUSBDevice.begin(0)) {
    // USB init failed - continue anyway as TRS MIDI still works
  }

  usb_midi.begin();
}

// ============================================================================
// PITCH BEND
// ============================================================================

void pitchBend(uint8_t ch, uint8_t lsb, uint8_t msb) {
  if (ch >= 9) return;
  int val = (msb << 7) | lsb;
  pitchBendSemis[ch] = ((float)val - 8192) / 8192 * 2.0f;
  updatePitchMod(ch);
}

// ============================================================================
// CC & MESSAGE HANDLING
// ============================================================================

void handleMidiMsg(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t cmd = status & 0xF0;
  uint8_t incomingCh = status & 0x0F;

  // Apply MIDI channel routing: remap incoming channel to internal channel
  // If midiChannelRemap[incomingCh] != MIDI_REMAP_NONE, use remapped channel
  // Otherwise, use the incoming channel as-is
  uint8_t remapped = midiChannelRemap[incomingCh];
  uint8_t ch = (remapped != MIDI_REMAP_NONE && remapped < 16)
               ? remapped
               : incomingCh;

  // Check if this channel should be processed (OFF disables the channel entirely)
  // Note: drum channel check uses INCOMING channel (drums are typically on ch 10)
  // but synth channel filter and routing uses the REMAPPED channel
  bool isDrumChannel = (midiDrumChannel != MIDI_CHANNEL_OFF) && (incomingCh == midiDrumChannel);
  bool isSynthChannel = (midiSynthChannel != MIDI_CHANNEL_OFF) &&
                        ((midiSynthChannel == MIDI_CHANNEL_OMNI) || (ch == midiSynthChannel));

  if (cmd == 0x90 && d2 > 0) {
    // Check drum channel first (takes priority)
    if (isDrumChannel) {
      sampleTrigger(d1, d2);
      return;
    }
    // Then check synth channel
    if (isSynthChannel) {
      noteOn(ch, d1, d2);
    }
  }
  else if (cmd == 0x80 || (cmd == 0x90 && d2 == 0)) {
    if (isSynthChannel) {
      noteOff(ch, d1);
    }
  }
  else if (cmd == 0xB0 && isSynthChannel) {
    switch (d1) {
      case 1:   modWheel[ch] = d2; updatePitchMod(ch); break;
      case 4:   cc4Shape[ch] = d2; break;
      case 5: {
        float norm = d2 / 127.0f;
        float curve = norm * norm;
        portamentoSpeed[ch] = PORTA_MIN + (PORTA_MAX - PORTA_MIN) * curve;
        updatePitchMod(ch);
      } break;
      case 7:
      case 11:  expressionVal[ch] = d2; updatePitchMod(ch); break;
      case 9:   pitchEnvAmt[ch] = (d2 / 127.0f) * 2.0f;
                pitchEnvIncrement[ch] = 1.0f / (200.0f / 5.0f); break;
      case 10:  pitchEnvShape[ch] = d2; break;
      case 64:  sustainOn[ch] = (d2 >= 64);
                if (!sustainOn[ch]) {
                  uint8_t startChip, endChip;
                  if (sidModeGlobal) {
                    // In SID mode, search chips 1-2
                    startChip = 1;
                    endChip = 3;
                  } else if (polyMode == 1 || unisonMode) {
                    startChip = 0;
                    endChip = 3;
                  } else {
                    startChip = midiToChip[ch];
                    endChip = startChip + 1;
                  }
                  for (uint8_t chip = startChip; chip < endChip; chip++) {
                    for (uint8_t v = 0; v < 3; v++) {
                      if (pendingRelease[chip][v] && voiceActive[chip][v] && voiceChan[chip][v] == ch) {
                        voiceActive[chip][v] = false;
                        stopVoice(chip, v);
                        pendingRelease[chip][v] = false;
                      }
                    }
                  }
                }
                break;
      case 65: {
                bool on = (d2 >= 64);
                if (!on) {
                  uint8_t startChip, endChip;
                  if (sidModeGlobal) {
                    // In SID mode, search chips 1-2
                    startChip = 1;
                    endChip = 3;
                  } else if (polyMode == 1 || unisonMode) {
                    startChip = 0;
                    endChip = 3;
                  } else {
                    startChip = midiToChip[ch];
                    endChip = startChip + 1;
                  }
                  for (uint8_t chip = startChip; chip < endChip; chip++) {
                    for (uint8_t v = 0; v < 3; v++) {
                      if (voiceActive[chip][v] && voiceChan[chip][v] == ch) {
                        curPeriod[chip][v] = 0;
                      }
                    }
                  }
                }
                portamentoOn[ch] = on;
                updatePitchMod(ch);
              }
              break;
      case 68:  laserMode[ch] = (d2 >= 64); break;
      case 69:  laserAmt[ch] = d2 / 127.0f; break;
      case 70:  // Polyphony mode
              if (d2 >= 85) polyMode = 2;
              else if (d2 >= 43) polyMode = 1;
              else polyMode = 0;
              break;
      case 71:  // SID toggle - now controls global SID mode
              {
                bool enableSid = (d2 >= 64);
                if (enableSid != sidModeGlobal) {
                  // Full reset when switching modes to avoid artifacts
                  allNotesOffPanic();
                  settingsInit();      // Reset all voice settings to defaults
                  effectsInit();       // Reset all effects state
                  fxSetEnabled(false); // Disable FX
                  polyMode = 1;        // Reset to full poly

                  // Set flag BEFORE init/stop so timer callback sees correct mode
                  sidModeGlobal = enableSid;
                  if (enableSid) {
                    sidModeInit();     // Initialize global SID mode
                  } else {
                    sidModeStop();     // Stop global SID mode
                  }
                }
              }
              break;
      case 72:  unisonMode = (d2 >= 64); break;
      case 73:  unisonDetuneCents = (d2 / 127.0f) * 50.0f;
              updatePitchMod(ch);
              break;
      case 74:  // SID duty/envelope freq
              if (sidPWM) {
                const uint8_t dutyValues[] = {2,3,4,5,6,7,8,12,16,32};
                uint8_t idx = (d2 * 9) / 127;
                if (idx > 9) idx = 9;
                sidDuty = dutyValues[idx];
                sidGeneratePattern();
                for (uint8_t c = 0; c < 3; c++) {
                  for (uint8_t v = 0; v < 3; v++) {
                    if (sidVoiceOn[c][v] && sidPeriod[c][v] > 0) {
                      sidPhaseInc[c][v] = calcPhaseInc(sidPeriod[c][v]);
                    }
                    sidPhase[c][v] = 0;
                  }
                }
              } else {
                sidEnvFreqFine = 127 - d2;
                for (uint8_t c = 0; c < 3; c++) {
                  ymSafeWrite(c, 0x0B, sidEnvFreqFine);
                  ymSafeWrite(c, 0x0C, sidEnvFreqCoarse);
                }
              }
              break;
      case 75:  // SID waveform/envelope shape
              if (sidPWM) {
                if (d2 < 64) sidWaveType = 0;  // Square
                else sidWaveType = 3;           // Pulse
                sidGeneratePattern();
                for (uint8_t c = 0; c < 3; c++)
                  for (uint8_t v = 0; v < 3; v++)
                    sidPhase[c][v] = 0;
              } else {
                if (d2 < 32) sidEnvShape = 0b1000;
                else if (d2 < 64) sidEnvShape = 0b1010;
                else if (d2 < 96) sidEnvShape = 0b1100;
                else sidEnvShape = 0b1110;
                for (uint8_t c = 0; c < 3; c++) {
                  ymSafeWrite(c, 0x0D, sidEnvShape);
                }
              }
              break;
      case 76:  vibRate[ch] = (d2 / 127.0f) * 10.0f; updatePitchMod(ch); break;
      case 77:  vibRangeSemi[ch] = (d2 / 127.0f) * 2.0f; updatePitchMod(ch); break;
      case 78:  // SID PWM detune
              if (sidPWM) {
                sidDetune = (int8_t)(d2 - 64) / 4;
                for (uint8_t c = 0; c < 3; c++) {
                  for (uint8_t v = 0; v < 3; v++) {
                    if (sidVoiceOn[c][v] && sidPeriod[c][v] > 0) {
                      sidPhaseInc[c][v] = calcPhaseInc(sidPeriod[c][v]);
                    }
                  }
                }
              }
              break;
      case 85:  vibDelayMs[ch] = map(d2, 0, 127, 0, 2000); break;
      case 120: allNotesOffChannel(ch); fxPanic(); break;
      case 121: resetAllControllers(ch); fxPanic(); break;
      case 123:
              if (sustainOn[ch]) {
                uint8_t startChip, endChip;
                if (sidModeGlobal) {
                  // In SID mode, search chips 1-2
                  startChip = 1;
                  endChip = 3;
                } else if (polyMode == 1 || unisonMode) {
                  startChip = 0;
                  endChip = 3;
                } else {
                  startChip = midiToChip[ch];
                  endChip = startChip + 1;
                }
                for (uint8_t chip = startChip; chip < endChip; chip++) {
                  for (uint8_t v = 0; v < 3; v++) {
                    if (voiceActive[chip][v] && voiceChan[chip][v] == ch) {
                      pendingRelease[chip][v] = true;
                    }
                  }
                }
              } else {
                allNotesOffChannel(ch);
              }
              fxPanic();  // Always clear FX/arp state on All Notes Off
              break;
    }
  }
  else if (cmd == 0xE0 && isSynthChannel) {
    pitchBend(ch, d1, d2);
  }
}

// ============================================================================
// SERIAL MIDI PARSER
// ============================================================================

void parseSerialMidi(uint8_t b) {
  // Real-time messages
  if (b >= 0xF8) {
    switch (b) {
      case 0xF8:  // MIDI Clock tick
        fxClockTick();
        break;
      case 0xFA:  // Start
        midiClockTick = 0;
        midiClockRunning = true;
        for (uint8_t ch = 0; ch < 9; ch++) {
          resetAllControllers(ch);
        }
        break;
      case 0xFB:  // Continue
        midiClockRunning = true;
        for (uint8_t ch = 0; ch < 9; ch++) {
          resetAllControllers(ch);
        }
        break;
      case 0xFC:  // Stop
        midiClockRunning = false;
        allNotesOffPanic();
        fxPanic();  // Clear FX/arp state
        for (uint8_t ch = 0; ch < 9; ch++) {
          resetAllControllers(ch);
        }
        break;
    }
    return;
  }

  // System-common (skip)
  if (b >= 0xF0) {
    serState = WAIT_STATUS;
    return;
  }

  // Channel status bytes
  if (b & 0x80) {
    serStatus = b;
    serState = WAIT_D1;
  }
  else if (serState == WAIT_D1) {
    serD1 = b;
    serState = WAIT_D2;
  }
  else if (serState == WAIT_D2) {
    handleMidiMsg(serStatus, serD1, b);
    serState = WAIT_D1;
  }
}

// ============================================================================
// USB MIDI PROCESSING
// ============================================================================

void processUsbMidi() {
  // Skip USB MIDI processing if in serial mode
  if (usbMode == USB_MODE_SERIAL) return;

  while (usb_midi.available()) {
    uint8_t packet[4];
    usb_midi.readPacket(packet);

    uint8_t code = packet[0] & 0x0F;
    uint8_t incomingCh = packet[1] & 0x0F;

    // Apply MIDI channel routing: remap incoming channel to internal channel
    uint8_t remappedUsb = midiChannelRemap[incomingCh];  // Read once to avoid TOCTOU
    uint8_t channel = (remappedUsb != MIDI_REMAP_NONE && remappedUsb < 16)
                      ? remappedUsb
                      : incomingCh;

    // Check if this channel should be processed (OFF disables the channel entirely)
    // Note: drum channel check uses INCOMING channel, synth uses REMAPPED channel
    bool isDrumChannel = (midiDrumChannel != MIDI_CHANNEL_OFF) && (incomingCh == midiDrumChannel);
    bool isSynthChannel = (midiSynthChannel != MIDI_CHANNEL_OFF) &&
                          ((midiSynthChannel == MIDI_CHANNEL_OMNI) || (channel == midiSynthChannel));

    switch (code) {
      case 0x09:  // Note On
        if (packet[3] > 0) {
          // Check drum channel first (takes priority)
          if (isDrumChannel) {
            sampleTrigger(packet[2], packet[3]);
          } else if (isSynthChannel) {
            noteOn(channel, packet[2], packet[3]);
          }
        } else {
          if (isSynthChannel) {
            noteOff(channel, packet[2]);
          }
        }
        break;
      case 0x08:  // Note Off
        if (isSynthChannel) {
          noteOff(channel, packet[2]);
        }
        break;
      case 0x0B:  // Control Change
        handleMidiMsg(packet[1], packet[2], packet[3]);
        break;
      case 0x0E:  // Pitch Bend
        if (isSynthChannel) {
          pitchBend(channel, packet[2], packet[3]);
        }
        break;
      case 0x0F:  // Single Byte (System Real-Time)
        // packet[1] contains the actual message byte
        switch (packet[1]) {
          case 0xF8:  // MIDI Clock tick
            fxClockTick();
            break;
          case 0xFA:  // Start
            midiClockTick = 0;
            midiClockRunning = true;
            for (uint8_t ch = 0; ch < 9; ch++) {
              resetAllControllers(ch);
            }
            break;
          case 0xFB:  // Continue
            midiClockRunning = true;
            for (uint8_t ch = 0; ch < 9; ch++) {
              resetAllControllers(ch);
            }
            break;
          case 0xFC:  // Stop
            midiClockRunning = false;
            allNotesOffPanic();
            fxPanic();  // Clear FX/arp state
            for (uint8_t ch = 0; ch < 9; ch++) {
              resetAllControllers(ch);
            }
            break;
        }
        break;
    }
  }
}
