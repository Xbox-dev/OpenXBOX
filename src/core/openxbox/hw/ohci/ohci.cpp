// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->devices->USBController->OHCI.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2018 ergo720
// *
// *  All rights reserved
// *
// ******************************************************************

#include "ohci.h"
#include "openxbox/log.h"

#include <stddef.h>

//#define DEBUG_PACKET
//#define DEBUG_ISOCH

namespace openxbox {

using namespace openxbox::cpu;

// Compute (a*b)/c with a 96 bit intermediate result
static inline uint64_t Muldiv64(uint64_t a, uint32_t b, uint32_t c) {
    union {
        uint64_t ll;
        struct {
            uint32_t low, high;
        } l;
    } u, res;
    uint64_t rl, rh;

    u.ll = a;
    rl = (uint64_t)u.l.low * (uint64_t)b;
    rh = (uint64_t)u.l.high * (uint64_t)b;
    rh += (rl >> 32);
    res.l.high = rh / c;
    res.l.low = (((rh % c) << 32) + (rl & 0xffffffff)) / c;
    return res.ll;
}



/* These macros are used to access the bits of the various registers */
// HcControl
#define OHCI_CTL_CBSR                       ((1<<0)|(1<<1))  // ControlBulkServiceRatio
#define OHCI_CTL_PLE                        (1<<2)           // PeriodicListEnable
#define OHCI_CTL_IE                         (1<<3)           // IsochronousEnable
#define OHCI_CTL_CLE                        (1<<4)           // ControlListEnable
#define OHCI_CTL_BLE                        (1<<5)           // BulkListEnable
#define OHCI_CTL_HCFS                       ((1<<6)|(1<<7))  // HostControllerFunctionalState
#define OHCI_CTL_IR                         (1<<8)           // InterruptRouting
#define OHCI_CTL_RWC                        (1<<9)           // RemoteWakeupConnected
#define OHCI_CTL_RWE                        (1<<10)          // RemoteWakeupEnable
// HcCommandStatus
#define OHCI_STATUS_HCR                     (1<<0)           // HostControllerReset
#define OHCI_STATUS_CLF                     (1<<1)           // ControlListFilled
#define OHCI_STATUS_BLF                     (1<<2)           // BulkListFilled
#define OHCI_STATUS_OCR                     (1<<3)           // OwnershipChangeRequest
#define OHCI_STATUS_SOC                     ((1<<6)|(1<<7))  // SchedulingOverrunCount
// HcInterruptStatus
#define OHCI_INTR_SO                        (1<<0)           // SchedulingOverrun
#define OHCI_INTR_WD                        (1<<1)           // WritebackDoneHead
#define OHCI_INTR_SF                        (1<<2)           // StartofFrame
#define OHCI_INTR_RD                        (1<<3)           // ResumeDetected
#define OHCI_INTR_UE                        (1<<4)           // UnrecoverableError
#define OHCI_INTR_FNO                       (1<<5)           // FrameNumberOverflow
#define OHCI_INTR_RHSC                      (1<<6)           // RootHubStatusChange
#define OHCI_INTR_OC                        (1<<30)          // OwnershipChange
// HcInterruptEnable, HcInterruptDisable
#define OHCI_INTR_MIE                       (1<<31)          // MasterInterruptEnable
// HcHCCA
#define OHCI_HCCA_MASK                      0xFFFFFF00       // HCCA mask
// HcFmInterval
#define OHCI_FMI_FI                         0x00003FFF       // FrameInterval
#define OHCI_FMI_FIT                        0x80000000       // FrameIntervalToggle
// HcFmRemaining
#define OHCI_FMR_FR                         0x00003FFF       // FrameRemaining
#define OHCI_FMR_FRT                        0x80000000       // FrameRemainingToggle
// LSThreshold
#define OHCI_LS_THRESH                      0x628            // LSThreshold
// HcRhDescriptorA
#define OHCI_RHA_RW_MASK                    0x00000000       // Mask of supported features
#define OHCI_RHA_PSM                        (1<<8)           // PowerSwitchingMode
#define OHCI_RHA_NPS                        (1<<9)           // NoPowerSwitching
#define OHCI_RHA_DT                         (1<<10)          // DeviceType
#define OHCI_RHA_OCPM                       (1<<11)          // OverCurrentProtectionMode
#define OHCI_RHA_NOCP                       (1<<12)          // NoOverCurrentProtection
// HcRhStatus
#define OHCI_RHS_LPS                        (1<<0)           // LocalPowerStatus
#define OHCI_RHS_OCI                        (1<<1)           // OverCurrentIndicator
#define OHCI_RHS_DRWE                       (1<<15)          // DeviceRemoteWakeupEnable
#define OHCI_RHS_LPSC                       (1<<16)          // LocalPowerStatusChange
#define OHCI_RHS_OCIC                       (1<<17)          // OverCurrentIndicatorChange
#define OHCI_RHS_CRWE                       (1<<31)          // ClearRemoteWakeupEnable
// HcRhPortStatus
#define OHCI_PORT_CCS                       (1<<0)           // CurrentConnectStatus
#define OHCI_PORT_PES                       (1<<1)           // PortEnableStatus
#define OHCI_PORT_PSS                       (1<<2)           // PortSuspendStatus
#define OHCI_PORT_POCI                      (1<<3)           // PortOverCurrentIndicator
#define OHCI_PORT_PRS                       (1<<4)           // PortResetStatus
#define OHCI_PORT_PPS                       (1<<8)           // PortPowerStatus
#define OHCI_PORT_LSDA                      (1<<9)           // LowSpeedDeviceAttached
#define OHCI_PORT_CSC                       (1<<16)          // ConnectStatusChange
#define OHCI_PORT_PESC                      (1<<17)          // PortEnableStatusChange
#define OHCI_PORT_PSSC                      (1<<18)          // PortSuspendStatusChange
#define OHCI_PORT_OCIC                      (1<<19)          // PortOverCurrentIndicatorChange
#define OHCI_PORT_PRSC                      (1<<20)          // PortResetStatusChange
#define OHCI_PORT_WTC                       (OHCI_PORT_CSC|OHCI_PORT_PESC|OHCI_PORT_PSSC \
                                                          |OHCI_PORT_OCIC|OHCI_PORT_PRSC)

/* Bitfields for the first word of an ED */
#define OHCI_ED_FA_SHIFT  0
#define OHCI_ED_FA_MASK   (0x7F<<OHCI_ED_FA_SHIFT)    // FunctionAddress
#define OHCI_ED_EN_SHIFT  7
#define OHCI_ED_EN_MASK   (0xF<<OHCI_ED_EN_SHIFT)     // EndpointNumber
#define OHCI_ED_D_SHIFT   11
#define OHCI_ED_D_MASK    (3<<OHCI_ED_D_SHIFT)        // Direction
#define OHCI_ED_S         (1<<13)                     // Speed
#define OHCI_ED_K         (1<<14)                     // sKip
#define OHCI_ED_F         (1<<15)                     // Format
#define OHCI_ED_MPS_SHIFT 16
#define OHCI_ED_MPS_MASK  (0x7FF<<OHCI_ED_MPS_SHIFT)  // MaximumPacketSize

/* Flags in the HeadP field of an ED */
#define OHCI_ED_H         1                           // Halted
#define OHCI_ED_C         2                           // toggleCarry

/* Bitfields for the first word of a TD */
#define OHCI_TD_R         (1<<18)                     // bufferRounding
#define OHCI_TD_DP_SHIFT  19
#define OHCI_TD_DP_MASK   (3<<OHCI_TD_DP_SHIFT)       // Direction-Pid
#define OHCI_TD_DI_SHIFT  21
#define OHCI_TD_DI_MASK   (7<<OHCI_TD_DI_SHIFT)       // DelayInterrupt
#define OHCI_TD_T0        (1<<24)
#define OHCI_TD_T1        (1<<25)                     // DataToggle (T0 and T1)
#define OHCI_TD_EC_SHIFT  26
#define OHCI_TD_EC_MASK   (3<<OHCI_TD_EC_SHIFT)       // ErrorCount
#define OHCI_TD_CC_SHIFT  28
#define OHCI_TD_CC_MASK   (0xF<<OHCI_TD_CC_SHIFT)     // ConditionCode
/* Bitfields for the first word of an Isochronous Transfer Desciptor.  */
/* CC & DI - same as in the General Transfer Desciptor */
#define OHCI_TD_SF_SHIFT  0
#define OHCI_TD_SF_MASK   (0xFFFF<<OHCI_TD_SF_SHIFT)
#define OHCI_TD_FC_SHIFT  24
#define OHCI_TD_FC_MASK   (7<<OHCI_TD_FC_SHIFT)

/* Isochronous Transfer Desciptor - Offset / PacketStatusWord */
#define OHCI_TD_PSW_CC_SHIFT 12
#define OHCI_TD_PSW_CC_MASK  (0xF<<OHCI_TD_PSW_CC_SHIFT)
#define OHCI_TD_PSW_SIZE_SHIFT 0
#define OHCI_TD_PSW_SIZE_MASK  (0xFFF<<OHCI_TD_PSW_SIZE_SHIFT)

/* Mask the four least significant bits in an ED address */
#define OHCI_DPTR_MASK    0xFFFFFFF0

#define OHCI_BM(val, field) \
  (((val) & OHCI_##field##_MASK) >> OHCI_##field##_SHIFT)

#define OHCI_SET_BM(val, field, newval) do { \
    val &= ~OHCI_##field##_MASK; \
    val |= ((newval) << OHCI_##field##_SHIFT) & OHCI_##field##_MASK; \
    } while(0)

/* Indicates the direction of data flow as specified by the TD */
#define OHCI_TD_DIR_SETUP     0x0 // to endpoint
#define OHCI_TD_DIR_OUT       0x1 // to endpoint
#define OHCI_TD_DIR_IN        0x2 // from endpoint
#define OHCI_TD_DIR_RESERVED  0x3

#define OHCI_CC_NOERROR             0x0
#define OHCI_CC_CRC                 0x1
#define OHCI_CC_BITSTUFFING         0x2
#define OHCI_CC_DATATOGGLEMISMATCH  0x3
#define OHCI_CC_STALL               0x4
#define OHCI_CC_DEVICENOTRESPONDING 0x5
#define OHCI_CC_PIDCHECKFAILURE     0x6
#define OHCI_CC_UNDEXPETEDPID       0x7
#define OHCI_CC_DATAOVERRUN         0x8
#define OHCI_CC_DATAUNDERRUN        0x9
#define OHCI_CC_BUFFEROVERRUN       0xC
#define OHCI_CC_BUFFERUNDERRUN      0xD

#define USB_HZ 12000000

#define USUB(a, b) ((int16_t)((uint16_t)(a) - (uint16_t)(b)))

#define OHCI_PAGE_MASK    0xFFFFF000
#define OHCI_OFFSET_MASK  0xFFF

OHCI::OHCI(Cpu* cpu, int Irq, USBPCIDevice* UsbObj)
    : m_cpu(cpu)
{
    int offset = 0;
    USBPortOps* ops;

	m_IrqNum = Irq;
	m_UsbDevice = UsbObj;
    m_bFrameTime = false;
    ops = new USBPortOps();
    {
        using namespace std::placeholders;

        ops->attach = std::bind(&OHCI::OHCI_Attach, this, _1);
        ops->detach = std::bind(&OHCI::OHCI_Detach, this, _1);
        ops->child_detach = std::bind(&OHCI::OHCI_ChildDetach, this, _1);
        ops->wakeup = std::bind(&OHCI::OHCI_Wakeup, this, _1);
        ops->complete = std::bind(&OHCI::OHCI_AsyncCompletePacket, this, _1, _2);
    }
    
    if (m_IrqNum == 9) {
        offset = 4;
    }

	for (int i = 0; i < 4; i++) {
		m_UsbDevice->USB_RegisterPort(&m_Registers.RhPort[i].UsbPort, i + offset, USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL, ops);
	}
	OHCI_PacketInit(&m_UsbPacket);

	m_UsbFrameTime = 1000000ULL; // 1 ms expressed in ns
	m_TicksPerUsbTick = 1000000000ULL / USB_HZ; // 83

	// Do a hardware reset
	OHCI_StateReset();
}

void OHCI::OHCI_FrameBoundaryWrapper(void* pVoid)
{
	static_cast<OHCI*>(pVoid)->OHCI_FrameBoundaryWorker();
}

void OHCI::OHCI_FrameBoundaryWorker()
{
	OHCI_HCCA hcca;

    while (m_bFrameTime) {}
    m_bFrameTime = true;

	if (OHCI_ReadHCCA(m_Registers.HcHCCA, &hcca)) {
		log_warning("OHCI: HCCA read error at physical address 0x%X\n", m_Registers.HcHCCA);
		OHCI_FatalError();
        m_bFrameTime = false;
		return;
	}

	// Process all the lists at the end of the frame
	if (m_Registers.HcControl & OHCI_CTL_PLE) {
		// From the standard: "The head pointer used for a particular frame is determined by using the last 5 bits of the
		// Frame Counter as an offset into the interrupt array within the HCCA."
		int n = m_Registers.HcFmNumber & 0x1F;
		OHCI_ServiceEDlist(hcca.HccaInterrruptTable[n], 0); // dropped little -> big endian conversion from XQEMU
	}

	// Cancel all pending packets if either of the lists has been disabled
	if (m_OldHcControl & (~m_Registers.HcControl) & (OHCI_CTL_BLE | OHCI_CTL_CLE)) {
		if (m_AsyncTD) {
			m_UsbDevice->USB_CancelPacket(&m_UsbPacket);
			m_AsyncTD = 0;
		}
		OHCI_StopEndpoints();
	}
	m_OldHcControl = m_Registers.HcControl;
    OHCI_ProcessLists(0);

	// Stop if UnrecoverableError happened or OHCI_SOF will crash
	if (m_Registers.HcInterruptStatus & OHCI_INTR_UE) {
        m_bFrameTime = false;
		return;
	}

	// From the standard: "This bit is loaded from the FrameIntervalToggle field of
	// HcFmInterval whenever FrameRemaining reaches 0."
    m_Registers.HcFmRemaining = (m_Registers.HcFmInterval & OHCI_FMI_FIT) == 0 ?
        m_Registers.HcFmRemaining & ~OHCI_FMR_FRT : m_Registers.HcFmRemaining | OHCI_FMR_FRT;

	// Increment frame number
	m_Registers.HcFmNumber = (m_Registers.HcFmNumber + 1) & 0xFFFF; // prevent overflow
	hcca.HccaFrameNumber = m_Registers.HcFmNumber; // dropped big -> little endian conversion from XQEMU

	if (m_DoneCount == 0 && !(m_Registers.HcInterruptStatus & OHCI_INTR_WD)) {
		if (!m_Registers.HcDoneHead) {
			// From the standard: "This is set to zero whenever HC writes the content of this
			// register to HCCA. It also sets the WritebackDoneHead of HcInterruptStatus."
			log_fatal("OHCI: HcDoneHead is zero but WritebackDoneHead interrupt is not set!\n");
		}

		if (m_Registers.HcInterrupt & m_Registers.HcInterruptStatus) {
			// From the standard: "The least significant bit of this entry is set to 1 to indicate whether an
			// unmasked HcInterruptStatus was set when HccaDoneHead was written." It's tecnically incorrect to
			// do this to HcDoneHead instead of HccaDoneHead however it doesn't matter since HcDoneHead is
			// zeroed below
			m_Registers.HcDoneHead |= 1;
		}

		hcca.HccaDoneHead = m_Registers.HcDoneHead; // dropped big -> little endian conversion from XQEMU
		m_Registers.HcDoneHead = 0;
		m_DoneCount = 7;
		OHCI_SetInterrupt(OHCI_INTR_WD);
	}

	if (m_DoneCount != 7 && m_DoneCount != 0) {
		// decrease Done Queue counter
		m_DoneCount--;
	}

	// Do SOF stuff here
	OHCI_SOF(false);

	// Writeback HCCA
	if (OHCI_WriteHCCA(m_Registers.HcHCCA, &hcca)) {
		log_warning("OHCI: HCCA write error at physical address 0x%X\n", m_Registers.HcHCCA);
		OHCI_FatalError();
	}
    m_bFrameTime = false;
}

void OHCI::OHCI_FatalError()
{
	// According to the standard, an OHCI will stop operating, and set itself into error state
	// (which can be queried by MMIO). Instead of calling directly CxbxKrnlCleanup, we let the
	// HCD know the problem so that it can try to solve it

	OHCI_SetInterrupt(OHCI_INTR_UE);
	OHCI_BusStop();
	log_debug("OHCI: an unrecoverable error occoured!\n");
}

bool OHCI::OHCI_ReadHCCA(uint32_t Paddr, OHCI_HCCA* Hcca)
{
	// ergo720: there could be a peculiar problem if the shared memory between HCD and HC is allocated by the
	// VMManager with VirtualAlloc: the physical allocation would not reside in memory.bin and if we tried to
	// access the physical address of it, we would access an empty page. In practice, I disassembled various
	// xbe's of my games and discovered that this shared memory is allocated with MmAllocateContiguousMemory
	// which means we can access it from the contiguous region just fine (lucky)
	// ... provided that XDK revisions didn't alter this

	// NOTE: this shared memory contains the HCCA + EDs and TDs

	if (Paddr != 0) {
		memcpy(Hcca, reinterpret_cast<void*>(Paddr + CONTIGUOUS_MEMORY_BASE), sizeof(OHCI_HCCA));
		return false;
	}

	return true; // error
}

bool OHCI::OHCI_WriteHCCA(uint32_t Paddr, OHCI_HCCA* Hcca)
{
	if (Paddr != 0) {
		// We need to calculate the offset of the HccaFrameNumber member to avoid overwriting HccaInterrruptTable
		size_t OffsetOfFrameNumber = offsetof(OHCI_HCCA, HccaFrameNumber);

		memcpy(reinterpret_cast<void*>(Paddr + OffsetOfFrameNumber + CONTIGUOUS_MEMORY_BASE),
			reinterpret_cast<uint8_t*>(Hcca) + OffsetOfFrameNumber, 8);
		return false;
	}

	return true; // error
}

bool OHCI::OHCI_ReadED(uint32_t Paddr, OHCI_ED* Ed)
{
	if (Paddr != 0) {
        m_cpu->VMemRead(Paddr, sizeof(*Ed), Ed);
		return false;
	}
	return true; // error
}

bool OHCI::OHCI_WriteED(uint32_t Paddr, OHCI_ED* Ed)
{
	if (Paddr != 0) {
		// According to the standard, only the HeadP field is writable by the HC, so we'll write just that
		size_t OffsetOfHeadP = offsetof(OHCI_ED, HeadP);
        m_cpu->VMemWrite(Paddr, 4, Ed + OffsetOfHeadP);
		return false;
	}
	return true; // error
}

bool OHCI::OHCI_ReadTD(uint32_t Paddr, OHCI_TD* Td)
{
	if (Paddr != 0) {
        m_cpu->VMemRead(Paddr, sizeof(*Td), Td);
		return false;
	}
	return true; // error
}

bool OHCI::OHCI_WriteTD(uint32_t Paddr, OHCI_TD* Td)
{
	if (Paddr != 0) {
        m_cpu->VMemWrite(Paddr, sizeof(*Td), Td);
		return false;
	}
	return true; // error
}

bool OHCI::OHCI_ReadIsoTD(uint32_t Paddr, OHCI_ISO_TD* td) {
    if (Paddr != 0) {
        m_cpu->VMemRead(Paddr, sizeof(*td), td);
        return false;
    }
    return true; // error
}

bool OHCI::OHCI_WriteIsoTD(uint32_t Paddr, OHCI_ISO_TD* td) {
    if (Paddr != 0) {
        m_cpu->VMemWrite(Paddr, sizeof(*td), td);
        return false;
    }
    return true; // error
}

bool OHCI::OHCI_CopyTD(OHCI_TD* Td, uint8_t* Buffer, int Length, bool bIsWrite)
{
	uint32_t ptr, n;

	// Figure out if we are crossing a 4K page boundary
	ptr = Td->CurrentBufferPointer;
	n = 0x1000 - (ptr & 0xFFF);
	if (n > (unsigned int)Length) {
		n = (unsigned int)Length;
	}

	if (OHCI_FindAndCopyTD(ptr, Buffer, n, bIsWrite)) {
		return true; // error
	}
	if (n == (unsigned int)Length) {
		return false; // no bytes left to copy
	}

	// From the standard: "If during the data transfer the buffer address contained in the HC�fs working copy of
	// CurrentBufferPointer crosses a 4K boundary, the upper 20 bits of BufferEnd are copied to the
	// working value of CurrentBufferPointer causing the next buffer address to be the 0th byte in the
	// same 4K page that contains the last byte of the buffer."
	ptr = Td->BufferEnd & ~0xFFFu;
	Buffer += n;
	if (OHCI_FindAndCopyTD(ptr, Buffer, Length - n, bIsWrite)) {
		return true; // error
	}
	return false;
}

bool OHCI::OHCI_CopyIsoTD(uint32_t start_addr, uint32_t end_addr, uint8_t* Buffer, int Length, bool bIsWrite) {
    uint32_t ptr, n;

    ptr = start_addr;
    n = 0x1000 - (ptr & 0xFFF);
    if (n > (unsigned int)Length) {
        n = Length;
    }

    if (OHCI_FindAndCopyTD(ptr, Buffer, n, bIsWrite)) {
        return true; // error
    }
    if (n == (unsigned int)Length) {
        return false; // no bytes left to copy
    }
    ptr = end_addr & ~0xfffu;
    Buffer += n;
    if (OHCI_FindAndCopyTD(ptr, Buffer, Length - n, bIsWrite)) {
        return true; // error
    }
    return false;
}

bool OHCI::OHCI_FindAndCopyTD(uint32_t Paddr, uint8_t* Buffer, int Length, bool bIsWrite)
{
	// ergo720: the buffer pointed to by Paddr can be anywhere in memory (it depends on how the xbe has
	// allocated it) so, sadly, we cannot make any assumptions here regarding its location like we did
	// in OHCI_ReadHCCA and the problem with VirtualAlloc can arise this time. Because of the hack in
	// TranslateVAddrToPAddr, VirtualAlloc allocations are identity mapped and addresses below 0x4000000
	// (Xbox) or 0x8000000 (Chihiro, Devkit) cannot be used by the VMManager for anything but to allocate
	// xbe sections. This means that if Paddr is higher than the maximum possible physical address, then
	// we know it's an identity mapped address, otherwise it's a contiguous address

	int offset = 0;

	if (Paddr == 0) {
		return true; // error
	}

	//if (g_bIsRetail) {
		if (Paddr < XBOX_MEMORY_SIZE) {
			offset = CONTIGUOUS_MEMORY_BASE;
		}
	/*}
	else {
		if (Paddr < CHIHIRO_MEMORY_SIZE) {
			offset = CONTIGUOUS_MEMORY_BASE;
		}
	}*/

	if (bIsWrite) {
		memcpy(reinterpret_cast<void*>(Paddr + offset), Buffer, Length);
	}
	else {
		memcpy(Buffer, reinterpret_cast<void*>(Paddr + offset), Length);
	}

	return false;
}

int OHCI::OHCI_ServiceEDlist(uint32_t Head, int Completion)
{
	OHCI_ED ed;
	uint32_t next_ed;
	uint32_t current;
	int active;

	active = 0;

	if (Head == 0) {
		// no ED here, nothing to do
		return 0;
	}

	for (current = Head; current; current = next_ed) {
		if (OHCI_ReadED(current, &ed)) {
            log_warning("OHCI: ED read error at physical address 0x%X\n", current);
			OHCI_FatalError();
			return 0;
		}

		// From the standard "An Endpoint Descriptor (ED) is a 16-byte, memory resident structure that must be aligned to a
		// 16-byte boundary."
		next_ed = ed.NextED & OHCI_DPTR_MASK;

		if ((ed.HeadP & OHCI_ED_H) || (ed.Flags & OHCI_ED_K)) { // halted or skip
			// Cancel pending packets for ED that have been paused
			uint32_t addr = ed.HeadP & OHCI_DPTR_MASK;
			if (m_AsyncTD && addr == m_AsyncTD) {
				m_UsbDevice->USB_CancelPacket(&m_UsbPacket);
				m_AsyncTD = 0;
				m_UsbDevice->USB_DeviceEPstopped(m_UsbPacket.Endpoint->Dev, m_UsbPacket.Endpoint);
			}
			continue;
		}

		while ((ed.HeadP & OHCI_DPTR_MASK) != ed.TailP) { // a TD is available to be processed
#ifdef DEBUG_PACKET
			log_spew("OHCI: ED @ 0x%.8x fa=%u en=%u d=%u s=%u k=%u f=%u mps=%u "
				"h=%u c=%u\n  head=0x%.8x tailp=0x%.8x next=0x%.8x\n", current,
				OHCI_BM(ed.Flags, ED_FA), OHCI_BM(ed.Flags, ED_EN),
				OHCI_BM(ed.Flags, ED_D), (ed.Flags & OHCI_ED_S) != 0,
				(ed.Flags & OHCI_ED_K) != 0, (ed.Flags & OHCI_ED_F) != 0,
				OHCI_BM(ed.Flags, ED_MPS), (ed.HeadP & OHCI_ED_H) != 0,
				(ed.HeadP & OHCI_ED_C) != 0, ed.HeadP & OHCI_DPTR_MASK,
				ed.TailP & OHCI_DPTR_MASK, ed.NextED & OHCI_DPTR_MASK);
#endif
			active = 1;

			if ((ed.Flags & OHCI_ED_F) == 0) {
				// Handle control, interrupt or bulk endpoints
				if (OHCI_ServiceTD(&ed)) {
					break;
				}
			}
			else {
				// Handle isochronous endpoints
                if (OHCI_ServiceIsoTD(&ed, Completion)) {
                    break;
                }
			}
		}

		// Writeback ED
		if (OHCI_WriteED(current, &ed)) {
			OHCI_FatalError();
			return 0;
		}
	}

	return active;
}

int OHCI::OHCI_ServiceTD(OHCI_ED* Ed)
{
	int direction;
	size_t length = 0, packetlen = 0;
#ifdef DEBUG_PACKET
	const char *str = nullptr;
#endif
	int pid;
	int ret;
	int i;
    XboxDeviceState* dev;
	USBEndpoint* ep;
	OHCI_TD td;
	uint32_t addr;
	int flag_r;
	int completion;

	addr = Ed->HeadP & OHCI_DPTR_MASK;
	// See if this TD has already been submitted to the device
	completion = (addr == m_AsyncTD);
	if (completion && !m_AsyncComplete) {
#ifdef DEBUG_PACKET
        log_spew("OHCI: Skipping async TD\n");
#endif
		return 1;
	}
	if (OHCI_ReadTD(addr, &td)) {
        log_warning("OHCI: TD read error at physical address 0x%X\n", addr);
		OHCI_FatalError();
		return 0;
	}

	// From the standard: "This 2-bit field indicates the direction of data flow and the PID
	// to be used for the token. This field is only relevant to the HC if the D field in the ED
	// was set to 00b or 11b indicating that the PID determination is deferred to the TD."
	direction = OHCI_BM(Ed->Flags, ED_D);
	switch (direction) {
		case OHCI_TD_DIR_OUT:
		case OHCI_TD_DIR_IN:
			// Same value
			break;
		default:
			direction = OHCI_BM(td.Flags, TD_DP);
	}

	// Info: Each USB transaction consists of a
	// 1. Token Packet, (Header defining what it expects to follow).
	// 2. Optional Data Packet, (Containing the payload).
	// 3. Status Packet, (Used to acknowledge transactions and to provide a means of error correction).

	// There are three types of token packets:
	// In - Informs the USB device that the host wishes to read information.
	// Out - Informs the USB device that the host wishes to send information.
	// Setup - Used to begin control transfers.

	switch (direction) {
		case OHCI_TD_DIR_IN:
#ifdef DEBUG_PACKET
			str = "in";
#endif
			pid = USB_TOKEN_IN;
			break;
		case OHCI_TD_DIR_OUT:
#ifdef DEBUG_PACKET
			str = "out";
#endif
			pid = USB_TOKEN_OUT;
			break;
		case OHCI_TD_DIR_SETUP:
#ifdef DEBUG_PACKET
			str = "setup";
#endif
			pid = USB_TOKEN_SETUP;
			break;
		default:
            log_warning("OHCI: bad direction\n");
			return 1;
	}

	// Check if this TD has a buffer of user data to transfer
	if (td.CurrentBufferPointer && td.BufferEnd) {
		if ((td.CurrentBufferPointer & 0xFFFFF000) != (td.BufferEnd & 0xFFFFF000)) {
			// the buffer crosses a 4K page boundary
			length = (td.BufferEnd & 0xFFF) + 0x1001 - (td.CurrentBufferPointer & 0xFFF);
		}
		else {
			// the buffer is within a single page
			length = (td.BufferEnd - td.CurrentBufferPointer) + 1;
		}

		packetlen = length;
		if (length && direction != OHCI_TD_DIR_IN) {
			// The endpoint may not allow us to transfer it all now
			packetlen = (Ed->Flags & OHCI_ED_MPS_MASK) >> OHCI_ED_MPS_SHIFT;
			if (packetlen > length) {
				packetlen = length;
			}
			if (!completion) {
				if (OHCI_CopyTD(&td, m_UsbBuffer, packetlen, false)) {
					OHCI_FatalError();
				}
			}
		}
	}

	flag_r = (td.Flags & OHCI_TD_R) != 0;
#ifdef DEBUG_PACKET
	log_spew("OHCI: TD @ 0x%.8X %lld of %lld bytes %s r=%d cbp=0x%.8X be=0x%.8X\n",
        addr, (int64_t)packetlen, (int64_t)length, str, flag_r, td.CurrentBufferPointer, td.BufferEnd);

#if LOG_LEVEL >= LOG_LEVEL_SPEW
	if (packetlen  > 0 && direction != OHCI_TD_DIR_IN) {
		printf("  data:");
		for (i = 0; i < packetlen; i++) {
			printf(" %.2x", m_UsbBuffer[i]);
		}
        printf("\n");
	}
#endif

#endif
	if (completion) {
		m_AsyncTD = 0;
		m_AsyncComplete = 0;
	}
	else {
		if (m_AsyncTD) {
			// From XQEMU: "??? The hardware should allow one active packet per endpoint.
			// We only allow one active packet per controller. This should be sufficient
			// as long as devices respond in a timely manner."
            log_debug("OHCI: too many pending packets\n");
			return 1;
		}
		dev = OHCI_FindDevice(OHCI_BM(Ed->Flags, ED_FA));
		ep = m_UsbDevice->USB_GetEP(dev, pid, OHCI_BM(Ed->Flags, ED_EN));
		m_UsbDevice->USB_PacketSetup(&m_UsbPacket, pid, ep, 0, addr, !flag_r, OHCI_BM(td.Flags, TD_DI) == 0);
		m_UsbDevice->USB_PacketAddBuffer(&m_UsbPacket, m_UsbBuffer, packetlen);
		m_UsbDevice->USB_HandlePacket(dev, &m_UsbPacket);
#ifdef DEBUG_PACKET
        log_spew("OHCI: status=%d\n", m_UsbPacket.Status);
#endif
		if (m_UsbPacket.Status == USB_RET_ASYNC) {
			m_UsbDevice->USB_DeviceFlushEPqueue(dev, ep);
			m_AsyncTD = addr;
			return 1;
		}
	}
	if (m_UsbPacket.Status == USB_RET_SUCCESS) {
		ret = m_UsbPacket.ActualLength;
	}
	else {
		ret = m_UsbPacket.Status;
	}

	if (ret >= 0) {
		if (direction == OHCI_TD_DIR_IN) {
			if (OHCI_CopyTD(&td, m_UsbBuffer, ret, true)) {
				OHCI_FatalError();
			}
#ifdef DEBUG_PACKET
#if LOG_LEVEL >= LOG_LEVEL_SPEW
            printf("  data:");
			for (i = 0; i < ret; i++)
				printf(" %.2x", m_UsbBuffer[i]);
			printf("\n");
#endif
#endif
		}
		else {
			ret = packetlen;
		}
	}

	if (ret >= 0) {
		if ((td.CurrentBufferPointer & 0xFFF) + ret > 0xFFF) {
			td.CurrentBufferPointer = (td.BufferEnd & ~0xFFF) + ((td.CurrentBufferPointer + ret) & 0xFFF);
		}
		else {
			td.CurrentBufferPointer += ret;
		}
	}

	// Writeback
	if ((unsigned int)ret == packetlen || (direction == OHCI_TD_DIR_IN && ret >= 0 && flag_r)) {
		// Transmission succeeded
		if ((unsigned int)ret == length) {
			td.CurrentBufferPointer = 0;
		}
		td.Flags |= OHCI_TD_T1;
		td.Flags ^= OHCI_TD_T0;
		OHCI_SET_BM(td.Flags, TD_CC, OHCI_CC_NOERROR);
		OHCI_SET_BM(td.Flags, TD_EC, 0);

		if ((direction != OHCI_TD_DIR_IN) && ((unsigned int)ret != length)) {
			// Partial packet transfer: TD not ready to retire yet
			goto exit_no_retire;
		}

		// Setting ED_C is part of the TD retirement process
		Ed->HeadP &= ~OHCI_ED_C;
		if (td.Flags & OHCI_TD_T0)
			Ed->HeadP |= OHCI_ED_C;
	}
	else {
		if (ret >= 0) {
            log_debug("OHCI: Underrun\n");
			OHCI_SET_BM(td.Flags, TD_CC, OHCI_CC_DATAUNDERRUN);
		}
		else {
			switch (ret) {
				case USB_RET_IOERROR:
				case USB_RET_NODEV:
                    log_debug("OHCI: Received DEV ERROR\n");
					OHCI_SET_BM(td.Flags, TD_CC, OHCI_CC_DEVICENOTRESPONDING);
					break;
				case USB_RET_NAK:
                    log_debug("OHCI: Received NAK\n");
					return 1;
				case USB_RET_STALL:
                    log_debug("OHCI: Received STALL\n");
					OHCI_SET_BM(td.Flags, TD_CC, OHCI_CC_STALL);
					break;
				case USB_RET_BABBLE:
                    log_debug("OHCI: Received BABBLE\n");
					OHCI_SET_BM(td.Flags, TD_CC, OHCI_CC_DATAOVERRUN);
					break;
				default:
                    log_debug("OHCI: Bad device response %d\n", ret);
					OHCI_SET_BM(td.Flags, TD_CC, OHCI_CC_UNDEXPETEDPID);
					OHCI_SET_BM(td.Flags, TD_EC, 3);
			}
		}
		Ed->HeadP |= OHCI_ED_H;
	}

	// Retire this TD
	Ed->HeadP &= ~OHCI_DPTR_MASK;
	Ed->HeadP |= td.NextTD & OHCI_DPTR_MASK;
	td.NextTD = m_Registers.HcDoneHead;
	m_Registers.HcDoneHead = addr;
	i = OHCI_BM(td.Flags, TD_DI);
    if (i < m_DoneCount) {
        m_DoneCount = i;
    }
    if (OHCI_BM(td.Flags, TD_CC) != OHCI_CC_NOERROR) {
        m_DoneCount = 0;
    }

exit_no_retire:
	if (OHCI_WriteTD(addr, &td)) {
		OHCI_FatalError();
		return 1;
	}
	return OHCI_BM(td.Flags, TD_CC) != OHCI_CC_NOERROR;
}

XboxDeviceState* OHCI::OHCI_FindDevice(uint8_t Addr)
{
    XboxDeviceState* dev;
	int i;

	for (i = 0; i < 4; i++) {
		if ((m_Registers.RhPort[i].HcRhPortStatus & OHCI_PORT_PES) == 0) {
			continue; // port is disabled
		}
		dev = m_UsbDevice->USB_FindDevice(&m_Registers.RhPort[i].UsbPort, Addr);
		if (dev != nullptr) {
			return dev; // return found device
		}
	}

	return nullptr;
}

void OHCI::OHCI_StateReset()
{
	// The usb state can be USB_Suspend if it is a software reset, and USB_Reset if it is a hardware
	// reset or cold boot

	OHCI_BusStop();
	m_OldHcControl = 0;

	// Reset all registers
	m_Registers.HcRevision = 0x10;
	m_Registers.HcControl = 0;
	m_Registers.HcCommandStatus = 0;
	m_Registers.HcInterruptStatus = 0;
	m_Registers.HcInterrupt = OHCI_INTR_MIE; // enable interrupts

	m_Registers.HcHCCA = 0;
	m_Registers.HcPeriodCurrentED = 0;
	m_Registers.HcControlHeadED = m_Registers.HcControlCurrentED = 0;
	m_Registers.HcBulkHeadED = m_Registers.HcBulkCurrentED = 0;
	m_Registers.HcDoneHead = 0;

	m_Registers.HcFmInterval = 0;
	m_Registers.HcFmInterval |= (0x2778 << 16); // TBD according to the standard, using what XQEMU sets (FSLargestDataPacket)
	m_Registers.HcFmInterval |= 0x2EDF; // bit-time of a frame. 1 frame = 1 ms (FrameInterval)
	m_Registers.HcFmRemaining = 0;
	m_Registers.HcFmNumber = 0;
	m_Registers.HcPeriodicStart = 0;
    m_Registers.HcLSThreshold = OHCI_LS_THRESH;

	m_Registers.HcRhDescriptorA = OHCI_RHA_NOCP | OHCI_RHA_NPS | 4; // The xbox lacks the hw to switch off the power on the ports and has 4 ports per HC
	m_Registers.HcRhDescriptorB = 0; // The attached devices are removable and use PowerSwitchingMode to control the power on the ports
    m_Registers.HcRhStatus = 0;

	m_DoneCount = 7;

	for (int i = 0; i < 4; i++)
	{
		OHCIPort* Port = &m_Registers.RhPort[i];
		Port->HcRhPortStatus = 0;
		if (Port->UsbPort.Dev && Port->UsbPort.Dev->Attached) {
			m_UsbDevice->USB_PortReset(&Port->UsbPort);
		}
	}
	if (m_AsyncTD) {
		m_UsbDevice->USB_CancelPacket(&m_UsbPacket);
		m_AsyncTD = 0;
	}

	OHCI_StopEndpoints();

	log_debug("OHCI: Reset mode event.\n");
}

void OHCI::OHCI_BusStart()
{
	// Create the EOF timer. Let's try a factor of 50 (1 virtual ms -> 50 real ms)
	m_pEOFtimer = Timer_Create(OHCI_FrameBoundaryWrapper, this, 50);

    log_debug("OHCI: Operational mode event\n");

	// SOF event
	OHCI_SOF(true);
}

void OHCI::OHCI_BusStop()
{
	if (m_pEOFtimer) {
		// Delete existing EOF timer
		Timer_Exit(m_pEOFtimer);
	}
	m_pEOFtimer = nullptr;
}

void OHCI::OHCI_SOF(bool bCreate)
{
	// set current SOF time
	m_SOFtime = GetTime_NS(m_pEOFtimer);

	// make timer expire at SOF + 1 virtual ms from now
	if (bCreate) {
		Timer_Start(m_pEOFtimer, m_UsbFrameTime);
	}

	OHCI_SetInterrupt(OHCI_INTR_SF);
}

void OHCI::OHCI_ChangeState(uint32_t Value)
{
	uint32_t OldState = m_Registers.HcControl & OHCI_CTL_HCFS;
	m_Registers.HcControl = Value;
	uint32_t NewState = m_Registers.HcControl & OHCI_CTL_HCFS;

	// no state change
	if (OldState == NewState) {
		return;
	}

	switch (NewState)
	{
		case Operational:
			OHCI_BusStart();
			break;

		case Suspend:
			OHCI_BusStop();
            log_debug("OHCI: Suspend mode event\n");
			break;

		case Resume:
            log_debug("OHCI: Resume mode event\n");
			break;

		case Reset:
			OHCI_StateReset();
			break;

		default:
            log_warning("OHCI: Unknown USB mode!\n");
	}
}

void OHCI::OHCI_PacketInit(USBPacket* packet)
{
	IOVector* vec = &packet->IoVec;
	vec->IoVecStruct = new IoVec;
	vec->IoVecNumber = 0;
	vec->AllocNumber = 1;
	vec->Size = 0;
}

uint32_t OHCI::OHCI_ReadRegister(uint32_t Addr)
{
	uint32_t ret = 0xFFFFFFFF;

	if (Addr & 3) {
		// The standard allows only aligned reads to the registers
        log_debug("OHCI: Unaligned read. Ignoring.\n");
		return ret;
	}
	else {
		switch (Addr >> 2) // read the register
		{
			case 0: // HcRevision
				ret = m_Registers.HcRevision;
				break;

			case 1: // HcControl
				ret = m_Registers.HcControl;
				break;

			case 2: // HcCommandStatus
				ret = m_Registers.HcCommandStatus;
				break;

			case 3: // HcInterruptStatus
				ret = m_Registers.HcInterruptStatus;
				break;

			case 4: // HcInterruptEnable
			case 5: // HcInterruptDisable
				ret = m_Registers.HcInterrupt;
				break;

			case 6: // HcHCCA
				ret = m_Registers.HcHCCA;
				break;

			case 7: // HcPeriodCurrentED
				ret = m_Registers.HcPeriodCurrentED;
				break;

			case 8: // HcControlHeadED
				ret = m_Registers.HcControlHeadED;
				break;

			case 9: // HcControlCurrentED
				ret = m_Registers.HcControlCurrentED;
				break;

			case 10: // HcBulkHeadED
				ret = m_Registers.HcBulkHeadED;
				break;

			case 11: // HcBulkCurrentED
				ret = m_Registers.HcBulkCurrentED;
				break;

			case 12: // HcDoneHead
				ret = m_Registers.HcDoneHead;
				break;

			case 13: // HcFmInterval
				ret = m_Registers.HcFmInterval;
				break;

			case 14: // HcFmRemaining
				ret = OHCI_GetFrameRemaining();
				break;

			case 15: // HcFmNumber
				ret = m_Registers.HcFmNumber;
				break;

			case 16: // HcPeriodicStart
				ret = m_Registers.HcPeriodicStart;
				break;

			case 17: // HcLSThreshold
				ret = m_Registers.HcLSThreshold;
				break;

			case 18: // HcRhDescriptorA
				ret = m_Registers.HcRhDescriptorA;
				break;

			case 19: // HcRhDescriptorB
				ret = m_Registers.HcRhDescriptorB;
				break;

			case 20: // HcRhStatus
				ret = m_Registers.HcRhStatus;
				break;

			// Always report that the port power is on since the Xbox cannot switch off the electrical current to it
			case 21: // RhPort 0
				ret = m_Registers.RhPort[0].HcRhPortStatus | OHCI_PORT_PPS;
				break;

			case 22: // RhPort 1
				ret = m_Registers.RhPort[1].HcRhPortStatus | OHCI_PORT_PPS;
				break;

            case 23: // RhPort 2
                ret = m_Registers.RhPort[2].HcRhPortStatus | OHCI_PORT_PPS;
                break;

            case 24: // RhPort 3
                ret = m_Registers.RhPort[3].HcRhPortStatus | OHCI_PORT_PPS;
                break;

			default:
                log_warning("OHCI: Read register operation with bad offset %u. Ignoring.\n", Addr >> 2);
		}
		return ret;
	}
}

void OHCI::OHCI_WriteRegister(uint32_t Addr, uint32_t Value)
{
	if (Addr & 3) {
		// The standard allows only aligned writes to the registers
        log_debug("OHCI: Unaligned write. Ignoring.\n");
		return;
	}
	else {
		switch (Addr >> 2)
		{
			case 0: // HcRevision
				// This register is read-only
				break;

			case 1: // HcControl
				OHCI_ChangeState(Value);
				break;

			case 2: // HcCommandStatus
			{
				// SOC is read-only
				Value &= ~OHCI_STATUS_SOC;

				// From the standard: "The Host Controller must ensure that bits written as 1 become set
				// in the register while bits written as 0 remain unchanged in the register."
				m_Registers.HcCommandStatus |= Value;

				if (m_Registers.HcCommandStatus & OHCI_STATUS_HCR) {
					// Do a hardware reset
					OHCI_StateReset();
				}
			}
			break;

			case 3: // HcInterruptStatus
				m_Registers.HcInterruptStatus &= ~Value;
				OHCI_UpdateInterrupt();
				break;

			case 4: // HcInterruptEnable
				m_Registers.HcInterrupt |= Value;
				OHCI_UpdateInterrupt();
				break;

			case 5: // HcInterruptDisable
				m_Registers.HcInterrupt &= ~Value;
				OHCI_UpdateInterrupt();
				break;

			case 6: // HcHCCA
				// The standard says the minimum alignment is 256 bytes and so bits 0 through 7 are always zero
				m_Registers.HcHCCA = Value & OHCI_HCCA_MASK;
				break;

			case 7: // HcPeriodCurrentED
				// This register is read-only
				break;

			case 8: // HcControlHeadED
				m_Registers.HcControlHeadED = Value & OHCI_DPTR_MASK;
				break;

			case 9: // HcControlCurrentED
				m_Registers.HcControlCurrentED = Value & OHCI_DPTR_MASK;
				break;

			case 10: // HcBulkHeadED
				m_Registers.HcBulkHeadED = Value & OHCI_DPTR_MASK;
				break;

			case 11: // HcBulkCurrentED
				m_Registers.HcBulkCurrentED = Value & OHCI_DPTR_MASK;
				break;

			case 12: // HcDoneHead
				// This register is read-only
				break;

			case 13: // HcFmInterval
			{
				if ((Value & OHCI_FMI_FIT) != (m_Registers.HcFmInterval & OHCI_FMI_FIT)) {
                    log_debug("OHCI: Changing frame interval duration. New value is %u\n", Value & OHCI_FMI_FI);
				}
				m_Registers.HcFmInterval = Value & ~0xC000;
			}
			break;

			case 14: // HcFmRemaining
				// This register is read-only
				break;

			case 15: // HcFmNumber
				// This register is read-only
				break;

			case 16: // HcPeriodicStart
				m_Registers.HcPeriodicStart = Value & 0x3FFF;
				break;

			case 17: // HcLSThreshold
				m_Registers.HcLSThreshold = Value & 0xFFF;
				break;

			case 18: // HcRhDescriptorA
				m_Registers.HcRhDescriptorA &= ~OHCI_RHA_RW_MASK;
				m_Registers.HcRhDescriptorA |= Value & OHCI_RHA_RW_MASK; // ??
				break;

			case 19: // HcRhDescriptorB
				// Don't do anything, the attached devices are all removable and PowerSwitchingMode is always 0
				break;

			case 20: // HcRhStatus
				OHCI_SetHubStatus(Value);
				break;

			case 21: // RhPort 0
				OHCI_PortSetStatus(0, Value);
				break;

			case 22: // RhPort 1
				OHCI_PortSetStatus(1, Value);
				break;

            case 23: // RhPort 2
                OHCI_PortSetStatus(2, Value);
                break;

            case 24: // RhPort 3
                OHCI_PortSetStatus(3, Value);
                break;

			default:
                log_warning("OHCI: Write register operation with bad offset %u. Ignoring.\n", Addr >> 2);
		}
	}
}

void OHCI::OHCI_UpdateInterrupt()
{
    // TODO: interrupts
	if ((m_Registers.HcInterrupt & OHCI_INTR_MIE) && (m_Registers.HcInterruptStatus & m_Registers.HcInterrupt)) {
        //HalSystemInterrupts[m_IrqNum].Assert(false);
        //HalSystemInterrupts[m_IrqNum].Assert(true);
	}
}

void OHCI::OHCI_SetInterrupt(uint32_t Value)
{
	m_Registers.HcInterruptStatus |= Value;
	OHCI_UpdateInterrupt();
}

uint32_t OHCI::OHCI_GetFrameRemaining()
{
	uint16_t frame;
	uint64_t ticks;

	if ((m_Registers.HcControl & OHCI_CTL_HCFS) != Operational) {
		return m_Registers.HcFmRemaining & OHCI_FMR_FRT;
	}

	// Being in USB operational state guarantees that m_pEOFtimer and m_SOFtime were set already
	ticks = GetTime_NS(m_pEOFtimer) - m_SOFtime;

	// Avoid Muldiv64 if possible
	if (ticks >= m_UsbFrameTime) {
		return m_Registers.HcFmRemaining & OHCI_FMR_FRT;
	}

	ticks = Muldiv64(1, ticks, m_TicksPerUsbTick);
	frame = static_cast<uint16_t>((m_Registers.HcFmInterval & OHCI_FMI_FI) - ticks);

	return (m_Registers.HcFmRemaining & OHCI_FMR_FRT) | frame;
}

void OHCI::OHCI_StopEndpoints()
{
    XboxDeviceState* dev;
	int i, j;

	for (i = 0; i < 4; i++) {
		dev = m_Registers.RhPort[i].UsbPort.Dev;
		if (dev && dev->Attached) {
			m_UsbDevice->USB_DeviceEPstopped(dev, &dev->EP_ctl);
			for (j = 0; j < USB_MAX_ENDPOINTS; j++) {
				m_UsbDevice->USB_DeviceEPstopped(dev, &dev->EP_in[j]);
				m_UsbDevice->USB_DeviceEPstopped(dev, &dev->EP_out[j]);
			}
		}
	}
}

void OHCI::OHCI_SetHubStatus(uint32_t Value)
{
	uint32_t old_state;

	old_state = m_Registers.HcRhStatus;

	// write 1 to clear OCIC
	if (Value & OHCI_RHS_OCIC) {
		m_Registers.HcRhStatus &= ~OHCI_RHS_OCIC;
	}

	if (Value & OHCI_RHS_LPS) {
		int i;

		for (i = 0; i < 4; i++) {
			OHCI_PortPower(i, 0);
		}	
        log_debug("OHCI: powered down all ports\n");
	}

	if (Value & OHCI_RHS_LPSC) {
		int i;

		for (i = 0; i < 4; i++) {
			OHCI_PortPower(i, 1);
		}	
        log_debug("OHCI: powered up all ports\n");
	}

	if (Value & OHCI_RHS_DRWE) {
		m_Registers.HcRhStatus |= OHCI_RHS_DRWE;
	}

	if (Value & OHCI_RHS_CRWE) {
		m_Registers.HcRhStatus &= ~OHCI_RHS_DRWE;
	}

	if (old_state != m_Registers.HcRhStatus) {
		OHCI_SetInterrupt(OHCI_INTR_RHSC);
	}	
}

void OHCI::OHCI_PortPower(int i, int p)
{
	if (p) {
		m_Registers.RhPort[i].HcRhPortStatus |= OHCI_PORT_PPS;
	}
	else {
		m_Registers.RhPort[i].HcRhPortStatus &= ~(OHCI_PORT_PPS |
			OHCI_PORT_CCS |
			OHCI_PORT_PSS |
			OHCI_PORT_PRS);
	}
}

void OHCI::OHCI_PortSetStatus(int PortNum, uint32_t Value)
{
	uint32_t old_state;
	OHCIPort* port;

	port = &m_Registers.RhPort[PortNum];
	old_state = port->HcRhPortStatus;

	// Write to clear CSC, PESC, PSSC, OCIC, PRSC
	if (Value & OHCI_PORT_WTC) {
		port->HcRhPortStatus &= ~(Value & OHCI_PORT_WTC);
	}

	if (Value & OHCI_PORT_CCS) {
		port->HcRhPortStatus &= ~OHCI_PORT_PES;
	}

	OHCI_PortSetIfConnected(PortNum, Value & OHCI_PORT_PES);

	if (OHCI_PortSetIfConnected(PortNum, Value & OHCI_PORT_PSS)) {
        log_debug("OHCI: port %d: SUSPEND\n", PortNum);
	}

	if (OHCI_PortSetIfConnected(PortNum, Value & OHCI_PORT_PRS)) {
        log_debug("OHCI: port %d: RESET\n", PortNum);
		m_UsbDevice->USB_DeviceReset(port->UsbPort.Dev);
		port->HcRhPortStatus &= ~OHCI_PORT_PRS;
		// ??? Should this also set OHCI_PORT_PESC
		port->HcRhPortStatus |= OHCI_PORT_PES | OHCI_PORT_PRSC;
	}

	// Invert order here to ensure in ambiguous case, device is powered up...
	if (Value & OHCI_PORT_LSDA) {
		OHCI_PortPower(PortNum, 0);
	}

	if (Value & OHCI_PORT_PPS) {
		OHCI_PortPower(PortNum, 1);
	}	

	if (old_state != port->HcRhPortStatus) {
		OHCI_SetInterrupt(OHCI_INTR_RHSC);
	}
}

int OHCI::OHCI_PortSetIfConnected(int i, uint32_t Value)
{
	int ret = 1;

	// writing a 0 has no effect
	if (Value == 0) {
		return 0;
	}

	// If CurrentConnectStatus is cleared we set ConnectStatusChange
	if (!(m_Registers.RhPort[i].HcRhPortStatus & OHCI_PORT_CCS)) {
		m_Registers.RhPort[i].HcRhPortStatus |= OHCI_PORT_CSC;
		if (m_Registers.HcRhStatus & OHCI_RHS_DRWE) {
			// from XQEMU: TODO: CSC is a wakeup event
		}
		return 0;
	}

	if (m_Registers.RhPort[i].HcRhPortStatus & Value) {
		ret = 0;
	}	

	// set the bit
	m_Registers.RhPort[i].HcRhPortStatus |= Value;

	return ret;
}

void OHCI::OHCI_Detach(USBPort* Port)
{
	OHCIPort* port = &m_Registers.RhPort[Port->PortIndex];
	uint32_t old_state = port->HcRhPortStatus;

	OHCI_AsyncCancelDevice(Port->Dev);

	// set connect status
	if (port->HcRhPortStatus & OHCI_PORT_CCS) {
		port->HcRhPortStatus &= ~OHCI_PORT_CCS;
		port->HcRhPortStatus |= OHCI_PORT_CSC;
	}

	// disable port
	if (port->HcRhPortStatus & OHCI_PORT_PES) {
		port->HcRhPortStatus &= ~OHCI_PORT_PES;
		port->HcRhPortStatus |= OHCI_PORT_PESC;
	}

    log_debug("OHCI: Detached port %d\n", Port->PortIndex);

	if (old_state != port->HcRhPortStatus) {
		OHCI_SetInterrupt(OHCI_INTR_RHSC);
	}
}

void OHCI::OHCI_Attach(USBPort* Port)
{
	OHCIPort* port = &m_Registers.RhPort[Port->PortIndex];
	uint32_t old_state = port->HcRhPortStatus;

	// set connect status
	port->HcRhPortStatus |= OHCI_PORT_CCS | OHCI_PORT_CSC;

	// update speed
	if (port->UsbPort.Dev->Speed == USB_SPEED_LOW) {
		port->HcRhPortStatus |= OHCI_PORT_LSDA;
	}
	else {
		port->HcRhPortStatus &= ~OHCI_PORT_LSDA;
	}

	// notify of remote-wakeup
	if ((m_Registers.HcControl & OHCI_CTL_HCFS) == Suspend) {
		OHCI_SetInterrupt(OHCI_INTR_RD);
	}

    log_debug("OHCI: Attached port %d", Port->PortIndex);

	if (old_state != port->HcRhPortStatus) {
		OHCI_SetInterrupt(OHCI_INTR_RHSC);
	}
}

void OHCI::OHCI_ChildDetach(XboxDeviceState* child) {
    OHCI_AsyncCancelDevice(child);
}

void OHCI::OHCI_Wakeup(USBPort* port1) {
    OHCIPort* port = &m_Registers.RhPort[port1->PortIndex];
    uint32_t intr = 0;
    if (port->HcRhPortStatus & OHCI_PORT_PSS) {
        log_debug("OHCI: port %d: wakeup", port1->PortIndex);
        port->HcRhPortStatus |= OHCI_PORT_PSSC;
        port->HcRhPortStatus &= ~OHCI_PORT_PSS;
        intr = OHCI_INTR_RHSC;
    }
    // Note that the controller can be suspended even if this port is not
    if ((m_Registers.HcControl & OHCI_CTL_HCFS) == Suspend) {
        log_debug("OHCI: remote-wakeup: SUSPEND->RESUME");
        // From the standard: "The only interrupts possible in the USBSUSPEND state are ResumeDetected (the
        // Host Controller will have changed the HostControllerFunctionalState to the USBRESUME state)
        // and OwnershipChange."
        m_Registers.HcControl &= ~OHCI_CTL_HCFS;
        m_Registers.HcControl |= Resume;
        intr = OHCI_INTR_RD;
    }
    OHCI_SetInterrupt(intr);
}

void OHCI::OHCI_AsyncCompletePacket(USBPort* port, USBPacket* packet) {
#ifdef DEBUG_PACKET
    log_spew("OHCI: Async packet complete");
#endif
    m_AsyncComplete = 1;
    OHCI_ProcessLists(1);
}

void OHCI::OHCI_AsyncCancelDevice(XboxDeviceState* dev)
{
	if (m_AsyncTD &&
		m_UsbDevice->USB_IsPacketInflight(&m_UsbPacket) &&
		m_UsbPacket.Endpoint->Dev == dev) {
		m_UsbDevice->USB_CancelPacket(&m_UsbPacket);
        m_AsyncTD = 0;
	}
}

void OHCI::OHCI_ProcessLists(int completion)
{
    // Only process the control list if it is enabled (HcControl) and has available TD's (HcCommandStatus)
    if ((m_Registers.HcControl & OHCI_CTL_CLE) && (m_Registers.HcCommandStatus & OHCI_STATUS_CLF)) {
        if (m_Registers.HcControlCurrentED && m_Registers.HcControlCurrentED != m_Registers.HcControlHeadED) {
            log_debug("OHCI: head 0x%X, current 0x%X\n",
                m_Registers.HcControlHeadED, m_Registers.HcControlCurrentED);
        }
        if (!OHCI_ServiceEDlist(m_Registers.HcControlHeadED, completion)) {
            m_Registers.HcControlCurrentED = 0;
            m_Registers.HcCommandStatus &= ~OHCI_STATUS_CLF;
        }
    }

    // Only process the bulk list if it is enabled (HcControl) and has available TD's (HcCommandStatus)
    if ((m_Registers.HcControl & OHCI_CTL_BLE) && (m_Registers.HcCommandStatus & OHCI_STATUS_BLF)) {
        if (!OHCI_ServiceEDlist(m_Registers.HcBulkHeadED, completion)) {
            m_Registers.HcBulkCurrentED = 0;
            m_Registers.HcCommandStatus &= ~OHCI_STATUS_BLF;
        }
    }
}


int OHCI::OHCI_ServiceIsoTD(OHCI_ED* ed, int completion)
{
	int dir;
	size_t len = 0;
#ifdef DEBUG_ISOCH
	const char *str = nullptr;
#endif
	int pid;
	int ret;
	int i;
    XboxDeviceState* dev;
	USBEndpoint* ep;
	OHCI_ISO_TD iso_td;
	uint32_t addr;
	uint16_t starting_frame;
	int16_t relative_frame_number;
	int frame_count;
	uint32_t start_offset, next_offset, end_offset = 0;
	uint32_t start_addr, end_addr;

	addr = ed->HeadP & OHCI_DPTR_MASK;

	if (OHCI_ReadIsoTD(addr, &iso_td)) {
		log_debug("OHCI: ISO_TD read error at physical address 0x%X\n", addr);
		OHCI_FatalError();
		return 0;
	}

	starting_frame = OHCI_BM(iso_td.Flags, TD_SF);
	frame_count = OHCI_BM(iso_td.Flags, TD_FC);
	// From the standard: "The Host Controller does an unsigned subtraction of StartingFrame from the 16 bits of
	// HcFmNumber to arrive at a signed value for a relative frame number (frame R)."
	relative_frame_number = USUB(m_Registers.HcFmNumber & 0xFFFF, starting_frame);

#ifdef DEBUG_ISOCH
	log_spew("OHCI: --- ISO_TD ED head 0x%.8x tailp 0x%.8x\n"
		"0x%.8x 0x%.8x 0x%.8x 0x%.8x\n"
		"0x%.8x 0x%.8x 0x%.8x 0x%.8x\n"
		"0x%.8x 0x%.8x 0x%.8x 0x%.8x\n"
		"frame_number 0x%.8x starting_frame 0x%.8x\n"
		"frame_count  0x%.8x relative %d\n"
		"di 0x%.8x cc 0x%.8x\n",
		ed->HeadP  & OHCI_DPTR_MASK, ed->TailP & OHCI_DPTR_MASK,
        iso_td.Flags, iso_td.BufferPage0, iso_td.NextTD, iso_td.BufferEnd,
        iso_td.Offset[0], iso_td.Offset[1], iso_td.Offset[2], iso_td.Offset[3],
        iso_td.Offset[4], iso_td.Offset[5], iso_td.Offset[6], iso_td.Offset[7],
        m_Registers.HcFmNumber, starting_frame,
		frame_count, relative_frame_number,
		OHCI_BM(iso_td.Flags, TD_DI), OHCI_BM(iso_td.Flags, TD_CC));
#endif

	if (relative_frame_number < 0) {
		// From the standard: "If the relative frame number is negative, then the current frame is earlier than the 0th frame
		// of the Isochronous TD and the Host Controller advances to the next ED."
        log_debug("OHCI: ISO_TD R=%d < 0\n", relative_frame_number);
		return 1;
	}
	else if (relative_frame_number > frame_count) {
		// From the standard: "If the relative frame number is greater than
		// FrameCount, then the Isochronous TD has expired and a error condition exists."
        log_debug("OHCI: ISO_TD R=%d > FC=%d\n", relative_frame_number, frame_count);
		OHCI_SET_BM(iso_td.Flags, TD_CC, OHCI_CC_DATAOVERRUN);
		ed->HeadP &= ~OHCI_DPTR_MASK;
		ed->HeadP |= (iso_td.NextTD & OHCI_DPTR_MASK);
		iso_td.NextTD = m_Registers.HcDoneHead;
		m_Registers.HcDoneHead = addr;
		i = OHCI_BM(iso_td.Flags, TD_DI);
		if (i < m_DoneCount) {
			m_DoneCount = i;
		}
		if (OHCI_WriteIsoTD(addr, &iso_td)) {
			OHCI_FatalError();
			return 1;
		}
		return 0;
	}

	// From the standard: "If the relative frame number is between 0 and FrameCount, then the Host Controller issues
	// a token to the endpoint and attempts a data transfer using the buffer described by the Isochronous TD."	

	dir = OHCI_BM(ed->Flags, ED_D);
	switch (dir) {
		case OHCI_TD_DIR_IN:
#ifdef DEBUG_ISOCH
			str = "in";
#endif
			pid = USB_TOKEN_IN;
			break;
		case OHCI_TD_DIR_OUT:
#ifdef DEBUG_ISOCH
			str = "out";
#endif
			pid = USB_TOKEN_OUT;
			break;
		case OHCI_TD_DIR_SETUP:
#ifdef DEBUG_ISOCH
			str = "setup";
#endif
			pid = USB_TOKEN_SETUP;
			break;
		default:
			log_warning("OHCI: Bad direction %d\n", dir);
			return 1;
	}

	if (!iso_td.BufferPage0 || !iso_td.BufferEnd) {
        log_debug("OHCI: ISO_TD bp 0x%.8X be 0x%.8X\n", iso_td.BufferPage0, iso_td.BufferEnd);
		return 1;
	}

	start_offset = iso_td.Offset[relative_frame_number];
	next_offset = iso_td.Offset[relative_frame_number + 1];

	// From the standard: "If the Host Controller supports checking of the Offsets, if either Offset[R] or Offset[R+1] does
	// not have a ConditionCode of NOT ACCESSED or if the Offset[R + 1] is not greater than or equal to Offset[R], then
	// an Unrecoverable Error is indicated."
	// ergo720: I have a doubt here: according to the standard, the error condition is set if ConditionCode (bits 12-15 of
	// Offset[R(+1)] is not 111x (= NOT ACCESSED), however the check below is only triggered if the bits are all zeros
	// (= NO ERROR). So, if, for example, these bits are 1100 (= BUFFER OVERRUN), the check won't be triggered when actually
	// it should be

	if (!(OHCI_BM(start_offset, TD_PSW_CC) & 0xE) ||
		((relative_frame_number < frame_count) &&
			!(OHCI_BM(next_offset, TD_PSW_CC) & 0xE))) {
        log_debug("OHCI: ISO_TD cc != not accessed 0x%.8x 0x%.8x\n", start_offset, next_offset);
		return 1;
	}

	if ((relative_frame_number < frame_count) && (start_offset > next_offset)) {
        log_spew("OHCI: ISO_TD start_offset=0x%.8x > next_offset=0x%.8x\n", start_offset, next_offset);
		return 1;
	}

	// From the standard: "Bit 12 of offset R then selects the upper 20 bits of the physical address
	// as either BufferPage0 when bit 12 = 0 or the upper 20 bits of BufferEnd when bit 12 = 1."
		
	if ((start_offset & 0x1000) == 0) {
		start_addr = (iso_td.BufferPage0 & OHCI_PAGE_MASK) |
			(start_offset & OHCI_OFFSET_MASK);
	}
	else {
		start_addr = (iso_td.BufferEnd & OHCI_PAGE_MASK) |
			(start_offset & OHCI_OFFSET_MASK);
	}

	// From the standard: "If the data packet is not the last in an Isochronous TD (R not equal to FrameCount),
	// then the ending address of the buffer is found by using Offset[R + 1] - 1. This value is then used to create a
	// physical address in the same manner as the Offset[R] was used to create the starting physical address."	

	if (relative_frame_number < frame_count) {
		end_offset = next_offset - 1;
		if ((end_offset & 0x1000) == 0) {
			end_addr = (iso_td.BufferPage0 & OHCI_PAGE_MASK) |
				(end_offset & OHCI_OFFSET_MASK);
		}
		else {
			end_addr = (iso_td.BufferEnd & OHCI_PAGE_MASK) |
				(end_offset & OHCI_OFFSET_MASK);
		}
	}
	else {
		// From the standard: "If, however, the data packet is the last in an Isochronous TD(R = FrameCount),
		// then the value of BufferEnd is the address of the last byte in the buffer."	
		end_addr = iso_td.BufferEnd;
	}

	if ((start_addr & OHCI_PAGE_MASK) != (end_addr & OHCI_PAGE_MASK)) {
		len = (end_addr & OHCI_OFFSET_MASK) + 0x1001
			- (start_addr & OHCI_OFFSET_MASK);
	}
	else {
		len = end_addr - start_addr + 1;
	}

	if (len && dir != OHCI_TD_DIR_IN) {
		if (OHCI_CopyIsoTD(start_addr, end_addr, m_UsbBuffer, len, false)) {
			OHCI_FatalError();
			return 1;
		}
	}

	if (!completion) {
		bool int_req = relative_frame_number == frame_count && OHCI_BM(iso_td.Flags, TD_DI) == 0;
		dev = OHCI_FindDevice(OHCI_BM(ed->Flags, ED_FA));
		ep = m_UsbDevice->USB_GetEP(dev, pid, OHCI_BM(ed->Flags, ED_EN));
		m_UsbDevice->USB_PacketSetup(&m_UsbPacket, pid, ep, 0, addr, false, int_req);
		m_UsbDevice->USB_PacketAddBuffer(&m_UsbPacket, m_UsbBuffer, len);
		m_UsbDevice->USB_HandlePacket(dev, &m_UsbPacket);
		if (m_UsbPacket.Status == USB_RET_ASYNC) {
			m_UsbDevice->USB_DeviceFlushEPqueue(dev, ep);
			return 1;
		}
	}
	if (m_UsbPacket.Status == USB_RET_SUCCESS) {
		ret = m_UsbPacket.ActualLength;
	}
	else {
		ret = m_UsbPacket.Status;
	}

#ifdef DEBUG_ISOCH
	log_spew("OHCI: so 0x%.8x eo 0x%.8x\nsa 0x%.8x ea 0x%.8x\ndir %s len %zu ret %d\n",
		start_offset, end_offset, start_addr, end_addr, str, len, ret);
#endif

	// From the standard: "After each data packet transfer, the Rth Offset is replaced with a value that indicates the status of
	// the data packet transfer.The upper 4 bits of the value are the ConditionCode for the transfer and the lower 12 bits
	// represent the size of the transfer.Together, these two fields constitute the Packet Status Word(PacketStatusWord)."
	
    // Writeback
	if (dir == OHCI_TD_DIR_IN && ret >= 0 && (unsigned int)ret <= len) {
		// IN transfer succeeded
		if (OHCI_CopyIsoTD(start_addr, end_addr, m_UsbBuffer, ret, true)) {
			OHCI_FatalError();
			return 1;
		}
		OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_NOERROR);
		OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_SIZE, ret);
	}
	else if (dir == OHCI_TD_DIR_OUT && (unsigned int)ret == len) {
		// OUT transfer succeeded
		OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_NOERROR);
		OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_SIZE, 0);
	}
	else {
		// Handle the error condition
		if (ret > static_cast<ptrdiff_t>(len)) { // Sequence Error
            log_debug("OHCI: DataOverrun %d > %zu\n", ret, len);
			OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_DATAOVERRUN);
			OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_SIZE, len);
		}
		else if (ret >= 0) { // Sequence Error
            log_debug("OHCI: DataUnderrun %d\n", ret);
			OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_DATAUNDERRUN);
		}
		else {
			switch (ret) {
				case USB_RET_IOERROR: // Transmission Errors
				case USB_RET_NODEV:
					OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_DEVICENOTRESPONDING);
					OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_SIZE, 0);
					break;
				case USB_RET_NAK: // NAK and STALL
				case USB_RET_STALL:
                    log_debug("OHCI: got NAK/STALL %d\n", ret);
					OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_STALL);
					OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_SIZE, 0);
					break;
				default: // Unknown Error
                    log_debug("OHCI: Bad device response %d\n", ret);
					OHCI_SET_BM(iso_td.Offset[relative_frame_number], TD_PSW_CC, OHCI_CC_UNDEXPETEDPID);
					break;
			}
		}
	}

	if (relative_frame_number == frame_count) {
		// Last data packet of ISO TD - retire the TD to the Done Queue
		OHCI_SET_BM(iso_td.Flags, TD_CC, OHCI_CC_NOERROR);
		ed->HeadP &= ~OHCI_DPTR_MASK;
		ed->HeadP |= (iso_td.NextTD & OHCI_DPTR_MASK);
		iso_td.NextTD = m_Registers.HcDoneHead;
		m_Registers.HcDoneHead = addr;
		i = OHCI_BM(iso_td.Flags, TD_DI);
		if (i < m_DoneCount) {
			m_DoneCount = i;
		}
	}
	if (OHCI_WriteIsoTD(addr, &iso_td)) {
		OHCI_FatalError();
	}
	return 1;
}

}
