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
 *   Copyright 2003-2016 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jérôme Duval, jerome.duval@free.fr
 *       Filip Maryjański, widelec@morphos.pl
 *       Puck Meerburg, puck@puckipedia.nl
 *       Michael Phipps
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file ScreenSaverWindow.cpp
 * @brief Implementation of the ScreenSaver preflet window and its tabs.
 *
 * Hosts two tabs:
 *   - General (FadeView): timeout slider, DPMS turn-off, password lock,
 *     and the two ScreenCornerSelectors for "fade now" and "never fade"
 *     hot corners.
 *   - Screensavers (ModulesView): list of installed add-ons with a live
 *     preview, a Test button, and the per-saver settings UI.
 *
 * Settings are persisted through ScreenSaverSettings; the modules view
 * watches the add-on directories via the node monitor so that newly
 * installed savers appear without a relaunch.
 *
 * @see ScreenSaverSettings
 */


#include "ScreenSaverWindow.h"

#include <stdio.h>
#include <strings.h>

#include <Alignment.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <DefaultSettingsView.h>
#include <Directory.h>
#include <DurationFormat.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Rect.h>
#include <Roster.h>
#include <Screen.h>
#include <ScreenSaver.h>
#include <ScreenSaverRunner.h>
#include <ScrollView.h>
#include <Size.h>
#include <Slider.h>
#include <StringView.h>
#include <TabView.h>
#include <TextView.h>

#include <algorithm>
	// for std::max and std::min

#include "PreviewView.h"
#include "ScreenCornerSelector.h"
#include "ScreenSaverItem.h"
#include "ScreenSaverShared.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ScreenSaver"


/** @brief Spacing between the painted monitor and the controls below it. */
const uint32 kPreviewMonitorGap = 16;
/** @brief Minimum width allotted to the per-saver settings box. */
const uint32 kMinSettingsWidth = 230;
/** @brief Minimum height allotted to the per-saver settings box. */
const uint32 kMinSettingsHeight = 120;

/** @brief Message dispatched when the user picks a saver in the list view. */
const int32 kMsgSaverSelected = 'SSEL';
/** @brief Message dispatched when the password-lock checkbox is toggled. */
const int32 kMsgPasswordCheckBox = 'PWCB';
/** @brief Message dispatched when the run-after slider stops moving. */
const int32 kMsgRunSliderChanged = 'RSch';
/** @brief Live update message from the run-after slider while dragging. */
const int32 kMsgRunSliderUpdate = 'RSup';
/** @brief Message dispatched when the password-after slider stops moving. */
const int32 kMsgPasswordSliderChanged = 'PWch';
/** @brief Live update message from the password-after slider while dragging. */
const int32 kMsgPasswordSliderUpdate = 'PWup';
/** @brief Message dispatched by the "Password..." button. */
const int32 kMsgChangePassword = 'PWBT';
/** @brief Message dispatched when the master "Enable screensaver" checkbox is toggled. */
const int32 kMsgEnableScreenSaverBox = 'ESCH';

/** @brief Message dispatched when the "Turn off screen" checkbox is toggled. */
const int32 kMsgTurnOffCheckBox = 'TUOF';
/** @brief Message dispatched when the turn-off slider stops moving. */
const int32 kMsgTurnOffSliderChanged = 'TUch';
/** @brief Live update message from the turn-off slider while dragging. */
const int32 kMsgTurnOffSliderUpdate = 'TUup';

/** @brief Message dispatched when the "fade now" hot corner changes. */
const int32 kMsgFadeCornerChanged = 'fdcc';
/** @brief Message dispatched when the "never fade" hot corner changes. */
const int32 kMsgNeverFadeCornerChanged = 'nfcc';

/** @brief Default window width, scaled by the preferred font size. */
const float kWindowWidth = 446.0f;
/** @brief Default window height, scaled by the preferred font size. */
const float kWindowHeight = 325.0f;
/** @brief Reference item-spacing assumed by the layout when the font is at 12pt. */
const float kDefaultItemSpacingAt12pt = 12.0f * 0.85;


/**
 * @brief BSlider that maps an integer index to a humanized duration label.
 *
 * The slider values are indices into @c kTimeInUnits (seconds). The
 * label is updated whenever Value() changes, formatted via
 * @c BDurationFormat.
 */
class TimeSlider : public BSlider {
public:
								TimeSlider(const char* name,
									uint32 changedMessage,
									uint32 updateMessage);
	virtual						~TimeSlider();

	virtual	void				SetValue(int32 value);

			void				SetTime(bigtime_t useconds);
			bigtime_t			Time() const;

private:
			void				_TimeToString(bigtime_t useconds,
									BString& string);
};


/**
 * @brief BTabView that intercepts mouse-down events to coordinate with the
 *        modules tab.
 *
 * Switching to the modules tab triggers a saver re-open; switching away
 * causes the running preview saver to close.
 */
class TabView : public BTabView {
public:
								TabView();

	virtual	void				MouseDown(BPoint where);
};


/**
 * @brief BView hosting the General tab: timeouts, DPMS, password, and corners.
 *
 * The view owns no settings of its own; it reads from and writes back to
 * the shared ScreenSaverSettings on every interaction so changes persist
 * even if the window is force-closed.
 */
class FadeView : public BView {
public:
								FadeView(const char* name,
									ScreenSaverSettings& settings);

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				UpdateTurnOffScreen();
			void				UpdateStatus();

private:
			void				_UpdateColors();
			ScreenSaverSettings&	fSettings;
			uint32				fTurnOffScreenFlags;

			BCheckBox*			fEnableCheckBox;
			TimeSlider*			fRunSlider;

			BTextView*			fTurnOffNotSupported;
			BCheckBox*			fTurnOffCheckBox;
			TimeSlider*			fTurnOffSlider;

			BTextView*			fFadeNeverText;
			BTextView*			fFadeNowText;

			BCheckBox*			fPasswordCheckBox;
			TimeSlider*			fPasswordSlider;
			BButton*			fPasswordButton;

			ScreenCornerSelector*	fFadeNow;
			ScreenCornerSelector*	fFadeNever;
};


/**
 * @brief BView hosting the Screensavers tab: list, preview, and per-saver UI.
 *
 * Watches the add-on directories with the node monitor so newly installed
 * savers appear automatically. The current saver runs in a child preview
 * view via a ScreenSaverRunner; testing launches @c screen_blanker as a
 * separate process.
 */
class ModulesView : public BView {
public:
								ModulesView(const char* name,
									ScreenSaverSettings& settings);
	virtual						~ModulesView();

	virtual	void				DetachedFromWindow();
	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				MessageReceived(BMessage* message);

			void				EmptyScreenSaverList();
			void				PopulateScreenSaverList();

			void				SaveState();

			BScreenSaver*		ScreenSaver();

private:
	friend class TabView;

	static	int					_CompareScreenSaverItems(const void* left,
									const void* right);

			void				_CloseSaver();
			void				_OpenSaver();
			void				_AddNewScreenSaverToList(const char* name,
								BPath* path);
			void				_RemoveScreenSaverFromList(const char* name);

private:
		ScreenSaverSettings&	fSettings;

			BListView*			fScreenSaversListView;
			BButton*			fTestButton;

			ScreenSaverRunner*	fSaverRunner;
			BString				fCurrentName;

			BBox*				fSettingsBox;
			BView*				fSettingsView;

			PreviewView*		fPreviewView;

			team_id				fScreenSaverTestTeam;
};


//	#pragma mark - TimeSlider


/** @brief Tabulated durations (seconds) corresponding to slider indices. */
static const int32 kTimeInUnits[] = {
	30,    60,   90,
	120,   150,  180,
	240,   300,  360,
	420,   480,  540,
	600,   900,  1200,
	1500,  1800, 2400,
	3000,  3600, 5400,
	7200,  9000, 10800,
	14400, 18000
};

/** @brief Number of entries in @c kTimeInUnits. */
static const int32 kTimeUnitCount
	= sizeof(kTimeInUnits) / sizeof(kTimeInUnits[0]);


/**
 * @brief Constructs a TimeSlider with the given change and live-update messages.
 *
 * @param name           Internal BView name.
 * @param changedMessage Message sent when the user releases the thumb.
 * @param updateMessage  Message sent continuously while dragging.
 */
TimeSlider::TimeSlider(const char* name, uint32 changedMessage,
	uint32 updateMessage)
	:
	BSlider(name, B_TRANSLATE("30 seconds"), new BMessage(changedMessage),
		0, kTimeUnitCount - 1, B_HORIZONTAL, B_TRIANGLE_THUMB)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetModificationMessage(new BMessage(updateMessage));
	SetBarThickness(10);
}


/**
 * @brief Destroys the time slider; no extra resources are held.
 */
TimeSlider::~TimeSlider()
{
}


/**
 * @brief Sets the slider's index and updates its label to a humanized duration.
 *
 * @param value New index into @c kTimeInUnits.
 */
void
TimeSlider::SetValue(int32 value)
{
	int32 oldValue = Value();
	BSlider::SetValue(value);

	if (oldValue != Value()) {
		BString label;
		_TimeToString(kTimeInUnits[Value()] * 1000000LL, label);
		SetLabel(label.String());
	}
}


/**
 * @brief Selects the slider position closest to a given duration.
 *
 * @param useconds Duration in microseconds; only exact matches against
 *                 @c kTimeInUnits select a slot. Non-matching durations
 *                 leave the slider unchanged.
 */
void
TimeSlider::SetTime(bigtime_t useconds)
{
	for (int t = 0; t < kTimeUnitCount; t++) {
		if (kTimeInUnits[t] * 1000000LL == useconds) {
			SetValue(t);
			break;
		}
	}
}


/**
 * @brief Returns the slider's current value as a microsecond duration.
 *
 * @return @c kTimeInUnits[Value()] expressed in microseconds.
 */
bigtime_t
TimeSlider::Time() const
{
	return 1000000LL * kTimeInUnits[Value()];
}


/**
 * @brief Formats @a useconds into a human-readable label using BDurationFormat.
 *
 * @param useconds Duration in microseconds.
 * @param string   Output BString receiving the formatted label.
 */
void
TimeSlider::_TimeToString(bigtime_t useconds, BString& string)
{
	BDurationFormat formatter;
	formatter.Format(string, 0, useconds);
}


//	#pragma mark - FadeView


/**
 * @brief Builds the General tab layout: enable box, timing grid, hot corners.
 *
 * Constructs all sub-controls eagerly. The "Turn off screen" controls are
 * drawn whether DPMS is supported or not; AttachedToWindow() decides
 * which subset is visible based on the current screen capabilities.
 *
 * @param name     Internal BView name.
 * @param settings Shared ScreenSaverSettings model.
 */
FadeView::FadeView(const char* name, ScreenSaverSettings& settings)
	:
	BView(name, B_WILL_DRAW),
	fSettings(settings)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	float textHeight = ceilf(fontHeight.ascent + fontHeight.descent);

	fEnableCheckBox = new BCheckBox("EnableCheckBox",
		B_TRANSLATE("Enable screensaver"),
		new BMessage(kMsgEnableScreenSaverBox));

	BBox* box = new BBox("EnableScreenSaverBox");
	box->SetLabel(fEnableCheckBox);

	// Start Screensaver
	BStringView* startScreenSaver = new BStringView("startScreenSaver",
		B_TRANSLATE("Start screensaver"));
	startScreenSaver->SetAlignment(B_ALIGN_RIGHT);

	fRunSlider = new TimeSlider("RunSlider", kMsgRunSliderChanged,
		kMsgRunSliderUpdate);

	// Turn Off
	rgb_color textColor = disable_color(ui_color(B_PANEL_TEXT_COLOR),
		ViewColor());

	fTurnOffNotSupported = new BTextView("not_supported", be_plain_font,
		&textColor, B_WILL_DRAW);
	fTurnOffNotSupported->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		3 + textHeight * 3));
	fTurnOffNotSupported->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fTurnOffNotSupported->MakeEditable(false);
	fTurnOffNotSupported->MakeSelectable(false);
	fTurnOffNotSupported->SetText(
		B_TRANSLATE("Display Power Management Signaling not available"));

	fTurnOffCheckBox = new BCheckBox("TurnOffScreenCheckBox",
		B_TRANSLATE("Turn off screen"), new BMessage(kMsgTurnOffCheckBox));
	fTurnOffCheckBox->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));

	fTurnOffSlider = new TimeSlider("TurnOffSlider", kMsgTurnOffSliderChanged,
		kMsgTurnOffSliderUpdate);

	// Password
	fPasswordCheckBox = new BCheckBox("PasswordCheckbox",
		B_TRANSLATE("Password lock"), new BMessage(kMsgPasswordCheckBox));
	fPasswordCheckBox->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));

	fPasswordSlider = new TimeSlider("PasswordSlider",
		kMsgPasswordSliderChanged, kMsgPasswordSliderUpdate);

	fPasswordButton = new BButton("PasswordButton",
		B_TRANSLATE("Password" B_UTF8_ELLIPSIS),
		new BMessage(kMsgChangePassword));

	// Bottom
	float monitorHeight = 10 + textHeight * 3;
	float aspectRatio = 4.0f / 3.0f;
	float monitorWidth = monitorHeight * aspectRatio;
	BRect monitorRect = BRect(0, 0, monitorWidth, monitorHeight);

	fFadeNow = new ScreenCornerSelector(monitorRect, "FadeNow",
		new BMessage(kMsgFadeCornerChanged), B_FOLLOW_NONE);
	fFadeNowText = new BTextView("FadeNowText", B_WILL_DRAW);
	fFadeNowText->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		4 + textHeight * 4));
	fFadeNowText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fFadeNowText->MakeEditable(false);
	fFadeNowText->MakeSelectable(false);
	fFadeNowText->SetText(B_TRANSLATE("Fade now when mouse is here"));

	fFadeNever = new ScreenCornerSelector(monitorRect, "FadeNever",
		new BMessage(kMsgNeverFadeCornerChanged), B_FOLLOW_NONE);
	fFadeNeverText = new BTextView("FadeNeverText", B_WILL_DRAW);
	fFadeNeverText->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		4 + textHeight * 4));
	fFadeNeverText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fFadeNeverText->MakeEditable(false);
	fFadeNeverText->MakeSelectable(false);
	fFadeNeverText->SetText(B_TRANSLATE("Don't fade when mouse is here"));

	box->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.SetInsets(B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING,
			B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(startScreenSaver, 0, 0)
			.Add(fRunSlider, 1, 0)
			.Add(fTurnOffCheckBox, 0, 1)
			.Add(BLayoutBuilder::Group<>(B_VERTICAL)
				.Add(fTurnOffNotSupported)
				.Add(fTurnOffSlider)
				.View(), 1, 1)
			.Add(fPasswordCheckBox, 0, 2)
			.Add(fPasswordSlider, 1, 2)
			.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fPasswordButton)
			.End()
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.Add(fFadeNow)
			.AddGroup(B_VERTICAL, 0)
				.Add(fFadeNowText)
				.AddGlue()
				.End()
			.Add(fFadeNever)
			.AddGroup(B_VERTICAL, 0)
				.Add(fFadeNeverText)
				.AddGlue()
				.End()
			.End()
		.AddGlue()
		.View());

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, 0)
		.Add(box)
		.End();

}


/**
 * @brief BView AttachedToWindow hook: wires targets and seeds initial state.
 *
 * Points all sub-controls at this view as their target, copies values
 * from ScreenSaverSettings into the controls, and refreshes color and
 * DPMS-dependent state.
 */
void
FadeView::AttachedToWindow()
{
	fEnableCheckBox->SetTarget(this);
	fRunSlider->SetTarget(this);
	fTurnOffCheckBox->SetTarget(this);
	fTurnOffSlider->SetTarget(this);
	fFadeNow->SetTarget(this);
	fFadeNever->SetTarget(this);
	fPasswordCheckBox->SetTarget(this);
	fPasswordSlider->SetTarget(this);

	fEnableCheckBox->SetValue(
		fSettings.TimeFlags() & ENABLE_SAVER ? B_CONTROL_ON : B_CONTROL_OFF);
	fRunSlider->SetTime(fSettings.BlankTime());
	fTurnOffSlider->SetTime(fSettings.OffTime() + fSettings.BlankTime());
	fFadeNow->SetCorner(fSettings.BlankCorner());
	fFadeNever->SetCorner(fSettings.NeverBlankCorner());
	fPasswordCheckBox->SetValue(fSettings.LockEnable());
	fPasswordSlider->SetTime(fSettings.PasswordTime());

	_UpdateColors();
	UpdateTurnOffScreen();
	UpdateStatus();
}


/**
 * @brief BView message hook: applies slider clamping and persistence.
 *
 * The three sliders maintain a global ordering: run-after must not exceed
 * turn-off, and password-after must not be smaller than run-after. After
 * resolving any clamping, UpdateStatus() is called and the settings are
 * saved.
 *
 * @param message Incoming message.
 */
void
FadeView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case B_COLORS_UPDATED:
			_UpdateColors();
			break;
		case kMsgRunSliderChanged:
		case kMsgRunSliderUpdate:
			if (fRunSlider->Value() > fTurnOffSlider->Value())
				fTurnOffSlider->SetValue(fRunSlider->Value());

			if (fRunSlider->Value() > fPasswordSlider->Value())
				fPasswordSlider->SetValue(fRunSlider->Value());
			break;

		case kMsgTurnOffSliderChanged:
		case kMsgTurnOffSliderUpdate:
			if (fRunSlider->Value() > fTurnOffSlider->Value())
				fRunSlider->SetValue(fTurnOffSlider->Value());
			break;

		case kMsgPasswordSliderChanged:
		case kMsgPasswordSliderUpdate:
			if (fPasswordSlider->Value() < fRunSlider->Value())
				fRunSlider->SetValue(fPasswordSlider->Value());
			break;

		case kMsgTurnOffCheckBox:
			fTurnOffSlider->SetEnabled(
				fTurnOffCheckBox->Value() == B_CONTROL_ON);
			break;
	}

	switch (message->what) {
		case kMsgRunSliderChanged:
		case kMsgTurnOffSliderChanged:
		case kMsgPasswordSliderChanged:
		case kMsgPasswordCheckBox:
		case kMsgEnableScreenSaverBox:
		case kMsgFadeCornerChanged:
		case kMsgNeverFadeCornerChanged:
			UpdateStatus();
			fSettings.Save();
			break;

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Re-evaluates DPMS support and adjusts the turn-off controls.
 *
 * Queries the current screen for DPMS capabilities (off, stand-by,
 * suspend) and stores the resulting flag set in @c fTurnOffScreenFlags.
 * The turn-off checkbox is enabled only when the master enable is on and
 * at least one DPMS mode is supported; the corresponding slider replaces
 * the "not supported" message accordingly.
 */
void
FadeView::UpdateTurnOffScreen()
{
	bool enabled = (fSettings.TimeFlags() & ENABLE_DPMS_MASK) != 0;

	BScreen screen(Window());
	uint32 dpmsCapabilities = screen.DPMSCapabilites();

	fTurnOffScreenFlags = 0;
	if (dpmsCapabilities & B_DPMS_OFF)
		fTurnOffScreenFlags |= ENABLE_DPMS_OFF;
	if (dpmsCapabilities & B_DPMS_STAND_BY)
		fTurnOffScreenFlags |= ENABLE_DPMS_STAND_BY;
	if (dpmsCapabilities & B_DPMS_SUSPEND)
		fTurnOffScreenFlags |= ENABLE_DPMS_SUSPEND;

	fTurnOffCheckBox->SetValue(enabled && fTurnOffScreenFlags != 0
		? B_CONTROL_ON : B_CONTROL_OFF);

	enabled = fEnableCheckBox->Value() == B_CONTROL_ON;
	fTurnOffCheckBox->SetEnabled(enabled && fTurnOffScreenFlags != 0);
	if (fTurnOffScreenFlags != 0) {
		fTurnOffNotSupported->Hide();
		fTurnOffSlider->Show();
	} else {
		fTurnOffSlider->Hide();
		fTurnOffNotSupported->Show();
	}
}


/**
 * @brief Refreshes enable states and pushes current values into the model.
 *
 * Disables the turn-off, password, and run-after controls when the master
 * enable is off. Computes the DPMS off-time as the difference between
 * the turn-off slider and the run-after slider, and writes all current
 * values back to ScreenSaverSettings. Window updates are paused during
 * the work to avoid flicker.
 *
 * @todo Tell the password window to update its state when the password
 *       slider changes.
 */
void
FadeView::UpdateStatus()
{
	Window()->DisableUpdates();

	bool enabled = fEnableCheckBox->Value() == B_CONTROL_ON;
	fPasswordCheckBox->SetEnabled(enabled);
	fTurnOffCheckBox->SetEnabled(enabled && fTurnOffScreenFlags != 0);
	fRunSlider->SetEnabled(enabled);
	fTurnOffSlider->SetEnabled(enabled && fTurnOffCheckBox->Value());
	fPasswordSlider->SetEnabled(enabled && fPasswordCheckBox->Value());
	fPasswordButton->SetEnabled(enabled && fPasswordCheckBox->Value());

	Window()->EnableUpdates();

	// Update the saved preferences
	fSettings.SetWindowFrame(Frame());
	fSettings.SetTimeFlags((enabled ? ENABLE_SAVER : 0)
		| (fTurnOffCheckBox->Value() ? fTurnOffScreenFlags : 0));
	fSettings.SetBlankTime(fRunSlider->Time());
	bigtime_t offTime = fTurnOffSlider->Time() - fSettings.BlankTime();
	fSettings.SetOffTime(offTime);
	fSettings.SetSuspendTime(offTime);
	fSettings.SetStandByTime(offTime);
	fSettings.SetBlankCorner(fFadeNow->Corner());
	fSettings.SetNeverBlankCorner(fFadeNever->Corner());
	fSettings.SetLockEnable(fPasswordCheckBox->Value());
	fSettings.SetPasswordTime(fPasswordSlider->Time());

	// TODO - Tell the password window to update its stuff
}


/**
 * @brief Refreshes the static-text colors after a system color change.
 *
 * The two corner-selector caption views use plain text rendered in the
 * panel text color; this routine re-applies the current ui_color so the
 * labels track the system theme.
 */
void
FadeView::_UpdateColors()
{
	rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
	fFadeNeverText->SetFontAndColor(be_plain_font, 0, &color);
	fFadeNowText->SetFontAndColor(be_plain_font, 0, &color);
}


//	#pragma mark - ModulesView


/**
 * @brief Builds the Screensavers tab layout: list, preview, settings box.
 *
 * Creates the list view, scroll view, Test button, settings box (with a
 * minimum size derived from the default item spacing), and PreviewView,
 * then arranges them in two columns.
 *
 * @param name     Internal BView name.
 * @param settings Shared ScreenSaverSettings model.
 */
ModulesView::ModulesView(const char* name, ScreenSaverSettings& settings)
	:
	BView(name, B_WILL_DRAW),
	fSettings(settings),
	fScreenSaversListView(new BListView("SaversListView")),
	fTestButton(new BButton("TestButton", B_TRANSLATE("Test"),
		new BMessage(kMsgTestSaver))),
	fSaverRunner(NULL),
	fSettingsBox(new BBox("SettingsBox")),
	fSettingsView(NULL),
	fPreviewView(new PreviewView("preview")),
	fScreenSaverTestTeam(-1)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fScreenSaversListView->SetSelectionMessage(
		new BMessage(kMsgSaverSelected));
	fScreenSaversListView->SetInvocationMessage(
		new BMessage(kMsgTestSaver));
	BScrollView* saversListScrollView = new BScrollView("scroll_list",
		fScreenSaversListView, 0, false, true);

	fSettingsBox->SetLabel(B_TRANSLATE("Screensaver settings"));
	fSettingsBox->SetExplicitMinSize(BSize(
		floorf(be_control_look->DefaultItemSpacing()
			* ((kWindowWidth - 157.0f) / kDefaultItemSpacingAt12pt)),
		B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, 0)
		.AddGroup(B_VERTICAL)
			.Add(fPreviewView)
			.Add(saversListScrollView)
			.Add(fTestButton)
			.End()
		.Add(fSettingsBox)
		.End();
}


/**
 * @brief Destroys the modules view, stops node watching, and releases
 *        owned widgets.
 *
 * @note The list view is owned by the scroll view (and BView ownership)
 *       and is therefore not deleted explicitly here.
 */
ModulesView::~ModulesView()
{
	stop_watching(this);

	delete fTestButton;
	delete fSettingsBox;
	delete fPreviewView;
}


/**
 * @brief BView DetachedFromWindow hook: persists state and tears down the
 *        running saver.
 *
 * Triggered when the tab is being replaced or the window is closing.
 */
void
ModulesView::DetachedFromWindow()
{
	SaveState();
	EmptyScreenSaverList();

	_CloseSaver();
}


/**
 * @brief BView AttachedToWindow hook: routes list and button messages here.
 */
void
ModulesView::AttachedToWindow()
{
	fScreenSaversListView->SetTarget(this);
	fTestButton->SetTarget(this);
}


/**
 * @brief BView AllAttached hook: populates the saver list once the
 *        attachment is complete.
 */
void
ModulesView::AllAttached()
{
	PopulateScreenSaverList();
}


/**
 * @brief BView message hook: handles selection, testing, and node monitor.
 *
 * Recognized messages:
 *   - @c kMsgSaverSelected: closes the previous saver, opens the new one,
 *     persists the choice.
 *   - @c kMsgTestSaver: launches @c screen_blanker as a separate process,
 *     falling back to a hard-coded path when the roster lookup fails.
 *   - @c B_NODE_MONITOR: maintains the saver list as add-on directories change.
 *   - @c B_SOME_APP_QUIT: re-opens the in-window preview after the test
 *     process exits.
 *
 * @param message Incoming message.
 */
void
ModulesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSaverSelected:
		{
			int32 selection = fScreenSaversListView->CurrentSelection();
			if (selection < 0)
				break;

			ScreenSaverItem* item
				= (ScreenSaverItem*)fScreenSaversListView->ItemAt(selection);
			if (item == NULL)
				break;

			if (strcmp(item->Text(), B_TRANSLATE("Blackness")) == 0)
				fSettings.SetModuleName("");
			else
				fSettings.SetModuleName(item->Text());

			SaveState();
			_CloseSaver();
			_OpenSaver();
			fSettings.Save();
			break;
		}

		case kMsgTestSaver:
		{
			SaveState();
			fSettings.Save();

			_CloseSaver();

			be_roster->StartWatching(BMessenger(this, Looper()),
				B_REQUEST_QUIT);
			BMessage message(kMsgTestSaver);
			if (be_roster->Launch(SCREEN_BLANKER_SIG, &message,
					&fScreenSaverTestTeam) == B_OK) {
				break;
			}

			// Try really hard to launch it. It's very likely that this fails
			// when we run from the CD, and there is only an incomplete mime
			// database for example...
			BPath path;
			if (find_directory(B_SYSTEM_BIN_DIRECTORY, &path) != B_OK
				|| path.Append("screen_blanker") != B_OK) {
				path.SetTo("/bin/screen_blanker");
			}

			BEntry entry(path.Path());
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				be_roster->Launch(&ref, &message,
					&fScreenSaverTestTeam);
			}
			break;
		}

		case B_NODE_MONITOR:
		{
			switch (message->GetInt32("opcode", 0)) {
				case B_ENTRY_CREATED:
				{
					const char* name;
					node_ref nodeRef;

					message->FindString("name", &name);
					message->FindInt32("device", &nodeRef.device);
					message->FindInt64("directory", &nodeRef.node);

					BDirectory dir(&nodeRef);

					if (dir.InitCheck() == B_OK) {
						BPath path(&dir);
						_AddNewScreenSaverToList(name, &path);
					}
					break;
				}


				case B_ENTRY_MOVED:
				case B_ENTRY_REMOVED:
				{
					const char* name;

					message->FindString("name", &name);
					_RemoveScreenSaverFromList(name);

					break;
				}

				default:
					// ignore any other operations
					break;
			}
			break;
		}

		case B_SOME_APP_QUIT:
		{
			team_id team;
			if (message->FindInt32("be:team", &team) == B_OK
				&& team == fScreenSaverTestTeam) {
				be_roster->StopWatching(this);
				_OpenSaver();
			}
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Saves the current saver's per-module state into ScreenSaverSettings.
 *
 * No-op when no saver is loaded.
 */
void
ModulesView::SaveState()
{
	BScreenSaver* saver = ScreenSaver();
	if (saver == NULL)
		return;

	BMessage state;
	if (saver->SaveState(&state) == B_OK)
		fSettings.SetModuleState(fCurrentName.String(), &state);
}


/**
 * @brief Removes and deletes every item from the saver list view.
 */
void
ModulesView::EmptyScreenSaverList()
{
	fScreenSaversListView->DeselectAll();
	while (BListItem* item = fScreenSaversListView->RemoveItem((int32)0))
		delete item;
}


/**
 * @brief Populates the saver list from add-on directories and selects
 *        the previously chosen saver.
 *
 * Always inserts a "Blackness" pseudo-saver as the first entry. Then
 * iterates the user and system add-on directories (both packaged and
 * non-packaged), watches each directory for change notifications, and
 * appends any matching add-on. The list is sorted case-insensitively.
 */
void
ModulesView::PopulateScreenSaverList()
{
	// Blackness is a built-in screen saver
	ScreenSaverItem* defaultItem
		= new ScreenSaverItem(B_TRANSLATE("Blackness"), "");
	fScreenSaversListView->AddItem(defaultItem);

	// Iterate over add-on directories, and add their files to the list view

	directory_which which[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
	};
	ScreenSaverItem* selectedItem = NULL;

	for (uint32 i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		BPath basePath;
		if (find_directory(which[i], &basePath) != B_OK)
			continue;
		else if (basePath.Append("Screen Savers", true) != B_OK)
			continue;

		BDirectory dir(basePath.Path());
		BEntry entry;
		node_ref nodeRef;

		dir.GetNodeRef(&nodeRef);
		watch_node(&nodeRef, B_WATCH_DIRECTORY, this);

		while (dir.GetNextEntry(&entry, true) == B_OK) {
			char name[B_FILE_NAME_LENGTH];
			if (entry.GetName(name) != B_OK)
				continue;

			BPath path(basePath);
			if (path.Append(name) != B_OK)
				continue;

			ScreenSaverItem* item = new ScreenSaverItem(name, path.Path());
			fScreenSaversListView->AddItem(item);

			if (selectedItem != NULL)
				continue;

			if (strcmp(fSettings.ModuleName(), item->Text()) == 0)
				selectedItem = item;
		}
	}

	fScreenSaversListView->SortItems(_CompareScreenSaverItems);
	if (selectedItem == NULL)
		selectedItem = defaultItem;

	fScreenSaversListView->Select(fScreenSaversListView->IndexOf(selectedItem));
	fScreenSaversListView->ScrollToSelection();
}


/**
 * @brief Sorting predicate for ScreenSaverItem entries.
 *
 * @param left  Pointer to a ScreenSaverItem* (BList entry).
 * @param right Pointer to a ScreenSaverItem* (BList entry).
 * @return Result of @c strcasecmp on the items' display labels.
 */
int
ModulesView::_CompareScreenSaverItems(const void* left, const void* right)
{
	ScreenSaverItem* leftItem  = *(ScreenSaverItem **)left;
	ScreenSaverItem* rightItem = *(ScreenSaverItem **)right;

	return strcasecmp(leftItem->Text(), rightItem->Text());
}


/**
 * @brief Returns the BScreenSaver currently driven by the runner, or NULL.
 */
BScreenSaver*
ModulesView::ScreenSaver()
{
	if (fSaverRunner != NULL)
		return fSaverRunner->ScreenSaver();

	return NULL;
}


/**
 * @brief Tears down the running preview: removes views, stops the runner,
 *        and unloads the add-on.
 *
 * The runner is deleted last because it is responsible for unloading the
 * add-on, and the BScreenSaver pointer would dangle if the add-on
 * unloaded before the StopConfig() call returns.
 */
void
ModulesView::_CloseSaver()
{
	// remove old screen saver preview & config

	BScreenSaver* saver = ScreenSaver();
	BView* view = fPreviewView->RemovePreview();
	if (fSettingsView != NULL)
		fSettingsBox->RemoveChild(fSettingsView);

	if (fSaverRunner != NULL)
		fSaverRunner->Quit();

	if (saver != NULL)
		saver->StopConfig();

	delete view;
	delete fSettingsView;
	delete fSaverRunner;
		// the saver runner also unloads the add-on, so it must
		// be deleted last

	fSettingsView = NULL;
	fSaverRunner = NULL;
}


/**
 * @brief Spins up a fresh preview view, settings view, and runner.
 *
 * If the saver fails to start (for example "Blackness" or a missing
 * add-on), a black preview is shown. When the saver exposes no settings,
 * a default placeholder view is inserted by
 * @c BPrivate::BuildDefaultSettingsView() so the settings box is never
 * empty.
 */
void
ModulesView::_OpenSaver()
{
	// create new screen saver preview & config

	BView* view = fPreviewView->AddPreview();
	fCurrentName = fSettings.ModuleName();
	fSaverRunner = new ScreenSaverRunner(view->Window(), view, fSettings);

#ifdef __HAIKU__
	BRect rect = fSettingsBox->InnerFrame().InsetByCopy(4, 4);
#else
	BRect rect = fSettingsBox->Bounds().InsetByCopy(4, 4);
	rect.top += 14;
#endif
	fSettingsView = new BView(rect, "SettingsView", B_FOLLOW_ALL, B_WILL_DRAW);

	fSettingsView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fSettingsBox->AddChild(fSettingsView);

	BScreenSaver* saver = ScreenSaver();
	if (saver != NULL && fSettingsView != NULL) {
		saver->StartConfig(fSettingsView);
		if (saver->StartSaver(view, true) == B_OK) {
			fPreviewView->HideNoPreview();
			fSaverRunner->Run();
		} else
			fPreviewView->ShowNoPreview();
	} else {
		// Failed to load OR this is the "Blackness" screensaver. Show a black
		// preview (this is what will happen in both cases when screen_blanker
		// runs).
		fPreviewView->HideNoPreview();
	}

	if (fSettingsView->ChildAt(0) == NULL) {
		// There are no settings at all, we add the module name here to
		// let it look a bit better at least.
		BPrivate::BuildDefaultSettingsView(fSettingsView,
			fSettings.ModuleName()[0] ? fSettings.ModuleName()
				: B_TRANSLATE("Blackness"),
				saver != NULL || !fSettings.ModuleName()[0]
					? B_TRANSLATE("No options available")
					: B_TRANSLATE("Could not load screen saver"));
	}
}


/**
 * @brief Inserts a newly discovered screensaver into the sorted list.
 *
 * Preserves the current selection across the insertion so that the user's
 * choice is not silently lost when an add-on appears.
 *
 * @param name Display label of the new add-on.
 * @param path Pointer to a BPath rooted at the add-on directory; the
 *             function appends @a name to it before recording the path.
 */
void
ModulesView::_AddNewScreenSaverToList(const char* name, BPath* path)
{
	int32 oldSelected = fScreenSaversListView->CurrentSelection();
	ScreenSaverItem* selectedItem = (ScreenSaverItem*)fScreenSaversListView->ItemAt(
		oldSelected);

	path->Append(name);
	fScreenSaversListView->AddItem(new ScreenSaverItem(name, path->Path()));
	fScreenSaversListView->SortItems(_CompareScreenSaverItems);

	if (selectedItem != NULL) {
		fScreenSaversListView->Select(fScreenSaversListView->IndexOf(
			selectedItem));
		fScreenSaversListView->ScrollToSelection();
	}
}


/**
 * @brief Removes a screensaver from the list when its add-on is gone.
 *
 * If the deleted item happened to be the selected one, the selection
 * collapses to the first list entry; otherwise the previous selection is
 * preserved.
 *
 * @param name Display label of the add-on to remove.
 */
void
ModulesView::_RemoveScreenSaverFromList(const char* name)
{
	int32 oldSelected = fScreenSaversListView->CurrentSelection();
	ScreenSaverItem* selectedItem = (ScreenSaverItem*)fScreenSaversListView->ItemAt(
		oldSelected);

	if (strcasecmp(selectedItem->Text(), name) == 0) {
		fScreenSaversListView->RemoveItem(selectedItem);
		fScreenSaversListView->SortItems(_CompareScreenSaverItems);
		fScreenSaversListView->Select(0);
		fScreenSaversListView->ScrollToSelection();
		return;
	}

	for (int i = 0, max = fScreenSaversListView->CountItems(); i < max; i++) {
		ScreenSaverItem* item = (ScreenSaverItem*)fScreenSaversListView->ItemAt(
			i);

		if (strcasecmp(item->Text(), name) == 0) {
			fScreenSaversListView->RemoveItem(item);
			delete item;
			break;
		}
	}

	fScreenSaversListView->SortItems(_CompareScreenSaverItems);

	oldSelected = fScreenSaversListView->IndexOf(selectedItem);
	fScreenSaversListView->Select(oldSelected);
	fScreenSaversListView->ScrollToSelection();
}


//	#pragma mark - TabView


/**
 * @brief Constructs the tab view with width-from-label sizing.
 */
TabView::TabView()
	:
	BTabView("tab_view", B_WIDTH_FROM_LABEL)
{
}


/**
 * @brief BTabView MouseDown hook: keeps the saver runner consistent across
 *        tab switches.
 *
 * When the user clicks the General tab while the Modules tab is active,
 * the running saver is closed (so it does not steal cycles in the
 * background). When the user clicks the Modules tab, a synthetic
 * @c kMsgSaverSelected is sent so the preview is rebuilt.
 *
 * @param where Mouse location in view coordinates.
 */
void
TabView::MouseDown(BPoint where)
{
	BTab* fadeTab = TabAt(0);
	BRect fadeTabFrame(TabFrame(0));
	BTab* modulesTab = TabAt(1);
	BRect modulesTabFrame(TabFrame(1));
	ModulesView* modulesView = NULL;

	if (modulesTab != NULL)
		modulesView = dynamic_cast<ModulesView*>(modulesTab->View());

	if (fadeTab != NULL && Selection() != 0 && fadeTabFrame.Contains(where)
		&& modulesView != NULL) {
		// clicked on the fade tab
		modulesView->SaveState();
		modulesView->_CloseSaver();
	} else if (modulesTab != NULL && Selection() != 1
		&& modulesTabFrame.Contains(where) && modulesView != NULL) {
		// clicked on the modules tab
		BMessage message(kMsgSaverSelected);
		modulesView->MessageReceived(&message);
	}

	BTabView::MouseDown(where);
}


//	#pragma mark - ScreenSaverWindow


/**
 * @brief Constructs the preflet window, loads settings, and builds the tabs.
 *
 * Computes minimum dimensions from the system control spacing and font
 * metrics so the layout stays usable on high-DPI displays. Spawns the
 * separate PasswordWindow up front so that opening it later is just a
 * Show().
 */
ScreenSaverWindow::ScreenSaverWindow()
	:
	BWindow(BRect(50.0f, 50.0f, 50.0f + kWindowWidth, 50.0f + kWindowHeight),
		B_TRANSLATE_SYSTEM_NAME("ScreenSaver"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fSettings.Load();

	fMinWidth = floorf(be_control_look->DefaultItemSpacing()
		* (kWindowWidth / kDefaultItemSpacingAt12pt));

	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	float textHeight = ceilf(fontHeight.ascent + fontHeight.descent);

	fMinHeight = ceilf(std::max(kWindowHeight, textHeight * 28));

	// Create the password editing window
	fPasswordWindow = new PasswordWindow(fSettings);
	fPasswordWindow->Run();

	// Create the tab view
	fTabView = new TabView();
	fTabView->SetBorder(B_NO_BORDER);

	// Create the controls inside the tabs
	fFadeView = new FadeView(B_TRANSLATE("General"), fSettings);
	fModulesView = new ModulesView(B_TRANSLATE("Screensavers"), fSettings);

	fTabView->AddTab(fFadeView);
	fTabView->AddTab(fModulesView);

	// Create the topmost background view
	BView* topView = new BView("topView", B_WILL_DRAW);
	topView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	topView->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
	topView->SetExplicitMinSize(BSize(fMinWidth, fMinHeight));
	BLayoutBuilder::Group<>(topView, B_VERTICAL)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, B_USE_WINDOW_SPACING)
		.Add(fTabView)
		.End();

	SetLayout(new BGroupLayout(B_VERTICAL));
	GetLayout()->AddView(topView);

	fTabView->Select(fSettings.WindowTab());

	if (fSettings.WindowFrame().left > 0 && fSettings.WindowFrame().top > 0)
		MoveTo(fSettings.WindowFrame().left, fSettings.WindowFrame().top);

	if (fSettings.WindowFrame().Width() > 0
		&& fSettings.WindowFrame().Height() > 0) {
		ResizeTo(fSettings.WindowFrame().Width(),
			fSettings.WindowFrame().Height());
	}

	CenterOnScreen();
}


/**
 * @brief Saves state and tears down the modules tab before destruction.
 *
 * The window is hidden first so the user does not see an empty pane
 * while the modules view is detached. The modules tab is removed and
 * deleted explicitly so its DetachedFromWindow() runs while the window
 * is still alive (which it needs to flush state to disk).
 */
ScreenSaverWindow::~ScreenSaverWindow()
{
	Hide();
	fFadeView->UpdateStatus();
	fSettings.SetWindowTab(fTabView->Selection());

	delete fTabView->RemoveTab(1);
		// We delete this here in order to make sure the module view saves its
		// state while the window is still intact.

	fSettings.Save();
}


/**
 * @brief BWindow message hook: handles the password-edit and refresh messages.
 *
 * @param message Incoming message; @c kMsgChangePassword centers and
 *                shows the password window, @c kMsgUpdateList rebuilds
 *                the modules list.
 */
void
ScreenSaverWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgChangePassword:
			fPasswordWindow->CenterIn(Frame());
			fPasswordWindow->Show();
			break;

		case kMsgUpdateList:
			fModulesView->EmptyScreenSaverList();
			fModulesView->PopulateScreenSaverList();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief BWindow ScreenChanged hook: forwards to FadeView for DPMS reprobe.
 *
 * @param frame      New screen frame (unused).
 * @param colorSpace New color space (unused).
 */
void
ScreenSaverWindow::ScreenChanged(BRect frame, color_space colorSpace)
{
	fFadeView->UpdateTurnOffScreen();
}


/**
 * @brief BWindow QuitRequested hook: asks the application to quit.
 *
 * @return Always @c true; the window is allowed to close.
 */
bool
ScreenSaverWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
