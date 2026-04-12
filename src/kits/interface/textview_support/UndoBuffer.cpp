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
 *   Copyright 2003-2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (burton666@libero.it)
 */

//!	UndoBuffer and its subclasses handle different types of Undo operations.


/**
 * @file UndoBuffer.cpp
 * @brief Undo/redo command objects for BTextView editing operations.
 *
 * UndoBuffer is the abstract base that snapshots the text selection and
 * optional run array at the time of an edit.  Concrete subclasses handle
 * Cut, Paste, Clear, Drop, and Typing events.  Each subclass overrides
 * UndoSelf() and/or RedoSelf() to replay or reverse the specific operation.
 *
 * @see BTextView
 */


#include "UndoBuffer.h"
#include "utf8_functions.h"

#include <Clipboard.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// TODO: properly document this file


//	#pragma mark - UndoBuffer


/**
 * @brief Constructs an UndoBuffer by snapshotting the current selection.
 *
 * Captures the selected text and, if the view is stylable, the run array
 * for the selection.
 *
 * @param textView The BTextView being edited.
 * @param state    The undo-state category for this buffer (B_UNDO_CUT, etc.).
 */
BTextView::UndoBuffer::UndoBuffer(BTextView* textView, undo_state state)
	:
	fTextView(textView),
	fTextData(NULL),
	fRunArray(NULL),
	fRunArrayLength(0),
	fRedo(false),
	fState(state)
{
	fTextView->GetSelection(&fStart, &fEnd);
	fTextLength = fEnd - fStart;

	fTextData = (char*)malloc(fTextLength);
	memcpy(fTextData, fTextView->Text() + fStart, fTextLength);

	if (fTextView->IsStylable())
		fRunArray = fTextView->RunArray(fStart, fEnd, &fRunArrayLength);
}


/**
 * @brief Destroys the UndoBuffer and frees the saved text and run array.
 */
BTextView::UndoBuffer::~UndoBuffer()
{
	free(fTextData);
	BTextView::FreeRunArray(fRunArray);
}


/**
 * @brief Performs an undo or redo operation depending on the current state.
 *
 * Delegates to RedoSelf() when in redo mode, UndoSelf() otherwise, and
 * then flips the redo flag.
 *
 * @param clipboard System clipboard, passed to the concrete implementation.
 */
void
BTextView::UndoBuffer::Undo(BClipboard* clipboard)
{
	fRedo ? RedoSelf(clipboard) : UndoSelf(clipboard);

	fRedo = !fRedo;
}


/**
 * @brief Returns the undo-state category and whether a redo is pending.
 *
 * @param _isRedo Output: set to true if the next Undo() call will redo.
 * @return The undo_state value passed to the constructor.
 */
undo_state
BTextView::UndoBuffer::State(bool* _isRedo) const
{
	*_isRedo = fRedo;

	return fState;
}


/**
 * @brief Re-inserts the snapshotted text at its original position.
 *
 * @param clipboard Unused by the base implementation.
 */
void
BTextView::UndoBuffer::UndoSelf(BClipboard* clipboard)
{
	fTextView->Select(fStart, fStart);
	fTextView->Insert(fTextData, fTextLength, fRunArray);
	fTextView->Select(fStart, fStart);
}


/**
 * @brief Default redo implementation; does nothing in the base class.
 *
 * @param clipboard Unused.
 */
void
BTextView::UndoBuffer::RedoSelf(BClipboard* clipboard)
{
}


//	#pragma mark - CutUndoBuffer


/**
 * @brief Constructs a CutUndoBuffer for a B_UNDO_CUT operation.
 *
 * @param textView The BTextView from which text was cut.
 */
BTextView::CutUndoBuffer::CutUndoBuffer(BTextView* textView)
	: BTextView::UndoBuffer(textView, B_UNDO_CUT)
{
}


/**
 * @brief Destroys the CutUndoBuffer.
 */
BTextView::CutUndoBuffer::~CutUndoBuffer()
{
}


/**
 * @brief Re-cuts the original selection and puts the text back on the clipboard.
 *
 * @param clipboard System clipboard to receive the re-cut text.
 */
void
BTextView::CutUndoBuffer::RedoSelf(BClipboard* clipboard)
{
	BMessage* clip = NULL;

	fTextView->Select(fStart, fStart);
	fTextView->Delete(fStart, fEnd);
	if (clipboard->Lock()) {
		clipboard->Clear();
		if ((clip = clipboard->Data())) {
			clip->AddData("text/plain", B_MIME_TYPE, fTextData, fTextLength);
			if (fRunArray)
				clip->AddData("application/x-vnd.Be-text_run_array",
					B_MIME_TYPE, fRunArray, fRunArrayLength);
			clipboard->Commit();
		}
		clipboard->Unlock();
	}
}


//	#pragma mark - PasteUndoBuffer


/**
 * @brief Constructs a PasteUndoBuffer recording the pasted text and styles.
 *
 * @param textView     The BTextView into which text was pasted.
 * @param text         The pasted text bytes.
 * @param textLen      Byte length of @p text.
 * @param runArray     Run array for the pasted text; may be NULL.
 * @param runArrayLen  Byte length of @p runArray.
 */
BTextView::PasteUndoBuffer::PasteUndoBuffer(BTextView* textView,
		const char* text, int32 textLen, text_run_array* runArray,
		int32 runArrayLen)
	: BTextView::UndoBuffer(textView, B_UNDO_PASTE),
	fPasteText(NULL),
	fPasteTextLength(textLen),
	fPasteRunArray(NULL)
{
	fPasteText = (char*)malloc(fPasteTextLength);
	memcpy(fPasteText, text, fPasteTextLength);

	if (runArray)
		fPasteRunArray = BTextView::CopyRunArray(runArray);
}


/**
 * @brief Destroys the PasteUndoBuffer and frees paste text and run array.
 */
BTextView::PasteUndoBuffer::~PasteUndoBuffer()
{
	free(fPasteText);
	BTextView::FreeRunArray(fPasteRunArray);
}


/**
 * @brief Removes the pasted text and re-inserts the original selection.
 *
 * @param clipboard Unused.
 */
void
BTextView::PasteUndoBuffer::UndoSelf(BClipboard* clipboard)
{
	fTextView->Select(fStart, fStart);
	fTextView->Delete(fStart, fStart + fPasteTextLength);
	fTextView->Insert(fTextData, fTextLength, fRunArray);
	fTextView->Select(fStart, fEnd);
}


/**
 * @brief Re-applies the paste operation.
 *
 * @param clipboard Unused.
 */
void
BTextView::PasteUndoBuffer::RedoSelf(BClipboard* clipboard)
{
	fTextView->Select(fStart, fStart);
	fTextView->Delete(fStart, fEnd);
	fTextView->Insert(fPasteText, fPasteTextLength, fPasteRunArray);
	fTextView->Select(fStart + fPasteTextLength, fStart + fPasteTextLength);
}


//	#pragma mark - ClearUndoBuffer


/**
 * @brief Constructs a ClearUndoBuffer for a B_UNDO_CLEAR operation.
 *
 * @param textView The BTextView from which text was cleared.
 */
BTextView::ClearUndoBuffer::ClearUndoBuffer(BTextView* textView)
	: BTextView::UndoBuffer(textView, B_UNDO_CLEAR)
{
}


/**
 * @brief Destroys the ClearUndoBuffer.
 */
BTextView::ClearUndoBuffer::~ClearUndoBuffer()
{
}


/**
 * @brief Re-clears the original selection.
 *
 * @param clipboard Unused.
 */
void
BTextView::ClearUndoBuffer::RedoSelf(BClipboard* clipboard)
{
	fTextView->Select(fStart, fStart);
	fTextView->Delete(fStart, fEnd);
}


//	#pragma mark - DropUndoBuffer


/**
 * @brief Constructs a DropUndoBuffer recording a drag-and-drop text insertion.
 *
 * For internal drops the drop location is adjusted to account for the text
 * that was removed from before the destination.
 *
 * @param textView      The BTextView that received the drop.
 * @param text          The dropped text bytes.
 * @param textLen       Byte length of @p text.
 * @param runArray      Run array for the dropped text; may be NULL.
 * @param runArrayLen   Byte length of @p runArray.
 * @param location      Byte offset where the drop was inserted.
 * @param internalDrop  true if the drag originated from the same BTextView.
 */
BTextView::DropUndoBuffer::DropUndoBuffer(BTextView* textView,
		char const* text, int32 textLen, text_run_array* runArray,
		int32 runArrayLen, int32 location, bool internalDrop)
	: BTextView::UndoBuffer(textView, B_UNDO_DROP),
	fDropText(NULL),
	fDropTextLength(textLen),
	fDropRunArray(NULL)
{
	fInternalDrop = internalDrop;
	fDropLocation = location;

	fDropText = (char*)malloc(fDropTextLength);
	memcpy(fDropText, text, fDropTextLength);

	if (runArray)
		fDropRunArray = BTextView::CopyRunArray(runArray);

	if (fInternalDrop && fDropLocation >= fEnd)
		fDropLocation -= fDropTextLength;
}


/**
 * @brief Destroys the DropUndoBuffer and frees drop text and run array.
 */
BTextView::DropUndoBuffer::~DropUndoBuffer()
{
	free(fDropText);
	BTextView::FreeRunArray(fDropRunArray);
}


/**
 * @brief Reverses the drop: removes the dropped text and restores an internal
 *        drag source.
 *
 * @param clipboard Unused.
 */
void
BTextView::DropUndoBuffer::UndoSelf(BClipboard* )
{
	fTextView->Select(fDropLocation, fDropLocation);
	fTextView->Delete(fDropLocation, fDropLocation + fDropTextLength);
	if (fInternalDrop) {
		fTextView->Select(fStart, fStart);
		fTextView->Insert(fTextData, fTextLength, fRunArray);
	}
	fTextView->Select(fStart, fEnd);
}


/**
 * @brief Re-applies the drop operation.
 *
 * @param clipboard Unused.
 */
void
BTextView::DropUndoBuffer::RedoSelf(BClipboard* )
{
	if (fInternalDrop) {
		fTextView->Select(fStart, fStart);
		fTextView->Delete(fStart, fEnd);
	}
	fTextView->Select(fDropLocation, fDropLocation);
	fTextView->Insert(fDropText, fDropTextLength, fDropRunArray);
	fTextView->Select(fDropLocation, fDropLocation + fDropTextLength);
}


//	#pragma mark - TypingUndoBuffer


/**
 * @brief Constructs a TypingUndoBuffer to track an ongoing typing session.
 *
 * @param textView The BTextView receiving keystrokes.
 */
BTextView::TypingUndoBuffer::TypingUndoBuffer(BTextView* textView)
	: BTextView::UndoBuffer(textView, B_UNDO_TYPING),
	fTypedText(NULL),
	fTypedStart(fStart),
	fTypedEnd(fEnd),
	fUndone(0)
{
}


/**
 * @brief Destroys the TypingUndoBuffer and frees the typed-text snapshot.
 */
BTextView::TypingUndoBuffer::~TypingUndoBuffer()
{
	free(fTypedText);
}


/**
 * @brief Reverts the typed text: removes typed characters and restores the
 *        pre-typing selection.
 *
 * Snapshots the currently typed region before reverting so that RedoSelf()
 * can re-apply it.
 *
 * @param clipboard Unused.
 */
void
BTextView::TypingUndoBuffer::UndoSelf(BClipboard* clipboard)
{
	int32 len = fTypedEnd - fTypedStart;

	free(fTypedText);
	fTypedText = (char*)malloc(len);
	memcpy(fTypedText, fTextView->Text() + fTypedStart, len);

	fTextView->Select(fTypedStart, fTypedStart);
	fTextView->Delete(fTypedStart, fTypedEnd);
	fTextView->Insert(fTextData, fTextLength);
	fTextView->Select(fStart, fEnd);
	fUndone++;
}


/**
 * @brief Re-applies the typed text that was previously undone.
 *
 * @param clipboard Unused.
 */
void
BTextView::TypingUndoBuffer::RedoSelf(BClipboard* clipboard)
{
	fTextView->Select(fTypedStart, fTypedStart);
	fTextView->Delete(fTypedStart, fTypedStart + fTextLength);
	fTextView->Insert(fTypedText, fTypedEnd - fTypedStart);
	fUndone--;
}


/**
 * @brief Records that @p len bytes were typed at the current insertion point.
 *
 * If the insertion point has moved since the last keystroke, the buffer
 * is reset to start a fresh typing session.
 *
 * @param len Number of bytes just typed.
 */
void
BTextView::TypingUndoBuffer::InputCharacter(int32 len)
{
	int32 start, end;
	fTextView->GetSelection(&start, &end);

	if (start != fTypedEnd || end != fTypedEnd)
		_Reset();

	fTypedEnd += len;
}


/**
 * @brief Resets the typing buffer to snapshot the current selection.
 *
 * Called when the insertion point jumps, forcing a new undo transaction.
 */
void
BTextView::TypingUndoBuffer::_Reset()
{
	free(fTextData);
	fTextView->GetSelection(&fStart, &fEnd);
	fTextLength = fEnd - fStart;
	fTypedStart = fStart;
	fTypedEnd = fStart;

	fTextData = (char*)malloc(fTextLength);
	memcpy(fTextData, fTextView->Text() + fStart, fTextLength);

	free(fTypedText);
	fTypedText = NULL;
	fRedo = false;
	fUndone = 0;
}


/**
 * @brief Records a backward-erase (backspace) keystroke.
 *
 * Extends the saved pre-typing text to include the character about to be
 * erased, and adjusts the tracked start position.
 */
void
BTextView::TypingUndoBuffer::BackwardErase()
{
	int32 start, end;
	fTextView->GetSelection(&start, &end);

	const char* text = fTextView->Text();
	int32 charLen = UTF8PreviousCharLen(text + start, text);

	if (start != fTypedEnd || end != fTypedEnd) {
		_Reset();
		// if we've got a selection, we're already done
		if (start != end)
			return;
	}

	char* buffer = (char*)malloc(fTextLength + charLen);
	memcpy(buffer + charLen, fTextData, fTextLength);

	fTypedStart = start - charLen;
	start = fTypedStart;
	for (int32 x = 0; x < charLen; x++)
		buffer[x] = fTextView->ByteAt(start + x);
	free(fTextData);
	fTextData = buffer;

	fTextLength += charLen;
	fTypedEnd -= charLen;
}


/**
 * @brief Records a forward-erase (Delete key) keystroke.
 *
 * Extends the saved text to include the character about to be erased at the
 * current position.
 */
void
BTextView::TypingUndoBuffer::ForwardErase()
{
	// TODO: Cleanup
	int32 start, end;

	fTextView->GetSelection(&start, &end);

	int32 charLen = UTF8NextCharLen(fTextView->Text() + start);

	if (start != fTypedEnd || end != fTypedEnd || fUndone > 0) {
		_Reset();
		// if we've got a selection, we're already done
		if (fStart == fEnd) {
			free(fTextData);
			fTextLength = charLen;
			fTextData = (char*)malloc(fTextLength);

			// store the erased character
			for (int32 x = 0; x < charLen; x++)
				fTextData[x] = fTextView->ByteAt(start + x);
		}
	} else {
		// Here we need to store the erased text, so we get the text that it's
		// already in the buffer, and we add the erased character.
		// a realloc + memmove would maybe be cleaner, but that way we spare a
		// copy (malloc + memcpy vs realloc + memmove).

		int32 newLength = fTextLength + charLen;
		char* buffer = (char*)malloc(newLength);

		// copy the already stored data
		memcpy(buffer, fTextData, fTextLength);

		if (fTextLength < newLength) {
			// store the erased character
			for (int32 x = 0; x < charLen; x++)
				buffer[fTextLength + x] = fTextView->ByteAt(start + x);
		}

		fTextLength = newLength;
		free(fTextData);
		fTextData = buffer;
	}
}
