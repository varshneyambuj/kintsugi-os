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
 * MIT License. Copyright 2003-2008, Haiku, Inc.
 * Original authors: Stefano Ceccherini (burton666@libero.it).
 */

/** @file UndoBuffer.h
    @brief Single-step undo/redo machinery for BTextView; one polymorphic
           UndoBuffer per editing operation type (typing, cut, paste, etc.). */

#ifndef __UNDOBUFFER_H
#define __UNDOBUFFER_H

#include <TextView.h>


class BClipboard;


// UndoBuffer
/**
 * @brief Base class for one BTextView edit that can be undone and redone.
 *
 * Captures the affected text range, a saved copy of the text and run array,
 * and the undo state classification. Concrete subclasses override
 * UndoSelf()/RedoSelf() to implement operation-specific reversal logic.
 */
class BTextView::UndoBuffer {
public:
								UndoBuffer(BTextView* view, undo_state state);
	virtual						~UndoBuffer();

			void				Undo(BClipboard* clipboard);
			undo_state			State(bool* _isRedo) const;

protected:
	virtual	void				UndoSelf(BClipboard* clipboard);
	virtual	void				RedoSelf(BClipboard* clipboard);

			BTextView*			fTextView;
			int32				fStart;
			int32				fEnd;

			char*				fTextData;
			int32				fTextLength;
			text_run_array*		fRunArray;
			int32				fRunArrayLength;

			bool				fRedo;

private:
			undo_state			fState;
};


// CutUndoBuffer
/**
 * @brief Undo buffer for the Cut command; redo re-cuts the saved range. */
class BTextView::CutUndoBuffer : public BTextView::UndoBuffer {
public:
								CutUndoBuffer(BTextView* textView);
	virtual						~CutUndoBuffer();

protected:
	virtual	void				RedoSelf(BClipboard* clipboard);
};


// PasteUndoBuffer
/**
 * @brief Undo buffer for the Paste command; stores the pasted payload so it
 *        can be removed on undo and reinserted on redo. */
class BTextView::PasteUndoBuffer : public BTextView::UndoBuffer {
public:
								PasteUndoBuffer(BTextView* textView,
									const char* text, int32 textLength,
									text_run_array* runArray,
									int32 runArrayLen);
	virtual						~PasteUndoBuffer();

protected:
	virtual	void				UndoSelf(BClipboard* clipboard);
	virtual	void				RedoSelf(BClipboard* clipboard);

private:
			char*				fPasteText;
			int32				fPasteTextLength;
			text_run_array*		fPasteRunArray;
};


// ClearUndoBuffer
/**
 * @brief Undo buffer for the Clear/Delete-selection command. */
class BTextView::ClearUndoBuffer : public BTextView::UndoBuffer {
public:
								ClearUndoBuffer(BTextView* textView);
	virtual						~ClearUndoBuffer();

protected:
	virtual	void				RedoSelf(BClipboard* clipboard);
};


// DropUndoBuffer
/**
 * @brief Undo buffer for drag-and-drop text insertion.
 *
 * Tracks both the dropped payload and, for internal drops, the source range
 * so undo can restore the text to its original location.
 */
class BTextView::DropUndoBuffer : public BTextView::UndoBuffer {
public:
								DropUndoBuffer(BTextView* textView,
									char const* text, int32 textLength,
									text_run_array* runArray,
									int32 runArrayLength, int32 location,
									bool internalDrop);
	virtual						~DropUndoBuffer();

protected:
	virtual	void				UndoSelf(BClipboard* clipboard);
	virtual	void				RedoSelf(BClipboard* clipboard);

private:
			char*				fDropText;
			int32				fDropTextLength;
			text_run_array*		fDropRunArray;

			int32				fDropLocation;
			bool				fInternalDrop;
};


// TypingUndoBuffer
/**
 * @brief Undo buffer that coalesces consecutive keystrokes and erasures.
 *
 * InputCharacter() extends the active typing run; BackwardErase() and
 * ForwardErase() track contiguous deletes so the user undoes a logical chunk
 * at a time rather than one character per Undo invocation.
 */
class BTextView::TypingUndoBuffer : public BTextView::UndoBuffer {
public:
								TypingUndoBuffer(BTextView* textView);
	virtual						~TypingUndoBuffer();

			void				InputCharacter(int32 length);
			void				BackwardErase();
			void				ForwardErase();

protected:
	virtual	void				RedoSelf(BClipboard* clipboard);
	virtual	void				UndoSelf(BClipboard* clipboard);

private:
			void				_Reset();

			char*				fTypedText;
			int32				fTypedStart;
			int32				fTypedEnd;
			int32				fUndone;
};

#endif //__UNDOBUFFER_H
