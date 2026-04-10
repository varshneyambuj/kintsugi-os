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
 *       Stefano Ceccherini, stefano.ceccherini@gmail.com
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 *       Marc Flerackers, mflerackers@androme.be
 *       Rene Gollent, anevilyak@gmail.com
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Menu.cpp
 * @brief Implementation of BMenu, the core menu class
 *
 * BMenu manages a list of BMenuItem objects and presents them in a pop-up or
 * embedded menu. It handles keyboard navigation, item tracking,
 * radio-button-style exclusion, and hierarchical submenus. BMenuBar,
 * BPopUpMenu, and BMenuField all build on BMenu.
 *
 * @see BMenuItem, BMenuBar, BPopUpMenu, BMenuField
 */


#include <Menu.h>

#include <algorithm>
#include <new>
#include <set>

#include <ctype.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <Layout.h>
#include <LayoutUtils.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <SystemCatalog.h>
#include <UnicodeChar.h>
#include <Window.h>

#include <AppServerLink.h>
#include <AutoDeleter.h>
#include <binary_compatibility/Interface.h>
#include <BMCPrivate.h>
#include <MenuPrivate.h>
#include <MenuWindow.h>
#include <ServerProtocol.h>

#include "utf8_functions.h"


using BPrivate::gSystemCatalog;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Menu"

#undef B_TRANSLATE
#define B_TRANSLATE(str) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK(str), "Menu")


using std::nothrow;
using BPrivate::BMenuWindow;

namespace BPrivate {

/**
 * @brief Tracks the set of keyboard trigger characters already claimed by
 *        items in a menu, preventing duplicate automatic trigger assignments.
 *
 * Each character is stored in its lowercase canonical form so that trigger
 * matching is case-insensitive.
 */
class TriggerList {
public:
	/** @brief Construct an empty trigger list. */
	TriggerList() {}
	/** @brief Destroy the trigger list. */
	~TriggerList() {}

	/**
	 * @brief Check whether a character is already registered as a trigger.
	 * @param c Unicode code point to test (case-insensitive).
	 * @return true if the character (or its lowercase equivalent) is already
	 *         in the list, false otherwise.
	 */
	bool HasTrigger(uint32 c)
		{ return fList.find(BUnicodeChar::ToLower(c)) != fList.end(); }

	/**
	 * @brief Register a character as a used trigger.
	 * @param c Unicode code point to add (stored as lowercase).
	 * @return Always true.
	 */
	bool AddTrigger(uint32 c)
	{
		fList.insert(BUnicodeChar::ToLower(c));
		return true;
	}

private:
	/** @brief Set of lowercase Unicode code points that are already triggers. */
	std::set<uint32> fList;
};


/**
 * @brief Auxiliary per-menu data that extends BMenu without changing its ABI.
 *
 * Stores the optional custom tracking hook and the flag that records whether
 * the menu window was shifted left to stay on screen, so that child submenus
 * can open in the same direction.
 */
class ExtraMenuData {
public:
	/** @brief Optional callback invoked each tracking iteration to allow
	 *         the caller to abort tracking. */
	menu_tracking_hook	trackingHook;
	/** @brief Opaque context pointer passed to @c trackingHook. */
	void*				trackingState;

	/** @brief True when the menu window was nudged leftward to stay on screen;
	 *         used so submenus open in the correct direction. */
	bool				frameShiftedLeft;

	/** @brief Construct ExtraMenuData with all fields zeroed/false. */
	ExtraMenuData()
	{
		trackingHook = NULL;
		trackingState = NULL;
		frameShiftedLeft = false;
	}
};


/** @brief Signature of the comparator function accepted by BMenu::SortItems(). */
typedef int (*compare_func)(const BMenuItem*, const BMenuItem*);

/**
 * @brief Strict-weak-ordering functor that wraps a ::compare_func so that
 *        it can be passed to std::stable_sort().
 */
struct MenuItemComparator
{
	/**
	 * @brief Construct a comparator from a raw comparison function.
	 * @param compareFunc Function returning negative/zero/positive like strcmp.
	 */
	MenuItemComparator(compare_func compareFunc)
		:
		fCompareFunc(compareFunc)
	{
	}

	/**
	 * @brief Return true when item1 should sort before item2.
	 * @param item1 Left-hand item.
	 * @param item2 Right-hand item.
	 * @return true if compareFunc(item1, item2) < 0.
	 */
	bool operator () (const BMenuItem* item1, const BMenuItem* item2) {
		return fCompareFunc(item1, item2) < 0;
	}

private:
	/** @brief Underlying raw comparison function. */
	compare_func fCompareFunc;
};


}	// namespace BPrivate


/** @brief Cached system-wide menu settings shared by all BMenu instances. */
menu_info BMenu::sMenuInfo;

/** @brief Key code of the left Shift key, read once in AttachedToWindow(). */
uint32 BMenu::sShiftKey;
/** @brief Key code of the left Control key, read once in AttachedToWindow(). */
uint32 BMenu::sControlKey;
/** @brief Key code of the left Option key, read once in AttachedToWindow(). */
uint32 BMenu::sOptionKey;
/** @brief Key code of the left Command key, read once in AttachedToWindow(). */
uint32 BMenu::sCommandKey;
/** @brief Key code of the Menu key, read once in AttachedToWindow(). */
uint32 BMenu::sMenuKey;

/** @brief Scripting property table describing BMenu's supported properties. */
static property_info sPropList[] = {
	{ "Enabled", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if menu or menu item is "
		"enabled; false otherwise.",
		0, { B_BOOL_TYPE }
	},

	{ "Enabled", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Enables or disables menu or menu item.",
		0, { B_BOOL_TYPE }
	},

	{ "Label", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the string label of the menu or "
		"menu item.",
		0, { B_STRING_TYPE }
	},

	{ "Label", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the string label of the menu or menu "
		"item.",
		0, { B_STRING_TYPE }
	},

	{ "Mark", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if the menu item or the "
		"menu's superitem is marked; false otherwise.",
		0, { B_BOOL_TYPE }
	},

	{ "Mark", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Marks or unmarks the menu item or the "
		"menu's superitem.",
		0, { B_BOOL_TYPE }
	},

	{ "Menu", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label "
		"found in \"data\" and the int32 command found in \"what\" (used as "
		"the what field in the BMessage sent by the item)." , 0, {},
		{ 	{{{"data", B_STRING_TYPE}}}
		}
	},

	{ "Menu", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the selected menu or menus.", 0, {}
	},

	{ "Menu", { },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the "
		"current specifier off the stack.", 0, {}
	},

	{ "MenuItem", { B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Counts the number of menu items in the "
		"specified menu.",
		0, { B_INT32_TYPE }
	},

	{ "MenuItem", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label "
		"found in \"data\" and the int32 command found in \"what\" (used as "
		"the what field in the BMessage sent by the item).", 0, {},
		{	{ {{"data", B_STRING_TYPE },
			{"be:invoke_message", B_MESSAGE_TYPE},
			{"what", B_INT32_TYPE},
			{"be:target", B_MESSENGER_TYPE}} }
		}
	},

	{ "MenuItem", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the specified menu item from its parent menu."
	},

	{ "MenuItem", { B_EXECUTE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Invokes the specified menu item."
	},

	{ "MenuItem", { },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the "
		"current specifier off the stack."
	},

	{ 0 }
};


/** @brief Fallback label shown when a menu has no items; overwritten with a
 *         localized string in BMenu::_InitData(). */
const char* BPrivate::kEmptyMenuLabel = "<empty>";


/**
 * @brief Internal layout cache for BMenu, storing the preferred size and the
 *        resizing mode that was in effect when it was last computed.
 */
struct BMenu::LayoutData {
	/** @brief Preferred size computed by the last _ComputeLayout() call. */
	BSize	preferred;
	/** @brief ResizingMode() at the time @c preferred was calculated. */
	uint32	lastResizingMode;
};


// #pragma mark - BMenu


/**
 * @brief Construct a BMenu with a name and an item layout.
 *
 * Creates an initially empty menu with the specified layout. The view is sized
 * to (0,0) and resized to fit its items once they are added.
 *
 * @param name   Identifying name passed to BView.
 * @param layout Arrangement of items: B_ITEMS_IN_COLUMN, B_ITEMS_IN_ROW, or
 *               B_ITEMS_IN_MATRIX.
 * @see BMenu(const char*, float, float)
 */
BMenu::BMenu(const char* name, menu_layout layout)
	:
	BView(BRect(0, 0, 0, 0), name, 0, B_WILL_DRAW),
	fChosenItem(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(MENU_STATE_CLOSED),
	fLayout(layout),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


/**
 * @brief Construct a B_ITEMS_IN_MATRIX menu with an explicit initial size.
 *
 * Use this constructor when items will be placed at arbitrary positions via
 * AddItem(BMenuItem*, BRect). The @p width and @p height parameters set the
 * initial view dimensions; individual item frames determine the final size.
 *
 * @param name   Identifying name passed to BView.
 * @param width  Initial view width in pixels.
 * @param height Initial view height in pixels.
 */
BMenu::BMenu(const char* name, float width, float height)
	:
	BView(BRect(0.0f, 0.0f, 0.0f, 0.0f), name, 0, B_WILL_DRAW),
	fChosenItem(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(0),
	fLayout(B_ITEMS_IN_MATRIX),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


/**
 * @brief Reconstruct a BMenu from an archived BMessage.
 *
 * Restores layout, enabled state, radio mode, trigger settings, dynamic
 * name flag, maximum content width, and all archived child BMenuItem objects.
 * Matrix-layout item frames are restored from the "_i_frames" field.
 *
 * @param archive The archive message previously produced by Archive().
 * @see Archive()
 * @see Instantiate()
 */
BMenu::BMenu(BMessage* archive)
	:
	BView(archive),
	fChosenItem(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(MENU_STATE_CLOSED),
	fLayout(B_ITEMS_IN_ROW),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(archive);
}


/**
 * @brief Destroy the BMenu and release all owned resources.
 *
 * Destroys the cached menu window, removes and deletes all BMenuItem children,
 * and frees internal helper objects (matrix size cache, extra data, layout
 * data).
 *
 * @note Items are deleted regardless of how they were added.
 */
BMenu::~BMenu()
{
	_DeleteMenuWindow();

	RemoveItems(0, CountItems(), true);

	delete fInitMatrixSize;
	delete fExtraMenuData;
	delete fLayoutData;
}


/**
 * @brief Create a new BMenu from an archived BMessage.
 * @param archive The archive message to instantiate from.
 * @return A newly allocated BMenu if \a archive is a valid BMenu archive, or
 *         NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BMenu::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BMenu"))
		return new (nothrow) BMenu(archive);

	return NULL;
}


/**
 * @brief Archive the BMenu into a BMessage.
 *
 * Stores layout, resize-to-fit flag, enabled state, radio mode, trigger
 * enable flag, dynamic name flag, and maximum content width. When @p deep is
 * true, each BMenuItem child is archived recursively; for matrix layouts the
 * per-item frame is also stored.
 *
 * @param data The message to archive into.
 * @param deep If true, all child BMenuItems are archived as well.
 * @return B_OK on success, or an error code on the first failure.
 * @see Instantiate()
 */
status_t
BMenu::Archive(BMessage* data, bool deep) const
{
	status_t err = BView::Archive(data, deep);

	if (err == B_OK && Layout() != B_ITEMS_IN_ROW)
		err = data->AddInt32("_layout", Layout());
	if (err == B_OK)
		err = data->AddBool("_rsize_to_fit", fResizeToFit);
	if (err == B_OK)
		err = data->AddBool("_disable", !IsEnabled());
	if (err ==  B_OK)
		err = data->AddBool("_radio", IsRadioMode());
	if (err == B_OK)
		err = data->AddBool("_trig_disabled", AreTriggersEnabled());
	if (err == B_OK)
		err = data->AddBool("_dyn_label", fDynamicName);
	if (err == B_OK)
		err = data->AddFloat("_maxwidth", fMaxContentWidth);
	if (err == B_OK && deep) {
		BMenuItem* item = NULL;
		int32 index = 0;
		while ((item = ItemAt(index++)) != NULL) {
			BMessage itemData;
			item->Archive(&itemData, deep);
			err = data->AddMessage("_items", &itemData);
			if (err != B_OK)
				break;
			if (fLayout == B_ITEMS_IN_MATRIX) {
				err = data->AddRect("_i_frames", item->fBounds);
			}
		}
	}

	return err;
}


/**
 * @brief Perform menu-level setup when the view is added to a window.
 *
 * Reads the current modifier key codes, installs the menu on the window's
 * handler list (when this is a root menu), populates dynamic items, caches
 * font metrics, and performs an initial layout pass. Sets fAttachAborted if
 * dynamic item addition was cancelled so that the caller can handle it.
 *
 * @note Overrides BView::AttachedToWindow(). Always call the base version
 *       first if you override this in a subclass.
 * @see DetachedFromWindow()
 */
void
BMenu::AttachedToWindow()
{
	BView::AttachedToWindow();

	_GetShiftKey(sShiftKey);
	_GetControlKey(sControlKey);
	_GetCommandKey(sCommandKey);
	_GetOptionKey(sOptionKey);
	_GetMenuKey(sMenuKey);

	if (Superitem() == NULL)
		_Install(Window());

	// The menu should be added to the menu hierarchy and made visible if:
	// * the mouse is over the menu,
	// * the user has requested the menu via the keyboard.
	// So if we don't pass keydown in here, keyboard navigation breaks since
	// fAttachAborted will return false if the mouse isn't over the menu
	bool keyDown = Supermenu() != NULL
		? Supermenu()->fState == MENU_STATE_KEY_TO_SUBMENU : false;
	fAttachAborted = _AddDynamicItems(keyDown);

	if (!fAttachAborted) {
		_CacheFontInfo();
		_LayoutItems(0);
		_UpdateWindowViewSize(false);
	}
}


/**
 * @brief Perform cleanup when the view is removed from its window.
 *
 * Uninstalls the menu from the window's handler list when this is a root
 * menu (i.e. has no superitem).
 *
 * @see AttachedToWindow()
 */
void
BMenu::DetachedFromWindow()
{
	BView::DetachedFromWindow();

	if (Superitem() == NULL)
		_Uninstall();
}


/**
 * @brief Called after the entire view hierarchy has been attached to the window.
 *
 * Delegates to the BView base implementation. Override in subclasses that need
 * to act after all sibling and child views are also attached.
 */
void
BMenu::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called before the entire view hierarchy is detached from the window.
 *
 * Delegates to the BView base implementation. Override in subclasses that need
 * to act while sibling and child views are still attached.
 */
void
BMenu::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Draw the menu background and all visible items.
 *
 * If the layout is stale it is recomputed and the whole view is invalidated
 * instead of drawing, so the next update will paint with fresh geometry.
 * Otherwise DrawBackground() is called followed by DrawItems().
 *
 * @param updateRect The rectangle that needs to be redrawn (view coordinates).
 */
void
BMenu::Draw(BRect updateRect)
{
	if (_RelayoutIfNeeded()) {
		Invalidate();
		return;
	}

	DrawBackground(updateRect);
	DrawItems(updateRect);
}


/**
 * @brief Dispatch incoming messages to the appropriate handler.
 *
 * Scripting messages (those with specifiers) are forwarded to _ScriptReceived().
 * B_MOUSE_WHEEL_CHANGED scrolls the menu window when a BMenuWindow is present.
 * B_MODIFIERS_CHANGED is relayed to the parent menu so it can update modifier
 * key state. All other messages are forwarded to BView::MessageReceived().
 *
 * @param message The message to handle.
 */
void
BMenu::MessageReceived(BMessage* message)
{
	if (message->HasSpecifiers())
		return _ScriptReceived(message);

	switch (message->what) {
		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaY = 0;
			message->FindFloat("be:wheel_delta_y", &deltaY);
			if (deltaY == 0)
				return;

			BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
			if (window == NULL)
				return;

			float largeStep;
			float smallStep;
			window->GetSteps(&smallStep, &largeStep);

			// pressing the shift key scrolls faster
			if ((modifiers() & B_SHIFT_KEY) != 0)
				deltaY *= largeStep;
			else
				deltaY *= smallStep;

			window->TryScrollBy(deltaY);
			break;
		}

		case B_MODIFIERS_CHANGED:
			if (fSuper != NULL && fSuper->fState != MENU_STATE_CLOSED) {
				// inform parent to update its modifier keys and relayout
				BMessenger(fSuper).SendMessage(Window()->CurrentMessage());
			}
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Handle keyboard input while the menu is visible.
 *
 * Implements arrow-key navigation, page-up/page-down scrolling through the
 * BMenuWindow, Enter/Space to invoke the selected item, Escape to dismiss, and
 * trigger-character matching against the items in this menu.
 *
 * @param bytes    UTF-8 encoded byte string of the pressed key.
 * @param numBytes Number of bytes in @p bytes.
 * @see _SelectNextItem()
 * @see _QuitTracking()
 */
void
BMenu::KeyDown(const char* bytes, int32 numBytes)
{
	// TODO: Test how it works on BeOS R5 and implement this correctly
	switch (bytes[0]) {
		case B_UP_ARROW:
		case B_DOWN_ARROW:
		{
			BMenuBar* bar = dynamic_cast<BMenuBar*>(Supermenu());
			if (bar != NULL && fState == MENU_STATE_CLOSED) {
				// tell MenuBar's _Track:
				bar->fState = MENU_STATE_KEY_TO_SUBMENU;
			}
			if (fLayout == B_ITEMS_IN_COLUMN)
				_SelectNextItem(fSelected, bytes[0] == B_DOWN_ARROW);
			break;
		}

		case B_LEFT_ARROW:
			if (fLayout == B_ITEMS_IN_ROW)
				_SelectNextItem(fSelected, false);
			else {
				// this case has to be handled a bit specially.
				BMenuItem* item = Superitem();
				if (item) {
					if (dynamic_cast<BMenuBar*>(Supermenu())) {
						// If we're at the top menu below the menu bar, pass
						// the keypress to the menu bar so we can move to
						// another top level menu.
						BMessenger messenger(Supermenu());
						messenger.SendMessage(Window()->CurrentMessage());
					} else {
						// tell _Track
						fState = MENU_STATE_KEY_LEAVE_SUBMENU;
					}
				}
			}
			break;

		case B_RIGHT_ARROW:
			if (fLayout == B_ITEMS_IN_ROW)
				_SelectNextItem(fSelected, true);
			else {
				if (fSelected != NULL && fSelected->Submenu() != NULL) {
					fSelected->Submenu()->_SetStickyMode(true);
						// fix me: this shouldn't be needed but dynamic menus
						// aren't getting it set correctly when keyboard
						// navigating, which aborts the attach
					fState = MENU_STATE_KEY_TO_SUBMENU;
					_SelectItem(fSelected, true, true, true);
				} else if (dynamic_cast<BMenuBar*>(Supermenu())) {
					// if we have no submenu and we're an
					// item in the top menu below the menubar,
					// pass the keypress to the menubar
					// so you can use the keypress to switch menus.
					BMessenger messenger(Supermenu());
					messenger.SendMessage(Window()->CurrentMessage());
				}
			}
			break;

		case B_PAGE_UP:
		case B_PAGE_DOWN:
		{
			BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
			if (window == NULL || !window->HasScrollers())
				break;

			int32 deltaY = bytes[0] == B_PAGE_UP ? -1 : 1;

			float largeStep;
			window->GetSteps(NULL, &largeStep);
			window->TryScrollBy(deltaY * largeStep);
			break;
		}

		case B_ENTER:
		case B_SPACE:
			if (fSelected != NULL) {
				fChosenItem = fSelected;
					// preserve for exit handling
				_QuitTracking(false);
			}
			break;

		case B_ESCAPE:
			_SelectItem(NULL);
			if (fState == MENU_STATE_CLOSED
				&& dynamic_cast<BMenuBar*>(Supermenu())) {
				// Keyboard may show menu without tracking it
				BMessenger messenger(Supermenu());
				messenger.SendMessage(Window()->CurrentMessage());
			} else
				_QuitTracking(false);
			break;

		default:
		{
			if (AreTriggersEnabled()) {
				uint32 trigger = BUnicodeChar::FromUTF8(&bytes);

				for (uint32 i = CountItems(); i-- > 0;) {
					BMenuItem* item = ItemAt(i);
					if (item->fTriggerIndex < 0 || item->fTrigger != trigger)
						continue;

					_InvokeItem(item);
					_QuitTracking(false);
					break;
				}
			}
			break;
		}
	}
}


/**
 * @brief Return the minimum size of this menu.
 *
 * Revalidates the preferred-size cache if necessary and then composes the
 * computed size with any explicit minimum size set by the caller.
 *
 * @return The minimum BSize the menu requires.
 */
BSize
BMenu::MinSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() != NULL ? GetLayout()->MinSize()
		: fLayoutData->preferred);

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the maximum size of this menu.
 *
 * Revalidates the preferred-size cache if necessary and then composes the
 * computed size with any explicit maximum size set by the caller.
 *
 * @return The maximum BSize the menu allows.
 */
BSize
BMenu::MaxSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() != NULL ? GetLayout()->MaxSize()
		: fLayoutData->preferred);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


/**
 * @brief Return the preferred size of this menu.
 *
 * Revalidates the preferred-size cache if necessary and then composes the
 * computed size with any explicit preferred size set by the caller.
 *
 * @return The preferred BSize computed from the current item layout.
 */
BSize
BMenu::PreferredSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() != NULL ? GetLayout()->PreferredSize()
		: fLayoutData->preferred);

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


/**
 * @brief Write the preferred width and height into the supplied pointers.
 *
 * Either pointer may be NULL if that dimension is not needed.
 *
 * @param _width  Receives the preferred width, or ignored if NULL.
 * @param _height Receives the preferred height, or ignored if NULL.
 */
void
BMenu::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fLayoutData->preferred.width;

	if (_height)
		*_height = fLayoutData->preferred.height;
}


/**
 * @brief Resize the menu view to its preferred dimensions.
 *
 * Delegates to BView::ResizeToPreferred(), which calls GetPreferredSize()
 * internally.
 */
void
BMenu::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/**
 * @brief Perform a layout pass triggered by the layout system.
 *
 * If a BLayout has been explicitly attached, the base class handles it.
 * Otherwise the cached layout is invalidated and recomputed via
 * _RelayoutIfNeeded(), followed by an Invalidate() if the geometry changed.
 */
void
BMenu::DoLayout()
{
	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != NULL) {
		BView::DoLayout();
		return;
	}

	if (_RelayoutIfNeeded())
		Invalidate();
}


/**
 * @brief Notification that the menu view's frame has moved.
 * @param where The new top-left position of the view in parent coordinates.
 */
void
BMenu::FrameMoved(BPoint where)
{
	BView::FrameMoved(where);
}


/**
 * @brief Notification that the menu view's frame has been resized.
 * @param width  The new width of the view.
 * @param height The new height of the view.
 */
void
BMenu::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
}


/**
 * @brief Mark the cached item layout as stale so it is recomputed on next use.
 *
 * This override exists for backwards binary compatibility. It clears the
 * internal layout cache flag and also calls BView::InvalidateLayout() for good
 * measure.
 *
 * @note Do not remove this method even though it looks trivial; it is part of
 *       the public ABI.
 */
void
BMenu::InvalidateLayout()
{
	fUseCachedMenuLayout = false;
	// This method exits for backwards compatibility reasons, it is used to
	// invalidate the menu layout, but we also use call
	// BView::InvalidateLayout() for good measure. Don't delete this method!
	BView::InvalidateLayout(false);
}


/**
 * @brief Give or remove keyboard focus from this menu.
 * @param focused true to make this view the focus view, false to remove focus.
 */
void
BMenu::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
}


/**
 * @brief Append a BMenuItem to the end of the menu.
 * @param item The item to add; ownership transfers to the menu.
 * @return true on success, false if @p item is NULL or memory allocation fails.
 * @see AddItem(BMenuItem*, int32)
 */
bool
BMenu::AddItem(BMenuItem* item)
{
	return AddItem(item, CountItems());
}


/**
 * @brief Insert a BMenuItem at the specified index.
 *
 * Not valid for B_ITEMS_IN_MATRIX menus; use AddItem(BMenuItem*, BRect) for
 * those. Triggers a layout and redraw if the menu is currently visible.
 *
 * @param item  The item to add; ownership transfers to the menu.
 * @param index Zero-based position at which to insert the item.
 * @return true on success, false if @p item is NULL or the index is invalid.
 */
bool
BMenu::AddItem(BMenuItem* item, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenuItem*, int32) this method can only "
			"be called if the menu layout is not B_ITEMS_IN_MATRIX");
	}

	if (item == NULL)
		return false;

	const bool locked = LockLooper();

	if (!_AddItem(item, index)) {
		if (locked)
			UnlockLooper();
		return false;
	}

	InvalidateLayout();
	if (locked) {
		if (!Window()->IsHidden()) {
			_LayoutItems(index);
			_UpdateWindowViewSize(false);
			Invalidate();
		}
		UnlockLooper();
	}

	return true;
}


/**
 * @brief Add a BMenuItem at an explicit frame position (B_ITEMS_IN_MATRIX only).
 *
 * Only valid when the menu layout is B_ITEMS_IN_MATRIX. The supplied @p frame
 * defines the item's bounding rectangle within the menu view.
 *
 * @param item  The item to add; ownership transfers to the menu.
 * @param frame The desired bounding rectangle of the item in view coordinates.
 * @return true on success, false if @p item is NULL or the add fails.
 */
bool
BMenu::AddItem(BMenuItem* item, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenuItem*, BRect) this method can only "
			"be called if the menu layout is B_ITEMS_IN_MATRIX");
	}

	if (item == NULL)
		return false;

	const bool locked = LockLooper();

	item->fBounds = frame;

	int32 index = CountItems();
	if (!_AddItem(item, index)) {
		if (locked)
			UnlockLooper();
		return false;
	}

	if (locked) {
		if (!Window()->IsHidden()) {
			_LayoutItems(index);
			Invalidate();
		}
		UnlockLooper();
	}

	return true;
}


/**
 * @brief Append a submenu wrapped in a new BMenuItem at the end of the menu.
 *
 * A BMenuItem is created automatically and takes ownership of @p submenu.
 *
 * @param submenu The menu to attach as a submenu item.
 * @return true on success, false if allocation of the BMenuItem fails.
 */
bool
BMenu::AddItem(BMenu* submenu)
{
	BMenuItem* item = new (nothrow) BMenuItem(submenu);
	if (item == NULL)
		return false;

	if (!AddItem(item, CountItems())) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


/**
 * @brief Insert a submenu wrapped in a new BMenuItem at the specified index.
 *
 * Not valid for B_ITEMS_IN_MATRIX menus. A BMenuItem is created automatically
 * and takes ownership of @p submenu.
 *
 * @param submenu The menu to attach as a submenu item.
 * @param index   Zero-based position at which to insert the new item.
 * @return true on success, false if allocation fails or the index is invalid.
 */
bool
BMenu::AddItem(BMenu* submenu, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenuItem*, int32) this method can only "
			"be called if the menu layout is not B_ITEMS_IN_MATRIX");
	}

	BMenuItem* item = new (nothrow) BMenuItem(submenu);
	if (item == NULL)
		return false;

	if (!AddItem(item, index)) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


/**
 * @brief Add a submenu at an explicit frame position (B_ITEMS_IN_MATRIX only).
 *
 * Only valid when the menu layout is B_ITEMS_IN_MATRIX. A BMenuItem is created
 * automatically and takes ownership of @p submenu.
 *
 * @param submenu The menu to attach as a submenu item.
 * @param frame   The desired bounding rectangle in view coordinates.
 * @return true on success, false if allocation fails.
 */
bool
BMenu::AddItem(BMenu* submenu, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenu*, BRect) this method can only "
			"be called if the menu layout is B_ITEMS_IN_MATRIX");
	}

	BMenuItem* item = new (nothrow) BMenuItem(submenu);
	if (item == NULL)
		return false;

	if (!AddItem(item, frame)) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


/**
 * @brief Insert all BMenuItems in a BList starting at the given index.
 *
 * Items are inserted in list order beginning at @p index. The menu does not
 * take ownership of the BList itself, only of the BMenuItem objects inside it.
 *
 * @param list  A BList whose elements are BMenuItem pointers.
 * @param index Zero-based position at which insertion begins.
 * @return true on success; false if @p list is NULL.
 * @note This method is not documented in the Be Book; behaviour may differ
 *       from the original R5 implementation.
 */
bool
BMenu::AddList(BList* list, int32 index)
{
	// TODO: test this function, it's not documented in the bebook.
	if (list == NULL)
		return false;

	bool locked = LockLooper();

	int32 numItems = list->CountItems();
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem* item = static_cast<BMenuItem*>(list->ItemAt(i));
		if (item != NULL) {
			if (!_AddItem(item, index + i))
				break;
		}
	}

	InvalidateLayout();
	if (locked && Window() != NULL && !Window()->IsHidden()) {
		// Make sure we update the layout if needed.
		_LayoutItems(index);
		_UpdateWindowViewSize(false);
		Invalidate();
	}

	if (locked)
		UnlockLooper();

	return true;
}


/**
 * @brief Append a separator line to the end of the menu.
 *
 * Creates a new BSeparatorItem and appends it. The item is owned by the menu.
 *
 * @return true on success, false if memory allocation fails.
 */
bool
BMenu::AddSeparatorItem()
{
	BMenuItem* item = new (nothrow) BSeparatorItem();
	if (!item || !AddItem(item, CountItems())) {
		delete item;
		return false;
	}

	return true;
}


/**
 * @brief Remove a specific BMenuItem from the menu without deleting it.
 * @param item The item to remove.
 * @return true if the item was found and removed, false otherwise.
 */
bool
BMenu::RemoveItem(BMenuItem* item)
{
	return _RemoveItems(0, 0, item, false);
}


/**
 * @brief Remove and return the item at the given index without deleting it.
 * @param index Zero-based index of the item to remove.
 * @return The removed BMenuItem, or NULL if @p index is out of range.
 */
BMenuItem*
BMenu::RemoveItem(int32 index)
{
	BMenuItem* item = ItemAt(index);
	if (item != NULL)
		_RemoveItems(index, 1, NULL, false);
	return item;
}


/**
 * @brief Remove a range of items starting at @p index.
 * @param index       Zero-based index of the first item to remove.
 * @param count       Number of items to remove.
 * @param deleteItems If true, each removed item is deleted.
 * @return true if all requested items were removed successfully.
 */
bool
BMenu::RemoveItems(int32 index, int32 count, bool deleteItems)
{
	return _RemoveItems(index, count, NULL, deleteItems);
}


/**
 * @brief Remove the BMenuItem that wraps the given submenu.
 * @param submenu The submenu whose parent item should be removed.
 * @return true if a matching item was found and removed, false otherwise.
 */
bool
BMenu::RemoveItem(BMenu* submenu)
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (static_cast<BMenuItem*>(fItems.ItemAtFast(i))->Submenu()
				== submenu) {
			return _RemoveItems(i, 1, NULL, false);
		}
	}

	return false;
}


/**
 * @brief Return the number of items currently in the menu.
 * @return Item count as int32.
 */
int32
BMenu::CountItems() const
{
	return fItems.CountItems();
}


/**
 * @brief Return the BMenuItem at the given index.
 * @param index Zero-based item index.
 * @return The BMenuItem at @p index, or NULL if out of range.
 */
BMenuItem*
BMenu::ItemAt(int32 index) const
{
	return static_cast<BMenuItem*>(fItems.ItemAt(index));
}


/**
 * @brief Return the submenu attached to the item at the given index.
 * @param index Zero-based item index.
 * @return The BMenu submenu, or NULL if the item has none or is out of range.
 */
BMenu*
BMenu::SubmenuAt(int32 index) const
{
	BMenuItem* item = static_cast<BMenuItem*>(fItems.ItemAt(index));
	return item != NULL ? item->Submenu() : NULL;
}


/**
 * @brief Return the zero-based index of the given BMenuItem.
 * @param item The item to search for.
 * @return The index, or -1 if not found.
 */
int32
BMenu::IndexOf(BMenuItem* item) const
{
	return fItems.IndexOf(item);
}


/**
 * @brief Return the zero-based index of the item that wraps the given submenu.
 * @param submenu The submenu to locate.
 * @return The index, or -1 if not found.
 */
int32
BMenu::IndexOf(BMenu* submenu) const
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (ItemAt(i)->Submenu() == submenu)
			return i;
	}

	return -1;
}


/**
 * @brief Find an item by its text label, searching recursively into submenus.
 * @param label The exact label string to match.
 * @return The first matching BMenuItem, or NULL if none is found.
 */
BMenuItem*
BMenu::FindItem(const char* label) const
{
	BMenuItem* item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Label() && strcmp(item->Label(), label) == 0)
			return item;

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(label);
			if (item != NULL)
				return item;
		}
	}

	return NULL;
}


/**
 * @brief Find an item by its message command value, searching recursively.
 * @param command The what field value to match.
 * @return The first matching BMenuItem, or NULL if none is found.
 */
BMenuItem*
BMenu::FindItem(uint32 command) const
{
	BMenuItem* item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Command() == command)
			return item;

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(command);
			if (item != NULL)
				return item;
		}
	}

	return NULL;
}


/**
 * @brief Set the target BHandler for all items in this menu.
 *
 * Iterates over every direct child item (not recursing into submenus) and
 * calls BMenuItem::SetTarget(BHandler*) on each.
 *
 * @param handler The handler to set as the target for each item.
 * @return B_OK if all targets were set, or the first error code encountered.
 */
status_t
BMenu::SetTargetForItems(BHandler* handler)
{
	status_t status = B_OK;
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		status = ItemAt(i)->SetTarget(handler);
		if (status < B_OK)
			break;
	}

	return status;
}


/**
 * @brief Set a BMessenger target for all items in this menu.
 *
 * Iterates over every direct child item (not recursing into submenus) and
 * calls BMenuItem::SetTarget(BMessenger) on each.
 *
 * @param messenger The messenger to set as the target for each item.
 * @return B_OK if all targets were set, or the first error code encountered.
 */
status_t
BMenu::SetTargetForItems(BMessenger messenger)
{
	status_t status = B_OK;
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		status = ItemAt(i)->SetTarget(messenger);
		if (status < B_OK)
			break;
	}

	return status;
}


/**
 * @brief Enable or disable the menu and propagate the state to its superitem.
 *
 * When the enable state changes, the superitem (if any) is updated to match,
 * and if the direct parent is a _BMCMenuBar_ the parent menu is also updated.
 *
 * @param enable true to enable the menu, false to disable it.
 */
void
BMenu::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (dynamic_cast<_BMCMenuBar_*>(Supermenu()) != NULL)
		Supermenu()->SetEnabled(enable);

	if (fSuperitem)
		fSuperitem->SetEnabled(enable);
}


/**
 * @brief Enable or disable radio-button-style mutual exclusion for items.
 *
 * When turned off, SetLabelFromMarked(false) is also called automatically.
 *
 * @param on true to enable radio mode, false to disable it.
 * @see SetLabelFromMarked()
 */
void
BMenu::SetRadioMode(bool on)
{
	fRadioMode = on;
	if (!on)
		SetLabelFromMarked(false);
}


/**
 * @brief Enable or disable keyboard trigger characters for this menu.
 * @param enable true to show and respond to trigger characters, false to hide them.
 */
void
BMenu::SetTriggersEnabled(bool enable)
{
	fTriggerEnabled = enable;
}


/**
 * @brief Set the maximum width available for item content.
 *
 * Labels wider than this value are clipped. Pass 0 (the default) for no limit.
 *
 * @param width Maximum content width in pixels, or 0 for unlimited.
 */
void
BMenu::SetMaxContentWidth(float width)
{
	fMaxContentWidth = width;
}


/**
 * @brief Make the menu label track the currently marked item.
 *
 * When enabled, the superitem's label is updated to match the label of the
 * marked item whenever the mark changes. Enabling this also enables radio
 * mode automatically.
 *
 * @param on true to enable dynamic labelling, false to disable it.
 * @see SetRadioMode()
 */
void
BMenu::SetLabelFromMarked(bool on)
{
	fDynamicName = on;
	if (on)
		SetRadioMode(true);
}


/**
 * @brief Return whether the menu label tracks the marked item.
 * @return true if dynamic labelling is enabled.
 */
bool
BMenu::IsLabelFromMarked()
{
	return fDynamicName;
}


/**
 * @brief Return whether the menu is currently enabled.
 *
 * A menu is considered enabled only if its own enabled flag is set AND its
 * parent menu (if any) is also enabled.
 *
 * @return true if the menu and all ancestor menus are enabled.
 */
bool
BMenu::IsEnabled() const
{
	if (!fEnabled)
		return false;

	return fSuper ? fSuper->IsEnabled() : true ;
}


/**
 * @brief Return whether radio-button-style exclusion is active.
 * @return true if radio mode is on.
 */
bool
BMenu::IsRadioMode() const
{
	return fRadioMode;
}


/**
 * @brief Return whether keyboard trigger characters are enabled.
 * @return true if triggers are shown and honoured.
 */
bool
BMenu::AreTriggersEnabled() const
{
	return fTriggerEnabled;
}


/**
 * @brief Return whether the menu should be redrawn after leaving sticky mode.
 *
 * The base implementation always returns false. BPopUpMenu overrides this.
 *
 * @return false in BMenu; subclasses may return true.
 */
bool
BMenu::IsRedrawAfterSticky() const
{
	return false;
}


/**
 * @brief Return the maximum content width set by SetMaxContentWidth().
 * @return The maximum content width in pixels, or 0 if unlimited.
 */
float
BMenu::MaxContentWidth() const
{
	return fMaxContentWidth;
}


/**
 * @brief Return the first marked item in the menu.
 *
 * Only examines direct children; does not recurse into submenus.
 *
 * @return The first marked BMenuItem, or NULL if no item is marked.
 */
BMenuItem*
BMenu::FindMarked()
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		if (item->IsMarked())
			return item;
	}

	return NULL;
}


/**
 * @brief Return the index of the first marked item in the menu.
 *
 * Only examines direct children; does not recurse into submenus.
 *
 * @return Zero-based index of the first marked item, or -1 if none is marked.
 */
int32
BMenu::FindMarkedIndex()
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		if (item->IsMarked())
			return i;
	}

	return -1;
}


/**
 * @brief Return the parent BMenu that contains this menu's superitem.
 * @return The parent BMenu, or NULL if this is a root menu.
 */
BMenu*
BMenu::Supermenu() const
{
	return fSuper;
}


/**
 * @brief Return the BMenuItem in the parent menu whose submenu is this menu.
 * @return The superitem BMenuItem, or NULL if this is a root menu.
 */
BMenuItem*
BMenu::Superitem() const
{
	return fSuperitem;
}


/**
 * @brief Resolve the target handler for a scripting message.
 *
 * Checks the known property list (sPropList) and returns this menu as the
 * target when a match is found. Unknown properties are forwarded to
 * BView::ResolveSpecifier().
 *
 * @param msg       The scripting message being processed.
 * @param index     Index of the current specifier in the message.
 * @param specifier The current specifier extracted from the message.
 * @param form      The specifier form constant.
 * @param property  The property name string.
 * @return The BHandler that should handle the message.
 */
BHandler*
BMenu::ResolveSpecifier(BMessage* msg, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	BPropertyInfo propInfo(sPropList);
	BHandler* target = NULL;

	if (propInfo.FindMatch(msg, index, specifier, form, property) >= B_OK) {
		target = this;
	}

	if (!target)
		target = BView::ResolveSpecifier(msg, index, specifier, form,
		property);

	return target;
}


/**
 * @brief Report the scripting suites supported by BMenu.
 *
 * Adds the "suite/vnd.Be-menu" suite name and the corresponding property-info
 * flat data to @p data, then delegates to BView::GetSupportedSuites().
 *
 * @param data The reply message to populate with suite information.
 * @return B_OK on success, B_BAD_VALUE if @p data is NULL, or an error code.
 */
status_t
BMenu::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-menu");

	if (err < B_OK)
		return err;

	BPropertyInfo propertyInfo(sPropList);
	err = data->AddFlat("messages", &propertyInfo);

	if (err < B_OK)
		return err;

	return BView::GetSupportedSuites(data);
}


/**
 * @brief Binary-compatibility hook for calling overridden virtual methods.
 *
 * Dispatches perform codes for layout-related virtuals (MinSize, MaxSize,
 * PreferredSize, LayoutAlignment, HasHeightForWidth, GetHeightForWidth,
 * SetLayout, LayoutInvalidated, DoLayout) to the corresponding BMenu
 * overrides. Unknown codes are forwarded to BView::Perform().
 *
 * @param code  A PERFORM_CODE_* constant identifying the virtual to call.
 * @param _data Pointer to a perform_data_* struct carrying arguments and
 *              receiving the return value.
 * @return B_OK on success, or the result of BView::Perform() for unknown codes.
 */
status_t
BMenu::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BMenu::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BMenu::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BMenu::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BMenu::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BMenu::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BMenu::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BMenu::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BMenu::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BMenu::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


// #pragma mark - BMenu protected methods


/**
 * @brief Protected constructor used by BMenuBar and BPopUpMenu subclasses.
 *
 * Allows subclasses to specify an explicit initial frame, resizing mode, view
 * flags, layout, and whether the menu should resize itself to fit its items.
 *
 * @param frame        Initial frame rectangle in parent coordinates.
 * @param name         Identifying name passed to BView.
 * @param resizingMode BView resizing mode flags (B_FOLLOW_*).
 * @param flags        BView creation flags (B_WILL_DRAW, etc.).
 * @param layout       Item arrangement: B_ITEMS_IN_COLUMN, _ROW, or _MATRIX.
 * @param resizeToFit  If true the menu resizes itself to fit its items.
 */
BMenu::BMenu(BRect frame, const char* name, uint32 resizingMode, uint32 flags,
	menu_layout layout, bool resizeToFit)
	:
	BView(frame, name, resizingMode, flags),
	fChosenItem(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(MENU_STATE_CLOSED),
	fLayout(layout),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(resizeToFit),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


/**
 * @brief Set the padding applied around each item's content.
 *
 * These values define the spacing between the item's bounding rectangle and
 * its drawn content (label, shortcut, checkmark, etc.).
 *
 * @param left   Left padding in pixels.
 * @param top    Top padding in pixels.
 * @param right  Right padding in pixels.
 * @param bottom Bottom padding in pixels.
 */
void
BMenu::SetItemMargins(float left, float top, float right, float bottom)
{
	fPad.Set(left, top, right, bottom);
}


/**
 * @brief Retrieve the current item content padding values.
 *
 * Any of the output pointers may be NULL if that value is not needed.
 *
 * @param _left   Receives the left padding, or ignored if NULL.
 * @param _top    Receives the top padding, or ignored if NULL.
 * @param _right  Receives the right padding, or ignored if NULL.
 * @param _bottom Receives the bottom padding, or ignored if NULL.
 */
void
BMenu::GetItemMargins(float* _left, float* _top, float* _right,
	float* _bottom) const
{
	if (_left != NULL)
		*_left = fPad.left;

	if (_top != NULL)
		*_top = fPad.top;

	if (_right != NULL)
		*_right = fPad.right;

	if (_bottom != NULL)
		*_bottom = fPad.bottom;
}


/**
 * @brief Return the item layout mode of this menu.
 * @return One of B_ITEMS_IN_COLUMN, B_ITEMS_IN_ROW, or B_ITEMS_IN_MATRIX.
 */
menu_layout
BMenu::Layout() const
{
	return fLayout;
}


/**
 * @brief Show the menu without pre-selecting any item.
 * @see Show(bool)
 */
void
BMenu::Show()
{
	Show(false);
}


/**
 * @brief Make the menu visible in its BMenuWindow.
 * @param selectFirst If true, the first enabled item is pre-selected.
 * @see Hide()
 */
void
BMenu::Show(bool selectFirst)
{
	_Show(selectFirst);
}


/**
 * @brief Hide the menu and its BMenuWindow.
 * @see Show()
 */
void
BMenu::Hide()
{
	_Hide();
}


/**
 * @brief Display the menu and run the tracking loop, returning the chosen item.
 *
 * This is the main entry point used by BPopUpMenu::Go(). When @p sticky is
 * true the menu stays open after the mouse button is released until the user
 * clicks again. If @p clickToOpenRect is provided, clicking inside it in
 * non-sticky mode switches the menu to sticky mode.
 *
 * @param sticky           If true, start in sticky (click-to-open) mode.
 * @param clickToOpenRect  Optional screen-coordinate rectangle that activates
 *                         sticky mode on mouse release inside it.
 * @return The BMenuItem chosen by the user, or NULL if the menu was dismissed
 *         without a selection.
 */
BMenuItem*
BMenu::Track(bool sticky, BRect* clickToOpenRect)
{
	if (sticky && LockLooper()) {
		//RedrawAfterSticky(Bounds());
			// the call above didn't do anything, so I've removed it for now
		UnlockLooper();
	}

	if (clickToOpenRect != NULL && LockLooper()) {
		fExtraRect = clickToOpenRect;
		ConvertFromScreen(fExtraRect);
		UnlockLooper();
	}

	_SetStickyMode(sticky);

	int action;
	BMenuItem* menuItem = _Track(&action);

	fExtraRect = NULL;

	return menuItem;
}


// #pragma mark - BMenu private methods


/**
 * @brief Hook called to populate a dynamic menu with items.
 *
 * Subclasses override this to add items on demand. The @p state parameter
 * indicates which phase of the add sequence is occurring:
 * - B_INITIAL_ADD: called first; return true to continue, false to abort.
 * - B_PROCESSING:  called repeatedly until it returns false.
 * - B_ABORT:       called once if the operation should be cancelled.
 *
 * @param state One of B_INITIAL_ADD, B_PROCESSING, or B_ABORT.
 * @return true to continue adding, false when finished or to abort.
 */
bool
BMenu::AddDynamicItem(add_state state)
{
	// Implemented in subclasses
	return false;
}


/**
 * @brief Draw the menu's background region using the current ControlLook.
 *
 * Computes the appropriate border flags based on where the menu sits relative
 * to its window edges, then delegates to be_control_look->DrawMenuBackground().
 *
 * @param updateRect The rectangle that needs repainting (view coordinates).
 */
void
BMenu::DrawBackground(BRect updateRect)
{
	rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;

	if (IsFocus())
		flags |= BControlLook::B_FOCUSED;

	BRect rect = Bounds();
	uint32 borders = BControlLook::B_LEFT_BORDER
		| BControlLook::B_RIGHT_BORDER;
	if (Window() != NULL && Parent() != NULL) {
		if (Parent()->Frame().top == Window()->Bounds().top)
			borders |= BControlLook::B_TOP_BORDER;

		if (Parent()->Frame().bottom == Window()->Bounds().bottom)
			borders |= BControlLook::B_BOTTOM_BORDER;
	} else {
		borders |= BControlLook::B_TOP_BORDER
			| BControlLook::B_BOTTOM_BORDER;
	}
	be_control_look->DrawMenuBackground(this, rect, updateRect, base, flags,
		borders);
}


/**
 * @brief Install a custom tracking hook that can abort the tracking loop.
 *
 * The hook is called once per tracking iteration. If it returns true the
 * tracking loop exits immediately without selecting any item.
 *
 * @param func  The hook function, or NULL to clear an existing hook.
 * @param state Opaque context pointer forwarded to @p func on each call.
 */
void
BMenu::SetTrackingHook(menu_tracking_hook func, void* state)
{
	fExtraMenuData->trackingHook = func;
	fExtraMenuData->trackingState = state;
}


// #pragma mark - Reorder item methods


/**
 * @brief Sort menu items in-place using a caller-supplied comparison function.
 *
 * Uses std::stable_sort so that items that compare as equal retain their
 * original relative order. Invalidates the layout and redraws if the menu
 * is currently visible.
 *
 * @param compare A function returning negative/zero/positive like strcmp().
 */
void
BMenu::SortItems(int (*compare)(const BMenuItem*, const BMenuItem*))
{
	BMenuItem** begin = (BMenuItem**)fItems.Items();
	BMenuItem** end = begin + fItems.CountItems();

	std::stable_sort(begin, end, BPrivate::MenuItemComparator(compare));

	InvalidateLayout();
	if (Window() != NULL && !Window()->IsHidden() && LockLooper()) {
		_LayoutItems(0);
		Invalidate();
		UnlockLooper();
	}
}


/**
 * @brief Exchange the positions of two menu items.
 * @param indexA Zero-based index of the first item.
 * @param indexB Zero-based index of the second item.
 * @return true if the swap succeeded, false if either index is invalid.
 */
bool
BMenu::SwapItems(int32 indexA, int32 indexB)
{
	bool swapped = fItems.SwapItems(indexA, indexB);
	if (swapped) {
		InvalidateLayout();
		if (Window() != NULL && !Window()->IsHidden() && LockLooper()) {
			_LayoutItems(std::min(indexA, indexB));
			Invalidate();
			UnlockLooper();
		}
	}

	return swapped;
}


/**
 * @brief Move a menu item from one position to another.
 * @param indexFrom Zero-based index of the item to move.
 * @param indexTo   Zero-based destination index.
 * @return true if the move succeeded, false if either index is invalid.
 */
bool
BMenu::MoveItem(int32 indexFrom, int32 indexTo)
{
	bool moved = fItems.MoveItem(indexFrom, indexTo);
	if (moved) {
		InvalidateLayout();
		if (Window() != NULL && !Window()->IsHidden() && LockLooper()) {
			_LayoutItems(std::min(indexFrom, indexTo));
			Invalidate();
			UnlockLooper();
		}
	}

	return moved;
}


void BMenu::_ReservedMenu3() {}
void BMenu::_ReservedMenu4() {}
void BMenu::_ReservedMenu5() {}
void BMenu::_ReservedMenu6() {}


/**
 * @brief Common initialisation shared by all BMenu constructors.
 *
 * Sets the menu font from the global sMenuInfo, allocates the ExtraMenuData
 * and LayoutData helpers, configures the view colors, and — when @p archive is
 * non-NULL — restores all persisted state including child BMenuItems.
 *
 * @param archive Optional archive message to restore state from; NULL for a
 *                freshly constructed menu.
 */
void
BMenu::_InitData(BMessage* archive)
{
	BPrivate::kEmptyMenuLabel = B_TRANSLATE("<empty>");

	// TODO: Get _color, _fname, _fflt from the message, if present
	BFont font;
	font.SetFamilyAndStyle(sMenuInfo.f_family, sMenuInfo.f_style);
	font.SetSize(sMenuInfo.font_size);
	SetFont(&font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);

	fExtraMenuData = new (nothrow) BPrivate::ExtraMenuData();

	const float labelSpacing = be_control_look->DefaultLabelSpacing();
	fPad = BRect(ceilf(labelSpacing * 2.3f), ceilf(labelSpacing / 3.0f),
		ceilf((labelSpacing / 3.0f) * 10.0f), 0.0f);

	fLayoutData = new LayoutData;
	fLayoutData->lastResizingMode = ResizingMode();

	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetViewColor(B_TRANSPARENT_COLOR);

	fTriggerEnabled = sMenuInfo.triggers_always_shown;

	if (archive != NULL) {
		archive->FindInt32("_layout", (int32*)&fLayout);
		archive->FindBool("_rsize_to_fit", &fResizeToFit);
		bool disabled;
		if (archive->FindBool("_disable", &disabled) == B_OK)
			fEnabled = !disabled;
		archive->FindBool("_radio", &fRadioMode);

		bool disableTrigger = false;
		archive->FindBool("_trig_disabled", &disableTrigger);
		fTriggerEnabled = !disableTrigger;

		archive->FindBool("_dyn_label", &fDynamicName);
		archive->FindFloat("_maxwidth", &fMaxContentWidth);

		BMessage msg;
		for (int32 i = 0; archive->FindMessage("_items", i, &msg) == B_OK; i++) {
			BArchivable* object = instantiate_object(&msg);
			if (BMenuItem* item = dynamic_cast<BMenuItem*>(object)) {
				BRect bounds;
				if (fLayout == B_ITEMS_IN_MATRIX
					&& archive->FindRect("_i_frames", i, &bounds) == B_OK)
					AddItem(item, bounds);
				else
					AddItem(item);
			}
		}
	}
}


/**
 * @brief Open the BMenuWindow for this menu and optionally pre-select an item.
 *
 * Attempts to reuse a cached BMenuWindow from the supermenu; if none is
 * available a new one is created. Dynamic items are added before the window
 * is shown. If dynamic item addition is aborted the function returns false and
 * the window is cleaned up.
 *
 * @param selectFirstItem If true, the first item is selected after the window
 *                        is shown.
 * @param keyDown         True when the menu is being opened via keyboard
 *                        navigation; passed to _AddDynamicItems().
 * @return true if the menu was successfully shown, false otherwise.
 * @see _Hide()
 */
bool
BMenu::_Show(bool selectFirstItem, bool keyDown)
{
	if (Window() != NULL)
		return false;

	// See if the supermenu has a cached menuwindow,
	// and use that one if possible.
	BMenuWindow* window = NULL;
	bool ourWindow = false;
	if (fSuper != NULL) {
		fSuperbounds = fSuper->ConvertToScreen(fSuper->Bounds());
		window = fSuper->_MenuWindow();
	}

	// Otherwise, create a new one
	// This happens for "stand alone" BPopUpMenus
	// (i.e. not within a BMenuField)
	if (window == NULL) {
		// Menu windows get the BMenu's handler name
		window = new (nothrow) BMenuWindow(Name());
		ourWindow = true;
	}

	if (window == NULL)
		return false;

	if (window->Lock()) {
		bool addAborted = false;
		if (keyDown)
			addAborted = _AddDynamicItems(keyDown);

		if (addAborted) {
			if (ourWindow)
				window->Quit();
			else
				window->Unlock();
			return false;
		}
		fAttachAborted = false;

		window->AttachMenu(this);

		if (ItemAt(0) != NULL) {
			float width, height;
			ItemAt(0)->GetContentSize(&width, &height);

			window->SetSmallStep(ceilf(height));
		}

		// Menu didn't have the time to add its items: aborting...
		if (fAttachAborted) {
			window->DetachMenu();
			// TODO: Probably not needed, we can just let _hide() quit the
			// window.
			if (ourWindow)
				window->Quit();
			else
				window->Unlock();
			return false;
		}

		_UpdateWindowViewSize(true);
		window->Show();

		if (selectFirstItem)
			_SelectItem(ItemAt(0), false);

		window->Unlock();
	}

	return true;
}


/**
 * @brief Close the BMenuWindow for this menu.
 *
 * Deselects any selected item, hides and detaches the window, then either
 * unlocks it (if owned by the supermenu) or quits it (if this is a root menu).
 * Also deletes the submenu window cache.
 *
 * @see _Show()
 */
void
BMenu::_Hide()
{
	BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
	if (window == NULL || !window->Lock())
		return;

	if (fSelected != NULL)
		_SelectItem(NULL);

	window->Hide();
	window->DetachMenu();
		// we don't want to be deleted when the window is removed

	if (fSuper != NULL)
		window->Unlock();
	else
		window->Quit();
			// it's our window, quit it

	_DeleteMenuWindow();
		// Delete the menu window used by our submenus
}


/**
 * @brief Dispatch a scripting message targeting the menu itself.
 *
 * Handles Enabled get/set, Label/Mark (forwarded to the superitem),
 * Menu create/delete/navigate, MenuItem count/create/delete/execute/navigate.
 * Replies to the message with a B_REPLY or B_MESSAGE_NOT_UNDERSTOOD as
 * appropriate.
 *
 * @param message The scripting message with at least one specifier.
 */
void BMenu::_ScriptReceived(BMessage* message)
{
	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return BView::MessageReceived(message);
	}

	BPropertyInfo propertyInfo(sPropList);
	switch (propertyInfo.FindMatch(message, index, &specifier, what,
			property)) {
		case 0: // Enabled: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddBool("result", IsEnabled());
			break;
		case 1: // Enabled: SET
			if (message->what == B_SET_PROPERTY) {
				bool isEnabled;
				err = message->FindBool("data", &isEnabled);
				if (err >= B_OK)
					SetEnabled(isEnabled);
			}
			break;
		case 2: // Label: GET
		case 3: // Label: SET
		case 4: // Mark: GET
		case 5: { // Mark: SET
			BMenuItem *item = Superitem();
			if (item != NULL)
				return Supermenu()->_ItemScriptReceived(message, item);

			break;
		}
		case 6: // Menu: CREATE
			if (message->what == B_CREATE_PROPERTY) {
				const char *label;
				ObjectDeleter<BMessage> invokeMessage(new BMessage());
				BMessenger target;
				ObjectDeleter<BMenuItem> item;
				err = message->FindString("data", &label);
				if (err >= B_OK) {
					invokeMessage.SetTo(new BMessage());
					err = message->FindInt32("what",
						(int32*)&invokeMessage->what);
					if (err == B_NAME_NOT_FOUND) {
						invokeMessage.Unset();
						err = B_OK;
					}
				}
				if (err >= B_OK) {
					item.SetTo(new BMenuItem(new BMenu(label),
						invokeMessage.Detach()));
				}
				if (err >= B_OK) {
					err = _InsertItemAtSpecifier(specifier, what, item.Get());
				}
				if (err >= B_OK)
					item.Detach();
			}
			break;
		case 7: { // Menu: DELETE
			if (message->what == B_DELETE_PROPERTY) {
				BMenuItem *item = NULL;
				int32 index;
				err = _ResolveItemSpecifier(specifier, what, item, &index);
				if (err >= B_OK) {
					if (item->Submenu() == NULL)
						err = B_BAD_VALUE;
					else {
						if (index >= 0)
							RemoveItem(index);
						else
							RemoveItem(item);
					}
				}
			}
			break;
		}
		case 8: { // Menu: *
			// TODO: check that submenu looper is running and handle it
			// correctly
			BMenu *submenu = NULL;
			BMenuItem *item;
			err = _ResolveItemSpecifier(specifier, what, item);
			if (err >= B_OK)
				submenu = item->Submenu();
			if (submenu != NULL) {
				message->PopSpecifier();
				return submenu->_ScriptReceived(message);
			}
			break;
		}
		case 9: // MenuItem: COUNT
			if (message->what == B_COUNT_PROPERTIES)
				err = replyMsg.AddInt32("result", CountItems());
			break;
		case 10: // MenuItem: CREATE
			if (message->what == B_CREATE_PROPERTY) {
				const char *label;
				ObjectDeleter<BMessage> invokeMessage(new BMessage());
				bool targetPresent = true;
				BMessenger target;
				ObjectDeleter<BMenuItem> item;
				err = message->FindString("data", &label);
				if (err >= B_OK) {
					err = message->FindMessage("be:invoke_message",
						invokeMessage.Get());
					if (err == B_NAME_NOT_FOUND) {
						err = message->FindInt32("what",
							(int32*)&invokeMessage->what);
						if (err == B_NAME_NOT_FOUND) {
							invokeMessage.Unset();
							err = B_OK;
						}
					}
				}
				if (err >= B_OK) {
					err = message->FindMessenger("be:target", &target);
					if (err == B_NAME_NOT_FOUND) {
						targetPresent = false;
						err = B_OK;
					}
				}
				if (err >= B_OK) {
					item.SetTo(new BMenuItem(label, invokeMessage.Detach()));
					if (targetPresent)
						err = item->SetTarget(target);
				}
				if (err >= B_OK) {
					err = _InsertItemAtSpecifier(specifier, what, item.Get());
				}
				if (err >= B_OK)
					item.Detach();
			}
			break;
		case 11: // MenuItem: DELETE
			if (message->what == B_DELETE_PROPERTY) {
				BMenuItem *item = NULL;
				int32 index;
				err = _ResolveItemSpecifier(specifier, what, item, &index);
				if (err >= B_OK) {
					if (index >= 0)
						RemoveItem(index);
					else
						RemoveItem(item);
				}
			}
			break;
		case 12: { // MenuItem: EXECUTE
			if (message->what == B_EXECUTE_PROPERTY) {
				BMenuItem *item = NULL;
				err = _ResolveItemSpecifier(specifier, what, item);
				if (err >= B_OK) {
					if (!item->IsEnabled())
						err = B_NOT_ALLOWED;
					else
						err = item->Invoke();
				}
			}
			break;
		}
		case 13: { // MenuItem: *
			BMenuItem *item = NULL;
			err = _ResolveItemSpecifier(specifier, what, item);
			if (err >= B_OK) {
				message->PopSpecifier();
				return _ItemScriptReceived(message, item);
			}
			break;
		}
		default:
			return BView::MessageReceived(message);
	}

	if (err != B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;

		if (err == B_BAD_SCRIPT_SYNTAX)
			replyMsg.AddString("message", "Didn't understand the specifier(s)");
		else
			replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	message->SendReply(&replyMsg);
}


/**
 * @brief Dispatch a scripting message targeting a specific BMenuItem.
 *
 * Handles Enabled get/set, Label get/set, and Mark get/set for the given
 * @p item. Replies to the message with B_REPLY or B_MESSAGE_NOT_UNDERSTOOD.
 *
 * @param message The scripting message.
 * @param item    The item to read from or modify.
 */
void BMenu::_ItemScriptReceived(BMessage* message, BMenuItem* item)
{
	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return BView::MessageReceived(message);
	}

	BPropertyInfo propertyInfo(sPropList);
	switch (propertyInfo.FindMatch(message, index, &specifier, what,
			property)) {
		case 0: // Enabled: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddBool("result", item->IsEnabled());
			break;
		case 1: // Enabled: SET
			if (message->what == B_SET_PROPERTY) {
				bool isEnabled;
				err = message->FindBool("data", &isEnabled);
				if (err >= B_OK)
					item->SetEnabled(isEnabled);
			}
			break;
		case 2: // Label: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddString("result", item->Label());
			break;
		case 3: // Label: SET
			if (message->what == B_SET_PROPERTY) {
				const char *label;
				err = message->FindString("data", &label);
				if (err >= B_OK)
					item->SetLabel(label);
			}
		case 4: // Mark: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddBool("result", item->IsMarked());
			break;
		case 5: // Mark: SET
			if (message->what == B_SET_PROPERTY) {
				bool isMarked;
				err = message->FindBool("data", &isMarked);
				if (err >= B_OK)
					item->SetMarked(isMarked);
			}
			break;
		case 6: // Menu: CREATE
		case 7: // Menu: DELETE
		case 8: // Menu: *
		case 9: // MenuItem: COUNT
		case 10: // MenuItem: CREATE
		case 11: // MenuItem: DELETE
		case 12: // MenuItem: EXECUTE
		case 13: // MenuItem: *
			break;
		default:
			return BView::MessageReceived(message);
	}

	if (err != B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;
		replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	message->SendReply(&replyMsg);
}


/**
 * @brief Resolve a scripting specifier to the corresponding BMenuItem.
 *
 * Supports B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, and
 * B_NAME_SPECIFIER. On success, @p item is set to the found item.
 *
 * @param specifier The specifier sub-message from the scripting message.
 * @param what      The specifier form constant (B_INDEX_SPECIFIER, etc.).
 * @param item      Receives the found BMenuItem pointer.
 * @param _index    If non-NULL, receives the zero-based numeric index of the
 *                  item (-1 for name-based lookups).
 * @return B_OK on success, B_BAD_INDEX if the item could not be found, or
 *         another error if the specifier fields are missing.
 */
status_t BMenu::_ResolveItemSpecifier(const BMessage& specifier, int32 what,
	BMenuItem*& item, int32 *_index)
{
	status_t err;
	item = NULL;
	int32 index = -1;
	switch (what) {
		case B_INDEX_SPECIFIER:
		case B_REVERSE_INDEX_SPECIFIER: {
			err = specifier.FindInt32("index", &index);
			if (err < B_OK)
				return err;
			if (what == B_REVERSE_INDEX_SPECIFIER)
				index = CountItems() - index;
			item = ItemAt(index);
			break;
		}
		case B_NAME_SPECIFIER: {
			const char* name;
			err = specifier.FindString("name", &name);
			if (err < B_OK)
				return err;
			item = FindItem(name);
			break;
		}
	}
	if (item == NULL)
		return B_BAD_INDEX;

	if (_index != NULL)
		*_index = index;

	return B_OK;
}


/**
 * @brief Insert a BMenuItem at the position described by a scripting specifier.
 *
 * Supports B_INDEX_SPECIFIER and B_REVERSE_INDEX_SPECIFIER.
 * B_NAME_SPECIFIER is not supported and returns B_NOT_SUPPORTED.
 *
 * @param specifier The specifier sub-message describing the insertion point.
 * @param what      The specifier form constant.
 * @param item      The item to insert; caller retains ownership on failure.
 * @return B_OK on success, B_BAD_INDEX if the index is invalid, or
 *         B_NOT_SUPPORTED for unsupported specifier forms.
 */
status_t BMenu::_InsertItemAtSpecifier(const BMessage& specifier, int32 what,
	BMenuItem* item)
{
	status_t err;
	switch (what) {
		case B_INDEX_SPECIFIER:
		case B_REVERSE_INDEX_SPECIFIER: {
			int32 index;
			err = specifier.FindInt32("index", &index);
			if (err < B_OK) return err;
			if (what == B_REVERSE_INDEX_SPECIFIER)
				index = CountItems() - index;
			if (!AddItem(item, index))
				return B_BAD_INDEX;
			break;
		}
		case B_NAME_SPECIFIER:
			return B_NOT_SUPPORTED;
			break;
	}

	return B_OK;
}


// #pragma mark - mouse tracking


/** @brief Delay (microseconds) before a hovered submenu opens; currently 0. */
const static bigtime_t kOpenSubmenuDelay = 0;
/** @brief Timeout (microseconds) before the navigation-area protection expires. */
const static bigtime_t kNavigationAreaTimeout = 1000000;


/**
 * @brief Run the interactive mouse-tracking loop for this menu.
 *
 * Polls mouse position and button state in a tight loop, selecting items
 * under the cursor, opening and closing submenus, and honouring sticky mode.
 * Also handles keyboard-driven navigation when fState is set externally.
 * The loop exits when fState transitions to MENU_STATE_CLOSED.
 *
 * @param action Output parameter; receives the final fState value so the
 *               caller can distinguish normal close from keyboard navigation.
 * @param start  Unused legacy parameter (reserved for future use).
 * @return The BMenuItem that was ultimately chosen, or NULL if the menu was
 *         dismissed without a selection.
 * @see _UpdateStateOpenSelect()
 * @see _UpdateStateClose()
 */
BMenuItem*
BMenu::_Track(int* action, long start)
{
	// TODO: cleanup
	BMenuItem* item = NULL;
	BRect navAreaRectAbove;
	BRect navAreaRectBelow;
	bigtime_t selectedTime = system_time();
	bigtime_t navigationAreaTime = 0;

	fState = MENU_STATE_TRACKING;
	fChosenItem = NULL;
		// we will use this for keyboard selection

	BPoint location;
	uint32 buttons = 0;
	if (LockLooper()) {
		GetMouse(&location, &buttons);
		UnlockLooper();
	}

	bool releasedOnce = buttons == 0;
	while (fState != MENU_STATE_CLOSED) {
		if (_CustomTrackingWantsToQuit())
			break;

		if (!LockLooper())
			break;

		BMenuWindow* window = static_cast<BMenuWindow*>(Window());
		BPoint screenLocation = ConvertToScreen(location);
		if (window->CheckForScrolling(screenLocation)) {
			UnlockLooper();
			continue;
		}

		// The order of the checks is important
		// to be able to handle overlapping menus:
		// first we check if mouse is inside a submenu,
		// then if the mouse is inside this menu,
		// then if it's over a super menu.
		if (_OverSubmenu(fSelected, screenLocation)
			|| fState == MENU_STATE_KEY_TO_SUBMENU) {
			if (fState == MENU_STATE_TRACKING) {
				// not if from R.Arrow
				fState = MENU_STATE_TRACKING_SUBMENU;
			}
			navAreaRectAbove = BRect();
			navAreaRectBelow = BRect();

			// Since the submenu has its own looper,
			// we can unlock ours. Doing so also make sure
			// that our window gets any update message to
			// redraw itself
			UnlockLooper();

			// To prevent NULL access violation, ensure a menu has actually
			// been selected and that it has a submenu. Because keyboard and
			// mouse interactions set selected items differently, the menu
			// tracking thread needs to be careful in triggering the navigation
			// to the submenu.
			if (fSelected != NULL) {
				BMenu* submenu = fSelected->Submenu();
				int submenuAction = MENU_STATE_TRACKING;
				if (submenu != NULL) {
					submenu->_SetStickyMode(_IsStickyMode());

					// The following call blocks until the submenu
					// gives control back to us, either because the mouse
					// pointer goes out of the submenu's bounds, or because
					// the user closes the menu
					BMenuItem* submenuItem = submenu->_Track(&submenuAction);
					if (submenuAction == MENU_STATE_CLOSED) {
						item = submenuItem;
						fState = MENU_STATE_CLOSED;
					} else if (submenuAction == MENU_STATE_KEY_LEAVE_SUBMENU) {
						if (LockLooper()) {
							BMenuItem* temp = fSelected;
							// close the submenu:
							_SelectItem(NULL);
							// but reselect the item itself for user:
							_SelectItem(temp, false);
							UnlockLooper();
						}
						// cancel  key-nav state
						fState = MENU_STATE_TRACKING;
					} else
						fState = MENU_STATE_TRACKING;
				}
			}
			if (!LockLooper())
				break;
		} else if ((item = _HitTestItems(location, B_ORIGIN)) != NULL) {
			_UpdateStateOpenSelect(item, location, navAreaRectAbove,
				navAreaRectBelow, selectedTime, navigationAreaTime);
			releasedOnce = true;
		} else if (_OverSuper(screenLocation)
			&& fSuper->fState != MENU_STATE_KEY_TO_SUBMENU) {
			fState = MENU_STATE_TRACKING;
			UnlockLooper();
			break;
		} else if (fState == MENU_STATE_KEY_LEAVE_SUBMENU) {
			UnlockLooper();
			break;
		} else if (fSuper == NULL
			|| fSuper->fState != MENU_STATE_KEY_TO_SUBMENU) {
			// Mouse pointer outside menu:
			// If there's no other submenu opened,
			// deselect the current selected item
			if (fSelected != NULL
				&& (fSelected->Submenu() == NULL
					|| fSelected->Submenu()->Window() == NULL)) {
				_SelectItem(NULL);
				fState = MENU_STATE_TRACKING;
			}

			if (fSuper != NULL) {
				// Give supermenu the chance to continue tracking
				*action = fState;
				UnlockLooper();
				return NULL;
			}
		}

		UnlockLooper();

		if (releasedOnce)
			_UpdateStateClose(item, location, buttons);

		if (fState != MENU_STATE_CLOSED) {
			bigtime_t snoozeAmount = 50000;

			BPoint newLocation = location;
			uint32 newButtons = buttons;

			// If user doesn't move the mouse, loop here,
			// so we don't interfere with keyboard menu navigation
			do {
				snooze(snoozeAmount);
				if (!LockLooper())
					break;
				GetMouse(&newLocation, &newButtons, true);
				UnlockLooper();
			} while (newLocation == location && newButtons == buttons
				&& !(item != NULL && item->Submenu() != NULL
					&& item->Submenu()->Window() == NULL)
				&& fState == MENU_STATE_TRACKING);

			if (newLocation != location || newButtons != buttons) {
				if (!releasedOnce && newButtons == 0 && buttons != 0)
					releasedOnce = true;
				location = newLocation;
				buttons = newButtons;
			}

			if (releasedOnce)
				_UpdateStateClose(item, location, buttons);
		}
	}

	if (action != NULL)
		*action = fState;

	// keyboard Enter will set this
	if (fChosenItem != NULL)
		item = fChosenItem;
	else if (fSelected == NULL) {
		// needed to cover (rare) mouse/ESC combination
		item = NULL;
	}

	if (fSelected != NULL && LockLooper()) {
		_SelectItem(NULL);
		UnlockLooper();
	}

	// delete the menu window recycled for all the child menus
	_DeleteMenuWindow();

	return item;
}


/**
 * @brief Recalculate the triangular navigation-area rectangles for submenu access.
 *
 * When the currently selected item has an open submenu, the user should be
 * able to move the cursor diagonally toward the submenu without accidentally
 * selecting a different item. This function computes two axis-aligned
 * rectangles that bound the triangular safe zone on each side of the cursor.
 *
 * @param position         Current cursor position in screen coordinates.
 * @param navAreaRectAbove Receives the upper navigation area rectangle.
 * @param navAreaRectBelow Receives the lower navigation area rectangle.
 * @see _UpdateStateOpenSelect()
 */
void
BMenu::_UpdateNavigationArea(BPoint position, BRect& navAreaRectAbove,
	BRect& navAreaRectBelow)
{
#define NAV_AREA_THRESHOLD    8

	// The navigation area is a region in which mouse-overs won't select
	// the item under the cursor. This makes it easier to navigate to
	// submenus, as the cursor can be moved to submenu items directly instead
	// of having to move it horizontally into the submenu first. The concept
	// is illustrated below:
	//
	// +-------+----+---------+
	// |       |   /|         |
	// |       |  /*|         |
	// |[2]--> | /**|         |
	// |       |/[4]|         |
	// |------------|         |
	// |    [1]     |   [6]   |
	// |------------|         |
	// |       |\[5]|         |
	// |[3]--> | \**|         |
	// |       |  \*|         |
	// |       |   \|         |
	// |       +----|---------+
	// |            |
	// +------------+
	//
	// [1] Selected item, cursor position ('position')
	// [2] Upper navigation area rectangle ('navAreaRectAbove')
	// [3] Lower navigation area rectangle ('navAreaRectBelow')
	// [4] Upper navigation area
	// [5] Lower navigation area
	// [6] Submenu
	//
	// The rectangles are used to calculate if the cursor is in the actual
	// navigation area (see _UpdateStateOpenSelect()).

	if (fSelected == NULL)
		return;

	BMenu* submenu = fSelected->Submenu();

	if (submenu != NULL) {
		BRect menuBounds = ConvertToScreen(Bounds());

		BRect submenuBounds;
		if (fSelected->Submenu()->LockLooper()) {
			submenuBounds = fSelected->Submenu()->ConvertToScreen(
				fSelected->Submenu()->Bounds());
			fSelected->Submenu()->UnlockLooper();
		}

		if (menuBounds.left < submenuBounds.left) {
			navAreaRectAbove.Set(position.x + NAV_AREA_THRESHOLD,
				submenuBounds.top, menuBounds.right,
				position.y);
			navAreaRectBelow.Set(position.x + NAV_AREA_THRESHOLD,
				position.y, menuBounds.right,
				submenuBounds.bottom);
		} else {
			navAreaRectAbove.Set(menuBounds.left,
				submenuBounds.top, position.x - NAV_AREA_THRESHOLD,
				position.y);
			navAreaRectBelow.Set(menuBounds.left,
				position.y, position.x - NAV_AREA_THRESHOLD,
				submenuBounds.bottom);
		}
	} else {
		navAreaRectAbove = BRect();
		navAreaRectBelow = BRect();
	}
}


/**
 * @brief Update the selection and optionally open a submenu during tracking.
 *
 * Called each tracking iteration when the cursor is over a menu item. If the
 * cursor has moved to a different item, the navigation-area logic is applied:
 * the new item is selected immediately if the cursor is outside the protection
 * zone, or after kNavigationAreaTimeout if inside. When the cursor lingers on
 * an item with a submenu longer than kOpenSubmenuDelay the submenu is opened.
 *
 * @param item               The item currently under the cursor.
 * @param position           Current cursor position in view coordinates.
 * @param navAreaRectAbove   Upper navigation-area rectangle (may be updated).
 * @param navAreaRectBelow   Lower navigation-area rectangle (may be updated).
 * @param selectedTime       Timestamp when the current item was first selected.
 * @param navigationAreaTime Timestamp when cursor entered the navigation area.
 */
void
BMenu::_UpdateStateOpenSelect(BMenuItem* item, BPoint position,
	BRect& navAreaRectAbove, BRect& navAreaRectBelow, bigtime_t& selectedTime,
	bigtime_t& navigationAreaTime)
{
	if (fState == MENU_STATE_CLOSED)
		return;

	if (item != fSelected) {
		if (navigationAreaTime == 0)
			navigationAreaTime = system_time();

		position = ConvertToScreen(position);

		bool inNavAreaRectAbove = navAreaRectAbove.Contains(position);
		bool inNavAreaRectBelow = navAreaRectBelow.Contains(position);

		if (fSelected == NULL
			|| (!inNavAreaRectAbove && !inNavAreaRectBelow)) {
			_SelectItem(item, false);
			navAreaRectAbove = BRect();
			navAreaRectBelow = BRect();
			selectedTime = system_time();
			navigationAreaTime = 0;
			return;
		}

		bool isLeft = ConvertFromScreen(navAreaRectAbove).left == 0;
		BPoint p1, p2;

		if (inNavAreaRectAbove) {
			if (!isLeft) {
				p1 = navAreaRectAbove.LeftBottom();
				p2 = navAreaRectAbove.RightTop();
			} else {
				p2 = navAreaRectAbove.RightBottom();
				p1 = navAreaRectAbove.LeftTop();
			}
		} else {
			if (!isLeft) {
				p2 = navAreaRectBelow.LeftTop();
				p1 = navAreaRectBelow.RightBottom();
			} else {
				p1 = navAreaRectBelow.RightTop();
				p2 = navAreaRectBelow.LeftBottom();
			}
		}
		bool inNavArea =
			  (p1.y - p2.y) * position.x + (p2.x - p1.x) * position.y
			+ (p1.x - p2.x) * p1.y + (p2.y - p1.y) * p1.x >= 0;

		bigtime_t systime = system_time();

		if (!inNavArea || (navigationAreaTime > 0 && systime -
			navigationAreaTime > kNavigationAreaTimeout)) {
			// Don't delay opening of submenu if the user had
			// to wait for the navigation area timeout anyway
			_SelectItem(item, inNavArea);

			if (inNavArea) {
				_UpdateNavigationArea(position, navAreaRectAbove,
					navAreaRectBelow);
			} else {
				navAreaRectAbove = BRect();
				navAreaRectBelow = BRect();
			}

			selectedTime = system_time();
			navigationAreaTime = 0;
		}
	} else if (fSelected->Submenu() != NULL &&
		system_time() - selectedTime > kOpenSubmenuDelay) {
		_SelectItem(fSelected, true);

		if (!navAreaRectAbove.IsValid() && !navAreaRectBelow.IsValid()) {
			position = ConvertToScreen(position);
			_UpdateNavigationArea(position, navAreaRectAbove,
				navAreaRectBelow);
		}
	}

	if (fState != MENU_STATE_TRACKING)
		fState = MENU_STATE_TRACKING;
}


/**
 * @brief Determine whether the tracking loop should close based on mouse state.
 *
 * In sticky mode a mouse press outside an item closes the menu; a press on an
 * item disables sticky mode. In non-sticky mode a mouse release closes the
 * menu unless the cursor is inside the clickToOpenRect, in which case the menu
 * switches to sticky mode.
 *
 * @param item    The item under the cursor, or NULL if the cursor is outside.
 * @param where   Current cursor position in view coordinates.
 * @param buttons Current mouse button bitmask from GetMouse().
 */
void
BMenu::_UpdateStateClose(BMenuItem* item, const BPoint& where,
	const uint32& buttons)
{
	if (fState == MENU_STATE_CLOSED)
		return;

	if (buttons != 0 && _IsStickyMode()) {
		if (item == NULL) {
			if (item != fSelected && LockLooper()) {
				_SelectItem(item, false);
				UnlockLooper();
			}
			fState = MENU_STATE_CLOSED;
		} else
			_SetStickyMode(false);
	} else if (buttons == 0 && !_IsStickyMode()) {
		if (fExtraRect != NULL && fExtraRect->Contains(where)) {
			_SetStickyMode(true);
			fExtraRect = NULL;
				// Setting this to NULL will prevent this code
				// to be executed next time
		} else {
			if (item != fSelected && LockLooper()) {
				_SelectItem(item, false);
				UnlockLooper();
			}
			fState = MENU_STATE_CLOSED;
		}
	}
}


/**
 * @brief Low-level helper that inserts an item into the fItems list.
 *
 * Installs the item on the appropriate BWindow and sets its supermenu pointer.
 * Does not perform any layout or redraw.
 *
 * @param item  The item to insert; must not be NULL.
 * @param index Zero-based insertion position.
 * @return true on success, false if the index is out of range or the list
 *         operation fails.
 */
bool
BMenu::_AddItem(BMenuItem* item, int32 index)
{
	ASSERT(item != NULL);
	if (index < 0 || index > fItems.CountItems())
		return false;

	if (item->IsMarked())
		_ItemMarked(item);

	if (!fItems.AddItem(item, index))
		return false;

	// install the item on the supermenu's window
	// or onto our window, if we are a root menu
	BWindow* window = NULL;
	if (Superitem() != NULL)
		window = Superitem()->fWindow;
	else
		window = Window();
	if (window != NULL)
		item->Install(window);

	item->SetSuper(this);
	return true;
}


/**
 * @brief Low-level helper that removes items from fItems.
 *
 * When @p item is non-NULL that specific item is removed and @p index and
 * @p count are ignored. Otherwise items in the half-open range
 * [@p index, @p index + @p count) are removed. Each removed item is
 * uninstalled from its window and its supermenu pointer is cleared. If
 * @p deleteItems is true the items are also deleted.
 *
 * @param index       Start of the range to remove (ignored when @p item is set).
 * @param count       Number of items to remove (ignored when @p item is set).
 * @param item        Specific item to remove, or NULL to use the range.
 * @param deleteItems If true, each removed item is deleted.
 * @return true if all intended removals succeeded.
 */
bool
BMenu::_RemoveItems(int32 index, int32 count, BMenuItem* item,
	bool deleteItems)
{
	bool success = false;
	bool invalidateLayout = false;

	bool locked = LockLooper();
	BWindow* window = Window();

	// The plan is simple: If we're given a BMenuItem directly, we use it
	// and ignore index and count. Otherwise, we use them instead.
	if (item != NULL) {
		if (fItems.RemoveItem(item)) {
			if (item == fSelected && window != NULL)
				_SelectItem(NULL);
			item->Uninstall();
			item->SetSuper(NULL);
			if (deleteItems)
				delete item;
			success = invalidateLayout = true;
		}
	} else {
		// We iterate backwards because it's simpler
		int32 i = std::min(index + count - 1, fItems.CountItems() - 1);
		// NOTE: the range check for "index" is done after
		// calculating the last index to be removed, so
		// that the range is not "shifted" unintentionally
		index = std::max((int32)0, index);
		for (; i >= index; i--) {
			item = static_cast<BMenuItem*>(fItems.ItemAt(i));
			if (item != NULL) {
				if (fItems.RemoveItem(i)) {
					if (item == fSelected && window != NULL)
						_SelectItem(NULL);
					item->Uninstall();
					item->SetSuper(NULL);
					if (deleteItems)
						delete item;
					success = true;
					invalidateLayout = true;
				} else {
					// operation not entirely successful
					success = false;
					break;
				}
			}
		}
	}

	if (invalidateLayout) {
		InvalidateLayout();
		if (locked && window != NULL) {
			_LayoutItems(0);
			_UpdateWindowViewSize(false);
			Invalidate();
		}
	}

	if (locked)
		UnlockLooper();

	return success;
}


/**
 * @brief Recompute the item layout if the cached layout is stale.
 *
 * Clears the stale flag, refreshes the font info cache, performs a full layout
 * pass, and updates the window/view size.
 *
 * @return true if a layout pass was performed, false if the cache was still
 *         valid and nothing needed to be done.
 */
bool
BMenu::_RelayoutIfNeeded()
{
	if (!fUseCachedMenuLayout) {
		fUseCachedMenuLayout = true;
		_CacheFontInfo();
		_LayoutItems(0);
		_UpdateWindowViewSize(false);
		return true;
	}
	return false;
}


/**
 * @brief Compute trigger characters and lay out items starting from @p index.
 *
 * Calls _CalcTriggers() to (re)assign keyboard triggers, then _ComputeLayout()
 * to calculate item positions and the menu's required size. If fResizeToFit
 * is true the menu view is resized to exactly fit the computed dimensions.
 *
 * @param index Zero-based index of the first item whose position may have
 *              changed; currently the full layout is always recomputed.
 */
void
BMenu::_LayoutItems(int32 index)
{
	_CalcTriggers();

	float width;
	float height;
	_ComputeLayout(index, fResizeToFit, true, &width, &height);

	if (fResizeToFit)
		ResizeTo(width, height);
}


/**
 * @brief Ensure the preferred-size cache is current and return it.
 *
 * If the cached size is unset or the resizing mode has changed since the last
 * computation, _ComputeLayout() is called to refresh it.
 *
 * @return The validated preferred BSize.
 */
BSize
BMenu::_ValidatePreferredSize()
{
	if (!fLayoutData->preferred.IsWidthSet() || ResizingMode()
			!= fLayoutData->lastResizingMode) {
		_ComputeLayout(0, true, false, NULL, NULL);
		ResetLayoutInvalidation();
	}

	return fLayoutData->preferred;
}


/**
 * @brief Compute the overall menu geometry by delegating to the layout method
 *        appropriate for the current fLayout value.
 *
 * Dispatches to _ComputeColumnLayout(), _ComputeRowLayout(), or
 * _ComputeMatrixLayout(). Adjusts the width for B_FOLLOW_LEFT_RIGHT resizing
 * mode. Optionally writes the resulting size through @p _width / @p _height
 * and stores it in fLayoutData->preferred when @p bestFit is true.
 *
 * @param index      First item index that may have changed (currently unused;
 *                   the full layout is always recomputed).
 * @param bestFit    If true, store the result in the preferred-size cache.
 * @param moveItems  If true, update each item's fBounds to the new position.
 * @param _width     If non-NULL, receives the computed width.
 * @param _height    If non-NULL, receives the computed height.
 */
void
BMenu::_ComputeLayout(int32 index, bool bestFit, bool moveItems,
	float* _width, float* _height)
{
	// TODO: Take "bestFit", "moveItems", "index" into account,
	// Recalculate only the needed items,
	// not the whole layout every time

	fLayoutData->lastResizingMode = ResizingMode();

	BRect frame;
	switch (fLayout) {
		case B_ITEMS_IN_COLUMN:
		{
			BRect parentFrame;
			BRect* overrideFrame = NULL;
			if (dynamic_cast<_BMCMenuBar_*>(Supermenu()) != NULL) {
				// When the menu is modified while it's open, we get here in a
				// situation where trying to lock the looper would deadlock
				// (the window is locked waiting for the menu to terminate).
				// In that case, just give up on getting the supermenu bounds
				// and keep the menu at the current width and position.
				if (Supermenu()->LockLooperWithTimeout(0) == B_OK) {
					parentFrame = Supermenu()->Bounds();
					Supermenu()->UnlockLooper();
					overrideFrame = &parentFrame;
				}
			}

			_ComputeColumnLayout(index, bestFit, moveItems, overrideFrame,
				frame);
			break;
		}

		case B_ITEMS_IN_ROW:
			_ComputeRowLayout(index, bestFit, moveItems, frame);
			break;

		case B_ITEMS_IN_MATRIX:
			_ComputeMatrixLayout(frame);
			break;
	}

	// change width depending on resize mode
	BSize size;
	if ((ResizingMode() & B_FOLLOW_LEFT_RIGHT) == B_FOLLOW_LEFT_RIGHT) {
		if (dynamic_cast<_BMCMenuBar_*>(this) != NULL)
			size.width = Bounds().Width() - fPad.right;
		else if (Parent() != NULL)
			size.width = Parent()->Frame().Width();
		else if (Window() != NULL)
			size.width = Window()->Frame().Width();
		else
			size.width = Bounds().Width();
	} else
		size.width = frame.Width();

	size.height = frame.Height();

	if (_width)
		*_width = size.width;

	if (_height)
		*_height = size.height;

	if (bestFit)
		fLayoutData->preferred = size;

	if (moveItems)
		fUseCachedMenuLayout = true;
}


/**
 * @brief Compute item geometry for a B_ITEMS_IN_COLUMN menu.
 *
 * Sets each item's top, bottom, and left coordinates and computes the
 * required width accounting for shortcut modifier icons and submenu arrows.
 * Also applies fMaxContentWidth if set.
 *
 * @param index         First item index to lay out (items before this are
 *                      assumed already positioned).
 * @param bestFit       Unused in the current implementation.
 * @param moveItems     If true, item fBounds.right values are set to the
 *                      computed menu width.
 * @param overrideFrame If non-NULL, use its right edge as the initial frame
 *                      width (used when parented by a _BMCMenuBar_).
 * @param frame         Output: bounding rectangle enclosing all items.
 */
void
BMenu::_ComputeColumnLayout(int32 index, bool bestFit, bool moveItems,
	BRect* overrideFrame, BRect& frame)
{
	bool command = false;
	bool control = false;
	bool shift = false;
	bool option = false;
	bool submenu = false;

	if (index > 0)
		frame = ItemAt(index - 1)->Frame();
	else if (overrideFrame != NULL)
		frame.Set(0, 0, overrideFrame->right, -1);
	else
		frame.Set(0, 0, 0, -1);

	BFont font;
	GetFont(&font);

	// Loop over all items to set their top, bottom and left coordinates,
	// all while computing the width of the menu
	for (; index < fItems.CountItems(); index++) {
		BMenuItem* item = ItemAt(index);

		float width;
		float height;
		item->GetContentSize(&width, &height);

		if (item->fModifiers && item->fShortcutChar) {
			width += font.Size();
			if ((item->fModifiers & B_COMMAND_KEY) != 0)
				command = true;

			if ((item->fModifiers & B_CONTROL_KEY) != 0)
				control = true;

			if ((item->fModifiers & B_SHIFT_KEY) != 0)
				shift = true;

			if ((item->fModifiers & B_OPTION_KEY) != 0)
				option = true;
		}

		item->fBounds.left = 0.0f;
		item->fBounds.top = frame.bottom + 1.0f;
		item->fBounds.bottom = item->fBounds.top + height + fPad.top
			+ fPad.bottom;

		if (item->fSubmenu != NULL)
			submenu = true;

		frame.right = std::max(frame.right, width + fPad.left + fPad.right);
		frame.bottom = item->fBounds.bottom;
	}

	// Compute the extra space needed for shortcuts and submenus
	if (command) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemCommand()->Bounds().Width() + 1;
	}
	if (control) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemControl()->Bounds().Width() + 1;
	}
	if (option) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemOption()->Bounds().Width() + 1;
	}
	if (shift) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemShift()->Bounds().Width() + 1;
	}
	if (submenu) {
		frame.right += ItemAt(0)->Frame().Height() / 2;
		fHasSubmenus = true;
	} else {
		fHasSubmenus = false;
	}

	if (fMaxContentWidth > 0)
		frame.right = std::min(frame.right, fMaxContentWidth);

	frame.top = 0;
	frame.right = ceilf(frame.right);

	// Finally update the "right" coordinate of all items
	if (moveItems) {
		for (int32 i = 0; i < fItems.CountItems(); i++)
			ItemAt(i)->fBounds.right = frame.right;
	}
}


/**
 * @brief Compute item geometry for a B_ITEMS_IN_ROW menu (e.g. BMenuBar).
 *
 * Positions items left-to-right. All items share the same height derived from
 * the font metrics and fPad. When @p bestFit is true the computed width is
 * used; otherwise the current view width is kept.
 *
 * @param index     First item index to lay out.
 * @param bestFit   If true, use the computed width; otherwise keep Bounds().right.
 * @param moveItems If true, set each item's fBounds.bottom to the row height.
 * @param frame     Output: bounding rectangle enclosing all items.
 */
void
BMenu::_ComputeRowLayout(int32 index, bool bestFit, bool moveItems,
	BRect& frame)
{
	font_height fh;
	GetFontHeight(&fh);
	frame.Set(0.0f, 0.0f, 0.0f, ceilf(fh.ascent + fh.descent + fPad.top
		+ fPad.bottom));

	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		float width, height;
		item->GetContentSize(&width, &height);

		item->fBounds.left = frame.right;
		item->fBounds.top = 0.0f;
		item->fBounds.right = item->fBounds.left + width + fPad.left
			+ fPad.right;

		frame.right = item->Frame().right + 1.0f;
		frame.bottom = std::max(frame.bottom, height + fPad.top + fPad.bottom);
	}

	if (moveItems) {
		for (int32 i = 0; i < fItems.CountItems(); i++)
			ItemAt(i)->fBounds.bottom = frame.bottom;
	}

	if (bestFit)
		frame.right = ceilf(frame.right);
	else
		frame.right = Bounds().right;
}


/**
 * @brief Compute the bounding frame for a B_ITEMS_IN_MATRIX menu.
 *
 * Items in matrix menus have pre-assigned fBounds set by the caller. This
 * function computes the union of all item frames to determine the minimum
 * enclosing rectangle.
 *
 * @param frame Output: bounding rectangle that encloses all item frames.
 */
void
BMenu::_ComputeMatrixLayout(BRect &frame)
{
	frame.Set(0, 0, 0, 0);
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item != NULL) {
			frame.left = std::min(frame.left, item->Frame().left);
			frame.right = std::max(frame.right, item->Frame().right);
			frame.top = std::min(frame.top, item->Frame().top);
			frame.bottom = std::max(frame.bottom, item->Frame().bottom);
		}
	}
}


/**
 * @brief Respond to a layout invalidation from the BLayout system.
 *
 * Clears both the cached menu layout flag and the stored preferred size so
 * they will be recomputed on the next layout request.
 *
 * @param descendants True if descendant layouts were also invalidated.
 */
void
BMenu::LayoutInvalidated(bool descendants)
{
	fUseCachedMenuLayout = false;
	fLayoutData->preferred.Set(B_SIZE_UNSET, B_SIZE_UNSET);
}


/**
 * @brief Return the screen position at which this menu should appear.
 *
 * For column-layout supermenus the menu opens to the right of the superitem;
 * for row-layout supermenus it opens below it. The supermenu must be locked
 * by the caller because this method calls ConvertToScreen().
 *
 * @return The screen coordinate of the menu's top-left corner.
 * @note Subclasses (e.g. BPopUpMenu) override this to place the menu at an
 *       arbitrary screen position.
 */
BPoint
BMenu::ScreenLocation()
{
	BMenu* superMenu = Supermenu();
	BMenuItem* superItem = Superitem();

	if (superMenu == NULL || superItem == NULL) {
		debugger("BMenu can't determine where to draw."
			"Override BMenu::ScreenLocation() to determine location.");
	}

	BPoint point;
	if (superMenu->Layout() == B_ITEMS_IN_COLUMN)
		point = superItem->Frame().RightTop() + BPoint(1.0f, 1.0f);
	else
		point = superItem->Frame().LeftBottom() + BPoint(1.0f, 1.0f);

	superMenu->ConvertToScreen(&point);

	return point;
}


/**
 * @brief Calculate the on-screen frame for the menu window, keeping it visible.
 *
 * Starts from @p where and adjusts the frame so it stays within the screen
 * bounds. Handles edge cases for menu fields, column menus (opens left if no
 * room on the right), and row menus (opens upward if no room below). Sets
 * fExtraMenuData->frameShiftedLeft if the frame was nudged leftward.
 *
 * @param where    Desired top-left position of the menu in screen coordinates.
 * @param scrollOn If non-NULL, receives true when the menu is taller than the
 *                 available screen space and scroll bars are needed.
 * @return The adjusted screen frame for the menu window.
 */
BRect
BMenu::_CalcFrame(BPoint where, bool* scrollOn)
{
	// TODO: Improve me
	BRect bounds = Bounds();
	BRect frame = bounds.OffsetToCopy(where);

	BScreen screen(Window());
	BRect screenFrame = screen.Frame();

	BMenu* superMenu = Supermenu();
	BMenuItem* superItem = Superitem();

	// Reset frame shifted state since this menu is being redrawn
	fExtraMenuData->frameShiftedLeft = false;

	// TODO: Horrible hack:
	// When added to a BMenuField, a BPopUpMenu is the child of
	// a _BMCMenuBar_ to "fake" the menu hierarchy
	bool inMenuField = dynamic_cast<_BMCMenuBar_*>(superMenu) != NULL;

	// Offset the menu field menu window left by the width of the checkmark
	// so that the text when the menu is closed lines up with the text when
	// the menu is open.
	if (inMenuField)
		frame.OffsetBy(-8.0f, 0.0f);

	if (superMenu == NULL || superItem == NULL || inMenuField) {
		// just move the window on screen
		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
		else if (frame.top < screenFrame.top)
			frame.OffsetBy(0, -frame.top);

		if (frame.right > screenFrame.right) {
			frame.OffsetBy(screenFrame.right - frame.right, 0);
			fExtraMenuData->frameShiftedLeft = true;
		}
		else if (frame.left < screenFrame.left)
			frame.OffsetBy(-frame.left, 0);
	} else if (superMenu->Layout() == B_ITEMS_IN_COLUMN) {
		if (frame.right > screenFrame.right
				|| superMenu->fExtraMenuData->frameShiftedLeft) {
			frame.OffsetBy(-superItem->Frame().Width() - frame.Width() - 2, 0);
			fExtraMenuData->frameShiftedLeft = true;
		}

		if (frame.left < 0)
			frame.OffsetBy(-frame.left + 6, 0);

		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	} else {
		if (frame.bottom > screenFrame.bottom) {
			float spaceBelow = screenFrame.bottom - frame.top;
			float spaceOver = frame.top - screenFrame.top
				- superItem->Frame().Height();
			if (spaceOver > spaceBelow) {
				frame.OffsetBy(0, -superItem->Frame().Height()
					- frame.Height() - 3);
			}
		}

		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
	}

	if (scrollOn != NULL) {
		// basically, if this returns false, it means
		// that the menu frame won't fit completely inside the screen
		// TODO: Scrolling will currently only work up/down,
		// not left/right
		*scrollOn = screenFrame.top > frame.top
			|| screenFrame.bottom < frame.bottom;
	}

	return frame;
}


/**
 * @brief Draw all items whose frame intersects the given update rectangle.
 * @param updateRect The dirty region in view coordinates.
 */
void
BMenu::DrawItems(BRect updateRect)
{
	int32 itemCount = fItems.CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Frame().Intersects(updateRect))
			item->Draw();
	}
}


/**
 * @brief Return the current tracking state, recursing into open submenus.
 *
 * If a submenu is open and tracking, its state is returned instead of this
 * menu's state.
 *
 * @param item Unused output pointer (reserved for future use).
 * @return The effective MENU_STATE_* constant.
 */
int
BMenu::_State(BMenuItem** item) const
{
	if (fState == MENU_STATE_TRACKING || fState == MENU_STATE_CLOSED)
		return fState;

	if (fSelected != NULL && fSelected->Submenu() != NULL)
		return fSelected->Submenu()->_State(item);

	return fState;
}


/**
 * @brief Invoke a menu item, playing the selection animation first.
 *
 * Briefly highlights and un-highlights the item to provide visual feedback,
 * then locks the root menu's looper and calls BMenuItem::Invoke(). Disabled
 * items are silently ignored.
 *
 * @param item The item to invoke; must not be NULL.
 * @param now  Currently unused; reserved for future synchronous invocation.
 */
void
BMenu::_InvokeItem(BMenuItem* item, bool now)
{
	if (!item->IsEnabled())
		return;

	// Do the "selected" animation
	// TODO: Doesn't work. This is supposed to highlight
	// and dehighlight the item, works on beos but not on haiku.
	if (!item->Submenu() && LockLooper()) {
		snooze(50000);
		item->Select(true);
		Window()->UpdateIfNeeded();
		snooze(50000);
		item->Select(false);
		Window()->UpdateIfNeeded();
		snooze(50000);
		item->Select(true);
		Window()->UpdateIfNeeded();
		snooze(50000);
		item->Select(false);
		Window()->UpdateIfNeeded();
		UnlockLooper();
	}

	// Lock the root menu window before calling BMenuItem::Invoke()
	BMenu* parent = this;
	BMenu* rootMenu = NULL;
	do {
		rootMenu = parent;
		parent = rootMenu->Supermenu();
	} while (parent != NULL);

	if (rootMenu->LockLooper()) {
		item->Invoke();
		rootMenu->UnlockLooper();
	}
}


/**
 * @brief Test whether a screen point lies within the supermenu's bounds.
 * @param location Screen coordinate to test.
 * @return true if @p location is inside fSuperbounds, false otherwise or if
 *         there is no supermenu.
 */
bool
BMenu::_OverSuper(BPoint location)
{
	if (!Supermenu())
		return false;

	return fSuperbounds.Contains(location);
}


/**
 * @brief Test whether a screen point lies within @p item's open submenu.
 *
 * Checks the submenu's window frame and recursively checks the submenu's own
 * selected sub-submenu, so deeply nested menus are handled correctly.
 *
 * @param item The item whose submenu is to be tested, or NULL.
 * @param loc  Screen coordinate to test.
 * @return true if @p loc is inside the submenu or any of its open children.
 */
bool
BMenu::_OverSubmenu(BMenuItem* item, BPoint loc)
{
	if (item == NULL)
		return false;

	BMenu* subMenu = item->Submenu();
	if (subMenu == NULL || subMenu->Window() == NULL)
		return false;

	// assume that loc is in screen coordinates
	if (subMenu->Window()->Frame().Contains(loc))
		return true;

	return subMenu->_OverSubmenu(subMenu->fSelected, loc);
}


/**
 * @brief Retrieve or create the cached BMenuWindow used by submenus.
 *
 * The window is created lazily on the first call and is reused for all child
 * submenus to avoid repeated window creation overhead.
 *
 * @return The cached BMenuWindow, or NULL if allocation failed.
 * @see _DeleteMenuWindow()
 */
BMenuWindow*
BMenu::_MenuWindow()
{
	if (fCachedMenuWindow == NULL) {
		char windowName[64];
		snprintf(windowName, 64, "%s cached menu", Name());
		fCachedMenuWindow = new (nothrow) BMenuWindow(windowName);
	}

	return fCachedMenuWindow;
}


/**
 * @brief Destroy the cached BMenuWindow and set the pointer to NULL.
 *
 * Locks the window, calls Quit() to destroy it asynchronously, and clears
 * fCachedMenuWindow. Safe to call even when no window is cached.
 *
 * @see _MenuWindow()
 */
void
BMenu::_DeleteMenuWindow()
{
	if (fCachedMenuWindow != NULL) {
		fCachedMenuWindow->Lock();
		fCachedMenuWindow->Quit();
		fCachedMenuWindow = NULL;
	}
}


/**
 * @brief Return the non-separator item whose frame contains the given point.
 *
 * Returns NULL if @p where is outside the menu bounds or if only a
 * BSeparatorItem is at that position.
 *
 * @param where View-coordinate point to test.
 * @param slop  Extra hit-test tolerance (currently ignored).
 * @return The hit BMenuItem, or NULL if no eligible item is at @p where.
 */
BMenuItem*
BMenu::_HitTestItems(BPoint where, BPoint slop) const
{
	// TODO: Take "slop" into account ?

	// if the point doesn't lie within the menu's
	// bounds, bail out immediately
	if (!Bounds().Contains(where))
		return NULL;

	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Frame().Contains(where)
			&& dynamic_cast<BSeparatorItem*>(item) == NULL) {
			return item;
		}
	}

	return NULL;
}


/**
 * @brief Return the cached screen bounds of the supermenu.
 *
 * Updated at the start of _Show() via ConvertToScreen(). Used by _OverSuper()
 * to detect when the cursor has returned to the parent menu.
 *
 * @return The supermenu's bounding rectangle in screen coordinates.
 */
BRect
BMenu::_Superbounds() const
{
	return fSuperbounds;
}


/**
 * @brief Cache the current font's ascent, descent, and combined height.
 *
 * Results are stored in fAscent, fDescent, and fFontHeight for use during
 * item layout and drawing without repeated calls to GetFontHeight().
 */
void
BMenu::_CacheFontInfo()
{
	font_height fh;
	GetFontHeight(&fh);
	fAscent = fh.ascent;
	fDescent = fh.descent;
	fFontHeight = ceilf(fh.ascent + fh.descent + fh.leading);
}


/**
 * @brief Handle a mark change on the given item.
 *
 * In radio mode, all other items are unmarked. If IsLabelFromMarked() is true,
 * the superitem's label is updated to the newly marked item's label.
 *
 * @param item The item that was just marked.
 */
void
BMenu::_ItemMarked(BMenuItem* item)
{
	if (IsRadioMode()) {
		for (int32 i = 0; i < CountItems(); i++) {
			if (ItemAt(i) != item)
				ItemAt(i)->SetMarked(false);
		}
	}

	if (IsLabelFromMarked() && Superitem() != NULL)
		Superitem()->SetLabel(item->Label());
}


/**
 * @brief Install all child items onto a BWindow's shortcut handler.
 * @param target The window on which items should be installed.
 */
void
BMenu::_Install(BWindow* target)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Install(target);
}


/**
 * @brief Uninstall all child items from their current BWindow.
 */
void
BMenu::_Uninstall()
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Uninstall();
}


/**
 * @brief Change the currently highlighted (selected) item.
 *
 * Deselects the previously selected item and closes its submenu if open.
 * Selects the new @p item and, if @p showSubmenu is true and the item has a
 * submenu, opens that submenu via _Show().
 *
 * @param item            The item to select, or NULL to deselect all.
 * @param showSubmenu     If true and @p item has a submenu, open it.
 * @param selectFirstItem If true, pre-select the first item in the submenu.
 * @param keyDown         True when navigation is keyboard-driven.
 */
void
BMenu::_SelectItem(BMenuItem* item, bool showSubmenu, bool selectFirstItem, bool keyDown)
{
	// Avoid deselecting and reselecting the same item which would cause flickering.
	if (item != fSelected) {
		if (fSelected != NULL) {
			fSelected->Select(false);
			BMenu* subMenu = fSelected->Submenu();
			if (subMenu != NULL && subMenu->Window() != NULL)
				subMenu->_Hide();
		}

		fSelected = item;
		if (fSelected != NULL)
			fSelected->Select(true);
	}

	if (fSelected != NULL && showSubmenu) {
		BMenu* subMenu = fSelected->Submenu();
		if (subMenu != NULL && subMenu->Window() == NULL) {
			if (!subMenu->_Show(selectFirstItem, keyDown)) {
				// something went wrong, deselect the item
				fSelected->Select(false);
				fSelected = NULL;
			}
		}
	}
}


/**
 * @brief Move the selection to the next or previous enabled item.
 *
 * Wraps around at the ends of the list. After selecting the item the system
 * cursor is hidden to indicate keyboard navigation.
 *
 * @param item    The currently selected item, or NULL.
 * @param forward If true, select the next item; if false, select the previous.
 * @return true if a new item was selected, false if the menu is empty or no
 *         other enabled item could be found.
 */
bool
BMenu::_SelectNextItem(BMenuItem* item, bool forward)
{
	if (CountItems() == 0) // cannot select next item in an empty menu
		return false;

	BMenuItem* nextItem = _NextItem(item, forward);
	if (nextItem == NULL)
		return false;

	_SelectItem(nextItem, dynamic_cast<BMenuBar*>(this) != NULL);

	if (LockLooper()) {
		be_app->ObscureCursor();
		UnlockLooper();
	}

	return true;
}


/**
 * @brief Find the next enabled item in the given direction, wrapping around.
 *
 * Skips disabled items. If no enabled item exists other than the current one,
 * returns NULL.
 *
 * @param item    Starting item; the search begins one step past this item.
 * @param forward If true, search forward (increasing index); otherwise backward.
 * @return The next enabled BMenuItem, or NULL if none is available.
 */
BMenuItem*
BMenu::_NextItem(BMenuItem* item, bool forward) const
{
	const int32 numItems = fItems.CountItems();
	if (numItems == 0)
		return NULL;

	int32 index = fItems.IndexOf(item);
	int32 loopCount = numItems;
	while (--loopCount) {
		// Cycle through menu items in the given direction...
		if (forward)
			index++;
		else
			index--;

		// ... wrap around...
		if (index < 0)
			index = numItems - 1;
		else if (index >= numItems)
			index = 0;

		// ... and return the first suitable item found.
		BMenuItem* nextItem = ItemAt(index);
		if (nextItem->IsEnabled())
			return nextItem;
	}

	// If no other suitable item was found, return NULL.
	return NULL;
}


/**
 * @brief Set the sticky (click-to-open) mode on this menu and its ancestors.
 *
 * Sticky mode means the menu stays open after the mouse button is released.
 * The flag is propagated up the menu hierarchy. When enabling sticky mode on
 * the root BMenuBar, focus is stolen from the current focus view so that
 * keyboard events are delivered to the menu.
 *
 * @param sticky true to enter sticky mode, false to leave it.
 */
void
BMenu::_SetStickyMode(bool sticky)
{
	if (fStickyMode == sticky)
		return;

	fStickyMode = sticky;

	if (fSuper != NULL) {
		// propagate the status to the super menu
		fSuper->_SetStickyMode(sticky);
	} else {
		// TODO: Ugly hack, but it needs to be done in this method
		BMenuBar* menuBar = dynamic_cast<BMenuBar*>(this);
		if (sticky && menuBar != NULL && menuBar->LockLooper()) {
			// If we are switching to sticky mode,
			// steal the focus from the current focus view
			// (needed to handle keyboard navigation)
			menuBar->_StealFocus();
			menuBar->UnlockLooper();
		}
	}
}


/**
 * @brief Return whether this menu is currently in sticky mode.
 * @return true if sticky mode is active.
 */
bool
BMenu::_IsStickyMode() const
{
	return fStickyMode;
}


/**
 * @brief Read the left Shift key code into @p value.
 *
 * Falls back to the hardware default (0x4b) if get_modifier_key() fails.
 * Cannot be moved to init_interface_kit() because get_modifier_key() would
 * deadlock during input_server startup.
 *
 * @param value Receives the key code.
 */
void
BMenu::_GetShiftKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_SHIFT_KEY, &value) != B_OK)
		value = 0x4b;
}


/**
 * @brief Read the left Control key code into @p value.
 *
 * Falls back to the hardware default (0x5c) if get_modifier_key() fails.
 *
 * @param value Receives the key code.
 */
void
BMenu::_GetControlKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_CONTROL_KEY, &value) != B_OK)
		value = 0x5c;
}


/**
 * @brief Read the left Command key code into @p value.
 *
 * Falls back to the hardware default (0x66) if get_modifier_key() fails.
 *
 * @param value Receives the key code.
 */
void
BMenu::_GetCommandKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_COMMAND_KEY, &value) != B_OK)
		value = 0x66;
}


/**
 * @brief Read the left Option key code into @p value.
 *
 * Falls back to the hardware default (0x5d) if get_modifier_key() fails.
 *
 * @param value Receives the key code.
 */
void
BMenu::_GetOptionKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_OPTION_KEY, &value) != B_OK)
		value = 0x5d;
}


/**
 * @brief Read the Menu key code into @p value.
 *
 * Falls back to the hardware default (0x68) if get_modifier_key() fails.
 *
 * @param value Receives the key code.
 */
void
BMenu::_GetMenuKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_MENU_KEY, &value) != B_OK)
		value = 0x68;
}


/**
 * @brief Assign keyboard trigger characters to all items that lack one.
 *
 * First collects all manually set triggers via BMenuItem::Trigger() to avoid
 * duplicates. Then iterates over items without a trigger and calls
 * _ChooseTrigger() to find the best available character in the item's label.
 */
void
BMenu::_CalcTriggers()
{
	BPrivate::TriggerList triggerList;

	// Gathers the existing triggers set by the user
	for (int32 i = 0; i < CountItems(); i++) {
		char trigger = ItemAt(i)->Trigger();
		if (trigger != 0)
			triggerList.AddTrigger(trigger);
	}

	// Set triggers for items which don't have one yet
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Trigger() == 0) {
			uint32 trigger;
			int32 index;
			if (_ChooseTrigger(item->Label(), index, trigger, triggerList))
				item->SetAutomaticTrigger(index, trigger);
		}
	}
}


/**
 * @brief Choose the best trigger character for an item label.
 *
 * Makes two passes through the label: first prefers alphanumeric ASCII
 * characters that are not already taken; then falls back to any non-space
 * character. The chosen character is registered in @p triggers.
 *
 * @param title    The item label text (UTF-8 encoded); may be NULL.
 * @param index    Receives the byte offset of the chosen character in @p title.
 * @param trigger  Receives the lowercase Unicode code point of the trigger.
 * @param triggers The list of already-claimed triggers; updated on success.
 * @return true if a trigger character was found and registered, false otherwise.
 */
bool
BMenu::_ChooseTrigger(const char* title, int32& index, uint32& trigger,
	BPrivate::TriggerList& triggers)
{
	if (title == NULL)
		return false;

	index = 0;
	uint32 c;
	const char* nextCharacter, *character;

	// two runs: first we look out for alphanumeric ASCII characters
	nextCharacter = title;
	character = nextCharacter;
	while ((c = BUnicodeChar::FromUTF8(&nextCharacter)) != 0) {
		if (!(c < 128 && BUnicodeChar::IsAlNum(c)) || triggers.HasTrigger(c)) {
			character = nextCharacter;
			continue;
		}
		trigger = BUnicodeChar::ToLower(c);
		index = (int32)(character - title);
		return triggers.AddTrigger(c);
	}

	// then, if we still haven't found something, we accept anything
	nextCharacter = title;
	character = nextCharacter;
	while ((c = BUnicodeChar::FromUTF8(&nextCharacter)) != 0) {
		if (BUnicodeChar::IsSpace(c) || triggers.HasTrigger(c)) {
			character = nextCharacter;
			continue;
		}
		trigger = BUnicodeChar::ToLower(c);
		index = (int32)(character - title);
		return triggers.AddTrigger(c);
	}

	return false;
}


/**
 * @brief Resize the BMenuWindow to fit the menu content, adding scroll bars
 *        if the menu is taller than the screen.
 *
 * Has no effect for BMenuBar submenus or menus with fResizeToFit == false.
 * When @p move is true, also repositions the window using ScreenLocation().
 * Attaches scroll bars to the window when the content overflows the screen,
 * and scrolls to the marked item if one exists.
 *
 * @param move If true, move the window to the correct screen position as well.
 */
void
BMenu::_UpdateWindowViewSize(const bool &move)
{
	BMenuWindow* window = static_cast<BMenuWindow*>(Window());
	if (window == NULL)
		return;

	if (dynamic_cast<BMenuBar*>(this) != NULL)
		return;

	if (!fResizeToFit)
		return;

	bool scroll = false;
	const BPoint screenLocation = move ? ScreenLocation()
		: window->Frame().LeftTop();
	BRect frame = _CalcFrame(screenLocation, &scroll);
	ResizeTo(frame.Width(), frame.Height());

	if (fItems.CountItems() > 0) {
		if (!scroll) {
			if (fLayout == B_ITEMS_IN_COLUMN)
				window->DetachScrollers();

			window->ResizeTo(Bounds().Width(), Bounds().Height());
		} else {

			// Resize the window to fit the screen without overflowing the
			// frame, and attach scrollers to our cached BMenuWindow.
			BScreen screen(window);
			frame = frame & screen.Frame();
			window->ResizeTo(Bounds().Width(), frame.Height());

			// we currently only support scrolling for B_ITEMS_IN_COLUMN
			if (fLayout == B_ITEMS_IN_COLUMN) {
				window->AttachScrollers();

				BMenuItem* selectedItem = FindMarked();
				if (selectedItem != NULL) {
					// scroll to the selected item
					if (Supermenu() == NULL) {
						window->TryScrollTo(selectedItem->Frame().top);
					} else {
						BPoint point = selectedItem->Frame().LeftTop();
						BPoint superPoint = Superitem()->Frame().LeftTop();
						Supermenu()->ConvertToScreen(&superPoint);
						ConvertToScreen(&point);
						window->TryScrollTo(point.y - superPoint.y);
					}
				}
			}
		}
	} else {
		_CacheFontInfo();
		window->ResizeTo(StringWidth(BPrivate::kEmptyMenuLabel)
				+ fPad.left + fPad.right,
			fFontHeight + fPad.top + fPad.bottom);
	}

	if (move)
		window->MoveTo(frame.LeftTop());
}


/**
 * @brief Drive the AddDynamicItem() callback sequence to populate the menu.
 *
 * Calls AddDynamicItem(B_INITIAL_ADD) to start the sequence. If it returns
 * true, keeps calling AddDynamicItem(B_PROCESSING) until it returns false or
 * _OkToProceed() indicates the user has moved away. On cancellation,
 * AddDynamicItem(B_ABORT) is invoked.
 *
 * @param keyDown True when the menu is being opened via keyboard navigation;
 *                passed to _OkToProceed() to relax the mouse-position check.
 * @return true if dynamic item addition was aborted, false on normal completion.
 */
bool
BMenu::_AddDynamicItems(bool keyDown)
{
	bool addAborted = false;
	if (AddDynamicItem(B_INITIAL_ADD)) {
		BMenuItem* superItem = Superitem();
		BMenu* superMenu = Supermenu();
		do {
			if (superMenu != NULL
				&& !superMenu->_OkToProceed(superItem, keyDown)) {
				AddDynamicItem(B_ABORT);
				addAborted = true;
				break;
			}
		} while (AddDynamicItem(B_PROCESSING));
	}

	return addAborted;
}


/**
 * @brief Check whether the dynamic-item add loop should continue.
 *
 * Returns false if the user has pressed or released the mouse button in a way
 * that indicates they want to cancel, or if the pointer has moved off the
 * superitem. For keyboard navigation (@p keyDown true) the pointer position
 * check is bypassed.
 *
 * @param item    The superitem that opened this menu.
 * @param keyDown True when the menu was opened by a keyboard event.
 * @return true if dynamic item addition may continue, false if it should abort.
 */
bool
BMenu::_OkToProceed(BMenuItem* item, bool keyDown)
{
	BPoint where;
	uint32 buttons;
	GetMouse(&where, &buttons, false);
	bool stickyMode = _IsStickyMode();
	// Quit if user clicks the mouse button in sticky mode
	// or releases the mouse button in nonsticky mode
	// or moves the pointer over another item
	// TODO: I added the check for BMenuBar to solve a problem with Deskbar.
	// BeOS seems to do something similar. This could also be a bug in
	// Deskbar, though.
	if ((buttons != 0 && stickyMode)
		|| ((dynamic_cast<BMenuBar*>(this) == NULL
			&& (buttons == 0 && !stickyMode))
		|| ((_HitTestItems(where) != item) && !keyDown))) {
		return false;
	}

	return true;
}


/**
 * @brief Invoke the custom tracking hook and return whether it wants to quit.
 *
 * Returns false immediately if no tracking hook is installed.
 *
 * @return true if the custom hook signals that tracking should stop.
 * @see SetTrackingHook()
 */
bool
BMenu::_CustomTrackingWantsToQuit()
{
	if (fExtraMenuData != NULL && fExtraMenuData->trackingHook != NULL
		&& fExtraMenuData->trackingState != NULL) {
		return fExtraMenuData->trackingHook(this,
			fExtraMenuData->trackingState);
	}

	return false;
}


/**
 * @brief Terminate tracking and close the menu hierarchy.
 *
 * Deselects the current item and sets fState to MENU_STATE_CLOSED. When
 * @p onlyThis is false, also closes the parent menu chain, disables sticky
 * mode, and restores the system cursor. Finally calls _Hide() to remove the
 * window.
 *
 * @param onlyThis If true, close only this menu; if false, propagate the close
 *                 up through all parent menus.
 */
void
BMenu::_QuitTracking(bool onlyThis)
{
	_SelectItem(NULL);
	if (BMenuBar* menuBar = dynamic_cast<BMenuBar*>(this))
		menuBar->_RestoreFocus();

	fState = MENU_STATE_CLOSED;

	if (!onlyThis) {
		// Close the whole menu hierarchy
		if (Supermenu() != NULL)
			Supermenu()->fState = MENU_STATE_CLOSED;

		if (_IsStickyMode())
			_SetStickyMode(false);

		if (LockLooper()) {
			be_app->ShowCursor();
			UnlockLooper();
		}
	}

	_Hide();
}


//	#pragma mark - menu_info functions


// TODO: Maybe the following two methods would fit better into
// InterfaceDefs.cpp
// In R5, they do all the work client side, we let the app_server handle the
// details.

/**
 * @brief Apply new system-wide menu settings via the app_server.
 *
 * Sends the new settings to the app_server using AS_SET_MENU_INFO and, on
 * success, updates the local BMenu::sMenuInfo cache so that menus created in
 * this team immediately reflect the new appearance.
 *
 * @param info Pointer to the new menu_info structure; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if @p info is NULL, or an error code
 *         from the app_server.
 */
status_t
set_menu_info(menu_info* info)
{
	if (!info)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_MENU_INFO);
	link.Attach<menu_info>(*info);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		BMenu::sMenuInfo = *info;
		// Update also the local copy, in case anyone relies on it

	return status;
}


/**
 * @brief Retrieve the current system-wide menu settings from the app_server.
 *
 * Sends AS_GET_MENU_INFO to the server and reads the returned menu_info into
 * @p info.
 *
 * @param info Pointer to a menu_info structure to fill; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if @p info is NULL, or an error code
 *         from the app_server.
 */
status_t
get_menu_info(menu_info* info)
{
	if (!info)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_MENU_INFO);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<menu_info>(info);

	return status;
}


/**
 * @brief GCC 2 / GCC 4 binary-compatibility thunk for BMenu::InvalidateLayout().
 *
 * Provides a C linkage entry point under both the GCC 2 and GCC 4 mangled
 * names so that code compiled against older SDKs can still call
 * InvalidateLayout() on a BMenu object.
 *
 * @param menu        The BMenu instance whose layout should be invalidated.
 * @param descendants Unused; present for ABI compatibility only.
 */
extern "C" void
B_IF_GCC_2(InvalidateLayout__5BMenub,_ZN5BMenu16InvalidateLayoutEb)(
	BMenu* menu, bool descendants)
{
	menu->InvalidateLayout();
}
