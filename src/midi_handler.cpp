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

  // Check if this channel matches M.CH setting
  bool isChannel = (midiSynthChannel != MIDI_CHANNEL_OFF) &&
                   ((midiSynthChannel == MIDI_CHANNEL_OMNI) || (ch == midiSynthChannel));

  // SMPL mode: route matching notes to sample trigger, no synth
  if (sampleModeGlobal) {
    if (cmd == 0x90 && d2 > 0 && isChannel) {
      sampleTrigger(d1, d2);
    } else if (cmd == 0xE0 && isChannel) {
      sampleApplyPitchBend(d1, d2);
    }
    return;
  }

  // YM/SID mode: normal synth processing
  bool isSynthChannel = isChannel;

  if (cmd == 0x90 && d2 > 0) {
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
                  } else if (polyMode == 1 || polyMode == 3 || unisonMode) {
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
                  } else if (polyMode == 1 || polyMode == 3 || unisonMode) {
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
      case 70:  // Polyphony mode: 0-31=semi, 32-63=poly, 64-95=mono, 96-127=multi
              if (d2 >= 96) polyMode = 3;
              else if (d2 >= 64) polyMode = 2;
              else if (d2 >= 32) polyMode = 1;
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

      // ================================================================
      // Voice Settings (CC 14-17) — applied to all 9 voices
      // ================================================================
      case 14: {
        int8_t val = (int8_t)map(d2, 0, 127, -50, 50);
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].detuneCents = val;
        break;
      }
      case 15: {
        int8_t val = (int8_t)map(d2, 0, 127, -3, 3);
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].octaveShift = val;
        break;
      }
      case 16: {
        uint8_t val = d2 * 15 / 127;
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].maxVolume = val;
        break;
      }
      case 17: {
        uint8_t val = d2 * 31 / 127;
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].noiseFreq = val;
        for (uint8_t c = 0; c < 3; c++) ym.write(c, 0x06, val);
        break;
      }

      // ================================================================
      // Envelope ADSR (CC 18-21) — applied to all 9 voices
      // ================================================================
      case 18: { for (uint8_t v = 0; v < 9; v++) voiceSettings[v].envAttack = d2; break; }
      case 19: { for (uint8_t v = 0; v < 9; v++) voiceSettings[v].envDecay = d2; break; }
      case 20: { for (uint8_t v = 0; v < 9; v++) voiceSettings[v].envSustain = d2; break; }
      case 21: { for (uint8_t v = 0; v < 9; v++) voiceSettings[v].envRelease = d2; break; }

      // ================================================================
      // Tremolo (CC 22-24) — applied to all 9 voices
      // ================================================================
      case 22: { for (uint8_t v = 0; v < 9; v++) voiceSettings[v].tremoloOn = d2; break; }
      case 23: {
        uint8_t val = map(d2, 0, 127, 10, 150);
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].tremoloRate = val;
        break;
      }
      case 24: {
        uint8_t val = d2 * 100 / 127;
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].tremoloDepth = val;
        break;
      }

      // ================================================================
      // Pitch Envelope (CC 25-27) — applied to all 9 voices
      // ================================================================
      case 25: {
        uint8_t val = d2 * 24 / 127;
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].pitchEnvAmt = val;
        break;
      }
      case 26: { for (uint8_t v = 0; v < 9; v++) voiceSettings[v].pitchEnvTime = d2; break; }
      case 27: {
        uint8_t val = d2 >= 64 ? 1 : 0;
        for (uint8_t v = 0; v < 9; v++) voiceSettings[v].pitchEnvDir = val;
        break;
      }

      // ================================================================
      // FX Master (CC 28-30)
      // ================================================================
      case 28: fxSetEnabled(d2 >= 64); break;
      case 29: fxPanic(); fxType = d2 * 7 / 127; break;
      case 30: fxRouting = d2 * 8 / 127; break;

      // ================================================================
      // Echo (CC 33-36)
      // ================================================================
      case 33: echoDelayMs = map(d2, 0, 127, 50, 2000); break;
      case 34: echoRepeats = map(d2, 0, 127, 1, 10); break;
      case 35: echoDecay = map(d2, 0, 127, 1, 15); break;
      case 36: echoVolume = map(d2, 0, 127, 1, 15); break;

      // ================================================================
      // Arp (CC 37-40)
      // ================================================================
      case 37: arpSpeedMs = map(d2, 0, 127, 50, 500); break;
      case 38: arpPattern = d2 * 3 / 127; break;
      case 39: arpVolume = map(d2, 0, 127, 1, 15); break;
      case 40: arpOctave = (int8_t)map(d2, 0, 127, -2, 2); break;

      // ================================================================
      // BitCrush (CC 41-43)
      // ================================================================
      case 41: bitCrushBits = map(d2, 0, 127, 1, 4); break;
      case 42: bitCrushRate = map(d2, 0, 127, 1, 10); break;
      case 43: bitCrushVolume = map(d2, 0, 127, 1, 15); break;

      // ================================================================
      // Reverb (CC 44-48)
      // ================================================================
      case 44: reverbTaps = map(d2, 0, 127, 2, 6); break;
      case 45: reverbSpacing = map(d2, 0, 127, 20, 100); break;
      case 46: reverbDecay = map(d2, 0, 127, 1, 8); break;
      case 47: reverbDetune = (int8_t)map(d2, 0, 127, -5, 5); break;
      case 48: reverbVolume = map(d2, 0, 127, 1, 15); break;

      // ================================================================
      // Chorus (CC 49-52)
      // ================================================================
      case 49: chorusDetune1 = (int8_t)map(d2, 0, 127, -50, 50); break;
      case 50: chorusDetune2 = (int8_t)map(d2, 0, 127, -50, 50); break;
      case 51: chorusVolume = map(d2, 0, 127, 1, 15); break;
      case 52: chorusRate = d2 * 80 / 127; break;

      // ================================================================
      // Harmonizer (CC 53-55)
      // ================================================================
      case 53: harmChord = d2 * 11 / 127; break;
      case 54: harmVolume = map(d2, 0, 127, 1, 15); break;
      case 55: harmOctave = (int8_t)map(d2, 0, 127, -2, 2); break;

      // ================================================================
      // Gate (CC 56-60)
      // ================================================================
      case 56: gateRateMs = map(d2, 0, 127, 30, 500); break;
      case 57: gatePattern = d2 * 3 / 127; break;
      case 58: gateVolume = map(d2, 0, 127, 1, 15); break;
      case 59: gateDuty = map(d2, 0, 127, 1, 7); break;
      case 60: gateSeed = d2 * 15 / 127; break;

      // ================================================================
      // Sample Global (CC 102-105) — only in sample mode
      // ================================================================
      case 102:
        if (sampleModeGlobal) sampleVolume = map(d2, 0, 127, 1, 15);
        break;
      case 103:
        if (sampleModeGlobal) sampleMode = d2 * 3 / 127;
        break;
      case 104:
        if (sampleModeGlobal) {
          sampleSection = d2 * 2 / 127;
          if (sampleSection >= SAMPLE_SECTION_COUNT) sampleSection = SAMPLE_SECTION_COUNT - 1;
          uint8_t cnt = getSectionSampleCount(sampleSection);
          if (sampleSelect >= cnt) sampleSelect = 0;
        }
        break;
      case 105:
        if (sampleModeGlobal) {
          uint8_t cnt = getSectionSampleCount(sampleSection);
          sampleSelect = (cnt > 1) ? d2 * (cnt - 1) / 127 : 0;
        }
        break;

      // ================================================================
      // Link (CC 106-107)
      // ================================================================
      case 106: applyLinkMode(d2 * 3 / 127); break;
      case 107: applyVoiceLinkMask(d2 * 7 / 127); break;

      // ================================================================
      // Per-Sample Settings (CC 108-111) — targets current sample
      // ================================================================
      case 108:
        if (sampleModeGlobal) {
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          samplePitchArr[flat] = (int8_t)map(d2, 0, 127, -12, 12);
        }
        break;
      case 109:
        if (sampleModeGlobal) {
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          sampleOctaveArr[flat] = (int8_t)map(d2, 0, 127, -3, 3);
        }
        break;
      case 110:
        if (sampleModeGlobal) {
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          sampleLengthArr[flat] = d2 < 1 ? 1 : d2;
        }
        break;
      case 111:
        if (sampleModeGlobal) {
          uint8_t flat = sampleFlatIndex(sampleSection, sampleSelect);
          sampleCrushArr[flat] = d2 * 7 / 127;
        }
        break;

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

    // Check if this channel matches M.CH setting
    bool isChannel = (midiSynthChannel != MIDI_CHANNEL_OFF) &&
                     ((midiSynthChannel == MIDI_CHANNEL_OMNI) || (channel == midiSynthChannel));

    // SMPL mode: route matching notes to sample trigger
    if (sampleModeGlobal) {
      if (code == 0x09 && packet[3] > 0 && isChannel) {
        sampleTrigger(packet[2], packet[3]);
      } else if (code == 0x0E && isChannel) {
        sampleApplyPitchBend(packet[2], packet[3]);
      }
      // Still process system real-time messages below
      if (code != 0x0F) continue;
    }

    bool isSynthChannel = isChannel;

    switch (code) {
      case 0x09:  // Note On
        if (packet[3] > 0) {
          if (isSynthChannel) {
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
