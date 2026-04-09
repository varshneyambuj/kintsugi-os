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
 *   Copyright 2009-2024 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm, darkwyrm@earthlink.net
 *       John Scipione, jscipione@gmail.com
 */

/** @file ColorPreview.cpp
 *  @brief Implements BColorPreview, a BControl subclass that displays a solid
 *         color swatch and supports drag-and-drop of rgb_color values.
 */


#include <ColorPreview.h>

#include <stdio.h>

#include <algorithm>
#include <iostream>

#include <Application.h>
#include <Bitmap.h>
#include <Message.h>
#include <MessageRunner.h>
#include <String.h>
#include <Window.h>


static const int32 kMsgMessageRunner = 'MsgR';

static const rgb_color kRed = make_color(255, 0, 0, 255);


namespace BPrivate {


/** @brief Constructs a BColorPreview control initialized to red.
 *  @param name     The internal name of the view.
 *  @param label    The optional text label (passed to BControl).
 *  @param message  The message sent when the color is invoked.
 *  @param flags    View flags passed to BControl.
 */
BColorPreview::BColorPreview(const char* name, const char* label, BMessage* message, uint32 flags)
	:
	BControl(name, label, message, flags),
	fColor(kRed),
	fMessageRunner(NULL)
{
	SetExplicitSize(BSize(roundf(StringWidth("M") * 6), roundf(StringWidth("M") * 6)));
	SetExplicitAlignment(BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER));
}


/** @brief Destructor. */
BColorPreview::~BColorPreview()
{
}


/** @brief Draws the color swatch with a recessed border.
 *
 *  Renders a two-ring inset border using panel background tints and fills
 *  the inner rectangle with the current color.
 *
 *  @param updateRect  The dirty rectangle that needs redrawing.
 */
void
BColorPreview::Draw(BRect updateRect)
{
	rgb_color color = fColor;

	rgb_color bg = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color outer = tint_color(bg, B_DARKEN_1_TINT);
	rgb_color inner = tint_color(bg, B_DARKEN_3_TINT);
	rgb_color light = tint_color(bg, B_LIGHTEN_MAX_TINT);

	BRect bounds(Bounds());

	BeginLineArray(4);
	AddLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.left, bounds.top), outer);
	AddLine(BPoint(bounds.left + 1, bounds.top), BPoint(bounds.right, bounds.top), outer);
	AddLine(BPoint(bounds.right, bounds.top + 1), BPoint(bounds.right, bounds.bottom), light);
	AddLine(BPoint(bounds.right - 1, bounds.bottom), BPoint(bounds.left + 1, bounds.bottom),
		light);
	EndLineArray();

	bounds.InsetBy(1, 1);

	BeginLineArray(4);
	AddLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.left, bounds.top), inner);
	AddLine(BPoint(bounds.left + 1, bounds.top), BPoint(bounds.right, bounds.top), inner);
	AddLine(BPoint(bounds.right, bounds.top + 1), BPoint(bounds.right, bounds.bottom), bg);
	AddLine(BPoint(bounds.right - 1, bounds.bottom), BPoint(bounds.left + 1, bounds.bottom), bg);
	EndLineArray();

	bounds.InsetBy(1, 1);

	SetHighColor(color);
	FillRect(bounds);
}


/** @brief Invokes the control, attaching the current color to the message.
 *
 *  If no message is supplied a B_PASTE message is created.  The current
 *  color is added under the key "RGBColor" when not already present.
 *
 *  @param message  Optional message to send; may be NULL.
 *  @return Status code from BControl::Invoke().
 */
status_t
BColorPreview::Invoke(BMessage* message)
{
	if (message == NULL)
		message = new BMessage(B_PASTE);

	if (message->CountNames(B_RGB_COLOR_TYPE) == 0)
		message->AddData("RGBColor", B_RGB_COLOR_TYPE, &fColor, sizeof(fColor));

	return BControl::Invoke(message);
}


/** @brief Handles dropped color messages and timer-triggered drag operations.
 *
 *  Accepts a dropped message containing an "RGBColor" field and updates the
 *  displayed color.  Also responds to the internal kMsgMessageRunner message
 *  that triggers a drag after the mouse has been held down long enough.
 *
 *  @param message  The incoming message.
 */
void
BColorPreview::MessageReceived(BMessage* message)
{
	if (message->WasDropped()) {
		char* name;
		type_code type;
		rgb_color* color;
		ssize_t size;
		if (message->GetInfo(B_RGB_COLOR_TYPE, 0, &name, &type) == B_OK
			&& message->FindData(name, type, (const void**)&color, &size) == B_OK) {
			SetColor(*color);
			Invoke(message);
		}
	}

	switch (message->what) {
		case kMsgMessageRunner:
		{
			BPoint where;
			uint32 buttons;
			GetMouse(&where, &buttons);

			_DragColor(where);
			break;
		}

		default:
			BControl::MessageReceived(message);
			break;
	}
}


/** @brief Handles mouse-down by starting the long-press timer.
 *  @param where  The position of the mouse cursor (view coordinates).
 */
void
BColorPreview::MouseDown(BPoint where)
{
	fMessageRunner = new BMessageRunner(this, new BMessage(kMsgMessageRunner), 300000, 1);

	BControl::MouseDown(where);
}


/** @brief Handles mouse movement; initiates a drag if the timer has fired.
 *  @param where    The current mouse position (view coordinates).
 *  @param transit  One of the B_ENTERED_VIEW / B_INSIDE_VIEW / etc. constants.
 *  @param message  Any drag message currently in flight, or NULL.
 */
void
BColorPreview::MouseMoved(BPoint where, uint32 transit, const BMessage* message)
{
	if (fMessageRunner != NULL)
		_DragColor(where);

	BControl::MouseMoved(where, transit, message);
}


/** @brief Handles mouse-up by cancelling the long-press timer.
 *  @param where  The position of the mouse cursor when the button was released.
 */
void
BColorPreview::MouseUp(BPoint where)
{
	delete fMessageRunner;
	fMessageRunner = NULL;

	BControl::MouseUp(where);
}


/** @brief Returns the current preview color.
 *  @return The rgb_color currently shown in the swatch.
 */
rgb_color
BColorPreview::Color() const
{
	return fColor;
}


/** @brief Sets the preview color and redraws the swatch.
 *
 *  The alpha component is forced to 255 (fully opaque).
 *
 *  @param color  The new color to display.
 */
void
BColorPreview::SetColor(rgb_color color)
{
	color.alpha = 255;
	fColor = color;

	Invalidate();
}


/** @brief Initiates a drag operation carrying the current color.
 *
 *  Builds a B_PASTE message with "text/plain" (hex string) and "RGBColor"
 *  fields, attaches a small rendered color-chip bitmap, and starts the drag.
 *  Calls MouseUp() to clean up the press state after starting the drag.
 *
 *  @param where  The view-coordinate point from which the drag originates.
 */
void
BColorPreview::_DragColor(BPoint where)
{
	BString hexStr;
	hexStr.SetToFormat("#%.2X%.2X%.2X", fColor.red, fColor.green, fColor.blue);

	BMessage message(B_PASTE);
	message.AddData("text/plain", B_MIME_TYPE, hexStr.String(), hexStr.Length());
	message.AddData("RGBColor", B_RGB_COLOR_TYPE, &fColor, sizeof(fColor));
	message.AddMessenger("be:sender", BMessenger(this));
	message.AddPointer("source", this);
	message.AddInt64("when", (int64)system_time());

	BRect rect(0, 0, 16, 16);

	BBitmap* bitmap = new BBitmap(rect, B_RGB32, true);
	if (bitmap->Lock()) {
		BView* view = new BView(rect, "", B_FOLLOW_NONE, B_WILL_DRAW);
		bitmap->AddChild(view);

		view->SetHighColor(B_TRANSPARENT_COLOR);
		view->FillRect(view->Bounds());

		++rect.top;
		++rect.left;

		view->SetHighColor(0, 0, 0, 100);
		view->FillRect(rect);
		rect.OffsetBy(-1, -1);

		int32 red = std::min(255, (int)(1.2 * fColor.red + 40));
		int32 green = std::min(255, (int)(1.2 * fColor.green + 40));
		int32 blue = std::min(255, (int)(1.2 * fColor.blue + 40));

		view->SetHighColor(red, green, blue);
		view->StrokeRect(rect);

		++rect.left;
		++rect.top;

		red = (int32)(0.8 * fColor.red);
		green = (int32)(0.8 * fColor.green);
		blue = (int32)(0.8 * fColor.blue);

		view->SetHighColor(red, green, blue);
		view->StrokeRect(rect);

		--rect.right;
		--rect.bottom;

		view->SetHighColor(fColor.red, fColor.green, fColor.blue);
		view->FillRect(rect);
		view->Sync();

		bitmap->Unlock();
	}

	DragMessage(&message, bitmap, B_OP_ALPHA, BPoint(14.0f, 14.0f));

	MouseUp(where);
}


} // namespace BPrivate
