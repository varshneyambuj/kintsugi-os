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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2002-2009, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Tyler Dauwalder
 *       Ingo Weinhold
 *       Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Query.cpp
 * @brief Implementation of the BQuery class for filesystem attribute queries.
 *
 * This file implements BQuery, which provides a stack-based predicate builder and
 * query execution interface against BeOS/Haiku-style indexed file system attributes.
 * Callers push attribute names, operators, and values onto the predicate stack, then
 * call Fetch() to execute the query and iterate over matching entries via the
 * BEntryList interface. Live queries are supported via a BMessenger target.
 *
 * @see BQuery
 */


#include <Query.h>

#include <fcntl.h>
#include <new>
#include <time.h>

#include <Entry.h>
#include <fs_query.h>
#include <parsedate.h>
#include <Volume.h>

#include <MessengerPrivate.h>
#include <syscalls.h>

#include "QueryPredicate.h"
#include "storage_support.h"


using namespace std;
using namespace BPrivate::Storage;


/**
 * @brief Creates an uninitialized BQuery object.
 */
BQuery::BQuery()
	:
	BEntryList(),
	fStack(NULL),
	fPredicate(NULL),
	fDevice((dev_t)B_ERROR),
	fFlags(0),
	fPort(B_ERROR),
	fToken(0),
	fQueryFd(-1)
{
}


/**
 * @brief Frees all resources associated with the BQuery object.
 */
BQuery::~BQuery()
{
	Clear();
}


/**
 * @brief Resets the object to an uninitialized state, closing any open query.
 *
 * @return B_OK on success, or an error code if closing the query file descriptor failed.
 */
status_t
BQuery::Clear()
{
	// close the currently open query
	status_t error = B_OK;
	if (fQueryFd >= 0) {
		error = _kern_close(fQueryFd);
		fQueryFd = -1;
	}
	// delete the predicate stack and the predicate
	delete fStack;
	fStack = NULL;
	delete[] fPredicate;
	fPredicate = NULL;
	// reset the other parameters
	fDevice = (dev_t)B_ERROR;
	fFlags = 0;
	fPort = B_ERROR;
	fToken = 0;
	return error;
}


/**
 * @brief Pushes an attribute name onto the predicate stack.
 *
 * @param attrName The name of the file attribute to query.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushAttr(const char* attrName)
{
	return _PushNode(new(nothrow) AttributeNode(attrName), true);
}


/**
 * @brief Pushes a query operator onto the predicate stack.
 *
 * @param op The query_op operator to push (e.g., B_EQ, B_GT, B_AND).
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushOp(query_op op)
{
	status_t error = B_OK;
	switch (op) {
		case B_EQ:
		case B_GT:
		case B_GE:
		case B_LT:
		case B_LE:
		case B_NE:
		case B_CONTAINS:
		case B_BEGINS_WITH:
		case B_ENDS_WITH:
		case B_AND:
		case B_OR:
			error = _PushNode(new(nothrow) BinaryOpNode(op), true);
			break;
		case B_NOT:
			error = _PushNode(new(nothrow) UnaryOpNode(op), true);
			break;
		default:
			error = _PushNode(new(nothrow) SpecialOpNode(op), true);
			break;
	}
	return error;
}


/**
 * @brief Pushes a uint32 value onto the predicate stack.
 *
 * @param value The uint32 value to push.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushUInt32(uint32 value)
{
	return _PushNode(new(nothrow) UInt32ValueNode(value), true);
}


/**
 * @brief Pushes an int32 value onto the predicate stack.
 *
 * @param value The int32 value to push.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushInt32(int32 value)
{
	return _PushNode(new(nothrow) Int32ValueNode(value), true);
}


/**
 * @brief Pushes a uint64 value onto the predicate stack.
 *
 * @param value The uint64 value to push.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushUInt64(uint64 value)
{
	return _PushNode(new(nothrow) UInt64ValueNode(value), true);
}


/**
 * @brief Pushes an int64 value onto the predicate stack.
 *
 * @param value The int64 value to push.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushInt64(int64 value)
{
	return _PushNode(new(nothrow) Int64ValueNode(value), true);
}


/**
 * @brief Pushes a float value onto the predicate stack.
 *
 * @param value The float value to push.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushFloat(float value)
{
	return _PushNode(new(nothrow) FloatValueNode(value), true);
}


/**
 * @brief Pushes a double value onto the predicate stack.
 *
 * @param value The double value to push.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushDouble(double value)
{
	return _PushNode(new(nothrow) DoubleValueNode(value), true);
}


/**
 * @brief Pushes a string value onto the predicate stack.
 *
 * @param value The string value to push.
 * @param caseInsensitive If true, the string comparison will be case-insensitive.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushString(const char* value, bool caseInsensitive)
{
	return _PushNode(new(nothrow) StringNode(value, caseInsensitive), true);
}


/**
 * @brief Pushes a date string onto the predicate stack.
 *
 * @param date A human-readable date string parseable by parsedate().
 * @return B_OK on success, B_BAD_VALUE if the date string is NULL or unparseable,
 *         B_NO_MEMORY on allocation failure, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::PushDate(const char* date)
{
	if (date == NULL || !date[0] || parsedate(date, time(NULL)) < 0)
		return B_BAD_VALUE;

	return _PushNode(new(nothrow) DateNode(date), true);
}


/**
 * @brief Assigns a volume to the BQuery object.
 *
 * @param volume Pointer to the BVolume to query against.
 * @return B_OK on success, B_BAD_VALUE if volume is NULL, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::SetVolume(const BVolume* volume)
{
	if (volume == NULL)
		return B_BAD_VALUE;
	if (_HasFetched())
		return B_NOT_ALLOWED;

	if (volume->InitCheck() == B_OK)
		fDevice = volume->Device();
	else
		fDevice = (dev_t)B_ERROR;

	return B_OK;
}


/**
 * @brief Assigns the passed-in predicate expression string directly.
 *
 * @param expression The predicate string to assign (must not be NULL).
 * @return B_OK on success, B_BAD_VALUE if expression is NULL, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::SetPredicate(const char* expression)
{
	status_t error = (expression ? B_OK : B_BAD_VALUE);
	if (error == B_OK && _HasFetched())
		error = B_NOT_ALLOWED;
	if (error == B_OK)
		error = _SetPredicate(expression);
	return error;
}


/**
 * @brief Assigns the target messenger and makes the query live.
 *
 * @param messenger A valid BMessenger to receive live query update notifications.
 * @return B_OK on success, B_BAD_VALUE if messenger is invalid, or B_NOT_ALLOWED after Fetch().
 */
status_t
BQuery::SetTarget(BMessenger messenger)
{
	status_t error = (messenger.IsValid() ? B_OK : B_BAD_VALUE);
	if (error == B_OK && _HasFetched())
		error = B_NOT_ALLOWED;
	if (error == B_OK) {
		BMessenger::Private messengerPrivate(messenger);
		fPort = messengerPrivate.Port();
		fToken = (messengerPrivate.IsPreferredTarget()
			? -1 : messengerPrivate.Token());
	}
	return error;
}


/**
 * @brief Sets additional query flags.
 *
 * @param flags Flags to set (B_LIVE_QUERY is managed separately via SetTarget()).
 * @return B_OK on success, or B_NOT_ALLOWED if Fetch() has already been called.
 */
status_t
BQuery::SetFlags(uint32 flags)
{
	if (_HasFetched())
		return B_NOT_ALLOWED;

	fFlags = (flags & ~B_LIVE_QUERY);
	return B_OK;
}


/**
 * @brief Gets whether the query associated with this object is live.
 *
 * @return true if a valid target messenger has been set via SetTarget().
 */
bool
BQuery::IsLive() const
{
	return fPort >= 0;
}


/**
 * @brief Fills out buffer with the predicate string assigned to the BQuery object.
 *
 * @param buffer The buffer to receive the predicate string.
 * @param length The size of the buffer in bytes.
 * @return B_OK on success, B_BAD_VALUE if buffer is NULL or too small, or B_NO_INIT if
 *         no predicate has been set.
 */
status_t
BQuery::GetPredicate(char* buffer, size_t length)
{
	status_t error = (buffer ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		_EvaluateStack();
	if (error == B_OK && !fPredicate)
		error = B_NO_INIT;
	if (error == B_OK && length <= strlen(fPredicate))
		error = B_BAD_VALUE;
	if (error == B_OK)
		strcpy(buffer, fPredicate);
	return error;
}


/**
 * @brief Fills out the passed-in BString object with the predicate string.
 *
 * @param predicate Pointer to a BString to be set to the current predicate.
 * @return B_OK on success, B_BAD_VALUE if predicate is NULL, or B_NO_INIT if unset.
 */
status_t
BQuery::GetPredicate(BString* predicate)
{
	status_t error = (predicate ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		_EvaluateStack();
	if (error == B_OK && !fPredicate)
		error = B_NO_INIT;
	if (error == B_OK)
		predicate->SetTo(fPredicate);
	return error;
}


/**
 * @brief Gets the length of the predicate string including the null terminator.
 *
 * @return The number of bytes needed to store the predicate, or 0 if not set.
 */
size_t
BQuery::PredicateLength()
{
	status_t error = _EvaluateStack();
	if (error == B_OK && !fPredicate)
		error = B_NO_INIT;
	size_t size = 0;
	if (error == B_OK)
		size = strlen(fPredicate) + 1;
	return size;
}


/**
 * @brief Gets the device ID identifying the volume assigned to this BQuery object.
 *
 * @return The dev_t device identifier, or B_ERROR if no volume is set.
 */
dev_t
BQuery::TargetDevice() const
{
	return fDevice;
}


/**
 * @brief Starts fetching entries that satisfy the predicate.
 *
 * @return B_OK on success, B_NOT_ALLOWED if already fetched, B_NO_INIT if the
 *         predicate or volume is not set, or another error code on failure.
 */
status_t
BQuery::Fetch()
{
	if (_HasFetched())
		return B_NOT_ALLOWED;

	_EvaluateStack();

	if (!fPredicate || fDevice < 0)
		return B_NO_INIT;

	BString parsedPredicate;
	_ParseDates(parsedPredicate);

	fQueryFd = _kern_open_query(fDevice,
		parsedPredicate.String(), parsedPredicate.Length(),
		fFlags | ((fPort >= 0) ? B_LIVE_QUERY : 0),
		fPort, fToken);
	if (fQueryFd < 0)
		return fQueryFd;

	fcntl(fQueryFd, F_SETFD, FD_CLOEXEC);
	return B_OK;
}


//	#pragma mark - BEntryList interface


/**
 * @brief Fills out entry with the next entry, traversing symlinks if traverse is true.
 *
 * @param entry Pointer to a BEntry to be filled with the next result.
 * @param traverse If true, symbolic links are followed.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or another error code.
 */
status_t
BQuery::GetNextEntry(BEntry* entry, bool traverse)
{
	status_t error = (entry ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		entry_ref ref;
		error = GetNextRef(&ref);
		if (error == B_OK)
			error = entry->SetTo(&ref, traverse);
	}
	return error;
}


/**
 * @brief Fills out ref with the next entry as an entry_ref.
 *
 * @param ref Pointer to an entry_ref to be filled with the next result.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, B_FILE_ERROR if
 *         Fetch() has not been called, or another error code.
 */
status_t
BQuery::GetNextRef(entry_ref* ref)
{
	status_t error = (ref ? B_OK : B_BAD_VALUE);
	if (error == B_OK && !_HasFetched())
		error = B_FILE_ERROR;
	if (error == B_OK) {
		BPrivate::Storage::LongDirEntry longEntry;
		struct dirent* entry = longEntry.dirent();
		bool next = true;
		while (error == B_OK && next) {
			if (GetNextDirents(entry, sizeof(longEntry), 1) != 1) {
				error = B_ENTRY_NOT_FOUND;
			} else {
				next = (!strcmp(entry->d_name, ".")
						|| !strcmp(entry->d_name, ".."));
			}
		}
		if (error == B_OK) {
			ref->device = entry->d_pdev;
			ref->directory = entry->d_pino;
			error = ref->set_name(entry->d_name);
		}
	}
	return error;
}


/**
 * @brief Fills out up to count dirent structures from the query results.
 *
 * @param buffer Pointer to a buffer of dirent structures to fill.
 * @param length The total size of the buffer in bytes.
 * @param count The maximum number of entries to retrieve.
 * @return The number of entries written, B_BAD_VALUE if buffer is NULL,
 *         or B_FILE_ERROR if Fetch() has not been called.
 */
int32
BQuery::GetNextDirents(struct dirent* buffer, size_t length, int32 count)
{
	if (!buffer)
		return B_BAD_VALUE;
	if (!_HasFetched())
		return B_FILE_ERROR;
	return _kern_read_dir(fQueryFd, buffer, length, count);
}


/**
 * @brief Rewinds the entry list back to the first entry.
 *
 * @return B_OK on success, or B_FILE_ERROR if Fetch() has not been called.
 */
status_t
BQuery::Rewind()
{
	if (!_HasFetched())
		return B_FILE_ERROR;
	return _kern_rewind_dir(fQueryFd);
}


/**
 * @brief Unimplemented method of the BEntryList interface.
 *
 * @return Always returns B_ERROR.
 */
int32
BQuery::CountEntries()
{
	return B_ERROR;
}


/*!	Gets whether Fetch() has already been called on this object.

	\return \c true, if Fetch() was already called, \c false otherwise.
*/
/**
 * @brief Returns whether Fetch() has already been called on this object.
 *
 * @return true if Fetch() was already called, false otherwise.
 */
bool
BQuery::_HasFetched() const
{
	return fQueryFd >= 0;
}


/*!	Pushes a node onto the predicate stack.

	If the stack has not been allocate until this time, this method does
	allocate it.

	If the supplied node is \c NULL, it is assumed that there was not enough
	memory to allocate the node and thus \c B_NO_MEMORY is returned.

	In case the method fails, the caller retains the ownership of the supplied
	node and thus is responsible for deleting it, if \a deleteOnError is
	\c false. If it is \c true, the node is deleted, if an error occurs.

	\param node The node to push.
	\param deleteOnError Whether or not to delete the node if an error occurs.

	\return A status code.
	\retval B_OK Everything went fine.
	\retval B_NO_MEMORY \a node was \c NULL or there was insufficient memory to
	        allocate the predicate stack or push the node.
	\retval B_NOT_ALLOWED _PushNode() was called after Fetch().
*/
/**
 * @brief Pushes a QueryNode onto the predicate stack, allocating the stack if necessary.
 *
 * @param node The QueryNode to push; if NULL, B_NO_MEMORY is returned.
 * @param deleteOnError If true, deletes node when an error occurs.
 * @return B_OK on success, B_NO_MEMORY if node is NULL or allocation fails,
 *         or B_NOT_ALLOWED if called after Fetch().
 */
status_t
BQuery::_PushNode(QueryNode* node, bool deleteOnError)
{
	status_t error = (node ? B_OK : B_NO_MEMORY);
	if (error == B_OK && _HasFetched())
		error = B_NOT_ALLOWED;
	// allocate the stack, if necessary
	if (error == B_OK && !fStack) {
		fStack = new(nothrow) QueryStack;
		if (!fStack)
			error = B_NO_MEMORY;
	}
	if (error == B_OK)
		error = fStack->PushNode(node);
	if (error != B_OK && deleteOnError)
		delete node;
	return error;
}


/*!	Helper method to set the predicate.

	Does not check whether Fetch() has already been invoked.

	\param expression The predicate string to set.

	\return A status code.
	\retval B_OK Everything went fine.
	\retval B_NO_MEMORY There was insufficient memory to store the predicate.
*/
/**
 * @brief Internal helper that sets the predicate string without checking Fetch() state.
 *
 * @param expression The predicate string to store, or NULL to clear it.
 * @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
BQuery::_SetPredicate(const char* expression)
{
	status_t error = B_OK;
	// unset the old predicate
	delete[] fPredicate;
	fPredicate = NULL;
	// set the new one
	if (expression) {
		fPredicate = new(nothrow) char[strlen(expression) + 1];
		if (fPredicate)
			strcpy(fPredicate, expression);
		else
			error = B_NO_MEMORY;
	}
	return error;
}


/*!	Evaluates the predicate stack.

	The method does nothing (and returns \c B_OK), if the stack is \c NULL.
	If the stack is not  \c null and Fetch() has already been called, this
	method fails.

	\return A status code.
	\retval B_OK Everything went fine.
	\retval B_NO_MEMORY There was insufficient memory.
	\retval B_NOT_ALLOWED _EvaluateStack() was called after Fetch().
*/
/**
 * @brief Evaluates the predicate node stack into a predicate string.
 *
 * @return B_OK on success, B_NO_MEMORY on allocation failure,
 *         or B_NOT_ALLOWED if called after Fetch().
 */
status_t
BQuery::_EvaluateStack()
{
	status_t error = B_OK;
	if (fStack) {
		_SetPredicate(NULL);
		if (_HasFetched())
			error = B_NOT_ALLOWED;
		// convert the stack to a tree and evaluate it
		QueryNode* node = NULL;
		if (error == B_OK)
			error = fStack->ConvertToTree(node);
		BString predicate;
		if (error == B_OK)
			error = node->GetString(predicate);
		if (error == B_OK)
			error = _SetPredicate(predicate.String());
		delete fStack;
		fStack = NULL;
	}
	return error;
}


/*!	Fills out \a parsedPredicate with a parsed predicate string.

	\param parsedPredicate The predicate string to fill out.
*/
/**
 * @brief Replaces date placeholders in the predicate string with numeric timestamps.
 *
 * @param parsedPredicate BString to be filled with the date-resolved predicate.
 */
void
BQuery::_ParseDates(BString& parsedPredicate)
{
	const char* start = fPredicate;
	const char* pos = start;
	bool quotes = false;

	while (pos[0]) {
		if (pos[0] == '\\') {
			pos++;
			continue;
		}
		if (pos[0] == '"')
			quotes = !quotes;
		else if (!quotes && pos[0] == '%') {
			const char* end = strchr(pos + 1, '%');
			if (end == NULL)
				continue;

			parsedPredicate.Append(start, pos - start);
			start = end + 1;

			// We have a date string
			BString date(pos + 1, start - 1 - pos);
			parsedPredicate << parsedate(date.String(), time(NULL));

			pos = end;
		}
		pos++;
	}

	parsedPredicate.Append(start, pos - start);
}


// FBC
/**
 * @brief Reserved FBC virtual method slot 1.
 */
void BQuery::_QwertyQuery1() {}

/**
 * @brief Reserved FBC virtual method slot 2.
 */
void BQuery::_QwertyQuery2() {}

/**
 * @brief Reserved FBC virtual method slot 3.
 */
void BQuery::_QwertyQuery3() {}

/**
 * @brief Reserved FBC virtual method slot 4.
 */
void BQuery::_QwertyQuery4() {}

/**
 * @brief Reserved FBC virtual method slot 5.
 */
void BQuery::_QwertyQuery5() {}

/**
 * @brief Reserved FBC virtual method slot 6.
 */
void BQuery::_QwertyQuery6() {}
