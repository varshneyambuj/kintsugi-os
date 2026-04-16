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
 *   Copyright 2008-2009, Ingo Weinhold, ingo_weinhold@gmx.de
 *   Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file debug_commands.cpp
 * @brief Registry and dispatcher for kernel debugger (KDL) commands.
 *
 * Owns the singly-linked list of debugger_command records populated by
 * add_debugger_command() / add_debugger_command_etc(), and provides the
 * lookup, sort, pipe-execution, and fault-isolated invocation primitives
 * consumed by debug.cpp. The built-in commands themselves live in
 * debug_builtin_commands.cpp; this file only manages the registry.
 *
 * Execution context: command handlers are dispatched from the KDL main loop,
 * which runs with interrupts disabled on the panicking/breaking CPU (all
 * other CPUs are held in an IPI rendezvous). Handlers must therefore avoid
 * anything that requires interrupts to be on (waiting on mutexes, waiting for
 * timers, the scheduler, normal heap allocation) and use only KDL-safe
 * facilities like kprintf()/kputs() and debug_malloc() via DebugAllocPool.
 * The sSpinlock here guards the registry against the small number of call
 * sites that touch it from normal (non-KDL) context, e.g. add/remove from a
 * driver's init path; inside KDL the spinlock is uncontested because all
 * other CPUs are stopped.
 */


#include "debug_commands.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <KernelExport.h>

#include <debug.h>
#include <debug_heap.h>
#include <lock.h>
#include <thread.h>
#include <util/AutoLock.h>

#include "debug_output_filter.h"
#include "debug_variables.h"


#define INVOKE_COMMAND_FAULT	1
#define INVOKE_COMMAND_ERROR	2


struct invoke_command_parameters {
	debugger_command*	command;
	int					argc;
	char**				argv;
	int					result;
};


static const int32 kMaxInvokeCommandDepth = 5;
static const int32 kOutputBufferSize = 1024;


bool gInvokeCommandDirectly = false;

static spinlock sSpinlock = B_SPINLOCK_INITIALIZER;

static struct debugger_command *sCommands;

static jmp_buf sInvokeCommandEnv[kMaxInvokeCommandDepth];
static int32 sInvokeCommandLevel = 0;
static bool sInCommand = false;
static char sOutputBuffers[MAX_DEBUGGER_COMMAND_PIPE_LENGTH][kOutputBufferSize];
static debugger_command_pipe* sCurrentPipe;
static int32 sCurrentPipeSegment;


static int invoke_pipe_segment(debugger_command_pipe* pipe, int32 index,
	char* argument);


class PipeDebugOutputFilter : public DebugOutputFilter {
public:
	/**
	 * @brief Default-construct an empty filter (fields populated by placement new).
	 */
	PipeDebugOutputFilter()
	{
	}

	/**
	 * @brief Construct a filter that forwards complete lines to the next pipe stage.
	 *
	 * @param pipe       Pipe being executed.
	 * @param segment    Index of the segment that will consume this filter's output
	 *                   (this filter belongs to segment - 1).
	 * @param buffer     Scratch buffer for line assembly (owned by caller).
	 * @param bufferSize Size of @p buffer; one byte is reserved for NUL.
	 */
	PipeDebugOutputFilter(debugger_command_pipe* pipe, int32 segment,
			char* buffer, size_t bufferSize)
		:
		fPipe(pipe),
		fSegment(segment),
		fBuffer(buffer),
		fBufferCapacity(bufferSize - 1),
		fBufferSize(0)
	{
	}

	/**
	 * @brief Append a literal string, flushing each complete line into the next segment.
	 *
	 * Runs with interrupts disabled from the KDL command path. When the buffer
	 * fills without a newline, it is flushed anyway so the pipe keeps moving.
	 *
	 * @param string NUL-terminated string to enqueue.
	 */
	virtual void PrintString(const char* string)
	{
		if (fPipe->broken)
			return;

		size_t size = strlen(string);
		while (const char* newLine = strchr(string, '\n')) {
			size_t length = newLine - string;
			_Append(string, length);

			// invoke command
			fBuffer[fBufferSize] = '\0';
			invoke_pipe_segment(fPipe, fSegment + 1, fBuffer);

			fBufferSize = 0;

			string = newLine + 1;
			size -= length + 1;
		}

		_Append(string, size);

		if (fBufferSize == fBufferCapacity) {
			// buffer is full, but contains no newline -- execute anyway
			invoke_pipe_segment(fPipe, fSegment + 1, fBuffer);
			fBufferSize = 0;
		}
	}

	/**
	 * @brief Append a vsnprintf-formatted fragment, flushing each complete line.
	 *
	 * Formats directly into the line buffer to avoid intermediate allocations,
	 * then scans for '\n' and invokes the next pipe segment for every line.
	 * Runs with interrupts disabled from the KDL command path.
	 *
	 * @param format printf-style format string.
	 * @param args   Argument list.
	 */
	virtual void Print(const char* format, va_list args)
	{
		if (fPipe->broken)
			return;

		// print directly into the buffer
		if (fBufferSize < fBufferCapacity) {
			size_t totalBytes = vsnprintf(fBuffer + fBufferSize,
				fBufferCapacity - fBufferSize, format, args);
			fBufferSize += std::min(totalBytes,
				fBufferCapacity - fBufferSize - 1);
		}

		// execute every complete line
		fBuffer[fBufferSize] = '\0';
		char* line = fBuffer;

		while (char* newLine = strchr(line, '\n')) {
			// invoke command
			*newLine = '\0';
			invoke_pipe_segment(fPipe, fSegment + 1, line);

			line = newLine + 1;
		}

		size_t left = fBuffer + fBufferSize - line;

		if (left == fBufferCapacity) {
			// buffer is full, but contains no newline -- execute anyway
			invoke_pipe_segment(fPipe, fSegment + 1, fBuffer);
			left = 0;
		}

		if (left > 0)
			memmove(fBuffer, line, left);

		fBufferSize = left;
	}

private:
	/**
	 * @brief Copy bytes into the line buffer, capped to remaining capacity.
	 *
	 * @param string Source bytes (not necessarily NUL-terminated).
	 * @param length Desired length to append.
	 */
	void _Append(const char* string, size_t length)
	{
		size_t toAppend = min_c(length, fBufferCapacity - fBufferSize);
		memcpy(fBuffer + fBufferSize, string, toAppend);
		fBufferSize += length;
	}

private:
	debugger_command_pipe*	fPipe;
	int32					fSegment;
	char*					fBuffer;
	size_t					fBufferCapacity;
	size_t					fBufferSize;
};


static PipeDebugOutputFilter sPipeOutputFilters[
	MAX_DEBUGGER_COMMAND_PIPE_LENGTH - 1];


/**
 * @brief Trampoline invoked by debug_call_with_fault_handler().
 *
 * Unpacks the parameter struct and calls the actual command function. Any
 * page fault inside the call longjmp()s back to invoke_debugger_command().
 *
 * @param _parameters Pointer to an invoke_command_parameters on the stack of
 *                    the caller.
 */
static void
invoke_command_trampoline(void* _parameters)
{
	invoke_command_parameters* parameters
		= (invoke_command_parameters*)_parameters;
	parameters->result = parameters->command->func(parameters->argc,
		parameters->argv);
}


/**
 * @brief Execute one segment of a command pipe and restore filters on exit.
 *
 * Installs the segment's output filter (a PipeDebugOutputFilter that feeds
 * the next segment, or the default filter for the final segment), patches
 * the last-argument slot from the upstream segment, runs the command under
 * invoke_debugger_command(), then restores the previous filter. If the
 * segment returns B_KDEBUG_ERROR the pipe is marked broken and upstream
 * segments are aborted iteratively via abort_debugger_command().
 *
 * @param pipe     Pipe being executed.
 * @param index    Segment index within the pipe.
 * @param argument Output line passed in from the previous segment (NULL for
 *                 the leading segment).
 * @return The command's return code, or B_KDEBUG_ERROR on pipe break.
 */
static int
invoke_pipe_segment(debugger_command_pipe* pipe, int32 index, char* argument)
{
	// set debug output
	DebugOutputFilter* oldFilter = set_debug_output_filter(
		index == pipe->segment_count - 1
			? &gDefaultDebugOutputFilter
			: (DebugOutputFilter*)&sPipeOutputFilters[index]);

	// set last command argument
	debugger_command_pipe_segment& segment = pipe->segments[index];
	if (index > 0)
		segment.argv[segment.argc - 1] = argument;

	// invoke command
	int32 oldIndex = sCurrentPipeSegment;
	sCurrentPipeSegment = index;

	int result = invoke_debugger_command(segment.command, segment.argc,
		segment.argv);
	segment.invocations++;

	sCurrentPipeSegment = oldIndex;

	// reset debug output
	set_debug_output_filter(oldFilter);

	if (result == B_KDEBUG_ERROR) {
		pipe->broken = true;

		// Abort the previous pipe segment execution. The complete pipe is
		// aborted iteratively this way.
		if (index > 0)
			abort_debugger_command();
	}

	return result;
}


/**
 * @brief Iterate the command list, returning the next entry with a name prefix.
 *
 * Used both for explicit enumeration and for partial-match lookup. Pass NULL
 * to start from the head.
 *
 * @param command   Previously returned command, or NULL to start fresh.
 * @param prefix    Name prefix to match.
 * @param prefixLen Length of @p prefix.
 * @return The next matching command, or NULL when the list is exhausted.
 */
debugger_command*
next_debugger_command(debugger_command* command, const char* prefix,
	int prefixLen)
{
	if (command == NULL)
		command = sCommands;
	else
		command = command->next;

	while (command != NULL && strncmp(prefix, command->name, prefixLen) != 0)
		command = command->next;

	return command;
}


/**
 * @brief Look up a command by full or (optionally) partial name.
 *
 * Exact matches always take precedence. If @p partialMatch is true and no
 * exact match exists, the function succeeds only when exactly one command
 * starts with @p name; otherwise it returns NULL and sets @p ambiguous.
 *
 * @param name         Command name to look up.
 * @param partialMatch If true, prefix matches are allowed.
 * @param ambiguous    [out] Set to true when a prefix matches multiple commands.
 * @return Matching command, or NULL.
 */
debugger_command *
find_debugger_command(const char *name, bool partialMatch, bool& ambiguous)
{
	debugger_command *command;

	ambiguous = false;

	// search command by full name

	for (command = sCommands; command != NULL; command = command->next) {
		if (strcmp(name, command->name) == 0)
			return command;
	}

	// if it couldn't be found, search for a partial match

	if (partialMatch) {
		int length = strlen(name);
		command = next_debugger_command(NULL, name, length);
		if (command != NULL) {
			if (next_debugger_command(command, name, length) == NULL)
				return command;

			ambiguous = true;
		}
	}

	return NULL;
}


/**
 * @brief Report whether a debugger command is presently being dispatched.
 *
 * Consulted by blue_screen.cpp to decide whether next_line() should prompt
 * for paging and by other KDL helpers that need to distinguish command
 * output from unsolicited dprintf() traffic.
 *
 * @return true while inside invoke_debugger_command(), false otherwise.
 */
bool
in_command_invocation(void)
{
	return sInCommand;
}


/**
 * @brief Safely invoke a KDL command under a fault handler.
 *
 * Runs with interrupts disabled on the KDL CPU. Intercepts "--help" when a
 * usage string is registered, replaces argv[0] with the canonical command
 * name, sets up a DebugAllocPoolScope for command-local scratch allocations,
 * and then dispatches the command via debug_call_with_fault_handler(). A
 * setjmp/longjmp pair catches page faults inside the handler so a buggy
 * command cannot recursively re-enter KDL or break "cont". Enforces
 * kMaxInvokeCommandDepth to cap recursion from command pipes.
 *
 * @param command Command to invoke.
 * @param argc    Argument count (argv[0] is overwritten with command->name).
 * @param argv    Argument vector.
 * @return The command's return value; 0 for --help; B_KDEBUG_ERROR on fault
 *         or depth overflow.
 */
int
invoke_debugger_command(struct debugger_command *command, int argc, char** argv)
{
	// intercept invocations with "--help" and directly print the usage text
	// If we know the command's usage text, intercept "--help" invocations
	// and print it directly.
	if (argc == 2 && argv[1] != NULL && strcmp(argv[1], "--help") == 0
			&& command->usage != NULL) {
		kprintf_unfiltered("usage: %s ", command->name);
		kputs_unfiltered(command->usage);
		return 0;
	}

	// replace argv[0] with the actual command name
	argv[0] = (char *)command->name;

	DebugAllocPoolScope allocPoolScope;
		// Will automatically clean up all allocations the command leaves over.

	// Invoking the command directly might be useful when debugging debugger
	// commands.
	if (gInvokeCommandDirectly)
		return command->func(argc, argv);

	if (sInvokeCommandLevel >= kMaxInvokeCommandDepth) {
		kprintf_unfiltered("\n[*** MAX COMMAND DEPTH HIT ***]\n");
		return B_KDEBUG_ERROR;
	}

	invoke_command_parameters parameters;
	parameters.command = command;
	parameters.argc = argc;
	parameters.argv = argv;

	sInCommand = true;
	int result = debug_call_with_fault_handler(
		sInvokeCommandEnv[sInvokeCommandLevel++],
		&invoke_command_trampoline, &parameters);
	sInvokeCommandLevel--;
	sInCommand = false;

	switch (result) {
		case 0:
			return parameters.result;

		case INVOKE_COMMAND_FAULT:
		{
			debug_page_fault_info* info = debug_get_page_fault_info();
			if ((info->flags & DEBUG_PAGE_FAULT_NO_INFO) == 0) {
				kprintf_unfiltered("\n[*** %s FAULT at %#lx, pc: %#lx ***]\n",
					(info->flags & DEBUG_PAGE_FAULT_NO_INFO) != 0
						? "WRITE" : "READ",
					info->fault_address, info->pc);
			} else {
				kprintf_unfiltered("\n[*** READ/WRITE FAULT (?), "
					"pc: %#lx ***]\n", info->pc);
			}
			break;
		}
		case INVOKE_COMMAND_ERROR:
			// command aborted (no page fault)
			break;
	}

	return B_KDEBUG_ERROR;
}


/**
 * @brief Abort the currently executing command (and any enclosing pipe).
 *
 * longjmp()s back to the matching invoke_debugger_command() with status
 * INVOKE_COMMAND_ERROR, unwinding through any nested pipe segments. Does
 * nothing if gInvokeCommandDirectly is set (useful when debugging KDL
 * commands themselves). Interrupts are disabled in the KDL context from
 * which this is called.
 *
 * When it does unwind, this function does not return.
 */
void
abort_debugger_command()
{
	if (!gInvokeCommandDirectly && sInvokeCommandLevel > 0) {
		longjmp(sInvokeCommandEnv[sInvokeCommandLevel - 1],
			INVOKE_COMMAND_ERROR);
	}
}


/**
 * @brief Execute a parsed command pipe end-to-end.
 *
 * Stashes the caller's pipe context, constructs a PipeDebugOutputFilter for
 * each non-terminal segment (using placement new over the preallocated
 * sPipeOutputFilters array, since the heap is unavailable in KDL), then runs
 * the segments from left to right. After the primary run, any segments that
 * advertise B_KDEBUG_PIPE_FINAL_RERUN get one more invocation with a NULL
 * argument so they can emit their accumulated state; a segment may return
 * B_KDEBUG_RESTART_PIPE to restart the whole pipe. Runs with interrupts
 * disabled from the KDL context.
 *
 * @param pipe Pipe to execute.
 * @return The last command's return code, or B_KDEBUG_ERROR on pipe break.
 */
int
invoke_debugger_command_pipe(debugger_command_pipe* pipe)
{
	debugger_command_pipe* oldPipe = sCurrentPipe;
	sCurrentPipe = pipe;

	// prepare outputs
	// TODO: If a pipe is invoked in a pipe, outputs will clash.
	int32 segments = pipe->segment_count;
	for (int32 i = 0; i < segments - 1; i++) {
		new(&sPipeOutputFilters[i]) PipeDebugOutputFilter(pipe, i,
			sOutputBuffers[i], kOutputBufferSize);
	}

	int result;
	while (true) {
		result = invoke_pipe_segment(pipe, 0, NULL);

		// perform final rerun for all commands that want it
		for (int32 i = 1; result != B_KDEBUG_ERROR && i < segments; i++) {
			debugger_command_pipe_segment& segment = pipe->segments[i];
			if ((segment.command->flags & B_KDEBUG_PIPE_FINAL_RERUN) != 0) {
				result = invoke_pipe_segment(pipe, i, NULL);
				if (result == B_KDEBUG_RESTART_PIPE) {
					for (int32 j = 0; j < i; j++)
						pipe->segments[j].invocations = 0;
					break;
				}
			}
		}

		if (result != B_KDEBUG_RESTART_PIPE)
			break;
	}

	sCurrentPipe = oldPipe;

	return result;
}


/**
 * @brief Return the pipe currently being executed, or NULL.
 *
 * Intended for command handlers that need to know their context, e.g. to
 * suppress pagination when feeding output into another segment.
 *
 * @return The active pipe, or NULL if no pipe is running.
 */
debugger_command_pipe*
get_current_debugger_command_pipe()
{
	return sCurrentPipe;
}


/**
 * @brief Return the pipe segment currently executing, or NULL.
 *
 * @return The active segment within the current pipe, or NULL if no pipe
 *         is running.
 */
debugger_command_pipe_segment*
get_current_debugger_command_pipe_segment()
{
	return sCurrentPipe != NULL
		? &sCurrentPipe->segments[sCurrentPipeSegment] : NULL;
}


/**
 * @brief Access the head of the command registry.
 *
 * Used by the built-in "help" command and other enumerators; the returned
 * list may be mutated by concurrent add/remove calls outside KDL, so callers
 * should hold sSpinlock when walking it from non-KDL context.
 *
 * @return First registered command, or NULL if the registry is empty.
 */
debugger_command*
get_debugger_commands()
{
	return sCommands;
}


/**
 * @brief Alphabetically sort the command registry in place (bubble sort).
 *
 * Called once after the built-in commands have been registered so that
 * "help" lists them in order. O(n^2), but n is tiny and this runs at boot
 * rather than in KDL.
 */
void
sort_debugger_commands()
{
	// bubble sort the commands
	debugger_command* stopCommand = NULL;
	while (stopCommand != sCommands) {
		debugger_command** command = &sCommands;
		while (true) {
			debugger_command* nextCommand = (*command)->next;
			if (nextCommand == stopCommand) {
				stopCommand = *command;
				break;
			}

			if (strcmp((*command)->name, nextCommand->name) > 0) {
				(*command)->next = nextCommand->next;
				nextCommand->next = *command;
				*command = nextCommand;
			}

			command = &(*command)->next;
		}
	}
}


/**
 * @brief Register a new KDL command with description, usage text, and flags.
 *
 * Rejects duplicates (exact name match), allocates a debugger_command record,
 * and pushes it onto sCommands under sSpinlock (with interrupts disabled).
 * Normally called from driver/module init paths outside KDL; when called
 * from KDL (e.g. by another command) the spinlock is uncontested since the
 * other CPUs are stopped.
 *
 * @param name        Command name.
 * @param func        Handler to invoke. Will run with interrupts disabled
 *                    from the KDL context.
 * @param description One-line description shown by "help".
 * @param usage       Detailed usage text shown for "--help"; may be NULL.
 * @param flags       B_KDEBUG_* flags (e.g. B_KDEBUG_PIPE_FINAL_RERUN).
 * @return B_OK on success; B_NAME_IN_USE if the command already exists;
 *         B_NO_MEMORY on allocation failure.
 */
status_t
add_debugger_command_etc(const char* name, debugger_command_hook func,
	const char* description, const char* usage, uint32 flags)
{
	bool ambiguous;
	debugger_command *cmd = find_debugger_command(name, false, ambiguous);
	if (cmd != NULL && ambiguous == false)
		return B_NAME_IN_USE;

	cmd = (debugger_command*)malloc(sizeof(debugger_command));
	if (cmd == NULL)
		return B_NO_MEMORY;

	cmd->func = func;
	cmd->name = name;
	cmd->description = description;
	cmd->usage = usage;
	cmd->flags = flags;

	InterruptsSpinLocker _(sSpinlock);

	cmd->next = sCommands;
	sCommands = cmd;

	return B_OK;
}


/**
 * @brief Register @p newName as a second entry point to an existing command.
 *
 * Looks up @p oldName, then registers a new command that shares its handler,
 * usage, and flags. The alias is an independent registry entry, so removing
 * @p oldName does not remove it.
 *
 * @param newName     Name of the alias.
 * @param oldName     Name of the existing command.
 * @param description Description for the alias; falls back to the original's
 *                    description when NULL.
 * @return B_OK on success; B_NAME_NOT_FOUND if @p oldName does not exist;
 *         otherwise the error from add_debugger_command_etc().
 */
status_t
add_debugger_command_alias(const char* newName, const char* oldName,
	const char* description)
{
	// get the old command
	bool ambiguous;
	debugger_command* command = find_debugger_command(oldName, false,
		ambiguous);
	if (command == NULL)
		return B_NAME_NOT_FOUND;

	// register new command
	return add_debugger_command_etc(newName, command->func,
		description != NULL ? description : command->description,
		command->usage, command->flags);
}


/**
 * @brief Print usage text for a command via a prefix or exact name.
 *
 * Uses partial-match lookup (ignoring ambiguity). If a registered usage
 * string exists, prints it directly via kprintf_unfiltered(); otherwise
 * invokes the command with "--help" so it can print its own. Runs with
 * interrupts disabled in KDL context.
 *
 * @param commandName Command name or unambiguous prefix.
 * @return true if a command was found, false otherwise.
 */
bool
print_debugger_command_usage(const char* commandName)
{
	// get the command
	bool ambiguous;
	debugger_command* command = find_debugger_command(commandName, true,
		ambiguous);
	if (command == NULL)
		return false;

	// directly print the usage text, if we know it, otherwise invoke the
	// command with "--help"
	if (command->usage != NULL) {
		kprintf_unfiltered("usage: %s ", command->name);
		kputs_unfiltered(command->usage);
	} else {
		const char* args[3] = { NULL, "--help", NULL };
		invoke_debugger_command(command, 2, (char**)args);
	}

	return true;
}


/**
 * @brief Test whether a command is registered under the exact given name.
 *
 * @param commandName Name to look up (exact match only).
 * @return true if the command exists, false otherwise.
 */
bool
has_debugger_command(const char* commandName)
{
	bool ambiguous;
	return find_debugger_command(commandName, false, ambiguous) != NULL;
}


//	#pragma mark - public API

/**
 * @brief Public API: register a simple command with a description only.
 *
 * Thin wrapper around add_debugger_command_etc() that defaults usage to NULL
 * and flags to zero. This is the interface exposed to drivers and modules.
 *
 * @param name Command name.
 * @param func Handler (runs with interrupts disabled when invoked from KDL).
 * @param desc One-line description for "help".
 * @return B_OK on success; otherwise the error from add_debugger_command_etc().
 */
int
add_debugger_command(const char *name, int (*func)(int, char **),
	const char *desc)
{
	return add_debugger_command_etc(name, func, desc, NULL, 0);
}


/**
 * @brief Public API: unregister a command by name and handler address.
 *
 * Disables interrupts and acquires sSpinlock to splice the matching node out
 * of the registry, then frees it. The handler pointer is compared as well as
 * the name so two modules registering the same name cannot accidentally
 * remove each other's command.
 *
 * @param name Command name.
 * @param func Handler originally passed to add_debugger_command*().
 * @return B_NO_ERROR on success; B_NAME_NOT_FOUND if no matching entry exists.
 */
int
remove_debugger_command(const char * name, int (*func)(int, char **))
{
	struct debugger_command *cmd = sCommands;
	struct debugger_command *prev = NULL;
	cpu_status state;

	state = disable_interrupts();
	acquire_spinlock(&sSpinlock);

	while (cmd) {
		if (!strcmp(cmd->name, name) && cmd->func == func)
			break;

		prev = cmd;
		cmd = cmd->next;
	}

	if (cmd) {
		if (cmd == sCommands)
			sCommands = cmd->next;
		else
			prev->next = cmd->next;
	}

	release_spinlock(&sSpinlock);
	restore_interrupts(state);

	if (cmd) {
		free(cmd);
		return B_NO_ERROR;
	}

	return B_NAME_NOT_FOUND;
}

