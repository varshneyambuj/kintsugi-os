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
 *   Copyright 2003-2006, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (burton666@libero.it)
 */


/**
 * @file InlineInput.cpp
 * @brief BTextView helper that tracks the state of an active Input Method
 *        session.
 *
 * InlineInput stores the text range inserted by an Input Server method addon,
 * the current selection within that range, and any clause markers that the
 * addon has submitted.  BTextView uses this object to render the composing
 * text with appropriate highlight feedback.
 *
 * For background, see the "The Input Server" section of the Be Book.
 *
 * @see BTextView
 */

// For a deeper understanding of this class, see the BeBook, sez.
// "The Input Server".
// TODO: the bebook says we should highlight in blue/red different "clauses".
// Though it looks like what really matters is the "selection" field in
// the BMessage sent by the input method addon. Have I missed something ?

#include "InlineInput.h"

#include <cstdlib>

/** @brief Internal storage for a single input-method clause range. */
struct clause
{
	int32 start;
	int32 end;
};


/**
 * @brief Constructs an InlineInput and associates it with the given messenger.
 *
 * @param messenger BMessenger of the Input Server method addon that owns this
 *                  session.
 */
BTextView::InlineInput::InlineInput(BMessenger messenger)
	:
	fMessenger(messenger),
	fActive(false),
	fOffset(0),
	fLength(0),
	fSelectionOffset(0),
	fSelectionLength(0),
	fNumClauses(0),
	fClauses(NULL)
{
}


/**
 * @brief Destroys the InlineInput and frees any allocated clause memory.
 */
BTextView::InlineInput::~InlineInput()
{
	ResetClauses();
}


/**
 * @brief Returns a pointer to the Input Server Method BMessenger.
 *
 * @return Pointer to the BMessenger that owns this inline-input session.
 */
const BMessenger *
BTextView::InlineInput::Method() const
{
	return &fMessenger;
}


/**
 * @brief Returns whether the inline-input session is currently active.
 *
 * @return true if the input method is composing text; false otherwise.
 */
bool
BTextView::InlineInput::IsActive() const
{
	return fActive;
}


/**
 * @brief Activates or deactivates the inline-input session.
 *
 * @param active true to mark the session as active, false to deactivate it.
 */
void
BTextView::InlineInput::SetActive(bool active)
{
	fActive = active;
}


/**
 * @brief Returns the length (in bytes) of the text being composed.
 *
 * @return Byte length of the inline-input text.
 */
int32
BTextView::InlineInput::Length() const
{
	return fLength;
}


/**
 * @brief Sets the byte length of the inline-input text.
 *
 * @param len New text length, extracted from a B_INPUT_METHOD_CHANGED message.
 */
void
BTextView::InlineInput::SetLength(int32 len)
{
	fLength = len;
}


/**
 * @brief Returns the byte offset into BTextView where the inline text begins.
 *
 * @return Insertion offset.
 */
int32
BTextView::InlineInput::Offset() const
{
	return fOffset;
}


/**
 * @brief Sets the byte offset into BTextView where the inline text begins.
 *
 * @param offset Offset at which the composed text has been inserted.
 */
void
BTextView::InlineInput::SetOffset(int32 offset)
{
	fOffset = offset;
}


/**
 * @brief Returns the byte length of the current selection within the inline text.
 *
 * @return Selection length in bytes.
 */
int32
BTextView::InlineInput::SelectionLength() const
{
	return fSelectionLength;
}


/**
 * @brief Sets the byte length of the current selection.
 *
 * @param length New selection length in bytes.
 */
void
BTextView::InlineInput::SetSelectionLength(int32 length)
{
	fSelectionLength = length;
}


/**
 * @brief Returns the byte offset of the selection within the inline-input string.
 *
 * @return Selection start offset (relative to the start of the inline text).
 */
int32
BTextView::InlineInput::SelectionOffset() const
{
	return fSelectionOffset;
}


/**
 * @brief Sets the byte offset of the selection within the inline-input string.
 *
 * @param offset Offset where the selection starts, relative to the inline text.
 */
void
BTextView::InlineInput::SetSelectionOffset(int32 offset)
{
	fSelectionOffset = offset;
}


/**
 * @brief Appends a clause range to the clause list.
 *
 * Clauses partition the inline-input text into segments that may be
 * rendered with different highlight colours (see the Input Server docs).
 *
 * @param start Byte offset into the inline-input string where the clause starts.
 * @param end   Byte offset where the clause ends.
 * @return true on success, false if memory allocation failed.
 */
bool
BTextView::InlineInput::AddClause(int32 start, int32 end)
{
	void *newData = realloc(fClauses, (fNumClauses + 1) * sizeof(clause));
	if (newData == NULL)
		return false;

	fClauses = (clause *)newData;
	fClauses[fNumClauses].start = start;
	fClauses[fNumClauses].end = end;
	fNumClauses++;
	return true;
}


/**
 * @brief Retrieves the clause at a given index.
 *
 * @param index Index of the clause (0-based).
 * @param start Output: byte offset where the clause starts; may be NULL.
 * @param end   Output: byte offset where the clause ends; may be NULL.
 * @return true if the index is valid, false if it is out of range.
 */
bool
BTextView::InlineInput::GetClause(int32 index, int32 *start, int32 *end) const
{
	bool result = false;
	if (index >= 0 && index < fNumClauses) {
		result = true;
		clause *clause = &fClauses[index];
		if (start)
			*start = clause->start;
		if (end)
			*end = clause->end;
	}

	return result;
}


/**
 * @brief Returns the number of clauses currently registered.
 *
 * @return Clause count.
 */
int32
BTextView::InlineInput::CountClauses() const
{
	return fNumClauses;
}


/**
 * @brief Removes all clauses and frees the associated memory.
 */
void
BTextView::InlineInput::ResetClauses()
{
	fNumClauses = 0;
	free(fClauses);
	fClauses = NULL;
}
