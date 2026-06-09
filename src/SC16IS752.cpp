#include "SC16IS752.h"

// --- SPI command byte encoding ---
static uint8_t makeCmd(uint8_t channel, uint8_t reg, bool read) {
    return (read ? 0x80 : 0x00)
         | ((reg & 0x0F) << 3)
         | ((channel & 0x01) << 1);
}

SC16IS752::SC16IS752(uint8_t csPin)
    : _csPin(csPin), _spi(nullptr), _spiSettings(1000000, MSBFIRST, SPI_MODE0), _spiReady(false)
{}

bool SC16IS752::initSPI(uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin) {
    Serial.printf("SC16 SPI init: CS=%d, SCK=%d, MISO=%d, MOSI=%d, speed=1000000, mode=0\n",
                  _csPin, sckPin, misoPin, mosiPin);
    _spi = new SPIClass(FSPI);
    if (!_spi) return false;
    _spi->begin(sckPin, misoPin, mosiPin, _csPin);
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
    _spiReady = true;
    return true;
}

// --- readReg ---
uint8_t SC16IS752::readReg(uint8_t channel, uint8_t reg) {
    if (!_spiReady) return 0;
    uint8_t cmd = makeCmd(channel, reg, true);
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(cmd);
    uint8_t value = _spi->transfer(0xFF);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
    return value;
}

// --- writeReg ---
void SC16IS752::writeReg(uint8_t channel, uint8_t reg, uint8_t value) {
    if (!_spiReady) return;
    uint8_t cmd = makeCmd(channel, reg, false);
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(cmd);
    _spi->transfer(value);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}

// --- beginChannel with SPR self-test ---
bool SC16IS752::beginChannel(uint8_t channel, uint32_t baudrate) {
    if (!_spiReady) return false;

    const char* ch = (channel == SC16_CH_A) ? "A" : "B";

    // SPR self-test
    writeReg(channel, SPR, 0x55);
    if (readReg(channel, SPR) != 0x55) {
        Serial.printf("SC16 CH %s self test FAIL (0x55)\n", ch);
        return false;
    }
    writeReg(channel, SPR, 0xAA);
    if (readReg(channel, SPR) != 0xAA) {
        Serial.printf("SC16 CH %s self test FAIL (0xAA)\n", ch);
        return false;
    }
    Serial.printf("SC16 CH %s self test OK\n", ch);

    // Baud rate divisor for 14.7456MHz
    uint16_t divisor = (uint32_t)(14745600UL / (baudrate * 16UL));
    writeReg(channel, LCR, 0x80);    // DLAB = 1
    writeReg(channel, DLL, divisor & 0xFF);
    writeReg(channel, DLH, divisor >> 8);
    writeReg(channel, LCR, LCR_8N1); // DLAB = 0, 8N1
    Serial.printf("SC16 CH %s begin baud=%lu divisor=%u OK\n", ch, baudrate, divisor);

    // FIFO init
    writeReg(channel, IER, 0x00);    // polling mode, no interrupts
    writeReg(channel, FCR, FCR_FIFO_EN | FCR_RX_FIFO_RST | FCR_TX_FIFO_RST);
    delayMicroseconds(100);

    // Default: receive mode (RTS# high -> inverter -> DE/RE low)
    writeReg(channel, MCR, 0x00);

    return true;
}

// --- write ---
size_t SC16IS752::write(uint8_t channel, const uint8_t* data, size_t len) {
    if (!_spiReady || !data || len == 0) return 0;

    const char* ch = (channel == SC16_CH_A) ? "A" : "B";
    Serial.printf("SC16 write ch=%s len=%d\n", ch, len);

    // RTS direction: MCR RTS=1 -> RTS# low -> inverter -> DE/RE high = TX
    writeReg(channel, MCR, MCR_RTS);
    if (channel == SC16_CH_B) { Serial.printf("SC16 ch=B MCR after TX enable = 0x%02X\n", readReg(channel, MCR)); }

    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned long t0 = millis();
        while (readReg(channel, TXLVL) == 0) {
            if (millis() - t0 > 20) break;
        }
        writeReg(channel, THR, data[i]);
        written++;
    }

    unsigned long t1 = millis();
    uint8_t finalLsr = 0;
    while (!((finalLsr = readReg(channel, LSR)) & LSR_TEMT)) {
        if (millis() - t1 > 20) break;
    }
    if (channel == SC16_CH_B) { Serial.printf("SC16 ch=B final LSR = 0x%02X\n", finalLsr); }

    writeReg(channel, MCR, 0x00);
    if (channel == SC16_CH_B) { Serial.printf("SC16 ch=B MCR after RX enable = 0x%02X\n", readReg(channel, MCR)); }

    Serial.printf("SC16 write done ch=%s\n", ch);
    return written;
}
// --- available ---
bool SC16IS752::available(uint8_t channel) {
    if (!_spiReady) return false;
    return readReg(channel, RXLVL) > 0;
}

// --- read ---
int SC16IS752::read(uint8_t channel, uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) {
    if (!_spiReady || !buffer || maxLen == 0) return 0;

    const char* ch = (channel == SC16_CH_A) ? "A" : "B";
    size_t total = 0;
    unsigned long start = millis();
    unsigned long lastByte = millis();
    bool gotAny = false;

    while (millis() - start < timeoutMs && total < maxLen) {
        uint8_t level = readReg(channel, RXLVL);
        if (level > 0) {
            size_t toRead = (size_t)level < (maxLen - total) ? (size_t)level : (maxLen - total);
            for (size_t i = 0; i < toRead; i++) {
                buffer[total++] = readReg(channel, RHR);
            }
            gotAny = true;
            lastByte = millis();
            continue;
        }
        if (gotAny && (millis() - lastByte > 15)) {
            break;
        }
        delay(1);
    }

    Serial.printf("SC16 read ch=%s n=%d\n", ch, total);
    if (channel == SC16_CH_B && total > 0 && total < 9) {
        Serial.printf("SC16 ch=B short read bytes: ");
        for (size_t bi = 0; bi < total; bi++) Serial.printf("%02X ", buffer[bi]);
        Serial.println();
    }
    return (int)total;
}
// --- flushRX ---
void SC16IS752::flushRX(uint8_t channel) {
    if (!_spiReady) return;

    const char* ch = (channel == SC16_CH_A) ? "A" : "B";
    unsigned long start = millis();
    int cleared = 0;

    while (cleared < 128 && millis() - start < 5) {
        uint8_t rxlvl = readReg(channel, RXLVL);
        if (rxlvl == 0) break;
        size_t n = rxlvl < 16 ? rxlvl : 16;
        for (size_t i = 0; i < n; i++) {
            readReg(channel, RHR);
            cleared++;
        }
    }

    if (cleared > 0) {
        Serial.printf("SC16 flushRX ch=%s cleared=%d\n", ch, cleared);
    }
}
