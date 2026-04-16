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
 *   Copyright 2005-2007, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2002, Manuel J. Petit. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file gdb.cpp
 * @brief In-kernel stub implementing the GDB Remote Serial Protocol.
 *
 * Provides a minimal gdbserver-compatible target that a host "target
 * remote" gdb session can talk to over the debug serial port. Implements
 * the essential packet subset: "?" (stop reason), "H" (set thread,
 * stubbed OK), "q" (a few queries), "g" (read registers), "m" (read
 * memory through debug_memcpy), plus "c" / "s" / "D" / "k" which all
 * unwind the state machine back to the normal KDL prompt. Write-register
 * ("G") and set-breakpoint packets are not implemented and return "E01".
 *
 * @brief Packet framing and checksums.
 *
 * A full GDB packet has the shape "$payload#cc" where cc is the two-digit
 * modulo-256 checksum of payload. The receive side is structured as a
 * small state machine (INIT -> CMDREAD -> CKSUM1 -> CKSUM2 -> WAITACK)
 * driven one input byte at a time by gdb_state_dispatch(). Replies are
 * built into sReply with the same framing and acknowledged / retransmitted
 * based on '+' / '-' from the host.
 *
 * @brief KDL / interrupt context.
 *
 * cmd_gdb() is entered from the kernel debugger, which itself runs with
 * interrupts disabled and on a single CPU. No locking is performed
 * anywhere in this file because nothing else in the kernel is running
 * while the stub owns the serial line. Memory reads go through
 * debug_memcpy() so that bad pointers from gdb only produce E02 replies
 * instead of triple-faulting the CPU.
 */

/** Contains the code to interface with a remote GDB */

#include "gdb.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <ByteOrder.h>

#include <arch/debug.h>
#include <arch/debug_console.h>
#include <debug.h>
#include <elf.h>
#include <elf_priv.h>
#include <smp.h>
#include <vm/vm.h>


enum { INIT = 0, CMDREAD, CKSUM1, CKSUM2, WAITACK, QUIT, GDBSTATES };


static char sCommand[512];
static int sCommandIndex;
static int sCheckSum;

static char sReply[512];
static char sSafeMemory[512];


// utility functions


/**
 * @brief Converts a single hex ASCII character into its 4-bit value.
 *
 * Accepts '0'-'9', 'a'-'f' and 'A'-'F'. Any other input returns 0xff,
 * which callers treat as a framing error.
 *
 * @param input ASCII character to decode.
 * @return Nibble value 0x0-0xf, or 0xff when the byte is not a hex digit.
 */
static int
parse_nibble(int input)
{
	int nibble = 0xff;

	if (input >= '0' && input <= '9')
		nibble = input - '0';

	if (input >= 'A' && input <= 'F')
		nibble = 0x0a + input - 'A';

	if (input >= 'a' && input <= 'f')
		nibble = 0x0a + input - 'a';

	return nibble;
}


//	#pragma mark - GDB protocol


/**
 * @brief Sends a positive packet acknowledgement ('+') to the host.
 *
 * Used when a packet has been received and its checksum verified.
 *
 * @return void.
 */
static void
gdb_ack(void)
{
	arch_debug_serial_putchar('+');
}


/**
 * @brief Sends a negative packet acknowledgement ('-') to the host.
 *
 * Requests that the host retransmit the last packet because of a
 * checksum mismatch or framing error.
 *
 * @return void.
 */
static void
gdb_nak(void)
{
	arch_debug_serial_putchar('-');
}


/**
 * @brief Retransmits the last reply packet verbatim.
 *
 * Invoked in response to a '-' from the host, or after sReply has been
 * freshly composed and needs to hit the wire for the first time.
 *
 * @return void.
 */
static void
gdb_resend_reply(void)
{
	arch_debug_serial_puts(sReply);
}


/**
 * @brief Formats, checksums and transmits a reply packet.
 *
 * Expands the printf-style format into sReply after the '$' framing byte,
 * computes the modulo-256 checksum over the payload, appends "#xx" and
 * transmits the complete packet via the serial backend.
 *
 * @param format printf-style format string for the packet payload.
 * @param ...    Arguments consumed by the format string.
 * @return void.
 */
static void
gdb_reply(char const* format, ...)
{
	int i;
	int len;
	int sum;
	va_list args;

	va_start(args, format);
	sReply[0] = '$';
	vsprintf(sReply + 1, format, args);
	va_end(args);

	len = strlen(sReply);
	sum = 0;
	for (i = 1; i < len; i++) {
		sum += sReply[i];
	}
	sum %= 256;

	sprintf(sReply + len, "#%02x", sum);

	gdb_resend_reply();
}


/**
 * @brief Builds a reply packet containing the current register file.
 *
 * Delegates to arch_debug_gdb_get_registers() which writes the
 * architecture-defined hex-encoded register dump directly into sReply.
 * If the architecture call fails, or the checksum trailer does not fit
 * in the reply buffer, an E01 error reply is sent instead.
 *
 * @return void.
 */
static void
gdb_regreply()
{
	sReply[0] = '$';

	// get registers (architecture specific)
	ssize_t bytesWritten = arch_debug_gdb_get_registers(sReply + 1,
		sizeof(sReply) - 1);
	if (bytesWritten < 0) {
		gdb_reply("E01");
		return;
	}

	// add 1 for the leading '$'
	bytesWritten++;

	// compute check sum
	int sum = 0;
	for (int32 i = 1; i < bytesWritten; i++)
		sum += sReply[i];
	sum %= 256;

	// print check sum
	int result = snprintf(sReply + bytesWritten, sizeof(sReply) - bytesWritten,
		"#%02x", sum);
	if (result >= (ssize_t)sizeof(sReply) - bytesWritten) {
		gdb_reply("E01");
		return;
	}

	gdb_resend_reply();
}


/**
 * @brief Replies to an 'm' packet with a hex-encoded memory block.
 *
 * Each byte of the caller-supplied buffer is emitted as two hex digits
 * into sReply following the '$' framing byte; the trailing checksum and
 * '#xx' delimiter are then appended and the full packet transmitted.
 *
 * @param bytes Pointer to the bytes already copied out of kernel memory.
 * @param numbytes Number of bytes to encode.
 * @return void.
 */
static void
gdb_memreply(char const* bytes, int numbytes)
{
	int i;
	int len;
	int sum;

	sReply[0] = '$';
	for (i = 0; i < numbytes; i++)
		sprintf(sReply + 1 + 2 * i, "%02x", (uint8)bytes[i]);

	len = strlen(sReply);
	sum = 0;
	for (i = 1; i < len; i++)
		sum += sReply[i];
	sum %= 256;

	sprintf(sReply + len, "#%02x", sum);

	gdb_resend_reply();
}


//	#pragma mark - checksum verification


/**
 * @brief Validates the checksum of the just-received command packet.
 *
 * Recomputes the modulo-256 sum of every byte in sCommand and compares it
 * against sCheckSum, which was parsed out of the two hex digits after '#'.
 *
 * @return 1 when the checksum matches, 0 otherwise.
 */
static int
gdb_verify_checksum(void)
{
	int i;
	int len;
	int sum;

	len = strlen(sCommand);
	sum = 0;
	for (i = 0; i < len; i++)
		sum += sCommand[i];
	sum %= 256;

	return (sum == sCheckSum) ? 1 : 0;
}


//	#pragma mark - command parsing


/**
 * @brief Dispatches a fully-received GDB packet by its first byte.
 *
 * Validates the checksum (responding '-' + returning to INIT on failure,
 * '+' otherwise), then switches on sCommand[0] to implement the subset of
 * packets the stub supports:
 *   - '?'  fake stop reason (S09 / SIGKILL)
 *   - 'H'  set-thread, stubbed OK
 *   - 'q'  queries (Supported returns empty, Offsets returns kernel
 *          text/data deltas, others return empty)
 *   - 'c', 'D', 'k' detach/continue/kill - all unwind to QUIT
 *   - 'g'  register read (delegates to gdb_regreply)
 *   - 'G'  register write - not implemented, E01
 *   - 'm'  memory read - parses hex "AAA,LLL", caps len at 128 and uses
 *          debug_memcpy() through the safe sSafeMemory bounce buffer
 *   - 's'  step - not implemented, E01
 *   - default: empty reply (GDB treats as unsupported).
 *
 * @return Next state machine state, either WAITACK (packet answered) or
 *         QUIT (leave the stub) or INIT on checksum failure.
 */
static int
gdb_parse_command(void)
{
	if (!gdb_verify_checksum()) {
		gdb_nak();
		return INIT;
	} else
		gdb_ack();

	switch (sCommand[0]) {
		case '?':
			// command '?' is used for retrieving the signal
			// that stopped the program. Fully implemeting
			// this command requires help from the debugger,
			// by now we just fake a SIGKILL
			gdb_reply("S09");	/* SIGKILL = 9 */
			break;

		case 'H':
			// Command H (actually Hct) is used to select
			// the current thread (-1 meaning all threads)
			// We just fake we recognize the the command
			// and send an 'OK' response.
			gdb_reply("OK");
			break;

		case 'q':
		{
			// query commands

			if (strcmp(sCommand + 1, "Supported") == 0) {
				// get the supported features
				gdb_reply("");
			} else if (strcmp(sCommand + 1, "Offsets") == 0) {
				// get the segment offsets
				elf_image_info* kernelImage = elf_get_kernel_image();
				gdb_reply("Text=%lx;Data=%lx;Bss=%lx",
					kernelImage->text_region.delta,
					kernelImage->data_region.delta,
					kernelImage->data_region.delta);
			} else
				gdb_reply("");

			break;
		}

		case 'c':
			// continue at address
			// TODO: Parse the address and resume there!
			return QUIT;

		case 'g':
			gdb_regreply();
			break;

		case 'G':
			// write registers
			// TODO: Implement!
			gdb_reply("E01");
			break;


		case 'm':
		{
			char* ptr;
			addr_t address;
			size_t len;

			// The 'm' command has the form mAAA,LLL
			// where AAA is the address and LLL is the
			// number of bytes.
			ptr = sCommand + 1;
			address = 0;
			len = 0;
			while (ptr && *ptr && (*ptr != ',')) {
				address <<= 4;
				address += parse_nibble(*ptr);
				ptr += 1;
			}
			if (*ptr == ',')
				ptr += 1;

			while (ptr && *ptr) {
				len <<= 4;
				len += parse_nibble(*ptr);
				ptr += 1;
			}

			if (len > 128)
				len = 128;

			// We cannot directly access the requested memory
			// for gdb may be trying to access an stray pointer
			// We copy the memory to a safe buffer using
			// the bulletproof debug_memcpy().
			if (debug_memcpy(B_CURRENT_TEAM, sSafeMemory, (char*)address, len)
					< 0) {
				gdb_reply("E02");
			} else
				gdb_memreply(sSafeMemory, len);

			break;
		}

		case 'D':
			// detach
			return QUIT;

		case 'k':
			// Command 'k' actual semantics is 'kill the damn thing'.
			// However gdb sends that command when you disconnect
			// from a debug session. I guess that 'kill' for the
			// kernel would map to reboot... however that's a
			// a very mean thing to do, instead we just quit
			// the gdb state machine and fallback to the regular
			// kernel debugger command prompt.
			return QUIT;

		case 's':
			// "step" -- resume (?) at address
			// TODO: Implement!
			gdb_reply("E01");
			break;

		default:
			gdb_reply("");
			break;
	}

	return WAITACK;
}


//	#pragma mark - protocol state machine


/**
 * @brief State-machine handler for the INIT state.
 *
 * Waits for a '$' to mark the start of a new packet. All other bytes are
 * silently swallowed (the alternative '-' would be strictly correct but
 * empirically causes more churn with some gdb versions). On '$' the
 * sCommand buffer is cleared, the index reset, and the machine advances
 * to CMDREAD.
 *
 * @param input Byte just read from the serial port.
 * @return Next state (INIT or CMDREAD).
 */
static int
gdb_init_handler(int input)
{
	switch (input) {
		case '$':
			memset(sCommand, 0, sizeof(sCommand));
			sCommandIndex = 0;
			return CMDREAD;

		default:
#if 0
			gdb_nak();
#else
			// looks to me like we should send
			// a NAK here but it kinda works
			// better if we just gobble all
			// junk chars silently
#endif
			return INIT;
	}
}


/**
 * @brief State-machine handler for the CMDREAD state.
 *
 * Appends each byte to sCommand until '#' is seen, which marks the end of
 * the packet payload and triggers the transition to CKSUM1.
 *
 * @param input Byte just read from the serial port.
 * @return Next state (CMDREAD or CKSUM1).
 */
static int
gdb_cmdread_handler(int input)
{
	switch (input) {
		case '#':
			return CKSUM1;

		default:
			sCommand[sCommandIndex] = input;
			sCommandIndex += 1;
			return CMDREAD;
	}
}


/**
 * @brief State-machine handler for the first checksum nibble.
 *
 * Decodes the high nibble of the modulo-256 checksum following '#'. If
 * parse_nibble() reports a bad character, the input is silently dropped
 * (see the commented-out NAK path) but the machine still advances so the
 * eventual checksum comparison will fail and trigger retransmission.
 *
 * @param input ASCII hex digit just read.
 * @return Next state CKSUM2.
 */
static int
gdb_cksum1_handler(int input)
{
	int nibble = parse_nibble(input);

	if (nibble == 0xff) {
#if 0
		gdb_nak();
		return INIT;
#else
		// looks to me like we should send
		// a NAK here but it kinda works
		// better if we just gobble all
		// junk chars silently
#endif
	}

	sCheckSum = nibble << 4;

	return CKSUM2;
}


/**
 * @brief State-machine handler for the second checksum nibble.
 *
 * Completes sCheckSum with the low nibble and immediately hands off to
 * gdb_parse_command() which validates the packet and dispatches it.
 *
 * @param input ASCII hex digit just read.
 * @return The state returned by gdb_parse_command() (WAITACK, QUIT or INIT).
 */
static int
gdb_cksum2_handler(int input)
{
	int nibble = parse_nibble(input);

	if (nibble == 0xff) {
#if 0
		gdb_nak();
		return INIT;
#else
		// looks to me like we should send
		// a NAK here but it kinda works
		// better if we just gobble all
		// junk chars silently
#endif
	}

	sCheckSum += nibble;

	return gdb_parse_command();
}


/**
 * @brief State-machine handler for WAITACK.
 *
 * After sending a reply, the stub waits for the host to acknowledge it
 * with '+'. A '-' means the host wants a retransmit; any other byte is
 * treated as a framing desync: we NAK and drop back to INIT to hunt for
 * the next '$'.
 *
 * @param input Byte just read from the serial port.
 * @return Next state INIT or WAITACK.
 */
static int
gdb_waitack_handler(int input)
{
	switch (input) {
		case '+':
			return INIT;
		case '-':
			gdb_resend_reply();
			return WAITACK;

		default:
			// looks like gdb and us are out of sync,
			// send a NAK and retry from INIT state.
			gdb_nak();
			return INIT;
	}
}


/**
 * @brief State-machine handler for QUIT.
 *
 * Terminal state; the outer loop should already have exited before we
 * ever enter here. Provided so the dispatch table has a non-NULL entry
 * for every declared state.
 *
 * @param input Unused.
 * @return Always QUIT.
 */
static int
gdb_quit_handler(int input)
{
	(void)(input);

	// actually we should never be here
	return QUIT;
}


static int (*dispatch_table[GDBSTATES])(int) = {
	&gdb_init_handler,
	&gdb_cmdread_handler,
	&gdb_cksum1_handler,
	&gdb_cksum2_handler,
	&gdb_waitack_handler,
	&gdb_quit_handler
};


/**
 * @brief Dispatches a single input byte to the handler for the current state.
 *
 * Guards against stray state values by forcing QUIT when curr is out of
 * range, otherwise indexes into dispatch_table[] and returns the handler's
 * chosen next state.
 *
 * @param curr Current state.
 * @param input Byte read from the serial port.
 * @return The next state.
 */
static int
gdb_state_dispatch(int curr, int input)
{
	if (curr < INIT || curr >= GDBSTATES)
		return QUIT;

	return dispatch_table[curr](input);
}


/**
 * @brief Runs the blocking packet-reception loop until QUIT.
 *
 * Seeds the state to INIT and then repeatedly pulls bytes off the debug
 * serial port, feeding each one into gdb_state_dispatch(). Returns only
 * when a packet that terminates the session ('c', 'D' or 'k') has been
 * processed, at which point the kernel debugger regains control.
 *
 * @return 0 always.
 */
static int
gdb_state_machine(void)
{
	int state = INIT;
	int c;

	while (state != QUIT) {
		c = arch_debug_serial_getchar();
		state = gdb_state_dispatch(state, c);
	}

	return 0;
}


//	#pragma mark -


/**
 * @brief KDL "gdb" command: hand control to the in-kernel gdb stub.
 *
 * Usage: gdb
 *
 * Drops into gdb_state_machine() which runs until the remote host sends a
 * continue, detach or kill packet. Executed with interrupts disabled as
 * part of the KDL prompt; no locking is required because the stub owns
 * the machine for the duration of its run.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 when the state machine exits normally.
 */
int
cmd_gdb(int argc, char** argv)
{
	(void)(argc);
	(void)(argv);

	return gdb_state_machine();
}
