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
 *   Copyright 2019-2025, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 *       Pawan Yerramilli <me@pawanyerramilli.com>
 *       Samuel Rodríguez Pérez <samuelrp84@gmail.com>
 */


/**
 * @file InputTouchpadPrefView.cpp
 * @brief Implementation of TouchpadView and TouchpadPrefView.
 *
 * TouchpadView is a small custom BView showing the touchpad area with
 * draggable scroll-zone delimiters. TouchpadPrefView is the full
 * touchpad preferences card built around it: scroll behaviour, edge
 * motion, click handling, tapping sensitivity, padblocker, and
 * speed/acceleration sliders. The card persists changes through the
 * embedded TouchpadPref model.
 *
 * @see TouchpadPref
 */


#include "InputTouchpadPrefView.h"

#include <stdio.h>

#include <Alert.h>
#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <File.h>
#include <FindDirectory.h>
#include <Input.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <Window.h>

#include <keyboard_mouse_driver.h>


/** @brief Internal message: user is dragging the X scroll-zone delimiter. */
const uint32 SCROLL_X_DRAG = 'sxdr';
/** @brief Internal message: user is dragging the Y scroll-zone delimiter. */
const uint32 SCROLL_Y_DRAG = 'sydr';

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TouchpadPrefView"


/**
 * @brief Constructs the touchpad preview area.
 *
 * Initialises off-screen drawing state, copies @a frame as the preferred
 * frame, and seeds the scroll-zone delimiters at the right and bottom of
 * the inset pad rectangle (no scroll zones reserved by default).
 *
 * @param frame  Initial bounding rectangle of the preview view.
 */
TouchpadView::TouchpadView(BRect frame)
	:
	BView(frame, "TouchpadView", B_FOLLOW_NONE, B_WILL_DRAW)
{
	fXTracking = false;
	fYTracking = false;
	fOffScreenView = NULL;
	fOffScreenBitmap = NULL;

	fPrefRect = frame;
	fPadRect = fPrefRect;
	fPadRect.InsetBy(10, 10);
	fXScrollRange = fPadRect.Width();
	fYScrollRange = fPadRect.Height();
}


/**
 * @brief Destroys the preview, releasing the off-screen bitmap.
 *
 * @note The off-screen BView is owned by the BBitmap and is released
 *       transitively when the bitmap is deleted.
 */
TouchpadView::~TouchpadView()
{
	delete fOffScreenBitmap;
}


/**
 * @brief Paints the view by delegating to DrawSliders().
 *
 * @param updateRect  Update rectangle (unused; the whole view is redrawn).
 */
void
TouchpadView::Draw(BRect updateRect)
{
	DrawSliders();
}


/**
 * @brief Starts tracking when the user clicks a scroll-zone delimiter.
 *
 * Sets fXTracking or fYTracking and remembers the current scroll-range
 * values so the drag can be undone if the user opts to abort a large
 * scroll-zone change in MouseUp.
 *
 * @param point  Mouse-down position in view-local coordinates.
 */
void
TouchpadView::MouseDown(BPoint point)
{
	if (fXScrollDragZone.Contains(point)) {
		fXTracking = true;
		fOldXScrollRange = fXScrollRange;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	}

	if (fYScrollDragZone.Contains(point)) {
		fYTracking = true;
		fOldYScrollRange = fYScrollRange;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	}
}


/**
 * @brief Commits or aborts a scroll-zone drag on mouse-up.
 *
 * If the resulting zone exceeds 70% of the pad area the user is asked to
 * confirm. On confirmation a SCROLL_AREA_CHANGED notification is invoked;
 * on cancellation the previous range is restored and the view is
 * redrawn. No-op if neither axis is being tracked.
 *
 * @param point  Mouse-up position in view-local coordinates (unused).
 */
void
TouchpadView::MouseUp(BPoint point)
{
	if (!fXTracking && !fYTracking)
		return;

	fXTracking = false;
	fYTracking = false;

	const float kSoftScrollLimit = 0.7;

	int32 result = 0;
	if (GetRightScrollRatio() > kSoftScrollLimit
		|| GetBottomScrollRatio() > kSoftScrollLimit) {
		BAlert* alert = new BAlert(B_TRANSLATE("Please confirm"),
			B_TRANSLATE(
				"The new scroll area is very large and may impede "
				"normal mouse operation. Do you really want to change it?"),
			B_TRANSLATE("OK"), B_TRANSLATE("Cancel"), NULL, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		result = alert->Go();
	}

	if (result == 0) {
		BMessage msg(SCROLL_AREA_CHANGED);
		Invoke(&msg);
	} else {
		if (GetRightScrollRatio() > kSoftScrollLimit)
			fXScrollRange = fOldXScrollRange;
		if (GetBottomScrollRatio() > kSoftScrollLimit)
			fYScrollRange = fOldYScrollRange;
		DrawSliders();
	}
}


/**
 * @brief Allocates the off-screen bitmap used for double buffering.
 *
 * Creates a same-sized BView and an 8bpp BBitmap on first attach, parents
 * the view inside the bitmap, and uses the pair to render the touchpad
 * preview without flicker.
 */
void
TouchpadView::AttachedToWindow()
{
	if (!fOffScreenView)
		fOffScreenView = new BView(Bounds(), "", B_FOLLOW_ALL, B_WILL_DRAW);

	if (!fOffScreenBitmap) {
		fOffScreenBitmap = new BBitmap(Bounds(), B_CMAP8, true, false);

		if (fOffScreenBitmap && fOffScreenView)
			fOffScreenBitmap->AddChild(fOffScreenView);
	}
}


/**
 * @brief Updates the X and Y scroll-range delimiters and redraws.
 *
 * Converts the per-side scroll ratios from the model into absolute
 * coordinates inside the pad rectangle so DrawSliders() can place the
 * delimiter lines correctly.
 *
 * @param rightRange   Fraction of the pad width reserved for vertical
 *                     scrolling along the right edge (0.0-1.0).
 * @param bottomRange  Fraction of the pad height reserved for horizontal
 *                     scrolling along the bottom edge (0.0-1.0).
 */
void
TouchpadView::SetValues(float rightRange, float bottomRange)
{
	fXScrollRange = fPadRect.Width() * (1 - rightRange);
	fYScrollRange = fPadRect.Height() * (1 - bottomRange);
	Invalidate();
}


/**
 * @brief Reports the preferred size of the preview view.
 *
 * @param width   On return: preferred width in pixels (may be NULL).
 * @param height  On return: preferred height in pixels (may be NULL).
 */
void
TouchpadView::GetPreferredSize(float* width, float* height)
{
	if (width != NULL)
		*width = fPrefRect.Width();
	if (height != NULL)
		*height = fPrefRect.Height();
}


/**
 * @brief Updates scroll ranges in real time while the user drags.
 *
 * Clamps the drag position to the pad rectangle and redraws the
 * delimiters; the model is updated only on mouse-up so that an aborted
 * drag does not perturb the persisted settings.
 *
 * @param point    Current mouse position in view-local coordinates.
 * @param transit  Transit code (unused).
 * @param message  Drag message (unused).
 */
void
TouchpadView::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	if (fXTracking) {
		if (point.x > fPadRect.right)
			fXScrollRange = fPadRect.Width();
		else if (point.x < fPadRect.left)
			fXScrollRange = 0;
		else
			fXScrollRange = point.x - fPadRect.left;

		DrawSliders();
	}

	if (fYTracking) {
		if (point.y > fPadRect.bottom)
			fYScrollRange = fPadRect.Height();
		else if (point.y < fPadRect.top)
			fYScrollRange = 0;
		else
			fYScrollRange = point.y - fPadRect.top;

		DrawSliders();
	}
}


/**
 * @brief Renders the pad rectangle and scroll-zone delimiters off-screen.
 *
 * Draws the pad outline, fills the active scroll zones, and strokes the
 * X and Y delimiter lines plus their drag handles. The composite is then
 * blitted onto the on-screen view in one DrawBitmap call to avoid
 * flicker. Caches the drag-zone rectangles for use by MouseDown.
 */
void
TouchpadView::DrawSliders()
{
	BView* view = fOffScreenView != NULL ? fOffScreenView : this;

	if (!LockLooper())
		return;

	if (fOffScreenBitmap->Lock()) {
		view->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		view->FillRect(Bounds());
		view->SetHighColor(100, 100, 100);
		view->FillRoundRect(fPadRect, 4, 4);

		int32 dragSize = 3; // half drag size

		// scroll areas
		view->SetHighColor(145, 100, 100);
		BRect rightRect(fPadRect.left + fXScrollRange, fPadRect.top,
			fPadRect.right, fPadRect.bottom);
		view->FillRoundRect(rightRect, 4, 4);

		BRect bottomRect(fPadRect.left, fPadRect.top + fYScrollRange,
			fPadRect.right, fPadRect.bottom);
		view->FillRoundRect(bottomRect, 4, 4);

		// Stroke Rect
		view->SetHighColor(100, 100, 100);
		view->SetPenSize(2);
		view->StrokeRoundRect(fPadRect, 4, 4);

		// x scroll range line
		view->SetHighColor(200, 0, 0);
		view->StrokeLine(BPoint(fPadRect.left + fXScrollRange, fPadRect.top),
			BPoint(fPadRect.left + fXScrollRange, fPadRect.bottom));

		fXScrollDragZone = BRect(fPadRect.left + fXScrollRange - dragSize,
			fPadRect.top - dragSize, fPadRect.left + fXScrollRange + dragSize,
			fPadRect.bottom + dragSize);
		BRect xscrollDragZone1 = BRect(fPadRect.left + fXScrollRange - dragSize,
			fPadRect.top - dragSize, fPadRect.left + fXScrollRange + dragSize,
			fPadRect.top + dragSize);
		view->FillRect(xscrollDragZone1);
		BRect xscrollDragZone2 = BRect(fPadRect.left + fXScrollRange - dragSize,
			fPadRect.bottom - dragSize,
			fPadRect.left + fXScrollRange + dragSize,
			fPadRect.bottom + dragSize);
		view->FillRect(xscrollDragZone2);

		// y scroll range line
		view->StrokeLine(BPoint(fPadRect.left, fPadRect.top + fYScrollRange),
			BPoint(fPadRect.right, fPadRect.top + fYScrollRange));

		fYScrollDragZone = BRect(fPadRect.left - dragSize,
			fPadRect.top + fYScrollRange - dragSize, fPadRect.right + dragSize,
			fPadRect.top + fYScrollRange + dragSize);
		BRect yscrollDragZone1 = BRect(fPadRect.left - dragSize,
			fPadRect.top + fYScrollRange - dragSize, fPadRect.left + dragSize,
			fPadRect.top + fYScrollRange + dragSize);
		view->FillRect(yscrollDragZone1);
		BRect yscrollDragZone2 = BRect(fPadRect.right - dragSize,
			fPadRect.top + fYScrollRange - dragSize, fPadRect.right + dragSize,
			fPadRect.top + fYScrollRange + dragSize);
		view->FillRect(yscrollDragZone2);

		view->Sync();
		fOffScreenBitmap->Unlock();
		DrawBitmap(fOffScreenBitmap, B_ORIGIN);
	}

	UnlockLooper();
}


//	#pragma mark - TouchpadPrefView


/**
 * @brief Constructs the full touchpad settings card.
 *
 * Builds the inner control hierarchy via SetupView() and seeds every
 * widget from the persisted TouchpadPref::Settings.
 *
 * @param dev  BInputDevice for the touchpad. Ownership is forwarded into
 *             the embedded TouchpadPref.
 */
TouchpadPrefView::TouchpadPrefView(BInputDevice* dev)
	:
	BGroupView(),
	fTouchpadPref(dev)
{
	SetupView();
	// set view values
	SetValues(&fTouchpadPref.Settings());
}


/**
 * @brief Destroys the card.
 *
 * @note The TouchpadPref destructor handles persistence; nothing extra
 *       is required here.
 */
TouchpadPrefView::~TouchpadPrefView()
{
}


/**
 * @brief Translates control changes into TouchpadPref updates.
 *
 * Handles every touchpad-specific message: scroll area drags, scroll
 * controls, edge motion, finger click, software button areas, tap
 * sensitivity, padblocker, speed and acceleration, plus Defaults and
 * Revert. Every change re-enables the Revert button and pushes the live
 * settings to the input server.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BView::MessageReceived.
 */
void
TouchpadPrefView::MessageReceived(BMessage* message)
{
	touchpad_settings& settings = fTouchpadPref.Settings();

	switch (message->what) {
		case SCROLL_AREA_CHANGED:
			settings.scroll_rightrange = fTouchpadView->GetRightScrollRatio();
			settings.scroll_bottomrange = fTouchpadView->GetBottomScrollRatio();
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case SCROLL_CONTROL_CHANGED:
			settings.scroll_reverse = fScrollReverseBox->Value() == B_CONTROL_ON;
			settings.scroll_twofinger = fTwoFingerBox->Value() == B_CONTROL_ON;
			settings.scroll_twofinger_horizontal
				= fTwoFingerHorizontalBox->Value() == B_CONTROL_ON;
			settings.scroll_twofinger_natural_scrolling
				= fTwoFingerNaturalScrollingBox->Value() == B_CONTROL_ON;
			settings.scroll_acceleration = fScrollAccelSlider->Value();
			settings.scroll_xstepsize = (20 - fScrollStepXSlider->Value()) * 3;
			settings.scroll_ystepsize = (20 - fScrollStepYSlider->Value()) * 3;
			fTwoFingerHorizontalBox->SetEnabled(settings.scroll_twofinger);
			fTwoFingerNaturalScrollingBox->SetEnabled(settings.scroll_twofinger);
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case EDGE_MOTION_CHANGED:
			settings.edge_motion = fEdgeMotionOptionPopUp->Value();
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case FINGER_CLICK_CHANGED:
			settings.finger_click = fFingerClickBox->Value() == B_CONTROL_ON;
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case SOFTWARE_BUTTON_AREAS_CHANGED:
			settings.software_button_areas = fSoftwareButtonAreasBox->Value() == B_CONTROL_ON;
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case TAP_CONTROL_CHANGED:
			settings.tapgesture_sensibility = fTapSlider->Value();
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case PADBLOCK_TIME_CHANGED:
			settings.padblocker_threshold = fPadBlockerSlider->Value();
			// The maximum value means "disabled", but in the settings file that
			// must be stored as 0
			if (settings.padblocker_threshold == 1000)
				settings.padblocker_threshold = 0;
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			break;

		case PAD_SPEED_CHANGED:
		{
			fTouchpadPref.SetSpeed(fSpeedSlider->Value());
			fRevertButton->SetEnabled(true);
			break;
		}
		case PAD_ACCELERATION_CHANGED:
		{
			fTouchpadPref.SetAcceleration(fAccelSlider->Value());
			fRevertButton->SetEnabled(true);
			break;
		}
		case DEFAULT_SETTINGS:
			fTouchpadPref.Defaults();
			fRevertButton->SetEnabled(true);
			fTouchpadPref.UpdateRunningSettings();
			SetValues(&settings);
			break;

		case REVERT_SETTINGS:
			fTouchpadPref.Revert();
			fTouchpadPref.UpdateRunningSettings();
			fRevertButton->SetEnabled(false);
			SetValues(&settings);
			break;

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Wires every control to this view as the message target.
 *
 * Also resizes the parent window to the view's preferred size and
 * restores the saved window position; if the saved position is the
 * sentinel (-1, -1) the window is centred.
 */
void
TouchpadPrefView::AttachedToWindow()
{
	fTouchpadView->SetTarget(this);
	fScrollReverseBox->SetTarget(this);
	fTwoFingerBox->SetTarget(this);
	fTwoFingerHorizontalBox->SetTarget(this);
	fTwoFingerNaturalScrollingBox->SetTarget(this);
	fScrollStepXSlider->SetTarget(this);
	fScrollStepYSlider->SetTarget(this);
	fScrollAccelSlider->SetTarget(this);

	fEdgeMotionOptionPopUp->SetTarget(this);
	fFingerClickBox->SetTarget(this);
	fSoftwareButtonAreasBox->SetTarget(this);

	fPadBlockerSlider->SetTarget(this);
	fTapSlider->SetTarget(this);
	fSpeedSlider->SetTarget(this);
	fAccelSlider->SetTarget(this);

	fDefaultButton->SetTarget(this);
	fRevertButton->SetTarget(this);

	BSize size = PreferredSize();
	Window()->ResizeTo(size.width, size.height);

	BPoint position = fTouchpadPref.WindowPosition();
	// center window on screen if it had a bad position
	if (position.x < 0 && position.y < 0)
		Window()->CenterOnScreen();
	else
		Window()->MoveTo(position);
}


/**
 * @brief Persists the window position back into the TouchpadPref model.
 */
void
TouchpadPrefView::DetachedFromWindow()
{
	fTouchpadPref.SetWindowPosition(Window()->Frame().LeftTop());
}


/**
 * @brief Builds the entire touchpad control hierarchy.
 *
 * Constructs the scrolling box (preview, two-finger checkboxes, scroll
 * sliders), the edge motion option pop-up, finger click and software
 * button area checkboxes, the tap sensitivity and padblocker sliders,
 * the trackpad speed and acceleration sliders, and the Defaults/Revert
 * buttons. All controls share the message codes defined in the header.
 */
void
TouchpadPrefView::SetupView()
{
	SetLayout(new BGroupLayout(B_VERTICAL));
	BBox* scrollBox = new BBox("Touchpad");
	scrollBox->SetLabel(B_TRANSLATE("Scrolling"));


	fTouchpadView = new TouchpadView(BRect(0, 0, 130, 120));
	fTouchpadView->SetExplicitMaxSize(BSize(130, 120));

	// Create the scrolling acceleration slider...
	fScrollAccelSlider = new BSlider("scroll_accel",
		B_TRANSLATE("Acceleration"),
		new BMessage(SCROLL_CONTROL_CHANGED), 0, 20, B_HORIZONTAL);
	fScrollAccelSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fScrollAccelSlider->SetHashMarkCount(7);
	fScrollAccelSlider->SetLimitLabels(
		B_TRANSLATE("Low"), B_TRANSLATE("High"));
	fScrollAccelSlider->SetExplicitMinSize(BSize(150, B_SIZE_UNSET));

	fScrollStepXSlider = new BSlider("scroll_stepX", B_TRANSLATE("Horizontal"),
		new BMessage(SCROLL_CONTROL_CHANGED), 0, 20, B_HORIZONTAL);
	fScrollStepXSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fScrollStepXSlider->SetHashMarkCount(7);
	fScrollStepXSlider->SetLimitLabels(
		B_TRANSLATE("Slow"), B_TRANSLATE("Fast"));

	fScrollStepYSlider = new BSlider("scroll_stepY", B_TRANSLATE("Vertical"),
		new BMessage(SCROLL_CONTROL_CHANGED), 0, 20, B_HORIZONTAL);
	fScrollStepYSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fScrollStepYSlider->SetHashMarkCount(7);
	fScrollStepYSlider->SetLimitLabels(
		B_TRANSLATE("Slow"), B_TRANSLATE("Fast"));

	fPadBlockerSlider
		= new BSlider("padblocker", B_TRANSLATE("Keyboard lock delay"),
			new BMessage(PADBLOCK_TIME_CHANGED), 5, 1000, B_HORIZONTAL);
	fPadBlockerSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fPadBlockerSlider->SetHashMarkCount(10);
	fPadBlockerSlider->SetLimitLabels(
		B_TRANSLATE("Quick"), B_TRANSLATE("Never"));

	fScrollReverseBox = new BCheckBox(B_TRANSLATE("Reverse scroll direction"),
		new BMessage(SCROLL_CONTROL_CHANGED));
	fTwoFingerBox = new BCheckBox(B_TRANSLATE("Two finger scrolling"),
		new BMessage(SCROLL_CONTROL_CHANGED));
	fTwoFingerHorizontalBox = new BCheckBox(B_TRANSLATE("Horizontal scrolling"),
		new BMessage(SCROLL_CONTROL_CHANGED));
	fTwoFingerNaturalScrollingBox = new BCheckBox(B_TRANSLATE("Natural scrolling"),
		new BMessage(SCROLL_CONTROL_CHANGED));

	fEdgeMotionOptionPopUp = new BOptionPopUp("edge_motion",
		B_TRANSLATE("Edge motion:"), new BMessage(EDGE_MOTION_CHANGED));
	fEdgeMotionOptionPopUp->AddOption(B_TRANSLATE("Disabled"), B_EDGE_MOTION_DISABLED);
#if 0
	// Not exposed in the UI because it makes little sense to have this enabled on move but not
	// on drag
	fEdgeMotionOptionPopUp->AddOption(B_TRANSLATE("On move"), B_EDGE_MOTION_ON_MOVE);
#endif
	fEdgeMotionOptionPopUp->AddOption(B_TRANSLATE("On tap-drag only"), B_EDGE_MOTION_ON_TAP_DRAG);
	fEdgeMotionOptionPopUp->AddOption(B_TRANSLATE("When dragging"),
		B_EDGE_MOTION_ON_TAP_DRAG
		| B_EDGE_MOTION_ON_BUTTON_CLICK_MOVE | B_EDGE_MOTION_ON_BUTTON_CLICK_DRAG);
	fEdgeMotionOptionPopUp->AddOption(B_TRANSLATE("Always"),
		B_EDGE_MOTION_ON_MOVE | B_EDGE_MOTION_ON_TAP_DRAG
		| B_EDGE_MOTION_ON_BUTTON_CLICK_MOVE | B_EDGE_MOTION_ON_BUTTON_CLICK_DRAG);

	fFingerClickBox = new BCheckBox(B_TRANSLATE("Finger click"),
		new BMessage(FINGER_CLICK_CHANGED));
	fSoftwareButtonAreasBox = new BCheckBox(B_TRANSLATE("Software button areas"),
		new BMessage(SOFTWARE_BUTTON_AREAS_CHANGED));


	float spacing = be_control_look->DefaultItemSpacing();

	BView* scrollPrefLeftLayout
		= BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(fTouchpadView)
		.AddStrut(spacing)
		.Add(fScrollReverseBox)
		.Add(fTwoFingerBox)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(spacing * 2, 0, 0, 0)
			.Add(fTwoFingerHorizontalBox)
			.Add(fTwoFingerNaturalScrollingBox)
		.End()
		.AddGlue()
		.View();

	BGroupView* scrollPrefRightLayout = new BGroupView(B_VERTICAL);
	scrollPrefRightLayout->AddChild(fScrollAccelSlider);
	scrollPrefRightLayout->AddChild(fScrollStepXSlider);
	scrollPrefRightLayout->AddChild(fScrollStepYSlider);

	BGroupLayout* scrollPrefLayout = new BGroupLayout(B_HORIZONTAL);
	scrollPrefLayout->SetSpacing(spacing);
	scrollPrefLayout->SetInsets(
		spacing, scrollBox->TopBorderOffset() * 2 + spacing, spacing, spacing);
	scrollBox->SetLayout(scrollPrefLayout);

	scrollPrefLayout->AddView(scrollPrefLeftLayout);
	scrollPrefLayout->AddItem(
		BSpaceLayoutItem::CreateVerticalStrut(spacing * 1.5));
	scrollPrefLayout->AddView(scrollPrefRightLayout);

	fTapSlider = new BSlider("tap_sens", B_TRANSLATE("Tapping sensitivity"),
		new BMessage(TAP_CONTROL_CHANGED), 0, 50, B_HORIZONTAL);
	fTapSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fTapSlider->SetHashMarkCount(11);
	fTapSlider->SetLimitLabels(B_TRANSLATE("Off"), B_TRANSLATE("High"));

	fSpeedSlider = new BSlider("pad_speed", B_TRANSLATE("Trackpad speed"),
		new BMessage(PAD_SPEED_CHANGED), 0, 1000, B_HORIZONTAL);
	fSpeedSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fSpeedSlider->SetHashMarkCount(7);
	fSpeedSlider->SetLimitLabels(B_TRANSLATE("Slow"), B_TRANSLATE("Fast"));

	fAccelSlider = new BSlider("pad_accel", B_TRANSLATE("Trackpad acceleration"),
		new BMessage(PAD_ACCELERATION_CHANGED), 0, 1000, B_HORIZONTAL);
	fAccelSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAccelSlider->SetHashMarkCount(7);
	fAccelSlider->SetLimitLabels(B_TRANSLATE("Low"), B_TRANSLATE("High"));

	fDefaultButton
		= new BButton(B_TRANSLATE("Defaults"), new BMessage(DEFAULT_SETTINGS));

	fRevertButton
		= new BButton(B_TRANSLATE("Revert"), new BMessage(REVERT_SETTINGS));
	fRevertButton->SetEnabled(false);


	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scrollBox)
		.Add(fEdgeMotionOptionPopUp)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fFingerClickBox)
			.Add(new BSeparatorView(B_VERTICAL))
			.Add(fSoftwareButtonAreasBox)
			.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
				.Add(fTapSlider)
				.Add(fPadBlockerSlider)
				.End()
			.Add(new BSeparatorView(B_VERTICAL))
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
				.Add(fSpeedSlider)
				.Add(fAccelSlider)
				.End()
			.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
			.AddGroup(B_HORIZONTAL)
			.Add(fDefaultButton)
			.Add(fRevertButton)
			.AddGlue()
			.End()
		.End();
}


/**
 * @brief Pushes the supplied settings into every UI control.
 *
 * Used both at construction (to seed widgets from the loaded settings)
 * and after Defaults/Revert (to refresh them from the new model state).
 * The trackpad speed and acceleration sliders are mapped through the
 * inverse of the curves used in TouchpadPref::SetSpeed and SetAcceleration.
 *
 * @param settings  Settings whose values should be reflected in the UI.
 */
void
TouchpadPrefView::SetValues(touchpad_settings* settings)
{
	fTouchpadView->SetValues(settings->scroll_rightrange, settings->scroll_bottomrange);
	fScrollReverseBox->SetValue(settings->scroll_reverse ? B_CONTROL_ON : B_CONTROL_OFF);
	fTwoFingerBox->SetValue(settings->scroll_twofinger ? B_CONTROL_ON : B_CONTROL_OFF);
	fTwoFingerHorizontalBox->SetValue(
		settings->scroll_twofinger_horizontal ? B_CONTROL_ON : B_CONTROL_OFF);
	fTwoFingerHorizontalBox->SetEnabled(settings->scroll_twofinger);
	fTwoFingerNaturalScrollingBox->SetValue(
		settings->scroll_twofinger_natural_scrolling ? B_CONTROL_ON : B_CONTROL_OFF);
	fTwoFingerNaturalScrollingBox->SetEnabled(settings->scroll_twofinger);
	fFingerClickBox->SetValue(settings->finger_click);
	fSoftwareButtonAreasBox->SetValue(settings->software_button_areas);
	fEdgeMotionOptionPopUp->SetValue(settings->edge_motion);
	fScrollStepXSlider->SetValue(20 - settings->scroll_xstepsize / 2);
	fScrollStepYSlider->SetValue(20 - settings->scroll_ystepsize / 2);
	fScrollAccelSlider->SetValue(settings->scroll_acceleration);
	fTapSlider->SetValue(settings->tapgesture_sensibility);
	fPadBlockerSlider->SetValue(settings->padblocker_threshold);
	int32 value = int32((log(settings->trackpad_speed / 8192.0) / log(2)) * 1000 / 6);
	fSpeedSlider->SetValue(value);
	value = int32(sqrt(settings->trackpad_acceleration / 16384.0) * 1000 / 4);
	fAccelSlider->SetValue(value);
}
