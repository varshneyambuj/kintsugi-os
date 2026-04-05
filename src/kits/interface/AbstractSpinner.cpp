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
 *   Copyright 2004 DarkWyrm <darkwyrm@earthlink.net>
 *   Copyright 2013 FeemanLou
 *   Copyright 2014-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm, darkwyrm@earthlink.net
 *       FeemanLou
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file AbstractSpinner.cpp
 * @brief Implementation of BAbstractSpinner, the base class for spinner controls
 *
 * BAbstractSpinner provides a text field combined with increment/decrement
 * buttons for entering numeric values. It manages keyboard and mouse input,
 * drawing, and value-change notifications.
 *
 * @see BSpinner, BDecimalSpinner, BControl
 */


#include <AbstractSpinner.h>

#include <algorithm>

#include <AbstractLayoutItem.h>
#include <Alignment.h>
#include <ControlLook.h>
#include <Font.h>
#include <GradientLinear.h>
#include <LayoutItem.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Point.h>
#include <PropertyInfo.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>


/** @brief Pixel margin between the text view border and the surrounding frame. */
static const float kFrameMargin			= 2.0f;

/** @brief Archive field name used to store the layout item frame rectangle. */
const char* const kFrameField			= "BAbstractSpinner:layoutItem:frame";
/** @brief Archive field name used to identify the label layout item. */
const char* const kLabelItemField		= "BAbstractSpinner:labelItem";
/** @brief Archive field name used to identify the text-view layout item. */
const char* const kTextViewItemField	= "BAbstractSpinner:textViewItem";


static property_info sProperties[] = {
	{
		"Align",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the alignment of the spinner label.",
		0,
		{ B_INT32_TYPE }
	},
	{
		"Align",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the alignment of the spinner label.",
		0,
		{ B_INT32_TYPE }
	},

	{
		"ButtonStyle",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the style of the spinner buttons.",
		0,
		{ B_INT32_TYPE }
	},
	{
		"ButtonStyle",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the style of the spinner buttons.",
		0,
		{ B_INT32_TYPE }
	},

	{
		"Divider",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the divider position of the spinner.",
		0,
		{ B_FLOAT_TYPE }
	},
	{
		"Divider",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the divider position of the spinner.",
		0,
		{ B_FLOAT_TYPE }
	},

	{
		"Enabled",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns whether or not the spinner is enabled.",
		0,
		{ B_BOOL_TYPE }
	},
	{
		"Enabled",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets whether or not the spinner is enabled.",
		0,
		{ B_BOOL_TYPE }
	},

	{
		"Label",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the spinner label.",
		0,
		{ B_STRING_TYPE }
	},
	{
		"Label",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the spinner label.",
		0,
		{ B_STRING_TYPE }
	},

	{
		"Message",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the spinner invocation message.",
		0,
		{ B_MESSAGE_TYPE }
	},
	{
		"Message",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the spinner invocation message.",
		0,
		{ B_MESSAGE_TYPE }
	},

	{ 0 }
};


typedef enum {
	SPINNER_INCREMENT,
	SPINNER_DECREMENT
} spinner_direction;


class SpinnerButton : public BView {
public:
								SpinnerButton(BRect frame, const char* name,
									spinner_direction direction);
	virtual						~SpinnerButton();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* message);
	virtual void				MessageReceived(BMessage* message);

			void				AdoptSystemColors();
			bool				HasSystemColors() const;

			bool				IsEnabled() const { return fIsEnabled; }
	virtual	void				SetEnabled(bool enable) { fIsEnabled = enable; };

private:
			spinner_direction	fSpinnerDirection;
			BAbstractSpinner*	fParent;
			bool				fIsEnabled;
			bool				fIsMouseDown;
			bool				fIsMouseOver;
			BMessageRunner*		fRepeater;
};


class SpinnerTextView : public BTextView {
public:
								SpinnerTextView(BRect rect, BRect textRect);
	virtual						~SpinnerTextView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MakeFocus(bool focus);

private:
			BAbstractSpinner*	fParent;
};


class BAbstractSpinner::LabelLayoutItem : public BAbstractLayoutItem {
public:
								LabelLayoutItem(BAbstractSpinner* parent);
								LabelLayoutItem(BMessage* archive);

	virtual	bool				IsVisible();
	virtual	void				SetVisible(bool visible);

	virtual	BRect				Frame();
	virtual	void				SetFrame(BRect frame);

			void				SetParent(BAbstractSpinner* parent);
	virtual	BView*				View();

	virtual	BSize				BaseMinSize();
	virtual	BSize				BaseMaxSize();
	virtual	BSize				BasePreferredSize();
	virtual	BAlignment			BaseAlignment();

			BRect				FrameInParent() const;

	virtual status_t			Archive(BMessage* into, bool deep = true) const;
	static	BArchivable*		Instantiate(BMessage* from);

private:
			BAbstractSpinner*	fParent;
			BRect				fFrame;
};


class BAbstractSpinner::TextViewLayoutItem : public BAbstractLayoutItem {
public:
								TextViewLayoutItem(BAbstractSpinner* parent);
								TextViewLayoutItem(BMessage* archive);

	virtual	bool				IsVisible();
	virtual	void				SetVisible(bool visible);

	virtual	BRect				Frame();
	virtual	void				SetFrame(BRect frame);

			void				SetParent(BAbstractSpinner* parent);
	virtual	BView*				View();

	virtual	BSize				BaseMinSize();
	virtual	BSize				BaseMaxSize();
	virtual	BSize				BasePreferredSize();
	virtual	BAlignment			BaseAlignment();

			BRect				FrameInParent() const;

	virtual status_t			Archive(BMessage* into, bool deep = true) const;
	static	BArchivable*		Instantiate(BMessage* from);

private:
			BAbstractSpinner*	fParent;
			BRect				fFrame;
};


struct BAbstractSpinner::LayoutData {
	LayoutData(float width, float height)
	:
	label_layout_item(NULL),
	text_view_layout_item(NULL),
	label_width(0),
	label_height(0),
	text_view_width(0),
	text_view_height(0),
	previous_width(width),
	previous_height(height),
	valid(false)
	{
	}

	LabelLayoutItem* label_layout_item;
	TextViewLayoutItem* text_view_layout_item;

	font_height font_info;

	float label_width;
	float label_height;
	float text_view_width;
	float text_view_height;

	float previous_width;
	float previous_height;

	BSize min;
	BAlignment alignment;

	bool valid;
};


//	#pragma mark - SpinnerButton


/**
 * @brief Construct a SpinnerButton for incrementing or decrementing the spinner.
 *
 * @param frame   Initial bounding rectangle in the parent view's coordinate system.
 * @param name    View name used for identification.
 * @param direction Whether this button increments or decrements the value.
 */
SpinnerButton::SpinnerButton(BRect frame, const char* name,
	spinner_direction direction)
	:
	BView(frame, name, B_FOLLOW_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW),
	fSpinnerDirection(direction),
	fParent(NULL),
	fIsEnabled(true),
	fIsMouseDown(false),
	fIsMouseOver(false),
	fRepeater(NULL)
{
}


/**
 * @brief Destroy the SpinnerButton and stop any active repeat timer.
 */
SpinnerButton::~SpinnerButton()
{
	delete fRepeater;
}


/**
 * @brief Cache the parent spinner reference and adopt system UI colors.
 *
 * Called automatically when the button is added to a window. The parent
 * pointer is resolved here rather than at construction time so that it
 * remains valid for the lifetime of the window attachment.
 */
void
SpinnerButton::AttachedToWindow()
{
	fParent = static_cast<BAbstractSpinner*>(Parent());

	AdoptSystemColors();
	BView::AttachedToWindow();
}


/**
 * @brief Clear the cached parent spinner reference when leaving the window.
 */
void
SpinnerButton::DetachedFromWindow()
{
	fParent = NULL;

	BView::DetachedFromWindow();
}


/**
 * @brief Draw the spinner button including its frame, background, and arrow or +/- glyph.
 *
 * The visual state (pressed, hovered, disabled) is encoded into tint values
 * passed to BControlLook. Arrow direction and glyph type are determined by
 * the parent spinner's ButtonStyle() and this button's direction.
 *
 * @param updateRect The invalid region that needs to be redrawn.
 */
void
SpinnerButton::Draw(BRect updateRect)
{
	BRect rect(Bounds());
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	BView::Draw(updateRect);

	float frameTint = fIsEnabled ? B_DARKEN_1_TINT : B_NO_TINT;

	float fgTint;
	if (!fIsEnabled)
		fgTint = B_DARKEN_1_TINT;
	else if (fIsMouseDown)
		fgTint = B_DARKEN_MAX_TINT;
	else
		fgTint = 1.777f;	// 216 --> 48.2 (48)

	float bgTint;
	if (fIsEnabled && fIsMouseOver)
		bgTint = B_DARKEN_1_TINT;
	else
		bgTint = B_NO_TINT;

	rgb_color bgColor = ViewColor();
	if (bgColor.red + bgColor.green + bgColor.blue <= 128 * 3) {
		// if dark background make the tint lighter
		frameTint = 2.0f - frameTint;
		fgTint = 2.0f - fgTint;
		bgTint = 2.0f - bgTint;
	}

	uint32 borders = be_control_look->B_TOP_BORDER
		| be_control_look->B_BOTTOM_BORDER;

	if (fSpinnerDirection == SPINNER_INCREMENT)
		borders |= be_control_look->B_RIGHT_BORDER;
	else
		borders |= be_control_look->B_LEFT_BORDER;

	uint32 flags = fIsMouseDown ? BControlLook::B_ACTIVATED : 0;
	flags |= !fIsEnabled ? BControlLook::B_DISABLED : 0;

	// draw the button
	be_control_look->DrawButtonFrame(this, rect, updateRect,
		tint_color(bgColor, frameTint), bgColor, flags, borders);
	be_control_look->DrawButtonBackground(this, rect, updateRect,
		tint_color(bgColor, bgTint), flags, borders);

	switch (fParent->ButtonStyle()) {
		case SPINNER_BUTTON_HORIZONTAL_ARROWS:
		{
			int32 arrowDirection = fSpinnerDirection == SPINNER_INCREMENT
				? be_control_look->B_RIGHT_ARROW
				: be_control_look->B_LEFT_ARROW;

			rect.InsetBy(0.0f, 1.0f);
			be_control_look->DrawArrowShape(this, rect, updateRect, bgColor,
				arrowDirection, 0, fgTint);
			break;
		}

		case SPINNER_BUTTON_VERTICAL_ARROWS:
		{
			int32 arrowDirection = fSpinnerDirection == SPINNER_INCREMENT
				? be_control_look->B_UP_ARROW
				: be_control_look->B_DOWN_ARROW;

			rect.InsetBy(0.0f, 1.0f);
			be_control_look->DrawArrowShape(this, rect, updateRect, bgColor,
				arrowDirection, 0, fgTint);
			break;
		}

		default:
		case SPINNER_BUTTON_PLUS_MINUS:
		{
			BFont font;
			fParent->GetFont(&font);
			float inset = floorf(font.Size() / 4);
			rect.InsetBy(inset, inset);

			if (rect.IntegerWidth() % 2 != 0)
				rect.right -= 1;

			if (rect.IntegerHeight() % 2 != 0)
				rect.bottom -= 1;

			// draw the +/-
			float halfHeight = floorf(rect.Height() / 2);
			StrokeLine(BPoint(rect.left, rect.top + halfHeight),
				BPoint(rect.right, rect.top + halfHeight));
			if (fSpinnerDirection == SPINNER_INCREMENT) {
				float halfWidth = floorf(rect.Width() / 2);
				StrokeLine(BPoint(rect.left + halfWidth, rect.top + 1),
					BPoint(rect.left + halfWidth, rect.bottom - 1));
			}
		}
	}
}


/**
 * @brief Set the button's UI colors to the standard control color roles.
 *
 * Assigns B_CONTROL_BACKGROUND_COLOR to the view and low colors and
 * B_CONTROL_TEXT_COLOR to the high color so the button integrates with
 * the current system color theme.
 */
void
SpinnerButton::AdoptSystemColors()
{
	SetViewUIColor(B_CONTROL_BACKGROUND_COLOR);
	SetLowUIColor(B_CONTROL_BACKGROUND_COLOR);
	SetHighUIColor(B_CONTROL_TEXT_COLOR);
}


/**
 * @brief Return whether the button is currently using the standard system colors.
 *
 * @return \c true if all three UI color roles are set to the system defaults
 *         with no tint applied, \c false otherwise.
 */
bool
SpinnerButton::HasSystemColors() const
{
	float tint = B_NO_TINT;

	return ViewUIColor(&tint) == B_CONTROL_BACKGROUND_COLOR && tint == B_NO_TINT
		&& LowUIColor(&tint) == B_CONTROL_BACKGROUND_COLOR && tint == B_NO_TINT
		&& HighUIColor(&tint) == B_CONTROL_TEXT_COLOR && tint == B_NO_TINT;
}


/**
 * @brief Handle a mouse-button-down event by triggering the first value change and starting the repeat timer.
 *
 * When the button is enabled, this immediately invokes Increment() or
 * Decrement() on the parent spinner, then installs a BMessageRunner that
 * fires every 200 ms to continue changing the value while the button is
 * held down.
 *
 * @param where The cursor position in the button's coordinate system.
 */
void
SpinnerButton::MouseDown(BPoint where)
{
	if (fIsEnabled) {
		fIsMouseDown = true;
		fSpinnerDirection == SPINNER_INCREMENT
			? fParent->Increment()
			: fParent->Decrement();
		Invalidate();
		BMessage repeatMessage('rept');
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
		fRepeater = new BMessageRunner(BMessenger(this), repeatMessage,
			200000);
	}

	BView::MouseDown(where);
}


/**
 * @brief Track cursor position changes to update the hover highlight and handle exit events.
 *
 * Sets the hover state when the cursor is inside the button with no buttons
 * pressed, and clears it (also triggering a synthetic MouseUp) when the
 * cursor leaves the button's bounds.
 *
 * @param where   Current cursor position in the button's coordinate system.
 * @param transit One of B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW, or
 *                B_OUTSIDE_VIEW.
 * @param message Drag message, or NULL if this is not a drag operation.
 */
void
SpinnerButton::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	switch (transit) {
		case B_ENTERED_VIEW:
		case B_INSIDE_VIEW:
		{
			BPoint where;
			uint32 buttons;
			GetMouse(&where, &buttons);
			fIsMouseOver = Bounds().Contains(where) && buttons == 0;

			break;
		}

		case B_EXITED_VIEW:
		case B_OUTSIDE_VIEW:
			fIsMouseOver = false;
			MouseUp(Bounds().LeftTop());
			break;
	}

	BView::MouseMoved(where, transit, message);
}


/**
 * @brief Handle a mouse-button-up event by stopping the repeat timer and redrawing.
 *
 * @param where The cursor position in the button's coordinate system at release.
 */
void
SpinnerButton::MouseUp(BPoint where)
{
	fIsMouseDown = false;
	delete fRepeater;
	fRepeater = NULL;
	Invalidate();

	BView::MouseUp(where);
}


/**
 * @brief Handle the internal repeat message that fires while the button is held down.
 *
 * While the mouse button remains pressed and the repeat timer is active,
 * each 'rept' message triggers another Increment() or Decrement() call on
 * the parent spinner. All other messages are forwarded to BView.
 *
 * @param message The incoming message; 'rept' codes are handled internally.
 */
void
SpinnerButton::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'rept':
		{
			if (fIsMouseDown && fRepeater != NULL) {
				fSpinnerDirection == SPINNER_INCREMENT
					? fParent->Increment()
					: fParent->Decrement();
			}

			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


//	#pragma mark - SpinnerTextView


/**
 * @brief Construct the embedded text view used to display and edit the spinner value.
 *
 * @param rect     Frame rectangle in the parent view's coordinate system.
 * @param textRect Text content rectangle in the view's own coordinate system.
 */
SpinnerTextView::SpinnerTextView(BRect rect, BRect textRect)
	:
	BTextView(rect, "textview", textRect, B_FOLLOW_ALL,
		B_WILL_DRAW | B_NAVIGABLE),
	fParent(NULL)
{
	MakeResizable(true);
}


/**
 * @brief Destroy the SpinnerTextView.
 */
SpinnerTextView::~SpinnerTextView()
{
}


/**
 * @brief Cache the parent spinner pointer when this view is attached to a window.
 */
void
SpinnerTextView::AttachedToWindow()
{
	fParent = static_cast<BAbstractSpinner*>(Parent());

	BTextView::AttachedToWindow();
}


/**
 * @brief Clear the cached parent spinner pointer when this view is detached from its window.
 */
void
SpinnerTextView::DetachedFromWindow()
{
	fParent = NULL;

	BTextView::DetachedFromWindow();
}


/**
 * @brief Intercept key events to implement spinner-specific keyboard navigation.
 *
 * Enter and Space commit the current text to the parent spinner via
 * SetValueFromText(). Tab is forwarded to the parent for proper focus
 * traversal. Arrow keys adjust the value or move the cursor depending on
 * the active button style and modifier keys; all other keys are forwarded
 * to BTextView.
 *
 * @param bytes    Pointer to the UTF-8 byte sequence of the key event.
 * @param numBytes Number of bytes in \a bytes.
 */
void
SpinnerTextView::KeyDown(const char* bytes, int32 numBytes)
{
	if (fParent == NULL) {
		BTextView::KeyDown(bytes, numBytes);
		return;
	}

	switch (bytes[0]) {
		case B_ENTER:
		case B_SPACE:
			fParent->SetValueFromText();
			break;

		case B_TAB:
			fParent->KeyDown(bytes, numBytes);
			break;

		case B_LEFT_ARROW:
			if (fParent->ButtonStyle() == SPINNER_BUTTON_HORIZONTAL_ARROWS
				&& (modifiers() & B_CONTROL_KEY) != 0) {
				// need to hold down control, otherwise can't move cursor
				fParent->Decrement();
			} else
				BTextView::KeyDown(bytes, numBytes);
			break;

		case B_UP_ARROW:
			if (fParent->ButtonStyle() != SPINNER_BUTTON_HORIZONTAL_ARROWS)
				fParent->Increment();
			else
				BTextView::KeyDown(bytes, numBytes);
			break;

		case B_RIGHT_ARROW:
			if (fParent->ButtonStyle() == SPINNER_BUTTON_HORIZONTAL_ARROWS
				&& (modifiers() & B_CONTROL_KEY) != 0) {
				// need to hold down control, otherwise can't move cursor
				fParent->Increment();
			} else
				BTextView::KeyDown(bytes, numBytes);
			break;

		case B_DOWN_ARROW:
			if (fParent->ButtonStyle() != SPINNER_BUTTON_HORIZONTAL_ARROWS)
				fParent->Decrement();
			else
				BTextView::KeyDown(bytes, numBytes);
			break;

		default:
			BTextView::KeyDown(bytes, numBytes);
			break;
	}
}


/**
 * @brief Respond to focus changes by selecting all text on focus gain and committing the value on focus loss.
 *
 * When focus is gained the entire text is selected so the user can replace
 * it immediately. When focus is lost SetValueFromText() is called to
 * validate and apply whatever the user typed. The parent's text-view border
 * is redrawn in both cases to reflect the new focus state.
 *
 * @param focus \c true when gaining focus, \c false when losing it.
 */
void
SpinnerTextView::MakeFocus(bool focus)
{
	BTextView::MakeFocus(focus);

	if (fParent == NULL)
		return;

	if (focus)
		SelectAll();
	else
		fParent->SetValueFromText();

	fParent->_DrawTextView(fParent->Bounds());
}


//	#pragma mark - BAbstractSpinner::LabelLayoutItem


/**
 * @brief Construct a LabelLayoutItem for the given parent spinner.
 *
 * @param parent The BAbstractSpinner that owns this layout item.
 */
BAbstractSpinner::LabelLayoutItem::LabelLayoutItem(BAbstractSpinner* parent)
	:
	fParent(parent),
	fFrame()
{
}


/**
 * @brief Reconstruct a LabelLayoutItem from an archived BMessage.
 *
 * @param from The archive message; the frame rectangle is read from the
 *             kFrameField key.
 */
BAbstractSpinner::LabelLayoutItem::LabelLayoutItem(BMessage* from)
	:
	BAbstractLayoutItem(from),
	fParent(NULL),
	fFrame()
{
	from->FindRect(kFrameField, &fFrame);
}


/**
 * @brief Return whether the label is currently visible.
 *
 * @return \c true if the parent spinner is not hidden, \c false otherwise.
 */
bool
BAbstractSpinner::LabelLayoutItem::IsVisible()
{
	return !fParent->IsHidden(fParent);
}


/**
 * @brief No-op visibility setter; label visibility is controlled by the parent view.
 *
 * @param visible Ignored.
 */
void
BAbstractSpinner::LabelLayoutItem::SetVisible(bool visible)
{
}


/**
 * @brief Return the label item's frame rectangle in screen coordinates.
 *
 * @return The current frame as set by the last SetFrame() call.
 */
BRect
BAbstractSpinner::LabelLayoutItem::Frame()
{
	return fFrame;
}


/**
 * @brief Set the label item's frame and notify the parent to update its overall frame.
 *
 * @param frame The new frame rectangle in the coordinate system of the
 *              parent view's parent.
 */
void
BAbstractSpinner::LabelLayoutItem::SetFrame(BRect frame)
{
	fFrame = frame;
	fParent->_UpdateFrame();
}


/**
 * @brief Set the parent spinner that owns this layout item.
 *
 * @param parent The new owning BAbstractSpinner.
 */
void
BAbstractSpinner::LabelLayoutItem::SetParent(BAbstractSpinner* parent)
{
	fParent = parent;
}


/**
 * @brief Return the BView associated with this layout item.
 *
 * @return The parent BAbstractSpinner, which is itself a BView.
 */
BView*
BAbstractSpinner::LabelLayoutItem::View()
{
	return fParent;
}


/**
 * @brief Return the minimum size required to display the label.
 *
 * Validates layout data and returns a BSize that accommodates the full label
 * width plus the default label spacing. Returns (-1, -1) if there is no label.
 *
 * @return The minimum BSize for the label area.
 */
BSize
BAbstractSpinner::LabelLayoutItem::BaseMinSize()
{
	fParent->_ValidateLayoutData();

	if (fParent->Label() == NULL)
		return BSize(-1.0f, -1.0f);

	return BSize(fParent->fLayoutData->label_width
			+ be_control_look->DefaultLabelSpacing(),
		fParent->fLayoutData->label_height);
}


/**
 * @brief Return the maximum size for the label area, which equals the minimum size.
 *
 * @return The same value as BaseMinSize().
 */
BSize
BAbstractSpinner::LabelLayoutItem::BaseMaxSize()
{
	return BaseMinSize();
}


/**
 * @brief Return the preferred size for the label area, which equals the minimum size.
 *
 * @return The same value as BaseMinSize().
 */
BSize
BAbstractSpinner::LabelLayoutItem::BasePreferredSize()
{
	return BaseMinSize();
}


/**
 * @brief Return the alignment that allows this item to use its full allocated area.
 *
 * @return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT).
 */
BAlignment
BAbstractSpinner::LabelLayoutItem::BaseAlignment()
{
	return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);
}


/**
 * @brief Return the label frame translated into the parent spinner's local coordinate system.
 *
 * @return The frame rectangle offset so that the parent's top-left is the origin.
 */
BRect
BAbstractSpinner::LabelLayoutItem::FrameInParent() const
{
	return fFrame.OffsetByCopy(-fParent->Frame().left, -fParent->Frame().top);
}


/**
 * @brief Archive this label layout item into a BMessage.
 *
 * @param into The message to archive into.
 * @param deep If \c true, child objects are archived as well.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BAbstractSpinner::LabelLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t result = BAbstractLayoutItem::Archive(into, deep);

	if (result == B_OK)
		result = into->AddRect(kFrameField, fFrame);

	return archiver.Finish(result);
}


/**
 * @brief Instantiate a LabelLayoutItem from an archived BMessage.
 *
 * @param from The archive message to instantiate from.
 * @return A new LabelLayoutItem if the archive is valid, or NULL otherwise.
 * @see Archive()
 */
BArchivable*
BAbstractSpinner::LabelLayoutItem::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BAbstractSpinner::LabelLayoutItem"))
		return new LabelLayoutItem(from);

	return NULL;
}


//	#pragma mark - BAbstractSpinner::TextViewLayoutItem


/**
 * @brief Construct a TextViewLayoutItem for the given parent spinner.
 *
 * Sets the explicit maximum width to B_SIZE_UNLIMITED so the text view
 * area can expand horizontally inside a layout.
 *
 * @param parent The BAbstractSpinner that owns this layout item.
 */
BAbstractSpinner::TextViewLayoutItem::TextViewLayoutItem(BAbstractSpinner* parent)
	:
	fParent(parent),
	fFrame()
{
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}


/**
 * @brief Reconstruct a TextViewLayoutItem from an archived BMessage.
 *
 * @param from The archive message; the frame rectangle is read from the
 *             kFrameField key.
 */
BAbstractSpinner::TextViewLayoutItem::TextViewLayoutItem(BMessage* from)
	:
	BAbstractLayoutItem(from),
	fParent(NULL),
	fFrame()
{
	from->FindRect(kFrameField, &fFrame);
}


/**
 * @brief Return whether the text view area is currently visible.
 *
 * @return \c true if the parent spinner is not hidden, \c false otherwise.
 */
bool
BAbstractSpinner::TextViewLayoutItem::IsVisible()
{
	return !fParent->IsHidden(fParent);
}


/**
 * @brief No-op visibility setter; visibility is controlled by the parent view.
 *
 * @param visible Ignored.
 */
void
BAbstractSpinner::TextViewLayoutItem::SetVisible(bool visible)
{
	// not allowed
}


/**
 * @brief Return the text-view item's frame rectangle.
 *
 * @return The current frame as set by the last SetFrame() call.
 */
BRect
BAbstractSpinner::TextViewLayoutItem::Frame()
{
	return fFrame;
}


/**
 * @brief Set the text-view item's frame and notify the parent to update its overall frame.
 *
 * @param frame The new frame rectangle in the coordinate system of the
 *              parent view's parent.
 */
void
BAbstractSpinner::TextViewLayoutItem::SetFrame(BRect frame)
{
	fFrame = frame;
	fParent->_UpdateFrame();
}


/**
 * @brief Set the parent spinner that owns this layout item.
 *
 * @param parent The new owning BAbstractSpinner.
 */
void
BAbstractSpinner::TextViewLayoutItem::SetParent(BAbstractSpinner* parent)
{
	fParent = parent;
}


/**
 * @brief Return the BView associated with this layout item.
 *
 * @return The parent BAbstractSpinner, which is itself a BView.
 */
BView*
BAbstractSpinner::TextViewLayoutItem::View()
{
	return fParent;
}


/**
 * @brief Return the minimum size needed for the text view and its buttons.
 *
 * @return A BSize computed from the cached text_view_width and
 *         text_view_height fields in the parent's LayoutData.
 */
BSize
BAbstractSpinner::TextViewLayoutItem::BaseMinSize()
{
	fParent->_ValidateLayoutData();

	BSize size(fParent->fLayoutData->text_view_width,
		fParent->fLayoutData->text_view_height);

	return size;
}


/**
 * @brief Return the maximum size for the text view area, which equals the minimum size.
 *
 * @return The same value as BaseMinSize().
 */
BSize
BAbstractSpinner::TextViewLayoutItem::BaseMaxSize()
{
	return BaseMinSize();
}


/**
 * @brief Return the preferred size for the text view area, which equals the minimum size.
 *
 * @return The same value as BaseMinSize().
 */
BSize
BAbstractSpinner::TextViewLayoutItem::BasePreferredSize()
{
	return BaseMinSize();
}


/**
 * @brief Return the alignment that allows this item to use its full allocated area.
 *
 * @return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT).
 */
BAlignment
BAbstractSpinner::TextViewLayoutItem::BaseAlignment()
{
	return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);
}


/**
 * @brief Return the text-view frame translated into the parent spinner's local coordinate system.
 *
 * @return The frame rectangle offset so that the parent's top-left is the origin.
 */
BRect
BAbstractSpinner::TextViewLayoutItem::FrameInParent() const
{
	return fFrame.OffsetByCopy(-fParent->Frame().left, -fParent->Frame().top);
}


/**
 * @brief Archive this text-view layout item into a BMessage.
 *
 * @param into The message to archive into.
 * @param deep If \c true, child objects are archived as well.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BAbstractSpinner::TextViewLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t result = BAbstractLayoutItem::Archive(into, deep);

	if (result == B_OK)
		result = into->AddRect(kFrameField, fFrame);

	return archiver.Finish(result);
}


/**
 * @brief Instantiate a TextViewLayoutItem from an archived BMessage.
 *
 * @param from The archive message to instantiate from.
 * @return A new LabelLayoutItem if the archive is valid, or NULL otherwise.
 * @see Archive()
 */
BArchivable*
BAbstractSpinner::TextViewLayoutItem::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BAbstractSpinner::TextViewLayoutItem"))
		return new LabelLayoutItem(from);

	return NULL;
}


//	#pragma mark - BAbstractSpinner


/**
 * @brief Construct a BAbstractSpinner with an explicit frame rectangle (legacy frame-based layout).
 *
 * Initializes the control with the supplied geometry, label, and message,
 * then calls _InitObject() to create the embedded text view and buttons.
 *
 * @param frame        Initial bounding rectangle in the parent's coordinate system.
 * @param name         View name used for identification.
 * @param label        Descriptive label displayed to the left of the text field.
 * @param message      Invocation message sent when the value changes.
 * @param resizingMode Resizing mode flags (e.g. B_FOLLOW_LEFT | B_FOLLOW_TOP).
 * @param flags        View flags; B_WILL_DRAW and B_FRAME_EVENTS are always added.
 */
BAbstractSpinner::BAbstractSpinner(BRect frame, const char* name, const char* label,
	BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, label, message, resizingMode,
		flags | B_WILL_DRAW | B_FRAME_EVENTS)
{
	_InitObject();
}


/**
 * @brief Construct a BAbstractSpinner using the layout system (no explicit frame).
 *
 * Initializes the control with the supplied label and message, then calls
 * _InitObject() to create the embedded text view and buttons. The frame is
 * determined by the layout engine at layout time.
 *
 * @param name    View name used for identification.
 * @param label   Descriptive label displayed to the left of the text field.
 * @param message Invocation message sent when the value changes.
 * @param flags   View flags; B_WILL_DRAW and B_FRAME_EVENTS are always added.
 */
BAbstractSpinner::BAbstractSpinner(const char* name, const char* label, BMessage* message,
	uint32 flags)
	:
	BControl(name, label, message, flags | B_WILL_DRAW | B_FRAME_EVENTS)
{
	_InitObject();
}


/**
 * @brief Reconstruct a BAbstractSpinner from an archived BMessage.
 *
 * Restores alignment, button style, and divider position from the archive.
 * Missing fields fall back to default values.
 *
 * @param data The archive message produced by Archive().
 * @see Archive()
 * @see Instantiate()
 */
BAbstractSpinner::BAbstractSpinner(BMessage* data)
	:
	BControl(data),
	fButtonStyle(SPINNER_BUTTON_PLUS_MINUS)
{
	_InitObject();

	if (data->FindInt32("_align") != B_OK)
		fAlignment = B_ALIGN_LEFT;

	if (data->FindInt32("_button_style") != B_OK)
		fButtonStyle = SPINNER_BUTTON_PLUS_MINUS;

	if (data->FindInt32("_divider") != B_OK)
		fDivider = 0.0f;
}


/**
 * @brief Destroy the BAbstractSpinner and release its layout data.
 */
BAbstractSpinner::~BAbstractSpinner()
{
	delete fLayoutData;
	fLayoutData = NULL;
}


/**
 * @brief Instantiate a BAbstractSpinner from an archive — always returns NULL.
 *
 * BAbstractSpinner is an abstract base class and cannot be instantiated
 * directly. Concrete subclasses (BSpinner, BDecimalSpinner) override this.
 *
 * @param data Ignored.
 * @return Always NULL.
 */
BArchivable*
BAbstractSpinner::Instantiate(BMessage* data)
{
	// cannot instantiate an abstract spinner
	return NULL;
}


/**
 * @brief Archive the spinner's state into a BMessage.
 *
 * Stores the label alignment, button style, and divider position in addition
 * to all fields saved by BControl::Archive().
 *
 * @param data  The message to archive into.
 * @param deep  If \c true, child objects are archived as well.
 * @return B_OK on success, or an error code if any field could not be added.
 * @see Instantiate()
 */
status_t
BAbstractSpinner::Archive(BMessage* data, bool deep) const
{
	status_t status = BControl::Archive(data, deep);
	data->AddString("class", "Spinner");

	if (status == B_OK)
		status = data->AddInt32("_align", fAlignment);

	if (status == B_OK)
		data->AddInt32("_button_style", fButtonStyle);

	if (status == B_OK)
		status = data->AddFloat("_divider", fDivider);

	return status;
}


/**
 * @brief Report the scripting suites and property table supported by this spinner.
 *
 * Adds the "suite/vnd.Haiku-spinner" suite identifier and the full property
 * info table to \a message, then delegates to BControl::GetSupportedSuites().
 *
 * @param message The reply message to fill with suite and property information.
 * @return B_OK, or an error code from the base class.
 * @see ResolveSpecifier()
 */
status_t
BAbstractSpinner::GetSupportedSuites(BMessage* message)
{
	message->AddString("suites", "suite/vnd.Haiku-spinner");

	BPropertyInfo prop_info(sProperties);
	message->AddFlat("messages", &prop_info);

	return BControl::GetSupportedSuites(message);
}


/**
 * @brief Resolve a scripting specifier to the appropriate handler.
 *
 * Delegates directly to BControl::ResolveSpecifier() since BAbstractSpinner
 * does not define any custom specifier handling.
 *
 * @param message   The scripting message being resolved.
 * @param index     Current specifier index within the message.
 * @param specifier The specifier message extracted from \a message.
 * @param form      The specifier form (e.g. B_DIRECT_SPECIFIER).
 * @param property  The property name string from the specifier.
 * @return The BHandler that should handle the scripted request.
 * @see GetSupportedSuites()
 */
BHandler*
BAbstractSpinner::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier, form,
		property);
}


/**
 * @brief Complete initialization when the spinner is attached to a window.
 *
 * Sets the message target to the window if none has been set, synchronizes
 * the control value (which updates the text and button states), and applies
 * the correct text-view colors and editability for the current enabled state.
 */
void
BAbstractSpinner::AttachedToWindow()
{
	if (!Messenger().IsValid())
		SetTarget(Window());

	BControl::SetValue(Value());
		// sets the text and enables or disables the arrows

	_UpdateTextViewColors(IsEnabled());
	fTextView->MakeEditable(IsEnabled());

	BControl::AttachedToWindow();
}


/**
 * @brief Draw the spinner by rendering the label, text-view border, and button faces.
 *
 * Delegates label drawing to _DrawLabel(), text-view border drawing to
 * _DrawTextView(), and button rendering to each button's own Draw() via
 * Invalidate().
 *
 * @param updateRect The invalid region that triggered this draw call.
 */
void
BAbstractSpinner::Draw(BRect updateRect)
{
	_DrawLabel(updateRect);
	_DrawTextView(updateRect);
	fIncrement->Invalidate();
	fDecrement->Invalidate();
}


/**
 * @brief Respond to frame resize events by invalidating only the newly exposed or removed regions.
 *
 * Minimizes redraw work by computing the delta between the old and new
 * dimensions and invalidating only the affected border strips and label area.
 *
 * @param width  New width of the spinner's bounding rectangle.
 * @param height New height of the spinner's bounding rectangle.
 */
void
BAbstractSpinner::FrameResized(float width, float height)
{
	BControl::FrameResized(width, height);

	// TODO: this causes flickering still...

	// changes in width

	BRect bounds = Bounds();

	if (bounds.Width() > fLayoutData->previous_width) {
		// invalidate the region between the old and the new right border
		BRect rect = bounds;
		rect.left += fLayoutData->previous_width - kFrameMargin;
		rect.right--;
		Invalidate(rect);
	} else if (bounds.Width() < fLayoutData->previous_width) {
		// invalidate the region of the new right border
		BRect rect = bounds;
		rect.left = rect.right - kFrameMargin;
		Invalidate(rect);
	}

	// changes in height

	if (bounds.Height() > fLayoutData->previous_height) {
		// invalidate the region between the old and the new bottom border
		BRect rect = bounds;
		rect.top += fLayoutData->previous_height - kFrameMargin;
		rect.bottom--;
		Invalidate(rect);
		// invalidate label area
		rect = bounds;
		rect.right = fDivider;
		Invalidate(rect);
	} else if (bounds.Height() < fLayoutData->previous_height) {
		// invalidate the region of the new bottom border
		BRect rect = bounds;
		rect.top = rect.bottom - kFrameMargin;
		Invalidate(rect);
		// invalidate label area
		rect = bounds;
		rect.right = fDivider;
		Invalidate(rect);
	}

	fLayoutData->previous_width = bounds.Width();
	fLayoutData->previous_height = bounds.Height();
}


/**
 * @brief Hook called when the spinner's value changes; does nothing in the base class.
 *
 * Subclasses may override this to react to value changes without overriding
 * the full SetValue() machinery.
 */
void
BAbstractSpinner::ValueChanged()
{
	// hook method - does nothing
}


/**
 * @brief Handle system messages, forwarding to the base class after processing color updates.
 *
 * Intercepts B_COLORS_UPDATED to refresh the text-view colors when the
 * system color scheme changes. All other messages are forwarded to BControl.
 *
 * @param message The incoming message to handle.
 */
void
BAbstractSpinner::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED)
		_UpdateTextViewColors(IsEnabled());

	BControl::MessageReceived(message);
}


/**
 * @brief Redirect focus to the embedded text view.
 *
 * Focuses the inner SpinnerTextView rather than the spinner container
 * itself, so keyboard input goes directly to the text field.
 *
 * @param focus \c true to give focus, \c false to remove it.
 */
void
BAbstractSpinner::MakeFocus(bool focus)
{
	fTextView->MakeFocus(focus);
}


/**
 * @brief Resize the spinner to its preferred size and recalculate the divider.
 *
 * Calls BControl::ResizeToPreferred() to set the frame, then recomputes
 * the label divider based on the current label string width and the default
 * label spacing, and triggers a layout of the inner text view.
 */
void
BAbstractSpinner::ResizeToPreferred()
{
	BControl::ResizeToPreferred();

	const char* label = Label();
	if (label != NULL) {
		fDivider = ceilf(StringWidth(label))
			+ be_control_look->DefaultLabelSpacing();
	} else
		fDivider = 0.0f;

	_LayoutTextView();
}


/**
 * @brief Set view flags, keeping the B_NAVIGABLE flag only on the embedded text view.
 *
 * The spinner container itself is never made navigable; instead, the
 * B_NAVIGABLE flag is mirrored onto the inner SpinnerTextView so that tab
 * focus reaches the text field rather than the outer view.
 *
 * @param flags The new set of view flags.
 */
void
BAbstractSpinner::SetFlags(uint32 flags)
{
	// If the textview is navigable, set it to not navigable if needed,
	// else if it is not navigable, set it to navigable if needed
	if (fTextView->Flags() & B_NAVIGABLE) {
		if (!(flags & B_NAVIGABLE))
			fTextView->SetFlags(fTextView->Flags() & ~B_NAVIGABLE);
	} else {
		if (flags & B_NAVIGABLE)
			fTextView->SetFlags(fTextView->Flags() | B_NAVIGABLE);
	}

	// Don't make this one navigable
	flags &= ~B_NAVIGABLE;

	BControl::SetFlags(flags);
}


/**
 * @brief Redraw the text-view border when the window gains or loses activation.
 *
 * The focus ring appearance changes with window activation, so the text-view
 * frame is invalidated to pick up the new rendering.
 *
 * @param active \c true if the window is becoming active, \c false otherwise.
 */
void
BAbstractSpinner::WindowActivated(bool active)
{
	_DrawTextView(fTextView->Frame());
}


/**
 * @brief Set the horizontal alignment of the label text within the label area.
 *
 * @param align One of B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT.
 * @see Alignment()
 */
void
BAbstractSpinner::SetAlignment(alignment align)
{
	fAlignment = align;
}


/**
 * @brief Set the visual style of the increment and decrement buttons.
 *
 * @param buttonStyle The desired spinner_button_style (plus/minus, horizontal
 *                    arrows, or vertical arrows).
 * @see ButtonStyle()
 */
void
BAbstractSpinner::SetButtonStyle(spinner_button_style buttonStyle)
{
	fButtonStyle = buttonStyle;
}


/**
 * @brief Set the pixel position of the divider between the label and text-view areas.
 *
 * In layout mode the spinner triggers a relayout; in frame mode it
 * repositions the inner views directly and invalidates the affected region.
 *
 * @param position The new divider offset from the left edge of the spinner,
 *                 rounded to the nearest integer pixel.
 * @see Divider()
 */
void
BAbstractSpinner::SetDivider(float position)
{
	position = roundf(position);

	float delta = fDivider - position;
	if (delta == 0.0f)
		return;

	fDivider = position;

	if ((Flags() & B_SUPPORTS_LAYOUT) != 0) {
		// We should never get here, since layout support means, we also
		// layout the divider, and don't use this method at all.
		Relayout();
	} else {
		_LayoutTextView();
		Invalidate();
	}
}


/**
 * @brief Enable or disable the spinner and update all child views accordingly.
 *
 * Updates the embedded text view's editability, navigability, and colors,
 * repositions the inner views, and forces a window update so the new state
 * is displayed immediately.
 *
 * @param enable \c true to enable the spinner, \c false to disable it.
 * @see IsEnabled()
 */
void
BAbstractSpinner::SetEnabled(bool enable)
{
	if (IsEnabled() == enable)
		return;

	BControl::SetEnabled(enable);

	fTextView->MakeEditable(enable);
	if (enable)
		fTextView->SetFlags(fTextView->Flags() | B_NAVIGABLE);
	else
		fTextView->SetFlags(fTextView->Flags() & ~B_NAVIGABLE);

	_UpdateTextViewColors(enable);
	fTextView->Invalidate();

	_LayoutTextView();
	Invalidate();
	if (Window() != NULL)
		Window()->UpdateIfNeeded();
}


/**
 * @brief Set the spinner's label and trigger an immediate window update.
 *
 * @param label The new label string, or NULL to remove the label.
 * @see Label()
 */
void
BAbstractSpinner::SetLabel(const char* label)
{
	BControl::SetLabel(label);

	if (Window() != NULL)
		Window()->UpdateIfNeeded();
}


/**
 * @brief Return whether the decrement button is currently enabled.
 *
 * @return \c true if the decrement button accepts input, \c false otherwise.
 * @see SetDecrementEnabled()
 */
bool
BAbstractSpinner::IsDecrementEnabled() const
{
	return fDecrement->IsEnabled();
}


/**
 * @brief Enable or disable the decrement button independently of the overall spinner state.
 *
 * Useful for clamping: disable the decrement button when the value is at its
 * minimum so the user cannot go lower.
 *
 * @param enable \c true to enable the decrement button, \c false to disable it.
 * @see IsDecrementEnabled()
 */
void
BAbstractSpinner::SetDecrementEnabled(bool enable)
{
	if (IsDecrementEnabled() == enable)
		return;

	fDecrement->SetEnabled(enable);
	fDecrement->Invalidate();
}


/**
 * @brief Return whether the increment button is currently enabled.
 *
 * @return \c true if the increment button accepts input, \c false otherwise.
 * @see SetIncrementEnabled()
 */
bool
BAbstractSpinner::IsIncrementEnabled() const
{
	return fIncrement->IsEnabled();
}


/**
 * @brief Enable or disable the increment button independently of the overall spinner state.
 *
 * Useful for clamping: disable the increment button when the value is at its
 * maximum so the user cannot go higher.
 *
 * @param enable \c true to enable the increment button, \c false to disable it.
 * @see IsIncrementEnabled()
 */
void
BAbstractSpinner::SetIncrementEnabled(bool enable)
{
	if (IsIncrementEnabled() == enable)
		return;

	fIncrement->SetEnabled(enable);
	fIncrement->Invalidate();
}


/**
 * @brief Return the minimum layout size of the spinner.
 *
 * Validates cached layout data and composes it with any explicit minimum
 * size set by the caller.
 *
 * @return The composed minimum BSize for use by the layout engine.
 * @see MaxSize()
 * @see PreferredSize()
 */
BSize
BAbstractSpinner::MinSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), fLayoutData->min);
}


/**
 * @brief Return the maximum layout size of the spinner.
 *
 * The height is fixed at the minimum height; the width is set to
 * B_SIZE_UNLIMITED so the spinner can expand horizontally inside a layout.
 *
 * @return The composed maximum BSize for use by the layout engine.
 * @see MinSize()
 * @see PreferredSize()
 */
BSize
BAbstractSpinner::MaxSize()
{
	_ValidateLayoutData();

	BSize max = fLayoutData->min;
	max.width = B_SIZE_UNLIMITED;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), max);
}


/**
 * @brief Return the preferred layout size of the spinner.
 *
 * Validates cached layout data and composes it with any explicit preferred
 * size set by the caller. The preferred size equals the minimum size unless
 * overridden.
 *
 * @return The composed preferred BSize for use by the layout engine.
 * @see MinSize()
 * @see MaxSize()
 */
BSize
BAbstractSpinner::PreferredSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		fLayoutData->min);
}


/**
 * @brief Return the layout alignment of the spinner within its allocated cell.
 *
 * Composes any explicit alignment override with the default of left-horizontal
 * and vertically centered.
 *
 * @return The composed BAlignment for use by the layout engine.
 */
BAlignment
BAbstractSpinner::LayoutAlignment()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));
}


/**
 * @brief Create or return the existing layout item that represents the label area.
 *
 * This allows the label and text-view portions of the spinner to be placed
 * into separate layout cells when using the layout system.
 *
 * @return The LabelLayoutItem owned by this spinner.
 * @see CreateTextViewLayoutItem()
 */
BLayoutItem*
BAbstractSpinner::CreateLabelLayoutItem()
{
	if (fLayoutData->label_layout_item == NULL)
		fLayoutData->label_layout_item = new LabelLayoutItem(this);

	return fLayoutData->label_layout_item;
}


/**
 * @brief Create or return the existing layout item that represents the text-view and button area.
 *
 * This allows the label and text-view portions of the spinner to be placed
 * into separate layout cells when using the layout system.
 *
 * @return The TextViewLayoutItem owned by this spinner.
 * @see CreateLabelLayoutItem()
 */
BLayoutItem*
BAbstractSpinner::CreateTextViewLayoutItem()
{
	if (fLayoutData->text_view_layout_item == NULL)
		fLayoutData->text_view_layout_item = new TextViewLayoutItem(this);

	return fLayoutData->text_view_layout_item;
}


/**
 * @brief Return the embedded BTextView used to display and edit the spinner's value.
 *
 * @return A pointer to the inner SpinnerTextView cast to BTextView.
 */
BTextView*
BAbstractSpinner::TextView() const
{
	return dynamic_cast<BTextView*>(fTextView);
}


//	#pragma mark - BAbstractSpinner protected methods


/**
 * @brief Archive all layout items into the message after the main archive pass.
 *
 * Called by the archiving framework after Archive() to allow layout items
 * that were separately archived by the layout engine to register themselves
 * under the appropriate field names.
 *
 * @param into The archive message being built.
 * @return B_OK on success, or an error code if archiving a layout item fails.
 * @see AllUnarchived()
 */
status_t
BAbstractSpinner::AllArchived(BMessage* into) const
{
	status_t result;
	if ((result = BControl::AllArchived(into)) != B_OK)
		return result;

	BArchiver archiver(into);

	BArchivable* textViewItem = fLayoutData->text_view_layout_item;
	if (archiver.IsArchived(textViewItem))
		result = archiver.AddArchivable(kTextViewItemField, textViewItem);

	if (result != B_OK)
		return result;

	BArchivable* labelBarItem = fLayoutData->label_layout_item;
	if (archiver.IsArchived(labelBarItem))
		result = archiver.AddArchivable(kLabelItemField, labelBarItem);

	return result;
}


/**
 * @brief Restore all layout items from the archive after the main unarchive pass.
 *
 * Called by the unarchiving framework after the constructor completes. If
 * the label or text-view layout items were archived, they are located in the
 * message and their parent pointers are set back to this spinner.
 *
 * @param from The archive message being read.
 * @return B_OK on success, or an error code if a layout item cannot be found.
 * @see AllArchived()
 */
status_t
BAbstractSpinner::AllUnarchived(const BMessage* from)
{
	BUnarchiver unarchiver(from);

	status_t result = B_OK;
	if ((result = BControl::AllUnarchived(from)) != B_OK)
		return result;

	if (unarchiver.IsInstantiated(kTextViewItemField)) {
		TextViewLayoutItem*& textViewItem
			= fLayoutData->text_view_layout_item;
		result = unarchiver.FindObject(kTextViewItemField,
			BUnarchiver::B_DONT_ASSUME_OWNERSHIP, textViewItem);

		if (result == B_OK)
			textViewItem->SetParent(this);
		else
			return result;
	}

	if (unarchiver.IsInstantiated(kLabelItemField)) {
		LabelLayoutItem*& labelItem = fLayoutData->label_layout_item;
		result = unarchiver.FindObject(kLabelItemField,
			BUnarchiver::B_DONT_ASSUME_OWNERSHIP, labelItem);

		if (result == B_OK)
			labelItem->SetParent(this);
	}

	return result;
}


/**
 * @brief Perform the layout pass, positioning the text view and buttons within the spinner's bounds.
 *
 * If the spinner has no B_SUPPORTS_LAYOUT flag the method returns immediately.
 * If a child layout is present, it delegates to BControl::DoLayout(). Otherwise,
 * it recomputes the divider from the layout item frames (or from the cached label
 * width), calls _LayoutTextView(), and invalidates the union of the old and new
 * text-view, increment, and decrement rects.
 */
void
BAbstractSpinner::DoLayout()
{
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		return;

	if (GetLayout()) {
		BControl::DoLayout();
		return;
	}

	_ValidateLayoutData();

	BSize size(Bounds().Size());
	if (size.width < fLayoutData->min.width)
		size.width = fLayoutData->min.width;

	if (size.height < fLayoutData->min.height)
		size.height = fLayoutData->min.height;

	float divider = 0;
	if (fLayoutData->label_layout_item != NULL
		&& fLayoutData->text_view_layout_item != NULL
		&& fLayoutData->label_layout_item->Frame().IsValid()
		&& fLayoutData->text_view_layout_item->Frame().IsValid()) {
		divider = fLayoutData->text_view_layout_item->Frame().left
			- fLayoutData->label_layout_item->Frame().left;
	} else if (fLayoutData->label_width > 0) {
		divider = fLayoutData->label_width
			+ be_control_look->DefaultLabelSpacing();
	}
	fDivider = divider;

	BRect dirty(fTextView->Frame());
	_LayoutTextView();

	// invalidate dirty region
	dirty = dirty | fTextView->Frame();
	dirty = dirty | fIncrement->Frame();
	dirty = dirty | fDecrement->Frame();

	Invalidate(dirty);
}


/**
 * @brief Mark the cached layout data as invalid when the layout is invalidated.
 *
 * @param descendants \c true if descendant layouts are also being invalidated.
 */
void
BAbstractSpinner::LayoutInvalidated(bool descendants)
{
	if (fLayoutData != NULL)
		fLayoutData->valid = false;
}


//	#pragma mark - BAbstractSpinner private methods


/**
 * @brief Draw the spinner label into the region to the left of the divider.
 *
 * Clips drawing to the label area, computes the text position from the
 * current alignment setting and the cached font metrics, and delegates to
 * BControlLook::DrawLabel().
 *
 * @param updateRect The invalid region; drawing is skipped if the label area
 *                   does not intersect it.
 */
void
BAbstractSpinner::_DrawLabel(BRect updateRect)
{
	BRect rect(Bounds());
	rect.right = fDivider;
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	_ValidateLayoutData();

	const char* label = Label();
	if (label == NULL)
		return;

	// horizontal position
	float x;
	switch (fAlignment) {
		case B_ALIGN_RIGHT:
			x = fDivider - fLayoutData->label_width - 3.0f;
			break;

		case B_ALIGN_CENTER:
			x = fDivider - roundf(fLayoutData->label_width / 2.0f);
			break;

		default:
			x = 0.0f;
			break;
	}

	// vertical position
	font_height& fontHeight = fLayoutData->font_info;
	float y = rect.top
		+ roundf((rect.Height() + 1.0f - fontHeight.ascent
			- fontHeight.descent) / 2.0f)
		+ fontHeight.ascent;

	uint32 flags = be_control_look->Flags(this);

	rgb_color highColor = HighColor();
	be_control_look->DrawLabel(this, label, LowColor(), flags, BPoint(x, y), &highColor);
}


/**
 * @brief Draw the focus border around the embedded text view.
 *
 * Insets the text-view frame by the frame margin to produce the border
 * rectangle, sets the disabled and focused flags, and delegates to
 * BControlLook::DrawTextControlBorder().
 *
 * @param updateRect The invalid region; drawing is skipped if the border
 *                   area does not intersect it.
 */
void
BAbstractSpinner::_DrawTextView(BRect updateRect)
{
	BRect rect = fTextView->Frame();
	rect.InsetBy(-kFrameMargin, -kFrameMargin);
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	rgb_color base = ViewColor();
	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;

	if (fTextView->IsFocus() && Window()->IsActive())
		flags |= BControlLook::B_FOCUSED;

	be_control_look->DrawTextControlBorder(this, rect, updateRect, base,
		flags);
}


/**
 * @brief Initialize member variables and create the embedded text view and buttons.
 *
 * Sets default alignment and button style, computes the initial divider from
 * the label width, allocates the LayoutData, then constructs and adds the
 * SpinnerTextView, decrement SpinnerButton, and increment SpinnerButton as
 * child views. Strips B_NAVIGABLE from the container so only the text view
 * participates in focus traversal.
 */
void
BAbstractSpinner::_InitObject()
{
	fAlignment = B_ALIGN_LEFT;
	fButtonStyle = SPINNER_BUTTON_PLUS_MINUS;

	if (Label() != NULL) {
		fDivider = StringWidth(Label())
			+ be_control_look->DefaultLabelSpacing();
	} else
		fDivider = 0.0f;

	BControl::SetEnabled(true);
	BControl::SetValue(0);

	BRect rect(Bounds());
	fLayoutData = new LayoutData(rect.Width(), rect.Height());

	rect.left = fDivider;
	rect.InsetBy(kFrameMargin, kFrameMargin);
	rect.right -= rect.Height() * 2 + kFrameMargin * 2 + 1.0f;
	BRect textRect(rect.OffsetToCopy(B_ORIGIN));

	fTextView = new SpinnerTextView(rect, textRect);
	AddChild(fTextView);

	rect.InsetBy(0.0f, -kFrameMargin);

	rect.left = rect.right + kFrameMargin * 2;
	rect.right = rect.left + rect.Height() - kFrameMargin * 2;

	fDecrement = new SpinnerButton(rect, "decrement", SPINNER_DECREMENT);
	AddChild(fDecrement);

	rect.left = rect.right + 1.0f;
	rect.right = rect.left + rect.Height() - kFrameMargin * 2;

	fIncrement = new SpinnerButton(rect, "increment", SPINNER_INCREMENT);
	AddChild(fIncrement);

	uint32 navigableFlags = Flags() & B_NAVIGABLE;
	if (navigableFlags != 0)
		BControl::SetFlags(Flags() & ~B_NAVIGABLE);
}


/**
 * @brief Reposition and resize the text view and both buttons to fit the current frame and divider.
 *
 * Computes the text-view rectangle from either the layout item frame (when
 * in layout mode) or the view bounds and divider, insets by the frame margin,
 * reserves space for the two square buttons, and moves/resizes all three
 * child views accordingly.
 */
void
BAbstractSpinner::_LayoutTextView()
{
	BRect rect;
	if (fLayoutData->text_view_layout_item != NULL) {
		rect = fLayoutData->text_view_layout_item->FrameInParent();
	} else {
		rect = Bounds();
		rect.left = fDivider;
	}
	rect.InsetBy(kFrameMargin, kFrameMargin);
	rect.right -= rect.Height() * 2 + kFrameMargin * 2 + 1.0f;

	fTextView->MoveTo(rect.left, rect.top);
	fTextView->ResizeTo(rect.Width(), rect.Height());
	fTextView->SetTextRect(rect.OffsetToCopy(B_ORIGIN));

	rect.InsetBy(0.0f, -kFrameMargin);

	rect.left = rect.right + kFrameMargin * 2;
	rect.right = rect.left + rect.Height() - kFrameMargin * 2;

	fDecrement->ResizeTo(rect.Width(), rect.Height());
	fDecrement->MoveTo(rect.LeftTop());

	rect.left = rect.right + 1.0f;
	rect.right = rect.left + rect.Height() - kFrameMargin * 2;

	fIncrement->ResizeTo(rect.Width(), rect.Height());
	fIncrement->MoveTo(rect.LeftTop());
}


/**
 * @brief Synchronize the spinner's frame with the union of its two layout item frames.
 *
 * Called by the label and text-view layout items whenever their SetFrame()
 * is invoked. Updates the divider, moves and resizes the spinner to the
 * union rectangle, and triggers a relayout if the size changes.
 */
void
BAbstractSpinner::_UpdateFrame()
{
	if (fLayoutData->label_layout_item == NULL
		|| fLayoutData->text_view_layout_item == NULL) {
		return;
	}

	BRect labelFrame = fLayoutData->label_layout_item->Frame();
	BRect textViewFrame = fLayoutData->text_view_layout_item->Frame();

	if (!labelFrame.IsValid() || !textViewFrame.IsValid())
		return;

	// update divider
	fDivider = textViewFrame.left - labelFrame.left;

	BRect frame = textViewFrame | labelFrame;
	MoveTo(frame.left, frame.top);
	BSize oldSize = Bounds().Size();
	ResizeTo(frame.Width(), frame.Height());
	BSize newSize = Bounds().Size();

	// If the size changes, ResizeTo() will trigger a relayout, otherwise
	// we need to do that explicitly.
	if (newSize != oldSize)
		Relayout();
}


/**
 * @brief Apply the correct text color and background to the embedded text view.
 *
 * When enabled, uses the standard document background and low colors. When
 * disabled, blends the document background and text colors with the view
 * color to produce the standard dimmed appearance. The font color is updated
 * in both cases.
 *
 * @param enable \c true to apply enabled colors, \c false for disabled colors.
 */
void
BAbstractSpinner::_UpdateTextViewColors(bool enable)
{
	// Mimick BTextControl's appearance.
	rgb_color textColor = ui_color(B_DOCUMENT_TEXT_COLOR);

	if (enable) {
		fTextView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		fTextView->SetLowUIColor(ViewUIColor());
	} else {
		rgb_color color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		color = disable_color(ViewColor(), color);
		textColor = disable_color(textColor, ViewColor());

		fTextView->SetViewColor(color);
		fTextView->SetLowColor(color);
	}

	BFont font;
	fTextView->GetFontAndColor(0, &font);
	fTextView->SetFontAndColor(&font, B_FONT_ALL, &textColor);
}


/**
 * @brief Recompute and cache layout metrics if the cached data is stale.
 *
 * Measures the current font metrics, label dimensions, and the minimum
 * text-view size (width wide enough for five digits, height equal to one
 * line plus frame margins). Stores the results in fLayoutData and marks the
 * cache valid. Also calls ResetLayoutInvalidation() so the layout system
 * knows the data is fresh.
 *
 * @note This method is a no-op when fLayoutData->valid is already true.
 */
void
BAbstractSpinner::_ValidateLayoutData()
{
	if (fLayoutData->valid)
		return;

	font_height& fontHeight = fLayoutData->font_info;
	GetFontHeight(&fontHeight);

	if (Label() != NULL) {
		fLayoutData->label_width = StringWidth(Label());
		fLayoutData->label_height = ceilf(fontHeight.ascent
			+ fontHeight.descent + fontHeight.leading);
	} else {
		fLayoutData->label_width = 0;
		fLayoutData->label_height = 0;
	}

	float divider = 0;
	if (fLayoutData->label_width > 0) {
		divider = ceilf(fLayoutData->label_width
			+ be_control_look->DefaultLabelSpacing());
	}

	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		divider = std::max(divider, fDivider);

	float minTextWidth = fTextView->StringWidth("99999");

	float textViewHeight = fTextView->LineHeight(0) + kFrameMargin * 2;
	float textViewWidth = minTextWidth + textViewHeight * 2;

	fLayoutData->text_view_width = textViewWidth;
	fLayoutData->text_view_height = textViewHeight;

	BSize min(textViewWidth, textViewHeight);
	if (divider > 0.0f)
		min.width += divider;

	if (fLayoutData->label_height > min.height)
		min.height = fLayoutData->label_height;

	fLayoutData->min = min;
	fLayoutData->valid = true;

	ResetLayoutInvalidation();
}


// FBC padding

void BAbstractSpinner::_ReservedAbstractSpinner20() {}
void BAbstractSpinner::_ReservedAbstractSpinner19() {}
void BAbstractSpinner::_ReservedAbstractSpinner18() {}
void BAbstractSpinner::_ReservedAbstractSpinner17() {}
void BAbstractSpinner::_ReservedAbstractSpinner16() {}
void BAbstractSpinner::_ReservedAbstractSpinner15() {}
void BAbstractSpinner::_ReservedAbstractSpinner14() {}
void BAbstractSpinner::_ReservedAbstractSpinner13() {}
void BAbstractSpinner::_ReservedAbstractSpinner12() {}
void BAbstractSpinner::_ReservedAbstractSpinner11() {}
void BAbstractSpinner::_ReservedAbstractSpinner10() {}
void BAbstractSpinner::_ReservedAbstractSpinner9() {}
void BAbstractSpinner::_ReservedAbstractSpinner8() {}
void BAbstractSpinner::_ReservedAbstractSpinner7() {}
void BAbstractSpinner::_ReservedAbstractSpinner6() {}
void BAbstractSpinner::_ReservedAbstractSpinner5() {}
void BAbstractSpinner::_ReservedAbstractSpinner4() {}
void BAbstractSpinner::_ReservedAbstractSpinner3() {}
void BAbstractSpinner::_ReservedAbstractSpinner2() {}
void BAbstractSpinner::_ReservedAbstractSpinner1() {}
