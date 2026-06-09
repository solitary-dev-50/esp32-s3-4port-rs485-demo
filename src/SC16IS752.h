#ifndef SC16IS752_H
#define SC16IS752_H

#include <Arduino.h>
#include <SPI.h>

#define SC16_CH_A  0
#define SC16_CH_B  1

// Register addresses
#define RHR      0x00
#define THR      0x00
#define IER      0x01
#define FCR      0x02
#define IIR      0x02
#define LCR      0x03
#define MCR      0x04
#define LSR      0x05
#define MSR      0x06
#define SPR      0x07
#define TXLVL    0x08
#define RXLVL    0x09
#define IODIR    0x0A
#define IOSTATE  0x0B
#define IOINTENA 0x0C
#define IOCONTROL 0x0E
#define EFCR     0x0F

#define DLL      0x00
#define DLH      0x01
#define EFR      0x02

// LCR
#define LCR_DLAB 0x80
#define LCR_8N1  0x03

// FCR
#define FCR_FIFO_EN     0x01
#define FCR_RX_FIFO_RST 0x02
#define FCR_TX_FIFO_RST 0x04

// MCR
#define MCR_RTS  0x02

// LSR
#define LSR_RX_FIFO_DATA 0x01
#define LSR_TEMT         0x40

class SC16IS752 {
private:
    uint8_t _csPin;
    SPIClass* _spi;
    SPISettings _spiSettings;
    bool _spiReady;

    uint8_t readReg(uint8_t channel, uint8_t reg);
    void writeReg(uint8_t channel, uint8_t reg, uint8_t value);

public:
    SC16IS752(uint8_t csPin);
    bool initSPI(uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin);
    bool beginChannel(uint8_t channel, uint32_t baudrate);
    size_t write(uint8_t channel, const uint8_t* data, size_t len);
    int read(uint8_t channel, uint8_t* buffer, size_t maxLen, uint32_t timeoutMs);
    void flushRX(uint8_t channel);
    bool available(uint8_t channel);
};

#endif
