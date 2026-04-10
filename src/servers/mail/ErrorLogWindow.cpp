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
 */
/** @file ErrorLogWindow.cpp
 *  @brief Scrollable error log window for mail daemon status messages. */
#include <ControlLook.h>
#include <ScrollView.h>
#include <String.h>
#include <TextView.h>
#include <limits.h>

#include "ErrorLogWindow.h"

class Error : public BView {
	public:
		Error(BRect rect, alert_type type, const char *tag, const char *message, bool timestamp,
			rgb_color backgroundColor, rgb_color foregroundColor);

		void GetPreferredSize(float *width, float *height);
		void Draw(BRect updateRect);
		void FrameResized(float w, float h);
	private:
		alert_type type;
};


class ErrorPanel : public BView {
	public:
		ErrorPanel(BRect rect) : BView(rect, "ErrorScrollPanel", B_FOLLOW_ALL_SIDES,
			B_DRAW_ON_CHILDREN | B_FRAME_EVENTS), alerts_displayed(0), add_next_at(0)
		{
			AdoptSystemColors();
		}

		void GetPreferredSize(float *width, float *height) {
			*width = Bounds().Width();
			*height = add_next_at;
		}

		void TargetedByScrollView(BScrollView *scroll_view)
		{
			scroll = scroll_view;
		}

		void FrameResized(float w, float /*h*/) {
			add_next_at = 0;
			for (int32 i = 0; i < CountChildren(); i++) {
				ChildAt(i)->MoveTo(BPoint(0,add_next_at));
				ChildAt(i)->ResizeTo(w, ChildAt(i)->Frame().Height());
				ChildAt(i)->ResizeToPreferred();
				add_next_at += ChildAt(i)->Bounds().Height();
			}
		}
		int32 alerts_displayed;
		float add_next_at;
		BScrollView *scroll;
};


//	#pragma mark -


/**
 * @brief Constructs the error log window with a scrollable error panel.
 *
 * @param rect The initial window frame rectangle.
 * @param name The window title.
 * @param type The window type.
 */
ErrorLogWindow::ErrorLogWindow(BRect rect, const char *name, window_type type)
	:
	BWindow(rect, name, type, B_NO_WORKSPACE_ACTIVATION | B_NOT_MINIMIZABLE
		| B_ASYNCHRONOUS_CONTROLS),
	fIsRunning(false)
{
	SetColumnColors(ui_color(B_LIST_BACKGROUND_COLOR));
	fTextColor = ui_color(B_LIST_ITEM_TEXT_COLOR);

	rect = Bounds();
	rect.right -= be_control_look->GetScrollBarWidth(B_VERTICAL);

	view = new ErrorPanel(rect);
	AddChild(new BScrollView("ErrorScroller", view, B_FOLLOW_ALL_SIDES, 0, false, true));
	Show();
	Hide();
}


/**
 * @brief Adds an error or status message to the log window.
 *
 * Creates a new Error view with the message text, appends it to the
 * scrollable panel, updates the scroll bar range, and shows the window
 * if it was hidden.
 *
 * @param type      The alert severity type (info, warning, etc.).
 * @param message   The message text to display.
 * @param tag       Optional tag prefix for the message (e.g., account name).
 * @param timestamp If true, appends a timestamp to the message.
 */
void
ErrorLogWindow::AddError(alert_type type, const char *message, const char *tag, bool timestamp)
{
	ErrorPanel *panel = (ErrorPanel *)view;

	// first call?
	if (!fIsRunning) {
		fIsRunning = true;
		Show();
	}

	Lock();

	Error *newError = new Error(BRect(0, panel->add_next_at, panel->Bounds().right,
		panel->add_next_at + 1), type, tag, message, timestamp,
		(panel->alerts_displayed++ % 2 == 0) ? fColumnColor : fColumnAlternateColor, fTextColor);

	newError->ResizeToPreferred();
	panel->add_next_at += newError->Bounds().Height();
	panel->AddChild(newError);
	panel->ResizeToPreferred();

	if (panel->add_next_at > Frame().Height()) {
		BScrollBar *bar = panel->scroll->ScrollBar(B_VERTICAL);

		bar->SetRange(0, panel->add_next_at - Frame().Height());
		bar->SetSteps(1, Frame().Height());
		bar->SetProportion(Frame().Height() / panel->add_next_at);
	} else
		panel->scroll->ScrollBar(B_VERTICAL)->SetRange(0,0);

	if (IsHidden())
		Show();

	Unlock();
}


/**
 * @brief Handles system color change notifications to update column colors.
 *
 * @param message The incoming message.
 */
void
ErrorLogWindow::MessageReceived(BMessage* message)
{
	rgb_color color;
	if (message->what == B_COLORS_UPDATED) {
		if (message->FindColor(ui_color_name(B_LIST_BACKGROUND_COLOR), &color) == B_OK)
			SetColumnColors(color);

		if (message->FindColor(ui_color_name(B_LIST_ITEM_TEXT_COLOR), &color) == B_OK)
			fTextColor = color;
	}
	BWindow::MessageReceived(message);
}


/**
 * @brief Hides the window and removes all error entries instead of closing.
 *
 * @return Always returns false to prevent actual window destruction.
 */
bool
ErrorLogWindow::QuitRequested()
{
	Hide();

	while (view->CountChildren() != 0) {
		BView* child = view->ChildAt(0);
		view->RemoveChild(child);
		delete child;
	}

	ErrorPanel *panel = (ErrorPanel *)(view);
	panel->add_next_at = 0;
	panel->alerts_displayed = 0;

	view->ResizeToPreferred();
	return false;
}


/**
 * @brief Adjusts scroll bar range when the window is resized.
 *
 * @param newWidth  The new width of the window frame.
 * @param newHeight The new height of the window frame.
 */
void
ErrorLogWindow::FrameResized(float newWidth, float newHeight)
{
	ErrorPanel *panel = (ErrorPanel *)view;
	panel->Invalidate();

	if (panel->add_next_at > newHeight) {
		BScrollBar *bar = panel->scroll->ScrollBar(B_VERTICAL);

		bar->SetRange(0, panel->add_next_at - Frame().Height());
		bar->SetSteps(1, Frame().Height());
		bar->SetProportion(Frame().Height() / panel->add_next_at);
	} else
		panel->scroll->ScrollBar(B_VERTICAL)->SetRange(0,0);
}


/**
 * @brief Sets the alternating column background colors from a base color.
 *
 * @param color The base column background color.
 */
void
ErrorLogWindow::SetColumnColors(rgb_color color)
{
	fColumnColor = color;
	if (fColumnColor.IsDark())
		fColumnAlternateColor = tint_color(color, 0.85f);
	else
		fColumnAlternateColor = tint_color(color, B_DARKEN_2_TINT);
}


//	#pragma mark -


/**
 * @brief Constructs an error entry view with formatted message text.
 *
 * Creates a BTextView child displaying the message, optionally prefixed
 * with a bold tag and suffixed with a timestamp.
 *
 * @param rect            The initial view rectangle.
 * @param atype           The alert type (unused visually but stored).
 * @param tag             Optional bold prefix tag, or NULL.
 * @param message         The error/status message text.
 * @param timestamp       If true, appends the current time.
 * @param backgroundColor The background color for this entry row.
 * @param foregroundColor The text color.
 */
Error::Error(BRect rect, alert_type atype, const char *tag, const char *message,
	bool timestamp, rgb_color backgroundColor, rgb_color foregroundColor)
	:
	BView(rect,"error", B_FOLLOW_LEFT | B_FOLLOW_RIGHT | B_FOLLOW_TOP,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS), type(atype)
{
	SetViewColor(backgroundColor);
	SetLowColor(backgroundColor);

	text_run_array array;
	array.count = 1;
	array.runs[0].offset = 0;
	array.runs[0].font = *be_bold_font;
	array.runs[0].color = foregroundColor;

	BString msgString(message);
	msgString.RemoveAll("\r");

	BTextView *view = new BTextView(BRect(20, 0, rect.Width(), rect.Height()),
		"error_display", BRect(0,3,rect.Width() - 20 - 3, LONG_MAX),
		B_FOLLOW_ALL_SIDES);
	view->SetLowColor(backgroundColor);
	view->SetViewColor(backgroundColor);
	view->SetText(msgString.String());
	view->MakeSelectable(true);
	view->SetStylable(true);
	view->MakeEditable(false);

	if (tag != NULL) {
		BString tagString(tag);
		tagString += " ";
		view->Insert(0, tagString.String(), tagString.Length(), &array);
	}

	if (timestamp) {
		array.runs[0].color = foregroundColor.IsLight()
			? tint_color(foregroundColor, B_LIGHTEN_1_TINT)
			: tint_color(foregroundColor, B_DARKEN_2_TINT);
		array.runs[0].font.SetSize(9);
		time_t thetime = time(NULL);
		BString atime = asctime(localtime(&thetime));
		atime.Prepend(" [");
		atime.RemoveAll("\n");
		atime.Append("]");
		view->Insert(view->TextLength(),atime.String(),atime.Length(),&array);
	}

	float height,width;
	width = view->Frame().Width();
	height = view->TextHeight(0,view->CountLines()) + 3;
	view->ResizeTo(width,height);
	AddChild(view);
}


/**
 * @brief Calculates the preferred size based on the enclosed text height.
 *
 * @param width  Output: the preferred width.
 * @param height Output: the preferred height.
 */
void
Error::GetPreferredSize(float *width, float *height)
{
	BTextView *view = static_cast<BTextView *>(FindView("error_display"));

	*width = view->Frame().Width() + 20;
	*height = view->TextHeight(0, INT32_MAX) + 3;
}


/**
 * @brief Draws the error entry background.
 *
 * @param updateRect The rectangle that needs redrawing.
 */
void
Error::Draw(BRect updateRect)
{
	FillRect(updateRect, B_SOLID_LOW);
}


/**
 * @brief Resizes the enclosed text view to match the new frame dimensions.
 *
 * @param w The new width.
 * @param h The new height.
 */
void
Error::FrameResized(float w, float h)
{
	BTextView *view = static_cast<BTextView *>(FindView("error_display"));

	view->ResizeTo(w - 20, h);
	view->SetTextRect(BRect(0, 3, w - 20, h));
}
