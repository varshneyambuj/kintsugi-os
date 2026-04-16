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
 *   Copyright 2005-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file blue_screen.cpp
 * @brief Kernel Debug Land (KDL) / panic "blue screen" framebuffer renderer.
 *
 * Takes over the framebuffer (or serial console, depending on which module is
 * installed as gFrameBufferConsoleModule) when the kernel enters KDL or panics,
 * and implements a small VT100 emulator so the shared kprintf() formatting code
 * can draw the panic banner, register dump, stack trace, and command output.
 * Also implements on-screen paging with user keyboard input.
 *
 * Panic-time safety: by the time KDL is entered, interrupts are disabled, the
 * scheduler is stopped and the normal VM may not be trustworthy. Every function
 * in this file must therefore avoid sleeping locks, heap allocation, and any
 * operation that depends on page faults being serviced. The functions marked
 * "panic-safe" below are called from the KDL context; all of them only touch
 * the static sScreen state and the console module's direct glyph/cursor hooks,
 * plus spin() and kgetc() which are themselves panic-safe. blue_screen_init()
 * (but not blue_screen_init_early()) is the sole exception: it is invoked once
 * during normal boot and does register a debugger command, but is not called
 * again from panic context.
 */


#include "blue_screen.h"

#include <KernelExport.h>
#include <console.h>
#include <debug.h>
#include <arch/debug_console.h>
#include <safemode.h>

#include <string.h>
#include <stdio.h>

#include "debug_commands.h"


#define USE_SCROLLING 0
#define NO_CLEAR 1

#define MAX_ARGS 8

#define FMASK 0x0f
#define BMASK 0x70

typedef enum {
	CONSOLE_STATE_NORMAL = 0,
	CONSOLE_STATE_GOT_ESCAPE,
	CONSOLE_STATE_SEEN_BRACKET,
	CONSOLE_STATE_NEW_ARG,
	CONSOLE_STATE_PARSING_ARG,
} console_state;

typedef enum {
	LINE_ERASE_WHOLE,
	LINE_ERASE_LEFT,
	LINE_ERASE_RIGHT
} erase_line_mode;

struct screen_info {
	int32	columns;
	int32	rows;
	int32	x, y;
	uint8	attr;
	bool	bright_attr;
	bool	reverse_attr;
	int32	in_command_rows;
	bool	paging;
	bool	paging_timeout;
	bool	boot_debug_output;
	bool	ignore_output;

	// state machine
	console_state state;
	int32	arg_count;
	int32	args[MAX_ARGS];
} sScreen;

console_module_info *sModule = NULL;


/**
 * @brief Hide the hardware text cursor (panic-safe).
 *
 * Delegates to the console module's move_cursor with sentinel coordinates.
 * Called before a burst of glyph writes to prevent flicker.
 */
static inline void
hide_cursor(void)
{
	sModule->move_cursor(-1, -1);
}


/**
 * @brief Move the hardware cursor without touching logical state (panic-safe).
 *
 * @param x Column (0-based).
 * @param y Row (0-based).
 */
static inline void
update_cursor(int32 x, int32 y)
{
	sModule->move_cursor(x, y);
}


/**
 * @brief Update both logical and hardware cursor position (panic-safe).
 *
 * @param x New column.
 * @param y New row.
 */
static inline void
move_cursor(int32 x, int32 y)
{
	sScreen.x = x;
	sScreen.y = y;
	update_cursor(x, y);
}


#if USE_SCROLLING

/**
 * @brief Scroll the screen up one line (panic-safe; compile-time disabled).
 *
 * Only compiled when USE_SCROLLING is nonzero. Blits the screen up and clears
 * the exposed bottom line. Normally disabled because framebuffer blits are
 * prohibitively slow at panic time.
 */
static void
scroll_up(void)
{
	// move the screen up one
	sModule->blit(0, 1, sScreen.columns, sScreen.rows - 1, 0, 0);

	// clear the bottom line
	sModule->fill_glyph(0, 0, sScreen.columns, 1, ' ', sScreen.attr);
}
#endif


/**
 * @brief Advance to the next line, handling wrap and on-screen paging (panic-safe).
 *
 * When a debugger command is running, tracks the number of rows it has emitted;
 * once the screen has filled, prompts the user via kgetc() for paging input
 * (continue, skip, quit, disable paging, or enable timeout). If the user
 * chooses 'q' during a command, invokes abort_debugger_command(), which does
 * not return. At end-of-screen the cursor wraps back to row 0 and the top
 * three rows are cleared to act as a scroll frame.
 */
static void
next_line(void)
{
	bool abortCommand = false;

#if USE_SCROLLING
	// TODO: scrolling is usually too slow; we could probably just remove it
	if (sScreen.y == sScreen.rows - 1)
 		scroll_up();
 	else
 		sScreen.y++;
#else
	if (in_command_invocation())
		sScreen.in_command_rows++;
	else
		sScreen.in_command_rows = 0;

	if (sScreen.paging && ((sScreen.in_command_rows > 0
			&& ((sScreen.in_command_rows + 3) % sScreen.rows) == 0)
		|| (sScreen.boot_debug_output && sScreen.y == sScreen.rows - 1))) {
		if (sScreen.paging_timeout)
			spin(1000 * 1000 * 3);
		else {
			// Use the paging mechanism: either, we're in the debugger, and a
			// command is being executed, or we're currently showing boot debug
			// output
			const char *text = in_command_invocation()
				? "Press key to continue, Q to quit, S to skip output"
				: "Press key to continue, S to skip output, P to disable paging";
			int32 length = strlen(text);
			if (sScreen.x + length > sScreen.columns) {
				// make sure we don't overwrite too much
				text = "P";
				length = 1;
			}

			for (int32 i = 0; i < length; i++) {
				// yellow on black (or reverse, during boot)
				sModule->put_glyph(sScreen.columns - length + i, sScreen.y,
					text[i], sScreen.boot_debug_output ? 0x6f : 0xf6);
			}

			char c = kgetc();
			if (c == 's') {
				sScreen.ignore_output = true;
			} else if (c == 'q' && in_command_invocation()) {
				abortCommand = true;
				sScreen.ignore_output = true;
			} else if (c == 'p' && !in_command_invocation())
				sScreen.paging = false;
			else if (c == 't' && !in_command_invocation())
				sScreen.paging_timeout = true;

			// remove on screen text again
			sModule->fill_glyph(sScreen.columns - length, sScreen.y, length,
				1, ' ', sScreen.attr);
		}

		if (sScreen.in_command_rows > 0)
			sScreen.in_command_rows += 2;
	}
	if (sScreen.y == sScreen.rows - 1) {
		sScreen.y = 0;
		sModule->fill_glyph(0, 0, sScreen.columns, 2, ' ', sScreen.attr);
	} else
		sScreen.y++;
#endif

#if NO_CLEAR
	if (sScreen.y + 2 < sScreen.rows) {
		sModule->fill_glyph(0, (sScreen.y + 2) % sScreen.rows, sScreen.columns,
			1, ' ', sScreen.attr);
	}
#endif
	sScreen.x = 0;

	if (abortCommand) {
		abort_debugger_command();
			// should not return
	}
}


/**
 * @brief Erase part or all of the cursor's current line (panic-safe).
 *
 * @param mode LINE_ERASE_WHOLE, LINE_ERASE_LEFT (up to and including cursor),
 *             or LINE_ERASE_RIGHT (from cursor to end of line).
 */
static void
erase_line(erase_line_mode mode)
{
	switch (mode) {
		case LINE_ERASE_WHOLE:
			sModule->fill_glyph(0, sScreen.y, sScreen.columns, 1, ' ',
				sScreen.attr);
			break;
		case LINE_ERASE_LEFT:
			sModule->fill_glyph(0, sScreen.y, sScreen.x + 1, 1, ' ',
				sScreen.attr);
			break;
		case LINE_ERASE_RIGHT:
			sModule->fill_glyph(sScreen.x, sScreen.y, sScreen.columns
				- sScreen.x, 1, ' ', sScreen.attr);
			break;
	}
}


/**
 * @brief Handle a backspace by erasing the glyph to the left of the cursor.
 *
 * No-op at column zero. Panic-safe.
 */
static void
back_space(void)
{
	if (sScreen.x <= 0)
		return;

	sScreen.x--;
	sModule->put_glyph(sScreen.x, sScreen.y, ' ', sScreen.attr);
}


/**
 * @brief Write a single printable glyph at the cursor, wrapping if needed (panic-safe).
 *
 * @param c Character to deposit with the current attribute.
 */
static void
put_character(char c)
{
	if (++sScreen.x >= sScreen.columns) {
		next_line();
		sScreen.x++;
	}

	sModule->put_glyph(sScreen.x - 1, sScreen.y, c, sScreen.attr);
}


/**
 * @brief Apply an ANSI SGR (Set Graphic Rendition) escape sequence (panic-safe).
 *
 * Handles reset, bright/dim, blink, reverse, and the 30-37/40-47 colour ranges
 * by mutating sScreen.attr. Haiku's console module uses a single-byte attr with
 * the foreground in the low nibble and background in bits 4-6, so the mapping
 * table converts from ANSI colour order to that layout.
 *
 * @param args     Parsed numeric arguments from the escape.
 * @param argCount Number of valid entries in @p args (0 implies reset).
 */
static void
set_vt100_attributes(int32 *args, int32 argCount)
{
	if (argCount == 0) {
		// that's the default (attributes off)
		argCount++;
		args[0] = 0;
	}

	for (int32 i = 0; i < argCount; i++) {
		switch (args[i]) {
			case 0: // reset
				sScreen.attr = sScreen.boot_debug_output ? 0xf0 : 0x0f;
				sScreen.bright_attr = true;
				sScreen.reverse_attr = false;
				break;
			case 1: // bright
				sScreen.bright_attr = true;
				sScreen.attr |= 0x08; // set the bright bit
				break;
			case 2: // dim
				sScreen.bright_attr = false;
				sScreen.attr &= ~0x08; // unset the bright bit
				break;
			case 4: // underscore we can't do
				break;
			case 5: // blink
				sScreen.attr |= 0x80; // set the blink bit
				break;
			case 7: // reverse
				sScreen.reverse_attr = true;
				sScreen.attr = ((sScreen.attr & BMASK) >> 4)
					| ((sScreen.attr & FMASK) << 4);
				if (sScreen.bright_attr)
					sScreen.attr |= 0x08;
				break;
			case 8: // hidden?
				break;

			/* foreground colors */
			case 30: // black
			case 31: // red
			case 32: // green
			case 33: // yellow
			case 34: // blue
			case 35: // magenta
			case 36: // cyan
			case 37: // white
			{
				const uint8 colors[] = {0, 4, 2, 6, 1, 5, 3, 7};
				sScreen.attr = (sScreen.attr & ~FMASK) | colors[args[i] - 30]
					| (sScreen.bright_attr ? 0x08 : 0);
				break;
			}

			/* background colors */
			case 40: sScreen.attr = (sScreen.attr & ~BMASK) | (0 << 4); break; // black
			case 41: sScreen.attr = (sScreen.attr & ~BMASK) | (4 << 4); break; // red
			case 42: sScreen.attr = (sScreen.attr & ~BMASK) | (2 << 4); break; // green
			case 43: sScreen.attr = (sScreen.attr & ~BMASK) | (6 << 4); break; // yellow
			case 44: sScreen.attr = (sScreen.attr & ~BMASK) | (1 << 4); break; // blue
			case 45: sScreen.attr = (sScreen.attr & ~BMASK) | (5 << 4); break; // magenta
			case 46: sScreen.attr = (sScreen.attr & ~BMASK) | (3 << 4); break; // cyan
			case 47: sScreen.attr = (sScreen.attr & ~BMASK) | (7 << 4); break; // white
		}
	}
}


/**
 * @brief Dispatch a parsed VT100 escape sequence (panic-safe).
 *
 * Supports cursor motion (H/f/A/B/C/D/G/d and their lowercase aliases), line
 * erase (K), and SGR attribute changes (m). Unsupported letters are ignored
 * and reported back to the parser so it can resync.
 *
 * @param c           Final command byte.
 * @param seenBracket True if the sequence was introduced with CSI "ESC [".
 * @param args        Parsed numeric arguments.
 * @param argCount    Number of valid entries in @p args.
 * @return true if the command was recognised; false to make the parser drop
 *         the unknown sequence.
 */
static bool
process_vt100_command(const char c, bool seenBracket, int32 *args,
	int32 argCount)
{
	bool ret = true;

//	kprintf("process_vt100_command: c '%c', argCount %ld, arg[0] %ld, arg[1] %ld, seenBracket %d\n",
//		c, argCount, args[0], args[1], seenBracket);

	if (seenBracket) {
		switch (c) {
			case 'H':	// set cursor position
			case 'f':
			{
				int32 row = argCount > 0 ? args[0] : 1;
				int32 col = argCount > 1 ? args[1] : 1;
				if (row > 0)
					row--;
				if (col > 0)
					col--;
				move_cursor(col, row);
				break;
			}
			case 'A':	// move up
			{
				int32 deltaY = argCount > 0 ? -args[0] : -1;
				if (deltaY == 0)
					deltaY = -1;
				move_cursor(sScreen.x, sScreen.y + deltaY);
				break;
			}
			case 'e':
			case 'B':	// move down
			{
				int32 deltaY = argCount > 0 ? args[0] : 1;
				if (deltaY == 0)
					deltaY = 1;
				move_cursor(sScreen.x, sScreen.y + deltaY);
				break;
			}
			case 'D':	// move left
			{
				int32 deltaX = argCount > 0 ? -args[0] : -1;
				if (deltaX == 0)
					deltaX = -1;
				move_cursor(sScreen.x + deltaX, sScreen.y);
				break;
			}
			case 'a':
			case 'C':	// move right
			{
				int32 deltaX = argCount > 0 ? args[0] : 1;
				if (deltaX == 0)
					deltaX = 1;
				move_cursor(sScreen.x + deltaX, sScreen.y);
				break;
			}
			case '`':
			case 'G':	// set X position
			{
				int32 newX = argCount > 0 ? args[0] : 1;
				if (newX > 0)
					newX--;
				move_cursor(newX, sScreen.y);
				break;
			}
			case 'd':	// set y position
			{
				int32 newY = argCount > 0 ? args[0] : 1;
				if (newY > 0)
					newY--;
				move_cursor(sScreen.x, newY);
				break;
			}
#if 0
			case 's':	// save current cursor
				save_cur(console, false);
				break;
			case 'u':	// restore cursor
				restore_cur(console, false);
				break;
			case 'r':	// set scroll region
			{
				int32 low = argCount > 0 ? args[0] : 1;
				int32 high = argCount > 1 ? args[1] : sScreen.lines;
				if (low <= high)
					set_scroll_region(console, low - 1, high - 1);
				break;
			}
			case 'L':	// scroll virtual down at cursor
			{
				int32 lines = argCount > 0 ? args[0] : 1;
				while (lines > 0) {
					scrdown(console);
					lines--;
				}
				break;
			}
			case 'M':	// scroll virtual up at cursor
			{
				int32 lines = argCount > 0 ? args[0] : 1;
				while (lines > 0) {
					scrup(console);
					lines--;
				}
				break;
			}
#endif
			case 'K':
				if (argCount == 0 || args[0] == 0) {
					// erase to end of line
					erase_line(LINE_ERASE_RIGHT);
				} else if (argCount > 0) {
					if (args[0] == 1)
						erase_line(LINE_ERASE_LEFT);
					else if (args[0] == 2)
						erase_line(LINE_ERASE_WHOLE);
				}
				break;
#if 0
			case 'J':
				if (argCount == 0 || args[0] == 0) {
					// erase to end of screen
					erase_screen(console, SCREEN_ERASE_DOWN);
				} else {
					if (args[0] == 1)
						erase_screen(console, SCREEN_ERASE_UP);
					else if (args[0] == 2)
						erase_screen(console, SCREEN_ERASE_WHOLE);
				}
				break;
#endif
			case 'm':
				if (argCount >= 0)
					set_vt100_attributes(args, argCount);
				break;
			default:
				ret = false;
		}
	} else {
		switch (c) {
#if 0
			case 'c':
				reset_console(console);
				break;
			case 'D':
				rlf(console);
				break;
			case 'M':
				lf(console);
				break;
			case '7':
				save_cur(console, true);
				break;
			case '8':
				restore_cur(console, true);
				break;
#endif
			default:
				ret = false;
		}
	}

	return ret;
}


/**
 * @brief Feed one input byte through the VT100 state machine (panic-safe).
 *
 * Handles the common control characters (newline, backspace, tab, CR, bell)
 * directly and shuttles ESC-introduced bytes through the states
 * CONSOLE_STATE_GOT_ESCAPE / SEEN_BRACKET / NEW_ARG / PARSING_ARG until a
 * final command byte is seen and dispatched to process_vt100_command().
 *
 * @param c Input byte.
 */
static void
parse_character(char c)
{
	switch (sScreen.state) {
		case CONSOLE_STATE_NORMAL:
			// just output the stuff
			switch (c) {
				case '\n':
					next_line();
					break;
				case 0x8:
					back_space();
					break;
				case '\t':
					// TODO: real tab...
					sScreen.x = (sScreen.x + 8) & ~7;
					if (sScreen.x >= sScreen.columns)
						next_line();
					break;

				case '\r':
				case '\0':
				case '\a': // beep
					break;

				case 0x1b:
					// escape character
					sScreen.arg_count = 0;
					sScreen.state = CONSOLE_STATE_GOT_ESCAPE;
					break;
				default:
					put_character(c);
			}
			break;
		case CONSOLE_STATE_GOT_ESCAPE:
			// look for either commands with no argument, or the '[' character
			switch (c) {
				case '[':
					sScreen.state = CONSOLE_STATE_SEEN_BRACKET;
					break;
				default:
					sScreen.args[0] = 0;
					process_vt100_command(c, false, sScreen.args, 0);
					sScreen.state = CONSOLE_STATE_NORMAL;
			}
			break;
		case CONSOLE_STATE_SEEN_BRACKET:
			switch (c) {
				case '0'...'9':
					sScreen.arg_count = 0;
					sScreen.args[0] = c - '0';
					sScreen.state = CONSOLE_STATE_PARSING_ARG;
					break;
				case '?':
					// private DEC mode parameter follows - we ignore those
					// anyway
					break;
				default:
					process_vt100_command(c, true, sScreen.args, 0);
					sScreen.state = CONSOLE_STATE_NORMAL;
					break;
			}
			break;
		case CONSOLE_STATE_NEW_ARG:
			switch (c) {
				case '0'...'9':
					if (++sScreen.arg_count == MAX_ARGS) {
						sScreen.state = CONSOLE_STATE_NORMAL;
						break;
					}
					sScreen.args[sScreen.arg_count] = c - '0';
					sScreen.state = CONSOLE_STATE_PARSING_ARG;
					break;
				default:
					process_vt100_command(c, true, sScreen.args,
						sScreen.arg_count + 1);
					sScreen.state = CONSOLE_STATE_NORMAL;
					break;
			}
			break;
		case CONSOLE_STATE_PARSING_ARG:
			// parse args
			switch (c) {
				case '0'...'9':
					sScreen.args[sScreen.arg_count] *= 10;
					sScreen.args[sScreen.arg_count] += c - '0';
					break;
				case ';':
					sScreen.state = CONSOLE_STATE_NEW_ARG;
					break;
				default:
					process_vt100_command(c, true, sScreen.args,
						sScreen.arg_count + 1);
					sScreen.state = CONSOLE_STATE_NORMAL;
					break;
			}
	}
}


/**
 * @brief "paging" debugger command: toggle/enable/disable on-screen paging.
 *
 * Called from KDL with interrupts disabled. With no argument it toggles;
 * "on"/"off" set explicitly; any other token is parsed as an expression
 * whose truthiness controls the flag.
 *
 * @param argc Argument count (including argv[0] == "paging").
 * @param argv Argument vector.
 * @return Always 0.
 */
static int
set_paging(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [on|off]\n", argv[0]);
		return 0;
	}

	if (argc == 1)
		sScreen.paging = !sScreen.paging;
	else if (!strcmp(argv[1], "on"))
		sScreen.paging = true;
	else if (!strcmp(argv[1], "off"))
		sScreen.paging = false;
	else
		sScreen.paging = parse_expression(argv[1]) != 0;

	kprintf("paging is turned %s now.\n", sScreen.paging ? "on" : "off");
	return 0;
}


//	#pragma mark -


/**
 * @brief Early initialisation, runnable before the module subsystem is up.
 *
 * Bypasses get_module() (which is not usable this early) and binds directly
 * to gFrameBufferConsoleModule. Clears the paging flags. Safe to call from
 * the panic path as a last resort if blue_screen_init() was never reached.
 *
 * @return B_OK on success; B_ERROR if the console module failed B_MODULE_INIT.
 */
status_t
blue_screen_init_early()
{
	// we can't use get_module() here, since it's too early in the boot process
	extern console_module_info gFrameBufferConsoleModule;
	sModule = &gFrameBufferConsoleModule;

	if (sModule->info.std_ops(B_MODULE_INIT) != B_OK) {
		sModule = NULL;
		return B_ERROR;
	}

	sScreen.paging = sScreen.paging_timeout = false;
	return B_OK;
}


/**
 * @brief Full initialisation, called once during kernel boot.
 *
 * Chains blue_screen_init_early() if it did not previously succeed, reads the
 * "disable_onscreen_paging" safemode flag, and registers the "paging" KDL
 * command. Not panic-safe: this runs in normal kernel context and touches the
 * command registry and safemode subsystem.
 *
 * @return B_OK on success; B_ERROR if the console module is unavailable.
 */
status_t
blue_screen_init()
{
	if (sModule == NULL) {
		// Early initialization must have previously failed, or never run.
		if (blue_screen_init_early() != B_OK)
			return B_ERROR;
	}

	sScreen.paging = !get_safemode_boolean(
		"disable_onscreen_paging", false);
	sScreen.paging_timeout = false;

	add_debugger_command("paging", set_paging, "Enable or disable paging");
	return B_OK;
}


/**
 * @brief Enter KDL/panic mode and paint a clean screen (panic-safe).
 *
 * Resets the VT100 state, chooses the colour scheme (black on white for KDL,
 * white on black for boot debug output), clears the top three rows and places
 * the cursor at the origin. Called with interrupts disabled from the panic
 * path, so it only uses the direct console module hooks.
 *
 * @param debugOutput True for boot-debug colouring, false for KDL colouring.
 * @return B_OK on success; B_NO_INIT if the console module has not been bound.
 */
status_t
blue_screen_enter(bool debugOutput)
{
	sScreen.attr = debugOutput ? 0xf0 : 0x0f;
		// black on white for KDL, white on black for debug output
	sScreen.boot_debug_output = debugOutput;
	sScreen.ignore_output = false;

	sScreen.x = sScreen.y = 0;
	sScreen.state = CONSOLE_STATE_NORMAL;

	if (sModule == NULL)
		return B_NO_INIT;

	sModule->get_size(&sScreen.columns, &sScreen.rows);
#if !NO_CLEAR
	sModule->clear(sScreen.attr);
#else
	sModule->fill_glyph(0, sScreen.y, sScreen.columns, 3, ' ', sScreen.attr);
#endif
	return B_OK;
}


/**
 * @brief Query whether on-screen paging is currently enabled (panic-safe).
 *
 * @return true if next_line() should prompt the user at screen boundaries.
 */
bool
blue_screen_paging_enabled(void)
{
	return sScreen.paging;
}


/**
 * @brief Enable or disable on-screen paging (panic-safe).
 *
 * @param enabled New value for the paging flag.
 */
void
blue_screen_set_paging(bool enabled)
{
	sScreen.paging = enabled;
}


/**
 * @brief Clear the entire screen and home the cursor (panic-safe).
 *
 * Used between KDL commands when a clean slate is desired.
 */
void
blue_screen_clear_screen(void)
{
	sModule->clear(sScreen.attr);
	move_cursor(0, 0);
}


/**
 * @brief Non-blocking read of one keyboard byte (panic-safe).
 *
 * @return The byte, or -1 if none is available.
 */
int
blue_screen_try_getchar(void)
{
	return arch_debug_blue_screen_try_getchar();
}


/**
 * @brief Blocking read of one keyboard byte (panic-safe).
 *
 * Spins on the arch-specific debug console until a key is pressed. Since
 * interrupts are off in panic context, this is how KDL receives input.
 *
 * @return The received byte.
 */
char
blue_screen_getchar(void)
{
	return arch_debug_blue_screen_getchar();
}


/**
 * @brief Write one character through the VT100 parser (panic-safe).
 *
 * If the user previously pressed 's' to skip output during a command or boot
 * debug output, the character is silently dropped. Hides the cursor for the
 * duration of the write and restores it afterwards.
 *
 * @param c Byte to render.
 */
void
blue_screen_putchar(char c)
{
	if (sScreen.ignore_output
		&& (in_command_invocation() || sScreen.boot_debug_output))
		return;

	sScreen.ignore_output = false;
	hide_cursor();

	parse_character(c);

	update_cursor(sScreen.x, sScreen.y);
}


/**
 * @brief Write a NUL-terminated string through the VT100 parser (panic-safe).
 *
 * Equivalent to repeated blue_screen_putchar() but hides/restores the cursor
 * only once around the burst for less flicker. Respects the same skip-output
 * behaviour.
 *
 * @param text NUL-terminated string to render.
 */
void
blue_screen_puts(const char *text)
{
	if (sScreen.ignore_output
		&& (in_command_invocation() || sScreen.boot_debug_output))
		return;

	sScreen.ignore_output = false;
	hide_cursor();

	while (text[0] != '\0') {
		parse_character(text[0]);
		text++;
	}

	update_cursor(sScreen.x, sScreen.y);
}
