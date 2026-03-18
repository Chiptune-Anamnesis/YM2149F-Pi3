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
        // SEL_A=GPIO10, SEL_B=GPIO11, SEL_C=GPIO12 — write all 3 atomically
        gpio_put_masked(0x7u << PIN_SEL_A, (uint32_t)chip << PIN_SEL_A);
    }

    inline void busWrite(uint8_t value)
    {
        // Write all 8 data bits simultaneously (GPIO 0-7 contiguous from bit 0)
        gpio_put_masked(0xFF, (uint32_t)value);
    }

    // RAM-resident busy wait - avoids flash XIP cache jitter in timer ISR
    static inline void __attribute__((always_inline)) delayUs_ram(uint32_t us)
    {
        // RP2040 at 133MHz: ~133 cycles per µs
        // Inner loop: sub + bne = 3 cycles on Cortex-M0+
        uint32_t count = us * 44;
        __asm volatile (
            "1: sub %0, #1\n"
            "   bne 1b\n"
            : "+l"(count)
        );
    }

    // Half-microsecond RAM-resident delay (~500ns)
    // Used in writeFast for tighter YM2149 bus timing (1.5× safety margin over datasheet)
    static inline void __attribute__((always_inline)) delayHalfUs_ram()
    {
        uint32_t count = 22;
        __asm volatile (
            "1: sub %0, #1\n"
            "   bne 1b\n"
            : "+l"(count)
        );
    }

    inline void writeFast(uint8_t address, uint8_t value)
    {
        // Note: Assumes chip is already selected
        // YM2149 timing: tAS=300ns, tDW=300ns, tDS=200ns, tDH=50ns
        // 1µs delays are ~3x safety margin over datasheet minimums

        // === ADDRESS PHASE ===
        busWrite(address & 0x0F);
        delayHalfUs_ram();  // tAS: address setup (min 300ns, ~500ns actual)

        gpio_set_mask((1 << PIN_BC1) | (1 << PIN_BDIR));
        delayHalfUs_ram();  // tDW: write pulse width (min 300ns, ~500ns actual)

        gpio_clr_mask((1 << PIN_BC1) | (1 << PIN_BDIR));
        delayHalfUs_ram();  // tAH: address hold (min 80ns, ~500ns actual)

        // === DATA PHASE ===
        busWrite(value);
        delayHalfUs_ram();  // tDS: data setup (min 200ns, ~500ns actual)

        gpio_put(PIN_BDIR, 1);
        delayHalfUs_ram();  // tDW: write pulse width (min 300ns, ~500ns actual)

        gpio_put(PIN_BDIR, 0);
        delayHalfUs_ram();  // tDH: data hold (min 50ns, ~500ns actual)
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
