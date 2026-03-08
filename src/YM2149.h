#pragma once
#include <Arduino.h>
#include "hardware/gpio.h"

#define DIRECT_WRITE

// Pico GPIO pin definitions
#define PIN_DATA_BASE   0   // GPIO 0-7 for 8-bit data bus (contiguous)
#define PIN_BC1         8
#define PIN_BDIR        9
#define PIN_SEL_A      10
#define PIN_SEL_B      11
#define PIN_SEL_C      12
#define PIN_ENABLE     13  // Not used - 74HC138 enable hardwired on PCB
#define PIN_LED0       16  // Chip 0 LED (moved from 14 to avoid OLED conflict)
#define PIN_LED1       13  // Chip 1 LED (shares with unused ENABLE)
// #define PIN_LED2       22  // Disabled - GPIO 22 now used for encoder button
#define PIN_MIDI_TX    20
#define PIN_MIDI_RX    21

class YM2149Class {

public:
    static constexpr uint8_t CHIP_LED[2] = {PIN_LED0, PIN_LED1};  // LED2 disabled (GPIO 22 used for encoder button)

    static constexpr float YM_CLOCK_HZ = 2000000.0f;  // Must match actual oscillator!

    static constexpr uint8_t LED_ON = LOW;
    static constexpr uint8_t LED_OFF = HIGH;

    static constexpr uint8_t YM_0 = 0;
    static constexpr uint8_t YM_1 = 1;
    static constexpr uint8_t YM_2 = 2;

    void begin();
    void write(uint8_t chip, uint8_t address, uint8_t value);

#ifdef DIRECT_WRITE
    inline void selectYM(uint8_t chip)
    {
        currentChip = chip;  // Track selected chip for caching
        // Hardware mapping (74HC138 → 74LS04 → 74LS08):
        // Y0 (SEL=0) → YM₂, Y1 (SEL=1) → YM₁, Y2 (SEL=2) → YM₀
        // So chip 0 needs SEL=2, chip 1 needs SEL=1, chip 2 needs SEL=0
        chip = 2 - chip;
        gpio_put(PIN_SEL_A, (chip & 1) ? 1 : 0);
        gpio_put(PIN_SEL_B, (chip & 2) ? 1 : 0);
        gpio_put(PIN_SEL_C, (chip & 4) ? 1 : 0);
    }

    inline void busWrite(uint8_t value)
    {
        // Write each bit individually - slower but reliable
        for (uint8_t i = 0; i < 8; i++) {
            gpio_put(PIN_DATA_BASE + i, (value >> i) & 1);
        }
    }

    inline void writeFast(uint8_t address, uint8_t value)
    {
        // Note: Assumes chip is already selected
        // === ADDRESS PHASE ===
        busWrite(address & 0x0F);
        delayMicroseconds(2);

        // Simultaneous BC1+BDIR assertion
        gpio_set_mask((1 << PIN_BC1) | (1 << PIN_BDIR));
        delayMicroseconds(5);

        gpio_clr_mask((1 << PIN_BC1) | (1 << PIN_BDIR));
        delayMicroseconds(5);

        // === DATA PHASE ===
        busWrite(value);
        delayMicroseconds(2);

        gpio_put(PIN_BDIR, 1);
        delayMicroseconds(5);

        gpio_put(PIN_BDIR, 0);
        delayMicroseconds(2);
    }

#else
    void busWrite(uint8_t val);
#endif

    void setPin(uint8_t chip, uint8_t pin, bool value);
    uint8_t getPin(uint8_t chip, uint8_t pin);

    void setPort(uint8_t chip, bool port, uint8_t value);
    uint8_t getPort(uint8_t chip, bool port);
    void setPortIO(uint8_t chip, bool portA, bool portB);

    void setLED(uint8_t chip, bool state);
    bool getLED(uint8_t chip);

	void setNote(uint8_t chip, uint8_t synth, float value);
	void setFreq(uint8_t chip, uint8_t voice, uint32_t freqHz);
    void setTone(uint8_t chip, uint8_t voice, uint16_t value);
    void setVolume(uint8_t chip, uint8_t voice, uint8_t value);
    void setNoise(uint8_t chip, uint8_t voice, uint8_t value);
    void setEnv(uint8_t chip, uint8_t voice, uint8_t value);
    void setEnvShape(uint8_t chip, uint8_t cont, uint8_t att, uint8_t alt, uint8_t hold);
    void mute(uint8_t chip);

	static const uint8_t REG_A_FREQ = 0x00;
    static const uint8_t REG_B_FREQ = 0x02;
    static const uint8_t REG_C_FREQ = 0x04;
    static const uint8_t REG_NOISE_FREQ = 0x06;
    static const uint8_t REG_MIXER = 0x07;
    static const uint8_t REG_A_LEVEL = 0x08;
    static const uint8_t REG_B_LEVEL = 0x09;
    static const uint8_t REG_C_LEVEL = 0x0A;
    static const uint8_t REG_ENV_FREQ = 0x0B;
    static const uint8_t REG_ENV_SHAPE = 0x0D;
    static const uint8_t REG_DATAPORT_A = 0x0E;
    static const uint8_t REG_DATAPORT_B = 0x0F;

    uint8_t currentChip = 255;

private:
    uint8_t levelValue[3][3] = {{0}};
    uint8_t portAValue[3] = {0};
    uint8_t portBValue[3] = {0};
    uint8_t mixerValue[3] = {0b00111000, 0b00111000, 0b00111000};
    bool ledState[2] = {false, false};  // Only LED0 and LED1 (LED2/GPIO22 used for encoder button)
};

typedef YM2149Class YM2149;
