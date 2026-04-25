/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2003-2006, Haiku, Inc.
 * Original authors: Stefano Ceccherini (burton666@libero.it).
 */

/** @file InlineInput.h
    @brief Tracks the inline composition state used by an input method (IME)
           while preedit text is being assembled inside a BTextView. */

#ifndef __INLINEINPUT_H
#define __INLINEINPUT_H

#include <Messenger.h>
#include <TextView.h>

struct clause;

/**
 * @brief Per-BTextView record of an active inline input session.
 *
 * Holds a messenger to the input method add-on, the offset and length of the
 * preedit text inside the view, the current selection within that preedit
 * region, and a list of "clauses" (logical sub-segments) used by some input
 * methods to subdivide the preedit string.
 */
class BTextView::InlineInput {
public:
	InlineInput(BMessenger);
	~InlineInput();

	const BMessenger *Method() const;

	bool IsActive() const;
	void SetActive(bool active);

	int32 Length() const;
	void SetLength(int32 length);

	int32 Offset() const;
	void SetOffset(int32 offset);

	int32 SelectionLength() const;
	void SetSelectionLength(int32);

	int32 SelectionOffset() const;
	void SetSelectionOffset(int32 offset);

	bool AddClause(int32, int32);
	bool GetClause(int32 index, int32 *start, int32 *end) const;
	int32 CountClauses() const;

	void ResetClauses();

private:
	const BMessenger fMessenger;

	bool fActive;

	int32 fOffset;
	int32 fLength;

	int32 fSelectionOffset;
	int32 fSelectionLength;

	int32 fNumClauses;
	clause *fClauses;
};

#endif //__INLINEINPUT_H
