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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2020 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Frans van Nispen (xlr8@tref.nl)
 *       Marc Flerackers (mflerackers@androme.be)
 *       John Scipione (jscipione@gmail.com)
 */


/**
 * @file TextInput.cpp
 * @brief Implementation of BTextInput, the inner text view of BTextControl
 *
 * BTextInput is a private subclass of BTextView used inside BTextControl. It
 * restricts editing to a single line, handles Tab/Enter focus navigation, and
 * notifies BTextControl on content changes.
 *
 * @see BTextControl, BTextView
 */


#include "TextInput.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <InterfaceDefs.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <String.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>


namespace BPrivate {


/**
 * @brief Construct a _BTextInput_ with an explicit frame and text rectangle.
 *
 * Initialises the underlying BTextView, sets @c fPreviousText to NULL, and
 * enables auto-resizing so the field grows with its content.
 *
 * @param frame       The position and size of the view in its parent's
 *                    coordinate system.
 * @param textRect    The rectangle within @a frame in which text is drawn.
 * @param resizeMask  Resizing flags forwarded to BView.
 * @param flags       View flags forwarded to BView.
 * @see BTextView::BTextView()
 */
_BTextInput_::_BTextInput_(BRect frame, BRect textRect, uint32 resizeMask,
	uint32 flags)
	:
	BTextView(frame, "_input_", textRect, resizeMask, flags),
	fPreviousText(NULL),
	fInMouseDown(false)
{
	MakeResizable(true);
}


/**
 * @brief Unarchive constructor — reconstruct a _BTextInput_ from a BMessage.
 *
 * @param archive The archive message previously produced by Archive().
 * @see Archive()
 * @see Instantiate()
 */
_BTextInput_::_BTextInput_(BMessage* archive)
	:
	BTextView(archive),
	fPreviousText(NULL),
	fInMouseDown(false)
{
	MakeResizable(true);
}


/**
 * @brief Destroy the _BTextInput_ and release the saved previous-text buffer.
 */
_BTextInput_::~_BTextInput_()
{
	free(fPreviousText);
}


/**
 * @brief Create a new _BTextInput_ from an archive message.
 *
 * @param archive The BMessage produced by Archive().
 * @return A newly allocated _BTextInput_, or NULL if @a archive does not
 *         represent a valid _BTextInput_ instance.
 * @see Archive()
 */
BArchivable*
_BTextInput_::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "_BTextInput_"))
		return new _BTextInput_(archive);

	return NULL;
}


/**
 * @brief Archive this _BTextInput_ into a BMessage.
 *
 * Delegates directly to BTextView::Archive() with @c deep set to true so
 * that the full text content and run-array are preserved.
 *
 * @param data The message to archive into.
 * @param deep Ignored; archiving is always deep.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
_BTextInput_::Archive(BMessage* data, bool deep) const
{
	return BTextView::Archive(data, true);
}


/**
 * @brief Record that a mouse-down event is in progress, then forward it.
 *
 * Sets @c fInMouseDown so that MakeFocus() knows not to select all text
 * when focus is gained via a click (allowing the user to position the
 * cursor by clicking).
 *
 * @param where The click position in the view's coordinate system.
 * @see MakeFocus()
 */
void
_BTextInput_::MouseDown(BPoint where)
{
	fInMouseDown = true;
	BTextView::MouseDown(where);
	fInMouseDown = false;
}


/**
 * @brief Handle a view resize event.
 *
 * Forwards the resize notification to BTextView so that internal text
 * layout is recalculated to match the new dimensions.
 *
 * @param width  The new width of the view in pixels.
 * @param height The new height of the view in pixels.
 */
void
_BTextInput_::FrameResized(float width, float height)
{
	BTextView::FrameResized(width, height);
}


/**
 * @brief Process a key-down event, enforcing single-line behaviour.
 *
 * - B_ENTER: invokes the parent BTextControl if the text has changed since
 *   the last commit, updates @c fPreviousText, and selects all text.
 * - B_TAB: passes the key to BView (not BTextView) so that focus moves
 *   to the next control rather than inserting a tab character.
 * - All other keys: forwarded to BTextView for normal editing.
 *
 * @param bytes    Pointer to the UTF-8 byte sequence for the key.
 * @param numBytes Number of bytes in the sequence.
 * @see MakeFocus()
 * @see BTextControl::Invoke()
 */
void
_BTextInput_::KeyDown(const char* bytes, int32 numBytes)
{
	switch (*bytes) {
		case B_ENTER:
		{
			if (!TextControl()->IsEnabled())
				break;

			if (fPreviousText == NULL || strcmp(Text(), fPreviousText) != 0) {
				TextControl()->Invoke();
				free(fPreviousText);
				fPreviousText = strdup(Text());
			}

			SelectAll();
			break;
		}

		case B_TAB:
			BView::KeyDown(bytes, numBytes);
			break;

		default:
			BTextView::KeyDown(bytes, numBytes);
			break;
	}
}


/**
 * @brief Grant or remove input focus, managing text selection and change
 *        notification.
 *
 * When focus is gained, the current text is saved via SetInitialText() as
 * the baseline for change detection.  If the gain was not triggered by a
 * mouse click, all text is selected.  When focus is lost, the parent
 * BTextControl is invoked if the text has changed, and the saved baseline
 * is freed.  The parent view is always invalidated so the focus indicator
 * border is redrawn.
 *
 * @param state true to grant focus, false to remove it.
 * @see SetInitialText()
 * @see BTextControl::Invoke()
 */
void
_BTextInput_::MakeFocus(bool state)
{
	if (state == IsFocus())
		return;

	BTextView::MakeFocus(state);

	if (state) {
		SetInitialText();
		if (!fInMouseDown)
			SelectAll();
	} else {
		if (strcmp(Text(), fPreviousText) != 0)
			TextControl()->Invoke();

		free(fPreviousText);
		fPreviousText = NULL;
	}

	if (Window() != NULL) {
		// Invalidate parent to draw or remove the focus mark
		if (BTextControl* parent = dynamic_cast<BTextControl*>(Parent())) {
			BRect frame = Frame();
			frame.InsetBy(-1.0, -1.0);
			parent->Invalidate(frame);
		}
	}
}


/**
 * @brief Return the minimum size required to display one line of text.
 *
 * The minimum height is one line height plus two pixels of vertical inset.
 * The minimum width is three times the height, providing a reasonable
 * lower bound while still allowing the explicit minimum size to override.
 *
 * @return The minimum BSize, composed with any explicit minimum size.
 */
BSize
_BTextInput_::MinSize()
{
	BSize min;
	min.height = ceilf(LineHeight(0) + 2.0);
	// we always add at least one pixel vertical inset top/bottom for
	// the text rect.
	min.width = min.height * 3;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), min);
}


/**
 * @brief Snapshot the current text as the baseline for change detection.
 *
 * Called when the view gains focus so that a subsequent loss of focus (or
 * B_ENTER key) can determine whether the text was actually modified.
 *
 * @see MakeFocus()
 * @see KeyDown()
 */
void
_BTextInput_::SetInitialText()
{
	free(fPreviousText);
	fPreviousText = NULL;

	if (Text() != NULL)
		fPreviousText = strdup(Text());
}


/**
 * @brief Paste clipboard contents and immediately invalidate the view.
 *
 * Delegates to BTextView::Paste() and then forces a redraw to keep the
 * display consistent, particularly for styled text.
 *
 * @param clipboard The clipboard from which to paste.
 */
void
_BTextInput_::Paste(BClipboard* clipboard)
{
	BTextView::Paste(clipboard);
	Invalidate();
}


/**
 * @brief Insert text, stripping embedded newline and carriage-return
 *        characters to enforce single-line behaviour.
 *
 * Single-character newlines are replaced with a space character directly.
 * For multi-character insertions a temporary BString is used to batch-replace
 * both '\\n' and '\\r' before forwarding to BTextView.  After the insertion
 * the parent BTextControl's modification message is sent.
 *
 * @param inText   The UTF-8 text to insert (not NUL-terminated).
 * @param inLength The number of bytes to insert.
 * @param inOffset The byte offset in the text buffer at which to insert.
 * @param inRuns   Optional text-run style array, or NULL for the current style.
 * @see DeleteText()
 * @see BTextControl::InvokeNotify()
 */
void
_BTextInput_::InsertText(const char* inText, int32 inLength,
	int32 inOffset, const text_run_array* inRuns)
{
	// Filter all line breaks, note that inText is not terminated.
	if (inLength == 1) {
		if (*inText == '\n' || *inText == '\r')
			BTextView::InsertText(" ", 1, inOffset, inRuns);
		else
			BTextView::InsertText(inText, 1, inOffset, inRuns);
	} else {
		BString filteredText(inText, inLength);
		filteredText.ReplaceAll('\n', ' ');
		filteredText.ReplaceAll('\r', ' ');
		BTextView::InsertText(filteredText.String(), inLength, inOffset,
			inRuns);
	}

	TextControl()->InvokeNotify(TextControl()->ModificationMessage(),
		B_CONTROL_MODIFIED);
}


/**
 * @brief Delete a range of text and notify the parent of the modification.
 *
 * Forwards the deletion to BTextView, then sends the parent BTextControl's
 * modification message so that live-validation callbacks are triggered.
 *
 * @param fromOffset Byte offset of the first character to remove.
 * @param toOffset   Byte offset one past the last character to remove.
 * @see InsertText()
 * @see BTextControl::InvokeNotify()
 */
void
_BTextInput_::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);

	TextControl()->InvokeNotify(TextControl()->ModificationMessage(),
		B_CONTROL_MODIFIED);
}


/**
 * @brief Return the BTextControl that owns this input view.
 *
 * Performs a dynamic cast on Parent().  Calls debugger() if the parent is
 * not a BTextControl, as this indicates an illegal use of the class.
 *
 * @return The owning BTextControl.
 * @note This method will halt the program via debugger() if the parent is
 *       not a BTextControl.
 */
BTextControl*
_BTextInput_::TextControl()
{
	BTextControl* textControl = NULL;
	if (Parent() != NULL)
		textControl = dynamic_cast<BTextControl*>(Parent());

	if (textControl == NULL)
		debugger("_BTextInput_ should have a BTextControl as parent");

	return textControl;
}


}	// namespace BPrivate
