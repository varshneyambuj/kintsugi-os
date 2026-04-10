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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Erik Jaesler, erik@cgsoftware.com
 *       John Scipione, jscipione@gmail.com
 *       Ron Ben Aroya, sed4906birdie@gmail.com
 */


/**
 * @file Alert.cpp
 * @brief Implementation of BAlert, a modal alert dialog window
 *
 * BAlert displays a pre-formatted modal window with an icon, descriptive text,
 * and up to three labeled buttons. It blocks the calling thread until the user
 * dismisses it, returning the index of the selected button.
 *
 * @see BWindow, BButton
 */


#include <Alert.h>

#include <new>

#include <stdio.h>

#include <Beep.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <MediaSounds.h>
#include <MenuField.h>
#include <MessageFilter.h>
#include <Path.h>
#include <Resources.h>
#include <Screen.h>
#include <String.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


//#define DEBUG_ALERT
#ifdef DEBUG_ALERT
#	define FTRACE(x) fprintf(x)
#else
#	define FTRACE(x) ;
#endif


/**
 * @brief Internal view that renders the alert-type icon stripe on the left side.
 *
 * TAlertView owns the icon bitmap and draws a shaded stripe behind it, mirroring
 * the visual style used by StripeView in AboutWindow.
 */
class TAlertView : public BView {
public:
								TAlertView();
								TAlertView(BMessage* archive);
								~TAlertView();

	static	TAlertView*			Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				GetPreferredSize(float* _width, float* _height);
	virtual	BSize				MaxSize();
	virtual	void				Draw(BRect updateRect);

			void				SetBitmap(BBitmap* icon);
			BBitmap*			Bitmap()
									{ return fIconBitmap; }

private:
			BBitmap*			fIconBitmap;
};


/**
 * @brief Message filter that maps shortcut keys to alert button presses.
 *
 * Installed as a common filter on the BAlert window; intercepts B_KEY_DOWN
 * messages and forwards matching shortcut characters to the corresponding
 * button via BButton::KeyDown().
 */
class _BAlertFilter_ : public BMessageFilter {
public:
								_BAlertFilter_(BAlert* Alert);
								~_BAlertFilter_();

	virtual	filter_result		Filter(BMessage* msg, BHandler** target);

private:
			BAlert*				fAlert;
};


/** @brief Message constant sent by each alert button when pressed. */
static const unsigned int kAlertButtonMsg = 'ALTB';

/** @brief Semaphore acquire timeout (µs) used during synchronous Go() polling. */
static const int kSemTimeOut = 50000;

/** @brief Horizontal spacing (pixels) added before the leftmost button in offset mode. */
static const int kButtonOffsetSpacing = 62;

/** @brief Default button width (pixels) when B_WIDTH_AS_USUAL is selected. */
static const int kButtonUsualWidth = 55;

/** @brief Multiplier applied to DefaultLabelSpacing() to compute the icon stripe width. */
static const int kIconStripeWidthFactor = 5;

/** @brief Minimum window width (pixels) for standard button spacing. */
static const int kWindowMinWidth = 310;

/** @brief Minimum window width (pixels) when B_OFFSET_SPACING is active. */
static const int kWindowOffsetMinWidth = 335;


// #pragma mark -


/**
 * @brief Constructs a default BAlert with no text and a single info-style layout.
 *
 * The alert has no body text and no buttons until they are added explicitly.
 * Use AddButton() and SetText() to populate the dialog before calling Go().
 *
 * @see BAlert(const char*, const char*, const char*, const char*, const char*,
 *             button_width, alert_type)
 */
BAlert::BAlert()
	:
	BWindow(BRect(0, 0, 100, 100), "", B_MODAL_WINDOW,
		B_NOT_CLOSABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS)
{
	_Init(NULL, NULL, NULL, NULL, B_WIDTH_FROM_WIDEST, B_EVEN_SPACING,
		B_INFO_ALERT);
}


/**
 * @brief Constructs a BAlert with up to three buttons and standard even spacing.
 *
 * @param title   The window title (shown in the title bar on non-modal builds).
 * @param text    The descriptive body text displayed in the alert.
 * @param button1 Label for the first (leftmost) button; must not be NULL.
 * @param button2 Label for the second button, or NULL if not needed.
 * @param button3 Label for the third button, or NULL if not needed.
 * @param width   Policy controlling how button widths are computed.
 * @param type    The alert type that selects the icon and sound.
 *
 * @see BAlert(const char*, const char*, const char*, const char*, const char*,
 *             button_width, button_spacing, alert_type)
 */
BAlert::BAlert(const char *title, const char *text, const char *button1,
		const char *button2, const char *button3, button_width width,
		alert_type type)
	:
	BWindow(BRect(0, 0, 100, 100), title, B_MODAL_WINDOW,
		B_NOT_CLOSABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS)
{
	_Init(text, button1, button2, button3, width, B_EVEN_SPACING, type);
}


/**
 * @brief Constructs a BAlert with full control over button width and spacing policy.
 *
 * @param title   The window title.
 * @param text    The descriptive body text.
 * @param button1 Label for the first button; must not be NULL.
 * @param button2 Label for the second button, or NULL.
 * @param button3 Label for the third button, or NULL.
 * @param width   Policy for computing button widths
 *                (B_WIDTH_AS_USUAL, B_WIDTH_FROM_LABEL, B_WIDTH_FROM_WIDEST).
 * @param spacing Spacing layout for buttons
 *                (B_EVEN_SPACING or B_OFFSET_SPACING).
 * @param type    The alert type (B_INFO_ALERT, B_IDEA_ALERT, B_WARNING_ALERT,
 *                B_STOP_ALERT, or B_EMPTY_ALERT).
 */
BAlert::BAlert(const char *title, const char *text, const char *button1,
		const char *button2, const char *button3, button_width width,
		button_spacing spacing, alert_type type)
	:
	BWindow(BRect(0, 0, 100, 100), title, B_MODAL_WINDOW,
		B_NOT_CLOSABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS)
{
	_Init(text, button1, button2, button3, width, spacing, type);
}


/**
 * @brief Constructs a BAlert from an archived BMessage.
 *
 * Restores the text view, icon view, alert type, and per-button shortcut keys
 * from the archive. Used by the BArchivable instantiation mechanism.
 *
 * @param data The BMessage archive previously produced by Archive().
 *
 * @see Instantiate(), Archive()
 */
BAlert::BAlert(BMessage* data)
	:
	BWindow(data)
{
	fInvoker = NULL;
	fAlertSem = -1;
	fAlertValue = -1;

	fTextView = (BTextView*)FindView("_tv_");

	// TODO: window loses default button on dearchive!
	// TODO: ButtonAt() doesn't work afterwards (also affects shortcuts)

	TAlertView* view = (TAlertView*)FindView("_master_");
	if (view)
		view->SetBitmap(_CreateTypeIcon());

	// Get keys
	char key;
	for (int32 i = 0; i < 3; ++i) {
		if (data->FindInt8("_but_key", i, (int8*)&key) == B_OK)
			fKeys[i] = key;
	}

	AddCommonFilter(new(std::nothrow) _BAlertFilter_(this));
}


/**
 * @brief Destroys the BAlert and releases the synchronization semaphore.
 */
BAlert::~BAlert()
{
	// Probably not necessary, but it makes me feel better.
	if (fAlertSem >= B_OK)
		delete_sem(fAlertSem);
}


/**
 * @brief Instantiates a BAlert from a BMessage archive.
 *
 * @param data The BMessage archive to instantiate from.
 * @return A newly allocated BAlert, or NULL if the archive is invalid or
 *         allocation fails.
 *
 * @see Archive()
 */
BArchivable*
BAlert::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "BAlert"))
		return NULL;

	return new(std::nothrow) BAlert(data);
}


/**
 * @brief Archives this BAlert into a BMessage.
 *
 * Stores the body text ("_text"), alert type ("_atype"), button width policy
 * ("_but_width"), and shortcut keys ("_but_key") in addition to the base
 * BWindow archive fields.
 *
 * @param data  The BMessage to write the archive into.
 * @param deep  If true, child views are archived recursively.
 * @return B_OK on success, or an error code on failure.
 *
 * @see Instantiate()
 */
status_t
BAlert::Archive(BMessage* data, bool deep) const
{
	status_t ret = BWindow::Archive(data, deep);

	// Stow the text
	if (ret == B_OK)
		ret = data->AddString("_text", fTextView->Text());

	// Stow the alert type
	if (ret == B_OK)
		ret = data->AddInt32("_atype", fType);

	// Stow the button width
	if (ret == B_OK)
		ret = data->AddInt32("_but_width", fButtonWidth);

	// Stow the shortcut keys
	if (fKeys[0] || fKeys[1] || fKeys[2]) {
		// If we have any to save, we must save something for everyone so it
		// doesn't get confusing on the unarchive.
		if (ret == B_OK)
			ret = data->AddInt8("_but_key", fKeys[0]);
		if (ret == B_OK)
			ret = data->AddInt8("_but_key", fKeys[1]);
		if (ret == B_OK)
			ret = data->AddInt8("_but_key", fKeys[2]);
	}

	return ret;
}


/**
 * @brief Returns the alert type set on this BAlert.
 *
 * @return The current alert_type value.
 *
 * @see SetType()
 */
alert_type
BAlert::Type() const
{
	return (alert_type)fType;
}


/**
 * @brief Sets the alert type, which controls the icon and notification sound.
 *
 * The icon is not immediately updated; it is loaded lazily during _Prepare()
 * when Go() is called.
 *
 * @param type The new alert type.
 *
 * @see Type(), _CreateTypeIcon(), _PlaySound()
 */
void
BAlert::SetType(alert_type type)
{
	fType = type;
}


/**
 * @brief Sets the body text displayed in the alert's text view.
 *
 * @param text The descriptive message string to display.
 *
 * @see TextView()
 */
void
BAlert::SetText(const char* text)
{
	TextView()->SetText(text);
}


/**
 * @brief Overrides the alert icon with a custom bitmap.
 *
 * Replaces the automatically selected type icon with @a bitmap. The TAlertView
 * takes ownership of the bitmap and deletes the previous one.
 *
 * @param bitmap The custom icon bitmap to display, or NULL to clear the icon.
 *
 * @see SetType()
 */
void
BAlert::SetIcon(BBitmap* bitmap)
{
	fIconView->SetBitmap(bitmap);
}


/**
 * @brief Sets the button spacing layout policy.
 *
 * @param spacing B_EVEN_SPACING to distribute buttons evenly, or
 *                B_OFFSET_SPACING to push the first button to the left.
 */
void
BAlert::SetButtonSpacing(button_spacing spacing)
{
	fButtonSpacing = spacing;
}


/**
 * @brief Sets the button width policy.
 *
 * @param width One of B_WIDTH_AS_USUAL, B_WIDTH_FROM_LABEL, or
 *              B_WIDTH_FROM_WIDEST.
 */
void
BAlert::SetButtonWidth(button_width width)
{
	fButtonWidth = width;
}


/**
 * @brief Assigns a keyboard shortcut to a button.
 *
 * When the user presses @a key while the alert is open, the button at
 * @a index is activated as if clicked.
 *
 * @param index The zero-based button index.
 * @param key   The ASCII shortcut character.
 *
 * @see Shortcut()
 */
void
BAlert::SetShortcut(int32 index, char key)
{
	if (index >= 0 && (size_t)index < fKeys.size())
		fKeys[index] = key;
}


/**
 * @brief Returns the keyboard shortcut assigned to a button.
 *
 * @param index The zero-based button index.
 * @return The shortcut character, or 0 if @a index is out of range.
 *
 * @see SetShortcut()
 */
char
BAlert::Shortcut(int32 index) const
{
	if (index >= 0 && (size_t)index < fKeys.size())
		return fKeys[index];

	return 0;
}


/**
 * @brief Displays the alert synchronously and returns the index of the pressed button.
 *
 * Shows the alert window and blocks the calling thread until the user presses a
 * button. If the calling thread belongs to a BWindow, that window is kept
 * updated via UpdateIfNeeded() while waiting so it remains responsive. The
 * alert window is destroyed before this function returns.
 *
 * @return The zero-based index of the button pressed by the user, or -1 if the
 *         semaphore could not be created.
 *
 * @note Do NOT call Lock() on the BAlert after Go() returns; the window has
 *       already been deleted.
 * @see Go(BInvoker*)
 */
int32
BAlert::Go()
{
	fAlertSem = create_sem(0, "AlertSem");
	if (fAlertSem < 0) {
		Quit();
		return -1;
	}

	// Get the originating window, if it exists
	BWindow* window = dynamic_cast<BWindow*>(
		BLooper::LooperForThread(find_thread(NULL)));

	_Prepare();
	Show();
	_PlaySound();

	if (window != NULL) {
		status_t status;
		for (;;) {
			do {
				status = acquire_sem_etc(fAlertSem, 1, B_RELATIVE_TIMEOUT,
					kSemTimeOut);
				// We've (probably) had our time slice taken away from us
			} while (status == B_INTERRUPTED);

			if (status == B_BAD_SEM_ID) {
				// Semaphore was finally nuked in MessageReceived
				break;
			}
			window->UpdateIfNeeded();
		}
	} else {
		// No window to update, so just hang out until we're done.
		while (acquire_sem(fAlertSem) == B_INTERRUPTED) {
		}
	}

	// Have to cache the value since we delete on Quit()
	int32 value = fAlertValue;
	if (Lock())
		Quit();

	return value;
}


/**
 * @brief Displays the alert asynchronously, notifying @a invoker when dismissed.
 *
 * Shows the alert window without blocking the calling thread. When the user
 * presses a button, the pressed button's index is added to the invoker's
 * message under the key "which", and the invoker is triggered. The alert
 * window then destroys itself.
 *
 * @param invoker The BInvoker to notify on dismissal. The BAlert takes
 *                ownership of the invoker.
 * @return B_OK always.
 *
 * @see Go()
 */
status_t
BAlert::Go(BInvoker* invoker)
{
	fInvoker = invoker;
	_Prepare();
	Show();
	_PlaySound();
	return B_OK;
}


/**
 * @brief Handles the internal button-press message and wakes the synchronous Go().
 *
 * When a button is pressed, the message with @c what == kAlertButtonMsg is
 * received here. In synchronous mode the semaphore is deleted to unblock the
 * Go() call; in asynchronous mode the invoker is triggered with the button
 * index, then the window quits.
 *
 * @param msg The incoming BMessage; only kAlertButtonMsg is handled here.
 *
 * @see Go(), Go(BInvoker*)
 */
void
BAlert::MessageReceived(BMessage* msg)
{
	if (msg->what != kAlertButtonMsg)
		return BWindow::MessageReceived(msg);

	int32 which;
	if (msg->FindInt32("which", &which) == B_OK) {
		if (fAlertSem < 0) {
			// Semaphore hasn't been created; we're running asynchronous
			if (fInvoker != NULL) {
				BMessage* out = fInvoker->Message();
				if (out && (out->ReplaceInt32("which", which) == B_OK
							|| out->AddInt32("which", which) == B_OK))
					fInvoker->Invoke();
			}
			PostMessage(B_QUIT_REQUESTED);
		} else {
			// Created semaphore means were running synchronously
			fAlertValue = which;

			// TextAlertVar does release_sem() below, and then sets the
			// member var.  That doesn't make much sense to me, since we
			// want to be able to clean up at some point.  Better to just
			// nuke the semaphore now; we don't need it any more and this
			// lets synchronous Go() continue just as well.
			delete_sem(fAlertSem);
			fAlertSem = -1;
		}
	}
}


/**
 * @brief Handles frame resize events by delegating to the base class.
 *
 * @param newWidth  The new window width.
 * @param newHeight The new window height.
 */
void
BAlert::FrameResized(float newWidth, float newHeight)
{
	BWindow::FrameResized(newWidth, newHeight);
}


/**
 * @brief Adds a new button to the alert with an optional keyboard shortcut.
 *
 * The button is appended to the right side of the button layout and becomes
 * the new default button. Does nothing if @a label is NULL or empty.
 *
 * @param label The button label string.
 * @param key   An optional keyboard shortcut character, or 0 for none.
 *
 * @see CountButtons(), ButtonAt(), SetShortcut()
 */
void
BAlert::AddButton(const char* label, char key)
{
	if (label == NULL || label[0] == '\0')
		return;

	BButton* button = _CreateButton(fButtons.size(), label);
	fButtons.push_back(button);
	fKeys.push_back(key);

	SetDefaultButton(button);
	fButtonLayout->AddView(button);
}


/**
 * @brief Returns the number of buttons currently added to the alert.
 *
 * @return The button count.
 *
 * @see ButtonAt(), AddButton()
 */
int32
BAlert::CountButtons() const
{
	return (int32)fButtons.size();
}


/**
 * @brief Returns the button at the given zero-based index.
 *
 * @param index The button index (0-based).
 * @return A pointer to the BButton, or NULL if @a index is out of range.
 *
 * @see CountButtons()
 */
BButton*
BAlert::ButtonAt(int32 index) const
{
	if (index >= 0 && (size_t)index < fButtons.size())
		return fButtons[index];

	return NULL;
}


/**
 * @brief Returns the BTextView used for the alert body text.
 *
 * @return A pointer to the internal BTextView; never NULL after construction.
 *
 * @see SetText()
 */
BTextView*
BAlert::TextView() const
{
	return fTextView;
}


/**
 * @brief Resolves a scripting specifier by delegating to BWindow.
 *
 * @param msg       The scripting message.
 * @param index     The specifier index.
 * @param specifier The specifier message.
 * @param form      The specifier form.
 * @param property  The property name.
 * @return The resolved BHandler, or NULL.
 */
BHandler*
BAlert::ResolveSpecifier(BMessage* msg, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	return BWindow::ResolveSpecifier(msg, index, specifier, form, property);
}


/**
 * @brief Reports supported scripting suites by delegating to BWindow.
 *
 * @param data The BMessage to populate with suite information.
 * @return B_OK on success.
 */
status_t
BAlert::GetSupportedSuites(BMessage* data)
{
	return BWindow::GetSupportedSuites(data);
}


/**
 * @brief Dispatches a message to the appropriate handler by delegating to BWindow.
 *
 * @param msg     The message to dispatch.
 * @param handler The target handler.
 */
void
BAlert::DispatchMessage(BMessage* msg, BHandler* handler)
{
	BWindow::DispatchMessage(msg, handler);
}


/**
 * @brief Quits the alert window by delegating to BWindow::Quit().
 */
void
BAlert::Quit()
{
	BWindow::Quit();
}


/**
 * @brief Handles the quit request by delegating to BWindow::QuitRequested().
 *
 * @return true to allow quitting.
 */
bool
BAlert::QuitRequested()
{
	return BWindow::QuitRequested();
}


/**
 * @brief Computes a centered on-screen position for an alert window.
 *
 * Places the alert horizontally centered and approximately one quarter of the
 * way down the screen. Falls back to a 640x480 frame if the screen is invalid.
 *
 * @param width  The desired width of the alert.
 * @param height The desired height of the alert.
 * @return The recommended top-left BPoint.
 *
 * @note This method is deprecated; use BWindow::CenterIn() instead.
 */
BPoint
BAlert::AlertPosition(float width, float height)
{
	BPoint result(100, 100);

	BWindow* window =
		dynamic_cast<BWindow*>(BLooper::LooperForThread(find_thread(NULL)));

	BScreen screen(window);
	BRect screenFrame(0, 0, 640, 480);
	if (screen.IsValid())
		screenFrame = screen.Frame();

	// Horizontally, we're smack in the middle
	result.x = screenFrame.left + (screenFrame.Width() / 2.0) - (width / 2.0);

	// This is probably sooo wrong, but it looks right on 1024 x 768
	result.y = screenFrame.top + (screenFrame.Height() / 4.0) - ceil(height / 3.0);

	return result;
}


/**
 * @brief Handles binary-compatibility perform codes.
 *
 * Currently handles PERFORM_CODE_SET_LAYOUT; all other codes are forwarded to
 * BWindow::Perform().
 *
 * @param code  The perform operation code.
 * @param _data Opaque pointer to the perform-specific data structure.
 * @return B_OK if the code was handled, otherwise the result from BWindow.
 */
status_t
BAlert::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_SET_LAYOUT:
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BAlert::SetLayout(data->layout);
			return B_OK;
	}

	return BWindow::Perform(code, _data);
}


void BAlert::_ReservedAlert1() {}
void BAlert::_ReservedAlert2() {}
void BAlert::_ReservedAlert3() {}


/**
 * @brief Initializes the alert's internal state and builds its view hierarchy.
 *
 * Creates the TAlertView icon strip, the BTextView body, the button group
 * layout, sets the alert type and button width policy, and then adds the
 * supplied buttons. A key-filter is installed to support keyboard shortcuts.
 *
 * @param text        The body text to display, or NULL for no text.
 * @param button1     Label for the first button, or NULL to skip.
 * @param button2     Label for the second button, or NULL to skip.
 * @param button3     Label for the third button, or NULL to skip.
 * @param buttonWidth Button width policy.
 * @param spacing     Button spacing policy.
 * @param type        Alert type controlling the icon and sound.
 *
 * @see BAlert(), AddButton(), _CreateTypeIcon()
 */
void
BAlert::_Init(const char* text, const char* button1, const char* button2,
	const char* button3, button_width buttonWidth, button_spacing spacing,
	alert_type type)
{
	fInvoker = NULL;
	fAlertSem = -1;
	fAlertValue = -1;

	fIconView = new TAlertView();

	fTextView = new BTextView("_tv_");
	fTextView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
	fTextView->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
	fTextView->MakeEditable(false);
	fTextView->MakeSelectable(false);
	fTextView->SetWordWrap(true);
	fTextView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	fButtonLayout = new BGroupLayout(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING);

	SetType(type);
	SetButtonWidth(buttonWidth);
	SetButtonSpacing(spacing);
	SetText(text);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.Add(fIconView)
		.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_HALF_ITEM_INSETS)
			.Add(fTextView)
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.Add(fButtonLayout);

	AddButton(button1);
	AddButton(button2);
	AddButton(button3);

	AddCommonFilter(new(std::nothrow) _BAlertFilter_(this));
}


/**
 * @brief Loads the icon bitmap corresponding to the current alert type.
 *
 * Looks up the system icon name for the alert type, allocates an RGBA bitmap
 * scaled via be_control_look->ComposeIconSize(32), and fills it using
 * BIconUtils::GetSystemIcon(). Returns NULL for B_EMPTY_ALERT or unknown types.
 *
 * @return A newly allocated BBitmap containing the icon, or NULL if no icon
 *         should be displayed. The caller takes ownership.
 *
 * @see SetType(), _Init()
 */
BBitmap*
BAlert::_CreateTypeIcon()
{
	if (Type() == B_EMPTY_ALERT)
		return NULL;

	// The icons are in the app_server resources
	BBitmap* icon = NULL;

	// Which icon are we trying to load?
	const char* iconName;
	switch (fType) {
		case B_INFO_ALERT:
			iconName = "dialog-information";
			break;
		case B_IDEA_ALERT:
			iconName = "dialog-idea";
			break;
		case B_WARNING_ALERT:
			iconName = "dialog-warning";
			break;
		case B_STOP_ALERT:
			iconName = "dialog-error";
			break;

		default:
			// Alert type is either invalid or B_EMPTY_ALERT;
			// either way, we're not going to load an icon
			return NULL;
	}

	// Allocate the icon bitmap
	icon = new(std::nothrow) BBitmap(BRect(BPoint(0, 0), be_control_look->ComposeIconSize(32)),
		0, B_RGBA32);
	if (icon == NULL || icon->InitCheck() < B_OK) {
		FTRACE((stderr, "BAlert::_CreateTypeIcon() - No memory for bitmap\n"));
		delete icon;
		return NULL;
	}

	// Load the raw icon data
	BIconUtils::GetSystemIcon(iconName, icon);

	return icon;
}


/**
 * @brief Allocates a BButton wired to send kAlertButtonMsg with its index.
 *
 * Generates an internal name of the form "_b<which>_" and creates a BButton
 * whose message carries the button index under the "which" key.
 *
 * @param which The zero-based button index embedded in the button's message.
 * @param label The button label string.
 * @return A newly allocated BButton, or NULL on memory allocation failure.
 */
BButton*
BAlert::_CreateButton(int32 which, const char* label)
{
	BMessage* message = new BMessage(kAlertButtonMsg);
	if (message == NULL)
		return NULL;

	message->AddInt32("which", which);

	char name[32];
	snprintf(name, sizeof(name), "_b%" B_PRId32 "_", which);

	return new(std::nothrow) BButton(name, label, message);
}


/**
 * @brief Applies final layout tweaks and positions the alert window before display.
 *
 * Enforces minimum button widths according to the button-width policy,
 * optionally inserts a horizontal strut for offset spacing, sets the minimum
 * window width, calls ResizeToPreferred(), and centers the alert over its
 * parent window (or the screen if there is no parent).
 *
 * @note Calls debugger() if no buttons have been added.
 *
 * @see Go(), AddButton()
 */
void
BAlert::_Prepare()
{
	// Must have at least one button
	if (CountButtons() == 0)
		debugger("BAlerts must have at least one button.");

	float fontFactor = be_plain_font->Size() / 11.0f;

	if (fIconView->Bitmap() == NULL)
		fIconView->SetBitmap(_CreateTypeIcon());

	if (fButtonWidth == B_WIDTH_AS_USUAL) {
		float usualWidth = kButtonUsualWidth * fontFactor;

		for (int32 index = 0; index < CountButtons(); index++) {
			BButton* button = ButtonAt(index);
			if (button->MinSize().width < usualWidth)
				button->SetExplicitSize(BSize(usualWidth, B_SIZE_UNSET));
		}
	} else if (fButtonWidth == B_WIDTH_FROM_WIDEST) {
		// Get width of widest label
		float maxWidth = 0;
		for (int32 index = 0; index < CountButtons(); index++) {
			BButton* button = ButtonAt(index);
			float width;
			button->GetPreferredSize(&width, NULL);

			if (width > maxWidth)
				maxWidth = width;
		}
		for (int32 index = 0; index < CountButtons(); index++) {
			BButton* button = ButtonAt(index);
			button->SetExplicitSize(BSize(maxWidth, B_SIZE_UNSET));
		}
	}

	if (fButtonSpacing == B_OFFSET_SPACING && CountButtons() > 1) {
		// Insert some strut
		fButtonLayout->AddItem(1, BSpaceLayoutItem::CreateHorizontalStrut(
			kButtonOffsetSpacing * fontFactor));
	}

	// Position the alert so that it is centered vertically but offset a bit
	// horizontally in the parent window's frame or, if unavailable, the
	// screen frame.
	float minWindowWidth = (fButtonSpacing == B_OFFSET_SPACING
		? kWindowOffsetMinWidth : kWindowMinWidth) * fontFactor;
	GetLayout()->SetExplicitMinSize(BSize(minWindowWidth, B_SIZE_UNSET));

	ResizeToPreferred();

	// Return early if we've already been moved...
	if (Frame().left != 0 && Frame().right != 0)
		return;

	// otherwise center ourselves on-top of parent window/screen
	BWindow* parent = dynamic_cast<BWindow*>(BLooper::LooperForThread(
		find_thread(NULL)));
	const BRect frame = parent != NULL ? parent->Frame()
		: BScreen(this).Frame();

	MoveTo(static_cast<BWindow*>(this)->AlertPosition(frame));
		// Hidden by BAlert::AlertPosition()
}


/**
 * @brief Plays the system sound associated with the current alert type.
 *
 * Maps B_INFO_ALERT to MEDIA_SOUNDS_INFORMATION_ALERT, B_WARNING_ALERT to
 * MEDIA_SOUNDS_IMPORTANT_ALERT, and B_STOP_ALERT to MEDIA_SOUNDS_ERROR_ALERT.
 * All other types are silent.
 *
 * @see SetType(), _Prepare()
 */
void
BAlert::_PlaySound()
{
	switch (Type()) {
		case B_INFO_ALERT:
			system_beep(MEDIA_SOUNDS_INFORMATION_ALERT);
			break;
		case B_WARNING_ALERT:
			system_beep(MEDIA_SOUNDS_IMPORTANT_ALERT);
			break;
		case B_STOP_ALERT:
			system_beep(MEDIA_SOUNDS_ERROR_ALERT);
			break;

		default:
			break;
	}
}


//	#pragma mark - TAlertView


/**
 * @brief Constructs a TAlertView with no icon bitmap.
 *
 * Sets the view color to the panel background; the icon will be assigned
 * later via SetBitmap() when the alert type is resolved.
 */
TAlertView::TAlertView()
	:
	BView("TAlertView", B_WILL_DRAW),
	fIconBitmap(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


/**
 * @brief Constructs a TAlertView from an archive; icon bitmap starts as NULL.
 *
 * @param archive The BMessage archive to restore from.
 */
TAlertView::TAlertView(BMessage* archive)
	:
	BView(archive),
	fIconBitmap(NULL)
{
}


/**
 * @brief Destroys the TAlertView and frees the icon bitmap.
 */
TAlertView::~TAlertView()
{
	delete fIconBitmap;
}


/**
 * @brief Instantiates a TAlertView from an archive.
 *
 * @param archive The BMessage archive.
 * @return A newly allocated TAlertView, or NULL if the archive is invalid.
 */
TAlertView*
TAlertView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "TAlertView"))
		return NULL;

	return new(std::nothrow) TAlertView(archive);
}


/**
 * @brief Archives the TAlertView by delegating to BView::Archive().
 *
 * @param archive The target BMessage.
 * @param deep    If true, child views are archived recursively.
 * @return B_OK on success, or an error code on failure.
 */
status_t
TAlertView::Archive(BMessage* archive, bool deep) const
{
	return BView::Archive(archive, deep);
}


/**
 * @brief Replaces the icon bitmap and triggers a layout/redraw update.
 *
 * Deletes the previous bitmap (if any) and invalidates the view. If the bitmap
 * dimensions change, a full layout invalidation is requested so the stripe
 * resizes correctly.
 *
 * @param icon The new icon bitmap, or NULL to clear the icon.
 */
void
TAlertView::SetBitmap(BBitmap* icon)
{
	if (icon == NULL && fIconBitmap == NULL)
		return;

	ASSERT(icon != fIconBitmap);

	BBitmap* oldBitmap = fIconBitmap;
	fIconBitmap = icon;
	Invalidate();

	if (oldBitmap == NULL || icon == NULL || oldBitmap->Bounds() != icon->Bounds())
		InvalidateLayout();

	delete oldBitmap;
}


/**
 * @brief Reports the preferred size of the icon stripe view.
 *
 * Width is three times DefaultLabelSpacing() plus the icon width (or a
 * B_LARGE_ICON placeholder when no icon is set). Height follows the same
 * logic using the icon height.
 *
 * @param _width  Receives the preferred width; may be NULL.
 * @param _height Receives the preferred height; may be NULL.
 */
void
TAlertView::GetPreferredSize(float* _width, float* _height)
{
	if (_width != NULL) {
		*_width = be_control_look->DefaultLabelSpacing() * 3;
		if (fIconBitmap != NULL)
			*_width += fIconBitmap->Bounds().Width();
		else
			*_width += be_control_look->ComposeIconSize(B_LARGE_ICON).Width();
	}

	if (_height != NULL) {
		*_height = be_control_look->DefaultLabelSpacing();
		if (fIconBitmap != NULL)
			*_height += fIconBitmap->Bounds().Height();
		else
			*_height += be_control_look->ComposeIconSize(B_LARGE_ICON).Height();
	}
}


/**
 * @brief Returns the maximum size of the icon stripe (unlimited height).
 *
 * @return A BSize with the minimum width and B_SIZE_UNLIMITED height.
 */
BSize
TAlertView::MaxSize()
{
	return BSize(MinSize().width, B_SIZE_UNLIMITED);
}


/**
 * @brief Draws the icon stripe background and composites the icon bitmap.
 *
 * Fills the left portion of the view with a slightly darkened stripe, then
 * draws the icon bitmap with alpha blending at the standard inset offset.
 * Does nothing beyond the stripe fill when no icon bitmap is set.
 *
 * @param updateRect The region that needs to be redrawn.
 */
void
TAlertView::Draw(BRect updateRect)
{
	// Here's the fun stuff
	BRect stripeRect = Bounds();
	stripeRect.right = kIconStripeWidthFactor * be_control_look->DefaultLabelSpacing();
	SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
	FillRect(stripeRect);

	if (fIconBitmap == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmapAsync(fIconBitmap, BPoint(be_control_look->DefaultLabelSpacing() * 3,
		be_control_look->DefaultLabelSpacing()));
}


//	#pragma mark - _BAlertFilter_


/**
 * @brief Constructs a key-down filter for the given BAlert.
 *
 * @param alert The owning BAlert whose shortcut table is consulted on key events.
 */
_BAlertFilter_::_BAlertFilter_(BAlert* alert)
	: BMessageFilter(B_KEY_DOWN),
	fAlert(alert)
{
}


/**
 * @brief Destroys the alert key filter.
 */
_BAlertFilter_::~_BAlertFilter_()
{
}


/**
 * @brief Intercepts key-down events and activates the matching shortcut button.
 *
 * Reads the "byte" field from the key-down message and compares it against each
 * button's shortcut. If a match is found the button's KeyDown() handler is
 * invoked with a space character (to simulate a press) and the message is
 * consumed.
 *
 * @param msg    The BMessage to examine.
 * @param target The current message target (unused).
 * @return B_SKIP_MESSAGE if a shortcut matched; B_DISPATCH_MESSAGE otherwise.
 */
filter_result
_BAlertFilter_::Filter(BMessage* msg, BHandler** target)
{
	if (msg->what == B_KEY_DOWN) {
		char byte;
		if (msg->FindInt8("byte", (int8*)&byte) == B_OK) {
			for (int i = 0; i < fAlert->CountButtons(); ++i) {
				if (byte == fAlert->Shortcut(i) && fAlert->ButtonAt(i)) {
					char space = ' ';
					fAlert->ButtonAt(i)->KeyDown(&space, 1);

					return B_SKIP_MESSAGE;
				}
			}
		}
	}

	return B_DISPATCH_MESSAGE;
}
