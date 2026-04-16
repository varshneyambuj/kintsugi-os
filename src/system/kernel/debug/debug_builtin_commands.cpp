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
 *   Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de
 *   Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file debug_builtin_commands.cpp
 * @brief Built-in kernel debugger (KDL) command handlers.
 *
 * Implements the set of commands that are registered unconditionally at boot
 * so they are always available inside the kernel debugger prompt: session
 * control (help, continue, reboot, shutdown, gdb), expression evaluation
 * (expr, error), fault-handling toggle (faults), and a small collection of
 * pipe-aware text filters (head, tail, grep, wc) that can be chained with '|'
 * to post-process the output of other KDL commands. Each handler follows the
 * (argc, argv) convention and prints via kprintf.
 *
 * @brief Command dispatch convention.
 *
 * Every handler in this file is a function returning int that takes (argc,
 * argv). A return value of 0 means "success, stay in KDL"; B_KDEBUG_QUIT
 * causes the debugger loop to resume the kernel; B_KDEBUG_ERROR reports a
 * usage problem; and B_KDEBUG_RESTART_PIPE requests the pipe machinery to
 * feed the command a second time.
 *
 * @brief Pipe-segment state handling.
 *
 * Pipe-aware filters (head, tail, grep, wc) stash their running state in
 * the per-segment user_data block returned by
 * get_current_debugger_command_pipe_segment(), so multiple invocations can
 * accumulate counters or line tallies without touching globals.
 */

#include "debug_builtin_commands.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <debug.h>
#include <kernel.h>

#include "debug_commands.h"
#include "gdb.h"


/**
 * @brief KDL "reboot" command: hard-reboots the machine.
 *
 * Usage: reboot
 *
 * Invokes arch_cpu_shutdown(true) which does not return under normal
 * circumstances. The trailing return 0 is therefore only reached if the
 * architecture layer unexpectedly fails to restart the CPU.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on the unlikely case arch_cpu_shutdown() returns.
 */
static int
cmd_reboot(int argc, char **argv)
{
	arch_cpu_shutdown(true);
	return 0;
		// I'll be really suprised if this line ever runs! ;-)
}


/**
 * @brief KDL "shutdown" command: powers the machine off.
 *
 * Usage: shutdown
 *
 * Calls arch_cpu_shutdown(false) to request a clean power-off from the
 * architecture backend.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 if control unexpectedly returns from the shutdown path.
 */
static int
cmd_shutdown(int argc, char **argv)
{
	arch_cpu_shutdown(false);
	return 0;
}


/**
 * @brief KDL "help" command: lists registered debugger commands.
 *
 * Usage: help [name]
 *
 * With no argument, prints every registered KDL command and its short
 * description. With an argument that matches a command exactly, prints the
 * command and all of its aliases. Otherwise treats the argument as a prefix
 * and lists commands whose names start with it.
 *
 * @param argc Argument count.
 * @param argv Argument vector; argv[1] optionally holds a command name or
 *             prefix.
 * @return 0 always.
 */
static int
cmd_help(int argc, char **argv)
{
	debugger_command *command, *specified = NULL;
	const char *start = NULL;
	int32 startLength = 0;
	bool ambiguous;

	if (argc > 1) {
		specified = find_debugger_command(argv[1], false, ambiguous);
		if (specified == NULL) {
			start = argv[1];
			startLength = strlen(start);
		}
	}

	if (specified != NULL) {
		// only print out the help of the specified command (and all of its aliases)
		kprintf("debugger command for \"%s\" and aliases:\n", specified->name);
	} else if (start != NULL)
		kprintf("debugger commands starting with \"%s\":\n", start);
	else
		kprintf("debugger commands:\n");

	for (command = get_debugger_commands(); command != NULL;
			command = command->next) {
		if (specified && command->func != specified->func)
			continue;
		if (start != NULL && strncmp(start, command->name, startLength))
			continue;

		kprintf(" %-20s\t\t%s\n", command->name, command->description ? command->description : "-");
	}

	return 0;
}


/**
 * @brief KDL "continue" command: leaves the kernel debugger.
 *
 * Usage: continue (aliases: exit, es)
 *
 * Returns B_KDEBUG_QUIT to signal the debugger loop to resume normal kernel
 * execution.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return B_KDEBUG_QUIT to exit the KDL prompt.
 */
static int
cmd_continue(int argc, char **argv)
{
	return B_KDEBUG_QUIT;
}


/**
 * @brief KDL "expr" command: evaluates a debug expression.
 *
 * Usage: expr <expression>
 *
 * Parses argv[1] through evaluate_debug_expression() and, on success, prints
 * the result in both decimal and hexadecimal and stores it in the "_"
 * debug variable for reuse by later commands.
 *
 * @param argc Argument count; must be exactly 2.
 * @param argv Argument vector; argv[1] is the expression string.
 * @return 0 always (errors are reported via the expression machinery).
 */
static int
cmd_expr(int argc, char **argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	uint64 result;
	if (evaluate_debug_expression(argv[1], &result, false)) {
		kprintf("%" B_PRIu64 " (0x%" B_PRIx64 ")\n", result, result);
		set_debug_variable("_", result);
	}

	return 0;
}


/**
 * @brief KDL "error" command: translates an error code to text.
 *
 * Usage: error <error>
 *
 * Parses argv[1] as a numeric expression and prints the matching
 * strerror() description so that raw negative/positive error constants seen
 * inside the debugger can be decoded.
 *
 * @param argc Argument count; must be exactly 2.
 * @param argv Argument vector; argv[1] is the numeric error code.
 * @return 0 always.
 */
static int
cmd_error(int argc, char **argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	int32 error = parse_expression(argv[1]);
	kprintf("error 0x%" B_PRIx32 ": %s\n", error, strerror(error));

	return 0;
}


/**
 * @brief KDL "head" pipe filter: prints the first N lines of a pipe segment.
 *
 * Usage: <other command> | head <maxLines>
 *
 * Must be invoked as part of a debugger command pipe. On the first
 * invocation the maximum line count is parsed from argv[1] into per-segment
 * user data; on each subsequent call the current pipe line (argv[2]) is
 * echoed until the cap is reached, after which further lines are silently
 * discarded.
 *
 * @param argc Argument count; must be 3 (maxLines + current line).
 * @param argv Argument vector provided by the pipe machinery.
 * @return 0 on success, B_KDEBUG_ERROR when misused outside a pipe or with
 *         bad arguments.
 */
static int
cmd_head(int argc, char** argv)
{
	debugger_command_pipe_segment* segment
		= get_current_debugger_command_pipe_segment();
	if (segment == NULL) {
		kprintf_unfiltered("%s can only be run as part of a pipe!\n", argv[0]);
		return B_KDEBUG_ERROR;
	}

	struct user_data {
		uint64	max_lines;
		uint64	lines;
	};
	user_data* userData = (user_data*)segment->user_data;

	if (segment->invocations == 0) {
		if (argc != 3) {
			print_debugger_command_usage(argv[0]);
			return B_KDEBUG_ERROR;
		}

		if (!evaluate_debug_expression(argv[1], &userData->max_lines, false))
			return B_KDEBUG_ERROR;
		userData->lines = 0;
	}

	if (++userData->lines <= userData->max_lines) {
		kputs(argv[2]);
		kputs("\n");
	}

	return 0;
}


/**
 * @brief KDL "tail" pipe filter: prints the last N lines of a pipe segment.
 *
 * Usage: <other command> | tail [maxLines]
 *
 * Must be invoked as part of a debugger command pipe. First pass counts the
 * incoming lines; on end-of-input the pipe is restarted via
 * B_KDEBUG_RESTART_PIPE and the final pass echoes only the trailing
 * <maxLines> (default 10).
 *
 * @param argc Argument count; 2 or 3.
 * @param argv Argument vector provided by the pipe machinery.
 * @return 0 on success, B_KDEBUG_ERROR on misuse, or B_KDEBUG_RESTART_PIPE
 *         when the pipe needs to be re-run for the output phase.
 */
static int
cmd_tail(int argc, char** argv)
{
	debugger_command_pipe_segment* segment
		= get_current_debugger_command_pipe_segment();
	if (segment == NULL) {
		kprintf_unfiltered("%s can only be run as part of a pipe!\n", argv[0]);
		return B_KDEBUG_ERROR;
	}

	struct user_data {
		uint64	max_lines;
		int64	line_count;
		bool	restarted;
	};
	user_data* userData = (user_data*)segment->user_data;

	if (segment->invocations == 0) {
		if (argc > 3) {
			print_debugger_command_usage(argv[0]);
			return B_KDEBUG_ERROR;
		}

		userData->max_lines = 10;
		if (argc > 2 && !evaluate_debug_expression(argv[1],
				&userData->max_lines, false)) {
			return B_KDEBUG_ERROR;
		}

		userData->line_count = 1;
		userData->restarted = false;
	} else if (!userData->restarted) {
		if (argv[argc - 1] == NULL) {
			userData->restarted = true;
			userData->line_count -= userData->max_lines;
			return B_KDEBUG_RESTART_PIPE;
		}

		++userData->line_count;
	} else {
		if (argv[argc - 1] == NULL)
			return 0;

		if (--userData->line_count < 0) {
			kputs(argv[argc - 1]);
			kputs("\n");
		}
	}

	return 0;
}


/**
 * @brief KDL "grep" pipe filter: matches lines against a pattern.
 *
 * Usage: <other command> | grep [-i] [-v] <pattern>
 *
 * Parses option flags -i (case-insensitive) and -v (invert match), then for
 * each piped line prints it when the match/invert state agrees. The
 * case-insensitive path uses a simple strncasecmp scan, which is fine at
 * KDL-usage scale.
 *
 * @param argc Argument count (flags + pattern + current line).
 * @param argv Argument vector from the pipe machinery.
 * @return 0 on success, B_KDEBUG_ERROR on argument errors.
 */
static int
cmd_grep(int argc, char** argv)
{
	bool caseSensitive = true;
	bool inverseMatch = false;

	int argi = 1;
	for (; argi < argc; argi++) {
		const char* arg = argv[argi];
		if (arg[0] != '-')
			break;

		for (int32 i = 1; arg[i] != '\0'; i++) {
			if (arg[i] == 'i') {
				caseSensitive = false;
			} else if (arg[i] == 'v') {
				inverseMatch = true;
			} else {
				print_debugger_command_usage(argv[0]);
				return B_KDEBUG_ERROR;
			}
		}
	}

	if (argc - argi != 2) {
		print_debugger_command_usage(argv[0]);
		return B_KDEBUG_ERROR;
	}

	const char* pattern = argv[argi++];
	const char* line = argv[argi++];

	bool match;
	if (caseSensitive) {
		match = strstr(line, pattern) != NULL;
	} else {
		match = false;
		int32 lineLen = strlen(line);
		int32 patternLen = strlen(pattern);
		for (int32 i = 0; i <= lineLen - patternLen; i++) {
			// This is rather slow, but should be OK for our purposes.
			if (strncasecmp(line + i, pattern, patternLen) == 0) {
				match = true;
				break;
			}
		}
	}

	if (match != inverseMatch) {
		kputs(line);
		kputs("\n");
	}

	return 0;
}


/**
 * @brief KDL "wc" pipe filter: counts lines, words and characters.
 *
 * Usage: <other command> | wc
 *
 * Must be invoked as part of a debugger command pipe. On each piped line
 * bumps the running totals in the per-segment user data; on the final run
 * (line == NULL, signalled via B_KDEBUG_PIPE_FINAL_RERUN) prints the
 * aggregated counts in the classic wc layout.
 *
 * @param argc Argument count.
 * @param argv Argument vector; argv[1] is the current line or NULL for the
 *             terminating call.
 * @return 0 on success, B_KDEBUG_ERROR on misuse outside a pipe.
 */
static int
cmd_wc(int argc, char** argv)
{
	debugger_command_pipe_segment* segment
		= get_current_debugger_command_pipe_segment();
	if (segment == NULL) {
		kprintf_unfiltered("%s can only be run as part of a pipe!\n", argv[0]);
		return B_KDEBUG_ERROR;
	}

	struct user_data {
		uint64	lines;
		uint64	words;
		uint64	chars;
	};
	user_data* userData = (user_data*)segment->user_data;

	if (segment->invocations == 0) {
		if (argc != 2) {
			print_debugger_command_usage(argv[0]);
			return B_KDEBUG_ERROR;
		}

		userData->lines = 0;
		userData->words = 0;
		userData->chars = 0;
	}

	const char* line = argv[1];
	if (line == NULL) {
		// last run -- print results
		kprintf("%10" B_PRIu64 " %10" B_PRIu64 " %10" B_PRIu64 "\n",
			userData->lines, userData->words, userData->chars);
		return 0;
	}

	userData->lines++;
	userData->chars++;
		// newline

	// count words and chars in this line
	bool inWord = false;
	for (; *line != '\0'; line++) {
		userData->chars++;
		if ((isspace(*line) != 0) == inWord) {
			inWord = !inWord;
			if (inWord)
				userData->words++;
		}
	}

	return 0;
}


/**
 * @brief KDL "faults" command: toggles fault handling for KDL commands.
 *
 * Usage: faults [0|1]
 *
 * Without arguments prints the current setting. With argv[1] evaluating to
 * zero, disables the fault-catching wrapper so commands are invoked
 * directly (useful when the wrapper itself is suspect); non-zero re-enables
 * it.
 *
 * @param argc Argument count; 1 or 2.
 * @param argv Argument vector; argv[1] optionally holds 0 or 1.
 * @return 0 on success, B_KDEBUG_ERROR on too many arguments.
 */
static int
cmd_faults(int argc, char** argv)
{
	if (argc > 2) {
		print_debugger_command_usage(argv[0]);
		return B_KDEBUG_ERROR;
	}

	if (argc == 2)
		gInvokeCommandDirectly = parse_expression(argv[1]) == 0;

	kprintf("Fault handling is %s%s.\n", argc == 2 ? "now " : "",
		gInvokeCommandDirectly ? "off" : "on");
	return 0;
}


// #pragma mark -


/**
 * @brief Registers every built-in command with the KDL command table.
 *
 * Called once during kernel debugger initialisation. For each handler in
 * this file it installs a command entry plus help text via
 * add_debugger_command_etc(), and wires up the "exit" / "es" aliases for
 * "continue". Pipe-aware commands pass B_KDEBUG_PIPE_FINAL_RERUN so their
 * finaliser is invoked once the upstream stage has drained.
 *
 * @return void.
 */
void
debug_builtin_commands_init()
{
	add_debugger_command_etc("help", &cmd_help, "List all debugger commands",
		"[name]\n"
		"Lists all debugger commands or those starting with \"name\".\n", 0);
	add_debugger_command_etc("reboot", &cmd_reboot, "Reboot the system",
		"\n"
		"Reboots the system.\n", 0);
	add_debugger_command_etc("shutdown", &cmd_shutdown, "Shut down the system",
		"\n"
		"Shuts down the system.\n", 0);
	add_debugger_command_etc("gdb", &cmd_gdb, "Connect to remote gdb",
		"\n"
		"Connects to a remote gdb connected to the serial port.\n", 0);
	add_debugger_command_etc("continue", &cmd_continue, "Leave kernel debugger",
		"\n"
		"Leaves kernel debugger.\n", 0);
	add_debugger_command_alias("exit", "continue", "Same as \"continue\"");
	add_debugger_command_alias("es", "continue", "Same as \"continue\"");
	add_debugger_command_etc("expr", &cmd_expr,
		"Evaluates the given expression and prints the result",
		"<expression>\n"
		"Evaluates the given expression and prints the result.\n",
		B_KDEBUG_DONT_PARSE_ARGUMENTS);
	add_debugger_command_etc("error", &cmd_error,
		"Prints a human-readable description for an error code",
		"<error>\n"
		"Prints a human-readable description for the given numeric error\n"
		"code.\n"
		"  <error>  - The numeric error code.\n", 0);
	add_debugger_command_etc("faults", &cmd_faults, "Toggles fault handling "
		"for debugger commands",
		"[0|1]\n"
		"Toggles fault handling on (1) or off (0).\n", 0);
	add_debugger_command_etc("head", &cmd_head,
		"Prints only the first lines of output from another command",
		"<maxLines>\n"
		"Should be used in a command pipe. It prints only the first\n"
		"<maxLines> lines of output from the previous command in the pipe and\n"
		"silently discards the rest of the output.\n", 0);
	add_debugger_command_etc("tail", &cmd_tail,
		"Prints only the last lines of output from another command",
		"[ <maxLines> ]\n"
		"Should be used in a command pipe. It prints only the last\n"
		"<maxLines> (default 10) lines of output from the previous command in\n"
		"the pipe and silently discards the rest of the output.\n",
		B_KDEBUG_PIPE_FINAL_RERUN);
	add_debugger_command_etc("grep", &cmd_grep,
		"Filters output from another command",
		"[ -i ] [ -v ] <pattern>\n"
		"Should be used in a command pipe. It filters all output from the\n"
		"previous command in the pipe according to the given pattern.\n"
		"When \"-v\" is specified, only those lines are printed that don't\n"
		"match the given pattern, otherwise only those that do match. When\n"
		"\"-i\" is specified, the pattern is matched case insensitive,\n"
		"otherwise case sensitive.\n", 0);
	add_debugger_command_etc("wc", &cmd_wc,
		"Counts the lines, words, and characters of another command's output",
		"<maxLines>\n"
		"Should be used in a command pipe. It prints how many lines, words,\n"
		"and characters the output of the previous command consists of.\n",
		B_KDEBUG_PIPE_FINAL_RERUN);
}
