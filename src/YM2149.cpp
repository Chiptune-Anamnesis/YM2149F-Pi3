#include "YM2149.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

// Run timing-critical functions from RAM to avoid Flash bus contention
#define RAM_FUNC __attribute__((section(".time_critical")))

#ifdef DIRECT_WRITE
void YM2149Class::begin()
{
    // Initialize data bus (GPIO 0-7) as outputs, all LOW
    for (int i = 0; i < 8; i++) {
        gpio_init(PIN_DATA_BASE + i);
        gpio_set_dir(PIN_DATA_BASE + i, GPIO_OUT);
        gpio_set_drive_strength(PIN_DATA_BASE + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_put(PIN_DATA_BASE + i, 0);
    }

    // Initialize control pins as outputs - start with bus INACTIVE
    // Use maximum drive strength to help 3.3V reach 74HC138's VIH threshold at 5V
    gpio_init(PIN_BC1);
    gpio_set_dir(PIN_BC1, GPIO_OUT);
    gpio_set_drive_strength(PIN_BC1, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(PIN_BC1, 0);

    gpio_init(PIN_BDIR);
    gpio_set_dir(PIN_BDIR, GPIO_OUT);
    gpio_set_drive_strength(PIN_BDIR, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(PIN_BDIR, 0);

    // Chip select pins - CRITICAL: SEL_B needs HIGH for chip 0
    // Maximum drive strength helps 3.3V approach 74HC138's 3.5V VIH threshold
    gpio_init(PIN_SEL_A);
    gpio_set_dir(PIN_SEL_A, GPIO_OUT);
    gpio_set_drive_strength(PIN_SEL_A, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(PIN_SEL_A, 0);

    gpio_init(PIN_SEL_B);
    gpio_set_dir(PIN_SEL_B, GPIO_OUT);
    gpio_set_drive_strength(PIN_SEL_B, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(PIN_SEL_B, 0);

    gpio_init(PIN_SEL_C);
    gpio_set_dir(PIN_SEL_C, GPIO_OUT);
    gpio_set_drive_strength(PIN_SEL_C, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(PIN_SEL_C, 0);

    // NOTE: PIN_ENABLE not used - 74HC138 enable is hardwired on PCB

    // Initialize LED pins as outputs (active LOW) - only LED0 and LED1 (LED2/GPIO22 used for encoder button)
    for (int i = 0; i < 2; i++) {
        gpio_init(CHIP_LED[i]);
        gpio_set_dir(CHIP_LED[i], GPIO_OUT);
        gpio_put(CHIP_LED[i], 1);  // OFF (active LOW)
    }

    delay(10);  // Let everything settle

    // Reset internal state
    for (uint8_t c = 0; c < 3; c++) {
        mixerValue[c] = 0b00111000;
        portAValue[c] = 0;
        portBValue[c] = 0;
        for (uint8_t v = 0; v < 3; v++) {
            levelValue[c][v] = 0;
        }
    }
}

RAM_FUNC void YM2149Class::write(uint8_t chip, uint8_t reg, uint8_t val)
{
    // Skip chip-select + settling delay if already on this chip
    if (chip != currentChip) {
        selectYM(chip);

        // Chip 0 requires SEL_B=HIGH which is marginal with 3.3V driving 5V logic
        // Give it extra settling time for capacitive charging
        if (chip == 0) {
            delayMicroseconds(500);  // 500µs for chip 0 (needs SEL_B to reach HIGH threshold)
        } else {
            delayMicroseconds(100);  // 100µs for chips 1,2 (use SEL_B=LOW which is reliable)
        }
    }

    // ADDRESS PHASE: BC1=1, BDIR=1 latches register address
    busWrite(reg);
    delayMicroseconds(2);
    gpio_put(PIN_BC1, 1);
    gpio_put(PIN_BDIR, 1);
    delayMicroseconds(5);
    gpio_put(PIN_BC1, 0);
    gpio_put(PIN_BDIR, 0);
    delayMicroseconds(2);

    // DATA PHASE: BC1=0, BDIR=1 writes data to register
    busWrite(val);
    delayMicroseconds(2);
    gpio_put(PIN_BDIR, 1);
    delayMicroseconds(5);
    gpio_put(PIN_BDIR, 0);
    delayMicroseconds(2);
}

#else
void YM2149Class::begin()
{
    // Initialize data bus (GPIO 0-7) as outputs
    for (int i = 0; i < 8; i++) {
        gpio_init(PIN_DATA_BASE + i);
        gpio_set_dir(PIN_DATA_BASE + i, GPIO_OUT);
    }

    gpio_init(PIN_BC1);
    gpio_set_dir(PIN_BC1, GPIO_OUT);
    gpio_init(PIN_BDIR);
    gpio_set_dir(PIN_BDIR, GPIO_OUT);
    gpio_init(PIN_SEL_A);
    gpio_set_dir(PIN_SEL_A, GPIO_OUT);
    gpio_init(PIN_SEL_B);
    gpio_set_dir(PIN_SEL_B, GPIO_OUT);
    gpio_init(PIN_SEL_C);
    gpio_set_dir(PIN_SEL_C, GPIO_OUT);

    // NOTE: PIN_ENABLE not used - 74HC138 enable is hardwired on PCB

    // Initialize LED pins - only LED0 and LED1 (LED2/GPIO22 used for encoder button)
    for (int i = 0; i < 2; ++i) {
        gpio_init(CHIP_LED[i]);
        gpio_set_dir(CHIP_LED[i], GPIO_OUT);
        gpio_put(CHIP_LED[i], 1);
    }

    gpio_put(PIN_BC1, 0);
    gpio_put(PIN_BDIR, 0);
    gpio_put(PIN_SEL_A, 0);
    gpio_put(PIN_SEL_B, 0);
    gpio_put(PIN_SEL_C, 0);
}

void YM2149Class::selectYM(uint8_t chip)
{
    // Hardware mapping (74HC138 → 74LS04 → 74LS08):
    // Y0 (SEL=0) → YM₂, Y1 (SEL=1) → YM₁, Y2 (SEL=2) → YM₀
    chip = 2 - chip;
    gpio_put(PIN_SEL_A, chip & 1);
    gpio_put(PIN_SEL_B, (chip >> 1) & 1);
    gpio_put(PIN_SEL_C, (chip >> 2) & 1);
}

void YM2149Class::busWrite(uint8_t val)
{
    for (uint8_t i = 0; i < 8; ++i)
        gpio_put(PIN_DATA_BASE + i, (val >> i) & 1);
}

RAM_FUNC void YM2149Class::write(uint8_t chip, uint8_t reg, uint8_t val)
{
    // Skip chip-select + settling delay if already on this chip
    if (chip != currentChip) {
        selectYM(chip);

        // Chip 0 requires SEL_B=HIGH which is marginal with 3.3V driving 5V logic
        // Give it extra settling time for capacitive charging
        if (chip == 0) {
            delayMicroseconds(500);  // 500µs for chip 0 (needs SEL_B to reach HIGH threshold)
        } else {
            delayMicroseconds(100);  // 100µs for chips 1,2 (use SEL_B=LOW which is reliable)
        }
    }

    // ADDRESS PHASE: BC1=1, BDIR=1 latches register address
    busWrite(reg);
    delayMicroseconds(2);
    gpio_put(PIN_BC1, 1);
    gpio_put(PIN_BDIR, 1);
    delayMicroseconds(5);
    gpio_put(PIN_BC1, 0);
    gpio_put(PIN_BDIR, 0);
    delayMicroseconds(2);

    // DATA PHASE: BC1=0, BDIR=1 writes data to register
    busWrite(val);
    delayMicroseconds(2);
    gpio_put(PIN_BDIR, 1);
    delayMicroseconds(5);
    gpio_put(PIN_BDIR, 0);
    delayMicroseconds(2);
}

#endif

void YM2149Class::setPin(uint8_t chip, uint8_t pin, bool value)
{
    pin &= 0x0F;
    uint8_t mask = 1 << (pin & 0x07);

    if (pin >= 8) {
        // Port B
        if (value)
            portBValue[chip] |= mask;
        else
            portBValue[chip] &= ~mask;

        write(chip, REG_DATAPORT_B, portBValue[chip]);
    } else {
        // Port A
        if (value)
            portAValue[chip] |= mask;
        else
            portAValue[chip] &= ~mask;

        write(chip, REG_DATAPORT_A, portAValue[chip]);
    }
}

uint8_t YM2149Class::getPin(uint8_t chip, uint8_t pin)
{
    pin &= 0x0F;

    if (pin >= 8) {
        // Port B
        return (portBValue[chip] >> (pin - 8)) & 0x01;
    } else {
        // Port A
        return (portAValue[chip] >> pin) & 0x01;
    }
}

void YM2149Class::setPort(uint8_t chip, bool port, uint8_t value)
{
    if (port) {
        portBValue[chip] = value;
        write(chip, REG_DATAPORT_B, value);
    } else {
        portAValue[chip] = value;
        write(chip, REG_DATAPORT_A, value);
    }
}

uint8_t YM2149Class::getPort(uint8_t chip, bool port)
{
    return port ? portBValue[chip] : portAValue[chip];
}

void YM2149Class::setPortIO(uint8_t chip, bool portAOut, bool portBOut)
{
    // Bits 6 (port A) and 7 (port B) control I/O mode in the mixer register
    uint8_t portSettings = ((portBOut ? 1 : 0) << 7) | ((portAOut ? 1 : 0) << 6);
    mixerValue[chip] = (mixerValue[chip] & 0b00111111) | portSettings;
    write(chip, REG_MIXER, mixerValue[chip]);
}

void YM2149Class::setLED(uint8_t chip, bool state)
{
    if (chip > 1) return;  // Only LED0 and LED1 available (LED2/GPIO22 used for encoder button)
    ledState[chip] = state;
    gpio_put(CHIP_LED[chip], state ? 1 : 0);
}

bool YM2149Class::getLED(uint8_t chip)
{
    return ledState[chip];
}

void YM2149Class::setNote(uint8_t chip, uint8_t voice, float midiNote)
{
    uint16_t freqVal;

    if (voice != 3) {
        // Convert MIDI note to frequency
        float freq = 440.0f * powf(2.0f, (midiNote - 69.0f) / 12.0f);

        if (voice == 4) {
            // Envelope frequency: 500kHz / (256 * freq)
            freqVal = static_cast<uint16_t>((YM_CLOCK_HZ / (256.0f * freq)) + 0.5f);
        } else {
            // Tone channels: 500kHz / (16 * freq)
            freqVal = static_cast<uint16_t>((YM_CLOCK_HZ / (16.0f * freq)) + 0.5f);
        }
    } else {
        // Noise: approximate mapping
        freqVal = constrain(static_cast<uint16_t>(31 - midiNote / 4), 0, 31);
    }

    setTone(chip, voice, freqVal);
}

void YM2149Class::setFreq(uint8_t chip, uint8_t voice, uint32_t freqHz)
{
    if (freqHz == 0) return; // avoid divide-by-zero

    uint16_t divisor = (voice == 4) ? 256 : 16; // 256 for envelope, 16 for tone
    uint16_t freqVal = static_cast<uint16_t>((YM_CLOCK_HZ / freqHz) / divisor + 0.5f);
    setTone(chip, voice, freqVal);
}

void YM2149Class::setTone(uint8_t chip, uint8_t voice, uint16_t value)
{
    switch (voice)
    {
        case 0: // Channel A
            write(chip, REG_A_FREQ,     value & 0xFF);
            write(chip, REG_A_FREQ + 1, (value >> 8) & 0x0F);
            break;

        case 1: // Channel B
            write(chip, REG_B_FREQ,     value & 0xFF);
            write(chip, REG_B_FREQ + 1, (value >> 8) & 0x0F);
            break;

        case 2: // Channel C
            write(chip, REG_C_FREQ,     value & 0xFF);
            write(chip, REG_C_FREQ + 1, (value >> 8) & 0x0F);
            break;

        case 3: // Noise (5 bits)
            write(chip, REG_NOISE_FREQ, value & 0x1F);
            break;

        case 4: // Envelope
            // IMPORTANT: value must already be calculated as:
            // YM_CLOCK_HZ / (256.0f * frequency) + 0.5
            write(chip, REG_ENV_FREQ,     value & 0xFF);
            write(chip, REG_ENV_FREQ + 1, (value >> 8) & 0x0F);
            break;
    }
}

void YM2149Class::setVolume(uint8_t chip, uint8_t voice, uint8_t value)
{
    if (voice > 2) return;
    value &= 0x0F;
    levelValue[chip][voice] = (levelValue[chip][voice] & 0x10) | value;
    write(chip, REG_A_LEVEL + voice, levelValue[chip][voice]);
}

void YM2149Class::setNoise(uint8_t chip, uint8_t voice, uint8_t mode)
{
    if (voice > 2) return;

    uint8_t toneMask  = 1 << voice;        // Bits 0,1,2 for tone A/B/C
    uint8_t noiseMask = 1 << (voice + 3);  // Bits 3,4,5 for noise A/B/C

    // Clear only this voice's tone+noise mask from the mixer
    mixerValue[chip] &= ~(toneMask | noiseMask);

    switch (mode)
    {
        case 1: // Tone only
            mixerValue[chip] |= noiseMask;
            break;

        case 2: // Noise only
            mixerValue[chip] |= toneMask;
            break;

        case 3: // Mute both (optional case)
            mixerValue[chip] |= (toneMask | noiseMask);
            break;

        case 0: // Both tone and noise enabled
        default:
            // leave both cleared
            break;
    }

    write(chip, REG_MIXER, mixerValue[chip]);
}

void YM2149Class::setEnv(uint8_t chip, uint8_t voice, uint8_t value)
{
    if (voice > 2) return;
    uint8_t *state = &levelValue[chip][voice];

    value &= 0b00000001;
    value <<= 4; // Bit 4 enables envelope for this voice
    *state = (*state & 0x0F) | value;
    write(chip, REG_A_LEVEL + voice, *state);
}

void YM2149Class::setEnvShape(uint8_t chip, uint8_t cont, uint8_t att, uint8_t alt, uint8_t hold)
{
    uint8_t val = ((cont & 1) << 3) | ((att & 1) << 2) | ((alt & 1) << 1) | (hold & 1);

    // Force re-trigger envelope
    write(chip, REG_ENV_SHAPE, 0);   // dummy write to retrigger
    write(chip, REG_ENV_SHAPE, val); // real value
}

void YM2149Class::mute(uint8_t chip)
{
    for (uint8_t v = 0; v < 3; ++v) {
        levelValue[chip][v] = 0;
        write(chip, REG_A_LEVEL + v, 0);
    }
    write(chip, REG_MIXER, 0b00111000); // disable tone
}
