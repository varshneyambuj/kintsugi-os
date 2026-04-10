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
 *   Copyright 2001-2025 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Axel Dörfler, axeld@pinc-software.de
 *       Adrian Oanca, adioanca@cotty.iren.ro
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Window.cpp
 * @brief Implementation of BWindow, the top-level windowed container
 *
 * BWindow manages a native application window: its title, type, feel, look,
 * position, and size. It runs its own message loop (as a BLooper), dispatches
 * keyboard/mouse events to child BViews, handles menu shortcuts, and
 * coordinates layout updates. It communicates with the app_server to create
 * and update the on-screen window.
 *
 * @see BView, BLooper, BMenu, BLayout
 */


#include <Window.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <AppMisc.h>
#include <AppServerLink.h>
#include <ApplicationPrivate.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Button.h>
#include <Deskbar.h>
#include <DirectMessageTarget.h>
#include <FindDirectory.h>
#include <InputServerTypes.h>
#include <Layout.h>
#include <LayoutUtils.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MenuPrivate.h>
#include <MessagePrivate.h>
#include <MessageQueue.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PortLink.h>
#include <PropertyInfo.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <Screen.h>
#include <ServerProtocol.h>
#include <String.h>
#include <TextView.h>
#include <TokenSpace.h>
#include <ToolTipManager.h>
#include <ToolTipWindow.h>
#include <UnicodeChar.h>
#include <WindowPrivate.h>

#include <binary_compatibility/Interface.h>
#include <input_globals.h>
#include <tracker_private.h>


//#define DEBUG_WIN
#ifdef DEBUG_WIN
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

/** @brief Internal message code to hide all windows of the application (equivalent to iconifying the team). */
#define B_HIDE_APPLICATION '_AHD'
	// if we ever move this to a public namespace, we should also move the
	// handling of this message into BApplication

/** @brief Internal message code sent by the minimize keyboard shortcut handler. */
#define _MINIMIZE_			'_WMZ'
/** @brief Internal message code sent by the zoom keyboard shortcut handler. */
#define _ZOOM_				'_WZO'
/** @brief Internal message code that sends the window behind all others. */
#define _SEND_BEHIND_		'_WSB'
/** @brief Internal message code that raises the window to the front. */
#define _SEND_TO_FRONT_		'_WSF'


/**
 * @brief Minimizes or restores the team's windows using the Deskbar animation.
 *
 * @param zoomRect Source rectangle used for the zoom animation.
 * @param team     The team whose windows should be minimized or restored.
 * @param zoom     If @c true, restore (un-minimize); if @c false, minimize.
 */
void do_minimize_team(BRect zoomRect, team_id team, bool zoom);


/**
 * @brief Cookie used by _UnpackMessage() to iterate over all targets of a
 *        single preferred-handler input message.
 *
 * Because an app_server input event may carry multiple view tokens inside one
 * BMessage (e.g. the focus view plus every view that overlaps the mouse
 * cursor), this cookie tracks state across repeated calls to _UnpackMessage()
 * so each target receives its own copy of the message.
 */
struct BWindow::unpack_cookie {
	/**
	 * @brief Initialises the cookie to the sentinel state before the first
	 *        call to _UnpackMessage().
	 */
	unpack_cookie();

	/** @brief The original message being distributed; NULL signals iteration end. */
	BMessage*	message;
	/** @brief Index of the next "_token" field entry to process. */
	int32		index;
	/** @brief The focus-view handler extracted from the original target. */
	BHandler*	focus;
	/** @brief Token of the focus view, used for identity checks. */
	int32		focus_token;
	/** @brief Token of the last-mouse-moved view at the start of iteration. */
	int32		last_view_token;
	/** @brief Set to @c true once the focus token appears in the token list. */
	bool		found_focus;
	/** @brief Set to @c true once all "_token" fields have been scanned. */
	bool		tokens_scanned;
};


/**
 * @brief Represents a single registered keyboard shortcut in a BWindow.
 *
 * A Shortcut associates a key (normalised to upper-case Unicode) and a set of
 * modifier keys with either a BMenuItem (which is invoked directly) or a
 * BMessage (posted to an optional BHandler target).  BWindow owns the list of
 * Shortcut objects and is responsible for their lifetime.
 *
 * @see BWindow::AddShortcut(), BWindow::RemoveShortcut(), BWindow::HasShortcut()
 */
class BWindow::Shortcut {
public:
	/**
	 * @brief Constructs a shortcut that invokes a menu item.
	 *
	 * @param key       Raw Unicode code point for the shortcut key.
	 * @param modifiers Modifier-key bitmask (e.g. B_COMMAND_KEY).
	 * @param item      The BMenuItem to invoke when the shortcut fires.
	 */
						Shortcut(uint32 key, uint32 modifiers,
							BMenuItem* item);

	/**
	 * @brief Constructs a shortcut that posts a message to a handler.
	 *
	 * The Shortcut takes ownership of @a message and will delete it when
	 * the Shortcut itself is destroyed.
	 *
	 * @param key       Raw Unicode code point for the shortcut key.
	 * @param modifiers Modifier-key bitmask.
	 * @param message   The BMessage to post; ownership is transferred.
	 * @param target    Optional target handler; if NULL the focus view is used.
	 */
						Shortcut(uint32 key, uint32 modifiers,
							BMessage* message, BHandler* target);

	/**
	 * @brief Destructor; deletes the owned BMessage, if any.
	 */
						~Shortcut();

	/**
	 * @brief Returns @c true if this shortcut matches the given key and modifiers.
	 *
	 * @param key                Raw key value (already prepared via PrepareKey()).
	 * @param preparedModifiers  Modifier mask already prepared via PrepareModifiers().
	 * @return @c true on match, @c false otherwise.
	 */
		bool			Matches(uint32 key, uint32 preparedModifiers) const;

	/** @brief Returns the normalised (upper-case) key code. */
		uint32			Key() const { return fKey; };

	/**
	 * @brief Returns the modifier mask as reported to the caller.
	 *
	 * Adds B_NO_COMMAND_KEY to the prepared modifiers when B_COMMAND_KEY is
	 * absent, so callers can distinguish "explicitly without Command" from
	 * "Command implied by default".
	 */
		uint32			Modifiers() const;

	/** @brief Returns the internally prepared modifier bitmask. */
		uint32			PreparedModifiers() const { return fPreparedModifiers; };
	/** @brief Returns the associated BMenuItem, or NULL if this is a message shortcut. */
		BMenuItem*		MenuItem() const { return fMenuItem; }
	/** @brief Returns the associated BMessage, or NULL if this is a menu-item shortcut. */
		BMessage*		Message() const { return fMessage; }
	/** @brief Returns the target BHandler for message shortcuts, or NULL for the focus view. */
		BHandler*		Target() const { return fTarget; }

	/**
	 * @brief Returns the modifier-key bits that are eligible to participate in shortcuts.
	 *
	 * Only Command, Option, Shift, Control, and Menu modifier keys are considered.
	 */
	static	uint32			AllowedModifiers();

	/**
	 * @brief Normalises a key code to upper-case Unicode for comparison.
	 *
	 * @param key Raw Unicode code point.
	 * @return Upper-case code point.
	 */
	static	uint32			PrepareKey(uint32 key);

	/**
	 * @brief Strips disallowed modifiers and enforces the Command-key convention.
	 *
	 * If B_NO_COMMAND_KEY is present the Command bit is cleared; otherwise it
	 * is forced on.  Only bits in AllowedModifiers() are kept.
	 *
	 * @param modifiers Raw modifier bitmask.
	 * @return Normalised modifier bitmask.
	 */
	static	uint32			PrepareModifiers(uint32 modifiers);

private:
		/** @brief Normalised (upper-case) key code. */
		uint32			fKey;
		/** @brief Prepared modifier bitmask used for matching. */
		uint32			fPreparedModifiers;
		/** @brief Menu item to invoke, or NULL for message shortcuts. */
		BMenuItem*		fMenuItem;
		/** @brief Owned message to post, or NULL for menu-item shortcuts. */
		BMessage*		fMessage;
		/** @brief Target handler for message shortcuts, or NULL for focus view. */
		BHandler*		fTarget;
};


using BPrivate::gDefaultTokens;
using BPrivate::MenuPrivate;

/**
 * @brief Scripting property table for BWindow.
 *
 * Defines the properties exposed via the BeOS scripting protocol, including
 * "Active", "Feel", "Flags", "Frame", "Hidden", "Look", "Title",
 * "Workspaces", "MenuBar", "View" (count and specifier), "Minimize",
 * and "TabFrame" (read-only decorator tab rectangle).
 *
 * @see BWindow::ResolveSpecifier(), BWindow::GetSupportedSuites()
 */
static property_info sWindowPropInfo[] = {
	{
		"Active", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_BOOL_TYPE }
	},

	{
		"Feel", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"Flags", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"Frame", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_RECT_TYPE }
	},

	{
		"Hidden", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_BOOL_TYPE }
	},

	{
		"Look", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"Title", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_STRING_TYPE }
	},

	{
		"Workspaces", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"MenuBar", {},
		{ B_DIRECT_SPECIFIER }, NULL, 0, {}
	},

	{
		"View", { B_COUNT_PROPERTIES },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"View", {}, {}, NULL, 0, {}
	},

	{
		"Minimize", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_BOOL_TYPE }
	},

	{
		"TabFrame", { B_GET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_RECT_TYPE }
	},

	{ 0 }
};

/**
 * @brief Scripting command-value table for BWindow.
 *
 * Describes the scripting commands "MoveTo", "MoveBy", "ResizeTo", and
 * "ResizeBy" that can be sent to a BWindow via the BeOS scripting protocol.
 *
 * @see BWindow::GetSupportedSuites()
 */
static value_info sWindowValueInfo[] = {
	{
		"MoveTo", 'WDMT', B_COMMAND_KIND,
		"Moves to the position in the BPoint data"
	},

	{
		"MoveBy", 'WDMB', B_COMMAND_KIND,
		"Moves by the offsets in the BPoint data"
	},

	{
		"ResizeTo", 'WDRT', B_COMMAND_KIND,
		"Resize to the size in the BPoint data"
	},

	{
		"ResizeBy", 'WDRB', B_COMMAND_KIND,
		"Resize by the offsets in the BPoint data"
	},

	{ 0 }
};


/**
 * @brief Sets the menu-tracking semaphore on a BWindow.
 *
 * Called by BMenuBar to install a semaphore that the window destructor
 * acquires before destroying itself, ensuring that an in-progress menu
 * tracking loop finishes safely.
 *
 * @param window The window whose fMenuSem field will be updated.
 * @param sem    The semaphore ID to install, or -1 to clear it.
 */
void
_set_menu_sem_(BWindow* window, sem_id sem)
{
	if (window != NULL)
		window->fMenuSem = sem;
}


//	#pragma mark -


/**
 * @brief Constructs an unpack_cookie ready for the first _UnpackMessage() call.
 *
 * Sets @c message to a non-NULL sentinel value (@c ~0UL) so that the initial
 * check inside _UnpackMessage() can distinguish "not yet started" from "done".
 * All token fields are initialised to @c B_NULL_TOKEN and boolean flags to
 * @c false.
 */
BWindow::unpack_cookie::unpack_cookie()
	:
	message((BMessage*)~0UL),
		// message == NULL is our exit condition
	index(0),
	focus_token(B_NULL_TOKEN),
	last_view_token(B_NULL_TOKEN),
	found_focus(false),
	tokens_scanned(false)
{
}


//	#pragma mark - BWindow::Shortcut


/**
 * @brief Constructs a Shortcut that will invoke a BMenuItem.
 *
 * The key and modifiers are normalised via PrepareKey() and
 * PrepareModifiers() so that comparisons are consistent regardless of
 * how the caller specified them.
 *
 * @param key       Raw Unicode code point for the shortcut key.
 * @param modifiers Modifier-key bitmask (e.g. B_COMMAND_KEY).
 * @param item      The BMenuItem to invoke; ownership is not transferred.
 */
BWindow::Shortcut::Shortcut(uint32 key, uint32 modifiers, BMenuItem* item)
	:
	fKey(PrepareKey(key)),
	fPreparedModifiers(PrepareModifiers(modifiers)),
	fMenuItem(item),
	fMessage(NULL),
	fTarget(NULL)
{
}


/**
 * @brief Constructs a Shortcut that posts a BMessage to an optional handler.
 *
 * The Shortcut takes ownership of @a message and will delete it on destruction.
 * If @a target is NULL the message is posted to the focus view at the time the
 * shortcut fires.
 *
 * @param key       Raw Unicode code point for the shortcut key.
 * @param modifiers Modifier-key bitmask.
 * @param message   The BMessage to post; ownership is transferred.
 * @param target    Target BHandler, or NULL to use the current focus view.
 */
BWindow::Shortcut::Shortcut(uint32 key, uint32 modifiers, BMessage* message,
	BHandler* target)
	:
	fKey(PrepareKey(key)),
	fPreparedModifiers(PrepareModifiers(modifiers)),
	fMenuItem(NULL),
	fMessage(message),
	fTarget(target)
{
}


/**
 * @brief Destructs the Shortcut, releasing the owned BMessage if present.
 */
BWindow::Shortcut::~Shortcut()
{
	// we own the message, if any
	delete fMessage;
}


/**
 * @brief Returns @c true if this shortcut matches the given prepared key and modifiers.
 *
 * Both arguments must already have been processed through PrepareKey() and
 * PrepareModifiers() respectively.
 *
 * @param key                Normalised key code.
 * @param preparedModifiers  Normalised modifier bitmask.
 * @return @c true on an exact match, @c false otherwise.
 */
bool
BWindow::Shortcut::Matches(uint32 key, uint32 preparedModifiers) const
{
	return fKey == key && fPreparedModifiers == preparedModifiers;
}


/**
 * @brief Returns the modifier mask as it should be presented to external callers.
 *
 * Adds the B_NO_COMMAND_KEY pseudo-bit when the Command key is absent so that
 * callers can distinguish an explicitly command-free shortcut from one where
 * the Command key is implied by the default.
 *
 * @return The modifier bitmask, including B_NO_COMMAND_KEY if appropriate.
 */
uint32
BWindow::Shortcut::Modifiers() const
{
	return fPreparedModifiers
		| (((fPreparedModifiers & B_COMMAND_KEY) == 0) ? B_NO_COMMAND_KEY : 0);
}


/**
 * @brief Returns the bitmask of modifier keys that are eligible for shortcuts.
 *
 * Only Command, Option, Shift, Control, and Menu modifier bits are considered.
 * All other bits are stripped out by PrepareModifiers().
 *
 * @return Bitmask of allowed modifier keys.
 */
/*static*/
uint32
BWindow::Shortcut::AllowedModifiers()
{
	return B_COMMAND_KEY | B_OPTION_KEY | B_SHIFT_KEY | B_CONTROL_KEY | B_MENU_KEY;
}


/**
 * @brief Normalises a raw modifier bitmask for shortcut matching.
 *
 * Strips any bits not in AllowedModifiers().  If @c B_NO_COMMAND_KEY is set
 * the Command bit is removed; otherwise it is forced on (Command is implied
 * by default for shortcuts).
 *
 * @param modifiers Raw modifier bitmask from a key event or caller.
 * @return Normalised modifier bitmask.
 */
/*static*/
uint32
BWindow::Shortcut::PrepareModifiers(uint32 modifiers)
{
	if ((modifiers & B_NO_COMMAND_KEY) != 0)
		return (modifiers & AllowedModifiers()) & ~B_COMMAND_KEY;
	else
		return (modifiers & AllowedModifiers()) | B_COMMAND_KEY;
}


/**
 * @brief Normalises a key code to upper-case Unicode for case-insensitive matching.
 *
 * @param key Raw Unicode code point.
 * @return Upper-case equivalent of @a key.
 */
/*static*/
uint32
BWindow::Shortcut::PrepareKey(uint32 key)
{
	return BUnicodeChar::ToUpper(key);
}


//	#pragma mark - BWindow


/**
 * @brief Constructs a BWindow using a combined window_type convenience value.
 *
 * Decomposes @a type into the corresponding window_look and window_feel values,
 * then delegates to _InitData() to create the server-side window and set up the
 * top-level view.
 *
 * @param frame     Initial position and size of the window in screen coordinates.
 * @param title     Title string shown in the window's title tab.
 * @param type      Combined window type (e.g. B_TITLED_WINDOW, B_DOCUMENT_WINDOW).
 * @param flags     Window behaviour flags (e.g. B_NOT_RESIZABLE).
 * @param workspace Workspace bitmask; use B_CURRENT_WORKSPACE for the active one.
 *
 * @see BWindow::BWindow(BRect, const char*, window_look, window_feel, uint32, uint32)
 */
BWindow::BWindow(BRect frame, const char* title, window_type type,
		uint32 flags, uint32 workspace)
	:
	BLooper(title, B_DISPLAY_PRIORITY)
{
	window_look look;
	window_feel feel;
	_DecomposeType(type, &look, &feel);

	_InitData(frame, title, look, feel, flags, workspace);
}


/**
 * @brief Constructs a BWindow with explicit look and feel parameters.
 *
 * This is the most general public constructor.  The window is initially hidden
 * (show-level 1); call Show() to make it visible and start the message loop.
 *
 * @param frame     Initial position and size of the window in screen coordinates.
 * @param title     Title string shown in the window's title tab.
 * @param look      Visual appearance (e.g. B_TITLED_WINDOW_LOOK).
 * @param feel      Behaviour relative to other windows (e.g. B_NORMAL_WINDOW_FEEL).
 * @param flags     Window behaviour flags (e.g. B_NOT_RESIZABLE).
 * @param workspace Workspace bitmask; use B_CURRENT_WORKSPACE for the active one.
 */
BWindow::BWindow(BRect frame, const char* title, window_look look,
		window_feel feel, uint32 flags, uint32 workspace)
	:
	BLooper(title, B_DISPLAY_PRIORITY)
{
	_InitData(frame, title, look, feel, flags, workspace);
}


/**
 * @brief Archive constructor — restores a BWindow from a flattened BMessage.
 *
 * Called by Instantiate().  Reads all window attributes (frame, title, look,
 * feel, flags, workspaces, size limits, pulse rate, and child views) from
 * @a data, then creates the server-side window via _InitData() and
 * reconstructs every archived child view.
 *
 * @param data The archive message previously produced by Archive().
 *
 * @see BWindow::Archive(), BWindow::Instantiate()
 */
BWindow::BWindow(BMessage* data)
	:
	BLooper(data)
{
	data->FindRect("_frame", &fFrame);

	const char* title;
	data->FindString("_title", &title);

	window_look look;
	data->FindInt32("_wlook", (int32*)&look);

	window_feel feel;
	data->FindInt32("_wfeel", (int32*)&feel);

	if (data->FindInt32("_flags", (int32*)&fFlags) != B_OK)
		fFlags = 0;

	uint32 workspaces;
	data->FindInt32("_wspace", (int32*)&workspaces);

	uint32 type;
	if (data->FindInt32("_type", (int32*)&type) == B_OK)
		_DecomposeType((window_type)type, &fLook, &fFeel);

		// connect to app_server and initialize data
	_InitData(fFrame, title, look, feel, fFlags, workspaces);

	if (data->FindFloat("_zoom", 0, &fMaxZoomWidth) == B_OK
		&& data->FindFloat("_zoom", 1, &fMaxZoomHeight) == B_OK)
		SetZoomLimits(fMaxZoomWidth, fMaxZoomHeight);

	if (data->FindFloat("_sizel", 0, &fMinWidth) == B_OK
		&& data->FindFloat("_sizel", 1, &fMinHeight) == B_OK
		&& data->FindFloat("_sizel", 2, &fMaxWidth) == B_OK
		&& data->FindFloat("_sizel", 3, &fMaxHeight) == B_OK)
		SetSizeLimits(fMinWidth, fMaxWidth,
			fMinHeight, fMaxHeight);

	if (data->FindInt64("_pulse", &fPulseRate) == B_OK)
		SetPulseRate(fPulseRate);

	BMessage msg;
	int32 i = 0;
	while (data->FindMessage("_views", i++, &msg) == B_OK) {
		BArchivable* obj = instantiate_object(&msg);
		if (BView* child = dynamic_cast<BView*>(obj))
			AddChild(child);
	}
}


/**
 * @brief Internal constructor for off-screen bitmap windows.
 *
 * Creates an invisible, untyped window backed by the BBitmap identified by
 * @a bitmapToken.  Used internally by BBitmap to obtain a drawing context.
 *
 * @param frame       Dimensions of the off-screen drawing area.
 * @param bitmapToken Server-side token of the backing BBitmap.
 *
 * @note Not intended for application use.
 */
BWindow::BWindow(BRect frame, int32 bitmapToken)
	:
	BLooper("offscreen bitmap")
{
	_DecomposeType(B_UNTYPED_WINDOW, &fLook, &fFeel);
	_InitData(frame, "offscreen", fLook, fFeel, 0, 0, bitmapToken);
}


/**
 * @brief Destroys the BWindow, tearing down the server connection and all resources.
 *
 * Stops any active menu tracking, waits for any pending menu semaphore, removes
 * and deletes the top-level view, frees all registered shortcuts, disables
 * pulsing, notifies the app_server (AS_DELETE_WINDOW), and destroys the IPC
 * link ports.
 *
 * @note The BWindow must be locked before the destructor is called.
 * @see BWindow::Quit()
 */
BWindow::~BWindow()
{
	if (BMenu* menu = dynamic_cast<BMenu*>(fFocus)) {
		MenuPrivate(menu).QuitTracking();
	}

	// The BWindow is locked when the destructor is called,
	// we need to unlock because the menubar thread tries
	// to post a message, which will deadlock otherwise.
	// TODO: I replaced Unlock() with UnlockFully() because the window
	// was kept locked after that in case it was closed using ALT-W.
	// There might be an extra Lock() somewhere in the quitting path...
	UnlockFully();

	// Wait if a menu is still tracking
	if (fMenuSem > 0) {
		while (acquire_sem(fMenuSem) == B_INTERRUPTED)
			;
	}

	Lock();

	fTopView->RemoveSelf();
	delete fTopView;

	// remove all remaining shortcuts
	int32 shortcutCount = fShortcuts.CountItems();
	for (int32 i = 0; i < shortcutCount; i++)
		delete (Shortcut*)fShortcuts.ItemAtFast(i);

	// TODO: release other dynamically-allocated objects
	free(fTitle);

	// disable pulsing
	SetPulseRate(0);

	// tell app_server about our demise
	fLink->StartMessage(AS_DELETE_WINDOW);
	// sync with the server so that for example
	// a BBitmap can be sure that there are no
	// more pending messages that are executed
	// after the bitmap is deleted (which uses
	// a different link and server side thread)
	int32 code;
	fLink->FlushWithReply(code);

	// the sender port belongs to the app_server
	delete_port(fLink->ReceiverPort());
	delete fLink;
}


/**
 * @brief Creates a BWindow from an archived BMessage (BArchivable hook).
 *
 * Validates the archive's class field and, if correct, allocates a new
 * BWindow using the archive constructor.
 *
 * @param data The archive message to restore from.
 * @return A newly allocated BWindow, or NULL if the archive is invalid or
 *         allocation fails.
 *
 * @see BWindow::Archive()
 */
BArchivable*
BWindow::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "BWindow"))
		return NULL;

	return new(std::nothrow) BWindow(data);
}


/**
 * @brief Flattens the BWindow's state into a BMessage for archiving.
 *
 * Stores frame, title, look, feel, flags, workspace, size limits, pulse rate,
 * and (when @a deep is @c true) recursive archives of all direct child views.
 * Non-default size limits and zoom limits are only written when they differ
 * from their default values to keep archives compact.
 *
 * @param data  The message to write archive data into.
 * @param deep  If @c true, recursively archive child views.
 * @return @c B_OK on success, or a negative error code on failure.
 *
 * @see BWindow::Instantiate()
 */
status_t
BWindow::Archive(BMessage* data, bool deep) const
{
	status_t ret = BLooper::Archive(data, deep);

	if (ret == B_OK)
		ret = data->AddRect("_frame", fFrame);
	if (ret == B_OK)
		ret = data->AddString("_title", fTitle);
	if (ret == B_OK)
		ret = data->AddInt32("_wlook", fLook);
	if (ret == B_OK)
		ret = data->AddInt32("_wfeel", fFeel);
	if (ret == B_OK && fFlags != 0)
		ret = data->AddInt32("_flags", fFlags);
	if (ret == B_OK)
		ret = data->AddInt32("_wspace", (uint32)Workspaces());

	if (ret == B_OK && !_ComposeType(fLook, fFeel))
		ret = data->AddInt32("_type", (uint32)Type());

	if (fMaxZoomWidth != 32768.0 || fMaxZoomHeight != 32768.0) {
		if (ret == B_OK)
			ret = data->AddFloat("_zoom", fMaxZoomWidth);
		if (ret == B_OK)
			ret = data->AddFloat("_zoom", fMaxZoomHeight);
	}

	if (fMinWidth != 0.0 || fMinHeight != 0.0
		|| fMaxWidth != 32768.0 || fMaxHeight != 32768.0) {
		if (ret == B_OK)
			ret = data->AddFloat("_sizel", fMinWidth);
		if (ret == B_OK)
			ret = data->AddFloat("_sizel", fMinHeight);
		if (ret == B_OK)
			ret = data->AddFloat("_sizel", fMaxWidth);
		if (ret == B_OK)
			ret = data->AddFloat("_sizel", fMaxHeight);
	}

	if (ret == B_OK && fPulseRate != 500000)
		data->AddInt64("_pulse", fPulseRate);

	if (ret == B_OK && deep) {
		int32 noOfViews = CountChildren();
		for (int32 i = 0; i < noOfViews; i++){
			BMessage childArchive;
			ret = ChildAt(i)->Archive(&childArchive, true);
			if (ret == B_OK)
				ret = data->AddMessage("_views", &childArchive);
			if (ret != B_OK)
				break;
		}
	}

	return ret;
}


/**
 * @brief Hides the window and terminates its message loop.
 *
 * Hides the window (calling Hide() until the show-level reaches zero), posts
 * B_QUIT_REQUESTED to the application if B_QUIT_ON_WINDOW_CLOSE is set, then
 * delegates to BLooper::Quit() which deletes the looper.
 *
 * @note The window must be locked before Quit() is called; an error is printed
 *       if it is not.
 *
 * @see BLooper::Quit(), BWindow::QuitRequested()
 */
void
BWindow::Quit()
{
	if (!IsLocked()) {
		const char* name = Name();
		if (name == NULL)
			name = "no-name";

		printf("ERROR - you must Lock a looper before calling Quit(), "
			   "team=%" B_PRId32 ", looper=%s\n", Team(), name);
	}

	// Try to lock
	if (!Lock()){
		// We're toast already
		return;
	}

	while (!IsHidden())	{
		Hide();
	}

	if (fFlags & B_QUIT_ON_WINDOW_CLOSE)
		be_app->PostMessage(B_QUIT_REQUESTED);

	BLooper::Quit();
}


/**
 * @brief Adds a BView as a direct child of the window's top-level view.
 *
 * The window is auto-locked for the duration of the call.  If @a before is
 * not NULL the new child is inserted immediately before it in sibling order;
 * otherwise it is appended at the end.
 *
 * @param child  The view to add; must not already have a parent.
 * @param before Existing sibling before which @a child is inserted, or NULL
 *               to append.
 *
 * @see BWindow::RemoveChild(), BView::AddChild()
 */
void
BWindow::AddChild(BView* child, BView* before)
{
	BAutolock locker(this);
	if (locker.IsLocked())
		fTopView->AddChild(child, before);
}


/**
 * @brief Adds a BLayoutItem to the window's top-level layout.
 *
 * Forwards the request to the top-level view's layout under an auto-lock.
 *
 * @param child The layout item to add.
 *
 * @see BWindow::SetLayout(), BView::AddChild(BLayoutItem*)
 */
void
BWindow::AddChild(BLayoutItem* child)
{
	BAutolock locker(this);
	if (locker.IsLocked())
		fTopView->AddChild(child);
}


/**
 * @brief Removes a direct child view from the window's top-level view.
 *
 * The window is auto-locked for the duration of the call.
 *
 * @param child The view to remove.
 * @return @c true if the view was found and removed, @c false otherwise.
 *
 * @see BWindow::AddChild(), BView::RemoveChild()
 */
bool
BWindow::RemoveChild(BView* child)
{
	BAutolock locker(this);
	if (!locker.IsLocked())
		return false;

	return fTopView->RemoveChild(child);
}


/**
 * @brief Returns the number of direct child views of the window.
 *
 * @return Number of direct children, or 0 if the window cannot be locked.
 *
 * @see BWindow::ChildAt()
 */
int32
BWindow::CountChildren() const
{
	BAutolock locker(const_cast<BWindow*>(this));
	if (!locker.IsLocked())
		return 0;

	return fTopView->CountChildren();
}


/**
 * @brief Returns the direct child view at the given zero-based index.
 *
 * @param index Zero-based index of the child view.
 * @return The child view at @a index, or NULL if the index is out of range or
 *         the window cannot be locked.
 *
 * @see BWindow::CountChildren()
 */
BView*
BWindow::ChildAt(int32 index) const
{
	BAutolock locker(const_cast<BWindow*>(this));
	if (!locker.IsLocked())
		return NULL;

	return fTopView->ChildAt(index);
}


/**
 * @brief Minimizes or restores the window.
 *
 * Does nothing for modal, floating, or hidden windows, and is a no-op if the
 * window is already in the requested state.  Notifies the app_server via
 * AS_MINIMIZE_WINDOW.
 *
 * @param minimize @c true to minimize the window, @c false to restore it.
 *
 * @see BWindow::IsMinimized()
 */
void
BWindow::Minimize(bool minimize)
{
	if (IsModal() || IsFloating() || IsHidden() || fMinimized == minimize
		|| !Lock())
		return;

	fMinimized = minimize;

	fLink->StartMessage(AS_MINIMIZE_WINDOW);
	fLink->Attach<bool>(minimize);
	fLink->Flush();

	Unlock();
}


/**
 * @brief Stacks this window immediately behind another window.
 *
 * Sends AS_SEND_BEHIND to the app_server.  If @a window is NULL the window is
 * sent behind all other windows.
 *
 * @param window The window to stack behind, or NULL to go to the back.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BWindow::SendBehind(const BWindow* window)
{
	if (!Lock())
		return B_ERROR;

	fLink->StartMessage(AS_SEND_BEHIND);
	fLink->Attach<int32>(window != NULL ? _get_object_token_(window) : -1);
	fLink->Attach<team_id>(Team());

	status_t status = B_ERROR;
	fLink->FlushWithReply(status);

	Unlock();

	return status;
}


/**
 * @brief Flushes all pending drawing commands to the app_server without waiting.
 *
 * Acquires the window lock, flushes the BPortLink send buffer, then
 * releases the lock.  This is an asynchronous operation — the caller does
 * not block until the server has processed the commands.
 *
 * @see BWindow::Sync()
 */
void
BWindow::Flush() const
{
	if (const_cast<BWindow*>(this)->Lock()) {
		fLink->Flush();
		const_cast<BWindow*>(this)->Unlock();
	}
}


/**
 * @brief Synchronously flushes pending drawing commands to the app_server.
 *
 * Sends AS_SYNC and blocks until the server sends a reply, guaranteeing that
 * all prior drawing commands have been fully processed before this call
 * returns.  Useful when ordering is required relative to other server
 * operations (e.g. before deleting a BBitmap).
 *
 * @see BWindow::Flush()
 */
void
BWindow::Sync() const
{
	if (!const_cast<BWindow*>(this)->Lock())
		return;

	fLink->StartMessage(AS_SYNC);

	// waiting for the reply is the actual syncing
	int32 code;
	fLink->FlushWithReply(code);

	const_cast<BWindow*>(this)->Unlock();
}


/**
 * @brief Suspends automatic screen updates for this window.
 *
 * All drawing performed after this call will be queued on the server side but
 * not shown on screen until EnableUpdates() is called.  Useful for reducing
 * flicker during complex redraws.
 *
 * @see BWindow::EnableUpdates()
 */
void
BWindow::DisableUpdates()
{
	if (Lock()) {
		fLink->StartMessage(AS_DISABLE_UPDATES);
		fLink->Flush();
		Unlock();
	}
}


/**
 * @brief Re-enables automatic screen updates after a DisableUpdates() call.
 *
 * Instructs the app_server to flush any queued drawing operations and resume
 * normal update processing for this window.
 *
 * @see BWindow::DisableUpdates()
 */
void
BWindow::EnableUpdates()
{
	if (Lock()) {
		fLink->StartMessage(AS_ENABLE_UPDATES);
		fLink->Flush();
		Unlock();
	}
}


/**
 * @brief Begins a view transaction, batching all link messages to the server.
 *
 * Sets the internal transaction flag so that view operations are accumulated
 * in the link buffer rather than flushed immediately.  Must be balanced with
 * a call to EndViewTransaction().
 *
 * @see BWindow::EndViewTransaction(), BWindow::InViewTransaction()
 */
void
BWindow::BeginViewTransaction()
{
	if (Lock()) {
		fInTransaction = true;
		Unlock();
	}
}


/**
 * @brief Ends a view transaction and flushes all buffered commands to the server.
 *
 * Flushes the link buffer if a transaction is in progress, then clears the
 * transaction flag.
 *
 * @see BWindow::BeginViewTransaction(), BWindow::InViewTransaction()
 */
void
BWindow::EndViewTransaction()
{
	if (Lock()) {
		if (fInTransaction)
			fLink->Flush();
		fInTransaction = false;
		Unlock();
	}
}


/**
 * @brief Returns @c true if a view transaction is currently in progress.
 *
 * @return @c true between BeginViewTransaction() and EndViewTransaction() calls.
 *
 * @see BWindow::BeginViewTransaction(), BWindow::EndViewTransaction()
 */
bool
BWindow::InViewTransaction() const
{
	BAutolock locker(const_cast<BWindow*>(this));
	return fInTransaction;
}


/**
 * @brief Returns @c true if this window is currently the front-most window.
 *
 * Queries the app_server synchronously via AS_IS_FRONT_WINDOW.
 *
 * @return @c true if the window is at the front of the window stack.
 */
bool
BWindow::IsFront() const
{
	BAutolock locker(const_cast<BWindow*>(this));
	if (!locker.IsLocked())
		return false;

	fLink->StartMessage(AS_IS_FRONT_WINDOW);

	status_t status;
	if (fLink->FlushWithReply(status) == B_OK)
		return status >= B_OK;

	return false;
}


/**
 * @brief Handles messages directed at the BWindow itself.
 *
 * Processes non-scripting messages (keyboard navigation, app_server restart
 * reconnection) and dispatches scripting messages (B_GET_PROPERTY /
 * B_SET_PROPERTY) for the "Active", "Feel", "Flags", "Frame", "Hidden",
 * "Look", "Title", "Workspaces", "Minimize", and "TabFrame" properties.
 * All unhandled messages are forwarded to BLooper::MessageReceived().
 *
 * @param message The BMessage to handle.
 *
 * @see BWindow::DispatchMessage(), BLooper::MessageReceived()
 * @see BWindow::ResolveSpecifier(), BWindow::GetSupportedSuites()
 */
void
BWindow::MessageReceived(BMessage* message)
{
	if (!message->HasSpecifiers()) {
		if (message->what == B_KEY_DOWN)
			_KeyboardNavigation();

		if (message->what == kMsgAppServerStarted) {
			fLink->SetSenderPort(
				BApplication::Private::ServerLink()->SenderPort());

			BPrivate::AppServerLink lockLink;
				// we're talking to the server application using our own
				// communication channel (fLink) - we better make sure no one
				// interferes by locking that channel (which AppServerLink does
				// implicitly)

			fLink->StartMessage(AS_CREATE_WINDOW);

			fLink->Attach<BRect>(fFrame);
			fLink->Attach<uint32>((uint32)fLook);
			fLink->Attach<uint32>((uint32)fFeel);
			fLink->Attach<uint32>(fFlags);
			fLink->Attach<uint32>(0);
			fLink->Attach<int32>(_get_object_token_(this));
			fLink->Attach<port_id>(fLink->ReceiverPort());
			fLink->Attach<port_id>(fMsgPort);
			fLink->AttachString(fTitle);

			port_id sendPort;
			int32 code;
			if (fLink->FlushWithReply(code) == B_OK
				&& code == B_OK
				&& fLink->Read<port_id>(&sendPort) == B_OK) {
				// read the frame size and its limits that were really
				// enforced on the server side

				fLink->Read<BRect>(&fFrame);
				fLink->Read<float>(&fMinWidth);
				fLink->Read<float>(&fMaxWidth);
				fLink->Read<float>(&fMinHeight);
				fLink->Read<float>(&fMaxHeight);

				fMaxZoomWidth = fMaxWidth;
				fMaxZoomHeight = fMaxHeight;
			} else
				sendPort = -1;

			// Redirect our link to the new window connection
			fLink->SetSenderPort(sendPort);

			// connect all views to the server again
			fTopView->_CreateSelf();

			EnableUpdates();
			_SendShowOrHideMessage();
		}

		return BLooper::MessageReceived(message);
	}

	BMessage replyMsg(B_REPLY);
	bool handled = false;

	BMessage specifier;
	int32 what;
	const char* prop;
	int32 index;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &prop) != B_OK)
		return BLooper::MessageReceived(message);

	BPropertyInfo propertyInfo(sWindowPropInfo);
	switch (propertyInfo.FindMatch(message, index, &specifier, what, prop)) {
		case 0:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddBool("result", IsActive());
				handled = true;
			} else if (message->what == B_SET_PROPERTY) {
				bool newActive;
				if (message->FindBool("data", &newActive) == B_OK) {
					Activate(newActive);
					handled = true;
				}
			}
			break;
		case 1:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddInt32("result", (uint32)Feel());
				handled = true;
			} else {
				uint32 newFeel;
				if (message->FindInt32("data", (int32*)&newFeel) == B_OK) {
					SetFeel((window_feel)newFeel);
					handled = true;
				}
			}
			break;
		case 2:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddInt32("result", Flags());
				handled = true;
			} else {
				uint32 newFlags;
				if (message->FindInt32("data", (int32*)&newFlags) == B_OK) {
					SetFlags(newFlags);
					handled = true;
				}
			}
			break;
		case 3:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddRect("result", Frame());
				handled = true;
			} else {
				BRect newFrame;
				if (message->FindRect("data", &newFrame) == B_OK) {
					MoveTo(newFrame.LeftTop());
					ResizeTo(newFrame.Width(), newFrame.Height());
					handled = true;
				}
			}
			break;
		case 4:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddBool("result", IsHidden());
				handled = true;
			} else {
				bool hide;
				if (message->FindBool("data", &hide) == B_OK) {
					if (hide) {
						if (!IsHidden())
							Hide();
					} else if (IsHidden())
						Show();
					handled = true;
				}
			}
			break;
		case 5:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddInt32("result", (uint32)Look());
				handled = true;
			} else {
				uint32 newLook;
				if (message->FindInt32("data", (int32*)&newLook) == B_OK) {
					SetLook((window_look)newLook);
					handled = true;
				}
			}
			break;
		case 6:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddString("result", Title());
				handled = true;
			} else {
				const char* newTitle = NULL;
				if (message->FindString("data", &newTitle) == B_OK) {
					SetTitle(newTitle);
					handled = true;
				}
			}
			break;
		case 7:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddInt32( "result", Workspaces());
				handled = true;
			} else {
				uint32 newWorkspaces;
				if (message->FindInt32("data", (int32*)&newWorkspaces) == B_OK) {
					SetWorkspaces(newWorkspaces);
					handled = true;
				}
			}
			break;
		case 11:
			if (message->what == B_GET_PROPERTY) {
				replyMsg.AddBool("result", IsMinimized());
				handled = true;
			} else {
				bool minimize;
				if (message->FindBool("data", &minimize) == B_OK) {
					Minimize(minimize);
					handled = true;
				}
			}
			break;
		case 12:
			if (message->what == B_GET_PROPERTY) {
				BMessage settings;
				if (GetDecoratorSettings(&settings) == B_OK) {
					BRect frame;
					if (settings.FindRect("tab frame", &frame) == B_OK) {
						replyMsg.AddRect("result", frame);
						handled = true;
					}
				}
			}
			break;
		default:
			return BLooper::MessageReceived(message);
	}

	if (handled) {
		if (message->what == B_SET_PROPERTY)
			replyMsg.AddInt32("error", B_OK);
	} else {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;
		replyMsg.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
		replyMsg.AddString("message", "Didn't understand the specifier(s)");
	}
	message->SendReply(&replyMsg);
}


/**
 * @brief Central message dispatcher for the BWindow message loop.
 *
 * Intercepts system messages before they reach their target handler:
 * - B_ZOOM / _ZOOM_ — triggers the zoom operation.
 * - _MINIMIZE_ — minimizes the window via shortcut.
 * - _SEND_BEHIND_ / _SEND_TO_FRONT_ — reorder the window.
 * - B_MINIMIZE / B_HIDE_APPLICATION — minimize or hide the application team.
 * - B_WINDOW_RESIZED / B_WINDOW_MOVED — coalesces pending resize/move
 *   notifications and calls FrameResized() / FrameMoved().
 * - B_WINDOW_ACTIVATED — coalesces activation messages and calls WindowActivated().
 * - B_SCREEN_CHANGED / B_WORKSPACE_ACTIVATED / B_WORKSPACES_CHANGED —
 *   propagates to child views and calls the corresponding hook.
 * - B_KEY_DOWN / B_UNMAPPED_KEY_DOWN — tries shortcut and navigation handling.
 * - B_PULSE — delivers pulse events to all views.
 * - _UPDATE_ — performs the incremental repaint protocol with the app_server.
 * - _MENUS_DONE_ — calls MenusEnded().
 * - B_WINDOW_MOVE_BY / B_WINDOW_MOVE_TO — legacy scripting move commands.
 * - B_LAYOUT_WINDOW — triggers a layout pass.
 * - B_COLORS_UPDATED / B_FONTS_UPDATED — propagates theme changes.
 *
 * All other messages are forwarded to BLooper::DispatchMessage().
 *
 * @param message The message to dispatch.
 * @param target  The intended target handler for this message.
 *
 * @see BWindow::MessageReceived(), BLooper::DispatchMessage()
 */
void
BWindow::DispatchMessage(BMessage* message, BHandler* target)
{
	if (message == NULL)
		return;

	switch (message->what) {
		case B_ZOOM:
			Zoom();
			break;

		case _MINIMIZE_:
			// Used by the minimize shortcut
			if ((Flags() & B_NOT_MINIMIZABLE) == 0)
				Minimize(true);
			break;

		case _ZOOM_:
			// Used by the zoom shortcut
			if ((Flags() & B_NOT_ZOOMABLE) == 0)
				Zoom();
			break;

		case _SEND_BEHIND_:
			SendBehind(NULL);
			break;

		case _SEND_TO_FRONT_:
			Activate();
			break;

		case B_MINIMIZE:
		{
			bool minimize;
			if (message->FindBool("minimize", &minimize) == B_OK)
				Minimize(minimize);
			break;
		}

		case B_HIDE_APPLICATION:
		{
			// Hide all applications with the same signature
			// (ie. those that are part of the same group to be consistent
			// to what the Deskbar shows you).
			app_info info;
			be_app->GetAppInfo(&info);

			BList list;
			be_roster->GetAppList(info.signature, &list);

			for (int32 i = 0; i < list.CountItems(); i++) {
				do_minimize_team(BRect(), (team_id)(addr_t)list.ItemAt(i),
					false);
			}
			break;
		}

		case B_WINDOW_RESIZED:
		{
			int32 width, height;
			if (message->FindInt32("width", &width) == B_OK
				&& message->FindInt32("height", &height) == B_OK) {
				// combine with pending resize notifications
				BMessage* pendingMessage;
				while ((pendingMessage
						= MessageQueue()->FindMessage(B_WINDOW_RESIZED, 0))) {
					int32 nextWidth;
					if (pendingMessage->FindInt32("width", &nextWidth) == B_OK)
						width = nextWidth;

					int32 nextHeight;
					if (pendingMessage->FindInt32("height", &nextHeight)
							== B_OK) {
						height = nextHeight;
					}

					MessageQueue()->RemoveMessage(pendingMessage);
					delete pendingMessage;
						// this deletes the first *additional* message
						// fCurrentMessage is safe
				}
				if (width != fFrame.Width() || height != fFrame.Height()) {
					// NOTE: we might have already handled the resize
					// in an _UPDATE_ message
					fFrame.right = fFrame.left + width;
					fFrame.bottom = fFrame.top + height;

					_AdoptResize();
//					FrameResized(width, height);
				}
// call hook function anyways
// TODO: When a window is resized programmatically,
// it receives this message, and maybe it is wise to
// keep the asynchronous nature of this process to
// not risk breaking any apps.
FrameResized(width, height);
			}
			break;
		}

		case B_WINDOW_MOVED:
		{
			BPoint origin;
			if (message->FindPoint("where", &origin) == B_OK) {
				if (fFrame.LeftTop() != origin) {
					// NOTE: we might have already handled the move
					// in an _UPDATE_ message
					fFrame.OffsetTo(origin);

//					FrameMoved(origin);
				}
// call hook function anyways
// TODO: When a window is moved programmatically,
// it receives this message, and maybe it is wise to
// keep the asynchronous nature of this process to
// not risk breaking any apps.
FrameMoved(origin);
			}
			break;
		}

		case B_WINDOW_ACTIVATED:
			if (target != this) {
				target->MessageReceived(message);
				break;
			}

			bool active;
			if (message->FindBool("active", &active) != B_OK)
				break;

			// find latest activation message

			while (true) {
				BMessage* pendingMessage = MessageQueue()->FindMessage(
					B_WINDOW_ACTIVATED, 0);
				if (pendingMessage == NULL)
					break;

				bool nextActive;
				if (pendingMessage->FindBool("active", &nextActive) == B_OK)
					active = nextActive;

				MessageQueue()->RemoveMessage(pendingMessage);
				delete pendingMessage;
			}

			if (active != fActive) {
				fActive = active;

				WindowActivated(active);

				// call hook function 'WindowActivated(bool)' for all
				// views attached to this window.
				fTopView->_Activate(active);

				// we notify the input server if we are gaining or losing focus
				// from a view which has the B_INPUT_METHOD_AWARE on a window
				// activation
				if (!active)
					break;
				bool inputMethodAware = false;
				if (fFocus)
					inputMethodAware = fFocus->Flags() & B_INPUT_METHOD_AWARE;
				BMessage message(inputMethodAware ? IS_FOCUS_IM_AWARE_VIEW : IS_UNFOCUS_IM_AWARE_VIEW);
				BMessenger messenger(fFocus);
				BMessage reply;
				if (fFocus)
					message.AddMessenger("view", messenger);
				_control_input_server_(&message, &reply);
			}
			break;

		case B_SCREEN_CHANGED:
			if (target == this) {
				BRect frame;
				uint32 mode;
				if (message->FindRect("frame", &frame) == B_OK
					&& message->FindInt32("mode", (int32*)&mode) == B_OK) {
					_PropagateMessageToChildViews(message);
					// call hook method
					ScreenChanged(frame, (color_space)mode);
				}
			} else
				target->MessageReceived(message);
			break;

		case B_WORKSPACE_ACTIVATED:
			if (target == this) {
				uint32 workspace;
				bool active;
				if (message->FindInt32("workspace", (int32*)&workspace) == B_OK
					&& message->FindBool("active", &active) == B_OK) {
					_PropagateMessageToChildViews(message);
					// call hook method
					WorkspaceActivated(workspace, active);
				}
			} else
				target->MessageReceived(message);
			break;

		case B_WORKSPACES_CHANGED:
			if (target == this) {
				uint32 oldWorkspace;
				uint32 newWorkspace;
				if (message->FindInt32("old", (int32*)&oldWorkspace) == B_OK
					&& message->FindInt32("new", (int32*)&newWorkspace) == B_OK) {
					_PropagateMessageToChildViews(message);
					// call hook method
					WorkspacesChanged(oldWorkspace, newWorkspace);
				}
			} else
				target->MessageReceived(message);
			break;

		case B_KEY_DOWN:
			if (!_HandleKeyDown(message))
				target->MessageReceived(message);
			break;

		case B_UNMAPPED_KEY_DOWN:
			if (!_HandleUnmappedKeyDown(message))
				target->MessageReceived(message);
			break;

		case B_PULSE:
			if (target == this && fPulseRunner) {
				fTopView->_Pulse();
				fLink->Flush();
			} else
				target->MessageReceived(message);
			break;

		case _UPDATE_:
		{
//bigtime_t now = system_time();
//bigtime_t drawTime = 0;
			STRACE(("info:BWindow handling _UPDATE_.\n"));

			fLink->StartMessage(AS_BEGIN_UPDATE);
			fInTransaction = true;

			int32 code;
			if (fLink->FlushWithReply(code) == B_OK
				&& code == B_OK) {
				// read current window position and size first,
				// the update rect is in screen coordinates...
				// so we need to be up to date
				BPoint origin;
				fLink->Read<BPoint>(&origin);
				float width;
				float height;
				fLink->Read<float>(&width);
				fLink->Read<float>(&height);

				// read tokens for views that need to be drawn
				// NOTE: we need to read the tokens completely
				// first, we cannot draw views in between reading
				// the tokens, since other communication would likely
				// mess up the data in the link.
				struct ViewUpdateInfo {
					int32 token;
					BRect updateRect;
				};
				BList infos(20);
				while (true) {
					// read next token and create/add ViewUpdateInfo
					int32 token;
					status_t error = fLink->Read<int32>(&token);
					if (error < B_OK || token == B_NULL_TOKEN)
						break;
					ViewUpdateInfo* info = new(std::nothrow) ViewUpdateInfo;
					if (info == NULL || !infos.AddItem(info)) {
						delete info;
						break;
					}
					info->token = token;
					// read culmulated update rect (is in screen coords)
					error = fLink->Read<BRect>(&(info->updateRect));
					if (error < B_OK)
						break;
				}
				// Hooks should be called after finishing reading reply because
				// they can access fLink.
				if (origin != fFrame.LeftTop()) {
					// TODO: remove code duplicatation with
					// B_WINDOW_MOVED case...
					//printf("window position was not up to date\n");
					fFrame.OffsetTo(origin);
					FrameMoved(origin);
				}
				if (width != fFrame.Width() || height != fFrame.Height()) {
					// TODO: remove code duplicatation with
					// B_WINDOW_RESIZED case...
					//printf("window size was not up to date\n");
					fFrame.right = fFrame.left + width;
					fFrame.bottom = fFrame.top + height;

					_AdoptResize();
					FrameResized(width, height);
				}

				// draw
				int32 count = infos.CountItems();
				for (int32 i = 0; i < count; i++) {
//bigtime_t drawStart = system_time();
					ViewUpdateInfo* info
						= (ViewUpdateInfo*)infos.ItemAtFast(i);
					if (BView* view = _FindView(info->token))
						view->_Draw(info->updateRect);
					else {
						printf("_UPDATE_ - didn't find view by token: %"
							B_PRId32 "\n", info->token);
					}
//drawTime += system_time() - drawStart;
				}
				// NOTE: The tokens are actually hirachically sorted,
				// so traversing the list in revers and calling
				// child->_DrawAfterChildren() actually works like intended.
				for (int32 i = count - 1; i >= 0; i--) {
					ViewUpdateInfo* info
						= (ViewUpdateInfo*)infos.ItemAtFast(i);
					if (BView* view = _FindView(info->token))
						view->_DrawAfterChildren(info->updateRect);
					delete info;
				}

//printf("  %ld views drawn, total Draw() time: %lld\n", count, drawTime);
			}

			fLink->StartMessage(AS_END_UPDATE);
			fLink->Flush();
			fInTransaction = false;
			fUpdateRequested = false;

//printf("BWindow(%s) - UPDATE took %lld usecs\n", Title(), system_time() - now);
			break;
		}

		case _MENUS_DONE_:
			MenusEnded();
			break;

		// These two are obviously some kind of old scripting messages
		// this is NOT an app_server message and we have to be cautious
		case B_WINDOW_MOVE_BY:
		{
			BPoint offset;
			if (message->FindPoint("data", &offset) == B_OK)
				MoveBy(offset.x, offset.y);
			else
				message->SendReply(B_MESSAGE_NOT_UNDERSTOOD);
			break;
		}

		// this is NOT an app_server message and we have to be cautious
		case B_WINDOW_MOVE_TO:
		{
			BPoint origin;
			if (message->FindPoint("data", &origin) == B_OK)
				MoveTo(origin);
			else
				message->SendReply(B_MESSAGE_NOT_UNDERSTOOD);
			break;
		}

		case B_LAYOUT_WINDOW:
		{
			Layout(false);
			break;
		}

		case B_COLORS_UPDATED:
		{
			fTopView->_ColorsUpdated(message);
			target->MessageReceived(message);
			break;
		}

		case B_FONTS_UPDATED:
		{
			fTopView->_FontsUpdated(message);
			target->MessageReceived(message);
			break;
		}

		default:
			BLooper::DispatchMessage(message, target);
			break;
	}
}


/**
 * @brief Hook called when the window's position changes.
 *
 * The default implementation does nothing.  Override to respond to window
 * moves, e.g. to update cached position data or adjust child windows.
 *
 * @param newPosition The new top-left corner of the window in screen coordinates.
 */
void
BWindow::FrameMoved(BPoint newPosition)
{
	// does nothing
	// Hook function
}


/**
 * @brief Hook called when the window's size changes.
 *
 * The default implementation does nothing.  Override to respond to resize
 * events, e.g. to rearrange manually-laid-out child views.
 *
 * @param newWidth  New content width of the window in pixels.
 * @param newHeight New content height of the window in pixels.
 */
void
BWindow::FrameResized(float newWidth, float newHeight)
{
	// does nothing
	// Hook function
}


/**
 * @brief Hook called when the window's workspace membership changes.
 *
 * The default implementation does nothing.  Override to respond to the window
 * being moved between workspaces.
 *
 * @param oldWorkspaces Bitmask of workspaces the window belonged to before.
 * @param newWorkspaces Bitmask of workspaces the window now belongs to.
 */
void
BWindow::WorkspacesChanged(uint32 oldWorkspaces, uint32 newWorkspaces)
{
	// does nothing
	// Hook function
}


/**
 * @brief Hook called when the active workspace changes.
 *
 * The default implementation does nothing.  Override to respond to workspace
 * switches, e.g. to pause or resume activity when the window becomes invisible.
 *
 * @param workspace Index of the workspace whose activation state changed.
 * @param state     @c true if @a workspace became active, @c false if it
 *                  became inactive.
 */
void
BWindow::WorkspaceActivated(int32 workspace, bool state)
{
	// does nothing
	// Hook function
}


/**
 * @brief Hook called just before a menu begins tracking (i.e. is about to open).
 *
 * The default implementation does nothing.  Override to enable or update menu
 * items before they are displayed.  This hook is also invoked when a keyboard
 * shortcut is evaluated, giving subclasses a chance to install dynamic shortcuts.
 *
 * @see BWindow::MenusEnded()
 */
void
BWindow::MenusBeginning()
{
	// does nothing
	// Hook function
}


/**
 * @brief Hook called after a menu tracking session ends.
 *
 * The default implementation does nothing.  Override to perform cleanup or
 * update state after a menu is dismissed.
 *
 * @see BWindow::MenusBeginning()
 */
void
BWindow::MenusEnded()
{
	// does nothing
	// Hook function
}


/**
 * @brief Sets the minimum and maximum size constraints for the window.
 *
 * Sends the limits to the app_server (AS_SET_SIZE_LIMITS), which may adjust
 * the window's current size to satisfy the new constraints.  The actual limits
 * enforced by the server are read back and stored in the member variables.
 * Does nothing if minWidth > maxWidth or minHeight > maxHeight.
 *
 * @param minWidth  Minimum allowed content width in pixels.
 * @param maxWidth  Maximum allowed content width in pixels.
 * @param minHeight Minimum allowed content height in pixels.
 * @param maxHeight Maximum allowed content height in pixels.
 *
 * @see BWindow::GetSizeLimits(), BWindow::UpdateSizeLimits()
 */
void
BWindow::SetSizeLimits(float minWidth, float maxWidth,
	float minHeight, float maxHeight)
{
	if (minWidth > maxWidth || minHeight > maxHeight)
		return;

	if (!Lock())
		return;

	fLink->StartMessage(AS_SET_SIZE_LIMITS);
	fLink->Attach<float>(minWidth);
	fLink->Attach<float>(maxWidth);
	fLink->Attach<float>(minHeight);
	fLink->Attach<float>(maxHeight);

	int32 code;
	if (fLink->FlushWithReply(code) == B_OK
		&& code == B_OK) {
		// read the values that were really enforced on
		// the server side (the window frame could have
		// been changed, too)
		fLink->Read<BRect>(&fFrame);
		fLink->Read<float>(&fMinWidth);
		fLink->Read<float>(&fMaxWidth);
		fLink->Read<float>(&fMinHeight);
		fLink->Read<float>(&fMaxHeight);

		_AdoptResize();
			// TODO: the same has to be done for SetLook() (that can alter
			//		the size limits, and hence, the size of the window
	}
	Unlock();
}


/**
 * @brief Retrieves the current minimum and maximum size constraints.
 *
 * Any of the output pointers may be NULL; those values are simply skipped.
 *
 * @param _minWidth  Output: minimum content width, or NULL.
 * @param _maxWidth  Output: maximum content width, or NULL.
 * @param _minHeight Output: minimum content height, or NULL.
 * @param _maxHeight Output: maximum content height, or NULL.
 *
 * @see BWindow::SetSizeLimits()
 */
void
BWindow::GetSizeLimits(float* _minWidth, float* _maxWidth, float* _minHeight,
	float* _maxHeight)
{
	// TODO: What about locking?!?
	if (_minHeight != NULL)
		*_minHeight = fMinHeight;
	if (_minWidth != NULL)
		*_minWidth = fMinWidth;
	if (_maxHeight != NULL)
		*_maxHeight = fMaxHeight;
	if (_maxWidth != NULL)
		*_maxWidth = fMaxWidth;
}


/**
 * @brief Synchronises the window's size limits with its top-level layout's min/max sizes.
 *
 * Only has an effect when the B_AUTO_UPDATE_SIZE_LIMITS flag is set.  Queries
 * the top view's MinSize() and MaxSize() and forwards the results to
 * SetSizeLimits().
 *
 * @see BWindow::SetSizeLimits(), BWindow::SetFlags()
 */
void
BWindow::UpdateSizeLimits()
{
	BAutolock locker(this);

	if ((fFlags & B_AUTO_UPDATE_SIZE_LIMITS) != 0) {
		// Get min/max constraints of the top view and enforce window
		// size limits respectively.
		BSize minSize = fTopView->MinSize();
		BSize maxSize = fTopView->MaxSize();
		SetSizeLimits(minSize.width, maxSize.width,
			minSize.height, maxSize.height);
	}
}


/**
 * @brief Applies custom decorator settings to this window's decorator.
 *
 * Flattens the BMessage into a raw buffer and sends it to the app_server via
 * AS_SET_DECORATOR_SETTINGS.  The exact keys accepted depend on the active
 * decorator plug-in.
 *
 * @param settings A BMessage containing decorator-specific key/value pairs.
 * @return @c B_OK on success, or an error code if flattening or sending fails.
 *
 * @see BWindow::GetDecoratorSettings()
 */
status_t
BWindow::SetDecoratorSettings(const BMessage& settings)
{
	// flatten the given settings into a buffer and send
	// it to the app_server to apply the settings to the
	// decorator

	int32 size = settings.FlattenedSize();
	char buffer[size];
	status_t status = settings.Flatten(buffer, size);
	if (status != B_OK)
		return status;

	if (!Lock())
		return B_ERROR;

	status = fLink->StartMessage(AS_SET_DECORATOR_SETTINGS);

	if (status == B_OK)
		status = fLink->Attach<int32>(size);

	if (status == B_OK)
		status = fLink->Attach(buffer, size);

	if (status == B_OK)
		status = fLink->Flush();

	Unlock();

	return status;
}


/**
 * @brief Retrieves the current decorator settings for this window.
 *
 * Sends AS_GET_DECORATOR_SETTINGS to the app_server, reads the flattened
 * BMessage reply, and unflattens it into @a settings.  Useful for reading
 * the "tab frame" and "border width" values used by DecoratorFrame().
 *
 * @param settings Output BMessage that receives the decorator settings.
 * @return @c B_OK on success, or an error code if the query fails.
 *
 * @see BWindow::SetDecoratorSettings(), BWindow::DecoratorFrame()
 */
status_t
BWindow::GetDecoratorSettings(BMessage* settings) const
{
	// read a flattened settings message from the app_server
	// and put it into settings

	if (!const_cast<BWindow*>(this)->Lock())
		return B_ERROR;

	status_t status = fLink->StartMessage(AS_GET_DECORATOR_SETTINGS);

	if (status == B_OK) {
		int32 code;
		status = fLink->FlushWithReply(code);
		if (status == B_OK && code != B_OK)
			status = code;
	}

	if (status == B_OK) {
		int32 size;
		status = fLink->Read<int32>(&size);
		if (status == B_OK) {
			char buffer[size];
			status = fLink->Read(buffer, size);
			if (status == B_OK) {
				status = settings->Unflatten(buffer);
			}
		}
	}

	const_cast<BWindow*>(this)->Unlock();

	return status;
}


/**
 * @brief Sets the maximum dimensions the window may occupy when zoomed.
 *
 * The effective zoom limits are clamped to the size limits set via
 * SetSizeLimits() and further clipped to the screen size when Zoom() is
 * invoked.  Values larger than the current maximum size limit are silently
 * clamped.
 *
 * @param maxWidth  Maximum content width in pixels when zoomed.
 * @param maxHeight Maximum content height in pixels when zoomed.
 *
 * @see BWindow::Zoom(), BWindow::SetSizeLimits()
 */
void
BWindow::SetZoomLimits(float maxWidth, float maxHeight)
{
	// TODO: What about locking?!?
	if (maxWidth > fMaxWidth)
		maxWidth = fMaxWidth;
	fMaxZoomWidth = maxWidth;

	if (maxHeight > fMaxHeight)
		maxHeight = fMaxHeight;
	fMaxZoomHeight = maxHeight;
}


/**
 * @brief Hook called by the non-virtual Zoom() with the computed ideal frame.
 *
 * The default implementation simply moves and resizes the window to the
 * supplied @a origin, @a width, and @a height.  Override to customise zoom
 * behaviour (e.g. to preserve aspect ratio or animate the transition).
 *
 * @param origin Target top-left corner of the zoomed window in screen coordinates.
 * @param width  Target content width in pixels.
 * @param height Target content height in pixels.
 *
 * @see BWindow::Zoom(), BWindow::SetZoomLimits()
 */
void
BWindow::Zoom(BPoint origin, float width, float height)
{
	// the default implementation of this hook function
	// just does the obvious:
	MoveTo(origin);
	ResizeTo(width, height);
}


/**
 * @brief Zooms (or un-zooms) the window to the largest useful size on screen.
 *
 * Computes the ideal zoom rectangle as the smallest of: the zoom limits set
 * by SetZoomLimits(), the size limits set by SetSizeLimits(), and the usable
 * screen area (adjusted for the Deskbar unless it is auto-hidden or
 * B_SHIFT_KEY is held).  If the window is already at the computed size,
 * restores it to the previous frame.  Delegates the actual move/resize to the
 * Zoom(BPoint, float, float) hook.
 *
 * @see BWindow::Zoom(BPoint, float, float), BWindow::SetZoomLimits()
 */
void
BWindow::Zoom()
{
	// TODO: What about locking?!?

	// From BeBook:
	// The dimensions that non-virtual Zoom() passes to hook Zoom() are deduced
	// from the smallest of three rectangles:

	// 1) the rectangle defined by SetZoomLimits() and,
	// 2) the rectangle defined by SetSizeLimits()
	float maxZoomWidth = std::min(fMaxZoomWidth, fMaxWidth);
	float maxZoomHeight = std::min(fMaxZoomHeight, fMaxHeight);

	// 3) the screen rectangle
	BRect screenFrame = (BScreen(this)).Frame();
	maxZoomWidth = std::min(maxZoomWidth, screenFrame.Width());
	maxZoomHeight = std::min(maxZoomHeight, screenFrame.Height());

	BRect zoomArea = screenFrame; // starts at screen size

	BDeskbar deskbar;
	BRect deskbarFrame = deskbar.Frame();
	bool isShiftDown = (modifiers() & B_SHIFT_KEY) != 0;
	if (!isShiftDown && !deskbar.IsAutoHide()) {
		// remove area taken up by Deskbar unless hidden or shift is held down
		switch (deskbar.Location()) {
			case B_DESKBAR_TOP:
				zoomArea.top = deskbarFrame.bottom + 2;
				break;

			case B_DESKBAR_BOTTOM:
			case B_DESKBAR_LEFT_BOTTOM:
			case B_DESKBAR_RIGHT_BOTTOM:
				zoomArea.bottom = deskbarFrame.top - 2;
				break;

			// in vertical expando mode only if not always-on-top or auto-raise
			case B_DESKBAR_LEFT_TOP:
				if (!deskbar.IsExpanded())
					zoomArea.top = deskbarFrame.bottom + 2;
				else if (!deskbar.IsAlwaysOnTop() && !deskbar.IsAutoRaise())
					zoomArea.left = deskbarFrame.right + 2;
				break;

			default:
			case B_DESKBAR_RIGHT_TOP:
				if (!deskbar.IsExpanded())
					break;
				else if (!deskbar.IsAlwaysOnTop() && !deskbar.IsAutoRaise())
					zoomArea.right = deskbarFrame.left - 2;
				break;
		}
	}

	// TODO: Broken for tab on left side windows...
	float borderWidth;
	float tabHeight;
	_GetDecoratorSize(&borderWidth, &tabHeight);

	// remove the area taken up by the tab and border
	zoomArea.left += borderWidth;
	zoomArea.top += borderWidth + tabHeight;
	zoomArea.right -= borderWidth;
	zoomArea.bottom -= borderWidth;

	// inset towards center vertically first to see if there will be room
	// above or below Deskbar
	if (zoomArea.Height() > maxZoomHeight)
		zoomArea.InsetBy(0, roundf((zoomArea.Height() - maxZoomHeight) / 2));

	if (zoomArea.top > deskbarFrame.bottom
		|| zoomArea.bottom < deskbarFrame.top) {
		// there is room above or below Deskbar, start from screen width
		// minus borders instead of desktop width minus borders
		zoomArea.left = screenFrame.left + borderWidth;
		zoomArea.right = screenFrame.right - borderWidth;
	}

	// inset towards center
	if (zoomArea.Width() > maxZoomWidth)
		zoomArea.InsetBy(roundf((zoomArea.Width() - maxZoomWidth) / 2), 0);

	// Un-Zoom

	if (fPreviousFrame.IsValid()
		// NOTE: don't check for fFrame.LeftTop() == zoomArea.LeftTop()
		// -> makes it easier on the user to get a window back into place
		&& fFrame.Width() == zoomArea.Width()
		&& fFrame.Height() == zoomArea.Height()) {
		// already zoomed!
		Zoom(fPreviousFrame.LeftTop(), fPreviousFrame.Width(),
			fPreviousFrame.Height());
		return;
	}

	// Zoom

	// remember fFrame for later "unzooming"
	fPreviousFrame = fFrame;

	Zoom(zoomArea.LeftTop(), zoomArea.Width(), zoomArea.Height());
}


/**
 * @brief Hook called when the screen's resolution or colour depth changes.
 *
 * The default implementation does nothing.  Override to respond to screen
 * configuration changes, e.g. to reposition the window or reload
 * resolution-dependent resources.
 *
 * @param screenSize The new screen frame in screen coordinates.
 * @param depth      The new colour space of the screen.
 */
void
BWindow::ScreenChanged(BRect screenSize, color_space depth)
{
	// Hook function
}


/**
 * @brief Sets the interval at which B_PULSE messages are sent to child views.
 *
 * A rate of 0 disables pulsing.  If the rate changes, any existing
 * BMessageRunner is reconfigured or destroyed and a new one is created as
 * needed.  Negative values are ignored.
 *
 * @param rate Pulse interval in microseconds, or 0 to disable pulsing.
 *
 * @see BWindow::PulseRate(), BView::Pulse()
 */
void
BWindow::SetPulseRate(bigtime_t rate)
{
	// TODO: What about locking?!?
	if (rate < 0
		|| (rate == fPulseRate && !((rate == 0) ^ (fPulseRunner == NULL))))
		return;

	fPulseRate = rate;

	if (rate > 0) {
		if (fPulseRunner == NULL) {
			BMessage message(B_PULSE);
			fPulseRunner = new(std::nothrow) BMessageRunner(BMessenger(this),
				&message, rate);
		} else {
			fPulseRunner->SetInterval(rate);
		}
	} else {
		// rate == 0
		delete fPulseRunner;
		fPulseRunner = NULL;
	}
}


/**
 * @brief Returns the current pulse interval in microseconds.
 *
 * A return value of 0 means pulsing is disabled.
 *
 * @return The pulse rate in microseconds.
 *
 * @see BWindow::SetPulseRate()
 */
bigtime_t
BWindow::PulseRate() const
{
	return fPulseRate;
}


/**
 * @brief Internal helper used by BMenuItem to register its shortcut with the window.
 *
 * Prepares the key and modifiers through the Shortcut helper, removes any
 * existing shortcut with the same prepared key/modifiers, then adds the new
 * Shortcut to the internal list.  The prepared (normalised) key and modifier
 * values are written back through the output pointers so BMenuItem can cache
 * them.
 *
 * @param _key       In/out: raw key code on entry, prepared key code on return.
 * @param _modifiers In/out: raw modifier mask on entry, prepared mask on return.
 * @param item       The BMenuItem that owns this shortcut.
 */
void
BWindow::_AddShortcut(uint32* _key, uint32* _modifiers, BMenuItem* item)
{
	Shortcut* shortcut = new(std::nothrow) Shortcut(*_key, *_modifiers, item);
	if (shortcut == NULL)
		return;

	// removes the shortcut if it already exists!
	RemoveShortcut(shortcut->Key(), shortcut->Modifiers());

	// pass the prepared key and modifiers back to caller
	*_key = shortcut->Key();
	*_modifiers = shortcut->Modifiers();

	fShortcuts.AddItem(shortcut);
}


/**
 * @brief Registers a keyboard shortcut that posts a message to this window.
 *
 * Convenience overload that uses the BWindow itself as the target handler.
 *
 * @param key       Unicode code point for the shortcut key.
 * @param modifiers Modifier-key bitmask (e.g. B_COMMAND_KEY).
 * @param message   The message to post; ownership is transferred.
 *
 * @see BWindow::AddShortcut(uint32, uint32, BMessage*, BHandler*)
 * @see BWindow::RemoveShortcut(), BWindow::HasShortcut()
 */
void
BWindow::AddShortcut(uint32 key, uint32 modifiers, BMessage* message)
{
	AddShortcut(key, modifiers, message, this);
}


/**
 * @brief Registers a keyboard shortcut that posts a message to a specific handler.
 *
 * If a shortcut with the same prepared key and modifiers already exists it is
 * first removed.  Does nothing if @a message is NULL.  The window takes
 * ownership of @a message.
 *
 * @param key       Unicode code point for the shortcut key.
 * @param modifiers Modifier-key bitmask (e.g. B_COMMAND_KEY).
 * @param message   The message to post; ownership is transferred.
 * @param target    Target BHandler, or NULL to post to the current focus view.
 *
 * @see BWindow::RemoveShortcut(), BWindow::HasShortcut()
 */
void
BWindow::AddShortcut(uint32 key, uint32 modifiers, BMessage* message, BHandler* target)
{
	if (message == NULL)
		return;

	Shortcut* shortcut = new(std::nothrow) Shortcut(key, modifiers, message, target);
	if (shortcut == NULL)
		return;

	// removes the shortcut if it already exists!
	RemoveShortcut(shortcut->Key(), shortcut->Modifiers());

	fShortcuts.AddItem(shortcut);
}


/**
 * @brief Returns @c true if a shortcut for the given key and modifiers is registered.
 *
 * @param key       Unicode code point for the shortcut key.
 * @param modifiers Modifier-key bitmask.
 * @return @c true if a matching shortcut exists, @c false otherwise.
 *
 * @see BWindow::AddShortcut(), BWindow::RemoveShortcut()
 */
bool
BWindow::HasShortcut(uint32 key, uint32 modifiers)
{
	return _FindShortcut(key, modifiers) != NULL;
}


/**
 * @brief Removes the registered shortcut for the given key and modifiers.
 *
 * If no matching shortcut is found and the key/modifiers identify the
 * "Command+Q" quit shortcut, the internal @c fNoQuitShortcut flag is set so
 * that the built-in quit handling is also suppressed.
 *
 * @param key       Unicode code point for the shortcut key.
 * @param modifiers Modifier-key bitmask.
 *
 * @see BWindow::AddShortcut(), BWindow::HasShortcut()
 */
void
BWindow::RemoveShortcut(uint32 key, uint32 modifiers)
{
	Shortcut* shortcut = _FindShortcut(key, modifiers);
	if (shortcut != NULL && fShortcuts.RemoveItem(shortcut))
		delete shortcut;
	else if (key == 'Q' && modifiers == B_COMMAND_KEY)
		fNoQuitShortcut = true; // the quit shortcut is a fake shortcut
}


/**
 * @brief Returns the current default button of the window.
 *
 * The default button is invoked when the user presses Return/Enter without
 * any modifier keys while the window is active.
 *
 * @return The default BButton, or NULL if none has been set.
 *
 * @see BWindow::SetDefaultButton()
 */
BButton*
BWindow::DefaultButton() const
{
	// TODO: What about locking?!?
	return fDefaultButton;
}


/**
 * @brief Designates a BButton as the window's default button.
 *
 * The previously registered default button (if any) is deselected via
 * BButton::MakeDefault(false) and invalidated.  The new @a button is then
 * selected via BButton::MakeDefault(true) and invalidated.
 *
 * @param button The new default BButton, or NULL to clear the default button.
 *
 * @see BWindow::DefaultButton(), BButton::MakeDefault()
 */
void
BWindow::SetDefaultButton(BButton* button)
{
	// TODO: What about locking?!?
	if (fDefaultButton == button)
		return;

	if (fDefaultButton != NULL) {
		// tell old button it's no longer the default one
		BButton* oldDefault = fDefaultButton;
		oldDefault->MakeDefault(false);
		oldDefault->Invalidate();
	}

	fDefaultButton = button;

	if (button != NULL) {
		// notify new default button
		fDefaultButton->MakeDefault(true);
		fDefaultButton->Invalidate();
	}
}


/**
 * @brief Returns @c true if any part of the window needs to be redrawn.
 *
 * Queries the app_server synchronously via AS_NEEDS_UPDATE.
 *
 * @return @c true if there is a pending update, @c false otherwise (or if the
 *         window cannot be locked).
 *
 * @see BWindow::UpdateIfNeeded()
 */
bool
BWindow::NeedsUpdate() const
{
	if (!const_cast<BWindow*>(this)->Lock())
		return false;

	fLink->StartMessage(AS_NEEDS_UPDATE);

	int32 code = B_ERROR;
	fLink->FlushWithReply(code);

	const_cast<BWindow*>(this)->Unlock();

	return code == B_OK;
}


/**
 * @brief Immediately processes any pending _UPDATE_ messages in the window's queue.
 *
 * Must be called from the window thread.  Syncs with the app_server, dequeues
 * all pending messages from the port, then dispatches every _UPDATE_ message
 * found in the queue.  This allows a long-running operation on the window
 * thread to service redraws without returning to the event loop.
 *
 * @note This function has no effect when called from a thread other than the
 *       window thread, or when the message queue is already locked (recursive
 *       update dispatch).
 *
 * @see BWindow::NeedsUpdate()
 */
void
BWindow::UpdateIfNeeded()
{
	// works only from the window thread
	if (find_thread(NULL) != Thread())
		return;

	// if the queue is already locked we are called recursivly
	// from our own dispatched update message
	if (((const BMessageQueue*)MessageQueue())->IsLocked())
		return;

	if (!Lock())
		return;

	// make sure all requests that would cause an update have
	// arrived at the server
	Sync();

	// Since we're blocking the event loop, we need to retrieve
	// all messages that are pending on the port.
	_DequeueAll();

	BMessageQueue* queue = MessageQueue();

	// First process and remove any _UPDATE_ message in the queue
	// With the current design, there can only be one at a time

	while (true) {
		queue->Lock();

		BMessage* message = queue->FindMessage(_UPDATE_, 0);
		queue->RemoveMessage(message);

		queue->Unlock();

		if (message == NULL)
			break;

		BWindow::DispatchMessage(message, this);
		delete message;
	}

	Unlock();
}


/**
 * @brief Finds a descendant view by name.
 *
 * Searches the entire view hierarchy rooted at the top-level view for the
 * first view whose name matches @a viewName.
 *
 * @param viewName The name to search for.
 * @return The matching BView, or NULL if not found or the window cannot be locked.
 *
 * @see BWindow::FindView(BPoint) const
 */
BView*
BWindow::FindView(const char* viewName) const
{
	BAutolock locker(const_cast<BWindow*>(this));
	if (!locker.IsLocked())
		return NULL;

	return fTopView->FindView(viewName);
}


/**
 * @brief Finds the deepest visible view that contains the given point.
 *
 * @a point is interpreted in window coordinates (i.e. relative to the
 * window's content area).
 *
 * @param point A point in window coordinates to test.
 * @return The deepest visible BView that contains @a point, or NULL if none
 *         is found or the window cannot be locked.
 *
 * @see BWindow::FindView(const char*) const
 */
BView*
BWindow::FindView(BPoint point) const
{
	BAutolock locker(const_cast<BWindow*>(this));
	if (!locker.IsLocked())
		return NULL;

	// point is assumed to be in window coordinates,
	// fTopView has same bounds as window
	return _FindView(fTopView, point);
}


/**
 * @brief Returns the view that currently has keyboard focus in this window.
 *
 * @return The focused BView, or NULL if no view has focus.
 *
 * @see BView::MakeFocus(), BWindow::_SetFocus()
 */
BView*
BWindow::CurrentFocus() const
{
	return fFocus;
}


/**
 * @brief Activates or deactivates the window.
 *
 * Sends AS_ACTIVATE_WINDOW to the app_server.  Activating also un-minimizes
 * the window if it was minimized.  Does nothing if the window is hidden.
 *
 * @param active @c true to activate, @c false to deactivate.
 *
 * @see BWindow::IsActive(), BWindow::WindowActivated()
 */
void
BWindow::Activate(bool active)
{
	if (!Lock())
		return;

	if (!IsHidden()) {
		fMinimized = false;
			// activating a window will also unminimize it

		fLink->StartMessage(AS_ACTIVATE_WINDOW);
		fLink->Attach<bool>(active);
		fLink->Flush();
	}

	Unlock();
}


/**
 * @brief Hook called when the window gains or loses active (key) status.
 *
 * The default implementation does nothing.  Override to update UI elements
 * that depend on whether the window is the key window (e.g. draw focus rings,
 * enable or disable controls).
 *
 * @param focus @c true if the window just became active, @c false if it lost
 *              active status.
 *
 * @see BWindow::Activate(), BWindow::IsActive()
 */
void
BWindow::WindowActivated(bool focus)
{
	// hook function
	// does nothing
}


/**
 * @brief Converts a point from window coordinates to screen coordinates (in place).
 *
 * @param point In/out: the point to convert.
 */
void
BWindow::ConvertToScreen(BPoint* point) const
{
	point->x += fFrame.left;
	point->y += fFrame.top;
}


/**
 * @brief Converts a point from window coordinates to screen coordinates (by value).
 *
 * @param point The point to convert.
 * @return The converted point in screen coordinates.
 */
BPoint
BWindow::ConvertToScreen(BPoint point) const
{
	return point + fFrame.LeftTop();
}


/**
 * @brief Converts a point from screen coordinates to window coordinates (in place).
 *
 * @param point In/out: the point to convert.
 */
void
BWindow::ConvertFromScreen(BPoint* point) const
{
	point->x -= fFrame.left;
	point->y -= fFrame.top;
}


/**
 * @brief Converts a point from screen coordinates to window coordinates (by value).
 *
 * @param point The point to convert.
 * @return The converted point in window coordinates.
 */
BPoint
BWindow::ConvertFromScreen(BPoint point) const
{
	return point - fFrame.LeftTop();
}


/**
 * @brief Converts a rect from window coordinates to screen coordinates (in place).
 *
 * @param rect In/out: the rectangle to convert.
 */
void
BWindow::ConvertToScreen(BRect* rect) const
{
	rect->OffsetBy(fFrame.LeftTop());
}


/**
 * @brief Converts a rect from window coordinates to screen coordinates (by value).
 *
 * @param rect The rectangle to convert.
 * @return The converted rectangle in screen coordinates.
 */
BRect
BWindow::ConvertToScreen(BRect rect) const
{
	return rect.OffsetByCopy(fFrame.LeftTop());
}


/**
 * @brief Converts a rect from screen coordinates to window coordinates (in place).
 *
 * @param rect In/out: the rectangle to convert.
 */
void
BWindow::ConvertFromScreen(BRect* rect) const
{
	rect->OffsetBy(-fFrame.left, -fFrame.top);
}


/**
 * @brief Converts a rect from screen coordinates to window coordinates (by value).
 *
 * @param rect The rectangle to convert.
 * @return The converted rectangle in window coordinates.
 */
BRect
BWindow::ConvertFromScreen(BRect rect) const
{
	return rect.OffsetByCopy(-fFrame.left, -fFrame.top);
}


/**
 * @brief Returns @c true if the window is currently minimized (iconified).
 *
 * @return @c true if minimized, @c false otherwise (or if the window cannot
 *         be locked).
 *
 * @see BWindow::Minimize()
 */
bool
BWindow::IsMinimized() const
{
	BAutolock locker(const_cast<BWindow*>(this));
	if (!locker.IsLocked())
		return false;

	return fMinimized;
}


/**
 * @brief Returns the window's content area in window-local coordinates.
 *
 * Always returns a rectangle with a top-left at the origin (0, 0); the
 * bottom-right reflects the current width and height.
 *
 * @return The content bounds rectangle.
 *
 * @see BWindow::Frame(), BWindow::Size()
 */
BRect
BWindow::Bounds() const
{
	return BRect(0, 0, fFrame.Width(), fFrame.Height());
}


/**
 * @brief Returns the window's content frame in screen coordinates.
 *
 * Does not include the decorator border or title tab.
 *
 * @return The content frame in screen coordinates.
 *
 * @see BWindow::DecoratorFrame(), BWindow::Bounds()
 */
BRect
BWindow::Frame() const
{
	return fFrame;
}


/**
 * @brief Returns the full frame of the window including the decorator.
 *
 * Queries the decorator settings (tab frame, border width) from the
 * app_server and expands the content frame accordingly.  Falls back to
 * sensible defaults if the settings cannot be retrieved.
 *
 * @return The decorator frame in screen coordinates (includes border and title tab).
 *
 * @see BWindow::Frame(), BWindow::GetDecoratorSettings()
 */
BRect
BWindow::DecoratorFrame() const
{
	BRect decoratorFrame(Frame());
	BRect tabRect(0, 0, 0, 0);

	float borderWidth = 5.0;

	BMessage settings;
	if (GetDecoratorSettings(&settings) == B_OK) {
		settings.FindRect("tab frame", &tabRect);
		settings.FindFloat("border width", &borderWidth);
	} else {
		// probably no-border window look
		if (fLook == B_NO_BORDER_WINDOW_LOOK)
			borderWidth = 0.f;
		else if (fLook == B_BORDERED_WINDOW_LOOK)
			borderWidth = 1.f;
		// else use fall-back values from above
	}

	if (fLook == kLeftTitledWindowLook) {
		decoratorFrame.top -= borderWidth;
		decoratorFrame.left -= borderWidth + tabRect.Width();
		decoratorFrame.right += borderWidth;
		decoratorFrame.bottom += borderWidth;
	} else {
		decoratorFrame.top -= borderWidth + tabRect.Height();
		decoratorFrame.left -= borderWidth;
		decoratorFrame.right += borderWidth;
		decoratorFrame.bottom += borderWidth;
	}

	return decoratorFrame;
}


/**
 * @brief Returns the current content size of the window as a BSize.
 *
 * @return The width and height of the window's content area.
 *
 * @see BWindow::Frame(), BWindow::Bounds()
 */
BSize
BWindow::Size() const
{
	return BSize(fFrame.Width(), fFrame.Height());
}


/**
 * @brief Returns the window's title string.
 *
 * @return A pointer to the internally stored title.  The pointer remains
 *         valid until SetTitle() is called or the window is destroyed.
 *
 * @see BWindow::SetTitle()
 */
const char*
BWindow::Title() const
{
	return fTitle;
}


/**
 * @brief Changes the window's title text.
 *
 * Updates the internal title buffer, renames the handler and thread via
 * _SetName(), and notifies the app_server (AS_SET_WINDOW_TITLE).  NULL is
 * treated as an empty string.
 *
 * @param title The new title string; may not be NULL (treated as "").
 *
 * @see BWindow::Title()
 */
void
BWindow::SetTitle(const char* title)
{
	if (title == NULL)
		title = "";

	free(fTitle);
	fTitle = strdup(title);

	_SetName(title);

	// we notify the app_server so we can actually see the change
	if (Lock()) {
		fLink->StartMessage(AS_SET_WINDOW_TITLE);
		fLink->AttachString(fTitle);
		fLink->Flush();
		Unlock();
	}
}


/**
 * @brief Returns @c true if this window is currently the active (key) window.
 *
 * @return @c true if the window is active, @c false otherwise.
 *
 * @see BWindow::Activate(), BWindow::WindowActivated()
 */
bool
BWindow::IsActive() const
{
	return fActive;
}


/**
 * @brief Sets the window's key menu bar.
 *
 * The key menu bar receives keyboard focus when the user presses the menu
 * activation key (typically Command+Escape on Haiku).
 *
 * @param bar The BMenuBar to designate as the key menu bar, or NULL to clear it.
 *
 * @see BWindow::KeyMenuBar()
 */
void
BWindow::SetKeyMenuBar(BMenuBar* bar)
{
	fKeyMenuBar = bar;
}


/**
 * @brief Returns the window's key menu bar.
 *
 * @return The current key BMenuBar, or NULL if none has been set.
 *
 * @see BWindow::SetKeyMenuBar()
 */
BMenuBar*
BWindow::KeyMenuBar() const
{
	return fKeyMenuBar;
}


/**
 * @brief Returns @c true if the window has a modal feel.
 *
 * A window is modal if its feel is B_MODAL_SUBSET_WINDOW_FEEL,
 * B_MODAL_APP_WINDOW_FEEL, B_MODAL_ALL_WINDOW_FEEL, or kMenuWindowFeel.
 *
 * @return @c true for modal windows.
 *
 * @see BWindow::IsFloating(), BWindow::Feel()
 */
bool
BWindow::IsModal() const
{
	return fFeel == B_MODAL_SUBSET_WINDOW_FEEL
		|| fFeel == B_MODAL_APP_WINDOW_FEEL
		|| fFeel == B_MODAL_ALL_WINDOW_FEEL
		|| fFeel == kMenuWindowFeel;
}


/**
 * @brief Returns @c true if the window has a floating feel.
 *
 * A window is floating if its feel is B_FLOATING_SUBSET_WINDOW_FEEL,
 * B_FLOATING_APP_WINDOW_FEEL, or B_FLOATING_ALL_WINDOW_FEEL.
 *
 * @return @c true for floating windows.
 *
 * @see BWindow::IsModal(), BWindow::Feel()
 */
bool
BWindow::IsFloating() const
{
	return fFeel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| fFeel == B_FLOATING_APP_WINDOW_FEEL
		|| fFeel == B_FLOATING_ALL_WINDOW_FEEL;
}


/**
 * @brief Adds a normal window to this modal or floating subset window's subset.
 *
 * The subset window will restrict input to @a window and to itself when the
 * subset window is shown.  Both this window's feel must be subset-feel and
 * @a window must have B_NORMAL_WINDOW_FEEL.
 *
 * @param window The normal window to add to the subset.
 * @return @c B_OK on success, @c B_BAD_VALUE if the arguments are invalid,
 *         or another error code if the server request fails.
 *
 * @see BWindow::RemoveFromSubset(), BWindow::Feel()
 */
status_t
BWindow::AddToSubset(BWindow* window)
{
	if (window == NULL || window->Feel() != B_NORMAL_WINDOW_FEEL
		|| (fFeel != B_MODAL_SUBSET_WINDOW_FEEL
			&& fFeel != B_FLOATING_SUBSET_WINDOW_FEEL))
		return B_BAD_VALUE;

	if (!Lock())
		return B_ERROR;

	status_t status = B_ERROR;
	fLink->StartMessage(AS_ADD_TO_SUBSET);
	fLink->Attach<int32>(_get_object_token_(window));
	fLink->FlushWithReply(status);

	Unlock();

	return status;
}


/**
 * @brief Removes a normal window from this subset window's subset.
 *
 * @param window The window to remove from the subset.
 * @return @c B_OK on success, @c B_BAD_VALUE if the arguments are invalid,
 *         or another error code if the server request fails.
 *
 * @see BWindow::AddToSubset()
 */
status_t
BWindow::RemoveFromSubset(BWindow* window)
{
	if (window == NULL || window->Feel() != B_NORMAL_WINDOW_FEEL
		|| (fFeel != B_MODAL_SUBSET_WINDOW_FEEL
			&& fFeel != B_FLOATING_SUBSET_WINDOW_FEEL))
		return B_BAD_VALUE;

	if (!Lock())
		return B_ERROR;

	status_t status = B_ERROR;
	fLink->StartMessage(AS_REMOVE_FROM_SUBSET);
	fLink->Attach<int32>(_get_object_token_(window));
	fLink->FlushWithReply(status);

	Unlock();

	return status;
}


/**
 * @brief Implements virtual-dispatch compatibility for future API additions.
 *
 * Currently handles PERFORM_CODE_SET_LAYOUT to allow binary-compatible
 * addition of SetLayout() without breaking the vtable ABI.
 *
 * @param code  The perform code indicating which operation to execute.
 * @param _data Pointer to the operation-specific data struct.
 * @return @c B_OK on success, or the result from BLooper::Perform() for
 *         unknown codes.
 *
 * @see BLooper::Perform()
 */
status_t
BWindow::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BWindow::SetLayout(data->layout);
			return B_OK;
}
	}

	return BLooper::Perform(code, _data);
}


/**
 * @brief Sets both the look and feel of the window using a window_type constant.
 *
 * Decomposes the type into look and feel and forwards to SetLook() and
 * SetFeel() respectively.
 *
 * @param type The combined window type (e.g. B_TITLED_WINDOW).
 * @return @c B_OK on success, or the first error returned by SetLook() or SetFeel().
 *
 * @see BWindow::Type(), BWindow::SetLook(), BWindow::SetFeel()
 */
status_t
BWindow::SetType(window_type type)
{
	window_look look;
	window_feel feel;
	_DecomposeType(type, &look, &feel);

	status_t status = SetLook(look);
	if (status == B_OK)
		status = SetFeel(feel);

	return status;
}


/**
 * @brief Returns the combined window_type for the current look and feel.
 *
 * Returns B_UNTYPED_WINDOW when the look/feel combination does not map to a
 * named window_type constant.
 *
 * @return The combined window type.
 *
 * @see BWindow::SetType(), BWindow::Look(), BWindow::Feel()
 */
window_type
BWindow::Type() const
{
	return _ComposeType(fLook, fFeel);
}


/**
 * @brief Changes the visual appearance (look) of the window.
 *
 * Sends AS_SET_LOOK to the app_server.  On success the internal @c fLook field
 * is updated.  Changing the look may also alter the window's size constraints
 * (e.g. removing the title tab changes the minimum height).
 *
 * @param look The new window look (e.g. B_TITLED_WINDOW_LOOK).
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see BWindow::Look(), BWindow::SetFeel(), BWindow::SetType()
 */
status_t
BWindow::SetLook(window_look look)
{
	BAutolock locker(this);
	if (!locker.IsLocked())
		return B_BAD_VALUE;

	fLink->StartMessage(AS_SET_LOOK);
	fLink->Attach<int32>((int32)look);

	status_t status = B_ERROR;
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
		fLook = look;

	// TODO: this could have changed the window size, and thus, we
	//	need to get it from the server (and call _AdoptResize()).

	return status;
}


/**
 * @brief Returns the window's current visual look.
 *
 * @return The current window_look value.
 *
 * @see BWindow::SetLook()
 */
window_look
BWindow::Look() const
{
	return fLook;
}


/**
 * @brief Changes the behaviour (feel) of the window relative to other windows.
 *
 * Sends AS_SET_FEEL to the app_server.  On success the internal @c fFeel field
 * is updated.
 *
 * @param feel The new window feel (e.g. B_NORMAL_WINDOW_FEEL).
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see BWindow::Feel(), BWindow::SetLook(), BWindow::SetType()
 */
status_t
BWindow::SetFeel(window_feel feel)
{
	BAutolock locker(this);
	if (!locker.IsLocked())
		return B_BAD_VALUE;

	fLink->StartMessage(AS_SET_FEEL);
	fLink->Attach<int32>((int32)feel);

	status_t status = B_ERROR;
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
		fFeel = feel;

	return status;
}


/**
 * @brief Returns the window's current feel.
 *
 * @return The current window_feel value.
 *
 * @see BWindow::SetFeel()
 */
window_feel
BWindow::Feel() const
{
	return fFeel;
}


/**
 * @brief Sets the window behaviour flags.
 *
 * Sends AS_SET_FLAGS to the app_server.  On success the internal @c fFlags
 * field is updated to the new value.
 *
 * @param flags New bitmask of window flags (e.g. B_NOT_RESIZABLE |
 *              B_NOT_CLOSABLE).
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see BWindow::Flags()
 */
status_t
BWindow::SetFlags(uint32 flags)
{
	BAutolock locker(this);
	if (!locker.IsLocked())
		return B_BAD_VALUE;

	fLink->StartMessage(AS_SET_FLAGS);
	fLink->Attach<uint32>(flags);

	int32 status = B_ERROR;
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
		fFlags = flags;

	return status;
}


/**
 * @brief Returns the current window behaviour flags.
 *
 * @return The current flags bitmask.
 *
 * @see BWindow::SetFlags()
 */
uint32
BWindow::Flags() const
{
	return fFlags;
}


/**
 * @brief Sets pixel or byte alignment constraints for window movement and resizing.
 *
 * Sends AS_SET_ALIGNMENT to the app_server.  The server enforces that all
 * position and size changes satisfy the specified grid.
 *
 * @param mode        Alignment mode (B_BYTE_ALIGNMENT or B_PIXEL_ALIGNMENT).
 * @param h           Horizontal grid size.
 * @param hOffset     Horizontal offset within the grid.
 * @param width       Width grid size.
 * @param widthOffset Width offset within the grid.
 * @param v           Vertical grid size.
 * @param vOffset     Vertical offset within the grid.
 * @param height      Height grid size.
 * @param heightOffset Height offset within the grid.
 * @return @c B_OK on success, @c B_BAD_VALUE for invalid parameters, or an
 *         error code if the server request fails.
 *
 * @see BWindow::GetWindowAlignment()
 */
status_t
BWindow::SetWindowAlignment(window_alignment mode,
	int32 h, int32 hOffset, int32 width, int32 widthOffset,
	int32 v, int32 vOffset, int32 height, int32 heightOffset)
{
	if ((mode & (B_BYTE_ALIGNMENT | B_PIXEL_ALIGNMENT)) == 0
		|| (hOffset >= 0 && hOffset <= h)
		|| (vOffset >= 0 && vOffset <= v)
		|| (widthOffset >= 0 && widthOffset <= width)
		|| (heightOffset >= 0 && heightOffset <= height))
		return B_BAD_VALUE;

	// TODO: test if hOffset = 0 and set it to 1 if true.

	if (!Lock())
		return B_ERROR;

	fLink->StartMessage(AS_SET_ALIGNMENT);
	fLink->Attach<int32>((int32)mode);
	fLink->Attach<int32>(h);
	fLink->Attach<int32>(hOffset);
	fLink->Attach<int32>(width);
	fLink->Attach<int32>(widthOffset);
	fLink->Attach<int32>(v);
	fLink->Attach<int32>(vOffset);
	fLink->Attach<int32>(height);
	fLink->Attach<int32>(heightOffset);

	status_t status = B_ERROR;
	fLink->FlushWithReply(status);

	Unlock();

	return status;
}


/**
 * @brief Retrieves the current window alignment constraints from the app_server.
 *
 * Sends AS_GET_ALIGNMENT and reads back all alignment parameters.
 *
 * @param mode         Output: current alignment mode.
 * @param h            Output: horizontal grid size.
 * @param hOffset      Output: horizontal offset.
 * @param width        Output: width grid size.
 * @param widthOffset  Output: width offset.
 * @param v            Output: vertical grid size.
 * @param vOffset      Output: vertical offset.
 * @param height       Output: height grid size.
 * @param heightOffset Output: height offset.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see BWindow::SetWindowAlignment()
 */
status_t
BWindow::GetWindowAlignment(window_alignment* mode,
	int32* h, int32* hOffset, int32* width, int32* widthOffset,
	int32* v, int32* vOffset, int32* height, int32* heightOffset) const
{
	if (!const_cast<BWindow*>(this)->Lock())
		return B_ERROR;

	fLink->StartMessage(AS_GET_ALIGNMENT);

	status_t status;
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK) {
		fLink->Read<int32>((int32*)mode);
		fLink->Read<int32>(h);
		fLink->Read<int32>(hOffset);
		fLink->Read<int32>(width);
		fLink->Read<int32>(widthOffset);
		fLink->Read<int32>(v);
		fLink->Read<int32>(hOffset);
		fLink->Read<int32>(height);
		fLink->Read<int32>(heightOffset);
	}

	const_cast<BWindow*>(this)->Unlock();
	return status;
}


/**
 * @brief Returns the bitmask of workspaces the window currently occupies.
 *
 * Queries the app_server synchronously via AS_GET_WORKSPACES.
 *
 * @return Workspace bitmask, or 0 if the window cannot be locked.
 *
 * @see BWindow::SetWorkspaces(), BWindow::CurrentWorkspace()
 */
uint32
BWindow::Workspaces() const
{
	if (!const_cast<BWindow*>(this)->Lock())
		return 0;

	uint32 workspaces = 0;

	fLink->StartMessage(AS_GET_WORKSPACES);

	status_t status;
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
		fLink->Read<uint32>(&workspaces);

	const_cast<BWindow*>(this)->Unlock();
	return workspaces;
}


/**
 * @brief Moves the window to the specified workspace(s).
 *
 * Only applies to normal (B_NORMAL_WINDOW_FEEL) windows.  Pass
 * B_CURRENT_WORKSPACE to move the window to the active workspace, or a
 * bitmask for specific workspaces.
 *
 * @param workspaces Workspace bitmask, e.g. B_CURRENT_WORKSPACE or
 *                   (1 << 0) | (1 << 2) for workspaces 1 and 3.
 *
 * @see BWindow::Workspaces()
 */
void
BWindow::SetWorkspaces(uint32 workspaces)
{
	// TODO: don't forget about Tracker's background window.
	if (fFeel != B_NORMAL_WINDOW_FEEL)
		return;

	if (Lock()) {
		fLink->StartMessage(AS_SET_WORKSPACES);
		fLink->Attach<uint32>(workspaces);
		fLink->Flush();
		Unlock();
	}
}


/**
 * @brief Returns the view over which the mouse pointer was last moved.
 *
 * Updated during B_MOUSE_MOVED processing in _SanitizeMessage().
 *
 * @return The last BView that received a B_MOUSE_MOVED event, or NULL.
 */
BView*
BWindow::LastMouseMovedView() const
{
	return fLastMouseMovedView;
}


/**
 * @brief Moves the window by the given offsets relative to its current position.
 *
 * A no-op if both offsets are zero.  Delegates to MoveTo().
 *
 * @param dx Horizontal offset in pixels (positive moves right).
 * @param dy Vertical offset in pixels (positive moves down).
 *
 * @see BWindow::MoveTo()
 */
void
BWindow::MoveBy(float dx, float dy)
{
	if ((dx != 0.0f || dy != 0.0f) && Lock()) {
		MoveTo(fFrame.left + dx, fFrame.top + dy);
		Unlock();
	}
}


/**
 * @brief Moves the window to the specified screen position (BPoint overload).
 *
 * @param point The new top-left corner of the window content area in screen
 *              coordinates.
 *
 * @see BWindow::MoveTo(float, float), BWindow::MoveBy()
 */
void
BWindow::MoveTo(BPoint point)
{
	MoveTo(point.x, point.y);
}


/**
 * @brief Moves the window so that its top-left corner is at (@a x, @a y).
 *
 * Coordinates are rounded to the nearest pixel and sent to the app_server
 * via AS_WINDOW_MOVE.  The internal frame is updated on success.
 *
 * @param x New left edge of the content area in screen coordinates.
 * @param y New top edge of the content area in screen coordinates.
 *
 * @see BWindow::MoveTo(BPoint), BWindow::MoveBy()
 */
void
BWindow::MoveTo(float x, float y)
{
	if (!Lock())
		return;

	x = roundf(x);
	y = roundf(y);

	if (fFrame.left != x || fFrame.top != y) {
		fLink->StartMessage(AS_WINDOW_MOVE);
		fLink->Attach<float>(x);
		fLink->Attach<float>(y);

		status_t status;
		if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
			fFrame.OffsetTo(x, y);
	}

	Unlock();
}


/**
 * @brief Resizes the window by the given deltas relative to its current size.
 *
 * Delegates to ResizeTo() which enforces size limits.
 *
 * @param dx Width change in pixels (positive makes the window wider).
 * @param dy Height change in pixels (positive makes the window taller).
 *
 * @see BWindow::ResizeTo()
 */
void
BWindow::ResizeBy(float dx, float dy)
{
	if (Lock()) {
		ResizeTo(fFrame.Width() + dx, fFrame.Height() + dy);
		Unlock();
	}
}


/**
 * @brief Resizes the window's content area to the specified dimensions.
 *
 * The requested size is clamped to the current size limits before being sent
 * to the app_server via AS_WINDOW_RESIZE.  On success, the internal frame
 * and the top-level view are updated via _AdoptResize().
 *
 * @param width  Desired content width in pixels.
 * @param height Desired content height in pixels.
 *
 * @see BWindow::ResizeBy(), BWindow::SetSizeLimits()
 */
void
BWindow::ResizeTo(float width, float height)
{
	if (!Lock())
		return;

	width = roundf(width);
	height = roundf(height);

	// stay in minimum & maximum frame limits
	if (width < fMinWidth)
		width = fMinWidth;
	else if (width > fMaxWidth)
		width = fMaxWidth;

	if (height < fMinHeight)
		height = fMinHeight;
	else if (height > fMaxHeight)
		height = fMaxHeight;

	if (width != fFrame.Width() || height != fFrame.Height()) {
		fLink->StartMessage(AS_WINDOW_RESIZE);
		fLink->Attach<float>(width);
		fLink->Attach<float>(height);

		status_t status;
		if (fLink->FlushWithReply(status) == B_OK && status == B_OK) {
			fFrame.right = fFrame.left + width;
			fFrame.bottom = fFrame.top + height;
			_AdoptResize();
		}
	}

	Unlock();
}


/**
 * @brief Resizes the window to the top-level layout's preferred size.
 *
 * Performs a layout pass, queries the top view's preferred, minimum, and
 * maximum sizes, optionally adjusts the height for height-for-width layouts,
 * then calls ResizeTo().
 *
 * @see BWindow::Layout(), BWindow::GetLayout()
 */
void
BWindow::ResizeToPreferred()
{
	BAutolock locker(this);
	Layout(false);

	float width = fTopView->PreferredSize().width;
	width = std::min(width, fTopView->MaxSize().width);
	width = std::max(width, fTopView->MinSize().width);

	float height = fTopView->PreferredSize().height;
	height = std::min(height, fTopView->MaxSize().height);
	height = std::max(height, fTopView->MinSize().height);

	if (GetLayout()->HasHeightForWidth())
		GetLayout()->GetHeightForWidth(width, NULL, NULL, &height);

	ResizeTo(width, height);
}


/**
 * @brief Centers the window within the given rectangle.
 *
 * Updates size limits first (to account for B_AUTO_UPDATE_SIZE_LIMITS), then
 * moves the window so that it is centred horizontally and vertically within
 * @a rect.  Calls MoveOnScreen() afterwards to ensure the window is fully
 * visible.
 *
 * @param rect The reference rectangle in screen coordinates.
 *
 * @see BWindow::CenterOnScreen(), BWindow::MoveOnScreen()
 */
void
BWindow::CenterIn(const BRect& rect)
{
	BAutolock locker(this);

	// Set size limits now if needed
	UpdateSizeLimits();

	MoveTo(BLayoutUtils::AlignInFrame(rect, Size(),
		BAlignment(B_ALIGN_HORIZONTAL_CENTER,
			B_ALIGN_VERTICAL_CENTER)).LeftTop());
	MoveOnScreen(B_DO_NOT_RESIZE_TO_FIT | B_MOVE_IF_PARTIALLY_OFFSCREEN);
}


/**
 * @brief Centers the window on the screen it currently occupies.
 *
 * Convenience wrapper around CenterIn() using the current screen's frame.
 *
 * @see BWindow::CenterIn(), BWindow::CenterOnScreen(screen_id)
 */
void
BWindow::CenterOnScreen()
{
	CenterIn(BScreen(this).Frame());
}


/**
 * @brief Centers the window on a specific screen identified by @a id.
 *
 * @param id The identifier of the screen to centre on.
 *
 * @see BWindow::CenterOnScreen(), BWindow::CenterIn()
 */
void
BWindow::CenterOnScreen(screen_id id)
{
	CenterIn(BScreen(id).Frame());
}


/**
 * @brief Moves (and optionally resizes) the window so it is fully on screen.
 *
 * Adjusts the window's position so that the entire decorator frame, including
 * the title tab and border, is visible on the screen.  If @a flags does not
 * include B_DO_NOT_RESIZE_TO_FIT, the window is also resized to fit on screen.
 * If the window is entirely off-screen, CenterOnScreen() is called instead.
 *
 * @param flags Control flags:
 *              - B_DO_NOT_RESIZE_TO_FIT: do not resize, only move.
 *              - B_MOVE_IF_PARTIALLY_OFFSCREEN: only move when partially off-screen.
 *
 * @see BWindow::CenterOnScreen(), BWindow::UpdateSizeLimits()
 */
void
BWindow::MoveOnScreen(uint32 flags)
{
	// Set size limits now if needed
	UpdateSizeLimits();

	BRect screenFrame = BScreen(this).Frame();
	BRect frame = Frame();

	float borderWidth;
	float tabHeight;
	_GetDecoratorSize(&borderWidth, &tabHeight);

	frame.InsetBy(-borderWidth, -borderWidth);
	frame.top -= tabHeight;

	if ((flags & B_DO_NOT_RESIZE_TO_FIT) == 0) {
		// Make sure the window fits on the screen
		if (frame.Width() > screenFrame.Width())
			frame.right -= frame.Width() - screenFrame.Width();
		if (frame.Height() > screenFrame.Height())
			frame.bottom -= frame.Height() - screenFrame.Height();

		BRect innerFrame = frame;
		innerFrame.top += tabHeight;
		innerFrame.InsetBy(borderWidth, borderWidth);
		ResizeTo(innerFrame.Width(), innerFrame.Height());
	}

	if (((flags & B_MOVE_IF_PARTIALLY_OFFSCREEN) == 0
			&& !screenFrame.Contains(frame))
		|| !frame.Intersects(screenFrame)) {
		// Off and away
		CenterOnScreen();
		return;
	}

	// Move such that the upper left corner, and most of the window
	// will be visible.
	float left = frame.left;
	if (left < screenFrame.left)
		left = screenFrame.left;
	else if (frame.right > screenFrame.right)
		left = std::max(0.f, screenFrame.right - frame.Width());

	float top = frame.top;
	if (top < screenFrame.top)
		top = screenFrame.top;
	else if (frame.bottom > screenFrame.bottom)
		top = std::max(0.f, screenFrame.bottom - frame.Height());

	if (top != frame.top || left != frame.left)
		MoveTo(left + borderWidth, top + tabHeight + borderWidth);
}


/**
 * @brief Makes the window visible and, on the first call, starts the message loop.
 *
 * Decrements the show-level counter and notifies the app_server.  If the
 * window has not yet been run (first Show() call), Run() is invoked
 * automatically to start the looper thread.  Subsequent Show() calls simply
 * counteract Hide() calls.
 *
 * @note If the app_server link is not yet valid the looper thread is not
 *       started and @c fThread is set to @c B_ERROR.
 *
 * @see BWindow::Hide(), BWindow::IsHidden(), BWindow::Run()
 */
void
BWindow::Show()
{
	bool runCalled = true;
	if (Lock()) {
		fShowLevel--;

		_SendShowOrHideMessage();

		runCalled = fRunCalled;

		Unlock();
	}

	if (!runCalled) {
		// This is the fist time Show() is called, which implicitly runs the
		// looper. NOTE: The window is still locked if it has not been
		// run yet, so accessing members is safe.
		if (fLink->SenderPort() < B_OK) {
			// We don't have valid app_server connection; there is no point
			// in starting our looper
			fThread = B_ERROR;
			return;
		} else
			Run();
	}
}


/**
 * @brief Hides the window from the screen.
 *
 * Increments the show-level counter and notifies the app_server.  If the
 * window was minimized and is being hidden from show-level 0, it is first
 * un-minimized.  Multiple Hide() calls are stacked and must each be matched
 * with a Show() call.
 *
 * @see BWindow::Show(), BWindow::IsHidden()
 */
void
BWindow::Hide()
{
	if (Lock()) {
		// If we are minimized and are about to be hidden, unminimize
		if (IsMinimized() && fShowLevel == 0)
			Minimize(false);

		fShowLevel++;

		_SendShowOrHideMessage();

		Unlock();
	}
}


/**
 * @brief Returns @c true if the window is currently hidden.
 *
 * The window is hidden whenever its show-level is greater than zero, which
 * happens after Hide() calls have outnumbered Show() calls.
 *
 * @return @c true if the show-level is > 0.
 *
 * @see BWindow::Show(), BWindow::Hide()
 */
bool
BWindow::IsHidden() const
{
	return fShowLevel > 0;
}


/**
 * @brief Hook called when the window receives a B_QUIT_REQUESTED message.
 *
 * The default implementation delegates to BLooper::QuitRequested() which
 * returns @c true, causing the window to be closed.  Override to present a
 * "save changes?" prompt or to suppress closing.
 *
 * @return @c true to allow the window to close, @c false to keep it open.
 *
 * @see BLooper::QuitRequested(), BWindow::Quit()
 */
bool
BWindow::QuitRequested()
{
	return BLooper::QuitRequested();
}


/**
 * @brief Starts the window's message loop thread.
 *
 * Enables screen updates (EnableUpdates()) before calling BLooper::Run() so
 * that the first frame is visible as soon as the loop is running.  Normally
 * called automatically by the first Show() invocation.
 *
 * @return The thread_id of the window's looper thread.
 *
 * @see BLooper::Run(), BWindow::Show()
 */
thread_id
BWindow::Run()
{
	EnableUpdates();
	return BLooper::Run();
}


/**
 * @brief Sets the layout manager for the window's content area.
 *
 * Adopts the layout's view colours for the top-level view before installing
 * the layout so that colours propagate correctly.
 *
 * @param layout The BLayout to install; may be NULL to remove the layout.
 *
 * @see BWindow::GetLayout(), BWindow::InvalidateLayout(), BWindow::Layout()
 */
void
BWindow::SetLayout(BLayout* layout)
{
	// Adopt layout's colors for fTopView
	if (layout != NULL)
		fTopView->AdoptViewColors(layout->View());

	fTopView->SetLayout(layout);
}


/**
 * @brief Returns the layout manager currently installed on the content area.
 *
 * @return The current BLayout, or NULL if no layout is installed.
 *
 * @see BWindow::SetLayout()
 */
BLayout*
BWindow::GetLayout() const
{
	return fTopView->GetLayout();
}


/**
 * @brief Marks the layout as invalid, scheduling a layout pass on the next update.
 *
 * @param descendants If @c true, also invalidates the layout of all descendant views.
 *
 * @see BWindow::Layout(), BWindow::SetLayout()
 */
void
BWindow::InvalidateLayout(bool descendants)
{
	fTopView->InvalidateLayout(descendants);
}


/**
 * @brief Performs a layout pass on the window's view hierarchy.
 *
 * Updates size limits first (via UpdateSizeLimits()) and then delegates the
 * layout calculation to the top-level view.
 *
 * @param force If @c true, forces a full layout even if nothing appears to
 *              have changed since the last pass.
 *
 * @see BWindow::InvalidateLayout(), BWindow::SetLayout()
 */
void
BWindow::Layout(bool force)
{
	UpdateSizeLimits();

	// Do the actual layout
	fTopView->Layout(force);
}


/**
 * @brief Returns @c true if this window is an off-screen (BBitmap) window.
 *
 * @return @c true for windows created via the BBitmap-backed constructor.
 */
bool
BWindow::IsOffscreenWindow() const
{
	return fOffscreen;
}


/**
 * @brief Reports the scripting suites and properties this window supports.
 *
 * Adds "suite/vnd.Be-window" to @a data's "suites" array along with the
 * property and value info tables, then calls BLooper::GetSupportedSuites()
 * to add inherited suite information.
 *
 * @param data The BMessage to populate with suite information.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a data is NULL, or another
 *         error code on failure.
 *
 * @see BWindow::ResolveSpecifier()
 */
status_t
BWindow::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("suites", "suite/vnd.Be-window");
	if (status == B_OK) {
		BPropertyInfo propertyInfo(sWindowPropInfo, sWindowValueInfo);

		status = data->AddFlat("messages", &propertyInfo);
		if (status == B_OK)
			status = BLooper::GetSupportedSuites(data);
	}

	return status;
}


/**
 * @brief Resolves a scripting specifier to the appropriate BHandler.
 *
 * Handles B_WINDOW_MOVE_BY and B_WINDOW_MOVE_TO by returning @c this
 * directly, routes "View" specifiers to the top-level view without popping the
 * specifier, and routes "MenuBar" specifiers to the key menu bar (if present).
 * All other properties that match sWindowPropInfo are handled by this window.
 * Unrecognised specifiers are forwarded to BLooper::ResolveSpecifier().
 *
 * @param message   The scripting message being processed.
 * @param index     Current specifier index in the message.
 * @param specifier The current specifier sub-message.
 * @param what      The specifier type constant.
 * @param property  The property name string.
 * @return The BHandler that should process the message, or NULL on error.
 *
 * @see BWindow::GetSupportedSuites()
 */
BHandler*
BWindow::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	if (message->what == B_WINDOW_MOVE_BY
		|| message->what == B_WINDOW_MOVE_TO)
		return this;

	BPropertyInfo propertyInfo(sWindowPropInfo);
	if (propertyInfo.FindMatch(message, index, specifier, what, property) >= 0) {
		if (strcmp(property, "View") == 0) {
			// we will NOT pop the current specifier
			return fTopView;
		} else if (strcmp(property, "MenuBar") == 0) {
			if (fKeyMenuBar) {
				message->PopSpecifier();
				return fKeyMenuBar;
			} else {
				BMessage replyMsg(B_MESSAGE_NOT_UNDERSTOOD);
				replyMsg.AddInt32("error", B_NAME_NOT_FOUND);
				replyMsg.AddString("message",
					"This window doesn't have a main MenuBar");
				message->SendReply(&replyMsg);
				return NULL;
			}
		} else
			return this;
	}

	return BLooper::ResolveSpecifier(message, index, specifier, what, property);
}


//	#pragma mark - Private Methods


/**
 * @brief Initialises all BWindow member variables and creates the server-side window.
 *
 * Called by all public constructors.  Rounds the frame coordinates, stores all
 * window attributes, installs default keyboard shortcuts (cut/copy/paste/select-all,
 * minimize, zoom, hide-app, front/back), creates the IPC port and BPortLink,
 * sends AS_CREATE_WINDOW (or AS_CREATE_OFFSCREEN_WINDOW for bitmaps) to the
 * app_server and reads back the enforced frame and size limits, then calls
 * _CreateTopView() to set up the top-level BView.
 *
 * @param frame        Initial frame of the window's content area in screen coords.
 * @param title        Window title; NULL is treated as "".
 * @param look         Visual appearance of the window.
 * @param feel         Behavioural feel of the window.
 * @param flags        Window flags bitmask.
 * @param workspace    Workspace bitmask.
 * @param bitmapToken  Server token of a backing BBitmap, or -1 for a normal window.
 *
 * @note Calls debugger() and aborts if no BApplication exists or if the port
 *       cannot be created.
 */
void
BWindow::_InitData(BRect frame, const char* title, window_look look,
	window_feel feel, uint32 flags,	uint32 workspace, int32 bitmapToken)
{
	STRACE(("BWindow::InitData()\n"));

	if (be_app == NULL) {
		debugger("You need a valid BApplication object before interacting with "
			"the app_server");
		return;
	}

	frame.left = roundf(frame.left);
	frame.top = roundf(frame.top);
	frame.right = roundf(frame.right);
	frame.bottom = roundf(frame.bottom);

	fFrame = frame;

	if (title == NULL)
		title = "";

	fTitle = strdup(title);

	_SetName(title);

	fFeel = feel;
	fLook = look;
	fFlags = flags | B_ASYNCHRONOUS_CONTROLS;

	fInTransaction = bitmapToken >= 0;
	fUpdateRequested = false;
	fActive = false;
	fShowLevel = 1;

	fTopView = NULL;
	fFocus = NULL;
	fLastMouseMovedView	= NULL;
	fKeyMenuBar = NULL;
	fDefaultButton = NULL;

	// Shortcut 'Q' is handled in _HandleKeyDown() directly, as its message
	// get sent to the application, and not one of our handlers.
	// It is only installed for non-modal windows, though.
	fNoQuitShortcut = IsModal();

	if ((fFlags & B_NOT_CLOSABLE) == 0 && !IsModal()) {
		// Modal windows default to non-closable, but you can add the
		// shortcut manually, if a different behaviour is wanted
		AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));
	}

	// Edit modifier keys

	AddShortcut('X', B_COMMAND_KEY, new BMessage(B_CUT), NULL);
	AddShortcut('C', B_COMMAND_KEY, new BMessage(B_COPY), NULL);
	AddShortcut('V', B_COMMAND_KEY, new BMessage(B_PASTE), NULL);
	AddShortcut('A', B_COMMAND_KEY, new BMessage(B_SELECT_ALL), NULL);

	// Window modifier keys

	AddShortcut('M', B_COMMAND_KEY | B_CONTROL_KEY,
		new BMessage(_MINIMIZE_), NULL);
	AddShortcut('Z', B_COMMAND_KEY | B_CONTROL_KEY,
		new BMessage(_ZOOM_), NULL);
	AddShortcut('Z', B_SHIFT_KEY | B_COMMAND_KEY | B_CONTROL_KEY,
		new BMessage(_ZOOM_), NULL);
	AddShortcut('H', B_COMMAND_KEY | B_CONTROL_KEY,
		new BMessage(B_HIDE_APPLICATION), NULL);
	AddShortcut('F', B_COMMAND_KEY | B_CONTROL_KEY,
		new BMessage(_SEND_TO_FRONT_), NULL);
	AddShortcut('B', B_COMMAND_KEY | B_CONTROL_KEY,
		new BMessage(_SEND_BEHIND_), NULL);

	// We set the default pulse rate, but we don't start the pulse
	fPulseRate = 500000;
	fPulseRunner = NULL;

	fIsFilePanel = false;

	fMenuSem = -1;

	fMinimized = false;

	fMaxZoomHeight = 32768.0;
	fMaxZoomWidth = 32768.0;
	fMinHeight = 0.0;
	fMinWidth = 0.0;
	fMaxHeight = 32768.0;
	fMaxWidth = 32768.0;

	fLastViewToken = B_NULL_TOKEN;

	// TODO: other initializations!
	fOffscreen = false;

	// Create the server-side window

	port_id receivePort = create_port(B_LOOPER_PORT_DEFAULT_CAPACITY,
		"w<app_server");
	if (receivePort < B_OK) {
		// TODO: huh?
		debugger("Could not create BWindow's receive port, used for "
				 "interacting with the app_server!");
		delete this;
		return;
	}

	STRACE(("BWindow::InitData(): contacting app_server...\n"));

	// let app_server know that a window has been created.
	fLink = new(std::nothrow) BPrivate::PortLink(
		BApplication::Private::ServerLink()->SenderPort(), receivePort);
	if (fLink == NULL) {
		// Zombie!
		return;
	}

	{
		BPrivate::AppServerLink lockLink;
			// we're talking to the server application using our own
			// communication channel (fLink) - we better make sure no one
			// interferes by locking that channel (which AppServerLink does
			// implicetly)

		if (bitmapToken < 0) {
			fLink->StartMessage(AS_CREATE_WINDOW);
		} else {
			fLink->StartMessage(AS_CREATE_OFFSCREEN_WINDOW);
			fLink->Attach<int32>(bitmapToken);
			fOffscreen = true;
		}

		fLink->Attach<BRect>(fFrame);
		fLink->Attach<uint32>((uint32)fLook);
		fLink->Attach<uint32>((uint32)fFeel);
		fLink->Attach<uint32>(fFlags);
		fLink->Attach<uint32>(workspace);
		fLink->Attach<int32>(_get_object_token_(this));
		fLink->Attach<port_id>(receivePort);
		fLink->Attach<port_id>(fMsgPort);
		fLink->AttachString(title);

		port_id sendPort;
		int32 code;
		if (fLink->FlushWithReply(code) == B_OK
			&& code == B_OK
			&& fLink->Read<port_id>(&sendPort) == B_OK) {
			// read the frame size and its limits that were really
			// enforced on the server side

			fLink->Read<BRect>(&fFrame);
			fLink->Read<float>(&fMinWidth);
			fLink->Read<float>(&fMaxWidth);
			fLink->Read<float>(&fMinHeight);
			fLink->Read<float>(&fMaxHeight);

			fMaxZoomWidth = fMaxWidth;
			fMaxZoomHeight = fMaxHeight;
		} else
			sendPort = -1;

		// Redirect our link to the new window connection
		fLink->SetSenderPort(sendPort);
		STRACE(("Server says that our send port is %ld\n", sendPort));
	}

	STRACE(("Window locked?: %s\n", IsLocked() ? "True" : "False"));

	_CreateTopView();
}


/**
 * @brief Renames the BHandler and (if running) the looper thread to match the title.
 *
 * Builds a thread name of the form "w>title" (truncated to B_OS_NAME_LENGTH),
 * calls BHandler::SetName(), and — if the message loop is already running —
 * renames the thread via rename_thread().
 *
 * @param title The new window title; NULL is treated as "".
 */
void
BWindow::_SetName(const char* title)
{
	if (title == NULL)
		title = "";

	// we will change BWindow's thread name to "w>window title"

	char threadName[B_OS_NAME_LENGTH];
	strcpy(threadName, "w>");
#ifdef __HAIKU__
	strlcat(threadName, title, B_OS_NAME_LENGTH);
#else
	int32 length = strlen(title);
	length = min_c(length, B_OS_NAME_LENGTH - 3);
	memcpy(threadName + 2, title, length);
	threadName[length + 2] = '\0';
#endif

	// change the handler's name
	SetName(threadName);

	// if the message loop has been started...
	if (Thread() >= B_OK)
		rename_thread(Thread(), threadName);
}


/**
 * @brief Drains all pending messages from the window's IPC port into the message queue.
 *
 * Reads the current port count and retrieves that many messages using a zero
 * timeout so the call never blocks.  Each message is appended to the direct
 * target's queue.
 */
void
BWindow::_DequeueAll()
{
	//	Get message count from port
	int32 count = port_count(fMsgPort);

	for (int32 i = 0; i < count; i++) {
		BMessage* message = MessageFromPort(0);
		if (message != NULL)
			fDirectTarget->Queue()->AddMessage(message);
	}
}


/**
 * @brief The BWindow message loop — an extended version of BLooper::task_looper().
 *
 * Differs from the standard BLooper loop in three important ways:
 * -# Uses _DetermineTarget() to select the correct handler when no explicit
 *    target is embedded in the message.
 * -# Calls _UnpackMessage() to distribute a single app_server input event to
 *    multiple target views (each gets its own copy).
 * -# Calls _SanitizeMessage() to add view-local coordinate fields and transit
 *    information expected by BView mouse handlers.
 *
 * This design is necessary because the app_server always sends input events
 * to the preferred handler and relies on the window to fan them out to the
 * correct per-view targets.
 */
void
BWindow::task_looper()
{
	STRACE(("info: BWindow::task_looper() started.\n"));

	// Check that looper is locked (should be)
	AssertLocked();
	Unlock();

	if (IsLocked())
		debugger("window must not be locked!");

	while (!fTerminating) {
		// Did we get a message?
		BMessage* msg = MessageFromPort();
		if (msg)
			_AddMessagePriv(msg);

		//	Get message count from port
		int32 msgCount = port_count(fMsgPort);
		for (int32 i = 0; i < msgCount; ++i) {
			// Read 'count' messages from port (so we will not block)
			// We use zero as our timeout since we know there is stuff there
			msg = MessageFromPort(0);
			// Add messages to queue
			if (msg)
				_AddMessagePriv(msg);
		}

		bool dispatchNextMessage = true;
		while (!fTerminating && dispatchNextMessage) {
			// Get next message from queue (assign to fLastMessage after
			// locking)
			BMessage* message = fDirectTarget->Queue()->NextMessage();

			// Lock the looper
			if (!Lock()) {
				delete message;
				break;
			}

			fLastMessage = message;

			if (fLastMessage == NULL) {
				// No more messages: Unlock the looper and terminate the
				// dispatch loop.
				dispatchNextMessage = false;
			} else {
				// Get the target handler
				BMessage::Private messagePrivate(fLastMessage);
				bool usePreferred = messagePrivate.UsePreferredTarget();
				BHandler* handler = NULL;
				bool dropMessage = false;

				if (usePreferred) {
					handler = PreferredHandler();
					if (handler == NULL)
						handler = this;
				} else {
					gDefaultTokens.GetToken(messagePrivate.GetTarget(),
						B_HANDLER_TOKEN, (void**)&handler);

					// if this handler doesn't belong to us, we drop the message
					if (handler != NULL && handler->Looper() != this) {
						dropMessage = true;
						handler = NULL;
					}
				}

				if ((handler == NULL && !dropMessage) || usePreferred)
					handler = _DetermineTarget(fLastMessage, handler);

				unpack_cookie cookie;
				while (_UnpackMessage(cookie, &fLastMessage, &handler, &usePreferred)) {
					// if there is no target handler, the message is dropped
					if (handler != NULL) {
						_SanitizeMessage(fLastMessage, handler, usePreferred);

						// Is this a scripting message?
						if (fLastMessage->HasSpecifiers()) {
							int32 index = 0;
							// Make sure the current specifier is kosher
							if (fLastMessage->GetCurrentSpecifier(&index) == B_OK)
								handler = resolve_specifier(handler, fLastMessage);
						}

						if (handler != NULL)
							handler = _TopLevelFilter(fLastMessage, handler);

						if (handler != NULL)
							DispatchMessage(fLastMessage, handler);
					}

					// Delete the current message
					delete fLastMessage;
					fLastMessage = NULL;
				}
			}

			if (fTerminating) {
				// we leave the looper locked when we quit
				return;
			}

			Unlock();

			// Are any messages on the port?
			if (port_count(fMsgPort) > 0) {
				// Do outer loop
				dispatchNextMessage = false;
			}
		}
	}
}


/**
 * @brief Derives the window_type constant from separate look and feel values.
 *
 * Returns B_UNTYPED_WINDOW for any combination that does not map to one of the
 * named typed constants (B_TITLED_WINDOW, B_DOCUMENT_WINDOW, B_MODAL_WINDOW,
 * B_FLOATING_WINDOW, or B_BORDERED_WINDOW).
 *
 * @param look The window's visual look.
 * @param feel The window's feel.
 * @return The corresponding window_type, or B_UNTYPED_WINDOW.
 *
 * @see BWindow::_DecomposeType()
 */
window_type
BWindow::_ComposeType(window_look look, window_feel feel) const
{
	switch (feel) {
		case B_NORMAL_WINDOW_FEEL:
			switch (look) {
				case B_TITLED_WINDOW_LOOK:
					return B_TITLED_WINDOW;

				case B_DOCUMENT_WINDOW_LOOK:
					return B_DOCUMENT_WINDOW;

				case B_BORDERED_WINDOW_LOOK:
					return B_BORDERED_WINDOW;

				default:
					return B_UNTYPED_WINDOW;
			}
			break;

		case B_MODAL_APP_WINDOW_FEEL:
			if (look == B_MODAL_WINDOW_LOOK)
				return B_MODAL_WINDOW;
			break;

		case B_FLOATING_APP_WINDOW_FEEL:
			if (look == B_FLOATING_WINDOW_LOOK)
				return B_FLOATING_WINDOW;
			break;

		default:
			return B_UNTYPED_WINDOW;
	}

	return B_UNTYPED_WINDOW;
}


/**
 * @brief Splits a window_type constant into its constituent look and feel values.
 *
 * Unknown or B_UNTYPED_WINDOW types default to B_TITLED_WINDOW_LOOK and
 * B_NORMAL_WINDOW_FEEL.
 *
 * @param type  The combined window type to decompose.
 * @param _look Output: the corresponding window_look.
 * @param _feel Output: the corresponding window_feel.
 *
 * @see BWindow::_ComposeType()
 */
void
BWindow::_DecomposeType(window_type type, window_look* _look,
	window_feel* _feel) const
{
	switch (type) {
		case B_DOCUMENT_WINDOW:
			*_look = B_DOCUMENT_WINDOW_LOOK;
			*_feel = B_NORMAL_WINDOW_FEEL;
			break;

		case B_MODAL_WINDOW:
			*_look = B_MODAL_WINDOW_LOOK;
			*_feel = B_MODAL_APP_WINDOW_FEEL;
			break;

		case B_FLOATING_WINDOW:
			*_look = B_FLOATING_WINDOW_LOOK;
			*_feel = B_FLOATING_APP_WINDOW_FEEL;
			break;

		case B_BORDERED_WINDOW:
			*_look = B_BORDERED_WINDOW_LOOK;
			*_feel = B_NORMAL_WINDOW_FEEL;
			break;

		case B_TITLED_WINDOW:
		case B_UNTYPED_WINDOW:
		default:
			*_look = B_TITLED_WINDOW_LOOK;
			*_feel = B_NORMAL_WINDOW_FEEL;
			break;
	}
}


/**
 * @brief Creates and installs the window's top-level BView.
 *
 * Allocates a BView that covers the entire content area, marks it as a
 * top-level view, assigns this window as its owner, sets the last-view token,
 * and calls BView::_CreateSelf() to register it with the app_server.
 *
 * @note This must only be called once from _InitData().
 */
void
BWindow::_CreateTopView()
{
	STRACE(("_CreateTopView(): enter\n"));

	BRect frame = fFrame.OffsetToCopy(B_ORIGIN);
	// TODO: what to do here about std::nothrow?
	fTopView = new BView(frame, "fTopView", B_FOLLOW_ALL, B_WILL_DRAW);
	fTopView->fTopLevelView = true;

	//inhibit check_lock()
	fLastViewToken = _get_object_token_(fTopView);

	// set fTopView's owner, add it to window's eligible handler list
	// and also set its next handler to be this window.

	STRACE(("Calling setowner fTopView = %p this = %p.\n",
		fTopView, this));

	fTopView->_SetOwner(this);

	// we can't use AddChild() because this is the top view
	fTopView->_CreateSelf();
	STRACE(("BuildTopView ended\n"));
}


/**
 * @brief Resizes the top-level view (and its children) to match the current window frame.
 *
 * Computes the delta between the new frame dimensions and the top view's
 * current bounds, then calls BView::_ResizeBy() to propagate the change
 * through the view hierarchy.  This mirrors the resize logic performed
 * server-side, avoiding a round-trip to the app_server.
 *
 * Must be called whenever @c fFrame changes (i.e. after a move/resize reply).
 */
void
BWindow::_AdoptResize()
{
	// Resize views according to their resize modes - this
	// saves us some server communication, as the server
	// does the same with our views on its side.

	int32 deltaWidth = (int32)(fFrame.Width() - fTopView->Bounds().Width());
	int32 deltaHeight = (int32)(fFrame.Height() - fTopView->Bounds().Height());
	if (deltaWidth == 0 && deltaHeight == 0)
		return;

	fTopView->_ResizeBy(deltaWidth, deltaHeight);
}


/**
 * @brief Sets the keyboard focus to the specified view.
 *
 * Updates @c fFocus and the looper's preferred handler.  When @a notifyInputServer
 * is @c true and the window is currently active, notifies the input server
 * about the focus transition (used to toggle input-method awareness).
 *
 * @param focusView         The view to receive focus, or NULL to clear focus.
 * @param notifyInputServer @c true to inform the input server of the change.
 *
 * @see BWindow::CurrentFocus(), BView::MakeFocus()
 */
void
BWindow::_SetFocus(BView* focusView, bool notifyInputServer)
{
	if (fFocus == focusView)
		return;

	// we notify the input server if we are passing focus
	// from a view which has the B_INPUT_METHOD_AWARE to a one
	// which does not, or vice-versa
	if (notifyInputServer && fActive) {
		bool inputMethodAware = false;
		if (focusView)
			inputMethodAware = focusView->Flags() & B_INPUT_METHOD_AWARE;
		BMessage msg(inputMethodAware ? IS_FOCUS_IM_AWARE_VIEW : IS_UNFOCUS_IM_AWARE_VIEW);
		BMessenger messenger(focusView);
		BMessage reply;
		if (focusView)
			msg.AddMessenger("view", messenger);
		_control_input_server_(&msg, &reply);
	}

	fFocus = focusView;
	SetPreferredHandler(focusView);
}


/**
 * @brief Determines the correct target handler for an incoming message.
 *
 * For key events routes to the default button (on B_ENTER) or the focus view.
 * For mouse events reads the "_view_token" field and finds the view by token,
 * falling back to the last-mouse-moved view.  For B_PULSE and B_QUIT_REQUESTED
 * returns @c this (the window).  For dropped messages, uses the last-mouse-moved
 * view.  All other messages fall through to the supplied @a target.
 *
 * @param message The message for which a target is needed.
 * @param target  Suggested target, or NULL if none was embedded in the message.
 * @return The resolved target BHandler.
 */
BHandler*
BWindow::_DetermineTarget(BMessage* message, BHandler* target)
{
	if (target == NULL)
		target = this;

	switch (message->what) {
		case B_KEY_DOWN:
		case B_KEY_UP:
		{
			// if we have a default button, it might want to hear
			// about pressing the <enter> key
			BButton* defaultButton = DefaultButton();
			if (defaultButton != NULL) {
				int32 rawChar = message->GetInt32("raw_char", 0);
				uint32 mods = modifiers();
				if (rawChar == B_ENTER && (mods & Shortcut::AllowedModifiers()) == 0)
					return defaultButton;
			}
			// supposed to fall through
		}
		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
		case B_MODIFIERS_CHANGED:
			// these messages should be dispatched by the focus view
			if (CurrentFocus() != NULL)
				return CurrentFocus();
			break;

		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
		case B_MOUSE_MOVED:
		case B_MOUSE_WHEEL_CHANGED:
		case B_MOUSE_IDLE:
		{
			// is there a token of the view that is currently under the mouse?
			int32 token;
			if (message->FindInt32("_view_token", &token) == B_OK) {
				BView* view = _FindView(token);
				if (view != NULL)
					return view;
			}

			// if there is no valid token in the message, we try our
			// luck with the last target, if available
			if (fLastMouseMovedView != NULL)
				return fLastMouseMovedView;
			break;
		}

		case B_PULSE:
		case B_QUIT_REQUESTED:
			// TODO: test whether R5 will let BView dispatch these messages
			return this;

		case _MESSAGE_DROPPED_:
			if (fLastMouseMovedView != NULL)
				return fLastMouseMovedView;
			break;

		default:
			break;
	}

	return target;
}


/**
 * @brief Returns @c true if this message is directed at the focus view.
 *
 * A message targets the focus view when it was sent to the preferred handler
 * and either has no "_token" field (direct preferred target) or carries a
 * "_feed_focus" flag set to @c true.  Returns @c false if the message has an
 * explicit non-preferred target or if "_feed_focus" is present but @c false.
 *
 * @param message The message to test.
 * @return @c true if the message is intended for the focus view.
 */
bool
BWindow::_IsFocusMessage(BMessage* message)
{
	BMessage::Private messagePrivate(message);
	if (!messagePrivate.UsePreferredTarget())
		return false;

	bool feedFocus;
	if (message->HasInt32("_token")
		&& (message->FindBool("_feed_focus", &feedFocus) != B_OK || !feedFocus))
		return false;

	return true;
}


/**
 * @brief Iterates over all per-view targets embedded in a preferred-handler message.
 *
 * On the first call (cookie.index == 0) the cookie is initialised from the
 * message and the focus/last-mouse-moved tokens are extracted.  Subsequent
 * calls yield copies of the message for each additional "_token" recipient.
 * After all secondary targets have been served, the original message is
 * returned for delivery to the focus view (unless focus is no longer valid or
 * "_feed_focus" suppresses it).
 *
 * @param cookie        Persistent iteration state; initialised by the caller
 *                      before the first invocation.
 * @param _message      In/out: the message to dispatch; may be replaced with a
 *                      copy for secondary targets.
 * @param _target       In/out: the current target handler.
 * @param _usePreferred In/out: preferred-handler flag for the current iteration.
 * @return @c true while there are more targets to serve, @c false when done.
 *
 * @see BWindow::_SanitizeMessage(), BWindow::_IsFocusMessage()
 */
bool
BWindow::_UnpackMessage(unpack_cookie& cookie, BMessage** _message,
	BHandler** _target, bool* _usePreferred)
{
	if (cookie.message == NULL)
		return false;

	if (cookie.index == 0 && !cookie.tokens_scanned) {
		// We were called the first time for this message

		if (!*_usePreferred) {
			// only consider messages targeted at the preferred handler
			cookie.message = NULL;
			return true;
		}

		// initialize our cookie
		cookie.message = *_message;
		cookie.focus = *_target;

		if (cookie.focus != NULL)
			cookie.focus_token = _get_object_token_(*_target);

		if (fLastMouseMovedView != NULL && cookie.message->what == B_MOUSE_MOVED)
			cookie.last_view_token = _get_object_token_(fLastMouseMovedView);

		*_usePreferred = false;
	}

	_DequeueAll();

	// distribute the message to all targets specified in the
	// message directly (but not to the focus view)

	for (int32 token; !cookie.tokens_scanned
			&& cookie.message->FindInt32("_token", cookie.index, &token)
				== B_OK;
			cookie.index++) {
		// focus view is preferred and should get its message directly
		if (token == cookie.focus_token) {
			cookie.found_focus = true;
			continue;
		}
		if (token == cookie.last_view_token)
			continue;

		BView* target = _FindView(token);
		if (target == NULL)
			continue;

		*_message = new BMessage(*cookie.message);
		// the secondary copies of the message should not be treated as focus
		// messages, otherwise there will be unintended side effects, i.e.
		// keyboard shortcuts getting processed multiple times.
		(*_message)->RemoveName("_feed_focus");
		*_target = target;
		cookie.index++;
		return true;
	}

	cookie.tokens_scanned = true;

	// if there is a last mouse moved view, and the new focus is
	// different, the previous view wants to get its B_EXITED_VIEW
	// message
	if (cookie.last_view_token != B_NULL_TOKEN && fLastMouseMovedView != NULL
		&& fLastMouseMovedView != cookie.focus) {
		*_message = new BMessage(*cookie.message);
		*_target = fLastMouseMovedView;
		cookie.last_view_token = B_NULL_TOKEN;
		return true;
	}

	bool dispatchToFocus = true;

	// check if the focus token is still valid (could have been removed in the mean time)
	BHandler* handler;
	if (gDefaultTokens.GetToken(cookie.focus_token, B_HANDLER_TOKEN, (void**)&handler) != B_OK
		|| handler->Looper() != this)
		dispatchToFocus = false;

	if (dispatchToFocus && cookie.index > 0) {
		// should this message still be dispatched by the focus view?
		bool feedFocus;
		if (!cookie.found_focus
			&& (cookie.message->FindBool("_feed_focus", &feedFocus) != B_OK
				|| feedFocus == false))
			dispatchToFocus = false;
	}

	if (!dispatchToFocus) {
		delete cookie.message;
		cookie.message = NULL;
		return false;
	}

	*_message = cookie.message;
	*_target = cookie.focus;
	*_usePreferred = true;
	cookie.message = NULL;
	return true;
}


/**
 * @brief Post-processes a message before it is dispatched to its target.
 *
 * For mouse-related messages, adds view-local and window-local coordinate
 * fields ("where", "be:view_where") computed from the "screen_where" field,
 * and — for B_MOUSE_MOVED — adds the "be:transit" field and updates
 * @c fLastMouseMovedView when dispatching to the preferred handler.
 * For B_MOUSE_IDLE, adds the "be:view_where" field.
 * For _MESSAGE_DROPPED_, restores the original "what" code.
 *
 * @param message     The message to sanitize (modified in place).
 * @param target      The target handler that will receive the message.
 * @param usePreferred @c true if the message is being dispatched to the
 *                    preferred (focus) handler.
 */
void
BWindow::_SanitizeMessage(BMessage* message, BHandler* target, bool usePreferred)
{
	if (target == NULL)
		return;

	switch (message->what) {
		case B_MOUSE_MOVED:
		case B_MOUSE_UP:
		case B_MOUSE_DOWN:
		{
			BPoint where;
			if (message->FindPoint("screen_where", &where) != B_OK)
				break;

			BView* view = dynamic_cast<BView*>(target);

			if (view == NULL || message->what == B_MOUSE_MOVED) {
				// add local window coordinates, only
				// for regular mouse moved messages
				message->AddPoint("where", ConvertFromScreen(where));
			}

			if (view != NULL) {
				// add local view coordinates
				BPoint viewWhere = view->ConvertFromScreen(where);
				if (message->what != B_MOUSE_MOVED) {
					// Yep, the meaning of "where" is different
					// for regular mouse moved messages versus
					// mouse up/down!
					message->AddPoint("where", viewWhere);
				}
				message->AddPoint("be:view_where", viewWhere);

				if (message->what == B_MOUSE_MOVED) {
					// is there a token of the view that is currently under
					// the mouse?
					BView* viewUnderMouse = NULL;
					int32 token;
					if (message->FindInt32("_view_token", &token) == B_OK)
						viewUnderMouse = _FindView(token);

					// add transit information
					uint32 transit
						= _TransitForMouseMoved(view, viewUnderMouse);
					message->AddInt32("be:transit", transit);

					if (usePreferred)
						fLastMouseMovedView = viewUnderMouse;
				}
			}
			break;
		}

		case B_MOUSE_IDLE:
		{
			// App Server sends screen coordinates, convert the point to
			// local view coordinates, then add the point in be:view_where
			BPoint where;
			if (message->FindPoint("screen_where", &where) != B_OK)
				break;

			BView* view = dynamic_cast<BView*>(target);
			if (view != NULL) {
				// add local view coordinates
				message->AddPoint("be:view_where",
					view->ConvertFromScreen(where));
			}
			break;
		}

		case _MESSAGE_DROPPED_:
		{
			uint32 originalWhat;
			if (message->FindInt32("_original_what",
					(int32*)&originalWhat) == B_OK) {
				message->what = originalWhat;
				message->RemoveName("_original_what");
			}
			break;
		}
	}
}


/**
 * @brief Intercepts a B_MOUSE_MOVED message on behalf of BView::GetMouse().
 *
 * Called while the message queue lock is held.  Determines whether the message
 * can be consumed by GetMouse(): if the message has multiple targets (secondary
 * "_token" views), the "_feed_focus" flag is stripped and @a deleteMessage is
 * set to @c false; otherwise the message is removed from the queue and
 * @a deleteMessage is set to @c true.  B_ENTERED_VIEW / B_EXITED_VIEW transit
 * messages are never deleted.
 *
 * @param message       The B_MOUSE_MOVED message candidate.
 * @param deleteMessage Output: @c true if the caller should delete the message.
 * @return @c true if the message is usable by GetMouse(), @c false if it should
 *         not be consumed (e.g. has an explicit non-preferred target).
 */
bool
BWindow::_StealMouseMessage(BMessage* message, bool& deleteMessage)
{
	BMessage::Private messagePrivate(message);
	if (!messagePrivate.UsePreferredTarget()) {
		// this message is targeted at a specific handler, so we should
		// not steal it
		return false;
	}

	int32 token;
	if (message->FindInt32("_token", 0, &token) == B_OK) {
		// This message has other targets, so we can't remove it;
		// just prevent it from being sent to the preferred handler
		// again (if it should have gotten it at all).
		bool feedFocus;
		if (message->FindBool("_feed_focus", &feedFocus) != B_OK || !feedFocus)
			return false;

		message->RemoveName("_feed_focus");
		deleteMessage = false;
	} else {
		deleteMessage = true;

		if (message->what == B_MOUSE_MOVED) {
			// We need to update the last mouse moved view, as this message
			// won't make it to _SanitizeMessage() anymore.
			BView* viewUnderMouse = NULL;
			int32 token;
			if (message->FindInt32("_view_token", &token) == B_OK)
				viewUnderMouse = _FindView(token);

			// Don't remove important transit messages!
			uint32 transit = _TransitForMouseMoved(fLastMouseMovedView,
				viewUnderMouse);
			if (transit == B_ENTERED_VIEW || transit == B_EXITED_VIEW)
				deleteMessage = false;
		}

		if (deleteMessage) {
			// The message is only thought for the preferred handler, so we
			// can just remove it.
			MessageQueue()->RemoveMessage(message);
		}
	}

	return true;
}


/**
 * @brief Computes the mouse-transit constant for a B_MOUSE_MOVED event.
 *
 * Compares the @a view receiving the event with the view currently under the
 * mouse (@a viewUnderMouse) and the previous last-mouse-moved view to determine
 * whether the pointer entered, exited, or remained inside/outside the view.
 *
 * @param view           The view for which transit is being computed.
 * @param viewUnderMouse The view currently under the mouse pointer.
 * @return One of B_ENTERED_VIEW, B_EXITED_VIEW, B_INSIDE_VIEW, or
 *         B_OUTSIDE_VIEW.
 */
uint32
BWindow::_TransitForMouseMoved(BView* view, BView* viewUnderMouse) const
{
	uint32 transit;
	if (viewUnderMouse == view) {
		// the mouse is over the target view
		if (fLastMouseMovedView != view)
			transit = B_ENTERED_VIEW;
		else
			transit = B_INSIDE_VIEW;
	} else {
		// the mouse is not over the target view
		if (view == fLastMouseMovedView)
			transit = B_EXITED_VIEW;
		else
			transit = B_OUTSIDE_VIEW;
	}
	return transit;
}


/**
 * @brief Forwards a keyboard shortcut to the Deskbar's application switcher.
 *
 * Repeating key events are silently ignored (only the first press is sent).
 * Does nothing if the Deskbar is not running.
 *
 * @param rawKey   The raw key code from the key event.
 * @param modifiers The modifier bitmask at the time of the key press.
 * @param repeat   @c true if this is a key-repeat event.
 */
void
BWindow::_Switcher(int32 rawKey, uint32 modifiers, bool repeat)
{
	// only send the first key press, no repeats
	if (repeat)
		return;

	BMessenger deskbar(kDeskbarSignature);
	if (!deskbar.IsValid()) {
		// TODO: have some kind of fallback-handling in case the Deskbar is
		// not available?
		return;
	}

	BMessage message('TASK');
	message.AddInt32("key", rawKey);
	message.AddInt32("modifiers", modifiers);
	message.AddInt64("when", system_time());
	message.AddInt32("team", Team());
	deskbar.SendMessage(&message);
}


/**
 * @brief Pre-processes a B_KEY_DOWN message before it reaches the target view.
 *
 * Evaluates the following in order:
 * - Command+Escape to open the key menu bar.
 * - Tab with B_OPTION_KEY for keyboard navigation.
 * - Tab or raw-key 0x11 with B_CONTROL_KEY for the Deskbar Switcher.
 * - Escape with B_CLOSE_ON_ESCAPE flag to close the window.
 * - Print-Screen key combinations to take a screenshot.
 * - Command+Q to post B_QUIT_REQUESTED to the application.
 * - Command+Left/Right Arrow forwarded to a focused BTextView.
 * - Registered keyboard shortcuts (after calling MenusBeginning/MenusEnded).
 * - Any remaining Command-modified key (consumed without forwarding).
 *
 * Only processes the message if it was targeted at the focus view.
 *
 * @param event The B_KEY_DOWN BMessage to evaluate.
 * @return @c true if the event was consumed (not forwarded to the target).
 */
bool
BWindow::_HandleKeyDown(BMessage* event)
{
	// Only handle special functions when the event targeted the active focus
	// view
	if (!_IsFocusMessage(event))
		return false;

	const char* bytes;
	if (event->FindString("bytes", &bytes) != B_OK)
		return false;

	char key = Shortcut::PrepareKey(bytes[0]);

	uint32 modifiers;
	if (event->FindInt32("modifiers", (int32*)&modifiers) != B_OK)
		modifiers = 0;

	uint32 rawKey;
	if (event->FindInt32("key", (int32*)&rawKey) != B_OK)
		rawKey = 0;

	// handle BMenuBar key
	if (key == B_ESCAPE && (modifiers & B_COMMAND_KEY) != 0 && fKeyMenuBar != NULL) {
		fKeyMenuBar->StartMenuBar(0, true, false, NULL);
		return true;
	}

	// Keyboard navigation through views
	// (B_OPTION_KEY makes BTextViews and friends navigable, even in editing
	// mode)
	if (key == B_TAB && (modifiers & B_OPTION_KEY) != 0) {
		_KeyboardNavigation();
		return true;
	}

	// Deskbar's Switcher
	if ((key == B_TAB || rawKey == 0x11) && (modifiers & B_CONTROL_KEY) != 0) {
		_Switcher(rawKey, modifiers, event->HasInt32("be:key_repeat"));
		return true;
	}

	// Optionally close window when the escape key is pressed
	if (key == B_ESCAPE && (Flags() & B_CLOSE_ON_ESCAPE) != 0) {
		BMessage message(B_QUIT_REQUESTED);
		message.AddBool("shortcut", true);
		PostMessage(&message);
		return true;
	}

	// PrtScr key takes a screenshot
	if (key == B_FUNCTION_KEY && rawKey == B_PRINT_KEY) {
		// With no modifier keys the best way to get a screenshot is by
		// calling the screenshot CLI
		if (modifiers == 0) {
			be_roster->Launch("application/x-vnd.haiku-screenshot-cli");
			return true;
		}

		// If option is held, then launch the area selector via CLI
		if ((modifiers & B_OPTION_KEY) != 0) {
			BMessage message(B_ARGV_RECEIVED);
			message.AddString("argv", "screenshot");
			message.AddString("argv", "--area");
			message.AddInt32("argc", 2);
			be_roster->Launch("application/x-vnd.haiku-screenshot-cli", &message);
			return true;
		}

		// Prepare a message based on the modifier keys pressed and launch the
		// screenshot GUI
		BMessage message(B_ARGV_RECEIVED);
		int32 argc = 1;
		message.AddString("argv", "Screenshot");
		if ((modifiers & B_CONTROL_KEY) != 0) {
			argc++;
			message.AddString("argv", "--clipboard");
		}
		if ((modifiers & B_SHIFT_KEY) != 0) {
			argc++;
			message.AddString("argv", "--silent");
		}
		message.AddInt32("argc", argc);
		be_roster->Launch("application/x-vnd.haiku-screenshot", &message);
		return true;
	}

	// Special handling for Command+q, Command+Left, Command+Right
	if ((modifiers & B_COMMAND_KEY) != 0) {
		// Command+q has been pressed, so, we will quit
		// the shortcut mechanism doesn't allow handlers outside the window
		if (!fNoQuitShortcut && key == 'Q') {
			BMessage message(B_QUIT_REQUESTED);
			message.AddBool("shortcut", true);
			be_app->PostMessage(&message);
			return true;
		}

		// Send Command+Left and Command+Right to textview if it has focus
		if (key == B_LEFT_ARROW || key == B_RIGHT_ARROW) {
			// check key before doing expensive dynamic_cast
			BTextView* textView = dynamic_cast<BTextView*>(CurrentFocus());
			if (textView != NULL) {
				textView->KeyDown(bytes, modifiers);
				return true;
			}
		}
	}

	// Handle shortcuts
	{
		// Pretend that the user opened a menu, to give the subclass a
		// chance to update its menus. This may install new shortcuts,
		// which is why we have to call it here, before trying to find
		// a shortcut for the given key.
		MenusBeginning();

		Shortcut* shortcut = _FindShortcut(key, modifiers
			| (((modifiers & B_COMMAND_KEY) == 0) ? B_NO_COMMAND_KEY : 0));
		if (shortcut != NULL) {
			// TODO: would be nice to move this functionality to
			//	a Shortcut::Invoke() method - but since BMenu::InvokeItem()
			//	(and BMenuItem::Invoke()) are private, I didn't want
			//	to mess with them (BMenuItem::Invoke() is public in
			//	Dano/Zeta, though, maybe we should just follow their
			//	example)
			if (shortcut->MenuItem() != NULL) {
				BMenu* menu = shortcut->MenuItem()->Menu();
				if (menu != NULL && shortcut->MenuItem()->IsEnabled()) {
					MenuPrivate(menu).InvokeItem(shortcut->MenuItem(), true);
				} else {
					// Process disabled shortcuts as if they did not exist.
					// (This lets B_NO_COMMAND_KEY shortcuts fall back to regular key events.)
					shortcut = NULL;
				}
			} else {
				BHandler* target = shortcut->Target();
				if (target == NULL)
					target = CurrentFocus();

				if (shortcut->Message() != NULL) {
					BMessage message(*shortcut->Message());
					if (message.ReplaceInt64("when", system_time()) != B_OK)
						message.AddInt64("when", system_time());
					if (message.ReplaceBool("shortcut", true) != B_OK)
						message.AddBool("shortcut", true);
					PostMessage(&message, target);
				}
			}
		}

		MenusEnded();

		if (shortcut != NULL)
			return true;
	}

	if ((modifiers & B_COMMAND_KEY) != 0) {
		// we always eat the event if the command key was pressed
		return true;
	}

	// TODO: convert keys to the encoding of the target view

	return false;
}


/**
 * @brief Pre-processes a B_UNMAPPED_KEY_DOWN message before it reaches the target.
 *
 * Handles the special case of raw key 0x11 (Tab-equivalent) with B_CONTROL_KEY
 * for the Deskbar Switcher, which is the only unmapped key requiring window-level
 * handling.  Only processes messages targeted at the focus view.
 *
 * @param event The B_UNMAPPED_KEY_DOWN BMessage to evaluate.
 * @return @c true if the event was consumed, @c false to forward it.
 */
bool
BWindow::_HandleUnmappedKeyDown(BMessage* event)
{
	// Only handle special functions when the event targeted the active focus
	// view
	if (!_IsFocusMessage(event))
		return false;

	uint32 modifiers;
	int32 rawKey;
	if (event->FindInt32("modifiers", (int32*)&modifiers) != B_OK
		|| event->FindInt32("key", &rawKey))
		return false;

	// Deskbar's Switcher
	if (rawKey == 0x11 && (modifiers & B_CONTROL_KEY) != 0) {
		_Switcher(rawKey, modifiers, event->HasInt32("be:key_repeat"));
		return true;
	}

	return false;
}


/**
 * @brief Implements Tab-key focus navigation between views.
 *
 * Reads the current B_KEY_DOWN message and moves focus to the next or previous
 * navigable view (B_NAVIGABLE), or to the next navigable group (B_NAVIGABLE_JUMP)
 * when B_OPTION_KEY is held.  Shift reverses the direction.
 *
 * Called from DispatchMessage() for B_KEY_DOWN (when navigation is needed) and
 * from MessageReceived() for subsequent B_KEY_DOWN messages.
 */
void
BWindow::_KeyboardNavigation()
{
	BMessage* message = CurrentMessage();
	if (message == NULL)
		return;

	const char* bytes;
	if (message->FindString("bytes", &bytes) != B_OK || bytes[0] != B_TAB)
		return;

	uint32 modifiers;
	if (message->FindInt32("modifiers", (int32*)&modifiers) != B_OK)
		modifiers = 0;

	BView* nextFocus;
	int32 jumpGroups = (modifiers & B_OPTION_KEY) != 0 ? B_NAVIGABLE_JUMP : B_NAVIGABLE;
	if ((modifiers & B_SHIFT_KEY) != 0)
		nextFocus = _FindPreviousNavigable(fFocus, jumpGroups);
	else
		nextFocus = _FindNextNavigable(fFocus, jumpGroups);

	if (nextFocus != NULL && nextFocus != fFocus)
		nextFocus->MakeFocus(true);
}


/**
 * @brief Returns the ideal position for an alert dialog relative to @a frame.
 *
 * Centres the window horizontally and places it approximately 3/4 of the way
 * down from the top of @a frame.  Clamps the result to remain within screen
 * bounds and to not be obscured by another window's title tab.
 *
 * @param frame The reference rectangle (typically the screen or a parent window frame).
 * @return The recommended top-left corner position for the alert window.
 */
BPoint
BWindow::AlertPosition(const BRect& frame)
{
	float width = Bounds().Width();
	float height = Bounds().Height();

	BPoint point(frame.left + (frame.Width() / 2.0f) - (width / 2.0f),
		frame.top + (frame.Height() / 4.0f) - ceil(height / 3.0f));

	BRect screenFrame = BScreen(this).Frame();
	if (frame == screenFrame) {
		// reference frame is screen frame, skip the below adjustments
		return point;
	}

	float borderWidth;
	float tabHeight;
	_GetDecoratorSize(&borderWidth, &tabHeight);

	// clip the x position within the horizontal edges of the screen
	if (point.x < screenFrame.left + borderWidth)
		point.x = screenFrame.left + borderWidth;
	else if (point.x + width > screenFrame.right - borderWidth)
		point.x = screenFrame.right - borderWidth - width;

	// lower the window down if it is covering the window tab
	float tabPosition = frame.LeftTop().y + tabHeight + borderWidth;
	if (point.y < tabPosition)
		point.y = tabPosition;

	// clip the y position within the vertical edges of the screen
	if (point.y < screenFrame.top + borderWidth)
		point.y = screenFrame.top + borderWidth;
	else if (point.y + height > screenFrame.bottom - borderWidth)
		point.y = screenFrame.bottom - borderWidth - height;

	return point;
}


/**
 * @brief Converts a raw port message into a BMessage.
 *
 * Delegates directly to BLooper::ConvertToMessage().  Present to allow
 * subclasses to override the raw-message conversion step.
 *
 * @param raw  Pointer to the raw message data read from the port.
 * @param code The message code identifying the message type.
 * @return A newly allocated BMessage, or NULL on failure.
 */
BMessage*
BWindow::ConvertToMessage(void* raw, int32 code)
{
	return BLooper::ConvertToMessage(raw, code);
}


/**
 * @brief Searches the shortcut list for a matching key and modifier combination.
 *
 * Prepares the key and modifiers before searching so the comparison is
 * normalised.
 *
 * @param key       Unicode code point for the shortcut key.
 * @param modifiers Modifier bitmask (raw; will be prepared internally).
 * @return The matching Shortcut, or NULL if none is found.
 */
BWindow::Shortcut*
BWindow::_FindShortcut(uint32 key, uint32 modifiers)
{
	key = Shortcut::PrepareKey(key);
	uint32 preparedModifiers = Shortcut::PrepareModifiers(modifiers);

	int32 shortcutCount = fShortcuts.CountItems();
	for (int32 index = 0; index < shortcutCount; index++) {
		Shortcut* shortcut = (Shortcut*)fShortcuts.ItemAt(index);
		if (shortcut != NULL && shortcut->Matches(key, preparedModifiers))
			return shortcut;
	}

	return NULL;
}


/**
 * @brief Finds a view in this window by its server-side token.
 *
 * Looks up the token in the global token space and verifies that the found
 * handler is a BView belonging to this window.
 *
 * @param token The server-side handler token.
 * @return The matching BView, or NULL if not found or not owned by this window.
 */
BView*
BWindow::_FindView(int32 token)
{
	BHandler* handler;
	if (gDefaultTokens.GetToken(token, B_HANDLER_TOKEN,
			(void**)&handler) != B_OK) {
		return NULL;
	}

	// the view must belong to us in order to be found by this method
	BView* view = dynamic_cast<BView*>(handler);
	if (view != NULL && view->Window() == this)
		return view;

	return NULL;
}


/**
 * @brief Recursively finds the deepest visible view that contains @a point.
 *
 * @a point must be in @a view's local coordinate system.  Hidden views are
 * skipped.  If no child contains the point, @a view itself is returned.
 *
 * @param view  The root of the view sub-tree to search.
 * @param point A point in @a view's local coordinate system.
 * @return The deepest visible view containing @a point, or NULL if @a view
 *         itself does not contain it.
 */
BView*
BWindow::_FindView(BView* view, BPoint point) const
{
	// point is assumed to be already in view's coordinates
	if (!view->IsHidden(view) && view->Bounds().Contains(point)) {
		if (view->fFirstChild == NULL)
			return view;
		else {
			BView* child = view->fFirstChild;
			while (child != NULL) {
				BPoint childPoint = point - child->Frame().LeftTop();
				BView* subView  = _FindView(child, childPoint);
				if (subView != NULL)
					return subView;

				child = child->fNextSibling;
			}
		}
		return view;
	}
	return NULL;
}


/**
 * @brief Finds the next view after @a focus that has the given navigability flags.
 *
 * Performs a depth-first forward traversal of the view tree, wrapping around
 * at the top view.  Returns NULL if no navigable view is found.
 *
 * @param focus The currently focused view, or NULL to start from the top view.
 * @param flags Navigability flags to match (B_NAVIGABLE or B_NAVIGABLE_JUMP).
 * @return The next navigable view, or NULL if none exists.
 *
 * @see BWindow::_FindPreviousNavigable()
 */
BView*
BWindow::_FindNextNavigable(BView* focus, uint32 flags)
{
	if (focus == NULL)
		focus = fTopView;

	BView* nextFocus = focus;

	// Search the tree for views that accept focus (depth search)
	while (true) {
		if (nextFocus->fFirstChild)
			nextFocus = nextFocus->fFirstChild;
		else if (nextFocus->fNextSibling)
			nextFocus = nextFocus->fNextSibling;
		else {
			// go to the nearest parent with a next sibling
			while (!nextFocus->fNextSibling && nextFocus->fParent) {
				nextFocus = nextFocus->fParent;
			}

			if (nextFocus == fTopView) {
				// if we started with the top view, we traversed the whole tree already
				if (nextFocus == focus)
					return NULL;

				nextFocus = nextFocus->fFirstChild;
			} else
				nextFocus = nextFocus->fNextSibling;
		}

		if (nextFocus == focus || nextFocus == NULL) {
			// When we get here it means that the hole tree has been
			// searched and there is no view with B_NAVIGABLE(_JUMP) flag set!
			return NULL;
		}

		if (!nextFocus->IsHidden() && (nextFocus->Flags() & flags) != 0)
			return nextFocus;
	}
}


/**
 * @brief Finds the previous view before @a focus that has the given navigability flags.
 *
 * Performs a reverse depth-first traversal of the view tree using
 * _LastViewChild() to walk backwards through sibling chains.  Returns NULL if
 * no navigable view is found.
 *
 * @param focus The currently focused view, or NULL to start from the top view.
 * @param flags Navigability flags to match (B_NAVIGABLE or B_NAVIGABLE_JUMP).
 * @return The previous navigable view, or NULL if none exists.
 *
 * @see BWindow::_FindNextNavigable()
 */
BView*
BWindow::_FindPreviousNavigable(BView* focus, uint32 flags)
{
	if (focus == NULL)
		focus = fTopView;

	BView* previousFocus = focus;

	// Search the tree for the previous view that accept focus
	while (true) {
		if (previousFocus->fPreviousSibling) {
			// find the last child in the previous sibling
			previousFocus = _LastViewChild(previousFocus->fPreviousSibling);
		} else {
			previousFocus = previousFocus->fParent;
			if (previousFocus == fTopView)
				previousFocus = _LastViewChild(fTopView);
		}

		if (previousFocus == focus || previousFocus == NULL) {
			// When we get here it means that the hole tree has been
			// searched and there is no view with B_NAVIGABLE(_JUMP) flag set!
			return NULL;
		}

		if (!previousFocus->IsHidden() && (previousFocus->Flags() & flags) != 0)
			return previousFocus;
	}
}


/**
 * @brief Returns the deepest last-child descendant of @a parent.
 *
 * Walks the first-child chain and then the sibling chain at each level until
 * the very last leaf view is reached.  Used exclusively by
 * _FindPreviousNavigable() to implement reverse view-tree traversal.
 *
 * @param parent The view whose last descendant is desired.
 * @return The deepest last-child descendant, which may be @a parent itself if
 *         it has no children.
 */
BView*
BWindow::_LastViewChild(BView* parent)
{
	while (true) {
		BView* last = parent->fFirstChild;
		if (last == NULL)
			return parent;

		while (last->fNextSibling) {
			last = last->fNextSibling;
		}

		parent = last;
	}
}


/**
 * @brief Marks or unmarks this window as a file panel.
 *
 * File-panel windows receive special treatment from the system (e.g.
 * they are excluded from the application's window list in the Deskbar).
 *
 * @param isFilePanel @c true to mark as a file panel, @c false to clear it.
 *
 * @see BWindow::IsFilePanel()
 */
void
BWindow::SetIsFilePanel(bool isFilePanel)
{
	fIsFilePanel = isFilePanel;
}


/**
 * @brief Returns @c true if this window has been marked as a file panel.
 *
 * @return @c true if the window is a file panel.
 *
 * @see BWindow::SetIsFilePanel()
 */
bool
BWindow::IsFilePanel() const
{
	return fIsFilePanel;
}


/**
 * @brief Retrieves the decorator's border width and tab height.
 *
 * Queries the decorator settings from the app_server.  Falls back to
 * reasonable defaults (5 px border, 21 px tab) if the query fails, and
 * adjusts to 0 for B_NO_BORDER_WINDOW_LOOK windows.
 *
 * @param _borderWidth Output: border width in pixels, or NULL.
 * @param _tabHeight   Output: title-tab height in pixels, or NULL.
 */
void
BWindow::_GetDecoratorSize(float* _borderWidth, float* _tabHeight) const
{
	// fallback in case retrieving the decorator settings fails
	// (highly unlikely)
	float borderWidth = 5.0;
	float tabHeight = 21.0;

	BMessage settings;
	if (GetDecoratorSettings(&settings) == B_OK) {
		BRect tabRect;
		if (settings.FindRect("tab frame", &tabRect) == B_OK)
			tabHeight = tabRect.Height();
		settings.FindFloat("border width", &borderWidth);
	} else {
		// probably no-border window look
		if (fLook == B_NO_BORDER_WINDOW_LOOK) {
			borderWidth = 0.0;
			tabHeight = 0.0;
		}
		// else use fall-back values from above
	}

	if (_borderWidth != NULL)
		*_borderWidth = borderWidth;
	if (_tabHeight != NULL)
		*_tabHeight = tabHeight;
}


/**
 * @brief Sends the current show-level to the app_server.
 *
 * Sends AS_SHOW_OR_HIDE_WINDOW with @c fShowLevel so the server can
 * show or hide the on-screen window accordingly.  Called by Show() and Hide().
 */
void
BWindow::_SendShowOrHideMessage()
{
	fLink->StartMessage(AS_SHOW_OR_HIDE_WINDOW);
	fLink->Attach<int32>(fShowLevel);
	fLink->Flush();
}


/**
 * @brief Posts a copy of @a message to each direct child view of the window.
 *
 * Used to propagate system-wide notifications such as B_SCREEN_CHANGED,
 * B_WORKSPACE_ACTIVATED, and B_WORKSPACES_CHANGED to all top-level child views
 * before invoking the corresponding window hook.
 *
 * @param message The message to forward to each child view.
 */
void
BWindow::_PropagateMessageToChildViews(BMessage* message)
{
	int32 childrenCount = CountChildren();
	for (int32 index = 0; index < childrenCount; index++) {
		BView* view = ChildAt(index);
		if (view != NULL)
			PostMessage(message, view);
	}
}


//	#pragma mark - C++ binary compatibility kludge


/**
 * @brief Binary-compatibility trampoline for BWindow::SetLayout().
 *
 * Exported as a C symbol so that code compiled against an older version of
 * libbe (which had SetLayout in the vtable as _ReservedWindow1) can still
 * call the current SetLayout() implementation via Perform().
 *
 * @param window The BWindow on which to call SetLayout.
 * @param layout The BLayout to install.
 */
extern "C" void
_ReservedWindow1__7BWindow(BWindow* window, BLayout* layout)
{
	// SetLayout()
	perform_data_set_layout data;
	data.layout = layout;
	window->Perform(PERFORM_CODE_SET_LAYOUT, &data);
}


/** @brief Reserved virtual slot 2 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow2() {}
/** @brief Reserved virtual slot 3 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow3() {}
/** @brief Reserved virtual slot 4 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow4() {}
/** @brief Reserved virtual slot 5 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow5() {}
/** @brief Reserved virtual slot 6 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow6() {}
/** @brief Reserved virtual slot 7 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow7() {}
/** @brief Reserved virtual slot 8 — not yet used; provided for ABI stability. */
void BWindow::_ReservedWindow8() {}

