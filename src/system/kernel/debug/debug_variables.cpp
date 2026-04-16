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
 *   Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file debug_variables.cpp
 * @brief Named variable store for the kernel debugger (KDL) expression parser.
 *
 * Implements the `$foo = 42` style named variables used by KDL commands to
 * capture intermediate values (typically addresses) between invocations. Two
 * pools are maintained: a fixed set of persistent slots and a smaller LRU pool
 * for temporary variables whose names begin with '_'. Symbol-prefixed names
 * ('@') and architecture-prefixed names ('$') are forwarded to the ELF symbol
 * lookup and the per-arch debug-variable backend respectively.
 */


#include "debug_variables.h"

#include <string.h>

#include <KernelExport.h>

#include <arch/debug.h>
#include <debug.h>
#include <elf.h>
#include <util/DoublyLinkedList.h>


static const int kVariableCount				= 64;
static const int kTemporaryVariableCount	= 32;
static const char kTemporaryVariablePrefix	= '_';
static const char kArchSpecificVariablePrefix = '$';
static const char kSymbolVariablePrefix = '@';
static const char* const kCommandReturnValueVariable = "_";


struct Variable {
	char	name[MAX_DEBUG_VARIABLE_NAME_LEN];
	uint64	value;

	inline bool IsUsed() const
	{
		return name[0] != '\0';
	}

	void Init(const char* variableName)
	{
		strlcpy(name, variableName, sizeof(name));
	}

	void Uninit()
	{
		name[0] = '\0';
	}

	inline bool HasName(const char* variableName) const
	{
		return strncmp(name, variableName, sizeof(name)) == 0;
	}
};

struct TemporaryVariable : Variable,
		DoublyLinkedListLinkImpl<TemporaryVariable> {
	bool queued;
};

static Variable sVariables[kVariableCount];
static TemporaryVariable sTemporaryVariables[kTemporaryVariableCount];

static DoublyLinkedList<TemporaryVariable> sTemporaryVariablesLRUQueue;


// Returns true if the name begins with the temporary-variable prefix ('_').
static inline bool
is_temporary_variable(const char* variableName)
{
	return variableName[0] == kTemporaryVariablePrefix;
}


// Returns true if the name uses the arch-specific sigil ('$').
static inline bool
is_arch_specific_variable(const char* variableName)
{
	return variableName[0] == kArchSpecificVariablePrefix;
}


// Returns true if the name uses the ELF-symbol sigil ('@').
static inline bool
is_symbol_variable(const char* variableName)
{
	return variableName[0] == kSymbolVariablePrefix;
}


/**
 * @brief Remove a temporary variable from the LRU queue if currently queued.
 *
 * The temporary-variable LRU tracks usage order so the oldest slot can be
 * reclaimed when the pool is full.
 *
 * @param variable Temporary variable to detach from the queue; may already be
 *                 unqueued in which case the call is a no-op.
 * @return void
 */
static void
dequeue_temporary_variable(TemporaryVariable* variable)
{
	// dequeue if queued
	if (variable->queued) {
		sTemporaryVariablesLRUQueue.Remove(variable);
		variable->queued = false;
	}
}


/**
 * @brief Release a variable slot, updating LRU bookkeeping if temporary.
 *
 * For temporary variables the LRU entry is first removed; then the slot's
 * name is cleared which marks it as free for reuse.
 *
 * @param variable Variable slot to free; must be currently in use.
 * @return void
 */
static void
unset_variable(Variable* variable)
{
	if (is_temporary_variable(variable->name))
		dequeue_temporary_variable(static_cast<TemporaryVariable*>(variable));

	variable->Uninit();
}


/**
 * @brief Mark a variable as recently used by moving it to the LRU tail.
 *
 * Only temporary variables participate in the LRU; calls for persistent
 * variables return immediately.
 *
 * @param _variable Variable that was just read or written.
 * @return void
 */
static void
touch_variable(Variable* _variable)
{
	if (!is_temporary_variable(_variable->name))
		return;

	TemporaryVariable* variable = static_cast<TemporaryVariable*>(_variable);

	// move to the end of the queue
	dequeue_temporary_variable(variable);
	sTemporaryVariablesLRUQueue.Add(variable);
	variable->queued = true;
}


/**
 * @brief Evict the least-recently-used temporary variable and return its slot.
 *
 * Used when a new temporary variable must be created but all slots are
 * occupied; the evicted slot is cleared before being returned.
 *
 * @return Pointer to a now-free TemporaryVariable slot, or NULL if the LRU
 *         queue was empty.
 */
static Variable*
free_temporary_variable_slot()
{
	TemporaryVariable* variable = sTemporaryVariablesLRUQueue.RemoveHead();
	if (variable) {
		variable->queued = false;
		variable->Uninit();
	}

	return variable;
}


/**
 * @brief Look up a variable slot, optionally creating one on miss.
 *
 * Scans either the temporary or persistent array based on the name prefix;
 * on miss the first free slot (or an LRU-evicted one, for temporaries) is
 * initialised with the given name and returned.
 *
 * @param variableName Null-terminated variable name.
 * @param create If true, allocate a new slot when the variable is not found.
 * @return Pointer to the matching or freshly created Variable, or NULL if not
 *         found and not creating, or if no slot could be allocated.
 */
static Variable*
get_variable(const char* variableName, bool create)
{
	// find the variable in the respective array and a free slot, we can
	// use, if it doesn't exist yet
	Variable* freeSlot = NULL;

	if (is_temporary_variable(variableName)) {
		// temporary variable
		for (int i = 0; i < kTemporaryVariableCount; i++) {
			TemporaryVariable* variable = sTemporaryVariables + i;

			if (!variable->IsUsed()) {
				if (freeSlot == NULL)
					freeSlot = variable;
			} else if (variable->HasName(variableName))
				return variable;
		}

		if (create && freeSlot == NULL)
			freeSlot = free_temporary_variable_slot();
	} else {
		// persistent variable
		for (int i = 0; i < kVariableCount; i++) {
			Variable* variable = sVariables + i;

			if (!variable->IsUsed()) {
				if (freeSlot == NULL)
					freeSlot = variable;
			} else if (variable->HasName(variableName))
				return variable;
		}
	}


	if (create && freeSlot != NULL) {
		freeSlot->Init(variableName);
		return freeSlot;
	}

	return NULL;
}


// #pragma mark - debugger commands


/**
 * @brief KDL command handler: `unset <variable>`.
 *
 * Clears a single named variable. Prints usage if arguments are malformed or
 * if `--help` is supplied.
 *
 * @param argc Argument count as passed by the KDL dispatcher.
 * @param argv Argument vector; argv[1] is the variable name.
 * @return Always 0 (KDL commands do not propagate status here).
 */
static int
cmd_unset_variable(int argc, char **argv)
{
	static const char* const usage = "usage: unset <variable>\n"
		"Unsets the given variable, if it exists.\n";
	if (argc != 2 || strcmp(argv[1], "--help") == 0) {
		kprintf(usage);
		return 0;
	}

	const char* variable = argv[1];

	if (!unset_debug_variable(variable))
		kprintf("Did not find variable %s.\n", variable);

	return 0;
}


/**
 * @brief KDL command handler: `unset_all`.
 *
 * Clears every persistent and temporary debug variable slot.
 *
 * @param argc Argument count; only `--help` is recognised.
 * @param argv Argument vector.
 * @return Always 0.
 */
static int
cmd_unset_all_variables(int argc, char **argv)
{
	static const char* const usage = "usage: %s\n"
		"Unsets all variables.\n";
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		kprintf(usage, argv[0]);
		return 0;
	}

	unset_all_debug_variables();

	return 0;
}


/**
 * @brief KDL command handler: `vars` — list currently defined variables.
 *
 * Iterates both arrays and prints every used slot in both decimal and hex.
 *
 * @param argc Argument count; command takes no arguments.
 * @param argv Argument vector (unused other than argv[0]).
 * @return Always 0.
 */
static int
cmd_variables(int argc, char **argv)
{
	static const char* const usage = "usage: vars\n"
		"Unsets the given variable, if it exists.\n";
	if (argc != 1) {
		kprintf(usage);
		return 0;
	}

	// persistent variables
	for (int i = 0; i < kVariableCount; i++) {
		Variable& variable = sVariables[i];
		if (variable.IsUsed()) {
			kprintf("%16s: %" B_PRIu64 " (0x%" B_PRIx64 ")\n", variable.name,
				variable.value, variable.value);
		}
	}

	// temporary variables
	for (int i = 0; i < kTemporaryVariableCount; i++) {
		Variable& variable = sTemporaryVariables[i];
		if (variable.IsUsed()) {
			kprintf("%16s: %" B_PRIu64 " (0x%" B_PRIx64 ")\n", variable.name,
				variable.value, variable.value);
		}
	}

	return 0;
}


// #pragma mark - kernel public functions


/**
 * @brief Report whether a variable name resolves to something.
 *
 * Checks the local pools first, then falls back to ELF symbol lookup for
 * '@' names and to the arch backend for '$' names.
 *
 * @param variableName Null-terminated variable name including any sigil.
 * @return true if a value can be produced for this name.
 */
bool
is_debug_variable_defined(const char* variableName)
{
	if (get_variable(variableName, false) != NULL)
		return true;

	if (is_symbol_variable(variableName))
		return elf_debug_lookup_symbol(variableName + 1) != 0;

	return is_arch_specific_variable(variableName)
		&& arch_is_debug_variable_defined(variableName + 1);
}


/**
 * @brief Assign a 64-bit value to a named debug variable.
 *
 * Symbol-prefixed names are read-only and rejected. Arch-prefixed names are
 * forwarded to the arch backend. All other names are stored in the local
 * pools, creating a slot if necessary and updating LRU state for temporaries.
 *
 * @param variableName Null-terminated variable name (may include sigil).
 * @param value 64-bit value to store.
 * @return true on success, false if assignment is not permitted or no slot
 *         could be obtained.
 */
bool
set_debug_variable(const char* variableName, uint64 value)
{
	if (is_symbol_variable(variableName))
		return false;

	if (is_arch_specific_variable(variableName))
		return arch_set_debug_variable(variableName + 1, value) == B_OK;

	if (Variable* variable = get_variable(variableName, true)) {
		variable->value = value;
		touch_variable(variable);
		return true;
	}

	return false;
}


/**
 * @brief Fetch the value of a named debug variable.
 *
 * Local pool entries are consulted first (and touched in the LRU), falling
 * back to the arch backend for '$' names and to ELF symbol resolution for
 * '@' names. If none yield a value, `defaultValue` is returned.
 *
 * @param variableName Null-terminated variable name (may include sigil).
 * @param defaultValue Value to return when no definition is found.
 * @return The stored value, the resolved symbol address, or defaultValue.
 */
uint64
get_debug_variable(const char* variableName, uint64 defaultValue)
{
	if (Variable* variable = get_variable(variableName, false)) {
		touch_variable(variable);
		return variable->value;
	}

	uint64 value;
	if (is_arch_specific_variable(variableName)
		&& arch_get_debug_variable(variableName + 1, &value) == B_OK) {
		return value;
	}

	if (is_symbol_variable(variableName)) {
		addr_t value = elf_debug_lookup_symbol(variableName + 1);
		if (value != 0)
			return value;
	}

	return defaultValue;
}


/**
 * @brief Delete a named debug variable from the local pools.
 *
 * Only locally stored variables are affected; symbol and arch-backed names
 * are not cleared by this function and simply report a miss.
 *
 * @param variableName Null-terminated variable name.
 * @return true if a slot was found and cleared, false otherwise.
 */
bool
unset_debug_variable(const char* variableName)
{
	if (Variable* variable = get_variable(variableName, false)) {
		unset_variable(variable);
		return true;
	}

	return false;
}


/**
 * @brief Clear every persistent and temporary variable slot.
 *
 * Walks both arrays and releases each currently used entry, including
 * updating LRU bookkeeping for temporaries.
 *
 * @return void
 */
void
unset_all_debug_variables()
{
	// persistent variables
	for (int i = 0; i < kVariableCount; i++) {
		Variable& variable = sVariables[i];
		if (variable.IsUsed())
			unset_variable(&variable);
	}

	// temporary variables
	for (int i = 0; i < kTemporaryVariableCount; i++) {
		Variable& variable = sTemporaryVariables[i];
		if (variable.IsUsed())
			unset_variable(&variable);
	}
}


/**
 * @brief Register the debug-variable KDL commands during debugger init.
 *
 * Installs `unset`, `unset_all` and `vars` into the command table. Called
 * once from the kernel debugger's bring-up path.
 *
 * @return void
 */
void
debug_variables_init()
{
	add_debugger_command("unset", &cmd_unset_variable,
		"Unsets the given variable");
	add_debugger_command("unset_all", &cmd_unset_all_variables,
		"Unsets all variables");
	add_debugger_command("vars", &cmd_variables,
		"Lists all defined variables with their values");
}
