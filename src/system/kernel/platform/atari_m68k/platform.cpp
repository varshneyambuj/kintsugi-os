/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Atari kernel platform code.
 */

/**
 * @file platform.cpp
 * @brief Atari (m68k) kernel platform implementation.
 *
 * Provides the concrete M68KPlatform subclass for Atari/Falcon-class
 * machines, wrapping the MFP interrupt controller(s), the MC146818A RTC,
 * the IKBD keyboard used for the blue screen console, and the ARAnyM
 * NatFeatures hooks used for debug output. Exposes initialization,
 * interrupt enable/ack, RTC access, and a hardware timer driven from
 * the ST MFP.
 */

#include <arch_platform.h>

#include <new>
#include <util/kernel_cpp.h>

#include <KernelExport.h>

#include <boot/kernel_args.h>
#include <arch/cpu.h>
//#include <platform/openfirmware/openfirmware.h>
#include <platform/atari_m68k/MFP.h>
#include <platform/atari_m68k/platform_atari_m68k.h>
#include <real_time_clock.h>
#include <timer.h>

#include "debugger_keymaps.h"

/* which MFP timer to use */
#define SYS_TDR		MFP_TADR
#define SYS_TCR		MFP_TACR
#define SYS_TCRMASK	0x0f	/* mask for timer control (0xf for A&B, 0x7 for C, 0x38 for D) */
#define SYS_TENABLE	0x01	/* delay mode with /4 prescaler: 0x01 (<<3 for timer D) */
#define SYS_TDISABLE	0x00
#define SYS_TVECTOR	13
#define MFP_PRESCALER	4

/* used for timer interrupt */
#define MFP_TIMER_RATE	(MFP_FREQ/MFP_PRESCALER)
#define MFP_MAX_TIMER_INTERVAL	(0xff * 1000000L / MFP_TIMER_RATE)

/* used for system_time() calculation */
#define MFP_SYSTEM_TIME_RATE	(MFP_FREQ/MFP_PRESCALER)


#define MFP0_BASE	0xFFFFFA00
#define MFP1_BASE	0xFFFFFA80

#define MFP0_VECTOR_BASE	64
#define MFP1_VECTOR_BASE	(MFP0_VECTOR_BASE+16)

#define TT_RTC_BASE	0xFFFF8960

#define TT_RTC_VECTOR	(MFP1_VECTOR_BASE+14)

// ?
#define SCC_C0_VECTOR_BASE	(MFP1_VECTOR_BASE+16)
// ??
#define SCC_C1_VECTOR_BASE	(0x1BC/4)

#define IKBD_BASE	0xFFFFFC00
#define IKBD_CTRL	0
#define IKBD_DATA	2
#define IKBD_STATUS_READ_BUFFER_FULL	0x01

// keyboard scancodes, very much like PC ones
// see
// http://www.classiccmp.org/dunfield/atw800/h/atw800k.jpg
// ST Mag Nr 57 page 55
enum keycodes {
	LEFT_SHIFT		= 42,
	RIGHT_SHIFT		= 54,

	LEFT_CONTROL	= 29,

	LEFT_ALT		= 56,

	CURSOR_LEFT		= 75,
	CURSOR_RIGHT	= 77,
	CURSOR_UP		= 72,
	CURSOR_DOWN		= 80,
	CURSOR_HOME		= 71,
	CURSOR_END		= 79,	// not on real atari keyboard
	PAGE_UP			= 73,	// not on real atari keyboard XXX remap Help ?
	PAGE_DOWN		= 81,	// not on real atari keyboard XXX remap Undo ?

	DELETE			= 83,
	F12				= 88,	// but it's shifted

};

#define in8(a)  (*(volatile uint8 *)(a))
#define out8(v, a)  (*(volatile uint8 *)(a) = v)


namespace BPrivate {

//class MfpPIC;

// #pragma mark - Atari (Falcon)


class M68KAtari : public M68KPlatform {
public:
	class MFP {
	public:
		MFP(uint32 base, int vector);
		~MFP();

		uint32 Base() const { return fBase; };
		int Vector() const { return fVector; };

		uint8 ReadReg(uint32 reg) { return in8(fBase + reg); };
		void WriteReg(uint32 reg, uint8 v) { out8(v, fBase + reg); };

		void EnableIOInterrupt(int32 irq);
		void DisableIOInterrupt(int32 irq);
		bool AcknowledgeIOInterrupt(int32 irq);

	private:
		uint32 fBase;
		int fVector;
	};

	class RTC {
	public:
		RTC(uint32 base, int vector);
		~RTC();

		uint32 Base() const { return fBase; };
		int Vector() const { return fVector; };

		uint8 ReadReg(uint32 reg);
		void WriteReg(uint32 reg, uint8 v) { out8((uint8)reg,fBase+1); out8(v,fBase+3); };

	private:
		uint32 fBase;
		int fVector;
	};

	M68KAtari();
	virtual ~M68KAtari();

	void ProbeHardware(struct kernel_args *kernelArgs);

	virtual status_t Init(struct kernel_args *kernelArgs);
	virtual status_t InitSerialDebug(struct kernel_args *kernelArgs);
	virtual status_t InitPostVM(struct kernel_args *kernelArgs);
	virtual status_t InitPIC(struct kernel_args *kernelArgs);
	virtual status_t InitRTC(struct kernel_args *kernelArgs,
		struct real_time_data *data);
	virtual status_t InitTimer(struct kernel_args *kernelArgs);

	virtual char BlueScreenGetChar();

	virtual char SerialDebugGetChar();
	virtual void SerialDebugPutChar(char c);

	virtual void EnableIOInterrupt(int32 irq);
	virtual void DisableIOInterrupt(int32 irq);
	virtual bool AcknowledgeIOInterrupt(int32 irq);

	virtual	uint8 ReadRTCReg(uint8 reg);
	virtual	void WriteRTCReg(uint8 reg, uint8 val);
	virtual	void SetHardwareRTC(uint64 seconds);
	virtual	uint32 GetHardwareRTC();

	virtual void SetHardwareTimer(bigtime_t timeout);
	virtual void ClearHardwareTimer(void);

	virtual	void ShutDown(bool reboot);

private:
	MFP	*MFPForIrq(int irq);
	static int32	MFPTimerInterrupt(void *data);

	MFP	*fMFP[2];

	RTC	*fRTC;

	// native features (ARAnyM emulator)
	uint32 (*nfGetID)(const char *name);
	int32 (*nfCall)(uint32 ID, ...);
	char *nfPage;
	uint32 nfDebugPrintfID;

};


}	// namespace BPrivate

using BPrivate::M68KAtari;


// #pragma mark - M68KAtari::MFP


static char sMFP0Buffer[sizeof(M68KAtari::MFP)];
static char sMFP1Buffer[sizeof(M68KAtari::MFP)];

/**
 * @brief Constructs an MFP wrapper for a memory-mapped MFP controller.
 * @param base   Base MMIO address of the MFP.
 * @param vector First interrupt vector served by this MFP.
 */
M68KAtari::MFP::MFP(uint32 base, int vector)
{
	fBase = base;
	fVector = vector;
}


/**
 * @brief Destroys the MFP wrapper. No hardware action is performed.
 */
M68KAtari::MFP::~MFP()
{
}

#warning M68K: use enable or mark register ?

/**
 * @brief Enables a single IRQ line on this MFP by setting the matching
 *        bit in the appropriate Interrupt Enable Register.
 * @param irq MFP-relative interrupt number (0..15).
 */
void
M68KAtari::MFP::EnableIOInterrupt(int irq)
{
	uint8 bit = 1 << (irq % 8);
	// I*B[0] is vector+0, I*A[0] is vector+8
	uint32 reg = Base() + ((irq > 8) ? (MFP_IERA) : (MFP_IERB));
	uint8 val = in8(reg);
	if (val & bit == 0) {
		val |= bit;
		out8(val, reg);
	}
}


/**
 * @brief Disables a single IRQ line on this MFP by clearing the matching
 *        bit in the appropriate Interrupt Enable Register.
 * @param irq MFP-relative interrupt number (0..15).
 */
void
M68KAtari::MFP::DisableIOInterrupt(int irq)
{
	uint8 bit = 1 << (irq % 8);
	// I*B[0] is vector+0, I*A[0] is vector+8
	uint32 reg = Base() + ((irq > 8) ? (MFP_IERA) : (MFP_IERB));
	uint8 val = in8(reg);
	if (val & bit) {
		val &= ~bit;
		out8(val, reg);
	}
}


/**
 * @brief Acknowledges a pending IRQ on this MFP by clearing its bit in
 *        the In-Service Register.
 * @param irq MFP-relative interrupt number (0..15).
 * @return true if the IRQ was pending and has been cleared; false otherwise.
 */
bool
M68KAtari::MFP::AcknowledgeIOInterrupt(int irq)
{
	uint8 bit = 1 << (irq % 8);
	// I*B[0] is vector+0, I*A[0] is vector+8
	uint32 reg = Base() + ((irq > 8) ? (MFP_ISRA) : (MFP_ISRB));
	uint8 val = in8(reg);
	if (val & bit) {
		val &= ~bit;
		out8(val, reg);
		return true;
	}
	return false;
}


// #pragma mark - M68KAtari::RTc


static char sRTCBuffer[sizeof(M68KAtari::RTC)];

/**
 * @brief Constructs an RTC wrapper around a MC146818A-compatible chip.
 * @param base   Base MMIO address of the RTC (index/data pair).
 * @param vector Interrupt vector associated with the RTC.
 */
M68KAtari::RTC::RTC(uint32 base, int vector)
{
	fBase = base;
	fVector = vector;
}


/**
 * @brief Destroys the RTC wrapper. No hardware action is performed.
 */
M68KAtari::RTC::~RTC()
{
}


/**
 * @brief Reads an MC146818A-style RTC register through the index/data pair,
 *        waiting briefly for any time-of-day update in progress to finish.
 * @param reg Register index to read.
 * @return The current register value.
 */
uint8
M68KAtari::RTC::ReadReg(uint32 reg)
{
	int waitTime = 10000;

	if (reg < 0x0a) {	// time of day stuff...
				// check for in progress updates before accessing
		out8(0x0a, fBase+1);
		while((in8(fBase+3) & 0x80) && --waitTime);
	}

	out8((uint8)reg,fBase+1);
	return in8(fBase+3);
}


// #pragma mark - M68KAtari


/**
 * @brief Constructs the Atari m68k platform object and tags it with the
 *        Atari platform identifier.
 */
M68KAtari::M68KAtari()
	: M68KPlatform(M68K_PLATFORM_ATARI)
{
}


/**
 * @brief Destroys the Atari m68k platform object.
 */
M68KAtari::~M68KAtari()
{
}


/**
 * @brief Probes for optional Atari hardware (TT MFP, TT RTC) and
 *        constructs wrapper objects for the devices that are present.
 * @param kernelArgs Kernel boot arguments (unused by the probe).
 */
void
M68KAtari::ProbeHardware(struct kernel_args *kernelArgs)
{
	dprintf("Atari hardware:\n");
	// if we are here we already know we have one
	dprintf("	ST MFP\n");
	if (m68k_is_hw_register_readable(MFP1_BASE)) {
		dprintf("	TT MFP\n");
		fMFP[1] = new(sMFP1Buffer) M68KAtari::MFP(MFP1_BASE, MFP1_VECTOR_BASE);
	}
	if (m68k_is_hw_register_readable(TT_RTC_BASE)) {
		dprintf("	TT RTC MC146818A\n");
		fRTC = new(sRTCBuffer) M68KAtari::RTC(TT_RTC_BASE,TT_RTC_VECTOR);
	} else
		panic("TT RTC required!");
}


/**
 * @brief Performs the early platform init: clears device pointers, picks
 *        up ARAnyM NatFeatures hooks from kernel_args, and requires at
 *        least the ST MFP to be present.
 * @param kernelArgs Kernel boot arguments carrying ARAnyM hook pointers.
 * @return B_OK on success; panics if the mandatory ST MFP is missing.
 */
status_t
M68KAtari::Init(struct kernel_args *kernelArgs)
{
	fMFP[0] = NULL;
	fMFP[1] = NULL;
	fRTC = NULL;

	// initialize ARAnyM NatFeatures
	nfGetID =
		kernelArgs->arch_args.plat_args.atari.nat_feat.nf_get_id;
	nfCall =
		kernelArgs->arch_args.plat_args.atari.nat_feat.nf_call;
	nfPage = (char *)
		kernelArgs->arch_args.plat_args.atari.nat_feat.nf_page;

	// probe for ST-MFP
	if (m68k_is_hw_register_readable(MFP0_BASE)) {
		fMFP[0] = new(sMFP0Buffer) M68KAtari::MFP(MFP0_BASE, MFP0_VECTOR_BASE);
	} else
		// won't really work anyway from here
		panic("You MUST have an ST MFP! Wait, is that *really* an Atari ???");

	return B_OK;
}


/**
 * @brief Sets up the debug output channel (the ARAnyM debug-printf
 *        NatFeature) and then probes for optional hardware.
 * @param kernelArgs Kernel boot arguments carrying the debug-printf hook id.
 * @return B_OK on success.
 */
status_t
M68KAtari::InitSerialDebug(struct kernel_args *kernelArgs)
{
	nfDebugPrintfID =
		kernelArgs->arch_args.plat_args.atari.nat_feat.nf_dprintf_id;

#warning M68K: add real serial debug output someday

	//out8(0x11, IKBD_BASE+IKBD_DATA);

	// now we can expect to see something
	ProbeHardware(kernelArgs);

	return B_OK;
}


/**
 * @brief Post-VM platform init hook. Currently a stub that returns
 *        B_NO_INIT; debugger commands are left commented out.
 * @param kernelArgs Kernel boot arguments (unused).
 * @return B_NO_INIT.
 */
status_t
M68KAtari::InitPostVM(struct kernel_args *kernelArgs)
{
#if 0
	add_debugger_command("of_exit", &debug_command_of_exit,
		"Exit to the Open Firmware prompt. No way to get back into the OS!");
	add_debugger_command("of_enter", &debug_command_of_enter,
		"Enter a subordinate Open Firmware interpreter. Quitting it returns "
		"to KDL.");
#endif
	return B_NO_INIT;
	return B_OK;
}


/**
 * @brief Initializes the programmable interrupt controller. The MFP
 *        requires no explicit setup here, so this just returns success.
 * @param kernelArgs Kernel boot arguments (unused).
 * @return B_OK.
 */
status_t
M68KAtari::InitPIC(struct kernel_args *kernelArgs)
{
	return B_OK;
}


/**
 * @brief Initializes the real-time clock subsystem by publishing the
 *        MFP-derived time-base frequency into kernel_args.
 * @param kernelArgs Kernel boot arguments; time_base_frequency is set here.
 * @param data       Real-time data structure (currently unused).
 * @return B_OK.
 */
status_t
M68KAtari::InitRTC(struct kernel_args *kernelArgs,
	struct real_time_data *data)
{
	// XXX we should do this in the bootloader maybe...
	kernelArgs->arch_args.time_base_frequency = MFP_SYSTEM_TIME_RATE;
	return B_OK;
}


/**
 * @brief Initializes the hardware timer by stopping MFP timer A and
 *        installing the MFP timer interrupt handler.
 * @param kernelArgs Kernel boot arguments (unused).
 * @return B_OK.
 */
status_t
M68KAtari::InitTimer(struct kernel_args *kernelArgs)
{

	fMFP[0]->WriteReg(MFP_TACR, 0); // stop it
	install_io_interrupt_handler(fMFP[0]->Vector()+13, &MFPTimerInterrupt, this, 0);
	return B_OK;
}


/**
 * @brief Reads a single character from the IKBD keyboard by polling,
 *        translating Atari scancodes and modifier state into an ASCII
 *        byte (with ANSI escape sequences synthesized for cursor keys
 *        and the like). Used by the kernel blue-screen console.
 * @return The translated character.
 */
char
M68KAtari::BlueScreenGetChar()
{
	/* polling the keyboard, similar to code in keyboard
	 * driver, but without using an interrupt
	 * taken almost straight from x86 code
	 * XXX: maybe use the keymap from the _AKP cookie instead ?
	 */
	static bool shiftPressed = false;
	static bool controlPressed = false;
	static bool altPressed = false;
	static uint8 special = 0;
	static uint8 special2 = 0;
	uint8 key = 0;

	if (special & 0x80) {
		special &= ~0x80;
		return '[';
	}
	if (special != 0) {
		key = special;
		special = 0;
		return key;
	}
	if (special2 != 0) {
		key = special2;
		special2 = 0;
		return key;
	}

	while (true) {
		uint8 status = in8(IKBD_BASE+IKBD_CTRL);

		if ((status & IKBD_STATUS_READ_BUFFER_FULL) == 0) {
			// no data in keyboard buffer
			spin(200);
			//kprintf("no key\n");
			continue;
		}

		spin(200);
		key = in8(IKBD_BASE+IKBD_DATA);
		/*
		kprintf("key: %02x, %sshift %scontrol %salt\n",
			key,
			shiftPressed?"":"!",
			controlPressed?"":"!",
			altPressed?"":"!");
		*/

		if (key & 0x80) {
			// key up
			switch (key & ~0x80) {
				case LEFT_SHIFT:
				case RIGHT_SHIFT:
					shiftPressed = false;
					break;
				case LEFT_CONTROL:
					controlPressed = false;
					break;
				case LEFT_ALT:
					altPressed = false;
					break;
			}
		} else {
			// key down
			switch (key) {
				case LEFT_SHIFT:
				case RIGHT_SHIFT:
					shiftPressed = true;
					break;

				case LEFT_CONTROL:
					controlPressed = true;
					break;

				case LEFT_ALT:
					altPressed = true;
					break;

				// start escape sequence for cursor movement
				case CURSOR_UP:
					special = 0x80 | 'A';
					return '\x1b';
				case CURSOR_DOWN:
					special = 0x80 | 'B';
					return '\x1b';
				case CURSOR_RIGHT:
					special = 0x80 | 'C';
					return '\x1b';
				case CURSOR_LEFT:
					special = 0x80 | 'D';
					return '\x1b';
				case CURSOR_HOME:
					special = 0x80 | 'H';
					return '\x1b';
				case CURSOR_END:
					special = 0x80 | 'F';
					return '\x1b';
				case PAGE_UP:
					special = 0x80 | '5';
					special2 = '~';
					return '\x1b';
				case PAGE_DOWN:
					special = 0x80 | '6';
					special2 = '~';
					return '\x1b';


				case DELETE:
					if (controlPressed && altPressed)
						arch_cpu_shutdown(true);

					special = 0x80 | '3';
					special2 = '~';
					return '\x1b';

				default:
					if (controlPressed) {
						char c = kShiftedKeymap[key];
						if (c >= 'A' && c <= 'Z')
							return 0x1f & c;
					}

					if (altPressed)
						return kAltedKeymap[key];

					return shiftPressed
						? kShiftedKeymap[key] : kUnshiftedKeymap[key];
			}
		}
	}
}


/**
 * @brief Reads one character from the serial debug input. Currently
 *        redirected to BlueScreenGetChar() since no real UART backend is
 *        wired up yet.
 * @return The next input character.
 */
char
M68KAtari::SerialDebugGetChar()
{
	//WRITEME
	return BlueScreenGetChar();
	//return 0;
}


/**
 * @brief Writes a single character to the debug output using the ARAnyM
 *        NatFeature debug-printf hook when available.
 * @param c Character to print.
 */
void
M68KAtari::SerialDebugPutChar(char c)
{
	if (nfCall && nfDebugPrintfID) {
#if 0
		static char buffer[2] = { '\0', '\0' };
		buffer[0] = c;

		nfCall(nfDebugPrintfID /*| 0*/, buffer);
#endif
		nfPage[0] = c;
		nfPage[1] = '\0';
		nfCall(nfDebugPrintfID /*| 0*/, nfPage);
	}

#warning M68K: WRITEME
	// real serial
	//panic("WRITEME");
}


/**
 * @brief Enables the given global IRQ, dispatching to whichever MFP
 *        instance owns it.
 * @param irq Global interrupt vector number.
 */
void
M68KAtari::EnableIOInterrupt(int32 irq)
{
	MFP *mfp = MFPForIrq(irq);

	if (mfp)
		mfp->EnableIOInterrupt(irq - mfp->Vector());
}


/**
 * @brief Disables the given global IRQ, dispatching to the owning MFP.
 * @param irq Global interrupt vector number.
 */
void
M68KAtari::DisableIOInterrupt(int32 irq)
{
	MFP *mfp = MFPForIrq(irq);

	if (mfp)
		mfp->DisableIOInterrupt(irq - mfp->Vector());
}


/**
 * @brief Acknowledges the given global IRQ on the owning MFP.
 * @param irq Global interrupt vector number.
 * @return true if the IRQ was pending; false otherwise (or if no MFP owns it).
 */
bool
M68KAtari::AcknowledgeIOInterrupt(int32 irq)
{
	MFP *mfp = MFPForIrq(irq);

	if (mfp)
		return mfp->AcknowledgeIOInterrupt(irq - mfp->Vector());
	return false;
}


/**
 * @brief Reads an RTC register, faking the century byte (register 0x32)
 *        as 0x20 since we are permanently in the 21st century.
 * @param reg RTC register index.
 * @return The register value (possibly faked for the century byte).
 */
uint8
M68KAtari::ReadRTCReg(uint8 reg)
{
	// fake century
	// (on MC146818A it's in the RAM, but probably it's used for that purpose...)
	// but just in case, we're in 20xx now anyway :)
	if (reg == 0x32)
		return 0x20;
	return fRTC->ReadReg(reg);
}


/**
 * @brief Writes a value to an RTC register.
 * @param reg RTC register index.
 * @param val Byte to write.
 */
void
M68KAtari::WriteRTCReg(uint8 reg, uint8 val)
{
	fRTC->WriteReg(reg, val);
}

/**
 * @brief Sets the hardware RTC to the given UNIX-epoch seconds value.
 *        Not yet implemented for the Atari platform.
 * @param seconds Seconds since 1970-01-01 UTC.
 */
void
M68KAtari::SetHardwareRTC(uint64 seconds)
{
#warning M68K: WRITEME
}


/**
 * @brief Reads the hardware RTC as a UNIX-epoch second count.
 *        Not yet implemented for the Atari platform.
 * @return Always 0 in the current stub.
 */
uint32
M68KAtari::GetHardwareRTC()
{
#warning M68K: WRITEME
	return 0;
}


/**
 * @brief Arms the MFP timer so that it fires once after the requested
 *        interval, clamping the count into the MFP's 8-bit range.
 * @param timeout Microseconds until the next timer interrupt.
 */
void
M68KAtari::SetHardwareTimer(bigtime_t timeout)
{
	uint8 nextEventClocks;
	if (timeout <= 0)
		nextEventClocks = 2;
	else if (timeout < MFP_MAX_TIMER_INTERVAL)
		nextEventClocks = timeout * MFP_TIMER_RATE / 1000000;
	else
		nextEventClocks = 0xff;

	fMFP[0]->WriteReg(SYS_TDR, nextEventClocks);
	// delay mode, device by 4
	fMFP[0]->WriteReg(SYS_TCR, (fMFP[0]->ReadReg(SYS_TCR) & SYS_TCRMASK) | SYS_TENABLE);
	// enable irq
	EnableIOInterrupt(MFP1_VECTOR_BASE + SYS_TVECTOR);
}


/**
 * @brief Disables the MFP hardware timer and masks its IRQ.
 */
void
M68KAtari::ClearHardwareTimer(void)
{
	// disable the irq (as on PC but I'm not sure it's needed)
	DisableIOInterrupt(MFP1_VECTOR_BASE + SYS_TVECTOR);
	// stop it, we don't want another countdown
	fMFP[0]->WriteReg(SYS_TCR, (fMFP[0]->ReadReg(SYS_TCR) & SYS_TCRMASK) | SYS_TDISABLE);
}


/**
 * @brief Shuts the machine down or reboots it. Currently unimplemented
 *        for the Atari platform; this routine panics.
 * @param reboot true to reboot; false to power off.
 */
void
M68KAtari::ShutDown(bool reboot)
{
	panic("Bombs!");
	panic("WRITEME");
}


/**
 * @brief Finds the MFP instance that owns the given global IRQ vector.
 * @param irq Global interrupt vector number.
 * @return Pointer to the owning MFP, or NULL if none matches.
 */
M68KAtari::MFP *
M68KAtari::MFPForIrq(int irq)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (fMFP[i]) {
			if (irq >= fMFP[i]->Vector() && irq < fMFP[i]->Vector() + 16)
				return fMFP[i];
		}
	}
	return NULL;
}

/**
 * @brief MFP timer-interrupt trampoline: clears the hardware timer and
 *        forwards into the generic kernel timer_interrupt().
 * @param data Opaque cookie, actually a pointer to the M68KAtari instance.
 * @return Whatever timer_interrupt() returns.
 */
int32
M68KAtari::MFPTimerInterrupt(void *data)
{
	M68KAtari *_this = (M68KAtari *)data;
	// disable the timer, else it will loop again with the same value
	_this->ClearHardwareTimer();
	// handle the timer
	return timer_interrupt();
}


// static buffer for constructing the actual M68KPlatform
static char *sM68KPlatformBuffer[sizeof(M68KAtari)];
#warning PTR HERE ???


/**
 * @brief Constructs the singleton Atari M68KPlatform instance in a
 *        static buffer and returns a pointer to it.
 * @return Pointer to the platform object.
 */
M68KPlatform *instanciate_m68k_platform_atari()
{
	return new(sM68KPlatformBuffer) M68KAtari;
}
