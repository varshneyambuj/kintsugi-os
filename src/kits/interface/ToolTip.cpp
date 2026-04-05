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
 *   Copyright 2009 Axel Dörfler. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file ToolTip.cpp
 * @brief Implementation of BToolTip and BTextToolTip for tooltip display
 *
 * BToolTip is the abstract base class for tooltip objects. BTextToolTip
 * provides a concrete text-only tooltip. Tooltips are managed by the
 * BToolTipManager singleton and shown in a floating tooltip window.
 *
 * @see BToolTipManager, BView
 */


#include <ToolTip.h>

#include <new>

#include <ControlLook.h>
#include <Message.h>
#include <TextView.h>
#include <ToolTipManager.h>


/**
 * @brief Construct a BToolTip with default settings.
 *
 * Initialises the sticky flag to false, the mouse-relative location to a
 * system-defined spacing offset, and the alignment to bottom-right of the
 * cursor position.
 */
BToolTip::BToolTip()
{
	_InitData();
}


/**
 * @brief Unarchiving constructor; restores a BToolTip from a BMessage.
 *
 * Reads the "sticky" boolean from @a archive if present. Additional fields
 * are not yet implemented.
 *
 * @param archive  The archive message produced by Archive().
 * @see   Archive()
 */
BToolTip::BToolTip(BMessage* archive)
{
	_InitData();

	bool sticky;
	if (archive->FindBool("sticky", &sticky) == B_OK)
		fIsSticky = sticky;

	// TODO!
}


/**
 * @brief Destroy the BToolTip.
 *
 * Subclasses that own a BView (returned by View()) must delete it in their
 * own destructor; BToolTip's destructor does not delete it.
 */
BToolTip::~BToolTip()
{
}


/**
 * @brief Archive the tooltip's settings into a BMessage.
 *
 * Currently archives the "sticky" flag when it is true. Other fields are
 * not yet implemented.
 *
 * @param archive  The message to archive into.
 * @param deep     If true, child objects are archived recursively (currently unused).
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK On success.
 * @see   BArchivable::Archive()
 */
status_t
BToolTip::Archive(BMessage* archive, bool deep) const
{
	status_t status = BArchivable::Archive(archive, deep);

	if (fIsSticky)
		status = archive->AddBool("sticky", fIsSticky);

	// TODO!
	return status;
}


/**
 * @brief Set whether the tooltip stays visible when the mouse moves.
 *
 * A sticky tooltip is repositioned relative to the cursor rather than being
 * hidden when the mouse moves over the tooltip window.
 *
 * @param enable  true to make the tooltip sticky, false for normal behaviour.
 * @see   IsSticky()
 */
void
BToolTip::SetSticky(bool enable)
{
	fIsSticky = enable;
}


/**
 * @brief Return whether the tooltip is sticky.
 *
 * @return true if the tooltip stays visible when the mouse moves over it.
 * @see   SetSticky()
 */
bool
BToolTip::IsSticky() const
{
	return fIsSticky;
}


/**
 * @brief Set the tooltip window's offset relative to the mouse cursor.
 *
 * The location is added to the cursor position when computing where to place
 * the tooltip window on screen.
 *
 * @param location  The pixel offset from the cursor, in screen coordinates.
 * @see   MouseRelativeLocation()
 */
void
BToolTip::SetMouseRelativeLocation(BPoint location)
{
	fRelativeLocation = location;
}


/**
 * @brief Return the tooltip window's offset relative to the mouse cursor.
 *
 * @return The BPoint offset from the cursor position used when placing the
 *         tooltip window.
 * @see   SetMouseRelativeLocation()
 */
BPoint
BToolTip::MouseRelativeLocation() const
{
	return fRelativeLocation;
}


/**
 * @brief Set the preferred alignment of the tooltip window relative to the cursor.
 *
 * The tooltip manager uses this alignment as a first preference when positioning
 * the window, falling back to alternative positions if the window would be
 * clipped by the screen frame.
 *
 * @param alignment  The desired BAlignment (horizontal and vertical components).
 * @see   Alignment()
 */
void
BToolTip::SetAlignment(BAlignment alignment)
{
	fAlignment = alignment;
}


/**
 * @brief Return the preferred alignment of the tooltip window relative to the cursor.
 *
 * @return The BAlignment used by the tooltip manager as the first placement preference.
 * @see   SetAlignment()
 */
BAlignment
BToolTip::Alignment() const
{
	return fAlignment;
}


/**
 * @brief Called when the tooltip's view has been attached to a window.
 *
 * Subclasses may override to perform initialisation that requires a valid
 * window context. The default implementation does nothing.
 *
 * @see   DetachedFromWindow()
 */
void
BToolTip::AttachedToWindow()
{
}


/**
 * @brief Called when the tooltip's view has been detached from its window.
 *
 * Subclasses may override to release window-specific resources. The default
 * implementation does nothing.
 *
 * @see   AttachedToWindow()
 */
void
BToolTip::DetachedFromWindow()
{
}


/**
 * @brief Acquire an exclusive lock on the tooltip's view or the tooltip manager.
 *
 * If the tooltip's view is attached to a window, locks the window's looper.
 * If the view is not yet attached, locks the BToolTipManager instead. Retries
 * until a consistent lock is obtained to avoid a race between the two cases.
 *
 * @return Always returns true; the function does not fail.
 * @see   Unlock()
 */
bool
BToolTip::Lock()
{
	bool lockedLooper;
	while (true) {
		lockedLooper = View()->LockLooper();
		if (!lockedLooper) {
			BToolTipManager* manager = BToolTipManager::Manager();
			manager->Lock();

			if (View()->Window() != NULL) {
				manager->Unlock();
				continue;
			}
		}
		break;
	}

	fLockedLooper = lockedLooper;
	return true;
}


/**
 * @brief Release the lock acquired by Lock().
 *
 * Unlocks either the view's window looper or the BToolTipManager, depending
 * on which one was locked by the corresponding Lock() call.
 *
 * @see   Lock()
 */
void
BToolTip::Unlock()
{
	if (fLockedLooper)
		View()->UnlockLooper();
	else
		BToolTipManager::Manager()->Unlock();
}


/**
 * @brief Initialise member variables to their default values.
 *
 * Sets the sticky flag to false, the mouse-relative location to a
 * system-defined big-spacing offset, and the alignment to
 * (B_ALIGN_RIGHT, B_ALIGN_BOTTOM).
 */
void
BToolTip::_InitData()
{
	float spacing = be_control_look->ComposeSpacing(B_USE_BIG_SPACING);

	fIsSticky = false;
	fRelativeLocation = BPoint(spacing, spacing);
	fAlignment = BAlignment(B_ALIGN_RIGHT, B_ALIGN_BOTTOM);
}


//	#pragma mark -


/**
 * @brief Construct a BTextToolTip displaying the given text.
 *
 * Creates an internal BTextView configured with the system tooltip colours
 * and word-wrapping disabled. The text view is not editable.
 *
 * @param text  The tooltip text to display.
 * @see   SetText(), Text()
 */
BTextToolTip::BTextToolTip(const char* text)
{
	_InitData(text);
}


/**
 * @brief Unarchiving constructor; restores a BTextToolTip from a BMessage.
 *
 * @param archive  The archive message produced by Archive().
 * @note  Full unarchiving support is not yet implemented.
 * @see   Archive()
 */
BTextToolTip::BTextToolTip(BMessage* archive)
{
	// TODO!
}


/**
 * @brief Destroy the BTextToolTip and release the internal BTextView.
 */
BTextToolTip::~BTextToolTip()
{
	delete fTextView;
}


/**
 * @brief Instantiate a BTextToolTip from an archived BMessage.
 *
 * @param archive  The archive message to instantiate from.
 * @return A newly allocated BTextToolTip, or NULL if @a archive is not a
 *         valid BTextToolTip archive.
 * @see   Archive()
 */
/*static*/ BTextToolTip*
BTextToolTip::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "BTextToolTip"))
		return NULL;

	return new(std::nothrow) BTextToolTip(archive);
}


/**
 * @brief Archive the BTextToolTip into a BMessage.
 *
 * Delegates to BToolTip::Archive(). Full text archiving is not yet implemented.
 *
 * @param archive  The message to archive into.
 * @param deep     If true, child objects are archived recursively.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK On success.
 * @see   Instantiate()
 */
status_t
BTextToolTip::Archive(BMessage* archive, bool deep) const
{
	status_t status = BToolTip::Archive(archive, deep);
	// TODO!

	return status;
}


/**
 * @brief Return the BView used to render this tooltip.
 *
 * Returns the internal BTextView. The tooltip manager embeds this view into
 * the floating tooltip window.
 *
 * @return Pointer to the internal BTextView.
 */
BView*
BTextToolTip::View() const
{
	return fTextView;
}


/**
 * @brief Return the text currently displayed by this tooltip.
 *
 * @return A pointer to the text string owned by the internal BTextView.
 *         The string remains valid until the next call to SetText() or
 *         until this object is destroyed.
 * @see   SetText()
 */
const char*
BTextToolTip::Text() const
{
	return fTextView->Text();
}


/**
 * @brief Replace the text displayed by this tooltip.
 *
 * Locks the tooltip (via Lock()), updates the internal BTextView, and
 * invalidates its layout so that the tooltip window resizes to fit the new
 * content. Does nothing if Lock() fails.
 *
 * @param text  The new text to display.
 * @see   Text()
 */
void
BTextToolTip::SetText(const char* text)
{
	if (!Lock())
		return;

	fTextView->SetText(text);
	fTextView->InvalidateLayout();

	Unlock();
}


/**
 * @brief Initialise the internal BTextView with the given text and system colours.
 *
 * Configures the view to use B_TOOL_TIP_BACKGROUND_COLOR and
 * B_TOOL_TIP_TEXT_COLOR, disables editing and word-wrap, then sets the initial
 * text.
 *
 * @param text  The initial tooltip text.
 */
void
BTextToolTip::_InitData(const char* text)
{
	fTextView = new BTextView("tool tip text");
	fTextView->SetText(text);
	fTextView->MakeEditable(false);
	fTextView->SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);
	rgb_color color = ui_color(B_TOOL_TIP_TEXT_COLOR);
	fTextView->SetFontAndColor(NULL, 0, &color);
	fTextView->SetWordWrap(false);
}
