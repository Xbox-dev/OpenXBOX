#pragma once

#include <cstdint>

#include "pci_regs.h"

namespace openxbox {

#define PCI_CONFIG_HEADER_SIZE   0x40

#define	PCI_BAR_TYPE_IO          1
#define PCI_BAR_TYPE_MEMORY      0

#define PCI_NUM_BARS_DEVICE      6
#define PCI_NUM_BARS_PCI_BRIDGE  2

#define PCI_VENDOR_ID_NVIDIA     0x10DE

class PCIDevice;

typedef struct {
    union {
        struct {
            uint32_t type : 1;
            uint32_t locatable : 2;
            uint32_t prefetchable : 1;
            uint32_t address : 28;
        } Memory;

        struct {
            uint32_t type : 1;
            uint32_t reserved : 1;
            uint32_t address : 30;
        } IO;

        struct {
            uint32_t type : 1;
            uint32_t other : 31;
        } Raw;

        uint32_t value;
    };

} PCIBarRegister;

class PCIDevice {
    // PCI Device Interface
public:
    virtual void Init() = 0;
    virtual void Reset() = 0;
    virtual uint32_t IORead(int barIndex, uint32_t port, unsigned size) = 0;
    virtual void IOWrite(int barIndex, uint32_t port, uint32_t value, unsigned size) = 0;
    virtual uint32_t MMIORead(int barIndex, uint32_t addr, unsigned size) = 0;
    virtual void MMIOWrite(int barIndex, uint32_t addr, uint32_t value, unsigned size) = 0;

    // PCI Device Implementation
public:
    PCIDevice(uint8_t type, uint16_t vendorID, uint16_t deviceID, uint8_t revisionID, uint16_t classID,
		uint16_t subsystemVendorID = 0x00, uint16_t subsystemID = 0x00);
    bool GetIOBar(uint32_t port, uint8_t* barIndex, uint32_t *baseAddress);
    bool GetMMIOBar(uint32_t addr, uint8_t* barIndex, uint32_t *baseAddress);
    bool RegisterBAR(int index, uint32_t size, uint32_t type);
    
    void ReadConfig(uint32_t reg, uint8_t *value, uint8_t size);
    void WriteConfig(uint32_t reg, uint32_t value, uint8_t size);
protected:
    uint32_t m_BARSizes[PCI_NUM_BARS_DEVICE];

    uint8_t m_configSpace[256];
	uint8_t m_writeMask[256];
	uint8_t m_checkMask[256];
	uint8_t m_write1ToClearMask[256];

    inline uint8_t  Read8 (uint8_t *buf, uint32_t reg) { return buf[reg]; }
    inline uint16_t Read16(uint8_t *buf, uint32_t reg) { return *reinterpret_cast<uint16_t *>(&buf[reg]); }
    inline uint32_t Read32(uint8_t *buf, uint32_t reg) { return *reinterpret_cast<uint32_t *>(&buf[reg]); }

    inline void Write8 (uint8_t *buf, uint32_t reg, uint8_t  value) { buf[reg] = value; }
    inline void Write16(uint8_t *buf, uint32_t reg, uint16_t value) { *reinterpret_cast<uint16_t *>(&buf[reg]) = value; }
    inline void Write32(uint8_t *buf, uint32_t reg, uint32_t value) { *reinterpret_cast<uint32_t *>(&buf[reg]) = value; }

	inline uint8_t  TestAndSet8 (uint8_t *buf, uint32_t reg, uint8_t  mask) { uint8_t  val = Read8 (buf, reg); Write8 (buf, reg, val | mask); return val & mask; }
	inline uint16_t TestAndSet16(uint8_t *buf, uint32_t reg, uint16_t mask) { uint16_t val = Read16(buf, reg); Write16(buf, reg, val | mask); return val & mask; }
	inline uint32_t TestAndSet32(uint8_t *buf, uint32_t reg, uint32_t mask) { uint32_t val = Read32(buf, reg); Write32(buf, reg, val | mask); return val & mask; }
};

}
