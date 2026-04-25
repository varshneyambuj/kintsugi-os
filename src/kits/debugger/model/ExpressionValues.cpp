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
 *   Copyright 2014-2016, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ExpressionValues.cpp
 * @brief Implementation of ExpressionValues, a hash table mapping
 *        (function id, thread, expression text) keys to their evaluated
 *        BVariant values.
 *
 * The debugger uses this to remember evaluated watch and conditional
 * expressions across stops so the inspector can present cached values
 * immediately when re-entering the same function on the same thread.
 */


#include "ExpressionValues.h"

#include <new>

#include "FunctionID.h"
#include "model/Thread.h"


/**
 * @brief Composite (function id, thread, expression) lookup key.
 */
struct ExpressionValues::Key {
	FunctionID*			function;
	::Thread*			thread;
	BString				expression;

	/**
	 * @brief Constructs a Key wrapping the three components by reference.
	 *
	 * @param function   Function id scoping the expression.
	 * @param thread     Thread on which the expression was evaluated.
	 * @param expression Source-form expression text.
	 */
	Key(FunctionID* function, ::Thread* thread, const BString& expression)
		:
		function(function),
		thread(thread),
		expression(expression)
	{
	}

	/**
	 * @brief Combines per-component hashes into one composite hash.
	 *
	 * @return Hash value identifying the key.
	 */
	uint32 HashValue() const
	{
		return function->HashValue() ^ thread->ID()
			^ expression.HashValue();
	}

	/**
	 * @brief Tests two keys for equality on all three components.
	 *
	 * @param other Other key.
	 * @return     True if function, thread id, and expression all match.
	 */
	bool operator==(const Key& other) const
	{
		return *function == *other.function
			&& thread->ID() == other.thread->ID()
			&& expression == other.expression;
	}
};


/**
 * @brief Hash-table entry binding a Key to its captured BVariant value.
 */
struct ExpressionValues::ValueEntry : Key {
	BVariant			value;
	ValueEntry*			next;

	/**
	 * @brief Constructs an entry, acquiring references to function and thread.
	 *
	 * @param function   Function id (reference acquired).
	 * @param thread     Thread (reference acquired).
	 * @param expression Source-form expression text.
	 */
	ValueEntry(FunctionID* function, ::Thread* thread,
		const BString& expression)
		:
		Key(function, thread, expression)
	{
		function->AcquireReference();
		thread->AcquireReference();
	}

	/**
	 * @brief Releases the references acquired in the constructor.
	 */
	~ValueEntry()
	{
		function->ReleaseReference();
		thread->ReleaseReference();
	}
};


/**
 * @brief BOpenHashTable adapter for ValueEntry hashing and linking.
 */
struct ExpressionValues::ValueEntryHashDefinition {
	typedef Key			KeyType;
	typedef	ValueEntry	ValueType;

	/**
	 * @brief Hashes a lookup key.
	 *
	 * @param key Composite key.
	 * @return   Key hash.
	 */
	size_t HashKey(const Key& key) const
	{
		return key.HashValue();
	}

	/**
	 * @brief Hashes an existing entry (reuses the key hash).
	 *
	 * @param value Entry to hash.
	 * @return     Entry hash.
	 */
	size_t Hash(const ValueEntry* value) const
	{
		return value->HashValue();
	}

	/**
	 * @brief Compares a key against an existing entry.
	 *
	 * @param key   Candidate key.
	 * @param value Existing entry.
	 * @return     True if @a key matches.
	 */
	bool Compare(const Key& key, const ValueEntry* value) const
	{
		return key == *value;
	}

	/**
	 * @brief Returns the chain link inside @a value used by the hash table.
	 *
	 * @param value Entry whose link to expose.
	 * @return     Reference to the next-pointer slot.
	 */
	ValueEntry*& GetLink(ValueEntry* value) const
	{
		return value->next;
	}
};


/**
 * @brief Constructs an uninitialised ExpressionValues; @c Init() must follow.
 */
ExpressionValues::ExpressionValues()
	:
	fValues(NULL)
{
}


/**
 * @brief Copy-constructs by initialising and cloning every entry.
 *
 * Throws @c std::bad_alloc on allocation failure.
 *
 * @param other Source instance whose entries are duplicated.
 */
ExpressionValues::ExpressionValues(const ExpressionValues& other)
	:
	fValues(NULL)
{
	try {
		// init
		if (Init() != B_OK)
			throw std::bad_alloc();

		// clone all values
		for (ValueTable::Iterator it = other.fValues->GetIterator();
				ValueEntry* entry = it.Next();) {
			if (SetValue(entry->function, entry->thread, entry->expression,
				entry->value) != B_OK) {
				throw std::bad_alloc();
			}
		}
	} catch (...) {
		_Cleanup();
		throw;
	}
}


/**
 * @brief Destroys the table and releases all entry references.
 */
ExpressionValues::~ExpressionValues()
{
	_Cleanup();
}


/**
 * @brief Allocates and initialises the underlying hash table.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
ExpressionValues::Init()
{
	fValues = new(std::nothrow) ValueTable;
	if (fValues == NULL)
		return B_NO_MEMORY;

	return fValues->Init();
}


/**
 * @brief Looks up the cached value for (function, thread, expression).
 *
 * @param function   Function id to look up.
 * @param thread     Thread on which the expression was evaluated.
 * @param expression Pointer to the expression text (must be non-NULL).
 * @param _value     Receives the value when found.
 * @return          True if a value was found.
 */
bool
ExpressionValues::GetValue(FunctionID* function, ::Thread* thread,
	const BString* expression, BVariant& _value) const
{
	ValueEntry* entry = fValues->Lookup(Key(function, thread, *expression));
	if (entry == NULL)
		return false;

	_value = entry->value;
	return true;
}


/**
 * @brief Tests whether (function, thread, expression) has a cached value.
 *
 * @param function   Function id.
 * @param thread     Thread.
 * @param expression Pointer to the expression text (must be non-NULL).
 * @return          True if an entry exists.
 */
bool
ExpressionValues::HasValue(FunctionID* function, ::Thread* thread,
	const BString* expression) const
{
	return fValues->Lookup(Key(function, thread, *expression)) != NULL;
}


/**
 * @brief Stores or replaces the cached value at (function, thread, expression).
 *
 * @param function   Function id (reference acquired by entry).
 * @param thread     Thread (reference acquired by entry).
 * @param expression Source-form expression text.
 * @param value      Value to store.
 * @return          @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
ExpressionValues::SetValue(FunctionID* function, ::Thread* thread,
	const BString& expression, const BVariant& value)
{
	ValueEntry* entry = fValues->Lookup(Key(function, thread, expression));
	if (entry == NULL) {
		entry = new(std::nothrow) ValueEntry(function, thread, expression);
		if (entry == NULL)
			return B_NO_MEMORY;
		fValues->Insert(entry);
	}

	entry->value = value;
	return B_OK;
}


/**
 * @brief Releases every entry, deletes the table, and resets @c fValues to NULL.
 */
void
ExpressionValues::_Cleanup()
{
	if (fValues != NULL) {
		ValueEntry* entry = fValues->Clear(true);

		while (entry != NULL) {
			ValueEntry* next = entry->next;
			delete entry;
			entry = next;
		}

		delete fValues;
		fValues = NULL;
	}
}
