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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file debug.cpp
 * @brief Kernel debug output and kernel debugger (KDL) core.
 *
 * Implements the primary kernel output paths (dprintf, kprintf, panic,
 * kernel_debugger) plus the KDL session machinery: CPU-state capture, halting
 * peer CPUs, taking over the serial/blue-screen console, command registration
 * and lookup, line editing with history and tab-completion, and emergency-key
 * hotkeys. Also hosts the syslog ring buffer, the dprintf repeat collapser,
 * and debugger-only memory accessors guarded by a setjmp/fault handler.
 */


#include "blue_screen.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <algorithm>

#include <AutoDeleter.h>
#include <boot/kernel_args.h>
#include <cpu.h>
#include <debug.h>
#include <debug_heap.h>
#include <debug_paranoia.h>
#include <driver_settings.h>
#include <frame_buffer_console.h>
#include <interrupts.h>
#include <kernel.h>
#include <ksystem_info.h>
#include <safemode.h>
#include <smp.h>
#include <thread.h>
#include <tracing.h>
#include <vm/vm.h>
#include <vm/VMTranslationMap.h>

#include <arch/debug_console.h>
#include <arch/debug.h>
#include <util/AutoLock.h>
#include <util/ring_buffer.h>

#include <syslog_daemon.h>

#include "debug_builtin_commands.h"
#include "debug_commands.h"
#include "debug_output_filter.h"
#include "debug_variables.h"


#if __GNUC__ == 2
#	define va_copy(to, from)	__va_copy(to, from)
#endif


struct debug_memcpy_parameters {
	void*		to;
	const void*	from;
	size_t		size;
};

struct debug_strlcpy_parameters {
	char*		to;
	const char*	from;
	size_t		size;
	size_t		result;
};


static const char* const kKDLPrompt = "kdebug> ";
static const char* const kKDLMessageCommandSeparator = "@!";
	// separates panic() message from command list to execute

extern "C" int kgets(char* buffer, int length);

void call_modules_hook(bool enter);

static void syslog_write(const char* text, int32 length, bool notify);

static arch_debug_registers sDebugRegisters[SMP_MAX_CPUS];

static debug_page_fault_info sPageFaultInfo;

static bool sSerialDebugEnabled = true;
static bool sSyslogOutputEnabled = true;
static bool sBlueScreenEnabled = false;
	// must always be false on startup
static bool sDebugScreenEnabled = false;
static bool sBlueScreenOutput = true;
static bool sEmergencyKeysEnabled = true;
static spinlock sSpinlock = B_SPINLOCK_INITIALIZER;
static int32 sDebuggerOnCPU = -1;

static sem_id sSyslogNotify = -1;
static thread_id sSyslogWriter = -1;
static port_id sSyslogPort = -1;
static struct syslog_message* sSyslogMessage;
static struct ring_buffer* sSyslogBuffer;
static size_t sSyslogBufferOffset = 0;
	// (relative) buffer offset of the yet unsent syslog messages
static bool sSyslogDropped = false;
static bool sDebugSyslog = false;
static size_t sSyslogDebuggerOffset = 0;
	// (relative) buffer offset of the kernel debugger messages of the current
	// KDL session

static void* sPreviousSessionSyslogBuffer = NULL;
static size_t sPreviousSessionSyslogBufferSize = 0;

static const char* sCurrentKernelDebuggerMessagePrefix;
static const char* sCurrentKernelDebuggerMessage;
static va_list sCurrentKernelDebuggerMessageArgs;

#define DEFAULT_SYSLOG_BUFFER_SIZE 65536
#define OUTPUT_BUFFER_SIZE 1024
static char sOutputBuffer[OUTPUT_BUFFER_SIZE];
static char sInterruptOutputBuffer[OUTPUT_BUFFER_SIZE];
static char sLastOutputBuffer[OUTPUT_BUFFER_SIZE];
static DebugOutputFilter* sDebugOutputFilter = NULL;
DefaultDebugOutputFilter gDefaultDebugOutputFilter;
static mutex sOutputLock = MUTEX_INITIALIZER("debug output");

static void flush_pending_repeats(bool notifySyslog);
static void check_pending_repeats(void* data, int iter);

static int64 sMessageRepeatFirstTime = 0;
static int64 sMessageRepeatLastTime = 0;
static int32 sMessageRepeatCount = 0;

static debugger_module_info* sDebuggerModules[8];
static const uint32 kMaxDebuggerModules = sizeof(sDebuggerModules)
	/ sizeof(sDebuggerModules[0]);

#define LINE_BUFFER_SIZE 1024
#define HISTORY_SIZE 16

static char sLineBuffer[HISTORY_SIZE][LINE_BUFFER_SIZE] = { "", };
static int32 sCurrentLine = 0;

static debugger_demangle_module_info* sDemangleModule;

static Thread* sDebuggedThread;
static int32 sInDebugger = 0;
static bool sPreviousDprintfState;
static volatile bool sHandOverKDL = false;
static int32 sHandOverKDLToCPU = -1;
static bool sCPUTrapped[SMP_MAX_CPUS];


// #pragma mark - DebugOutputFilter


/**
 * @brief Default constructs a base output filter.
 */
DebugOutputFilter::DebugOutputFilter()
{
}


/**
 * @brief Virtual destructor for the output filter base class.
 */
DebugOutputFilter::~DebugOutputFilter()
{
}


/**
 * @brief No-op base implementation of PrintString.
 *
 * Subclasses override to route raw text to serial, syslog, and/or screen.
 *
 * @param string NUL-terminated text to emit (ignored by the base class).
 */
void
DebugOutputFilter::PrintString(const char* string)
{
}


/**
 * @brief No-op base implementation of printf-style Print.
 *
 * @param format printf-style format string (ignored by the base class).
 * @param args Variadic argument list matching @a format.
 */
void
DebugOutputFilter::Print(const char* format, va_list args)
{
}


/**
 * @brief Emits a raw string to every enabled debug sink.
 *
 * Called from both thread and KDL context. Writes to serial, syslog, blue
 * screen, and any registered debugger modules as dictated by the current
 * enable flags.
 *
 * @param string NUL-terminated text to emit.
 */
void
DefaultDebugOutputFilter::PrintString(const char* string)
{
	size_t length = strlen(string);

	if (sSerialDebugEnabled)
		arch_debug_serial_puts(string);
	if (sSyslogOutputEnabled)
		syslog_write(string, length, false);
	if (sBlueScreenEnabled || sDebugScreenEnabled)
		blue_screen_puts(string);

	for (uint32 i = 0; sSerialDebugEnabled && i < kMaxDebuggerModules; i++) {
		if (sDebuggerModules[i] && sDebuggerModules[i]->debugger_puts)
			sDebuggerModules[i]->debugger_puts(string, length);
	}
}


/**
 * @brief Formats into the interrupt scratch buffer and emits the result.
 *
 * Uses @c sInterruptOutputBuffer directly and flushes any pending repeat
 * summary first. Safe to call with interrupts disabled (KDL / panic path).
 *
 * @param format printf-style format string.
 * @param args Variadic argument list matching @a format.
 */
void
DefaultDebugOutputFilter::Print(const char* format, va_list args)
{
	vsnprintf(sInterruptOutputBuffer, OUTPUT_BUFFER_SIZE, format, args);
	flush_pending_repeats(false);
	PrintString(sInterruptOutputBuffer);
}


// #pragma mark -


/**
 * @brief Installs a new debug output filter and returns the previous one.
 *
 * Called by subsystems that want to intercept kprintf output (for example
 * the GDB serial stub). Must be invoked with interrupts disabled while
 * inside KDL.
 *
 * @param filter New filter to install (may be @c NULL to detach).
 * @return Previously installed filter, or @c NULL if none was set.
 */
DebugOutputFilter*
set_debug_output_filter(DebugOutputFilter* filter)
{
	DebugOutputFilter* oldFilter = sDebugOutputFilter;
	sDebugOutputFilter = filter;
	return oldFilter;
}


/**
 * @brief Writes a single character directly to every enabled sink.
 *
 * Used by the line editor to update the terminal one character at a time.
 * Bypasses the repeat collapser and the output filter, so it is safe to
 * call with interrupts disabled and from within KDL.
 *
 * @param c Character to emit.
 */
static void
kputchar(char c)
{
	if (sSerialDebugEnabled)
		arch_debug_serial_putchar(c);
	if (sBlueScreenEnabled || sDebugScreenEnabled)
		blue_screen_putchar(c);
	for (uint32 i = 0; sSerialDebugEnabled && i < kMaxDebuggerModules; i++)
		if (sDebuggerModules[i] && sDebuggerModules[i]->debugger_puts)
			sDebuggerModules[i]->debugger_puts(&c, sizeof(c));
}


/**
 * @brief Writes a string through the active output filter.
 *
 * Intended for use from KDL command handlers; falls through silently if no
 * filter is installed (i.e. not currently inside a KDL session).
 *
 * @param s NUL-terminated string to print.
 */
void
kputs(const char* s)
{
	if (sDebugOutputFilter != NULL)
		sDebugOutputFilter->PrintString(s);
}


/**
 * @brief Writes a string bypassing any currently installed filter.
 *
 * Always uses the default filter, so text reaches the serial/screen sinks
 * even when a GDB stub or similar has hijacked output.
 *
 * @param s NUL-terminated string to print.
 */
void
kputs_unfiltered(const char* s)
{
	gDefaultDebugOutputFilter.PrintString(s);
}


/**
 * @brief Inserts a run of characters into the KDL edit buffer at the cursor.
 *
 * Shifts the tail of the line right to make room, copies the new bytes,
 * echoes them to the terminal, and repositions the cursor with an ANSI CUB
 * sequence when the insertion happened mid-line.
 *
 * @param buffer Backing edit buffer.
 * @param position [in,out] Cursor position; advanced by @a charCount.
 * @param length [in,out] Current buffer length; grown by @a charCount.
 * @param chars Characters to insert.
 * @param charCount Number of characters in @a chars.
 */
static void
insert_chars_into_line(char* buffer, int32& position, int32& length,
	const char* chars, int32 charCount)
{
	// move the following chars to make room for the ones to insert
	if (position < length) {
		memmove(buffer + position + charCount, buffer + position,
			length - position);
	}

	// insert chars
	memcpy(buffer + position, chars, charCount);
	int32 oldPosition = position;
	position += charCount;
	length += charCount;

	// print the new chars (and the following ones)
	kprintf("%.*s", (int)(length - oldPosition),
		buffer + oldPosition);

	// reposition cursor, if necessary
	if (position < length)
		kprintf("\x1b[%" B_PRId32 "D", length - position);
}


/**
 * @brief Convenience wrapper that inserts a single character.
 *
 * @param buffer Backing edit buffer.
 * @param position [in,out] Cursor position; advanced by one.
 * @param length [in,out] Current buffer length; grown by one.
 * @param c Character to insert.
 */
static void
insert_char_into_line(char* buffer, int32& position, int32& length, char c)
{
	insert_chars_into_line(buffer, position, length, &c, 1);
}


/**
 * @brief Deletes the character under the cursor and redraws the line tail.
 *
 * If the cursor is already at the end of the line the call is a no-op. The
 * trailing residue is overwritten with a space and the cursor is moved back
 * into place with an ANSI CUB sequence.
 *
 * @param buffer Backing edit buffer.
 * @param position [in,out] Cursor position; unchanged on return.
 * @param length [in,out] Current buffer length; decremented by one when a
 *   character was removed.
 */
static void
remove_char_from_line(char* buffer, int32& position, int32& length)
{
	if (position == length)
		return;

	length--;

	if (position < length) {
		// move the subsequent chars
		memmove(buffer + position, buffer + position + 1, length - position);

		// print the rest of the line again, if necessary
		for (int32 i = position; i < length; i++)
			kputchar(buffer[i]);
	}

	// visually clear the last char
	kputchar(' ');

	// reposition the cursor
	kprintf("\x1b[%" B_PRId32 "D", length - position + 1);
}


class LineEditingHelper {
public:
	virtual	~LineEditingHelper() {}

	virtual	void TabCompletion(char* buffer, int32 capacity, int32& position,
		int32& length) = 0;
};


class CommandLineEditingHelper : public LineEditingHelper {
public:
	/**
	 * @brief Default constructs a KDL command-line tab completion helper.
	 */
	CommandLineEditingHelper()
	{
	}

	virtual	~CommandLineEditingHelper() {}

	/**
	 * @brief Expands or suggests debugger command names on TAB.
	 *
	 * If the line already contains a space, prints the usage of the command
	 * before the space. Otherwise it enumerates matching registered commands:
	 * a single hit is auto-completed and followed by a space, a common prefix
	 * longer than what has been typed is appended, and multiple matches are
	 * printed in columns. Called from KDL context with interrupts disabled.
	 *
	 * @param buffer Edit buffer holding the partial command.
	 * @param capacity Total capacity of @a buffer including the NUL.
	 * @param position [in,out] Cursor position, adjusted as text is inserted.
	 * @param length [in,out] Current line length, grown as text is inserted.
	 */
	virtual	void TabCompletion(char* buffer, int32 capacity, int32& position,
		int32& length)
	{
		// find the first space
		char tmpChar = buffer[position];
		buffer[position] = '\0';
		char* firstSpace = strchr(buffer, ' ');
		buffer[position] = tmpChar;

		bool reprintLine = false;

		if (firstSpace != NULL) {
			// a complete command -- print its help

			// get the command
			tmpChar = *firstSpace;
			*firstSpace = '\0';
			bool ambiguous;
			debugger_command* command = find_debugger_command(buffer, true, ambiguous);
			*firstSpace = tmpChar;

			if (command != NULL) {
				kputs("\n");
				print_debugger_command_usage(command->name);
			} else {
				if (ambiguous)
					kprintf("\nambiguous command\n");
				else
					kprintf("\nno such command\n");
			}

			reprintLine = true;
		} else {
			// a partial command -- look for completions

			// check for possible completions
			int32 count = 0;
			int32 longestName = 0;
			debugger_command* command = NULL;
			int32 longestCommonPrefix = 0;
			const char* previousCommandName = NULL;
			while ((command = next_debugger_command(command, buffer, position))
					!= NULL) {
				count++;
				int32 nameLength = strlen(command->name);
				longestName = max_c(longestName, nameLength);

				// updated the length of the longest common prefix of the
				// commands
				if (count == 1) {
					longestCommonPrefix = longestName;
				} else {
					longestCommonPrefix = min_c(longestCommonPrefix,
						nameLength);

					for (int32 i = position; i < longestCommonPrefix; i++) {
						if (previousCommandName[i] != command->name[i]) {
							longestCommonPrefix = i;
							break;
						}
					}
				}

				previousCommandName = command->name;
			}

			if (count == 0) {
				// no possible completions
				kprintf("\nno completions\n");
				reprintLine = true;
			} else if (count == 1) {
				// exactly one completion
				command = next_debugger_command(NULL, buffer, position);

				// check for sufficient space in the buffer
				int32 neededSpace = longestName - position + 1;
					// remainder of the name plus one space
				// also consider the terminating null char
				if (length + neededSpace + 1 >= capacity)
					return;

				insert_chars_into_line(buffer, position, length,
					command->name + position, longestName - position);
				insert_char_into_line(buffer, position, length, ' ');
			} else if (longestCommonPrefix > position) {
				// multiple possible completions with longer common prefix
				// -- insert the remainder of the common prefix

				// check for sufficient space in the buffer
				int32 neededSpace = longestCommonPrefix - position;
				// also consider the terminating null char
				if (length + neededSpace + 1 >= capacity)
					return;

				insert_chars_into_line(buffer, position, length,
					previousCommandName + position, neededSpace);
			} else {
				// multiple possible completions without longer common prefix
				// -- print them all
				kprintf("\n");
				reprintLine = true;

				int columns = 80 / (longestName + 2);
				debugger_command* command = NULL;
				int column = 0;
				while ((command = next_debugger_command(command, buffer, position))
						!= NULL) {
					// spacing
					if (column > 0 && column % columns == 0)
						kputs("\n");
					column++;

					kprintf("  %-*s", (int)longestName, command->name);
				}
				kputs("\n");
			}
		}

		// reprint the editing line, if necessary
		if (reprintLine) {
			kprintf("%s%.*s", kKDLPrompt, (int)length, buffer);
			if (position < length)
				kprintf("\x1b[%" B_PRId32 "D", length - position);
		}
	}
};


/**
 * @brief Reads a single line of input inside the kernel debugger.
 *
 * Implements the KDL line editor: cursor movement, backspace, delete,
 * kill-line (CTRL-K), screen clear (CTRL-L), history navigation (up/down
 * and PgUp/PgDn prefix search), GDB remote-protocol autodetect, and
 * optional TAB completion via @a editingHelper. Pulls one character at a
 * time from kgetc() and must only be called from KDL context (interrupts
 * disabled).
 *
 * @param buffer Destination line buffer.
 * @param maxLength Capacity of @a buffer in bytes.
 * @param editingHelper Optional helper used to service TAB completion.
 * @return Number of characters stored in @a buffer including the trailing
 *   NUL.
 */
static int
read_line(char* buffer, int32 maxLength,
	LineEditingHelper* editingHelper = NULL)
{
	int32 currentHistoryLine = sCurrentLine;
	int32 position = 0;
	int32 length = 0;
	bool done = false;
	char c = 0;

	while (!done) {
		c = kgetc();

		switch (c) {
			case '\n':
			case '\r':
				buffer[length++] = '\0';
				kputs("\n");
				done = true;
				break;
			case '\t':
			{
				if (editingHelper != NULL) {
					editingHelper->TabCompletion(buffer, maxLength,
						position, length);
				}
				break;
			}
			case 8:		// backspace (CTRL-H)
			case 0x7f:	// backspace (xterm)
				if (position > 0) {
					kputs("\x1b[1D"); // move to the left one
					position--;
					remove_char_from_line(buffer, position, length);
				}
				break;
			case 0x1f & 'D':	// CTRL-D -- continue
				length = 0;
				buffer[length++] = 'e';
				buffer[length++] = 's';
				buffer[length++] = '\0';
				kputchar('\n');
				done = true;
				break;
			case 0x1f & 'K':	// CTRL-K -- clear line after current position
				if (position < length) {
					// clear chars
					for (int32 i = position; i < length; i++)
						kputchar(' ');

					// reposition cursor
					kprintf("\x1b[%" B_PRId32 "D", length - position);

					length = position;
				}
				break;
			case 0x1f & 'L':	// CTRL-L -- clear screen
				if (sBlueScreenOutput) {
					// All the following needs to be transparent for the
					// serial debug output. I.e. after clearing the screen
					// we have to get the on-screen line into the visual state
					// it should have.

					// clear screen
					blue_screen_clear_screen();

					// reprint line
					buffer[length] = '\0';
					blue_screen_puts(kKDLPrompt);
					blue_screen_puts(buffer);

					// reposition cursor
					if (position < length) {
						for (int i = length; i > position; i--)
							blue_screen_puts("\x1b[1D");
					}
				}
				break;
			case 27: // escape sequence
				c = kgetc();
				if (c != '[') {
					// ignore broken escape sequence
					break;
				}
				c = kgetc();
				switch (c) {
					case 'C': // right arrow
						if (position < length) {
							kputs("\x1b[1C"); // move to the right one
							position++;
						}
						break;
					case 'D': // left arrow
						if (position > 0) {
							kputs("\x1b[1D"); // move to the left one
							position--;
						}
						break;
					case 'A': // up arrow
					case 'B': // down arrow
					{
						int32 historyLine = 0;

						if (c == 'A') {
							// up arrow
							historyLine = currentHistoryLine - 1;
							if (historyLine < 0)
								historyLine = HISTORY_SIZE - 1;
						} else {
							// down arrow
							if (currentHistoryLine == sCurrentLine)
								break;

							historyLine = currentHistoryLine + 1;
							if (historyLine >= HISTORY_SIZE)
								historyLine = 0;
						}

						// clear the history again if we're in the current line again
						// (the buffer we get just is the current line buffer)
						if (historyLine == sCurrentLine) {
							sLineBuffer[historyLine][0] = '\0';
						} else if (sLineBuffer[historyLine][0] == '\0') {
							// empty history lines are unused -- so bail out
							break;
						}

						// swap the current line with something from the history
						if (position > 0)
							kprintf("\x1b[%" B_PRId32 "D", position); // move to beginning of line

						strcpy(buffer, sLineBuffer[historyLine]);
						length = position = strlen(buffer);
						kprintf("%s\x1b[K", buffer); // print the line and clear the rest
						currentHistoryLine = historyLine;
						break;
					}
					case '5':	// if "5~", it's PAGE UP
					case '6':	// if "6~", it's PAGE DOWN
					{
						if (kgetc() != '~')
							break;

						// PAGE UP: search backward, PAGE DOWN: forward
						int32 searchDirection = (c == '5' ? -1 : 1);

						bool found = false;
						int32 historyLine = currentHistoryLine;
						do {
							historyLine = (historyLine + searchDirection
								+ HISTORY_SIZE) % HISTORY_SIZE;
							if (historyLine == sCurrentLine)
								break;

							if (strncmp(sLineBuffer[historyLine], buffer,
									position) == 0) {
								found = true;
							}
						} while (!found);

						// bail out, if we've found nothing or hit an empty
						// (i.e. unused) history line
						if (!found || strlen(sLineBuffer[historyLine]) == 0)
							break;

						// found a suitable line -- replace the current buffer
						// content with it
						strcpy(buffer, sLineBuffer[historyLine]);
						length = strlen(buffer);
						kprintf("%s\x1b[K", buffer + position);
							// print the line and clear the rest
						kprintf("\x1b[%" B_PRId32 "D", length - position);
							// reposition cursor
						currentHistoryLine = historyLine;

						break;
					}
					case 'H': // home
					{
						if (position > 0) {
							kprintf("\x1b[%" B_PRId32 "D", position);
							position = 0;
						}
						break;
					}
					case 'F': // end
					{
						if (position < length) {
							kprintf("\x1b[%" B_PRId32 "C", length - position);
							position = length;
						}
						break;
					}
					case '3':	// if "3~", it's DEL
					{
						if (kgetc() != '~')
							break;

						if (position < length)
							remove_char_from_line(buffer, position, length);

						break;
					}
					default:
						break;
				}
				break;
			case '$':
			case '+':
				if (!sBlueScreenOutput) {
					/* HACK ALERT!!!
					 *
					 * If we get a $ at the beginning of the line
					 * we assume we are talking with GDB
					 */
					if (position == 0) {
						strcpy(buffer, "gdb");
						position = 4;
						done = true;
						break;
					}
				}
				/* supposed to fall through */
			default:
				if (isprint(c))
					insert_char_into_line(buffer, position, length, c);
				break;
		}

		if (length >= maxLength - 2) {
			buffer[length++] = '\0';
			kputs("\n");
			done = true;
			break;
		}
	}

	return length;
}


/**
 * @brief Blocking single-character read from any KDL input source.
 *
 * Polls the serial port, the blue-screen keyboard driver, and any
 * registered debugger module's @c debugger_getchar hook in turn, sleeping
 * briefly between rounds. Only safe inside KDL, where interrupts are
 * disabled and the normal keyboard stack is unavailable.
 *
 * @return The character read. This function never returns on error; it
 *   loops until some source yields input.
 */
char
kgetc(void)
{
	while (true) {
		// check serial input
		int c = arch_debug_serial_try_getchar();
		if (c >= 0)
			return (char)c;

		// check blue screen input
		if (sBlueScreenOutput) {
			c = blue_screen_try_getchar();
			if (c >= 0)
				return (char)c;
		}

		// give the kernel debugger modules a chance
		for (uint32 i = 0; i < kMaxDebuggerModules; i++) {
			if (sDebuggerModules[i] && sDebuggerModules[i]->debugger_getchar) {
				int getChar = sDebuggerModules[i]->debugger_getchar();
				if (getChar >= 0)
					return (char)getChar;
			}
		}

		arch_debug_snooze(5000);
	}
}


/**
 * @brief Reads a line from the KDL console without tab completion.
 *
 * Thin C-linkage wrapper over read_line() used by helpers that just want
 * raw line input (for example the GDB protocol implementation). KDL only.
 *
 * @param buffer Destination buffer.
 * @param length Capacity of @a buffer in bytes.
 * @return Number of characters stored including the trailing NUL.
 */
int
kgets(char* buffer, int length)
{
	return read_line(buffer, length);
}


/**
 * @brief Prints the prefix and message passed to the current KDL entry.
 *
 * Used on first entry and by the @c message KDL command. If the saved
 * format string contains the command separator ("@!"), only the portion
 * before the separator is printed: the trailing part is a command list
 * reserved for execute_panic_commands(). KDL context only.
 */
static void
print_kernel_debugger_message()
{
	if (sCurrentKernelDebuggerMessagePrefix != NULL
			|| sCurrentKernelDebuggerMessage != NULL) {
		if (sCurrentKernelDebuggerMessagePrefix != NULL)
			kprintf("%s", sCurrentKernelDebuggerMessagePrefix);
		if (sCurrentKernelDebuggerMessage != NULL
				&& sDebugOutputFilter != NULL) {
			va_list args;
			va_copy(args, sCurrentKernelDebuggerMessageArgs);

			if (const char* commandDelimiter = strstr(
					sCurrentKernelDebuggerMessage,
					kKDLMessageCommandSeparator)) {
				// The message string contains a list of commands to be
				// executed when entering the kernel debugger. We don't
				// want to print those, so we copy the interesting part of
				// the format string.
				if (commandDelimiter != sCurrentKernelDebuggerMessage) {
					size_t length = commandDelimiter
						- sCurrentKernelDebuggerMessage;
					if (char* format = (char*)debug_malloc(length + 1)) {
						memcpy(format, sCurrentKernelDebuggerMessage, length);
						format[length] = '\0';
						sDebugOutputFilter->Print(format, args);
						debug_free(format);
					} else {
						// allocation failed -- just print everything
						sDebugOutputFilter->Print(sCurrentKernelDebuggerMessage,
							args);
					}
				}
			} else
				sDebugOutputFilter->Print(sCurrentKernelDebuggerMessage, args);

			va_end(args);
		}

		kprintf("\n");
	}
}


/**
 * @brief Evaluates the command suffix embedded in a panic() format string.
 *
 * panic() callers may append "@!<cmds>" to their message to have a list of
 * KDL commands executed automatically on entry. This function expands the
 * format string, locates that suffix, and feeds it to
 * evaluate_debug_command(). KDL context only.
 */
static void
execute_panic_commands()
{
	if (sCurrentKernelDebuggerMessage == NULL
		|| strstr(sCurrentKernelDebuggerMessage,
				kKDLMessageCommandSeparator) == NULL) {
		return;
	}

	// Indeed there are commands to execute.
	const size_t kCommandBufferSize = 512;
	char* commandBuffer = (char*)debug_malloc(kCommandBufferSize);
	if (commandBuffer != NULL) {
		va_list tempArgs;
		va_copy(tempArgs, sCurrentKernelDebuggerMessageArgs);

		if (vsnprintf(commandBuffer, kCommandBufferSize,
				sCurrentKernelDebuggerMessage, tempArgs)
					< (int)kCommandBufferSize) {
			const char* commands = strstr(commandBuffer,
				kKDLMessageCommandSeparator);
			if (commands != NULL) {
				commands += strlen(kKDLMessageCommandSeparator);
				kprintf("initial commands: %s\n", commands);
				evaluate_debug_command(commands);
			}
		}

		va_end(tempArgs);

		debug_free(commandBuffer);
	}
}


/**
 * @brief Trampoline that invokes the architecture stack tracer.
 *
 * Packaged as a @c void(*)(void*) so it can be launched under
 * debug_call_with_fault_handler() and survive a page fault while walking
 * a corrupt stack.
 *
 * @param ignored Parameter required by the fault-handler calling
 *   convention; unused.
 */
static void
stack_trace_trampoline(void*)
{
	arch_debug_stack_trace();
}


/**
 * @brief Runs the interactive KDL prompt until the user quits.
 *
 * Establishes a debug alloc pool, prints the banner and any entry message,
 * initialises the @c _cpu/_thread/_team debug variables, dumps a stack
 * trace when appropriate, executes the panic() command suffix, then enters
 * the read-line / evaluate loop. Must be called from KDL context with
 * interrupts disabled and after enter_kernel_debugger() has taken effect.
 *
 * @param messagePrefix Optional prefix already printed to distinguish the
 *   entry reason (for example "PANIC: ").
 * @param message Optional printf-style message passed to panic()/
 *   kernel_debugger().
 * @param args Variadic list matching @a message.
 * @param cpu Index of the CPU driving the KDL session.
 */
static void
kernel_debugger_loop(const char* messagePrefix, const char* message,
	va_list args, int32 cpu)
{
	DebugAllocPool* allocPool = create_debug_alloc_pool();

	sCurrentKernelDebuggerMessagePrefix = messagePrefix;
	sCurrentKernelDebuggerMessage = message;
	if (sCurrentKernelDebuggerMessage != NULL)
		va_copy(sCurrentKernelDebuggerMessageArgs, args);

	sSyslogDebuggerOffset = sSyslogBuffer != NULL
		? ring_buffer_readable(sSyslogBuffer) : 0;

	print_kernel_debugger_message();

	kprintf("Welcome to Kernel Debugging Land...\n");
	kprintf("revision: %s\n", get_haiku_revision());

	// Set a few temporary debug variables and print on which CPU and in which
	// thread we are running.
	set_debug_variable("_cpu", sDebuggerOnCPU);

	Thread* thread = thread_get_current_thread();
	if (thread == NULL) {
		kprintf("Running on CPU %" B_PRId32 "\n", sDebuggerOnCPU);
	} else if (!debug_is_kernel_memory_accessible((addr_t)thread,
			sizeof(Thread), B_KERNEL_READ_AREA)) {
		kprintf("Running on CPU %" B_PRId32 "\n", sDebuggerOnCPU);
		kprintf("Current thread pointer is %p, which is an address we "
			"can't read from.\n", thread);
		arch_debug_unset_current_thread();
	} else {
		set_debug_variable("_thread", (uint64)(addr_t)thread);
		set_debug_variable("_threadID", thread->id);

		kprintf("Thread %" B_PRId32 " \"%.64s\" running on CPU %" B_PRId32 "\n",
			thread->id, thread->name, sDebuggerOnCPU);

		if (thread->cpu != gCPU + cpu) {
			kprintf("The thread's CPU pointer is %p, but should be %p.\n",
				thread->cpu, gCPU + cpu);
			arch_debug_unset_current_thread();
		} else if (thread->team != NULL) {
			if (debug_is_kernel_memory_accessible((addr_t)thread->team,
					sizeof(Team), B_KERNEL_READ_AREA)) {
				set_debug_variable("_team", (uint64)(addr_t)thread->team);
				set_debug_variable("_teamID", thread->team->id);
			} else {
				kprintf("The thread's team pointer is %p, which is an "
					"address we can't read from.\n", thread->team);
				arch_debug_unset_current_thread();
			}
		}
	}

	if (!has_debugger_command("help") || message != NULL) {
		// No commands yet or we came here via a panic(). Always print a stack
		// trace in these cases.
		jmp_buf* jumpBuffer = (jmp_buf*)debug_malloc(sizeof(jmp_buf));
		if (jumpBuffer != NULL) {
			debug_call_with_fault_handler(*jumpBuffer, &stack_trace_trampoline,
				NULL);
			debug_free(jumpBuffer);
		} else
			arch_debug_stack_trace();
	}

	if (has_debugger_command("help")) {
		// Commands are registered already -- execute panic() commands. Do that
		// with paging disabled, so everything is printed, even if the user
		// can't use the keyboard.
		bool pagingEnabled = blue_screen_paging_enabled();
		blue_screen_set_paging(false);

		execute_panic_commands();

		blue_screen_set_paging(pagingEnabled);
	}

	int32 continuableLine = -1;
		// Index of the previous command line, if the command returned
		// B_KDEBUG_CONT, i.e. asked to be repeatable, -1 otherwise.

	for (;;) {
		CommandLineEditingHelper editingHelper;
		kprintf(kKDLPrompt);
		char* line = sLineBuffer[sCurrentLine];
		read_line(line, LINE_BUFFER_SIZE, &editingHelper);

		// check, if the line is empty or whitespace only
		bool whiteSpaceOnly = true;
		for (int i = 0 ; line[i] != '\0'; i++) {
			if (!isspace(line[i])) {
				whiteSpaceOnly = false;
				break;
			}
		}

		if (whiteSpaceOnly) {
			if (continuableLine < 0)
				continue;

			// the previous command can be repeated
			sCurrentLine = continuableLine;
			line = sLineBuffer[sCurrentLine];
		}

		int rc = evaluate_debug_command(line);

		if (rc == B_KDEBUG_QUIT) {
			// okay, exit now.
			break;
		}

		// If the command is continuable, remember the current line index.
		continuableLine = (rc == B_KDEBUG_CONT ? sCurrentLine : -1);

		int previousLine = sCurrentLine - 1;
		if (previousLine < 0)
			previousLine = HISTORY_SIZE - 1;

		// Only use the next slot in the history, if the entries differ
		if (strcmp(sLineBuffer[sCurrentLine], sLineBuffer[previousLine])) {
			if (++sCurrentLine >= HISTORY_SIZE)
				sCurrentLine = 0;
		}
	}

	if (sCurrentKernelDebuggerMessage != NULL)
		va_end(sCurrentKernelDebuggerMessageArgs);

	delete_debug_alloc_pool(allocPool);
}


/**
 * @brief Takes over the machine for a new KDL session.
 *
 * Races with other CPUs on @c sInDebugger; the winner saves CPU registers,
 * enables dprintf output, broadcasts an SMP_MSG_CPU_HALT ICI so peers spin
 * in debug_trap_cpu_in_kdl(), switches to blue-screen output when
 * configured, installs the default output filter, and sorts the command
 * table. Interrupts must already be disabled on entry.
 *
 * @param cpu Index of the entering CPU.
 * @param previousCPU [out] Receives the previous value of
 *   @c sDebuggerOnCPU; used on recursive entries so exit can restore it.
 */
static void
enter_kernel_debugger(int32 cpu, int32& previousCPU)
{
	while (atomic_add(&sInDebugger, 1) > 0) {
		atomic_add(&sInDebugger, -1);

		// The debugger is already running, find out where...
		if (sDebuggerOnCPU == cpu) {
			// We are re-entering the debugger on the same CPU.
			break;
		}

		// Some other CPU must have entered the debugger and tried to halt
		// us. Process ICIs to ensure we get the halt request. Then we are
		// blocking there until everyone leaves the debugger and we can
		// try to enter it again.
		smp_intercpu_interrupt_handler(cpu);
	}

	arch_debug_save_registers(&sDebugRegisters[cpu]);
	sPreviousDprintfState = set_dprintf_enabled(true);

	if (!gKernelStartup && sDebuggerOnCPU != cpu && smp_get_num_cpus() > 1) {
		// First entry on a MP system, send a halt request to all of the other
		// CPUs. Should they try to enter the debugger they will be caught in
		// the loop above.
		CPUSet cpuMask;
		cpuMask.SetAll();
		cpuMask.ClearBit(cpu);
		smp_multicast_ici_interrupts_disabled(cpu, cpuMask, SMP_MSG_CPU_HALT, 0, 0,
			0, NULL, SMP_MSG_FLAG_SYNC);
	}

	previousCPU = sDebuggerOnCPU;
	sDebuggerOnCPU = cpu;

	if (sBlueScreenOutput) {
		if (blue_screen_enter(false) == B_OK)
			sBlueScreenEnabled = true;
	}

	sDebugOutputFilter = &gDefaultDebugOutputFilter;

	sDebuggedThread = NULL;

	// sort the commands
	sort_debugger_commands();

	call_modules_hook(true);
}


/**
 * @brief Tears down the KDL session and resumes normal operation.
 *
 * Mirrors enter_kernel_debugger(): notifies debugger modules, restores
 * dprintf state, drops the output filter, and decrements @c sInDebugger
 * which unblocks halted peer CPUs waiting in debug_trap_cpu_in_kdl().
 */
static void
exit_kernel_debugger()
{
	call_modules_hook(false);
	set_dprintf_enabled(sPreviousDprintfState);

	sDebugOutputFilter = NULL;

	sBlueScreenEnabled = false;
	if (sDebugScreenEnabled)
		blue_screen_enter(true);

	atomic_add(&sInDebugger, -1);
}


/**
 * @brief Blocks until another CPU has picked up the KDL session.
 *
 * Invoked after the user types "cpu <n>": spins on @c sHandOverKDLToCPU
 * while the target CPU, trapped in debug_trap_cpu_in_kdl(), accepts the
 * hand-off and clears the variable. KDL context only.
 */
static void
hand_over_kernel_debugger()
{
	// Wait until the hand-over is complete.
	// The other CPU gets our sInDebugger reference and will release it when
	// done. Note, that there's a small race condition: the other CPU could
	// hand over to another CPU without us noticing. Since this is only
	// initiated by the user, it is harmless, though.
	sHandOverKDL = true;
	while (atomic_get(&sHandOverKDLToCPU) >= 0)
		cpu_wait(&sHandOverKDLToCPU, -1);
}


/**
 * @brief Central KDL dispatch: enter, run, and possibly hand over.
 *
 * Handles the first entry, recursive re-entry, and CPU-to-CPU hand-off in
 * a single loop. The outer loop body enters the debugger, runs the prompt,
 * and either exits cleanly, continues on the same CPU, or waits for the
 * designated successor to pick up the session. Must be called with
 * interrupts disabled.
 *
 * @param messagePrefix Optional entry prefix (e.g. "PANIC: ").
 * @param message Optional entry message; may embed a panic command suffix.
 * @param args Variadic arguments for @a message.
 * @param cpu Index of the CPU making the call.
 */
static void
kernel_debugger_internal(const char* messagePrefix, const char* message,
	va_list args, int32 cpu)
{
	while (true) {
		// If we're called recursively sDebuggerOnCPU will be != -1.
		int32 previousCPU = -1;

		if (sHandOverKDLToCPU == cpu) {
			sHandOverKDLToCPU = -1;
			sHandOverKDL = false;

			previousCPU = sDebuggerOnCPU;
			sDebuggerOnCPU = cpu;
		} else
			enter_kernel_debugger(cpu, previousCPU);

		kernel_debugger_loop(messagePrefix, message, args, cpu);

		if (sHandOverKDLToCPU < 0 && previousCPU == -1) {
			// We're not handing over to a different CPU and we weren't
			// called recursively, so we'll exit the debugger.
			exit_kernel_debugger();
		}

		sDebuggerOnCPU = previousCPU;

		if (sHandOverKDLToCPU < 0)
			break;

		hand_over_kernel_debugger();

		debug_trap_cpu_in_kdl(cpu, true);

		if (sHandOverKDLToCPU != cpu)
			break;
	}
}


/**
 * @brief KDL command handler: reprints the current entry message.
 *
 * Bound to the "message" command. Invoked with interrupts disabled inside
 * a KDL session.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Always 0.
 */
static int
cmd_dump_kdl_message(int argc, char** argv)
{
	print_kernel_debugger_message();
	return 0;
}


/**
 * @brief KDL command handler: re-runs the panic() command suffix.
 *
 * Bound to the "panic_commands" command. Invoked with interrupts disabled.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Always 0.
 */
static int
cmd_execute_panic_commands(int argc, char** argv)
{
	execute_panic_commands();
	return 0;
}


/**
 * @brief KDL command handler: dumps the kernel syslog ring buffer.
 *
 * Accepts "-n" to limit output to unsent messages, and "-k" to include the
 * KDL-session output that is otherwise suppressed. Reads through the ring
 * buffer in chunks, stripping padding bytes, and prints to the active
 * output filter. Interrupts-off invariant of KDL applies.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on completion.
 */
static int
cmd_dump_syslog(int argc, char** argv)
{
	if (!sSyslogOutputEnabled) {
		kprintf("Syslog is not enabled.\n");
		return 0;
	}

	bool unsentOnly = false;
	bool ignoreKDLOutput = true;

	int argi = 1;
	for (; argi < argc; argi++) {
		if (strcmp(argv[argi], "-n") == 0)
			unsentOnly = true;
		else if (strcmp(argv[argi], "-k") == 0)
			ignoreKDLOutput = false;
		else
			break;
	}

	if (argi < argc) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	size_t debuggerOffset = sSyslogDebuggerOffset;
	size_t start = unsentOnly ? sSyslogBufferOffset : 0;
	size_t end = ignoreKDLOutput
		? debuggerOffset : ring_buffer_readable(sSyslogBuffer);

	// allocate a buffer for processing the syslog output
	size_t bufferSize = 1024;
	char* buffer = (char*)debug_malloc(bufferSize);
	char stackBuffer[64];
	if (buffer == NULL) {
		buffer = stackBuffer;
		bufferSize = sizeof(stackBuffer);
	}

	// filter the output
	bool newLine = false;
	while (start < end) {
		size_t bytesRead = ring_buffer_peek(sSyslogBuffer, start, buffer,
			std::min(end - start, bufferSize - 1));
		if (bytesRead == 0)
			break;
		start += bytesRead;

		// remove '\0' and 0xcc
		size_t toPrint = 0;
		for (size_t i = 0; i < bytesRead; i++) {
			if (buffer[i] != '\0' && (uint8)buffer[i] != 0xcc)
				buffer[toPrint++] = buffer[i];
		}

		if (toPrint > 0) {
			newLine = buffer[toPrint - 1] == '\n';
			buffer[toPrint] = '\0';
			kputs(buffer);
		}

		if (debuggerOffset > sSyslogDebuggerOffset) {
			// Our output caused older syslog output to be evicted from the
			// syslog buffer. We need to adjust our offsets accordingly. Note,
			// this can still go wrong, if the buffer was already full and more
			// was written to it than we have processed, but we can't help that.
			size_t diff = debuggerOffset - sSyslogDebuggerOffset;
			start -= std::min(start, diff);
			end -= std::min(end, diff);
			debuggerOffset = sSyslogDebuggerOffset;
		}
	}

	if (!newLine)
		kputs("\n");

	if (buffer != stackBuffer)
		debug_free(buffer);

	return 0;
}


/**
 * @brief KDL command handler: displays or switches the active KDL CPU.
 *
 * With no argument, prints the CPU currently running the prompt. With one
 * argument, records the hand-off target in @c sHandOverKDLToCPU and
 * returns @c B_KDEBUG_QUIT so the loop drops into hand_over_kernel_debugger().
 * Interrupts-off invariant of KDL applies.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return @c B_KDEBUG_QUIT when a hand-off was requested, 0 otherwise.
 */
static int
cmd_switch_cpu(int argc, char** argv)
{
	if (argc > 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (argc == 1) {
		kprintf("running on CPU %" B_PRId32 "\n", smp_get_current_cpu());
		return 0;
	}

	int32 newCPU = parse_expression(argv[1]);
	if (newCPU < 0 || newCPU >= smp_get_num_cpus()) {
		kprintf("invalid CPU index\n");
		return 0;
	}

	if (newCPU == smp_get_current_cpu()) {
		kprintf("already running on CPU %" B_PRId32 "\n", newCPU);
		return 0;
	}

	sHandOverKDLToCPU = newCPU;

	return B_KDEBUG_QUIT;
}


/**
 * @brief Kernel thread that forwards syslog data to the userland daemon.
 *
 * Waits on @c sSyslogNotify (with a 5 s timeout so it also polls), drains
 * the syslog ring buffer under @c sOutputLock and @c sSpinlock, and posts
 * SYSLOG_MESSAGE packets to @c sSyslogPort. If the daemon's port
 * disappears the thread exits; transient failures cause a retry using the
 * previously formatted buffer. Thread context only.
 *
 * @param data Unused thread argument.
 * @return @c B_BAD_PORT_ID when the destination port is gone; otherwise
 *   the loop never returns.
 */
static status_t
syslog_sender(void* data)
{
	bool bufferPending = false;
	int32 length = 0;

	while (true) {
		// wait for syslog data to become available
		acquire_sem_etc(sSyslogNotify, 1, B_RELATIVE_TIMEOUT, 5000000);
			// Note: We time out since in some situations output is added to
			// the syslog buffer without being allowed to notify us (e.g. in
			// the kernel debugger).
			// TODO: A semaphore is rather unhandy here. It is released for
			// every single message written to the buffer, but we potentially
			// send a lot more than a single message per iteration. On the other
			// hand, as long as the syslog daemon is not running, we acquire
			// the semaphore anyway. A better solution would be a flag + a
			// condition variable.

		sSyslogMessage->when = real_time_clock();

		if (!bufferPending) {
			// We need to have exclusive access to our syslog buffer
			MutexLocker mutexLocker(sOutputLock);
			InterruptsSpinLocker spinLocker(sSpinlock);

			length = ring_buffer_readable(sSyslogBuffer)
				- sSyslogBufferOffset;
			if (length > (int32)SYSLOG_MAX_MESSAGE_LENGTH)
				length = SYSLOG_MAX_MESSAGE_LENGTH;

			uint8* message = (uint8*)sSyslogMessage->message;
			if (sSyslogDropped) {
				memcpy(message, "<DROP>", 6);
				message += 6;
				if ((length + 6) > (int32)SYSLOG_MAX_MESSAGE_LENGTH)
					length -= 6;
				sSyslogDropped = false;
			}

			length = ring_buffer_peek(sSyslogBuffer, sSyslogBufferOffset,
				message, length);
			sSyslogBufferOffset += length;
			length += (addr_t)message - (addr_t)sSyslogMessage->message;
		}

		if (length == 0) {
			// The buffer we came here for might have been sent already
			bufferPending = false;
			continue;
		}

		status_t status = write_port_etc(sSyslogPort, SYSLOG_MESSAGE,
			sSyslogMessage, sizeof(struct syslog_message) + length,
			B_RELATIVE_TIMEOUT, 0);
		if (status == B_BAD_PORT_ID) {
			// The port is gone, there is no need to run anymore
			sSyslogWriter = -1;
			return status;
		}

		if (status != B_OK) {
			// Sending has failed - just wait, maybe it'll work later.
			bufferPending = true;
			continue;
		}

		if (bufferPending) {
			// We could write the last pending buffer, try to read more
			// from the syslog ring buffer
			release_sem_etc(sSyslogNotify, 1, B_DO_NOT_RESCHEDULE);
			bufferPending = false;
		}
	}

	return 0;
}


/**
 * @brief Appends text to the syslog ring buffer, dropping older data if full.
 *
 * Called from both thread context (via dprintf) and KDL/interrupt context
 * via the default output filter, so it assumes the caller holds the
 * appropriate lock. Text longer than the buffer is truncated with a
 * "<TRUNC>" marker. If space must be reclaimed, older bytes are flushed
 * and @c sSyslogDropped is raised so the sender prepends "<DROP>".
 *
 * @param text Bytes to append.
 * @param length Number of bytes in @a text.
 * @param notify When @c true, releases @c sSyslogNotify to wake the sender.
 */
static void
syslog_write(const char* text, int32 length, bool notify)
{
	if (sSyslogBuffer == NULL)
		return;

	if (length > sSyslogBuffer->size) {
		syslog_write("<TRUNC>", 7, false);

		text += length - (sSyslogBuffer->size - 7);
		length = sSyslogBuffer->size - 7;
	}

	int32 writable = ring_buffer_writable(sSyslogBuffer);
	if (writable < length) {
		// drop old data
		size_t toDrop = length - writable;
		ring_buffer_flush(sSyslogBuffer, toDrop);

		if (toDrop > sSyslogBufferOffset) {
			sSyslogBufferOffset = 0;
			sSyslogDropped = true;
		} else
			sSyslogBufferOffset -= toDrop;

		sSyslogDebuggerOffset -= std::min(toDrop, sSyslogDebuggerOffset);
	}

	ring_buffer_write(sSyslogBuffer, (uint8*)text, length);

	if (notify)
		release_sem_etc(sSyslogNotify, 1, B_DO_NOT_RESCHEDULE);
}


/**
 * @brief Creates the syslog notify semaphore once threading is available.
 *
 * On failure the syslog subsystem is torn down: the buffer and message
 * scratch area are released and syslog output is disabled for the rest of
 * the session. Must be called from thread context.
 *
 * @return @c B_OK on success, @c B_ERROR when the semaphore could not be
 *   created.
 */
static status_t
syslog_init_post_threads(void)
{
	if (!sSyslogOutputEnabled)
		return B_OK;

	sSyslogNotify = create_sem(0, "syslog data");
	if (sSyslogNotify >= 0)
		return B_OK;

	// initializing kernel syslog service failed -- disable it

	sSyslogOutputEnabled = false;

	if (sSyslogBuffer != NULL) {
		if (sDebugSyslog)
			delete_area(area_for(sSyslogBuffer));
		else
			delete_ring_buffer(sSyslogBuffer);

		sSyslogBuffer = NULL;
	}

	free(sSyslogMessage);
	delete_sem(sSyslogNotify);

	return B_ERROR;
}


/**
 * @brief Allocates the syslog ring buffer once VM is ready.
 *
 * Reads the @c syslog_buffer_size kernel driver setting, creates the ring
 * buffer (or wraps the already-existing debug-syslog buffer), primes the
 * syslog message envelope, copies any early boot output into it, and
 * registers the "syslog" KDL command. Called once from debug_init_post_settings().
 *
 * @param args Kernel-args block containing optional pre-boot debug output.
 * @return @c B_OK on success, @c B_NO_MEMORY if buffer or message
 *   allocation failed.
 */
static status_t
syslog_init_post_vm(struct kernel_args* args)
{
	status_t status;
	int32 length = 0;

	if (!sSyslogOutputEnabled) {
		sSyslogBuffer = NULL;
			// Might already have been set in syslog_init(), if the debug syslog
			// was enabled. Just drop it -- we'll never create the area.
		return B_OK;
	}

	sSyslogMessage = (syslog_message*)malloc(SYSLOG_MESSAGE_BUFFER_SIZE);
	if (sSyslogMessage == NULL) {
		status = B_NO_MEMORY;
		goto err1;
	}

	if (sSyslogBuffer == NULL) {
		size_t bufferSize = DEFAULT_SYSLOG_BUFFER_SIZE;
		void* handle = load_driver_settings("kernel");
		if (handle != NULL) {
			const char* sizeString = get_driver_parameter(handle,
				"syslog_buffer_size", NULL, NULL);
			if (sizeString != NULL) {
				bufferSize = strtoul(sizeString, NULL, 0);
				if (bufferSize > 262144)
					bufferSize = 262144;
				else if (bufferSize < SYSLOG_MESSAGE_BUFFER_SIZE)
					bufferSize = SYSLOG_MESSAGE_BUFFER_SIZE;
			}

			unload_driver_settings(handle);
		}

		sSyslogBuffer = create_ring_buffer(bufferSize);

		if (sSyslogBuffer == NULL) {
			status = B_NO_MEMORY;
			goto err2;
		}
	} else if (args->keep_debug_output_buffer) {
		// create an area for the already-existing debug syslog buffer
		void* base = (void*)ROUNDDOWN((addr_t)(void *)args->debug_output, B_PAGE_SIZE);
		size_t size = ROUNDUP(args->debug_size, B_PAGE_SIZE);
		area_id area = create_area("syslog debug", &base, B_EXACT_ADDRESS, size,
			B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (area < 0) {
			status = B_NO_MEMORY;
			goto err2;
		}
	}

	if (!args->keep_debug_output_buffer && args->debug_output != NULL) {
		syslog_write((const char*)args->debug_output.Pointer(),
			args->debug_size, false);
	}

	// initialize syslog message
	sSyslogMessage->from = 0;
	sSyslogMessage->options = LOG_KERN;
	sSyslogMessage->priority = LOG_DEBUG;
	sSyslogMessage->ident[0] = '\0';

	// Allocate memory for the previous session's debug syslog output. In
	// syslog_init_post_modules() we'll write it back to disk and free it.
	if (args->previous_debug_output != NULL) {
		sPreviousSessionSyslogBuffer = malloc(args->previous_debug_size);
		if (sPreviousSessionSyslogBuffer != NULL) {
			sPreviousSessionSyslogBufferSize = args->previous_debug_size;
			memcpy(sPreviousSessionSyslogBuffer, args->previous_debug_output,
				sPreviousSessionSyslogBufferSize);
		}
	}

	char revisionBuffer[64];
	length = snprintf(revisionBuffer, sizeof(revisionBuffer),
		"Welcome to syslog debug output!\nHaiku revision: %s\n",
		get_haiku_revision());
	syslog_write(revisionBuffer,
		std::min(length, (int32)sizeof(revisionBuffer) - 1), false);

	add_debugger_command_etc("syslog", &cmd_dump_syslog,
		"Dumps the syslog buffer.",
		"[ \"-n\" ] [ \"-k\" ]\n"
		"Dumps the whole syslog buffer, or, if -k is specified, only "
		"the part that hasn't been sent yet.\n", 0);

	return B_OK;

err2:
	free(sSyslogMessage);
err1:
	sSyslogOutputEnabled = false;
	sSyslogBuffer = NULL;
	return status;
}

/**
 * @brief Persists the previous session's debug-syslog buffer to disk.
 *
 * If the boot loader preserved output from the previous boot, writes it to
 * @c /var/log/previous_syslog and frees the buffer. Safe to call only once
 * modules and the VFS are up.
 */
static void
syslog_init_post_modules()
{
	if (sPreviousSessionSyslogBuffer == NULL)
		return;

	void* buffer = sPreviousSessionSyslogBuffer;
	size_t bufferSize = sPreviousSessionSyslogBufferSize;
	sPreviousSessionSyslogBuffer = NULL;
	sPreviousSessionSyslogBufferSize = 0;
	MemoryDeleter bufferDeleter(buffer);

	int fd = open("/var/log/previous_syslog", O_WRONLY | O_CREAT | O_TRUNC,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		dprintf("Failed to open previous syslog file: %s\n", strerror(errno));
		return;
	}

	write(fd, buffer, bufferSize);
	close(fd);
}

/**
 * @brief Bootstraps syslog early by adopting the bootloader's debug buffer.
 *
 * Only takes effect when the bootloader preserved its debug output buffer
 * (@c keep_debug_output_buffer). The buffer is re-wrapped as a ring
 * buffer so early dprintf output is retained. Called from debug_init()
 * before the VM subsystem is up.
 *
 * @param args Kernel-args block.
 * @return Always @c B_OK.
 */
static status_t
syslog_init(struct kernel_args* args)
{
	if (!args->keep_debug_output_buffer || args->debug_output == NULL)
		return B_OK;

	sSyslogBuffer = create_ring_buffer_etc(args->debug_output, args->debug_size,
		RING_BUFFER_INIT_FROM_BUFFER);
	sDebugSyslog = true;

	return B_OK;
}


/**
 * @brief Trampoline that wraps memcpy() for execution under a fault handler.
 *
 * Called by debug_memcpy() through debug_call_with_fault_handler() so a
 * bad address causes a setjmp/longjmp rather than crashing the debugger.
 *
 * @param _parameters Pointer to a @ref debug_memcpy_parameters struct.
 */
static void
debug_memcpy_trampoline(void* _parameters)
{
	debug_memcpy_parameters* parameters = (debug_memcpy_parameters*)_parameters;
	memcpy(parameters->to, parameters->from, parameters->size);
}


/**
 * @brief Trampoline that wraps strlcpy() for execution under a fault handler.
 *
 * Stores the strlcpy() return value back into the parameter struct so
 * debug_strlcpy() can observe it after the fault-handler frame unwinds.
 *
 * @param _parameters Pointer to a @ref debug_strlcpy_parameters struct.
 */
static void
debug_strlcpy_trampoline(void* _parameters)
{
	debug_strlcpy_parameters* parameters
		= (debug_strlcpy_parameters*)_parameters;
	parameters->result = strlcpy(parameters->to, parameters->from,
		parameters->size);
}


/**
 * @brief Notifies every registered debugger module of a KDL transition.
 *
 * Walks @c sDebuggerModules and invokes the module's @c enter_debugger or
 * @c exit_debugger callback as appropriate. Called from KDL context.
 *
 * @param enter @c true to notify modules of entry, @c false for exit.
 */
void
call_modules_hook(bool enter)
{
	uint32 index = 0;
	while (index < kMaxDebuggerModules && sDebuggerModules[index] != NULL) {
		debugger_module_info* module = sDebuggerModules[index];

		if (enter && module->enter_debugger != NULL)
			module->enter_debugger();
		else if (!enter && module->exit_debugger != NULL)
			module->exit_debugger();

		index++;
	}
}


/**
 * @brief Core dprintf sink: de-duplicates and fans out to every channel.
 *
 * Compares the new text against the most recent output and, when they
 * match, simply bumps the repeat counter so flush_pending_repeats() can
 * later collapse them into "Last message repeated N times." Otherwise the
 * pending summary is flushed and the string is dispatched to serial,
 * syslog, blue screen, and debugger modules. Must be called with
 * @c sSpinlock held, so it is safe from both thread and interrupt context.
 *
 * @param string Bytes to emit.
 * @param length Length of @a string in bytes.
 * @param notifySyslog When @c true, wake the syslog sender after writing.
 */
//!	Must be called with the sSpinlock held.
static void
debug_output(const char* string, int32 length, bool notifySyslog)
{
	if (length >= OUTPUT_BUFFER_SIZE)
		length = OUTPUT_BUFFER_SIZE - 1;

	if (length > 1 && string[length - 1] == '\n'
		&& strncmp(string, sLastOutputBuffer, length) == 0) {
		sMessageRepeatCount++;
		sMessageRepeatLastTime = system_time();
		if (sMessageRepeatFirstTime == 0)
			sMessageRepeatFirstTime = sMessageRepeatLastTime;
	} else {
		flush_pending_repeats(notifySyslog);

		if (sSerialDebugEnabled)
			arch_debug_serial_puts(string);
		if (sSyslogOutputEnabled)
			syslog_write(string, length, notifySyslog);
		if (sBlueScreenEnabled || sDebugScreenEnabled)
			blue_screen_puts(string);
		if (sSerialDebugEnabled) {
			for (uint32 i = 0; i < kMaxDebuggerModules; i++) {
				if (sDebuggerModules[i] && sDebuggerModules[i]->debugger_puts)
					sDebuggerModules[i]->debugger_puts(string, length);
			}
		}

		memcpy(sLastOutputBuffer, string, length);
		sLastOutputBuffer[length] = 0;
	}
}


/**
 * @brief Emits the pending repeat summary (or the original line) to all sinks.
 *
 * If more than one repeat was accumulated, prints "Last message repeated N
 * times."; if exactly one, re-emits the original line. Resets the repeat
 * counters. Must be called with @c sSpinlock held.
 *
 * @param notifySyslog When @c true, wake the syslog sender after writing.
 */
//!	Must be called with the sSpinlock held.
static void
flush_pending_repeats(bool notifySyslog)
{
	if (sMessageRepeatCount <= 0)
		return;

	if (sMessageRepeatCount > 1) {
		static char temp[40];
		size_t length = snprintf(temp, sizeof(temp),
			"Last message repeated %" B_PRId32 " times.\n", sMessageRepeatCount);
		length = std::min(length, sizeof(temp) - 1);

		if (sSerialDebugEnabled)
			arch_debug_serial_puts(temp);
		if (sSyslogOutputEnabled)
			syslog_write(temp, length, notifySyslog);
		if (sBlueScreenEnabled || sDebugScreenEnabled)
			blue_screen_puts(temp);
		if (sSerialDebugEnabled) {
			for (uint32 i = 0; i < kMaxDebuggerModules; i++) {
				if (sDebuggerModules[i] && sDebuggerModules[i]->debugger_puts)
					sDebuggerModules[i]->debugger_puts(temp, length);
			}
		}
	} else {
		// if we only have one repeat just reprint the last buffer
		size_t length = strlen(sLastOutputBuffer);

		if (sSerialDebugEnabled)
			arch_debug_serial_puts(sLastOutputBuffer);
		if (sSyslogOutputEnabled)
			syslog_write(sLastOutputBuffer, length, notifySyslog);
		if (sBlueScreenEnabled || sDebugScreenEnabled)
			blue_screen_puts(sLastOutputBuffer);
		if (sSerialDebugEnabled) {
			for (uint32 i = 0; i < kMaxDebuggerModules; i++) {
				if (sDebuggerModules[i] && sDebuggerModules[i]->debugger_puts) {
					sDebuggerModules[i]->debugger_puts(sLastOutputBuffer,
						length);
				}
			}
		}
	}

	sMessageRepeatFirstTime = 0;
	sMessageRepeatCount = 0;
}


/**
 * @brief Kernel daemon tick: flushes stale repeat counts if time elapsed.
 *
 * Registered with register_kernel_daemon() at 10 Hz. Acquires
 * @c sOutputLock with a short timeout so it cannot stall on a busy log,
 * then flushes the pending repeat summary if at least one second passed
 * since the last match (or three seconds since the first). Thread context.
 *
 * @param data Unused.
 * @param iteration Unused.
 */
static void
check_pending_repeats(void* /*data*/, int /*iteration*/)
{
	if (mutex_lock_with_timeout(&sOutputLock, B_RELATIVE_TIMEOUT, 100 * 1000) != B_OK)
		return;
	MutexLocker locker(sOutputLock, true);

	if (sMessageRepeatCount > 0
		&& (system_time() - sMessageRepeatLastTime > 1000000
			|| system_time() - sMessageRepeatFirstTime > 3000000)) {
		cpu_status state = disable_interrupts();
		acquire_spinlock(&sSpinlock);

		flush_pending_repeats(true);

		release_spinlock(&sSpinlock);
		restore_interrupts(state);
	}
}


/**
 * @brief Shared dprintf implementation that formats into the output buffer.
 *
 * When interrupts are enabled, takes @c sOutputLock and uses the regular
 * @c sOutputBuffer. When they are not (KDL/panic/interrupt path), skips
 * the mutex and uses @c sInterruptOutputBuffer under @c sSpinlock only.
 * Both paths funnel into debug_output().
 *
 * @param format printf-style format string.
 * @param args Variadic arguments matching @a format.
 * @param notifySyslog When @c true, wake the syslog sender after writing.
 */
static void
dprintf_args(const char* format, va_list args, bool notifySyslog)
{
	if (are_interrupts_enabled()) {
		MutexLocker locker(sOutputLock);

		int32 length = vsnprintf(sOutputBuffer, OUTPUT_BUFFER_SIZE, format,
			args);
		length = std::min(length, (int32)OUTPUT_BUFFER_SIZE - 1);

		InterruptsSpinLocker _(sSpinlock);
		debug_output(sOutputBuffer, length, notifySyslog);
	} else {
		InterruptsSpinLocker _(sSpinlock);

		int32 length = vsnprintf(sInterruptOutputBuffer, OUTPUT_BUFFER_SIZE,
			format, args);
		length = std::min(length, (int32)OUTPUT_BUFFER_SIZE - 1);

		debug_output(sInterruptOutputBuffer, length, notifySyslog);
	}
}


// #pragma mark - private kernel API


/**
 * @brief Reports whether the low-level debug framebuffer console is active.
 *
 * @return @c true if @c sDebugScreenEnabled is set.
 */
bool
debug_screen_output_enabled(void)
{
	return sDebugScreenEnabled;
}


/**
 * @brief Disables future debug output through the framebuffer console.
 *
 * Called once user-space has claimed the framebuffer so the kernel stops
 * drawing over it. Does not tear down serial or syslog output.
 */
void
debug_stop_screen_debug_output(void)
{
	sDebugScreenEnabled = false;
}


/**
 * @brief Reports whether a KDL session is currently in progress.
 *
 * Safe to call from any context. Used by locking primitives to avoid
 * deadlock when they are exercised from within KDL.
 *
 * @return @c true when some CPU holds the debugger.
 */
bool
debug_debugger_running(void)
{
	return sDebuggerOnCPU != -1;
}


/**
 * @brief Synchronous variant of dprintf() that takes a pre-formatted buffer.
 *
 * Acquires @c sOutputLock and @c sSpinlock and calls debug_output() with
 * syslog notification enabled. Thread context only, since it takes the
 * mutex unconditionally.
 *
 * @param string Bytes to emit.
 * @param length Number of bytes in @a string.
 */
void
debug_puts(const char* string, int32 length)
{
	MutexLocker mutexLocker(sOutputLock);
	InterruptsSpinLocker _(sSpinlock);
	debug_output(string, length, true);
}


/**
 * @brief Emits a message via the very early serial path only.
 *
 * Used before debug_init() when neither the syslog buffer nor the filter
 * chain exist. Any context.
 *
 * @param string NUL-terminated message to print.
 */
void
debug_early_boot_message(const char* string)
{
	arch_debug_serial_early_boot_message(string);
}


/**
 * @brief First-stage debug-subsystem initialisation.
 *
 * Placement-constructs the default output filter, adopts any preserved
 * boot-loader debug buffer via syslog_init(), wires up paranoia tracking,
 * the serial console, and (when available) the blue-screen framebuffer.
 * Runs before VM is up.
 *
 * @param args Kernel-args block from the bootloader.
 */
void
debug_init(kernel_args* args)
{
	new(&gDefaultDebugOutputFilter) DefaultDebugOutputFilter;

	syslog_init(args);

	debug_paranoia_init();
	arch_debug_console_init(args);

	if (frame_buffer_console_init(args) == B_OK && blue_screen_init_early() == B_OK)
		sBlueScreenOutput = true;
}


/**
 * @brief Registers core KDL commands once VM is available.
 *
 * Installs the "cpu", "message", and "panic_commands" commands, brings up
 * the builtin command table, arch-specific debug helpers, the debug heap,
 * debug variables, framebuffer console, and tracing. Thread context.
 *
 * @param args Kernel-args block from the bootloader.
 */
void
debug_init_post_vm(kernel_args* args)
{
	add_debugger_command_etc("cpu", &cmd_switch_cpu,
		"Switches to another CPU.",
		"<cpu>\n"
		"Switches to CPU with the index <cpu>.\n", 0);
	add_debugger_command_etc("message", &cmd_dump_kdl_message,
		"Reprint the message printed when entering KDL",
		"\n"
		"Reprints the message printed when entering KDL.\n", 0);
	add_debugger_command_etc("panic_commands", &cmd_execute_panic_commands,
		"Execute commands associated with the panic() that caused "
			"entering KDL",
		"\n"
		"Executes the commands associated with the panic() that caused "
			"entering KDL.\n", 0);

	debug_builtin_commands_init();
	arch_debug_init(args);

	debug_heap_init();
	debug_variables_init();
	frame_buffer_console_init_post_vm(args);
	tracing_init();
}


/**
 * @brief Applies safemode toggles and starts post-VM debug services.
 *
 * Reads the user's safemode preferences for serial debug, syslog output,
 * blue-screen, emergency keys, and debug-screen, attempts to bring up
 * blue-screen when requested, finalises arch console settings, and calls
 * syslog_init_post_vm(). Thread context.
 *
 * @param args Kernel-args block from the bootloader.
 */
void
debug_init_post_settings(struct kernel_args* args)
{
	// get debug settings

	sSerialDebugEnabled = get_safemode_boolean("serial_debug_output",
		sSerialDebugEnabled);
	sSyslogOutputEnabled = get_safemode_boolean("syslog_debug_output",
		sSyslogOutputEnabled);
	sBlueScreenOutput = get_safemode_boolean("bluescreen", true);
	sEmergencyKeysEnabled = get_safemode_boolean("emergency_keys",
		sEmergencyKeysEnabled);
	sDebugScreenEnabled = get_safemode_boolean("debug_screen", false);

	if ((sBlueScreenOutput || sDebugScreenEnabled)
			&& blue_screen_init() != B_OK)
		sBlueScreenOutput = sDebugScreenEnabled = false;

	if (sDebugScreenEnabled)
		blue_screen_enter(true);

	arch_debug_console_init_settings(args);
	syslog_init_post_vm(args);
}


/**
 * @brief Final debug-subsystem initialisation after modules are available.
 *
 * Persists the previous boot's syslog, starts the repeat-flush daemon,
 * spawns the syslog sender thread, loads every kernel debugger add-on
 * module (including the optional demangler), and finalises the
 * framebuffer console. Thread context.
 *
 * @param args Kernel-args block from the bootloader.
 */
void
debug_init_post_modules(struct kernel_args* args)
{
	syslog_init_post_modules();

	// check for dupped lines every 10/10 second
	register_kernel_daemon(check_pending_repeats, NULL, 10);

	syslog_init_post_threads();

	// load kernel debugger addons

	static const char* kDemanglePrefix = "debugger/demangle/";

	void* cookie = open_module_list("debugger");
	uint32 count = 0;
	while (count < kMaxDebuggerModules) {
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);

		if (read_next_module_name(cookie, name, &nameLength) != B_OK)
			break;

		// get demangle module, if any
		if (!strncmp(name, kDemanglePrefix, strlen(kDemanglePrefix))) {
			if (sDemangleModule == NULL)
				get_module(name, (module_info**)&sDemangleModule);
			continue;
		}

		if (get_module(name, (module_info**)&sDebuggerModules[count]) == B_OK) {
			dprintf("kernel debugger extension \"%s\": loaded\n", name);
			count++;
		} else
			dprintf("kernel debugger extension \"%s\": failed to load\n", name);
	}
	close_module_list(cookie);

	frame_buffer_console_init_post_modules(args);
}


/**
 * @brief Records the page-fault details that triggered a KDL entry.
 *
 * Populates the static @c sPageFaultInfo block that the arch fault handler
 * consults before dropping into kernel_debugger_internal(). Invoked from
 * the arch fault handler with interrupts disabled.
 *
 * @param faultAddress Linear address the faulting access targeted.
 * @param pc Instruction pointer at the time of the fault.
 * @param flags Bitmask describing the fault (see @c debug_page_fault_info).
 */
void
debug_set_page_fault_info(addr_t faultAddress, addr_t pc, uint32 flags)
{
	sPageFaultInfo.fault_address = faultAddress;
	sPageFaultInfo.pc = pc;
	sPageFaultInfo.flags = flags;
}


/**
 * @brief Returns a pointer to the most recent page-fault info.
 *
 * Consumed by KDL command handlers ("page_fault_info") and demand-paging
 * diagnostics. KDL context only.
 *
 * @return Address of the internal @c debug_page_fault_info record.
 */
debug_page_fault_info*
debug_get_page_fault_info()
{
	return &sPageFaultInfo;
}


/**
 * @brief Halts the current CPU while another CPU owns the debugger.
 *
 * Called from the SMP halt ICI handler: saves registers, marks this CPU
 * trapped, and spins until @c sInDebugger drops back to zero, servicing
 * ICIs in the meantime. If a hand-off targets this CPU, either returns
 * (when @a returnIfHandedOver is @c true) or recursively enters the
 * debugger. Interrupts must already be off.
 *
 * @param cpu Index of the current CPU.
 * @param returnIfHandedOver When @c true, return instead of entering KDL
 *   after a successful hand-off.
 */
void
debug_trap_cpu_in_kdl(int32 cpu, bool returnIfHandedOver)
{
	InterruptsLocker locker;

	// return, if we've been called recursively (we call
	// smp_intercpu_interrupt_handler() below)
	if (sCPUTrapped[cpu])
		return;

	arch_debug_save_registers(&sDebugRegisters[cpu]);

	sCPUTrapped[cpu] = true;

	while (sInDebugger != 0) {
		arch_debug_snooze(10000);

		if (sHandOverKDL && sHandOverKDLToCPU == cpu) {
			if (returnIfHandedOver)
				break;

			kernel_debugger_internal(NULL, NULL,
				sCurrentKernelDebuggerMessageArgs, cpu);
		} else
			smp_intercpu_interrupt_handler(cpu);
	}

	sCPUTrapped[cpu] = false;
}


/**
 * @brief Enters KDL labelled as a double-fault.
 *
 * Called from the arch double-fault handler after the CPU has already
 * pivoted to the double-fault stack. Interrupts are off on entry and the
 * call does not return normally.
 *
 * @param cpu Index of the faulting CPU.
 */
void
debug_double_fault(int32 cpu)
{
	kernel_debugger_internal("Double Fault!", NULL,
		sCurrentKernelDebuggerMessageArgs, cpu);
}


/**
 * @brief Dispatches an alt-sysrq style emergency keystroke.
 *
 * Invoked from the keyboard driver ISR. The built-in "d" keystroke enters
 * the debugger via kernel_debugger(); all other keys are offered to every
 * registered debugger module until one claims them. Interrupt context.
 *
 * @param key Key character delivered by the keyboard driver.
 * @return @c true if some handler claimed the key, @c false otherwise.
 */
bool
debug_emergency_key_pressed(char key)
{
	if (!sEmergencyKeysEnabled)
		return false;

	if (key == 'd') {
		kernel_debugger("Keyboard Requested Halt.");
		return true;
	}

	// Broadcast to the kernel debugger modules

	for (uint32 i = 0; i < kMaxDebuggerModules; i++) {
		if (sDebuggerModules[i] && sDebuggerModules[i]->emergency_key_pressed) {
			if (sDebuggerModules[i]->emergency_key_pressed(key))
				return true;
		}
	}

	return false;
}


/**
 * @brief Verifies that a memory range is accessible in the current context.
 *
 * Walks the range page by page through the arch translation map, so it
 * works even when the VM core cannot be trusted. KDL context only.
 *
 * @param address Start of the range to probe.
 * @param size Length of the range in bytes.
 * @param protection Bitwise OR of @c B_KERNEL_READ_AREA and/or
 *   @c B_KERNEL_WRITE_AREA describing the required access.
 * @return @c true when every page satisfies @a protection, @c false
 *   otherwise.
 */
bool
debug_is_kernel_memory_accessible(addr_t address, size_t size,
	uint32 protection)
{
	addr_t endAddress = ROUNDUP(address + size, B_PAGE_SIZE);
	address = ROUNDDOWN(address, B_PAGE_SIZE);

	if (!IS_KERNEL_ADDRESS(address) || endAddress < address)
		return false;

	for (; address < endAddress; address += B_PAGE_SIZE) {
		if (!arch_vm_translation_map_is_kernel_page_accessible(address,
				protection)) {
			return false;
		}
	}

	return true;
}


/**
 * @brief Invokes @a function under a setjmp() + per-CPU fault handler.
 *
 * Saves the CPU's current fault handler state, plants @a jumpBuffer as the
 * new handler, and calls @a function. If the callee page-faults, control
 * returns here via longjmp() and the original handler is restored. KDL
 * context only.
 *
 * @param jumpBuffer Scratch buffer used by setjmp()/longjmp().
 * @param function Callback to execute while protected.
 * @param parameter Opaque pointer forwarded to @a function.
 * @return 0 on clean execution, 1 on page fault, or any other value the
 *   callee passes to longjmp().
 */
int
debug_call_with_fault_handler(jmp_buf jumpBuffer, void (*function)(void*),
	void* parameter)
{
	// save current fault handler
	cpu_ent* cpu = gCPU + sDebuggerOnCPU;
	addr_t oldFaultHandler = cpu->fault_handler;
	addr_t oldFaultHandlerStackPointer = cpu->fault_handler_stack_pointer;

	int result = setjmp(jumpBuffer);
	if (result == 0) {
		arch_debug_call_with_fault_handler(cpu, jumpBuffer, function,
			parameter);
	}

	// restore old fault handler
	cpu->fault_handler = oldFaultHandler;
	cpu->fault_handler_stack_pointer = oldFaultHandlerStackPointer;

	return result;
}


/**
 * @brief Debugger-safe memcpy() across a possibly-foreign address space.
 *
 * First tries a regular memcpy() guarded by the fault handler; if that
 * fails it falls back to vm_debug_copy_page_memory() which can read from
 * cached-but-unmapped pages. Never sleeps and never takes the VM locks.
 * KDL context only.
 *
 * @param teamID Address space selector (@c B_CURRENT_TEAM for the
 *   currently debugged thread's team, or any valid team ID). Ignored when
 *   both addresses are kernel addresses.
 * @param to Destination address.
 * @param from Source address.
 * @param size Number of bytes to copy.
 * @return @c B_OK on success, @c B_BAD_ADDRESS when the range could not
 *   be satisfied.
 */
status_t
debug_memcpy(team_id teamID, void* to, const void* from, size_t size)
{
	// don't allow address overflows
	if ((addr_t)from + size < (addr_t)from || (addr_t)to + size < (addr_t)to)
		return B_BAD_ADDRESS;

	// Try standard memcpy() with fault handler, if the addresses can be
	// interpreted in the current address space.
	if ((IS_KERNEL_ADDRESS(from) && IS_KERNEL_ADDRESS(to))
			|| debug_is_debugged_team(teamID)) {
		debug_memcpy_parameters parameters = {to, from, size};

		if (debug_call_with_fault_handler(gCPU[sDebuggerOnCPU].fault_jump_buffer,
				&debug_memcpy_trampoline, &parameters) == 0) {
			return B_OK;
		}
	}

	// Try harder. The pages of the respective memory could be unmapped but
	// still exist in a cache (the page daemon does that with inactive pages).
	while (size > 0) {
		uint8 buffer[32];
		size_t toCopy = std::min(size, sizeof(buffer));

		// restrict the size so we don't cross page boundaries
		if (((addr_t)from + toCopy) % B_PAGE_SIZE < toCopy)
			toCopy -= ((addr_t)from + toCopy) % B_PAGE_SIZE;
		if (((addr_t)to + toCopy) % B_PAGE_SIZE < toCopy)
			toCopy -= ((addr_t)to + toCopy) % B_PAGE_SIZE;

		if (vm_debug_copy_page_memory(teamID, (void*)from, buffer, toCopy,
				false) != B_OK
			|| vm_debug_copy_page_memory(teamID, to, buffer, toCopy, true)
				!= B_OK) {
			return B_BAD_ADDRESS;
		}

		from = (const uint8*)from + toCopy;
		to = (uint8*)to + toCopy;
		size -= toCopy;
	}

	return B_OK;
}


/**
 * @brief Debugger-safe strlcpy() across a possibly-foreign address space.
 *
 * Like debug_memcpy(), but stops at the first NUL. Handles the case where
 * @a size is smaller than the source string and returns the would-be
 * length for strlcpy() callers. KDL context only.
 *
 * @param teamID Address space selector (@c B_CURRENT_TEAM or a valid team
 *   ID). Ignored when both addresses are kernel addresses.
 * @param to Destination buffer.
 * @param from Source string.
 * @param size Capacity of @a to in bytes.
 * @return Length of @a from on success, or @c B_BAD_ADDRESS on failure.
 */
ssize_t
debug_strlcpy(team_id teamID, char* to, const char* from, size_t size)
{
	if (from == NULL || (to == NULL && size > 0))
		return B_BAD_ADDRESS;

	// limit size to avoid address overflows
	size_t maxSize = std::min((addr_t)size,
		~(addr_t)0 - std::max((addr_t)from, (addr_t)to) + 1);
		// NOTE: Since strlcpy() determines the length of \a from, the source
		// address might still overflow.

	// Try standard strlcpy() with fault handler, if the addresses can be
	// interpreted in the current address space.
	if ((IS_KERNEL_ADDRESS(from) && IS_KERNEL_ADDRESS(to))
			|| debug_is_debugged_team(teamID)) {
		debug_strlcpy_parameters parameters = {to, from, maxSize};

		if (debug_call_with_fault_handler(
				gCPU[sDebuggerOnCPU].fault_jump_buffer,
				&debug_strlcpy_trampoline, &parameters) == 0) {
			// If we hit the address overflow boundary, fail.
			if (parameters.result >= maxSize && maxSize < size)
				return B_BAD_ADDRESS;

			return parameters.result;
		}
	}

	// Try harder. The pages of the respective memory could be unmapped but
	// still exist in a cache (the page daemon does that with inactive pages).
	size_t totalLength = 0;
	while (maxSize > 0) {
		char buffer[32];
		size_t toCopy = std::min(maxSize, sizeof(buffer));

		// restrict the size so we don't cross page boundaries
		if (((addr_t)from + toCopy) % B_PAGE_SIZE < toCopy)
			toCopy -= ((addr_t)from + toCopy) % B_PAGE_SIZE;
		if (((addr_t)to + toCopy) % B_PAGE_SIZE < toCopy)
			toCopy -= ((addr_t)to + toCopy) % B_PAGE_SIZE;

		// copy the next part of the string from the source
		if (vm_debug_copy_page_memory(teamID, (void*)from, buffer, toCopy,
				false) != B_OK) {
			return B_BAD_ADDRESS;
		}

		// determine the length of the part and whether we've reached the end
		// of the string
		size_t length = strnlen(buffer, toCopy);
		bool endOfString = length < toCopy;

		from = (const char*)from + toCopy;
		totalLength += length;
		maxSize -= length;

		if (endOfString) {
			// only copy the actual string, including the terminating null
			toCopy = length + 1;
		}

		if (size > 0) {
			// We still have space left in the target buffer.
			if (size <= length) {
				// Not enough space for the complete part. Null-terminate it and
				// copy what we can.
				buffer[size - 1] = '\0';
				totalLength += length - size;
				toCopy = size;
			}

			if (vm_debug_copy_page_memory(teamID, to, buffer, toCopy, true)
					!= B_OK) {
				return B_BAD_ADDRESS;
			}

			to = (char*)to + toCopy;
			size -= toCopy;
		}

		if (endOfString)
			return totalLength;
	}

	return totalLength;
}


// #pragma mark - public API


/**
 * @brief Evaluates a KDL arithmetic expression and returns its value.
 *
 * Delegates to evaluate_debug_expression(); warnings are printed to the
 * KDL output by the evaluator itself. KDL context only.
 *
 * @param expression NUL-terminated expression to parse.
 * @return The numeric value of @a expression, or 0 if evaluation failed.
 */
uint64
parse_expression(const char* expression)
{
	uint64 result;
	return evaluate_debug_expression(expression, &result, true) ? result : 0;
}


/**
 * @brief Aborts the kernel and drops into KDL with a formatted message.
 *
 * The message may embed a command suffix separated by "@!" which will be
 * fed to evaluate_debug_command() after the prompt appears. Note that a
 * second panic() while already inside a panic-triggered KDL session is
 * treated as an unrecoverable fault by the arch layer and triggers a hard
 * reset. Safe to call from any context; interrupts are disabled on entry.
 *
 * @param format printf-style format string describing the failure.
 */
void
panic(const char* format, ...)
{
	va_list args;
	va_start(args, format);

	cpu_status state = disable_interrupts();

	kernel_debugger_internal("PANIC: ", format, args,
		thread_get_current_thread() ? smp_get_current_cpu() : 0);

	restore_interrupts(state);

	va_end(args);
}


/**
 * @brief Voluntarily enters KDL with a plain (non-formatted) message.
 *
 * Unlike panic(), this path is recoverable: once the user types "continue"
 * the caller resumes execution. Any context; interrupts are disabled on
 * entry and restored on return.
 *
 * @param message Text to display as the entry banner.
 */
void
kernel_debugger(const char* message)
{
	cpu_status state = disable_interrupts();

	kernel_debugger_internal(message, NULL, sCurrentKernelDebuggerMessageArgs,
		smp_get_current_cpu());

	restore_interrupts(state);
}


/**
 * @brief Toggles serial dprintf output and returns the previous state.
 *
 * Used by KDL to force dprintf on during a session and to restore it on
 * exit. Can be called from any context.
 *
 * @param newState Desired enabled state.
 * @return The previous enabled state.
 */
bool
set_dprintf_enabled(bool newState)
{
	bool oldState = sSerialDebugEnabled;
	sSerialDebugEnabled = newState;

	return oldState;
}


/**
 * @brief Kernel printf to every enabled debug sink with syslog notification.
 *
 * Early-outs if serial, syslog, and blue screen are all disabled. Safe
 * from any context: takes @c sOutputLock only when interrupts are enabled.
 *
 * @param format printf-style format string.
 * @param ... Variadic arguments for @a format.
 */
void
dprintf(const char* format, ...)
{
	va_list args;

	if (!sSerialDebugEnabled && !sSyslogOutputEnabled && !sBlueScreenEnabled)
		return;

	va_start(args, format);
	dprintf_args(format, args, true);
	va_end(args);
}


/**
 * @brief va_list variant of dprintf().
 *
 * @param format printf-style format string.
 * @param args Pre-captured variadic argument list.
 */
void
dvprintf(const char* format, va_list args)
{
	if (!sSerialDebugEnabled && !sSyslogOutputEnabled && !sBlueScreenEnabled)
		return;

	dprintf_args(format, args, true);
}


/**
 * @brief dprintf variant that suppresses the syslog-daemon wake-up.
 *
 * Used from paths where waking the syslog sender would be unsafe (for
 * instance deep inside a semaphore release). Serial and blue-screen sinks
 * still receive the output. Any context.
 *
 * @param format printf-style format string.
 * @param ... Variadic arguments for @a format.
 */
void
dprintf_no_syslog(const char* format, ...)
{
	va_list args;

	if (!sSerialDebugEnabled && !sBlueScreenEnabled)
		return;

	va_start(args, format);
	dprintf_args(format, args, false);
	va_end(args);
}


/**
 * @brief printf for KDL output that honours the installed filter.
 *
 * Unlike dprintf(), does not take any lock and does not touch the repeat
 * collapser; this makes it safe to call with interrupts disabled from
 * within KDL command handlers. Silently drops output when no filter is
 * installed (i.e. outside a KDL session).
 *
 * @param format printf-style format string.
 * @param ... Variadic arguments for @a format.
 */
void
kprintf(const char* format, ...)
{
	if (sDebugOutputFilter != NULL) {
		va_list args;
		va_start(args, format);
		sDebugOutputFilter->Print(format, args);
		va_end(args);
	}
}


/**
 * @brief kprintf that always targets the default (unfiltered) sink.
 *
 * Used when a filter (for example the GDB stub) has hijacked kprintf but
 * the caller needs to reach the raw console regardless. KDL context.
 *
 * @param format printf-style format string.
 * @param ... Variadic arguments for @a format.
 */
void
kprintf_unfiltered(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	gDefaultDebugOutputFilter.Print(format, args);
	va_end(args);
}


/**
 * @brief Demangles a symbol through the optional demangler module.
 *
 * Falls back to returning @a symbol unchanged when no demangler has been
 * registered. Safe to call from any context.
 *
 * @param symbol Mangled symbol string.
 * @param buffer Scratch buffer the demangler may write into.
 * @param bufferSize Size of @a buffer in bytes.
 * @param _isObjectMethod [out, optional] Set to @c true if the symbol
 *   denotes a non-static member function.
 * @return Pointer to the demangled name (typically inside @a buffer) or
 *   @a symbol itself if demangling is unavailable.
 */
const char*
debug_demangle_symbol(const char* symbol, char* buffer, size_t bufferSize,
	bool* _isObjectMethod)
{
	if (sDemangleModule != NULL && sDemangleModule->demangle_symbol != NULL) {
		return sDemangleModule->demangle_symbol(symbol, buffer, bufferSize,
			_isObjectMethod);
	}

	if (_isObjectMethod != NULL)
		*_isObjectMethod = false;

	return symbol;
}


/**
 * @brief Iterates over the parameter list encoded in a mangled symbol.
 *
 * Delegates to the demangler module; returns @c B_NOT_SUPPORTED when no
 * demangler is loaded. Any context.
 *
 * @param _cookie [in,out] Opaque iteration cookie; start with 0.
 * @param symbol Mangled symbol string being decoded.
 * @param name [out] Buffer to receive the argument's decoded name.
 * @param nameSize Size of @a name in bytes.
 * @param _type [out] Receives a type classifier understood by the
 *   demangler module.
 * @param _argumentLength [out] Receives the raw byte length of the
 *   argument in memory.
 * @return @c B_OK on a successful step, @c B_NOT_SUPPORTED when no
 *   demangler is loaded, or another error from the demangler.
 */
status_t
debug_get_next_demangled_argument(uint32* _cookie, const char* symbol,
	char* name, size_t nameSize, int32* _type, size_t* _argumentLength)
{
	if (sDemangleModule != NULL && sDemangleModule->get_next_argument != NULL) {
		return sDemangleModule->get_next_argument(_cookie, symbol, name,
			nameSize, _type, _argumentLength);
	}

	return B_NOT_SUPPORTED;
}


/**
 * @brief Returns the saved register set captured when @a cpu entered KDL.
 *
 * Used by arch backtrace code. KDL context only.
 *
 * @param cpu Index of the CPU whose state is requested.
 * @return Pointer to the arch register snapshot, or @c NULL when @a cpu
 *   is out of range.
 */
struct arch_debug_registers*
debug_get_debug_registers(int32 cpu)
{
	if (cpu < 0 || cpu > smp_get_num_cpus())
		return NULL;

	return &sDebugRegisters[cpu];
}


/**
 * @brief Designates a thread as the "debugged" thread for follow-up queries.
 *
 * debug_get_debugged_thread() and debug_is_debugged_team() consult the
 * thread set by this call, which lets "thread X" commands pivot subsequent
 * memory accesses into that thread's address space. KDL context.
 *
 * @param thread Thread to designate, or @c NULL to fall back to the
 *   current running thread.
 * @return Previously designated thread.
 */
Thread*
debug_set_debugged_thread(Thread* thread)
{
	Thread* previous = sDebuggedThread;
	sDebuggedThread = thread;
	return previous;
}


/**
 * @brief Returns the currently designated KDL target thread.
 *
 * Falls back to thread_get_current_thread() when no override is active.
 * KDL context only.
 *
 * @return The target thread, or whatever thread_get_current_thread()
 *   returns when none has been set.
 */
Thread*
debug_get_debugged_thread()
{
	return sDebuggedThread != NULL
		? sDebuggedThread : thread_get_current_thread();
}


/**
 * @brief Tests whether @a teamID is the team of the current debugged thread.
 *
 * @c B_CURRENT_TEAM is treated as always matching. KDL context.
 *
 * @param teamID Team ID to test, or @c B_CURRENT_TEAM.
 * @return @c true when @a teamID matches the debugged thread's team.
 */
bool
debug_is_debugged_team(team_id teamID)
{
	if (teamID == B_CURRENT_TEAM)
		return true;

	Thread* thread = debug_get_debugged_thread();
	return thread != NULL && thread->team != NULL
		&& thread->team->id == teamID;
}


//	#pragma mark -
//	userland syscalls


/**
 * @brief Syscall entry that lets a privileged process enter KDL.
 *
 * Only root may invoke this. The user-supplied message is copied into a
 * bounded kernel buffer, prefixed with "USER: ", and forwarded to
 * kernel_debugger(). Thread context.
 *
 * @param userMessage User-pointer to a NUL-terminated message.
 * @return @c B_OK on return from KDL, @c B_NOT_ALLOWED if the caller is
 *   not root, @c B_BAD_ADDRESS on user-pointer validation failure.
 */
status_t
_user_kernel_debugger(const char* userMessage)
{
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	char message[512];
	strcpy(message, "USER: ");
	size_t length = strlen(message);

	if (userMessage == NULL || !IS_USER_ADDRESS(userMessage) || user_strlcpy(
			message + length, userMessage, sizeof(message) - length) < 0) {
		return B_BAD_ADDRESS;
	}

	kernel_debugger(message);
	return B_OK;
}


/**
 * @brief Registers the userland syslog daemon and starts the forwarder.
 *
 * Root-only. Records the daemon's port and lazily spawns @ref syslog_sender
 * if it is not already running. Thread context.
 *
 * @param port Port to which SYSLOG_MESSAGE packets should be delivered.
 */
void
_user_register_syslog_daemon(port_id port)
{
	if (geteuid() != 0 || !sSyslogOutputEnabled || sSyslogNotify < 0)
		return;

	sSyslogPort = port;

	if (sSyslogWriter < 0) {
		sSyslogWriter = spawn_kernel_thread(syslog_sender, "syslog sender",
			B_LOW_PRIORITY, NULL);
		if (sSyslogWriter >= 0)
			resume_thread(sSyslogWriter);
	}
}


/**
 * @brief Syscall that forwards a user string to the kernel debug log.
 *
 * Copies the user pointer in chunks (up to 512 bytes each) and calls
 * debug_puts(). Silently returns if no debug sink is enabled or if the
 * pointer is not in user space. Thread context.
 *
 * @param userString User-pointer to a NUL-terminated string.
 */
void
_user_debug_output(const char* userString)
{
	if (!sSerialDebugEnabled && !sSyslogOutputEnabled)
		return;

	if (!IS_USER_ADDRESS(userString))
		return;

	char string[512];
	int32 length;
	int32 toWrite;
	do {
		length = user_strlcpy(string, userString, sizeof(string));
		if (length <= 0)
			break;
		toWrite = std::min(length, (int32)sizeof(string) - 1);
		debug_puts(string, toWrite);
		userString += toWrite;
	} while (length > toWrite);
}


/**
 * @brief Hex/ASCII dumps @a size bytes via dprintf(), 16 bytes per line.
 *
 * Each line is prefixed with @a prefix and the byte offset. Safe from any
 * context that can safely call dprintf().
 *
 * @param buffer Start of the data to dump.
 * @param size Number of bytes in @a buffer.
 * @param prefix String emitted at the beginning of every output line.
 */
void
dump_block(const char* buffer, int size, const char* prefix)
{
	const int DUMPED_BLOCK_SIZE = 16;
	int i;
	char lineBuffer[3 + DUMPED_BLOCK_SIZE * 4];

	for (i = 0; i < size;) {
		char* pointer = lineBuffer;
		int start = i;

		for (; i < start + DUMPED_BLOCK_SIZE; i++) {
			if (!(i % 4))
				pointer += sprintf(pointer, " ");

			if (i >= size)
				pointer += sprintf(pointer, "  ");
			else
				pointer += sprintf(pointer, "%02x", *(unsigned char*)(buffer + i));
		}
		pointer += sprintf(pointer, "  ");

		for (i = start; i < start + DUMPED_BLOCK_SIZE; i++) {
			if (i < size) {
				char c = buffer[i];

				if (c < 30)
					pointer += sprintf(pointer, ".");
				else
					pointer += sprintf(pointer, "%c", c);
			} else
				break;
		}
		dprintf("%s%04x%s\n", prefix, start, lineBuffer);
	}
}
